/*
 * FT600 Data Streamer Demo App
 *
 * Copyright (C) 2016 FTDI Chip
 *
 */

#pragma once
#include <initguid.h>

#define DEVICE_VID							_T("0403")
#define DEFAULT_VALUE_OPENBY_DESC			"FTDI SuperSpeed-FIFO Bridge"
#define DEFAULT_VALUE_OPENBY_SERIAL			"000000000001"
#define DEFAULT_VALUE_OPENBY_INDEX			"0"

#define APPLICATION_LOG_FILE_NAME           _T("FT600DataStreamer.txt")
#define APPLICATION_TITLE                   _T("FT600 Data Streamer")

//#define DISALLOW_CLOSING    1
#define READ_OR_WRITE_ONLY  1
#define USE_ASYNCHRONOUS    1

// {D1E8FE6A-AB75-4D9E-97D2-06FA22C7736C} // D3XX
DEFINE_GUID(GUID_DEVINTERFACE_FOR_D3XX,
	0xd1e8fe6a, 0xab75, 0x4d9e, 0x97, 0xd2, 0x6, 0xfa, 0x22, 0xc7, 0x73, 0x6c);

#define FT600_VID   0x0403
#define FT600_PID   0x601E
#define FT601_PID   0x601F


#define NUM_EP_PAIRS     1
#define BASE_WRITE_EP    0X02
#define BASE_READ_EP     0X82

#if USE_ASYNCHRONOUS
#define MAX_QUEUE_SIZE_CH1                  128
#define MAX_PAYLOAD_SIZE_CH1_USB3           1024*1024 // 256K
#define MAX_PAYLOAD_SIZE_CH1_USB2           1024*1024 // 256K
#else
#define MAX_QUEUE_SIZE_CH1                  16
#define MAX_PAYLOAD_SIZE_CH1_USB3           16*1024*1024 // 256K
#define MAX_PAYLOAD_SIZE_CH1_USB2           4*1024*1024 // 256K

#endif

