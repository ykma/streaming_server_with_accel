/************************************************************
** 文件: PrivProto.h
** 描述:
************************************************************/
#ifndef _PRIVPROTO_H
#define _PRIVPROTO_H
//
#include "stdint.h"

#define MAIN_STREAM_TCPPORT     (10460)
#define SUB_STREAM_TCPPORT      (10462)
#define ULTRA_STREAM_TCPPORT	(10464)
#define MAGIC_VALUE             (0x8888)

// 通信数据包格式定义...
// 包(package) = 头(commhdr_t) + 数据体(body)
// 通信数据头定义
typedef struct _cmdhdr_t {
    uint32_t size;                  /* 通信包大小, = sizeof(comhdr_t) + bodysize */
    uint16_t cmdid;                 /* 只传送视频数据，以备后续扩展，这里无需解析*/
    uint16_t magic;                 /* 校验值*/
} cmdhdr_t;

typedef struct _cmdbody_vframe_t {
    uint32_t    frameId;
    uint32_t    lengthOfEachPipe[8];/* 每个通道帧长度*/
    uint8_t     data[1];            /* 按0-7号通道排列的帧数据 */
} cmdbody_vframe_t;

#endif
#if 0
typedef struct _cmdhdr_t {
	uint16_t checksum;					/* _cmdhdr_t校验和*/
	uint16_t magic;                 /* magic key "0x5aa5"*/
    uint32_t cmdid:8;                 /* 只传送视频数据，以备后续扩展，这里无需解析*/
	uint32_t size:24;                  /* 通信包大小, = sizeof(comhdr_t) + bodysize */
} cmdhdr_t;
#endif