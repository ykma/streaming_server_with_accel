
#include "stdafx.h"
#include <windows.h>
#if 0
#include <iostream>
#include <TCHAR.h>
#include "stitcher_stream_server.h"
#include <math.h>
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/error.h>
#include <libavutil/timestamp.h>

using namespace cv;
extern HANDLE pano_log_lock;

DWORD WINAPI encoder_thread(LPVOID lpParameter)
{
#define YUV_BUF_COUNT 15
	CAMERA_INFO_S *cis = (CAMERA_INFO_S *)lpParameter;
	OutputDebugPrintf("[Encoder]Camera %d thread started.\n", cis->camera_no);
	return 0;
}

#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS SWS_BICUBIC
// a wrapper around a single output AVStream
typedef struct OutputStream {
	AVStream *st;
	AVCodecContext *enc;
	/* pts of the next frame that will be generated */
	int64_t next_pts;
	int samples_count;
	AVFrame *frame;
	AVFrame *tmp_frame;
	float t, tincr, tincr2;
	struct SwsContext *sws_ctx;
	struct SwrContext *swr_ctx;
} OutputStream;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{/*
 AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
 printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
 av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
 av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
 av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
 pkt->stream_index);
 */
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
	/* rescale output packet timestamp values from codec to stream timebase */
	av_packet_rescale_ts(pkt, *time_base, st->time_base);
	pkt->stream_index = st->index;
	/* Write the compressed frame to the media file. */
	log_packet(fmt_ctx, pkt);
	return av_interleaved_write_frame(fmt_ctx, pkt);
}
/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
	AVCodec **codec,
	enum AVCodecID codec_id)
{
	AVCodecContext *c;
	int i;
	/* find the encoder */
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		fprintf(stderr, "Could not find encoder for '%s'\n",
			avcodec_get_name(codec_id));
		exit(1);
	}
	ost->st = avformat_new_stream(oc, NULL);
	if (!ost->st) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}
	ost->st->id = oc->nb_streams - 1;
	c = avcodec_alloc_context3(*codec);
	if (!c) {
		fprintf(stderr, "Could not alloc an encoding context\n");
		exit(1);
	}
	ost->enc = c;
	switch ((*codec)->type) {
	case AVMEDIA_TYPE_VIDEO:
		c->codec_id = AV_CODEC_ID_H264;
		c->bit_rate = 400000;
		/* Resolution must be a multiple of two. */
		c->width = 1280;
		c->height = 720;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
		* of which frame timestamps are represented. For fixed-fps content,
		* timebase should be 1/framerate and timestamp increments should be
		* identical to 1. */
		ost->st->time_base.num = 1;
		ost->st->time_base.den = STREAM_FRAME_RATE;
		c->time_base = ost->st->time_base;
		c->gop_size = 50; /* emit one intra frame every twelve frames at most */
		c->pix_fmt = STREAM_PIX_FMT;
		break;
	default:
		break;
	}
	/* Some formats want stream headers to be separate. */
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}
/**************************************************************/
/* video output */
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture;
	int ret;
	picture = av_frame_alloc();
	if (!picture)
		return NULL;
	picture->format = pix_fmt;
	picture->width = width;
	picture->height = height;
	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate frame data.\n");
		exit(1);
	}
	return picture;
}
static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
	int ret;
	AVCodecContext *c = ost->enc;
	AVDictionary *opt = NULL;
	av_dict_copy(&opt, opt_arg, 0);
	/* open the codec */
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		//fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
		exit(1);
	}
	/* allocate and init a re-usable frame */
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!ost->frame) {
		//fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	/* If the output format is not YUV420P, then a temporary YUV420P
	* picture is needed too. It is then converted to the required
	* output format. */
	ost->tmp_frame = NULL;
	if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
		ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
		if (!ost->tmp_frame) {
			fprintf(stderr, "Could not allocate temporary picture\n");
			exit(1);
		}
	}
	/* copy the stream parameters to the muxer */
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0) {
		fprintf(stderr, "Could not copy the stream parameters\n");
		exit(1);
	}
}
/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame *pict, int frame_index,
	int width, int height)
{
	int x, y, i;
	i = frame_index;
	/* Y */
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
	/* Cb and Cr */
	for (y = 0; y < height / 2; y++) {
		for (x = 0; x < width / 2; x++) {
			pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
			pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
		}
	}
}
static AVFrame *get_video_frame(OutputStream *ost)
{
	AVCodecContext *c = ost->enc;
	/* check if we want to generate more frames */
	if (av_compare_ts(ost->next_pts, c->time_base,
		STREAM_DURATION, (AVRational) { 1, 1 }) >= 0)
		return NULL;
	/* when we pass a frame to the encoder, it may keep a reference to it
	* internally; make sure we do not overwrite it here */
	if (av_frame_make_writable(ost->frame) < 0)
		exit(1);
	if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
		/* as we only generate a YUV420P picture, we must convert it
		* to the codec pixel format if needed */
		if (!ost->sws_ctx) {
			ost->sws_ctx = sws_getContext(c->width, c->height,
				AV_PIX_FMT_YUV420P,
				c->width, c->height,
				c->pix_fmt,
				SCALE_FLAGS, NULL, NULL, NULL);
			if (!ost->sws_ctx) {
				fprintf(stderr,
					"Could not initialize the conversion context\n");
				exit(1);
			}
		}
		fill_yuv_image(ost->tmp_frame, ost->next_pts, c->width, c->height);
		sws_scale(ost->sws_ctx, (const uint8_t * const *)ost->tmp_frame->data,
			ost->tmp_frame->linesize, 0, c->height, ost->frame->data,
			ost->frame->linesize);
	}
	else {
		fill_yuv_image(ost->frame, ost->next_pts, c->width, c->height);
	}
	ost->frame->pts = ost->next_pts++;
	return ost->frame;
}
/*
* encode one video frame and send it to the muxer
* return 1 when encoding is finished, 0 otherwise
*/
static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
	int ret;
	AVCodecContext *c;
	AVFrame *frame;
	int got_packet = 0;
	AVPacket pkt = { 0 };
	c = ost->enc;
	frame = get_video_frame(ost);
	av_init_packet(&pkt);
	/* encode the image */
	ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
	if (ret < 0) {
		//fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
		exit(1);
	}
	if (got_packet) {
		ret = write_frame(oc, &c->time_base, ost->st, &pkt);
	}
	else {
		ret = 0;
	}
	if (ret < 0) {
		//fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
		exit(1);
	}
	return (frame || got_packet) ? 0 : 1;
}
static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
	avcodec_free_context(&ost->enc);
	av_frame_free(&ost->frame);
	av_frame_free(&ost->tmp_frame);
	sws_freeContext(ost->sws_ctx);
	swr_free(&ost->swr_ctx);
}
/**************************************************************/
/* media file output */
DWORD WINAPI encoder_thread(LPVOID lpParameter)
{
#define YUV_BUF_COUNT 15
	CAMERA_INFO_S *cis = (CAMERA_INFO_S *)lpParameter;
	OutputStream video_st = { 0 }, audio_st = { 0 };
	const char *filename;
	AVOutputFormat *fmt;
	AVFormatContext *oc;
	AVCodec *audio_codec, *video_codec;
	int ret;
	int have_video = 0, have_audio = 0;
	int encode_video = 0, encode_audio = 0;
	AVDictionary *opt = NULL;
	int i;
	if (argc < 2) {
		printf("usage: %s output_file\n"
			"API example program to output a media file with libavformat.\n"
			"This program generates a synthetic audio and video stream, encodes and\n"
			"muxes them into a file named output_file.\n"
			"The output format is automatically guessed according to the file extension.\n"
			"Raw images can also be output by using '%%d' in the filename.\n"
			"\n", argv[0]);
		return 1;
	}
	filename = argv[1];
	for (i = 2; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
			av_dict_set(&opt, argv[i] + 1, argv[i + 1], 0);
	}
	/* allocate the output media context */
	avformat_alloc_output_context2(&oc, NULL, NULL, filename);
	if (!oc) {
		printf("Could not deduce output format from file extension: using MPEG.\n");
		avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
	}
	if (!oc)
		return 1;
	fmt = oc->oformat;
	/* Add the audio and video streams using the default format codecs
	* and initialize the codecs. */
	if (fmt->video_codec != AV_CODEC_ID_NONE) {
		add_stream(&video_st, oc, &video_codec, fmt->video_codec);
		have_video = 1;
		encode_video = 1;
	}
	/* Now that all the parameters are set, we can open the audio and
	* video codecs and allocate the necessary encode buffers. */
	if (have_video)
		open_video(oc, video_codec, &video_st, opt);
	av_dump_format(oc, 0, filename, 1);
	/* open the output file, if needed */
	if (!(fmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
		if (ret < 0) {
			//fprintf(stderr, "Could not open '%s': %s\n", filename, av_err2str(ret));
			return 1;
		}
	}
	/* Write the stream header, if any. */
	ret = avformat_write_header(oc, &opt);
	if (ret < 0) {
		//fprintf(stderr, "Error occurred when opening output file: %s\n", av_err2str(ret));
		return 1;
	}
	while (encode_video) {
		/* select the stream to encode */
		if (encode_video &&
			(!encode_audio || av_compare_ts(video_st.next_pts, video_st.enc->time_base,
				audio_st.next_pts, audio_st.enc->time_base) <= 0)) {
			encode_video = !write_video_frame(oc, &video_st);
		}
	}
	/* Write the trailer, if any. The trailer must be written before you
	* close the CodecContexts open when you wrote the header; otherwise
	* av_write_trailer() may try to use memory that was freed on
	* av_codec_close(). */
	av_write_trailer(oc);
	/* Close each codec. */
	if (have_video)
		close_stream(oc, &video_st);
	if (!(fmt->flags & AVFMT_NOFILE))
		/* Close the output file. */
		avio_closep(&oc->pb);
	/* free the stream */
	avformat_free_context(oc);
	return 0;
}

#endif