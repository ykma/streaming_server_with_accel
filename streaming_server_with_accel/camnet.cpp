#include "stdafx.h"
//
#include "stitcher_stream_server.h"
#pragma comment(lib, "ws2_32.lib")

DWORD WINAPI big_frame_receiver_machine(LPVOID lpParameter)
{
	//SetThreadAffinityMask(GetCurrentThread(), 0x8);
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	STATUS_MACHINE_INFO *smi;
	smi = &si->smi;
	int buf_len;
	unsigned char *buf_end;
	int ready_buf_offset = 0;
	buf_len = smi->buf_len;
	int next_status = STATUS_MACHINE_START;
	unsigned char *ptr;
	unsigned long long int nFrame;
	unsigned long long tick_old;
	unsigned long long tick = clock();
	tick_old = tick;
	int i, j;
	ptr = smi->buf;
	buf_end = smi->buf + buf_len;
	int hit;
	unsigned int frame_len[8];
	int total_frame_len;
	unsigned char *frame_buf;
	if (si->net_frame_buf == NULL)
	{
		si->net_frame_buf = (unsigned char*)malloc(50 * 1024 * 1024);
	}

	frame_buf = si->net_frame_buf;
	//frame_buf = (unsigned char*)malloc(50 * 1024 * 1024);
	unsigned char *frame_buf_tmp;
	frame_buf_tmp = frame_buf;
	unsigned long long stamp;
	char *cstamp = (char *)&stamp;
	unsigned long long fake_stamp = 0;
	unsigned int startcode;
	unsigned int waiting_bytes = 4;
	char filename[256];
	int camnum = 8;
	//sprintf_s(filename,"net%d.h264", cis->camera_no);
	FILE *fp;
	//fopen_s(&fp,filename,"wb+");
	while (smi->ending == 0)
	{
		ready_buf_offset = smi->recv_offset - smi->processed_offset;
		//if (ready_buf_offset < smi->buffering_size)
		if (ready_buf_offset <waiting_bytes)
		{
			Sleep(50);
			continue;
		}
		ptr = smi->buf + (smi->processed_offset%buf_len);
		//printf("wait for %d byte, next status =%d, recv=%llu, processed=%llu\n",waiting_bytes, next_status, smi->recv_offset, smi->processed_offset);
		switch (next_status)
		{
		case STATUS_MACHINE_START:
			j = buf_end - ptr;
			hit = 0;
			for (i = 0; i < (j - 3); i++)
			{
				if (ptr[i] != 0xff || ptr[i + 1] != 0xff || ptr[i + 2] != 0xff || ptr[i + 3] != 0xff)
				{
					continue;
				}
				hit = 1;
				break;
			}
			smi->processed_offset += i;
			ptr += i;
			if (hit == 0)
			{
				switch (j)
				{
				case 3:
					if (ptr[0] == 0xff && ptr[1] == 0xff && ptr[2] == 0xff && smi->buf[0] == 0xff)
					{
						hit = 1;
					}
					break;
				case 2:
					if (ptr[0] == 0xff && ptr[1] == 0xff && smi->buf[0] == 0xff && smi->buf[1] == 0xff)
					{
						hit = 1;
					}
					break;
				case 1:
					if (ptr[0] == 0xff && smi->buf[0] == 0xff && smi->buf[1] == 0xff && smi->buf[2] == 0xff)
					{
						hit = 1;
					}
					break;
				default:
					printf("[Networking]camera %s header has a error value(0x%08x).\n", si->camera_name, j);
					break;
				}
			}
			if (hit == 1)
			{
				smi->processed_offset += 4;
				//OutputDebugPrintf("find bigframe head.\n");
				next_status = STATUS_MACHINE_STAMP;
				waiting_bytes = 8;
			}
			else
			{
				smi->processed_offset += 1;
				printf("[Networking]camera %s not head.\n", si->camera_name);
			}
			break;
		case STATUS_MACHINE_STAMP:
			j = buf_end - ptr;

			if (j >= 8)
			{
				memcpy((void*)cstamp, ptr, 8);
			}
			else
			{
				memcpy(cstamp, ptr, j);
				memcpy(cstamp + j, smi->buf, 8 - j);
			}
			smi->processed_offset += 8;
			next_status = STATUS_MACHINE_CAMERA_LEN;
			waiting_bytes = si->camnum * 4;
			break;

		case STATUS_MACHINE_CAMERA_LEN:
			j = buf_end - ptr;
			if (j >= camnum * 4)
			{
				memcpy((void*)frame_len, ptr, camnum * 4);
			}
			else
			{
				memcpy((void*)frame_len, ptr, j);
				memcpy(((char *)frame_len) + j, smi->buf, camnum * 4 - j);
			}
			total_frame_len = 0;
			for (i = 0; i < camnum; i++)
			{
				if ((frame_len[i] & 0xffffff)>(1024 * 1024))
				{
					printf("[Networking]Camera %s sub %d Error frame length is 0x%06x.\n", si->camera_name, i, frame_len[i] & 0xffffff);
					next_status = STATUS_MACHINE_START;
					waiting_bytes = 4;
					break;
				}
				total_frame_len += (frame_len[i] & 0xffffff);
			}
			if (next_status != STATUS_MACHINE_START)
			{
				smi->processed_offset += camnum * 4;
				next_status = STATUS_MACHINE_CAMERA_FRAME;
				waiting_bytes = total_frame_len;
			}
			break;
		case STATUS_MACHINE_CAMERA_FRAME:
			//OutputDebugPrintf("bigframe fetch...");
			ready_buf_offset = smi->recv_offset - smi->processed_offset;
			if (ready_buf_offset >= total_frame_len)
			{
				unsigned int channel_no;
				unsigned int dstlen;
				int is_disorder = 0;

				if (((frame_len[0] >> 24) != (frame_len[1] >> 24)) && ((frame_len[2] >> 24) != (frame_len[3] >> 24)))
					is_disorder = 1;
				//OutputDebugPrintf("%d %d(dis=%d)\n", frame_len[0] >> 24, frame_len[1] >> 24, is_disorder);
				for (i = 0; i < si->camnum; i++)
				{
					if (is_disorder == 0)
					{
						channel_no = i;
						dstlen = frame_len[i];
					}
					else
					{
						channel_no = frame_len[i] >> 24;
						dstlen = frame_len[i] & 0x1fffff;
						//OutputDebugPrintf("channel %d frame extract %d Bytes.\n", channel_no, dstlen);
					}
					if (dstlen == 0)
						continue;

					ptr = smi->buf + (smi->processed_offset%buf_len);
					j = buf_end - ptr;
					if (j >= dstlen)
					{
						si->dts[channel_no].stream_frame[(si->dts[channel_no].input_offset) % 256] = ptr;
						si->dts[channel_no].stream_frame_len[(si->dts[channel_no].input_offset) % 256] = (dstlen);
						si->dts[channel_no].stream_frame_stamp[(si->dts[channel_no].input_offset) % 256] = fake_stamp;
						si->submit_stamp = fake_stamp;
						si->dts[channel_no].input_offset++;
					}
					else
					{
						if ((frame_buf_tmp - frame_buf + dstlen) > 50 * 1024 * 1024)
						{
							frame_buf_tmp = frame_buf;
						}
						memcpy(frame_buf_tmp, ptr, j);
						memcpy(frame_buf_tmp + j, smi->buf, dstlen - j);
						si->dts[channel_no].stream_frame[(si->dts[channel_no].input_offset) % 256] = frame_buf_tmp;
						si->dts[channel_no].stream_frame_len[(si->dts[channel_no].input_offset) % 256] = dstlen;
						si->dts[channel_no].stream_frame_stamp[(si->dts[channel_no].input_offset) % 256] = fake_stamp;
						si->submit_stamp = fake_stamp;
						si->dts[channel_no].input_offset++;
						frame_buf_tmp += dstlen;
					}

					smi->processed_offset += dstlen;
				}
				fake_stamp++;
				si->net_frame_count++;
				//printf("%llu frames sumbitted from net\n", si->net_frame_count);
				if ((si->net_frame_count % 500) == 0)
				{
					tick = ::GetTickCount();
					printf("[Networking]Camera %s Network received frame rate is %f\n", si->camera_name, 500.0 * 1000 / (tick - tick_old));
					si->net_frame_rate = 500.0 * 1000 / (tick - tick_old);
					tick_old = tick;
				}
				next_status = STATUS_MACHINE_END;
				waiting_bytes = 4;
			}
			break;
		case STATUS_MACHINE_END:
			j = buf_end - ptr;
			hit = 0;
			if (j > 3)
			{
				if (ptr[0] == 0xff && ptr[1] == 0xff && ptr[2] == 0xff && ptr[3] == 0xfe)
				{
					hit = 1;
				}
			}
			else
			{
				switch (j)
				{
				case 3:
					if (ptr[0] == 0xff && ptr[1] == 0xff && ptr[2] == 0xff && smi->buf[0] == 0xfe)
					{
						hit = 1;
					}
					break;
				case 2:
					if (ptr[0] == 0xff && ptr[1] == 0xff && smi->buf[0] == 0xff && smi->buf[1] == 0xfe)
					{
						hit = 1;
					}
					break;
				case 1:
					if (ptr[0] == 0xff && smi->buf[0] == 0xff && smi->buf[1] == 0xff && smi->buf[2] == 0xfe)
					{
						hit = 1;
					}
					break;
				default:
					printf("[Networking]Camera %s ending has a error value(0x%08x) falut ending(0x%02x%02x%02x%02x).\n", si->camera_name, j, ptr[0], ptr[1], ptr[2], ptr[3]);
					break;
				}
			}

			if (hit == 1)
			{
				smi->processed_offset += 4;
				//OutputDebugPrintf("find bigframe head.\n");
			}
			else
			{
				smi->processed_offset += 1;
				printf("[Networking]Camera %s not ending(0x%02x%02x%02x%02x).\n", si->camera_name, ptr[0], ptr[1], ptr[2], ptr[3]);
			}
			next_status = STATUS_MACHINE_START;
			waiting_bytes = 4;
			break;
		default:
			printf("[Networking]Camera %s ERROR: UNKNOWN MACHINE STATUS.\n", si->camera_name);
			waiting_bytes = 4;
			break;
		}
	}
	return 0;
}
DWORD WINAPI file_recv_thread(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	si->is_starting = 2;
	printf("FILE read thread start.\n");
	int len;
	char *buf;
	buf = (char *)malloc(1024*1024);
	int count = 0;
	int offset;
	FILE *fp;
	fopen_s(&fp, si->local_filename, "rb");
	//fread(buf, 1, 8, fp);
	while ((len = fread(buf, 1, 1024 * 1024, fp)) > 0)
	{
		offset = si->smi.recv_offset % (si->smi.buf_len);
		if ((len + offset) < si->smi.buf_len)
		{
			memcpy(si->smi.buf + offset, buf, len);
		}
		else
		{
			memcpy(si->smi.buf + offset, buf, si->smi.buf_len - offset);
			memcpy(si->smi.buf, buf + (si->smi.buf_len - offset), len - (si->smi.buf_len - offset));
		}
		si->smi.recv_offset += len;
		Sleep(1000);
	}
	fclose(fp);
	printf("file read done.\n");
	while (1)
		Sleep(1000);
	return 0;
}
extern char cvepano_version_str[64];
#define RECV_LEN_ONE_PACKET (32*1024)
#define USE_TCP_TRANSPORT
void recv_from_net(LPVOID lpParameter)
{
	SOCKET *sock;
	sockaddr_in *sock_addrs;
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	int count = 0;
	DWORD tick = ::GetTickCount64();
	DWORD tick_old = tick;
	int bytes = 0;

	char str[256];
	int nRecvBuf = 32 * 1024 * 1024;//设置成32M
	int nSendBuf = 1 * 1024 * 1024;
	sock = &si->sock_data;
	memset(&si->sock_data_addrs, 0, sizeof(SOCKADDR));
	si->sock_data_addrs.sin_family = AF_INET;
	si->sock_data_addrs.sin_port = htons(si->iport);
	printf("IP: %s PORT: %d\n", si->server_ipstr, si->iport);
	inet_pton(AF_INET, si->server_ipstr, &si->sock_data_addrs.sin_addr);
	*sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (*sock == INVALID_SOCKET)
	{
		::MessageBoxA(GetParent(NULL), "[Networking]create socket failed\n", "ERROR", MB_OK);
		return;
	}
	else
	{
		int recvTimeout = 10 * 1000;  //5
		int sendTimeout = 10 * 1000;  //5

		setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&recvTimeout, sizeof(int));
		setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&sendTimeout, sizeof(int));
		setsockopt(*sock, SOL_SOCKET, SO_RCVBUF, (const char *)&nRecvBuf, sizeof(nRecvBuf));
		setsockopt(*sock, SOL_SOCKET, SO_SNDBUF, (const char *)&nSendBuf, sizeof(nSendBuf));
		int addr_len = sizeof(struct sockaddr_in);
		if (connect(*sock, (struct sockaddr *)&si->sock_data_addrs, addr_len) == SOCKET_ERROR)
		{
			closesocket(*sock);
			printf("[Networking]Camera %s connect ERROR %d@%d.\n", si->camera_name, errno, si->iport);
			return;
		}

		int len = RECV_LEN_ONE_PACKET;
		unsigned int offset = 0;
		char buf[32 * 1024];
		printf("[Networking]Camera %s Start to connect %d.\n", si->camera_name, si->iport);
		len = send(*sock, "start", 5, 0);

		if (len < 0)
		{
			printf("[Networking]Camera %s  failed to start. errno is %d! \n", si->camera_name, GetLastError());
		}
		Sleep(300);

		len = 32 * 1024;
		int ret = 0;
		int retry = 0;
		while (si->is_starting == 2)
		{
			offset = si->smi.recv_offset % (si->smi.buf_len);
			len = recv(*sock, buf, RECV_LEN_ONE_PACKET, 0);
			ret = GetLastError();
			if (len == -1)
			{
				printf("[Networking]Camera %s NETRECEIVE_ERROR=%d\n", si->camera_name, ret);
				char str[256];

				if (ret == WSAECONNRESET)
				{
					sprintf_s(str, "[Networking]Camera %s ip %s port %d connection reset.", si->camera_name, si->server_ipstr, si->iport);
					printf("%s\n", str);
					//::MessageBoxA(NULL, str, "连接错误", MB_OK);
				}
				else
					if (ret == WSAETIMEDOUT)
					{
						sprintf_s(str, "[Networking]Camera %s ip %s port %d connection timeout.", si->camera_name, si->server_ipstr, si->iport);
						printf("%s\n", str);
						//::MessageBoxA(NULL, str, "连接错误", MB_OK);
					}
					else
					{
						sprintf_s(str, "[Networking]Camera %s ip %s port %d connection error unknown.", si->camera_name, si->server_ipstr, si->iport);
						printf("%s\n", str);
						break;
					}
				if (retry > 3)
				{
					retry = 0;
					exit(0);
				}
				retry++;
				Sleep(300);
				continue;
			}
			if (len == 0)
				continue;
			//fwrite(buf,1,len,fp_bigframe);
			//fflush(fp_bigframe);
			if ((len + offset) < si->smi.buf_len)
			{
				memcpy(si->smi.buf + offset, buf, len);
			}
			else
			{
				memcpy(si->smi.buf + offset, buf, si->smi.buf_len - offset);
				memcpy(si->smi.buf, buf + (si->smi.buf_len - offset), len - (si->smi.buf_len - offset));
			}
			si->smi.recv_offset += len;
			bytes += len;
		}
		
		closesocket(*sock);
	}
	return;
}

DWORD WINAPI networking_recv_thread(LPVOID lpParameter)
{
	SERVER_INFO *si = (SERVER_INFO *)lpParameter;
	si->is_starting = 2;
	recv_from_net(lpParameter);
	return 0;
}
