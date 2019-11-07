#ifndef _STITCHER_STREAM_SERVER_H
#define _STITCHER_STREAM_SERVER_H

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}
#include <winsock2.h>
#include<Ws2tcpip.h>
#include <Windows.h>
#include "utils.h"
#include "clFunction.h"
#include"oclData.h"
#define STATUS_MACHINE_START 0
#define STATUS_MACHINE_STAMP 1
#define STATUS_MACHINE_CAMERA_LEN 2
#define STATUS_MACHINE_CAMERA_FRAME 3
#define STATUS_MACHINE_END 4

#define CMD_STREAM_CONTROL_OPEN 0
#define CMD_STREAM_CONTROL_START 1
#define CMD_STREAM_CONTROL_CLOSE 2

#define YUV_BUF_COUNT 50
typedef struct bigframe
{
	void *avframe[8];
} BIGFRAME;
typedef struct
{
	unsigned long long stamp;
	int no;
	int pick_no;
	char pad[4096 - sizeof(unsigned long long) - sizeof(int) - sizeof(int)];
	char frame_start;
}FRAME_BUF_INFO;
typedef struct _FRAME_INFO
{
	int width;
	int height;
	unsigned long long stamp;
	unsigned int frame_type;
	unsigned int frame_rate;
	int pad;
	int pad1;
}FRAME_INFO;
typedef struct _STATUS_MACHINE_INFO
{
	unsigned char *buf;
	unsigned long long recv_offset;
	unsigned long long processed_offset;
	unsigned int buf_len;
	int id;
	int ending;
	int buffering_size;
}STATUS_MACHINE_INFO;
#define DECODER_BUFFER_COUNT 17
typedef struct
{
	AVCodec *codec;
	FRAME_BUF_INFO *frame_buf_info_ptr[DECODER_BUFFER_COUNT];
	int tick, tick_old;
	int nIndex;
	int oneonone;
	unsigned long long stamp;
	int buf_pool_offset;
	HANDLE	decoder_thread_handle;
	unsigned char *stream_frame[256];
	unsigned int stream_frame_len[256];
	unsigned int stream_frame_stamp[256];
	long long input_offset;
	long long output_offset;
	int width;
	int height;
	int channel;
	int camera_no;
	unsigned char *submit_frame[128];
	int submit_frame_avail[256];
	unsigned int *max_stamp;

}DECODER_THREAD_S;
typedef struct _SERVER_INFO
{
	char local_filename[256];
	char server_ipstr[16];
	char iipstr[16];
	char camera_name[16];
	unsigned long long uuid;
	int iport;
	char oipstr[256];
	char oipstr_analyse[256];
	int oport;
	SOCKET sock_control;
	sockaddr_in sock_control_addrs;
	SOCKET sock_data;
	sockaddr_in sock_data_addrs;
	int is_starting;
	STATUS_MACHINE_INFO smi;
	int camnum;
	unsigned char *net_frame_buf;
	AVFormatContext *ifmtctx;
	AVFormatContext *ofmtctx;
	AVCodecContext *icodecctx;
	DECODER_THREAD_S dts[8];
	unsigned long long submit_stamp;
	unsigned long long net_frame_count;
	unsigned long long net_frame_rate;
	unsigned long long submit_frame_to_stitch_count;
	unsigned long long frame_stitched_count;
	unsigned long long stitch_real_frames_cnt;
	CRITICAL_SECTION dec2stitch_lock;
	int write_to_pipe_ready;
	AVFrame *yuv_frame[8 * YUV_BUF_COUNT];
	char *yuv_stitch_buf[YUV_BUF_COUNT];
	int stitch_buf_avaliable[YUV_BUF_COUNT];
	int camera_width;
	int camera_height;
	int output_width;
	int output_height;
	string exe_path;
	string datafilename;
	char render_str[256];
	int stream_id;
	int pipe_ok;
	int stitch_thread_count;
}SERVER_INFO;
DWORD WINAPI file_recv_thread(LPVOID lpParameter);
DWORD WINAPI networking_recv_thread(LPVOID lpParameter);
DWORD WINAPI big_frame_receiver_machine(LPVOID lpParameter);
DWORD WINAPI decoder_thread(LPVOID lpParameter);
DWORD WINAPI stitch_update(LPVOID lpParameter);
DWORD WINAPI write_to_pipe_encode(LPVOID lpParameter);
DWORD WINAPI write_to_pipe_analyse(LPVOID lpParameter);
DWORD WINAPI encode_deamon(LPVOID lpParameter);

int stream_control(SERVER_INFO *si, int cmd, void *param);
#endif
