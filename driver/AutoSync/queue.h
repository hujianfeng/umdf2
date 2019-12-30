/*

Copyright (c) 1990-2000  Microsoft Corporation

Module Name:

    queue.h

Abstract:

    This is a C version of a very simple sample driver that illustrates
    how to use the driver framework and demonstrates best practices.
    这是一个非常简单的C版本驱动程序示例，它说明了如何使用驱动程序框架并演示最佳实践。

*/

// Set max write length for testing
// 设置最大写入长度以进行测试
#define MAX_WRITE_LENGTH  1024*40

// Set timer period in ms
// 以毫秒为单位设置计时器周期
#define TIMER_PERIOD  1000*2

//
// This is the context that can be placed per queue
// and would contain per queue information.
// 这是可以按队列放置的上下文，并且将包含按队列的信息。
//
typedef struct _QUEUE_CONTEXT {

    // Here we allocate a buffer from a test write so it can be read back
    // 在这里，我们从测试写入中分配一个缓冲区，以便可以将其读回
    WDFMEMORY WriteMemory;

    // Timer DPC for this queue
    // 此队列的计时器DPC
    WDFTIMER   Timer;

    // Virtual I/O
    // 虚拟I/O
    WDFREQUEST  CurrentRequest;
    NTSTATUS    CurrentStatus;

} QUEUE_CONTEXT, *PQUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, QueueGetContext)

NTSTATUS EchoQueueInitialize(WDFDEVICE hDevice);

EVT_WDF_IO_QUEUE_CONTEXT_DESTROY_CALLBACK EchoEvtIoQueueContextDestroy;

//
// Events from the IoQueue object
// 来自IoQueue对象的事件
//
EVT_WDF_REQUEST_CANCEL EchoEvtRequestCancel;

EVT_WDF_IO_QUEUE_IO_READ EchoEvtIoRead;

EVT_WDF_IO_QUEUE_IO_WRITE EchoEvtIoWrite;

NTSTATUS EchoTimerCreate(IN WDFTIMER* pTimer, IN WDFQUEUE Queue);

EVT_WDF_TIMER EchoEvtTimerFunc;
