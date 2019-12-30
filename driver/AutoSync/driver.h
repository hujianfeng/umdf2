/*

Copyright (c) 1990-2000  Microsoft Corporation

Module Name:

    driver.h

Abstract:

    This is a C version of a very simple sample driver that illustrates
    how to use the driver framework and demonstrates best practices.
	这是一个非常简单的C版本驱动程序示例，它说明了如何使用驱动程序框架并演示最佳实践。

*/

#define INITGUID

#include <windows.h>
#include <wdf.h>
#include "device.h"
#include "queue.h"

#ifndef ASSERT
#if DBG
#define ASSERT( exp ) \
    ((!(exp)) ? \
        (KdPrint(( "\n*** Assertion failed: " #exp "\n\n")), \
         DebugBreak(), \
         FALSE) : \
        TRUE)
#else
#define ASSERT( exp )
#endif // DBG
#endif // ASSERT

//
// WDFDRIVER Events
// WDF驱动事件
//
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD EchoEvtDeviceAdd;

NTSTATUS EchoPrintDriverVersion();

