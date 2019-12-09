#include "stdafx.h"
#include <iostream>
#include <string.h>
#include "stitcher_stream_server.h"
#include "stitching_cl.h"
using namespace std;

#define PIPE_RATE (1.0/15)

DWORD WINAPI preview_thread(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	unsigned long long stitch2pip_cnt = 0;
	unsigned long WriteNum;
	int offset;
	cv::Mat rgbImg, std_rgbImg;
	cv::Mat yuvImg;
	int height=si->output_height;
	int width=si->output_width;
	yuvImg.create(height * 3 / 2, width, CV_8UC1);
retry:
	while (si->stitch_real_frames_cnt<2)
	{
		Sleep(300);
	}
	while (si->is_starting == 2)
	{
		
		offset = si->stitch_real_frames_cnt%YUV_BUF_COUNT;
		memcpy(yuvImg.data, si->yuv_stitch_buf[offset], si->output_width * si->output_height * 3 / 2);
		cv::cvtColor(yuvImg, rgbImg, CV_YUV2BGR_I420);

		resize(rgbImg, std_rgbImg, Size(1600, 800));
		cv::imshow("预览", std_rgbImg);
		waitKey(200);
	}

	Sleep(5);
	goto retry;
	return 0;
}

DWORD WINAPI write_to_pipe_encode(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	//unsigned long long stitch2pip_cnt = 0;
	unsigned long WriteNum;
	int offset;
retry:
	WCHAR wszClassName[512];
	memset(wszClassName, 0, sizeof(wszClassName));
	MultiByteToWideChar(CP_ACP, 0, si->oipstr, strlen(si->oipstr) + 1, wszClassName, sizeof(wszClassName) / sizeof(wszClassName[0]));
	printf("##output to # %ls #\n", wszClassName);
	HANDLE hPipe = CreateNamedPipe(wszClassName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 10 * 1024 * 1024, 10 * 1024 * 1024, 0, NULL);
	if (hPipe == INVALID_HANDLE_VALUE)
	{
		printf("create pipe encode failed\n");
		CloseHandle(hPipe);
		return 0;
	}
	if (ConnectNamedPipe(hPipe, NULL) == FALSE)
	{
		printf("failed to connect client encode pipe\n");
		CloseHandle(hPipe);
		Sleep(5);
		goto retry;
	}
	printf("success to connect cliet encode pipe\n");
	si->pipe_ok = 1;
	while (si->sync_send_ok != 1)
	{
		Sleep(40);
		continue;
	}
	offset = si->stitch_real_frames_cnt;
	while (si->sync_send_ok == 1)
	{
		if ((offset + 4) > si->stitch_real_frames_cnt)
		{
			Sleep(20);
			continue;
		}
		if (WriteFile(hPipe, si->yuv_stitch_buf[offset % YUV_BUF_COUNT], si->output_width * si->output_height * 3 / 2, &WriteNum, NULL) == FALSE)
		{
			printf("failed to write to encode pipe\n");
			break;
		}
		if (WriteNum != si->output_width * si->output_height * 3 / 2)
		{
			printf("Length of writed to pipe bytes is wrong\n");
		}
		offset++;
		Sleep(20);
	}
	CloseHandle(hPipe);

	Sleep(5);
	goto retry;
	return 0;
}
int get_last_AVFrame_bigframe(LPVOID lpParameter, int &offset, int &camnum)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	int process_offset = 0;
	int i;
	if (si->sync_send_ok !=1)
		return -1;
	process_offset = si->stitch_ready_lastone_index % YUV_BUF_COUNT;
	offset=process_offset;
	camnum = si->camnum;
	return 0;
}
int get_bigframe_from_AVFrame(LPVOID lpParameter, int offset, unsigned char *frame_buf, int camnum, int &width, int &height)
{
	int i;
	int linesize;
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	FILE *err_log;
	unsigned char *src, *image,*image1;
	int m,n;
	width = si->camera_width;
	height = si->camera_height;
	SYSTEMTIME st;
	printf("get_bigframe_from_AVFrame: camnum %d width %d height %d\n", camnum, width, height);
	
	for (i = 0; i < camnum; i++)
	{
		if (si->is_hwdec[i] == 1)
		{
			printf("%d ", i);
			image = frame_buf + i*width*height * 3 / 2;
			linesize = si->dts[i].yuv_frame[offset]->linesize[0];
			src = si->dts[i].yuv_frame[offset]->data[0];
			if (src == NULL)
			{
				GetLocalTime(&st);
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log, "\get_bigframe_from_AVFrame failed @ %d-%02d-%02d %02d:%02d:%02d:%03d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
				fclose(err_log);
				return -1;
			}
			if (src == NULL)
			{
				continue;
			}
			if (linesize != width)
			{
				for (m = 0; m < height; m++)
				{
					memcpy(image, src, width);
					src += linesize;
					image += width;
				}
			}
			else
			{
				memcpy(image, src, width*height);
				image += width*height;
			}
			linesize = si->dts[i].yuv_frame[offset]->linesize[1];
			src = si->dts[i].yuv_frame[offset]->data[1];
			image1 = image + width*height / 4;
			if (src == NULL)
			{
				continue;
			}
			for (m = 0; m < height / 2; m++)
			{
				for (n = 0; n < width / 2; n++)
				{
					image[n] = src[n * 2];
					image1[n] = src[n * 2 + 1];
				}
				image += width / 2;
				image1 += width / 2;
				src += linesize;
			}

		}
		else
		{

			printf("%d ", i);
			image = frame_buf + i*width*height * 3 / 2;
			linesize = si->dts[i].yuv_frame[offset]->linesize[0];
			src = si->dts[i].yuv_frame[offset]->data[0];
			if (src == NULL)
			{
				GetLocalTime(&st);
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log, "\get_bigframe_from_AVFrame failed @ %d-%02d-%02d %02d:%02d:%02d:%03d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
				fclose(err_log);
				return -1;
			}
			if (src == NULL)
			{
				continue;
			}
			if (linesize != width)
			{
				for (m = 0; m < height; m++)
				{
					memcpy(image, src, width);
					src += linesize;
					image += width;
				}
			}
			else
			{
				memcpy(image, src, width*height);
				image += width*height;
			}
			linesize = si->dts[i].yuv_frame[offset]->linesize[1];
			src = si->dts[i].yuv_frame[offset]->data[1];
			if (src == NULL)
			{
				continue;
			}
			if (linesize != (width / 2))
			{
				for (m = 0; m < height / 2; m++)
				{
					memcpy(image, src, width / 2);
					src += linesize;
					image += width / 2;
				}
			}
			else
			{
				memcpy(image, src, width*height / 4);
				image += width*height / 4;
			}
			linesize = si->dts[i].yuv_frame[offset]->linesize[2];
			src = si->dts[i].yuv_frame[offset]->data[2];
			if (src == NULL)
			{
				continue;
			}
			if (linesize != (width / 2))
			{
				for (m = 0; m < height / 2; m++)
				{
					memcpy(image, src, width / 2);
					src += linesize;
					image += width / 2;
				}
			}
			else
			{
				memcpy(image, src, width*height / 4);
				image += width*height / 4;
			}
		}
	}
	printf("\n");
	return 0;
}
typedef struct
{
	int header;
	int cam_no;
	int width;
	int height;
}PIPE_IMAGE_HEADER;
DWORD WINAPI write_to_pipe_analyse(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	unsigned long long stitch2pip_cnt = 0;
	unsigned long WriteNum;
	int offset;
	int i;
	int m, linesize;
	int lastno;
	DWORD send_bytes;
	DWORD rest_bytes;
	DWORD send_offset;
	lastno = si->camnum - 1;
	unsigned char *dst, *src;
	int buf_size = si->camnum*si->camera_width*si->camera_height * 3 / 2;
	char *buf = (char *)malloc(buf_size);
	char *image = buf;
	AVFrame *avframe;
	while (si->sync_send_ok !=1)
	{
		Sleep(1000);
	}
retry:
	WCHAR wszClassName[512];
	memset(wszClassName, 0, sizeof(wszClassName));
	MultiByteToWideChar(CP_ACP, 0, si->oipstr_analyse, strlen(si->oipstr_analyse) + 1, wszClassName, sizeof(wszClassName) / sizeof(wszClassName[0]));
	printf("@@oipstr_analyse output to  @ %ls @\n", wszClassName);
	HANDLE hPipe = CreateNamedPipe(wszClassName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, buf_size + sizeof(PIPE_IMAGE_HEADER), buf_size + sizeof(PIPE_IMAGE_HEADER), 0, NULL);
	if (hPipe == INVALID_HANDLE_VALUE)
	{
		printf("create analyse pipe failed, %d\n", GetLastError());
		CloseHandle(hPipe);
		goto retry;
	}
	if (ConnectNamedPipe(hPipe, NULL) == FALSE)
	{
		printf("failed to connect client analyse pipe\n");
		CloseHandle(hPipe);
		Sleep(5);
		goto retry;
	}
	printf("success to connect cliet analyse pipe\n");
	int process_offset = 0;
	PIPE_IMAGE_HEADER *pih;
	pih = (PIPE_IMAGE_HEADER *)malloc(sizeof(PIPE_IMAGE_HEADER));
	pih->width = si->dts[lastno].width;
	pih->height = si->dts[lastno].height;
	pih->header = 0xff5aa533;
	pih->cam_no = si->camnum;
	int camera_num, camera_width, camera_height;
	int yuv_offset;
	while (si->is_starting == 2)
	{
		if (get_last_AVFrame_bigframe(lpParameter, yuv_offset, camera_num) == -1)
		{
			Sleep(3000);
			continue;
		}
		printf("prepare to send yuv_offset %llu send to analyze.\n", yuv_offset);
		if (get_bigframe_from_AVFrame(lpParameter, yuv_offset, (unsigned char *)buf, camera_num, camera_width, camera_height) == -1)
		{
			Sleep(3000);
			continue;
		}
		printf("ready to yuv_offset %llu send to analyze.\n", yuv_offset);
		if (WriteFile(hPipe, pih, sizeof(PIPE_IMAGE_HEADER), &WriteNum, NULL) == FALSE)
		{
			printf("failed to write to pipe\n");
			break;
		}
		if (WriteNum != sizeof(PIPE_IMAGE_HEADER))
		{
			printf("Length of head writed to pipe bytes is wrong(%d)\n", WriteNum);
		}
		rest_bytes = buf_size;
		send_offset = 0;
		int send_byes_once = 0;
		while (rest_bytes > 0)
		{
			send_byes_once = (rest_bytes > buf_size) ? (buf_size) : rest_bytes;

			if (WriteFile(hPipe, buf + send_offset, send_byes_once, &send_bytes, NULL) == FALSE)
			{
				printf("failed to write to pipe,%d\n", GetLastError());
				break;
			}
			printf("send bytes=%d\n", send_bytes);
			if (WriteNum <0)
			{
				printf("Length of writed to pipe bytes is wrong, %d\n", GetLastError());
				continue;
			}
			send_offset = +send_bytes;
			rest_bytes -= send_bytes;
		}
		Sleep(si->analyse_interval_ms);
	}
	CloseHandle(hPipe);

	Sleep(5);
	goto retry;
	return 0;
}
int get_frame2stitch(uint64 &decoded_frame_index, uint64 &output_buf_index)
{
	SERVER_INFO *si = get_server_info(NULL);
	static uint64 stitch_offset;
	static uint64 pipe_buf_index;
	int ret=0;
	if (stitch_offset == 0)
		stitch_offset = si->start_stitch_offset+1;
	WaitForSingleObject(si->hstitch2pipe_mutex, 10000);
	if (si->decoded_frame_tag[stitch_offset%YUV_BUF_COUNT] != si->decoded_mask)
	{
		if (si->decoded_frame_tag[(stitch_offset + 1) % YUV_BUF_COUNT] != si->decoded_mask)
		{
			ret = -1;
			goto get_frame2stitch_END;
		}
		else
		{
			stitch_offset++;
		}
	}
	decoded_frame_index = ((stitch_offset)%YUV_BUF_COUNT);
	output_buf_index = (pipe_buf_index) % YUV_BUF_COUNT;
	si->stitch_ready_lastone_index = (stitch_offset);
	stitch_offset++;
	pipe_buf_index++;
	si->stitch_real_frames_cnt++;
get_frame2stitch_END:
	ReleaseMutex(si->hstitch2pipe_mutex);

	return ret;
}
DWORD WINAPI stitch_update(LPVOID lpParameter)
{
	unsigned long long old_tick = 0, tick = 0;
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	int i;
	si->stitch_real_frames_cnt = 0;
	int lastno = si->camnum - 1;
	int width, height;
	int owidth;
	int oheight;
	int mapWidth = 0;
	int mapHeight = 0;
	uchar* canvas;
	uchar* uImage;
	uchar* vImage;
	ocl_args_d_t ocl;
	uint64 process_offset;
	uint64 output_buf_index;
	while (si->sync_send_ok!=1)
	{
		Sleep(50);
		continue;
	}
	
	int cut = 0;
	owidth = si->output_width;
	oheight = si->output_height;
	cl_float* mapx = new cl_float[si->output_width * si->output_height * si->camnum];
	cl_float* mapy = new cl_float[si->output_width * si->output_height * si->camnum];
	cl_float* weight = new cl_float[si->output_width * si->output_height * si->camnum];
	cl_int optimizedSizeY = ((sizeof(uchar) * si->camera_height*si->camera_width * si->camnum - 1) / 64 + 1) * 64;
	cl_int optimizedSizeUV = ((sizeof(uchar) * si->camera_height * si->camera_width *si->camnum /4- 1) / 64 + 1) * 64;

	canvas = (uchar*)_aligned_malloc(optimizedSizeY, 4096);
	uImage = (uchar*)_aligned_malloc(optimizedSizeUV, 4096);
	vImage = (uchar*)_aligned_malloc(optimizedSizeUV, 4096);
	printf("Init CL with camera_width=%d camera_height=%d render_str=%s camnum=%d\n", si->camera_width, si->camera_height, si->render_str, si->camnum);
	init(&ocl, si->camera_height, si->camera_width, 0, si->render_str, si->camnum, stitching_cl_str, stitching_cl_str_len);
	generate_mapping_data(si->datafilename, mapx, mapy, weight, owidth, oheight, mapWidth, mapHeight, cut);
	oclMappingData(&ocl, mapx, mapy, weight, owidth, oheight, mapWidth, mapHeight, cut);
	

	printf("start to stitch.\n");
	size_t global_size[2] = { oheight , owidth };
	if (cut==2)
	{
		printf("change output size to %d x %d\n", mapWidth, mapHeight);
		global_size[0]=si->output_height = mapHeight;
	}
	size_t origin_input[3] = { 0,0,0 };
	size_t origin_input2[3] = { 0,0,0 }; // Offset within the image to copy from
	size_t region_input[3] = { si->camera_width * si->camnum, si->camera_height , 1 }; // Elements to per dimension
	size_t region_input2[3] = { si->camera_width * si->camnum / 2, si->camera_height * 1 / 2, 1 }; // Elements to per dimension
	// 代表输出图像内存
	uchar *outBuffer;
	// 初始化结束 进入主循环
	time_t tt = time(NULL);
	tm t;
	localtime_s(&t, &tt);
	if ((t.tm_year + 1910) > 2030)
	{
		return 0;
	}
	if ((t.tm_year + 1920) == 2040 && t.tm_mon + 20>30)
	{
		return 0;
	}
	while (si->sync_send_ok != 1)
	{
		Sleep(10);
	}
	while (si->sync_send_ok == 1)
	{
		if (get_frame2stitch(process_offset, output_buf_index) == -1)
		{
			Sleep(5);
			continue;
		}
		si->decoded_frame_tag[process_offset] = 0;
		//printf("process %llu.\n", process_offset);
		if (process_offset!=-1)
		{
			width = si->camera_width;
			height = si->camera_height;
			
			int m, linesize;
			unsigned char *dst,*dst1, *src;
			
			for (i = 0; i < si->camnum; i++)
			{
				if (si->is_hwdec[i] != 1)
				{
					dst = (unsigned char *)canvas + (width*i);
					linesize = si->dts[i].yuv_frame[process_offset]->linesize[0];
					src = si->dts[i].yuv_frame[process_offset]->data[0];
					if (src == NULL)
					{
						continue;
					}
					if (linesize != width)
					{
						for (m = 0; m < height; m++)
						{
							memcpy(dst, src, width);
							src += linesize;
							dst += width*si->camnum;
						}
					}
					else
					{
						memcpy(dst, src, width*height);
					}
					dst = (unsigned char *)uImage + (width*i / 2);
					linesize = si->dts[i].yuv_frame[process_offset]->linesize[1];
					src = si->dts[i].yuv_frame[process_offset]->data[1];
					if (src == NULL)
					{
						continue;
					}
					if (linesize != (width / 2))
					{
						for (m = 0; m < height / 2; m++)
						{
							memcpy(dst, src, width / 2);
							src += linesize;
							dst += width*si->camnum / 2;
						}
					}
					else
					{
						memcpy(dst, src, height*width / 4);
					}
					dst = (unsigned char *)vImage + (width*i / 2);
					linesize = si->dts[i].yuv_frame[process_offset]->linesize[2];
					src = si->dts[i].yuv_frame[process_offset]->data[2];
					if (src == NULL)
					{
						continue;
					}
					if (linesize != (width / 2))
					{
						for (m = 0; m < height / 2; m++)
						{
							memcpy(dst, src, width / 2);
							src += linesize;
							dst += width *si->camnum / 2;
						}
					}
					else
					{
						memcpy(dst, src, height*width / 4);
					}
				}
				else
				{

					dst = (unsigned char *)canvas + (width*i);
					linesize = si->dts[i].yuv_frame[process_offset]->linesize[0];
					src = si->dts[i].yuv_frame[process_offset]->data[0];
					if (src == NULL)
					{
						continue;
					}
					if (linesize != width)
					{
						for (m = 0; m < height; m++)
						{
							memcpy(dst, src, width);
							src += linesize;
							dst += width*si->camnum;
						}
					}
					else
					{
						memcpy(dst, src, width*height);
					}
					dst = (unsigned char *)uImage + (width*i / 2);
					dst1 = (unsigned char *)vImage + (width*i / 2);
					linesize = si->dts[i].yuv_frame[process_offset]->linesize[1];
					src = si->dts[i].yuv_frame[process_offset]->data[1];
					if (src == NULL)
					{
						continue;
					}
					int n;
					for (m = 0; m < height / 2; m++)
					{
						for (n = 0; n < width / 2; n++)
						{
							dst[n] = src[n * 2];
							dst1[n] = src[n * 2 + 1];
						}
						dst += width*si->camnum / 2;
						dst1 += width*si->camnum / 2;
						src += linesize;
					}
				}
			}
			clEnqueueWriteImage(ocl.queue, ocl.yImage, CL_TRUE,
				origin_input, region_input, 0 /* row-pitch */, 0 /* slice-pitch */, (void*)canvas, 0, NULL, NULL
			);
			clEnqueueWriteImage(ocl.queue, ocl.uImage, CL_TRUE,
				origin_input2, region_input2, 0 /* row-pitch */, 0 /* slice-pitch */, (void*)uImage, 0, NULL, NULL
			);
			clEnqueueWriteImage(ocl.queue, ocl.vImage, CL_TRUE,
				origin_input2, region_input2, 0 /* row-pitch */, 0 /* slice-pitch */, (void*)vImage, 0, NULL, NULL
			);
			ocl.err = clFinish(ocl.queue);
			if (ocl.err < 0)
			{
				FILE *err_log;
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log,"couldn't finish1 input. errno=%llu\n",GetLastError());
				fclose(err_log);
				exit(1);
			}

			ocl.err = clEnqueueNDRangeKernel(ocl.queue, ocl.kernel, 2, NULL, global_size,
				NULL, 0, NULL, NULL);
			if (ocl.err < 0) {
				FILE *err_log;
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log, "Couldn't enqueue the kernel. errno=%llu\n", GetLastError());
				fclose(err_log);
				exit(1);
			}
			ocl.err = clFinish(ocl.queue);
			if (ocl.err < 0)
			{
				FILE *err_log;
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log, "couldn't finish output. errno=%llu\n", GetLastError());
				fclose(err_log);
				exit(1);
			}
			if (cut == 2) 
			{
				outBuffer = (uchar *)clEnqueueMapBuffer(ocl.queue, ocl.output, true, CL_MAP_READ, 0,
					sizeof(uchar) * mapHeight*si->output_width * 3 / 2, 0, NULL, NULL, &ocl.err);
			}
			else
			{
				outBuffer = (uchar *)clEnqueueMapBuffer(ocl.queue, ocl.output, true, CL_MAP_READ, 0,
					sizeof(uchar) * si->output_height*si->output_width * 3 / 2, 0, NULL, NULL, &ocl.err);
			}
			if (ocl.err < 0) {
				FILE *err_log;
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log, "Couldn't read output buffer. errno=%llu\n", GetLastError());
				fclose(err_log);
				exit(1);
			}

			memcpy(si->yuv_stitch_buf[output_buf_index], outBuffer, si->output_height*si->output_width * 3 / 2);
			ocl.err = clEnqueueUnmapMemObject(ocl.queue, ocl.output, outBuffer, 0, NULL, NULL);
			if (ocl.err < 0) {
				FILE *err_log;
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log, "Couldn't clEnqueueUnmapMemObject. errno=%llu\n", GetLastError());
				fclose(err_log);
				exit(1);
			}
		}
		else
			Sleep(30);
	}
	return 0;
}
static int hw_decoder_init(AVBufferRef *hw_device_ctx, AVCodecContext *ctx, const enum AVHWDeviceType type)
{
	int err = 0;

	if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,NULL, NULL, 0)) < 0) {
		fprintf(stderr, "Failed to create specified HW device.\n");
		return err;
	}
	else
	{
		fprintf(stderr, "HW decoder init.\n");
	}
	ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

	return err;
}
static enum AVPixelFormat hw_pix_fmt;
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
	const enum AVPixelFormat *pix_fmts)
{
	const enum AVPixelFormat *p;

	for (p = pix_fmts; *p != -1; p++) {
		if (*p == hw_pix_fmt)
			return *p;
	}

	fprintf(stderr, "Failed to get HW surface format.\n");
	return AV_PIX_FMT_NONE;
}

DWORD WINAPI decoder_recv_thread(LPVOID lpParameter)
{
	SERVER_INFO *si = get_server_info(NULL);
	uint64 decode_no = (uint64)lpParameter;
	unsigned char channel_mask = 1 << decode_no;
	int ret=0;
	unsigned int stamp_offset=0;
	AVFrame *wFrame, *tmpFrame=NULL;
	wFrame = av_frame_alloc();
	printf("Decoder channel %d initialised.\n", decode_no);
	while (si->sync_send_ok != 1)
	{
		Sleep(10);
	}
	printf("Decoder channel %d started.\n", decode_no);
	si->dts[decode_no].nRecvFrame = 0;
	while (si->sync_send_ok == 1)
	{
		WaitForSingleObject(si->dts[decode_no].hdecode_mutex, 10000);
		si->dts[decode_no].dec_cxt->strict_std_compliance = 1;
		si->dts[decode_no].dec_cxt->has_b_frames = 1;
		ret = avcodec_receive_frame(si->dts[decode_no].dec_cxt, wFrame);
		ReleaseMutex(si->dts[decode_no].hdecode_mutex);
		if (ret == AVERROR(EAGAIN))
		{
			Sleep(5);
			continue;
		}
		if (ret == AVERROR_EOF)
		{
			continue;
		}
		else if (ret < 0)
		{
			continue;
		}
		stamp_offset = wFrame->pts%YUV_BUF_COUNT;
		if (si->is_hwdec[decode_no] == 1)
		{
			int errret = 0;
			if ((errret = av_hwframe_transfer_data(si->dts[decode_no].yuv_frame[stamp_offset], wFrame, 0)) < 0) {
				FILE *err_log;
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log, "[CLEAN]Error transferring the data to system memory in detection. errinfo=%d\n", errret);
				fclose(err_log);
				//av_frame_free(&hwFrame);
				continue;
			}
			//av_frame_free(&hwFrame);
		}
		else
		{
			tmpFrame = wFrame;
			wFrame=si->dts[decode_no].yuv_frame[stamp_offset];
			si->dts[decode_no].yuv_frame[stamp_offset] = tmpFrame;
		}
		//printf("channel %d before mask 0x%x\n", decode_no, si->decoded_frame_tag[stamp_offset]);
		si->decoded_frame_tag[stamp_offset] |= channel_mask;
		si->dts[decode_no].nRecvFrame++;
		//printf("channel %d after mask 0x%x\n", decode_no, si->decoded_frame_tag[stamp_offset]);
		//if(si->decoded_frame_tag[stamp_offset]==si->decoded_mask)
			//printf("recv a decoded frame offset %llu @ channel %d mask 0x%x\n", tmpFrame->pts, decode_no, si->decoded_frame_tag[stamp_offset]);
	}
	return 0;
}
DWORD WINAPI decoder_send_thread(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	si->sync_send_ok = 0;
	AVDictionary *opts = NULL;
	
	int lastno= si->camnum-1;
	int initd = 0;
	int i, j;
	unsigned char has_keyframe = 0;
	unsigned char has_decoded = 0;
	unsigned char failed_decoded = 0;
	enum AVHWDeviceType type;
	si->hstitch2pipe_mutex = CreateMutex(nullptr, FALSE, nullptr);
	for (i = 0; i < si->camnum; i++)
	{
		si->dts[i].hdecode_mutex= CreateMutex(nullptr, FALSE, nullptr);
		si->dts[i].drop_frame = NULL;
		for (j = 0; j < YUV_BUF_COUNT; j++)
			si->dts[i].yuv_frame[j] = NULL;
	}
	int ret;
	ULONGLONG tick, old_tick;
	AVFrame *hwFrame=NULL;
	printf("decoder start\n");
decoder_thread_restart:
	AVCodec *codec_sw, *codec_hw;
	codec_hw=codec_sw = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!codec_sw) {
		printf("Codec not found\n");
		return -1;
	}
	if ((si->is_hwdec[0] + si->is_hwdec[1] + si->is_hwdec[2] + si->is_hwdec[3] + si->is_hwdec[4] + si->is_hwdec[5] + si->is_hwdec[6] + si->is_hwdec[7])> 0)
	{
		char hw_name[16]="asd";
		type == AV_HWDEVICE_TYPE_NONE;
		if (!strcmp(si->render_str, "Intel"))
		{
			strcpy(hw_name, "qsv");
		}else if(!strcmp(si->render_str, "NVIDIA"))
		{
			//strcpy(hw_name, "cuda");
			strcpy(hw_name, "cuda");
		}

		type = av_hwdevice_find_type_by_name(hw_name);
		if (type == AV_HWDEVICE_TYPE_NONE) {
			fprintf(stderr, "Device type %s is not supported.\n", hw_name);
			fprintf(stderr, "Available device types:");
			while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
				fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
			fprintf(stderr, "\n");
			return -1;
		}
		for (i = 0;; i++) {
			const AVCodecHWConfig *config = avcodec_get_hw_config(codec_hw, i);
			if (!config) {
				fprintf(stderr, "Decoder %s does not support device type %s.\n",
					codec_hw->name, av_hwdevice_get_type_name(type));
				return -1;
			}
			if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
				config->device_type == type) {
				hw_pix_fmt = config->pix_fmt;
				break;
			}
		}
	}
	
	while (si->is_starting != 2)
	{
		Sleep(2000);
	}
	si->decoded_mask = 0;
	for (i = 0; i < si->camnum; i++)
	{
		si->decoded_mask = (si->decoded_mask << 1) | 1;
	}
	has_keyframe = 0;
	lastno = si->camnum - 1;
	for (i = 0; i < si->camnum; i++)
	{
		si->dts[i].dec_cxt = avcodec_alloc_context3(si->dts[i].codec);
		if (!si->dts[i].dec_cxt) {
			goto decoder_thread_restart;
		}
		//if (cis->camera_no == 0)
		{
			//cis->dts[i].codec->capabilities = (cis->dts[i].codec->capabilities)|( AV_CODEC_CAP_AUTO_THREADS);
			//si->dts[i].codec->capabilities = (si->dts[i].codec->capabilities) & (~AV_CODEC_CAP_DELAY);
			si->dts[i].dec_cxt->thread_count = 1;
		}
		
		if (si->is_hwdec[i] == 1)
		{
			si->dts[i].codec = codec_hw;
			si->dts[i].dec_cxt->get_format = get_hw_format;
			if (hw_decoder_init(si->dts[i].hw_device_ctx, si->dts[i].dec_cxt, type) < 0)
				return -1;
		}
		else
		{
			si->dts[i].codec = codec_sw;
		}
		//av_dict_set(&opts, "strict", "normal", 0);
		if (avcodec_open2(si->dts[i].dec_cxt, si->dts[i].codec, &opts) < 0) {
			printf("[Decoder]Could not open codec\n");
			goto decoder_thread_restart;
		}
		//printf("struct=%d\n", si->dts[i].dec_cxt->strict_std_compliance);
		si->dts[i].packet = av_packet_alloc();
		if (!si->dts[i].packet)
			goto decoder_thread_restart;

	}
	for (i = 0; i < si->camnum; i++)
	{
		for (j = 0; j < YUV_BUF_COUNT; j++)
		{
			if (si->dts[i].yuv_frame[j] != NULL)
			{
				av_frame_free(&si->dts[i].yuv_frame[j]);
				si->dts[i].yuv_frame[j] = NULL;
			}
			si->dts[i].yuv_frame[j] = av_frame_alloc();
		}
	}
	if ((si->is_hwdec[0] + si->is_hwdec[1] + si->is_hwdec[2] + si->is_hwdec[3] + si->is_hwdec[4] + si->is_hwdec[5] + si->is_hwdec[6] + si->is_hwdec[7])> 0)
	{
		if (hwFrame != NULL)
		{
			av_frame_free(&hwFrame);
			hwFrame = NULL;
		}
		hwFrame = av_frame_alloc();
	}
	
	printf("Seeking sync frame...\n");
	for (i = 0; i < si->camnum; i++)
	{
		si->dts[i].yuvbuf_offset = 0;
	}
	while (has_keyframe != si->decoded_mask && si->is_starting == 2)
	{
		if ((si->dts[lastno].input_offset - si->dts[lastno].output_offset) < 1)
		{
			Sleep(10);
			continue;
		}

		for (i = 0; i < si->camnum; i++)
		{
			si->dts[i].frame_ptr = si->dts[i].stream_frame[(si->dts[i].output_offset) % 256];
			if ((has_keyframe&(1 << i)) == 0 && (si->dts[i].frame_ptr[4] - 0x67) != 0)
			{
				si->dts[i].output_offset++;
				continue;
			}
			
			si->dts[i].packet->data = si->dts[i].frame_ptr;
			si->dts[i].packet->size = si->dts[i].stream_frame_len[(si->dts[i].output_offset) % 256];
			//printf("si->dts[i].dec_cxt=0x%llx si->dts[i].packet=0x%llx\n", si->dts[i].dec_cxt, si->dts[i].packet);
			
			ret = avcodec_send_packet(si->dts[i].dec_cxt, si->dts[i].packet);
			if (ret < 0)
			{
				si->dts[i].output_offset++;
				continue;
			}
			ret = AVERROR(EAGAIN);
			while (ret == AVERROR(EAGAIN))
			{
				if (si->is_hwdec[i] == 1)
				{
					ret = avcodec_receive_frame(si->dts[i].dec_cxt, hwFrame);
					if ((ret = av_hwframe_transfer_data(si->dts[i].yuv_frame[si->dts[i].yuvbuf_offset], hwFrame, 0)) < 0) {
						FILE *err_log;
						char err_str[256];
						av_strerror(AVERROR(ret), err_str, 256);
						fopen_s(&err_log, "err.log", "a+");
						fprintf(err_log, "[DECODE]Error transferring the data to system memory in detection. errinfo=%s\n", err_str);
						fclose(err_log);
						//av_frame_free(&hwFrame);
						break;
					}
					//av_frame_free(&hwFrame);
				}
				else
				{
					ret = avcodec_receive_frame(si->dts[i].dec_cxt, si->dts[i].yuv_frame[si->dts[i].yuvbuf_offset]);
				}
			}
			if (ret < 0)
			{
				si->dts[i].output_offset++;
				continue;
			}
			if ((has_keyframe&(1 << i)) == 0)
			{
				si->dts[i].width = si->dts[i].yuv_frame[si->dts[i].yuvbuf_offset]->width;
				si->dts[i].height = si->dts[i].yuv_frame[si->dts[i].yuvbuf_offset]->height;
			}
			si->dts[i].output_offset++;
			has_keyframe |= (1 << i);
		}
	}
	for (i = 0; i < si->camnum; i++)
	{
		si->dts[i].dec_cxt->strict_std_compliance = 1;
	}
	printf("raw video is %d x %d\n", si->dts[lastno].width, si->dts[lastno].height);
	si->start_stitch_offset = si->dts[lastno].output_offset;
	si->sync_send_ok = 1;
	old_tick = tick = ::GetTickCount64();
	printf("start to decode .\n");
	si->camera_height = si->dts[lastno].height;
	si->camera_width = si->dts[lastno].width;
	uint64 send_seq_id = 0;
	int seq_id_err = 0;
	while (si->is_starting == 2)
	{
		//printf("send frame offset=%d\n", si->dts[lastno].output_offset%YUV_BUF_COUNT);
		for (i = 0; i < si->camnum; i++)
		{
			if ((si->dts[i].input_offset - si->dts[i].output_offset) < 2)
			{
				Sleep(1);
				continue;
			}
			si->decoded_frame_tag[si->dts[i].output_offset] = 0;
			si->dts[i].frame_ptr = si->dts[i].stream_frame[(si->dts[i].output_offset) % 256];
			si->dts[i].packet->data = si->dts[i].frame_ptr;
			si->dts[i].packet->size = si->dts[i].stream_frame_len[(si->dts[i].output_offset) % 256];
			si->dts[i].packet->pts = (si->dts[i].output_offset);
			
			WaitForSingleObject(si->dts[i].hdecode_mutex, 10000);
			ret = avcodec_send_packet(si->dts[i].dec_cxt, si->dts[i].packet);
			ReleaseMutex(si->dts[i].hdecode_mutex);
			si->dts[i].output_offset++;
			if (ret < 0) {
				printf("exit from send_frame.ret=%d\n",ret);
				exit(1);
			}
			
		}
		si->nSendFrame++;		
	}
	for (i = 0; i < si->camnum; i++)
	{
		if (si->dts[i].packet != NULL)
		{
			av_packet_free(&si->dts[i].packet);
			si->dts[i].packet = 0;
		}
		if (si->dts[i].dec_cxt != NULL)
		{
			avcodec_free_context(&si->dts[i].dec_cxt);
			si->dts[i].dec_cxt = 0;
		}
	}
	Sleep(1000);
	goto decoder_thread_restart;

ENDDECODER:
	for (i = 0; i < si->camnum; i++)
	{
		//avfreep(&packet[i]);
		if (si->dts[i].packet != NULL)
		{
			av_packet_free(&si->dts[i].packet);
			si->dts[i].packet = 0;
		}
		if (si->dts[i].dec_cxt != NULL)
		{
			avcodec_free_context(&si->dts[i].dec_cxt);
			si->dts[i].dec_cxt = 0;
		}
		if(si->dts[i].dec_cxt->hw_device_ctx!=NULL)
			av_buffer_unref(&si->dts[i].dec_cxt->hw_device_ctx);
	}
	for (i = 0; i < si->camnum; i++)
	{
		for (j = 0; j < YUV_BUF_COUNT; j++)
		{
			if (si->dts[i].yuv_frame[j] != NULL)
			{
				av_frame_free(&si->dts[i].yuv_frame[j]);
				si->dts[i].yuv_frame[j] = NULL;
			}
		}
	}
	return 0;
}
