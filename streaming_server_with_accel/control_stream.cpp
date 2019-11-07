#include "stdafx.h"

#include "stitcher_stream_server.h"
#pragma comment(lib, "ws2_32.lib")

/*
cve监听协议端口是10512
第一步：添加摄像头
{
"type":"AddSetCameraName",
"para":
[
{"ip":"192.168.10.120", "name":"53"}
]
}

{"type":"AddSetCameraName","para":[{"ip":"192.168.10.120", "name":"53"}]}

第二步：申请码流
{
"type":"StartBitStream",
"para":
{
"ip":"192.168.10.120",
"uuid":"155496021771847",
"type":"0", //0 for main stream 1 for sub stream
}
}

{"type":"StartBitStream","para":{"ip":"192.168.10.120","uuid":"155496021771847","type":"0",}}

如果成功返回：
{
"type":"StartBitStream",
"result":0,
"msg":"success",
"data":
{
"ip":"192.168.10.55", //码流发送的ip地址
"port":"10555",//码流发送监听的端口号
"ip_camera":"192.168.10.120", //摄像头的ip地址
"type":"0" //0 for main stream 1 for sub stream
}
}

关闭码流
{
"type":"StopBitStream",
"para":
{
"ip":"192.168.10.120",
"uuid":"155496021771847",//uuid字符串跟申请的时候保持一直
"type":"0", //0 for main stream 1 for sub stream
}
}

{"type":"StopBitStream","para":{"ip":"192.168.10.120","uuid":"155496021771847","type":"0",}}

*/ 

int stream_control(SERVER_INFO *si, int cmd, void *param)
{
	char path[256];
	memset(path, 0xff, 256);
	int count = 0;
	DWORD tick = ::GetTickCount64();
	DWORD tick_old = tick;
	int bytes = 0;
	int len;
	int ret;
	char buf[32 * 1024];
	if (cmd == CMD_STREAM_CONTROL_OPEN)
	{
		memset(&si->sock_control_addrs, 0, sizeof(SOCKADDR));
		si->sock_control_addrs.sin_family = AF_INET;
		si->sock_control_addrs.sin_port = htons(10512);
		inet_pton(AF_INET, si->server_ipstr, &si->sock_control_addrs.sin_addr);
		si->sock_control = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (si->sock_control == INVALID_SOCKET)
		{
			::MessageBoxA(GetParent(NULL), "[Networking]create socket failed\n", "ERROR", MB_OK);
			printf("sock=%d, error=%d \n", si->sock_control, WSAGetLastError());
			return -1;
		}
		else
		{
			int recvTimeout = 10 * 1000;  //5
			int sendTimeout = 10 * 1000;  //5

			setsockopt(si->sock_control, SOL_SOCKET, SO_RCVTIMEO, (char *)&recvTimeout, sizeof(int));
			setsockopt(si->sock_control, SOL_SOCKET, SO_SNDTIMEO, (char *)&sendTimeout, sizeof(int));
			int addr_len = sizeof(struct sockaddr_in);
			if (connect(si->sock_control, (struct sockaddr *)&si->sock_control_addrs, addr_len) == SOCKET_ERROR)
			{
				printf("sock=%d, error=%d \n", si->sock_control, WSAGetLastError());
				closesocket(si->sock_control);
				return -1;
			}

			
			len = sprintf_s(buf, "{\"type\":\"AddSetCameraName\",\"para\":[{\"ip\":\"%s\", \"name\":\"%s\"}]}", si->iipstr, si->camera_name);
			len = send(si->sock_control, buf, len, 0);
			if (len < 0)
			{
				printf("[Networking]Camera  failed to add. errno is %d! \n", GetLastError());
			}
			Sleep(1);
			len = recv(si->sock_control, buf, 32 * 1024, 0);
			ret = GetLastError();
			if (len == -1)
			{
				printf("[Networking]Camera NETRECEIVE_ERROR=%d\n", ret);
				char str[256];

				if (ret == WSAECONNRESET)
				{
					sprintf_s(str, "[Networking]Camera %s ip %s port %d reset.", si->camera_name, si->iipstr, si->iport);
					printf("%s\n", str);
					//::MessageBoxA(NULL, str, "连接错误", MB_OK);
				}
				else if (ret == WSAETIMEDOUT)
				{
					sprintf_s(str, "[Networking]Camera %s ip %s port %d timeout.", si->camera_name, si->iipstr, si->iport);
					printf("%s\n", str);
					//::MessageBoxA(NULL, str, "连接错误", MB_OK);
				}
				else
				{
					sprintf_s(str, "[Networking]Camera %s ip %s port %d error unknown.", si->camera_name, si->iipstr, si->iport);
					printf("%s\n", str);
				}
			}
			else
			{
				buf[len] = 0;
				if (strstr(buf, "success") != NULL)
				{
					printf("open stream success %s@%d\n", si->iipstr, si->iport);
				}
				else
				{
					printf("open stream failed %s@%d\n", si->iipstr, si->iport);
				}
				
			}

		}
	}
	else if (cmd == CMD_STREAM_CONTROL_START)
	{
		len = sprintf_s(buf, "{\"type\":\"StartBitStream\",\"para\":{\"ip\":\"%s\",\"uuid\":\"%llu\",\"type\":\"%d\"}}", si->iipstr, si->uuid, si->stream_id);
		len = send(si->sock_control, buf, len, 0);
		if (len < 0)
		{
			printf("[Networking]Camera  failed to add. errno is %d! \n", GetLastError());
		}
		Sleep(1);
		len = recv(si->sock_control, buf, 32 * 1024, 0);
		ret = GetLastError();
		if (len == -1)
		{
			printf("[Networking]Camera NETRECEIVE_ERROR=%d\n", ret);
			char str[256];

			if (ret == WSAECONNRESET)
			{
				sprintf_s(str, "[Networking]Camera %s ip %s port %d reset.", si->camera_name, si->iipstr, si->iport);
				printf("%s\n", str);
				//::MessageBoxA(NULL, str, "连接错误", MB_OK);
			}
			else if (ret == WSAETIMEDOUT)
			{
				sprintf_s(str, "[Networking]Camera %s ip %s port %d timeout.", si->camera_name, si->iipstr, si->iport);
				printf("%s\n", str);
				//::MessageBoxA(NULL, str, "连接错误", MB_OK);
			}
			else
			{
				sprintf_s(str, "[Networking]Camera %s ip %s port %d error unknown.", si->camera_name, si->iipstr, si->iport);
				printf("%s\n", str);
			}
		}
		else
		{
			buf[len] = 0;
			if (strstr(buf, "success") != NULL)
			{
				char *tmp,*tmp1;
				tmp = strstr(buf, "port");
				tmp += 8;
				tmp1 = strstr(tmp,"\"");
				memcpy(&buf[4096], tmp, tmp1 - tmp);
				buf[4096 + (tmp1 - tmp)] = 0;
				si->iport = atoi(&buf[4096]);
				if (si->iport <= 0)
				{
					printf("port(%d) invalid. Maybe device is not ready. Restart...\n", si->iport);
					exit(-10);
				}
			}
			printf("start stream %s@%d\n", si->iipstr, si->iport);
		}
	}
	else if(cmd == CMD_STREAM_CONTROL_CLOSE)
	{
		closesocket(si->sock_control);
	}
	else
	{
		printf("Unknown Command.\n");
	}
	return 0;
}