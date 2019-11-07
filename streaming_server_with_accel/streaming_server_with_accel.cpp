// stitcher_stream_server.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "stitcher_stream_server.h"

int getOpts(SERVER_INFO *si, int argc, char **argv)
{
	int port, size, hz;
	char filepath[120];
	//int hasPort, hasSize, hasFile = 0;
	si->output_height = si->output_width = 0;
	for (int i = 1; i<argc; i++) {
		if (!strcmp(argv[i], "-iaddr") && i + 1<argc) {
			strcpy_s(si->iipstr, argv[++i]);
			printf("fetch from :%s\n", si->iipstr);
		}
		else if (!strcmp(argv[i], "-oaddr") && i + 1<argc) {
			strcpy_s(si->oipstr, argv[++i]);
			sprintf(si->oipstr_analyse, "%s_analyse", si->oipstr);
			printf("for encode send to: %s\nfor analyse send to :%s\n", si->oipstr,si->oipstr_analyse);
		}
		else if (!strcmp(argv[i], "-saddr") && i + 1<argc) {
			strcpy_s(si->server_ipstr, argv[++i]);
			printf("connect server to :%s\n", si->server_ipstr);
		}
		else if (!strcmp(argv[i], "-camnum") && i + 1<argc) {
			si->camnum = atoi(argv[++i]);
			printf("camnum : %d\n", si->camnum);
		}
		else if (!strcmp(argv[i], "-oport") && i + 1<argc) {
			si->oport = atoi(argv[++i]);
			printf("send to : port %d\n", si->oport);
		}
		else if (!strcmp(argv[i], "-stitch_thread_count") && i + 1<argc) {
			si->stitch_thread_count = atoi(argv[++i]);
			printf("stitch_thread_count : %d\n", si->stitch_thread_count);
		}
		else if (!strcmp(argv[i], "-owidth") && i + 1<argc) {
			si->output_width = atoi(argv[++i]);
			printf("output_width: %d\n", si->output_width);
		}
		else if (!strcmp(argv[i], "-oheight") && i + 1<argc) {
			si->output_height = atoi(argv[++i]);
			printf("output_height: %d\n", si->output_height);
		}
		else if (!strcmp(argv[i], "-datafile") && i + 1<argc) {
			si->datafilename=si->exe_path+string(argv[++i]);
			printf("datafile path: %s\n", si->datafilename.c_str());
		}
		else if (!strcmp(argv[i], "-render") && i + 1<argc) {
			strcpy_s(si->render_str, argv[++i]);
			printf("render : %s\n", si->render_str);
		}
		else if (!strcmp(argv[i], "-camname") && i + 1<argc) {
			strcpy_s(si->camera_name, argv[++i]);
			printf("camera_name : %s\n", si->camera_name);
		}
		else if (!strcmp(argv[i], "-stream_id") && i + 1<argc) {
			si->stream_id=atoi(argv[++i]);
			printf("stream_id : %d\n", si->stream_id);
		}
		else {
			return -1;
		}
	}
	return 0;
}
int init_server_info(SERVER_INFO *si)
{
	int i;
	memset(si, 0, sizeof(SERVER_INFO));
	si->smi.buf_len = 64 * 1024 *1024;
	si->smi.buf = (unsigned char *)malloc(si->smi.buf_len);
	for (i = 0; i < YUV_BUF_COUNT; i++)
	{
		si->yuv_stitch_buf[i] = (char *)malloc(4096*2160*3/2);
	}
	InitializeCriticalSection(&si->dec2stitch_lock);
	return 0;
}
std::string TCHAR2STRING(TCHAR* str)
{
	std::string strstr;
	try
	{
		int iLen = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);

		char* chRtn = new char[iLen * sizeof(char)];

		WideCharToMultiByte(CP_ACP, 0, str, -1, chRtn, iLen, NULL, NULL);

		strstr = chRtn;
	}
	catch (std::exception e)
	{
	}

	return strstr;
}
int main(int argc, char **argv)
{
	TCHAR szFilePath[MAX_PATH + 1] = { 0 };
	GetModuleFileName(NULL, szFilePath, MAX_PATH);
	(_tcsrchr(szFilePath, _T('\\')))[1] = 0;
	
	//cout<<szFilePath;
	int local = 0;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		return -1;
	}
	SERVER_INFO *si;
	si = (SERVER_INFO *)malloc(sizeof(SERVER_INFO));
	init_server_info(si);
	si->exe_path = TCHAR2STRING(szFilePath);
	printf("%s\n", si->exe_path.c_str());

	printf("%c\n", argv[1][0]);
	if (argv[1][0] == 'F')
	{
		local = 1;
		strcpy_s(si->local_filename, argv[2]);
	}
	else
		getOpts(si, argc, argv);
	
	si->uuid = clock();
	sprintf_s(si->camera_name, "CAM1");
	//memcpy(si->server_ipstr,"192.168.10.197",sizeof("192.168.10.197"));
	if (local == 0)
	{
		stream_control(si, CMD_STREAM_CONTROL_OPEN, NULL);
		Sleep(3000);
		stream_control(si, CMD_STREAM_CONTROL_START, NULL);
		Sleep(2000);
	}
	if (si->output_width == 0 || si->output_height == 0)
	{
		si->output_width = 1920;
		si->output_height = 1080;
		printf("Auto set output_width to 1920 and output_height to 1080.\n");
	}
	CreateThread(0, 0, decoder_thread, (LPVOID)si, NULL, 0);
	CreateThread(0, 0, big_frame_receiver_machine, (LPVOID)si, NULL, 0);
	if (local == 0)
	{
		CreateThread(0, 0, networking_recv_thread, (LPVOID)si, NULL, 0);
	}
	else
	{
		CreateThread(0, 0, file_recv_thread, (LPVOID)si, NULL, 0);
	}
	Sleep(2000);
	for (int i = 0; i < si->stitch_thread_count; i++)
	{
		CreateThread(0, 0, stitch_update, (LPVOID)si, NULL, 0);
	}
	CreateThread(0, 0, write_to_pipe_encode, (LPVOID)si, NULL, 0);
	Sleep(300);
	CreateThread(0, 0, write_to_pipe_analyse, (LPVOID)si, NULL, 0);
	//CreateThread(0, 0, encode_deamon, (LPVOID)si, NULL, 0);
	unsigned long long sitich_cnt_old=0;
	double stitch_fps = 0.0;
	long long stitch_queue=0;
	int failed_cnt=0;
	while (1)
	{
		Sleep(10000);
		stitch_fps = (si->stitch_real_frames_cnt - sitich_cnt_old) / 10.0;
		stitch_queue = si->submit_frame_to_stitch_count - si->frame_stitched_count;
		printf("[Stitch]camera %s : Stitched Frame Rate = %g fps  wait sitich queue=%d.(%llu, %llu, %llu)\n",
			si->camera_name, stitch_fps,
			stitch_queue, si->stitch_real_frames_cnt, si->submit_frame_to_stitch_count, si->frame_stitched_count);
		if (stitch_fps == 0 || stitch_queue < 0)
		{
			failed_cnt++;
			if (failed_cnt > 2)
			{
				exit(-2);
			}
		}
		sitich_cnt_old = si->stitch_real_frames_cnt;
	}
	stream_control(si, CMD_STREAM_CONTROL_CLOSE, NULL);
	DeleteCriticalSection(&si->dec2stitch_lock);
	free(si);
    return 0;
}

