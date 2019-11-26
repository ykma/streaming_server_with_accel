#include "stdafx.h"
#include <iostream>
#include <string.h>
#include "stitcher_stream_server.h"
#include "stitching_cl.h"
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
		if (stitch2pip_cnt + 40 < si->stitch_real_frames_cnt)
		{
			stitch2pip_cnt = si->stitch_real_frames_cnt - 25;
		}
		if (stitch2pip_cnt + 15 > si->stitch_real_frames_cnt)
			continue;
		if ((stitch2pip_cnt % 5) != 0)
			goto NEXT;
		offset = stitch2pip_cnt%YUV_BUF_COUNT;
		memcpy(yuvImg.data, si->yuv_stitch_buf[offset], si->output_width * si->output_height * 3 / 2);
		cv::cvtColor(yuvImg, rgbImg, CV_YUV2BGR_I420);

		resize(rgbImg, std_rgbImg, Size(1600, 800));
		cv::imshow("预览", std_rgbImg);
NEXT:
		stitch2pip_cnt++;
		waitKey(10);
	}

	Sleep(5);
	goto retry;
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
int get_last_AVFrame_bigframe(LPVOID lpParameter, int &offset, int &camnum)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	int process_offset = 0;
	int i;
	if (si->submit_frame_to_stitch_count < 10)
		return -1;
	process_offset = (si->submit_frame_to_stitch_count - 1) % YUV_BUF_COUNT;
	offset=process_offset*si->camnum;
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
	width = si->yuv_frame[offset]->width;
	height = si->yuv_frame[offset]->height;
	SYSTEMTIME st;
	printf("get_bigframe_from_AVFrame: camnum %d width %d height %d\n", camnum, width, height);
	
	for (i = 0; i < camnum; i++)
	{
		if (si->is_hwdec[i] == 1)
		{
			printf("%d ", i);
			image = frame_buf + i*width*height * 3 / 2;
			linesize = si->yuv_frame[offset + i]->linesize[0];
			src = si->yuv_frame[offset + i]->data[0];
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
			linesize = si->yuv_frame[offset + i]->linesize[1];
			src = si->yuv_frame[offset + i]->data[1];
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
			linesize = si->yuv_frame[offset + i]->linesize[0];
			src = si->yuv_frame[offset + i]->data[0];
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
			linesize = si->yuv_frame[offset + i]->linesize[1];
			src = si->yuv_frame[offset + i]->data[1];
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
			linesize = si->yuv_frame[offset + i]->linesize[2];
			src = si->yuv_frame[offset + i]->data[2];
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
#if 1
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
	while (si->submit_frame_to_stitch_count < 5)
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
		if (get_bigframe_from_AVFrame(lpParameter, yuv_offset, (unsigned char *)buf, camera_num, camera_width, camera_height) == -1)
		{
			Sleep(3000);
			continue;
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
#else
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
	while (si->submit_frame_to_stitch_count < 5)
	{
		Sleep(1000);
	}
retry:
	WCHAR wszClassName[512];
	memset(wszClassName, 0, sizeof(wszClassName));
	MultiByteToWideChar(CP_ACP, 0, si->oipstr_analyse, strlen(si->oipstr_analyse) + 1, wszClassName, sizeof(wszClassName) / sizeof(wszClassName[0]));
	printf("@@oipstr_analyse output to  @ %ls @\n", wszClassName);
	HANDLE hPipe = CreateNamedPipe(wszClassName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, buf_size+sizeof(PIPE_IMAGE_HEADER), buf_size + sizeof(PIPE_IMAGE_HEADER), 0, NULL);
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

		//pih->cam_no = si->camnum;
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
		Sleep(si->analyse_interval_ms);
	}
	CloseHandle(hPipe);

	Sleep(5);
	goto retry;
	return 0;
}
#endif

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
	while (si->submit_frame_to_stitch_count<2)
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
			unsigned char *dst,*dst1, *src;
			
			for (i = 0; i < si->camnum; i++)
			{
				if (si->is_hwdec[i] != 1)
				{
					dst = (unsigned char *)canvas + (width*i);
					linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[0];
					src = si->yuv_frame[i + process_offset*si->camnum]->data[0];
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
					linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[1];
					src = si->yuv_frame[i + process_offset*si->camnum]->data[1];
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
					linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[2];
					src = si->yuv_frame[i + process_offset*si->camnum]->data[2];
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
					linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[0];
					src = si->yuv_frame[i + process_offset*si->camnum]->data[0];
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
					linesize = si->yuv_frame[i + process_offset*si->camnum]->linesize[1];
					src = si->yuv_frame[i + process_offset*si->camnum]->data[1];
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
				FILE *err_log;
				fopen_s(&err_log, "err.log", "a+");
				fprintf(err_log, "Couldn't clEnqueueUnmapMemObject. errno=%llu\n", GetLastError());
				fclose(err_log);
				exit(1);
			}
#endif
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
	enum AVHWDeviceType type;
	AVCodecContext *dec_cxt[8] = { 0,0,0,0,0,0,0,0 };
	AVPacket *packet[8] = { 0,0,0,0,0,0,0,0 };
	AVFrame *drop_frame[8] = { NULL,NULL, NULL, NULL, NULL, NULL, NULL, NULL };
	for (j = 0; j < 8 * YUV_BUF_COUNT; j++)
		si->yuv_frame[j] = NULL;
	int yuvbuf_offset = 0;
	int ret;
	int first_done_camera_no;
	int64_t dts_std=0;
	unsigned long long nFrame = 0;
	unsigned long long nFrame_drop = 0;
	ULONGLONG tick, old_tick;
	unsigned int decoded_mask=0;
	AVFrame *hwFrame=NULL;
	printf("decoder start\n");
decoder_thread_restart:
	nFrame = 0;
	nFrame_drop = 0;
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
		
		if (si->is_hwdec[i] == 1)
		{
			si->dts[i].codec = codec_hw;
			dec_cxt[i]->get_format = get_hw_format;
			if (hw_decoder_init(si->dts[i].hw_device_ctx, dec_cxt[i], type) < 0)
				return -1;
		}
		else
		{
			si->dts[i].codec = codec_sw;
		}
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
	if ((si->is_hwdec[0] + si->is_hwdec[1] + si->is_hwdec[2] + si->is_hwdec[3] + si->is_hwdec[4] + si->is_hwdec[5] + si->is_hwdec[6] + si->is_hwdec[7])> 0)
	{
		if (hwFrame != NULL)
		{
			av_frame_free(&hwFrame);
			hwFrame = NULL;
		}
		hwFrame = av_frame_alloc();
	}
	for (j = 0; j <  si->camnum; j++)
	{
		if (drop_frame[j] != NULL)
		{
			av_frame_free(&drop_frame[j]);
			drop_frame[j] = NULL;
		}
		drop_frame[j] = av_frame_alloc();
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
				if (si->is_hwdec[i] == 1)
				{
					ret = avcodec_receive_frame(dec_cxt[i], hwFrame);
					if ((ret = av_hwframe_transfer_data(si->yuv_frame[i], hwFrame, 0)) < 0) {
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
					ret = avcodec_receive_frame(dec_cxt[i], si->yuv_frame[i]);
				}
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
			packet[i]->dts = (si->dts[lastno].output_offset);
			if (si->dts[i].output_offset != si->dts[0].output_offset)
			{
				printf("diff dts %d:%llu  %d:%llu\n", i,si->dts[i].output_offset,0, si->dts[i].output_offset);
			}
			ret = avcodec_send_packet(dec_cxt[i], packet[i]);
			if (ret < 0) {
				continue;
			}
		}
		
		int timeout = 0;
		has_decoded = 0;
		failed_decoded = 0;
		dts_std = 0;
		first_done_camera_no = -1;
		while (has_decoded != decoded_mask && timeout < 10)
		{
			Sleep(1);
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
				if (si->is_hwdec[i] == 1)
				{
					//printf("hard %d\n",i);
					ret = avcodec_receive_frame(dec_cxt[i], hwFrame);
					int errret = 0;
					if ((errret = av_hwframe_transfer_data(si->yuv_frame[i + yuvbuf_offset*si->camnum], hwFrame, 0)) < 0) {
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
					//printf("soft %d\n", i);
					ret = avcodec_receive_frame(dec_cxt[i], si->yuv_frame[i + yuvbuf_offset*si->camnum]);
				}
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
				if (first_done_camera_no == -1)
				{
					dts_std = si->yuv_frame[i + yuvbuf_offset*si->camnum]->pkt_dts;
					first_done_camera_no = i;
					if (i != 0)
					{
						printf("first done %d\n", i);
					}
				}
				else
				{
					if (dts_std != si->yuv_frame[i + yuvbuf_offset*si->camnum]->pkt_dts)
					{
						FILE *err_log;
						fopen_s(&err_log, "err.log","a+");
						fprintf(err_log, "dts_std=%llu err_std=");
						for (j = 0; j < si->camnum; j++)
						{
							fprintf(err_log, "%llu ", dts_std, si->yuv_frame[i + yuvbuf_offset*si->camnum]->pkt_dts);
							//printf("dts_std=%llu  err_std=%llu\n", dts_std, si->yuv_frame[i + yuvbuf_offset*si->camnum]->pkt_dts);
						}
						fprintf(err_log, "\n");
						fclose(err_log);
						for (j = 0; j < si->camnum; j++)
						{
							avcodec_send_packet(dec_cxt[j], NULL);
							while (ret != AVERROR_EOF)
							{
								ret = avcodec_receive_frame(dec_cxt[j], drop_frame[j]);
								av_frame_free(&drop_frame[j]);
							}
						}
						has_decoded = 0;
						goto skip_err;
					}
				}
				has_decoded |= (1 << i);
			}
			timeout++;
		}
		has_decoded = has_decoded&(~failed_decoded);
skip_err:
		nFrame++;

		if (has_decoded == decoded_mask)
		{
			//printf("%d frames decoded.\n", nFrame);
			//submit_bigframe(cis->screen_no, 0, 0, (&si->yuv_frame[yuvbuf_offset*cis->camnum]), cis->dts[lastno].width, cis->dts[lastno].height, 8);
			
			si->submit_frame_to_stitch_count++;
			yuvbuf_offset = (yuvbuf_offset + 1) % YUV_BUF_COUNT;
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
		if(dec_cxt[i]->hw_device_ctx!=NULL)
			av_buffer_unref(&dec_cxt[i]->hw_device_ctx);
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
