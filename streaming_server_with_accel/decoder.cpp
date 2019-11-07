#include "stdafx.h"
#include <iostream>
#include <string.h>
#include "stitcher_stream_server.h"
using namespace std;

#define PIPE_RATE (1.0/15)
DWORD WINAPI encode_deamon(LPVOID lpParameter)
{
	char cmd[1024];
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	string encode_file_path;
	FILE *fp;
	while (si->is_starting == 2)
	{
		if (si->write_to_pipe_ready == 1)
		{
			encode_file_path = si->exe_path + "audoencode.bat";
			//fopen_s(&fp, &encode_file_path.c_str,"w+");
			//fwrite();
			//fclose(fp);
		
			//sprintf_s(cmd,"ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -i \\\\.\\Pipe\\room -vcodec libx264 -an -f flv -vf drawtext=\"fontfile = simhei.ttf:x = 200 : y = 300 : fontcolor = white : fontsize = 30 : text = 'city:suzhou  weather:sunny now: \%{localtime\\:\%Y-\%m-\%d \%H\\\\\\:\%M\\\\\\:\%S}'\" rtmp://%s:1935/hls/room", si->oipstr);
			//sprintf_s(cmd,"ffmpeg -init_hw_device qsv=hw -filter_hw_device hw -f rawvideo -pix_fmt yuv420p -s 1280x720 -i \\\\.\\Pipe\\room -c:v h264_qsv -b:v 280k -an -f flv -vf drawtext=\"fontfile = simhei.ttf:x = 200 : y = 300 : fontcolor = white : fontsize = 30 : text = '地点：苏州  天气：多云   当前时间 %{localtime\:%Y-%m-%d %H\\\:%M\\\:%S}'\" rtmp://192.168.10.97:1935/hls/room", si->oipstr);
			system(cmd);
		}
		Sleep(1000);
	}
	return 0;
}
DWORD WINAPI write_to_pipe(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	unsigned long long stitch2pip_cnt = 0;
	unsigned long WriteNum;
	int offset;
retry:
	si->write_to_pipe_ready = 0;
	WCHAR wszClassName[512];
	memset(wszClassName, 0, sizeof(wszClassName));
	MultiByteToWideChar(CP_ACP, 0, si->oipstr, strlen(si->oipstr) + 1, wszClassName, sizeof(wszClassName) / sizeof(wszClassName[0]));
	printf("output to %ls\n", si->oipstr);
	HANDLE hPipe = CreateNamedPipe(wszClassName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 10*1024*1024, 10 * 1024 * 1024, 0, NULL);
	if (hPipe == INVALID_HANDLE_VALUE)
	{
		printf("create pipe failed\n");
		CloseHandle(hPipe);
		return 0;
	}
	si->write_to_pipe_ready = 1;
	if (ConnectNamedPipe(hPipe, NULL) == FALSE)
	{
		printf("failed to connect client pipe\n");
		CloseHandle(hPipe);
		Sleep(5);
		goto retry;
	}
	printf("success to connect cliet pipe\n");
	si->pipe_ok = 1;
	while (si->stitch_real_frames_cnt<2)
	{
		Sleep(300);
	}
	while (si->is_starting==2)
	{
		if (stitch2pip_cnt+40 < si->stitch_real_frames_cnt)
		{
			stitch2pip_cnt = si->stitch_real_frames_cnt - 25;
		}
		if (stitch2pip_cnt+15 > si->stitch_real_frames_cnt)
			continue;
		offset = stitch2pip_cnt%YUV_BUF_COUNT;
		
		if (WriteFile(hPipe, si->yuv_stitch_buf[offset], si->output_width * si->output_height * 3 / 2, &WriteNum, NULL) == FALSE)
		{
			printf("failed to write to pipe\n");
			break;
		}
		if (WriteNum != si->output_width * si->output_height * 3 / 2)
		{
			printf("Length of writed to pipe bytes is wrong\n");
		}
		
		stitch2pip_cnt++;
		Sleep(20);
	}
	CloseHandle(hPipe);

	Sleep(5);
	goto retry;
	return 0;
}
DWORD WINAPI write_to_pipe_encode(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	unsigned long long stitch2pip_cnt = 0;
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
	while (si->is_starting == 2)
	{
		if (stitch2pip_cnt + 40 < si->stitch_real_frames_cnt)
		{
			stitch2pip_cnt = si->stitch_real_frames_cnt - 25;
		}
		if (stitch2pip_cnt + 15 > si->stitch_real_frames_cnt)
			continue;
		offset = stitch2pip_cnt%YUV_BUF_COUNT;

		if (WriteFile(hPipe, si->yuv_stitch_buf[offset], si->output_width * si->output_height * 3 / 2, &WriteNum, NULL) == FALSE)
		{
			printf("failed to write to encode pipe\n");
			break;
		}
		if (WriteNum != si->output_width * si->output_height * 3 / 2)
		{
			printf("Length of writed to pipe bytes is wrong\n");
		}

		stitch2pip_cnt++;
		Sleep(20);
	}
	CloseHandle(hPipe);

	Sleep(5);
	goto retry;
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
	char *buf = (char *)malloc(si->camnum*si->camera_width*si->camera_height*3/2);
	char *image = buf;
	while (si->submit_frame_to_stitch_count < 5)
	{
		Sleep(1000);
	}
retry:
	WCHAR wszClassName[512];
	memset(wszClassName, 0, sizeof(wszClassName));
	MultiByteToWideChar(CP_ACP, 0, si->oipstr_analyse, strlen(si->oipstr_analyse) + 1, wszClassName, sizeof(wszClassName) / sizeof(wszClassName[0]));
	printf("@@oipstr_analyse output to  @ %ls @\n", wszClassName);
	HANDLE hPipe = CreateNamedPipe(wszClassName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 10 * 1024 * 1024, 10 * 1024 * 1024, 0, NULL);
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
	while (si->is_starting == 2)
	{
		process_offset = (si->submit_frame_to_stitch_count-1)%YUV_BUF_COUNT;
		image = buf;
		for (i = 0; i < si->camnum; i++)
		{
			pih->cam_no = i;
			linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[0];
			src = si->yuv_frame[i + process_offset*si->camnum]->data[0];
			if (src == NULL)
			{
				continue;
			}
			for (m = 0; m < si->dts[lastno].height; m++)
			{
				memcpy(image,src, si->dts[lastno].width);
				src += linesize;
				image += si->dts[lastno].width;
			}
			linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[1];
			src = si->yuv_frame[i + process_offset*si->camnum]->data[1];
			if (src == NULL)
			{
				continue;
			}
			for (m = 0; m < si->dts[lastno].height / 2; m++)
			{
				memcpy(image, src, si->dts[lastno].width / 2);
				src += linesize;
				image += si->dts[lastno].width / 2;
			}
			linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[2];
			src = si->yuv_frame[i + process_offset*si->camnum]->data[2];
			if (src == NULL)
			{
				continue;
			}
			for (m = 0; m < si->dts[lastno].height / 2; m++)
			{
				memcpy(image, src, si->dts[lastno].width / 2);
				src += linesize;
				image += si->dts[lastno].width / 2;
			}
		}
		if (WriteFile(hPipe, pih, sizeof(PIPE_IMAGE_HEADER), &WriteNum, NULL) == FALSE)
		{
			printf("failed to write to pipe\n");
			break;
		}
		if (WriteNum != sizeof(PIPE_IMAGE_HEADER))
		{
			printf("Length of head writed to pipe bytes is wrong(%d)\n", WriteNum);
		}
		rest_bytes = si->camnum*si->dts[lastno].width * si->dts[lastno].height * 3 / 2;
		send_offset = 0;
		int send_byes_once = 0;
		while (rest_bytes > 0)
		{
			send_byes_once = (rest_bytes > (4*1024 * 1024)) ? (4*1024 * 1024) : rest_bytes;
			
			if (WriteFile(hPipe, buf+ send_offset, send_byes_once, &send_bytes, NULL) == FALSE)
			{
				printf("failed to write to pipe,%d\n",GetLastError());
				break;
			}
			printf("send bytes=%d\n", send_bytes);
			if (WriteNum <0)
			{
				printf("Length of writed to pipe bytes is wrong, %d\n",GetLastError());
				continue;
			}
			send_offset = +send_bytes;
			rest_bytes -= send_bytes;
		}
		Sleep(2000);
	}
	CloseHandle(hPipe);

	Sleep(5);
	goto retry;
	return 0;
}
//#define DUMP_IMAGE 

DWORD WINAPI stitch_update(LPVOID lpParameter)
{
	unsigned long long old_tick = 0, tick = 0;
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	int i;
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
	unsigned long long stitch_offset = 0;
	int process_offset;
#ifdef DUMP_IMAGE
	char *image = (char*)malloc(64*1024*1024);
#endif
	while (si->submit_frame_to_stitch_count<3)
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
	init(&ocl, si->camera_height, si->camera_width, 0, si->render_str, si->camnum, si->exe_path);
	generate_mapping_data(si->datafilename, mapx, mapy, weight, owidth, oheight, mapWidth, mapHeight, cut);
	oclMappingData(&ocl, mapx, mapy, weight, owidth, oheight, mapWidth, mapHeight, cut);
	

	//printf("start to stitch.\n");
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
	while (si->is_starting == 2)
	{
		process_offset = -1;
		EnterCriticalSection(&si->dec2stitch_lock);
		if (si->frame_stitched_count+6 < si->submit_frame_to_stitch_count)
		{
			si->frame_stitched_count += 1;
		}
		if (si->frame_stitched_count < si->submit_frame_to_stitch_count)
		{
			process_offset = si->frame_stitched_count%YUV_BUF_COUNT;
			si->frame_stitched_count++;
		}
		else
		{
			Sleep(30);
			LeaveCriticalSection(&si->dec2stitch_lock);
			continue;
		}
		stitch_offset = si->stitch_real_frames_cnt;
		si->stitch_real_frames_cnt++;

		LeaveCriticalSection(&si->dec2stitch_lock);

		if (process_offset!=-1)
		{
			//size_t region_input[3] = { si->dts[lastno].width * si->camnum, si->dts[lastno].height, 1 }; // Elements to per dimension
			//size_t region_input2[3] = { si->dts[lastno].width * si->camnum/2, si->dts[lastno].height/2, 1 }; // Elements to per dimension
			//printf("stitch offset %d \n", process_offset);
			width = si->dts[lastno].width;
			height = si->dts[lastno].height;
			
			int m, linesize;
			unsigned char *dst, *src;
			for (i = 0; i < si->camnum; i++)
			{
				dst = (unsigned char *)canvas+(width*i);
				linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[0];
				src = si->yuv_frame[i + process_offset*si->camnum]->data[0];
				if (src == NULL)
				{
					continue;
				}
				for (m = 0; m < height; m++)
				{
					memcpy(dst, src, width);
					src += linesize;
					dst += width*si->camnum;
				}
				dst = (unsigned char *)uImage + (width*i/2);
				linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[1];
				src = si->yuv_frame[i + process_offset*si->camnum]->data[1];
				if (src == NULL)
				{
					continue;
				}
				for (m = 0; m < height/2; m++)
				{
					memcpy(dst, src, width/2);
					src += linesize;
					dst += width*si->camnum/2;
				}
				dst = (unsigned char *)vImage + (width*i / 2);
				linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[2];
				src = si->yuv_frame[i + process_offset*si->camnum]->data[2];
				if (src == NULL)
				{
					continue;
				}
				for (m = 0; m < height / 2; m++)
				{
					memcpy(dst, src, width / 2);
					src += linesize;
					dst += width *si->camnum/2;
				}
			}
#ifdef DUMP_IMAGE
			memcpy(image,canvas,si->camera_width*si->camera_height*8);
			memcpy(image + si->camera_width*si->camera_height * 8, uImage, si->camera_width*si->camera_height * 2);
			memcpy(image + si->camera_width*si->camera_height * 10, vImage, si->camera_width*si->camera_height * 2);
			Mat resulti;
			cvtColor(Mat(si->camera_height * 3 / 2, si->camera_width*8, CV_8UC1, (void*)image), resulti, CV_YUV420p2RGB);
			namedWindow("resulti", WINDOW_NORMAL);
			imshow("resulti", resulti);
			waitKey(0);
#endif
#if 1
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
				perror("couldn't finish1 input");
				exit(1);
			}

			ocl.err = clEnqueueNDRangeKernel(ocl.queue, ocl.kernel, 2, NULL, global_size,
				NULL, 0, NULL, NULL);
			if (ocl.err < 0) {
				perror("Couldn't enqueue the kernel");
				exit(1);
			}
			ocl.err = clFinish(ocl.queue);
			if (ocl.err < 0)
			{
				perror("couldn't finish output");
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
				perror("Couldn't read output buffer");
				exit(1);
			}
#ifdef DUMP_IMAGE
			Mat result;
			cvtColor(Mat(oheight * 3 / 2, owidth, CV_8UC1, (void*)outBuffer), result, CV_YUV420p2RGB);
			namedWindow("result", WINDOW_NORMAL);
			imshow("result", result);
			waitKey(0);
#endif
			memcpy(si->yuv_stitch_buf[stitch_offset%YUV_BUF_COUNT], outBuffer, si->output_height*si->output_width * 3 / 2);
			ocl.err = clEnqueueUnmapMemObject(ocl.queue, ocl.output, outBuffer, 0, NULL, NULL);
			if (ocl.err < 0) {
				perror("Couldn't read output buffer");
				exit(1);
			}
#endif
		}
		else
			Sleep(30);
	}

	return 0;
}
DWORD WINAPI decoder_thread(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	string path = si->datafilename;
	int lastno= si->camnum-1;
	int initd = 0;
	int i, j;
	unsigned char has_keyframe = 0;
	unsigned char has_decoded = 0;
	unsigned char failed_decoded = 0;
	unsigned char *frame_ptr[8];
	AVCodecContext *dec_cxt[8] = { 0,0,0,0,0,0,0,0 };
	AVPacket *packet[8] = { 0,0,0,0,0,0,0,0 };
	
	for (j = 0; j < 8 * YUV_BUF_COUNT; j++)
		si->yuv_frame[j] = NULL;
	int yuvbuf_offset = 0;
	int ret;
	unsigned long long nFrame = 0;
	unsigned long long nFrame_drop = 0;
	ULONGLONG tick, old_tick;
	unsigned int decoded_mask=0;
	printf("decoder start\n");
decoder_thread_restart:
	nFrame = 0;
	nFrame_drop = 0;
	AVCodec *codec;
	codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!codec) {
		printf("Codec not found\n");
		return -1;
	}
	while (si->is_starting != 2)
	{
		Sleep(2000);
	}
	decoded_mask = 0;
	for (i = 0; i < si->camnum; i++)
	{
		decoded_mask = (decoded_mask << 1) | 1;
	}
	has_keyframe = 0;
	lastno = si->camnum - 1;
	for (i = 0; i < si->camnum; i++)
	{
		dec_cxt[i] = avcodec_alloc_context3(si->dts[i].codec);
		if (!dec_cxt[i]) {
			goto decoder_thread_restart;
		}
		//if (cis->camera_no == 0)
		{
			//cis->dts[i].codec->capabilities = (cis->dts[i].codec->capabilities)|( AV_CODEC_CAP_AUTO_THREADS);
			dec_cxt[i]->thread_count = 1;
		}
		si->dts[i].codec = codec;
		if (avcodec_open2(dec_cxt[i], si->dts[i].codec, NULL) < 0) {
			printf("[Decoder]Could not open codec\n");
			goto decoder_thread_restart;
		}
		packet[i] = av_packet_alloc();
		if (!packet[i])
			goto decoder_thread_restart;

	}

	for (j = 0; j < YUV_BUF_COUNT * 8; j++)
	{
		if (si->yuv_frame[j] != NULL)
		{
			av_frame_free(&si->yuv_frame[j]);
			si->yuv_frame[j] = NULL;
		}
		si->yuv_frame[j] = av_frame_alloc();
	}
	printf("seek sync frame\n");
	while (has_keyframe != decoded_mask && si->is_starting == 2)
	{
		if ((si->dts[lastno].input_offset - si->dts[lastno].output_offset) < 1)
		{
			Sleep(10);
			continue;
		}

		for (i = 0; i < si->camnum; i++)
		{
			frame_ptr[i] = si->dts[i].stream_frame[(si->dts[i].output_offset) % 256];
			if ((has_keyframe&(1 << i)) == 0 && (frame_ptr[i][4] - 0x67) != 0)
			{
				si->dts[i].output_offset++;
				continue;
			}
			
			packet[i]->data = frame_ptr[i];
			packet[i]->size = si->dts[i].stream_frame_len[(si->dts[i].output_offset) % 256];
			ret = avcodec_send_packet(dec_cxt[i], packet[i]);
			if (ret < 0)
			{
				si->dts[i].output_offset++;
				continue;
			}
			ret = AVERROR(EAGAIN);
			while (ret == AVERROR(EAGAIN))
			{
				ret = avcodec_receive_frame(dec_cxt[i], si->yuv_frame[i]);
			}
			if (ret < 0)
			{
				si->dts[i].output_offset++;
				continue;
			}
			if ((has_keyframe&(1 << i)) == 0)
			{
				si->dts[i].width = si->yuv_frame[i]->width;
				si->dts[i].height = si->yuv_frame[i]->height;
			}
			si->dts[i].output_offset++;
			has_keyframe |= (1 << i);
		}
	}
	yuvbuf_offset = 0;
	printf("raw video is %d x %d\n", si->dts[lastno].width, si->dts[lastno].height);

	old_tick = tick = ::GetTickCount64();
	printf("start to decode.\n");
	si->camera_height = si->dts[lastno].height;
	si->camera_width = si->dts[lastno].width;
	while (si->is_starting == 2)
	{
		if ((si->dts[lastno].input_offset - si->dts[lastno].output_offset) < 1)
		{
			Sleep(5);
			continue;
		}

		for (i = 0; i < si->camnum; i++)
		{
			frame_ptr[i] = si->dts[i].stream_frame[(si->dts[i].output_offset) % 256];
			packet[i]->data = frame_ptr[i];
			packet[i]->size = si->dts[i].stream_frame_len[(si->dts[i].output_offset) % 256];
			ret = avcodec_send_packet(dec_cxt[i], packet[i]);
			if (ret < 0) {
				continue;
			}
		}
		int timeout = 0;
		has_decoded = 0;
		failed_decoded = 0;
		while (has_decoded != decoded_mask && timeout < 10)
		{
			for (i = 0; i < si->camnum; i++)
			{
				if ((has_decoded &(1 << i)) != 0)
				{
					continue;
				}
				if (ret == AVERROR(EAGAIN))
				{
					Sleep(1);
				}
				ret = avcodec_receive_frame(dec_cxt[i], si->yuv_frame[i + yuvbuf_offset*si->camnum]);
				if (ret == AVERROR(EAGAIN))
				{
					continue;
				}
				if (ret == AVERROR_EOF)
				{
					has_decoded |= (1 << i);
					failed_decoded |= (1 << i);
					continue;
				}
				else if (ret < 0)
				{
					continue;
				}
				
				has_decoded |= (1 << i);
			}
			timeout++;
		}
		has_decoded = has_decoded&(~failed_decoded);
		nFrame++;
		if (has_decoded == decoded_mask)
		{
			//printf("%d frames decoded.\n", nFrame);
			//submit_bigframe(cis->screen_no, 0, 0, (&si->yuv_frame[yuvbuf_offset*cis->camnum]), cis->dts[lastno].width, cis->dts[lastno].height, 8);
			
			si->submit_frame_to_stitch_count++;
			int m, linesize;
			unsigned char *dst, *src;
			if (0 && (si->submit_frame_to_stitch_count % 2) == 9)
				for (i = 0; i < si->camnum; i++)
				{
					FILE *fp;
					char filename[256];
					int countbytes;
					countbytes = 0;
					sprintf_s(filename, "e:\\yuvpic\\%d-%llu-%dx%d.yuv", i, si->submit_frame_to_stitch_count, si->dts[lastno].width, si->dts[lastno].height);
					
					fopen_s(&fp, filename, "wb+");
					linesize = si->yuv_frame[i + yuvbuf_offset*si->camnum]->linesize[0];
					src = si->yuv_frame[i + yuvbuf_offset*si->camnum]->data[0];
					
					if (src == NULL)
					{
						continue;
					}
					for (m = 0; m < si->dts[lastno].height; m++)
					{
						fwrite(src, 1, si->dts[lastno].width, fp);
						src += linesize;
						countbytes += si->dts[lastno].width;
					}
					printf("Y: linesize=%d  w=%d  h=%d count=%d\n", linesize, si->dts[lastno].width, si->dts[lastno].height, countbytes);
					linesize = si->yuv_frame[i + yuvbuf_offset*si->camnum]->linesize[1];
					src = si->yuv_frame[i + yuvbuf_offset*si->camnum]->data[1];
					if (src == NULL)
					{
						continue;
					}
					
					for (m = 0; m < si->dts[lastno].height / 2; m++)
					{
						fwrite(src, 1, si->dts[lastno].width / 2, fp);
						src += linesize;
						countbytes += si->dts[lastno].width/2;
					}
					printf("U: linesize=%d  w=%d  h=%d count=%d\n", linesize, si->dts[lastno].width, si->dts[lastno].height, countbytes);
					linesize = si->yuv_frame[i + yuvbuf_offset*si->camnum]->linesize[2];
					src = si->yuv_frame[i + yuvbuf_offset*si->camnum]->data[2];
					if (src == NULL)
					{
						continue;
					}
					
					for (m = 0; m < si->dts[lastno].height / 2; m++)
					{
						fwrite(src, 1, si->dts[lastno].width / 2, fp);
						src += linesize;
						countbytes += si->dts[lastno].width / 2;
					}
					printf("V: linesize=%d  w=%d  h=%d count=%d\n", linesize, si->dts[lastno].width, si->dts[lastno].height, countbytes);
					printf("%s len:%d\n", filename, ftell(fp));
					fclose(fp);
				}
		}
		else
		{
			nFrame_drop++;

		}
		if ((nFrame % 200) == 199)
		{
			old_tick = tick;
			tick = ::GetTickCount64();
			printf("[Decoder]camera %s : Decoded_Frame Rate = %g fps, Drop_Frame_Rate = %g\n",
				si->camera_name, (200.0 * 1000) / (tick - old_tick), nFrame_drop*1000.0 / (tick - old_tick));
			nFrame_drop = 0;
		}
		for (i = 0; i < si->camnum; i++)
			si->dts[i].output_offset++;
		
		yuvbuf_offset = (yuvbuf_offset + 1) % YUV_BUF_COUNT;
	}

	for (i = 0; i < si->camnum; i++)
	{
		//avfreep(&packet[i]);
		if (packet[i] != NULL)
		{
			av_packet_free(&packet[i]);
			packet[i] = 0;
		}
		if (dec_cxt[i] != NULL)
		{
			avcodec_free_context(&dec_cxt[i]);
			dec_cxt[i] = 0;
		}
	}
	Sleep(1000);
	if (0)
	{
		for (j = 0; j < YUV_BUF_COUNT * 8; j++)
		{
			//av_frame_unref(yuv_frame[j*si->camnum + i]);
			av_frame_free(&si->yuv_frame[j*si->camnum + i]);
			si->yuv_frame[j*si->camnum + i] = NULL;
		}
	}
	goto decoder_thread_restart;

ENDDECODER:
	for (i = 0; i < si->camnum; i++)
	{
		//avfreep(&packet[i]);
		if (packet[i] != NULL)
		{
			av_packet_free(&packet[i]);
			packet[i] = 0;
		}
		if (dec_cxt[i] != NULL)
		{
			avcodec_free_context(&dec_cxt[i]);
			dec_cxt[i] = 0;
		}
	}
	for (j = 0; j < YUV_BUF_COUNT * 8; j++)
	{
		if (si->yuv_frame[j] != NULL)
		{
			av_frame_free(&si->yuv_frame[j]);
			si->yuv_frame[j] = NULL;
		}
	}
	return 0;
}