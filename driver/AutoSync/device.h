/*++

Copyright (c) 1990-2000  Microsoft Corporation

Module Name:

    device.h

Abstract:

    This is a C version of a very simple sample driver that illustrates
    how to use the driver framework and demonstrates best practices.
	这是一个非常简单的C版本驱动程序示例，它说明了如何使用驱动程序框架并演示最佳实践。

--*/

#include "public.h"

//
// The device context performs the same job as
// a WDM device extension in the driver frameworks
//
typedef struct _DEVICE_CONTEXT
{
    ULONG PrivateDeviceData;  // just a placeholder

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

//
// This macro will generate an inline function called WdfObjectGet_DEVICE_CONTEXT
// which will be used to get a pointer to the device context memory
// in a type safe manner.
// 该宏将生成一个名为WdfObjectGet_DEVICE_CONTEXT的内联函数，该函数将以类型安全的方式获取指向设备上下文内存的指针。
//
WDF_DECLARE_CONTEXT_TYPE(DEVICE_CONTEXT)

//
// Function to initialize the device and its callbacks
// 初始化设备及其回调的函数
//
NTSTATUS EchoDeviceCreate(PWDFDEVICE_INIT DeviceInit);

//
// Device events
// 设备事件
//
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT EchoEvtDeviceSelfManagedIoStart;

EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND EchoEvtDeviceSelfManagedIoSuspend;

