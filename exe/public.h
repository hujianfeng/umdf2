/*++
Copyright (c) 1990-2000    Microsoft Corporation All Rights Reserved

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.
    该模块包含驱动程序和用户应用程序共享的通用声明。

Environment:

    user and kernel
    用户与内核

--*/

#pragma once

#define WHILE(a) \
__pragma(warning(suppress:4127)) while(a)

//
// Define an Interface Guid so that app can find the device and talk to it.
// 定义GUID，以便应用程序可以找到设备并与之对话。
//

DEFINE_GUID (GUID_DEVINTERFACE_ECHO,
    0xcdc35b6e, 0xbe4, 0x4936, 0xbf, 0x5f, 0x55, 0x37, 0x38, 0xa, 0x7c, 0x1a);
// {CDC35B6E-0BE4-4936-BF5F-5537380A7C1A}

