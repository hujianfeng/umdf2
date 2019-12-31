/*

Copyright (c) 1990-2000  Microsoft Corporation

Module Name:

    queue.c

Abstract:

    This is a C version of a very simple sample driver that illustrates
    how to use the driver framework and demonstrates best practices.
    这是一个非常简单的C版本驱动程序示例，它说明了如何使用驱动程序框架并演示最佳实践。

*/

#include "driver.h"

/*
Function:
    EchoQueueInitialize
    队列初始化，由EchoDeviceCreate调用。

Routine Description:

    The I/O dispatch callbacks for the frameworks device object
    are configured in this function.
    在此函数中, 配置框架设备对象的I/O调度回调。

    A single default I/O Queue is configured for serial request
    processing, and a driver context memory allocation is created
    to hold our structure QUEUE_CONTEXT.
    配置了单个默认的I/O队列用于串行请求处理，并创建了一个驱动程序
    上下文内存分配来保存我们的结构QUEUE_CONTEXT。

    This memory may be used by the driver automatically synchronized
    by the Queue's presentation lock.
    驱动程序可以使用此内存，该内存由队列的演示文稿锁自动同步。

    The lifetime of this memory is tied to the lifetime of the I/O
    Queue object, and we register an optional destructor callback
    to release any private allocations, and/or resources.
    该内存的生存期与I/O队列对象的生存期相关，我们注册了一个可选的
    析构函数回调以释放所有私有分配和/或资源。

Arguments:

    Device - Handle to a framework device object.
             框架设备对象句柄

Return Value:

	NTSTATUS
*/
NTSTATUS EchoQueueInitialize(WDFDEVICE Device)
{
    KdPrint(("EchoQueueInitialize\n"));

    WDFQUEUE queue;
    NTSTATUS status;
    PQUEUE_CONTEXT queueContext;
    WDF_IO_QUEUE_CONFIG    queueConfig;
    WDF_OBJECT_ATTRIBUTES  queueAttributes;

    //
    // Configure a default queue so that requests that are not
    // configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
    // other queues get dispatched here.
    // 配置缺省队列，以便使用WdfDeviceConfigureRequestDispatching转到其他队列
    // 的未进行配置转发的请求在此处分派。
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential
        );

    // 注册队列读写回调
    queueConfig.EvtIoRead   = EchoEvtIoRead;
    queueConfig.EvtIoWrite  = EchoEvtIoWrite;

    //
    // Fill in a callback for destroy, and our QUEUE_CONTEXT size
    // 填写要销毁的回调，以及我们的QUEUE_CONTEXT大小
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttributes, QUEUE_CONTEXT);

    //
    // Set synchronization scope on queue and have the timer to use queue as
    // the parent object so that queue and timer callbacks are synchronized
    // with the same lock.
    // 在队列上设置同步作用域，并让计时器将队列用作父对象，
    // 以便使用相同的锁同步队列和计时器回调。
    //
    queueAttributes.SynchronizationScope = WdfSynchronizationScopeQueue;
    queueAttributes.EvtDestroyCallback = EchoEvtIoQueueContextDestroy;

    KdPrint(("WdfIoQueueCreate\n"));
    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 &queueAttributes,
                 &queue
                 );

    if( !NT_SUCCESS(status) ) {
        KdPrint(("WdfIoQueueCreate failed 0x%x\n",status));
        return status;
    }

    // Get our Driver Context memory from the returned Queue handle
    // 从返回的队列句柄获取我们的驱动程序上下文内存
    queueContext = QueueGetContext(queue);
    queueContext->WriteMemory = NULL;
    queueContext->Timer = NULL;
    queueContext->CurrentRequest = NULL;
    queueContext->CurrentStatus = STATUS_INVALID_DEVICE_REQUEST;

    //
    // Create the Queue timer
    // 创建队列计时器
    //
    KdPrint(("EchoTimerCreate\n"));
    status = EchoTimerCreate(&queueContext->Timer, queue);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Error creating timer 0x%x\n",status));
        return status;
    }

    return status;
}

/*
Function:
    EchoEvtIoRead
    I/O读回调

Routine Description:

    This event is called when the framework receives IRP_MJ_READ request.
    It will copy the content from the queue-context buffer to the request buffer.
    If the driver hasn't received any write request earlier, the read returns zero.
    框架收到IRP_MJ_READ请求时将调用此事件，它将内容从队列上下文缓冲区复制到请求缓冲区。
    如果驱动程序之前未收到任何写请求，则读取返回零。

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.
             处理与I/O请求关联的框架队列对象

    Request - Handle to a framework request object.
              框架请求对象句柄

    Length  - number of bytes to be read.
              The default property of the queue is to not dispatch
              zero lenght read & write requests to the driver and
              complete is with status success. So we will never get
              a zero length request.
              要读取的字节数。
              队列的默认属性是不将零长度的读写请求分派给驱动程序，
              而完成状态成功。因此，我们永远不会收到长度为零的请求。

Return Value:

    VOID
*/
VOID EchoEvtIoRead(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
)
{
    KdPrint(("EchoEvtIoRead\n"));

    NTSTATUS Status;
    PQUEUE_CONTEXT queueContext = QueueGetContext(Queue);
    WDFMEMORY memory;
    size_t writeMemoryLength;

    _Analysis_assume_(Length > 0);

    KdPrint(("EchoEvtIoRead Called! Queue 0x%p, Request 0x%p Length %d\n",
        Queue, Request, Length));
    //
    // No data to read
    // 没有数据可读取
    //
    if ((queueContext->WriteMemory == NULL)) {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, (ULONG_PTR)0L);
        return;
    }

    //
    // Read what we have
    // 有数据时读
    //
    WdfMemoryGetBuffer(queueContext->WriteMemory, &writeMemoryLength);
    _Analysis_assume_(writeMemoryLength > 0);

    if (writeMemoryLength < Length) {
        Length = writeMemoryLength;
    }

    //
    // Get the request memory
    // 获取请求内存
    //
    Status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("EchoEvtIoRead Could not get request memory buffer 0x%x\n", Status));
        WdfVerifierDbgBreakPoint();
        WdfRequestCompleteWithInformation(Request, Status, 0L);

        return;
    }

    // Copy the memory out
    // 复制内存
    Status = WdfMemoryCopyFromBuffer(memory,    // destination
        0,         // offset into the destination memory
        WdfMemoryGetBuffer(queueContext->WriteMemory, NULL),
        Length
    );
    if (!NT_SUCCESS(Status)) {
        KdPrint(("EchoEvtIoRead: WdfMemoryCopyFromBuffer failed 0x%x\n", Status));
        WdfRequestComplete(Request, Status);
        return;
    }

    // Set transfer information
    // 设置传输信息
    WdfRequestSetInformation(Request, (ULONG_PTR)Length);

    // Mark the request is cancelable
    // 将请求标记为可取消
    WdfRequestMarkCancelable(Request, EchoEvtRequestCancel);

    // Defer the completion to another thread from the timer dpc
    // 将完成时间从计时器dpc推迟到另一个线程
    queueContext->CurrentRequest = Request;
    queueContext->CurrentStatus = Status;

    return;
}

/*
Function:
    EchoEvtIoWrite
    I/O写回调

Routine Description:

    This event is invoked when the framework receives IRP_MJ_WRITE request.
    This routine allocates memory buffer, copies the data from the request to it,
    and stores the buffer pointer in the queue-context with the length variable
    representing the buffers length. The actual completion of the request
    is defered to the periodic timer dpc.
    当框架收到IRP_MJ_WRITE请求时，将调用此事件。该例程分配内存缓冲区，将数据从请求
    复制到该缓冲区，然后将缓冲区指针存储在队列上下文中，其中length变量表示缓冲区的
    长度。请求的实际完成将延迟到定期计时器dpc。

Arguments:

    Queue -  Handle to the framework queue object that is associated with the I/O request.
             与I/O请求相关的框架队列对象句柄

    Request - Handle to a framework request object.
              框架请求对象句柄

    Length  - number of bytes to be read.
              The default property of the queue is to not dispatch
              zero lenght read & write requests to the driver and
              complete is with status success. So we will never get
              a zero length request.
              要读取的字节数
              队列的默认属性是不将零长度的读写请求分派给驱动程序，
              而完成状态成功。 因此，我们永远不会收到长度为零的请求。

Return Value:

    VOID
*/
VOID EchoEvtIoWrite(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
)
{
    KdPrint(("EchoEvtIoWrite\n"));

    NTSTATUS Status;
    WDFMEMORY memory;
    PQUEUE_CONTEXT queueContext = QueueGetContext(Queue);
    PVOID writeBuffer = NULL;

    _Analysis_assume_(Length > 0);

    KdPrint(("EchoEvtIoWrite Called! Queue 0x%p, Request 0x%p Length %d\n",
        Queue, Request, Length));

    if (Length > MAX_WRITE_LENGTH) {
        KdPrint(("EchoEvtIoWrite Buffer Length to big %d, Max is %d\n",
            Length, MAX_WRITE_LENGTH));
        WdfRequestCompleteWithInformation(Request, STATUS_BUFFER_OVERFLOW, 0L);
        return;
    }

    // Get the memory buffer
    // 获取内存缓冲区
    Status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("EchoEvtIoWrite Could not get request memory buffer 0x%x\n",
            Status));
        WdfVerifierDbgBreakPoint();
        WdfRequestComplete(Request, Status);
        return;
    }

    // Release previous buffer if set
    // 如果设置释放前一个缓冲区
    if (queueContext->WriteMemory != NULL) {
        WdfObjectDelete(queueContext->WriteMemory);
        queueContext->WriteMemory = NULL;
    }

    Status = WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES,
        NonPagedPoolNx,
        'sam1',
        Length,
        &queueContext->WriteMemory,
        &writeBuffer
    );

    if (!NT_SUCCESS(Status)) {
        KdPrint(("EchoEvtIoWrite: Could not allocate %d byte buffer\n", Length));
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    // Copy the memory in
    // 复制内存
    Status = WdfMemoryCopyToBuffer(memory,
        0,  // offset into the source memory
        writeBuffer,
        Length);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("EchoEvtIoWrite WdfMemoryCopyToBuffer failed 0x%x\n", Status));
        WdfVerifierDbgBreakPoint();

        WdfObjectDelete(queueContext->WriteMemory);
        queueContext->WriteMemory = NULL;

        WdfRequestComplete(Request, Status);
        return;
    }

    // Set transfer information
    // 设置传输信息
    WdfRequestSetInformation(Request, (ULONG_PTR)Length);

    // Specify the request is cancelable
    // 指定请求可取消
    WdfRequestMarkCancelable(Request, EchoEvtRequestCancel);

    // Defer the completion to another thread from the timer dpc
    // 将完成时间从计时器dpc推迟到另一个线程
    queueContext->CurrentRequest = Request;
    queueContext->CurrentStatus = Status;

    return;
}

/*
Function:
    EchoEvtRequestCancel
    请求取消

Routine Description:

    Called when an I/O request is cancelled after the driver has marked
    the request cancellable. This callback is automatically synchronized
    with the I/O callbacks since we have chosen to use frameworks Device
    level locking.
    在驱动程序标记请求可取消后取消I/O请求时调用。由于我们选择使用框架设备
    级别锁定，因此该回调将与I/O回调自动同步。

Arguments:

    Request - Request being cancelled.
              请求被取消

Return Value:

    VOID
*/
VOID EchoEvtRequestCancel(IN WDFREQUEST Request)
{
    KdPrint(("EchoEvtRequestCancel\n"));

    PQUEUE_CONTEXT queueContext = QueueGetContext(WdfRequestGetIoQueue(Request));

    KdPrint(("EchoEvtRequestCancel called on Request 0x%p\n", Request));

    //
    // The following is race free by the callside or DPC side
    // synchronizing completion by calling
    // WdfRequestMarkCancelable(Queue, Request, FALSE) before
    // completion and not calling WdfRequestComplete if the
    // return status == STATUS_CANCELLED.
    // 通过在完成之前调用WdfRequestMarkCancelable（Queue，Request，FALSE），
    // 并且在返回状态== STATUS_CANCELLED的情况下，不调用WdfRequestComplete，
    // 由调用方或DPC同步同步完成的下列操作是免费的。
    //
    WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0L);

    //
    // This book keeping is synchronized by the common
    // Queue presentation lock
    //
    ASSERT(queueContext->CurrentRequest == Request);
    queueContext->CurrentRequest = NULL;

    return;
}

/*
Function:
    EchoEvtIoQueueContextDestroy
    队列销毁回调函数

Routine Description:

    This is called when the Queue that our driver context memory
    is associated with is destroyed.
    当与我们的驱动程序上下文存储器关联的队列被销毁时，将调用此方法。

Arguments:

    Context - Context that's being freed.
              被释放的上下文。

Return Value:

    VOID
*/
VOID EchoEvtIoQueueContextDestroy(WDFOBJECT Object)
{
    KdPrint(("EchoEvtIoQueueContextDestroy\n"));

    PQUEUE_CONTEXT queueContext = QueueGetContext(Object);

    //
    // Release any resources pointed to in the queue context.
    // 释放队列上下文中指向的所有资源。
    //
    // The body of the queue context will be released after
    // this callback handler returns
    // 该回调处理程序返回后，将释放队列上下文的主体
    //

    //
    // If Queue context has an I/O buffer, release it
    // 如果队列上下文具有I/O缓冲区，请释放它
    //
    if (queueContext->WriteMemory != NULL) {
        WdfObjectDelete(queueContext->WriteMemory);
        queueContext->WriteMemory = NULL;
    }

    return;
}

/*
Function:
    EchoTimerCreate
    建立定时器, 由EchoQueueInitialize调用。

Routine Description:

    Subroutine to create timer. By associating the timerobject with
    the queue, we are basically telling the framework to serialize the queue
    callbacks with the timer callback. By doing so, we don't have to worry
    about protecting queue-context structure from multiple threads accessing
    it simultaneously.
    创建计时器的子例程。 通过将timer对象与队列相关联，我们基本上是在告诉框架
    将计时器回调与队列回调序列化。 这样，我们不必担心保护队列上下文结构免受
    多个线程同时访问的影响。

Arguments:


Return Value:

	NTSTATUS
*/
NTSTATUS EchoTimerCreate(
    IN WDFTIMER*       Timer,
    IN WDFQUEUE        Queue
    )
{
    KdPrint(("EchoTimerCreate\n"));

    NTSTATUS Status;
    WDF_TIMER_CONFIG       timerConfig;
    WDF_OBJECT_ATTRIBUTES  timerAttributes;

    //
    // Create a non-periodic timer since WDF does not allow periodic timer
    // at passive level, which is the level UMDF callbacks are invoked at.
    // The workaround is to always restart the timer in the timer callback.
    // 因为WDF不允许在被动级别（调用UMDF回调的级别）上使用周期性定时器，
    // 所以创建一个非周期性定时器。 解决方法是始终在计时器回调中重新启动计时器。
    //
    // WDF_TIMER_CONFIG_INIT sets AutomaticSerialization to TRUE by default.
    // WDF_TIMER_CONFIG_INIT 默认情况下将AutomaticSerialization设置为TRUE。
    //
    WDF_TIMER_CONFIG_INIT(&timerConfig, EchoEvtTimerFunc);

    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttributes);
    timerAttributes.ParentObject = Queue; // Synchronize with the I/O Queue
                                          // 与I/O队列同步
    timerAttributes.ExecutionLevel = WdfExecutionLevelPassive;

    Status = WdfTimerCreate(
        &timerConfig,
        &timerAttributes,
        Timer     // Output handle
    );

    return Status;
}

/*
Function:
    EchoEvtTimerFunc
    定时器回调

Routine Description:

    This is the TimerDPC the driver sets up to complete requests.
    This function is registered when the WDFTIMER object is created, and
    will automatically synchronize with the I/O Queue callbacks
    and cancel routine.
    这是驱动程序设置为完成请求的TimerDPC。创建WDFTIMER对象时将注册此函数，
    该函数将自动与I/O队列回调和取消例程同步。

Arguments:

    Timer - Handle to a framework Timer object.
            框架Timer对象句柄

Return Value:

	VOID
*/
VOID EchoEvtTimerFunc(IN WDFTIMER  Timer)
{
    KdPrint(("EchoEvtTimerFunc\n"));

    NTSTATUS    Status;
    WDFREQUEST  Request;
    WDFQUEUE  queue;
    PQUEUE_CONTEXT  queueContext;

    queue = WdfTimerGetParentObject(Timer);
    queueContext = QueueGetContext(queue);

    //
    // DPC is automatically synchronized to the Queue lock,
    // so this is race free without explicit driver managed locking.
    // DPC会自动同步到队列锁，因此无需明确的驱动程序管理的锁，它就不会出现冲突。
    //
    Request = queueContext->CurrentRequest;
    if (Request != NULL) {

        //
        // Attempt to remove cancel status from the request.
        // 尝试从请求中删除取消状态。
        //
        // The request is not completed if it is already cancelled
        // since the EchoEvtIoCancel function has run, or is about to run
        // and we are racing with it.
        // 如果由于EchoEvtIoCancel函数已运行或将要运行并且我们正在与之竞争，
        // 则该请求已被取消，则该请求尚未完成。
        //
        Status = WdfRequestUnmarkCancelable(Request);
        if( Status != STATUS_CANCELLED ) {

            queueContext->CurrentRequest = NULL;
            Status = queueContext->CurrentStatus;

            KdPrint(("CustomTimerDPC Completing request 0x%p, Status 0x%x \n", Request, Status));

            WdfRequestComplete(Request, Status);
        }
        else {
            KdPrint(("CustomTimerDPC Request 0x%p is STATUS_CANCELLED, not completing\n", Request));
        }
    }

    //
    // Restart the Timer since WDF does not allow periodic timer
    // with autosynchronization at passive level
    // 由于WDF不允许周期性计时器在被动级别进行自动同步，因此重新启动计时器
    //
    WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(TIMER_PERIOD));

    return;
}

