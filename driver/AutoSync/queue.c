/*

Copyright (c) 1990-2000  Microsoft Corporation

Module Name:

    queue.c

Abstract:

    This is a C version of a very simple sample driver that illustrates
    how to use the driver framework and demonstrates best practices.
    ����һ���ǳ��򵥵�C�汾��������ʾ������˵�������ʹ�����������ܲ���ʾ���ʵ����

*/

#include "driver.h"

/*
Function:
    EchoQueueInitialize
    ���г�ʼ������EchoDeviceCreate���á�

Routine Description:

    The I/O dispatch callbacks for the frameworks device object
    are configured in this function.
    �ڴ˺�����, ���ÿ���豸�����I/O���Ȼص���

    A single default I/O Queue is configured for serial request
    processing, and a driver context memory allocation is created
    to hold our structure QUEUE_CONTEXT.
    �����˵���Ĭ�ϵ�I/O�������ڴ�����������������һ����������
    �������ڴ�������������ǵĽṹQUEUE_CONTEXT��

    This memory may be used by the driver automatically synchronized
    by the Queue's presentation lock.
    �����������ʹ�ô��ڴ棬���ڴ��ɶ��е���ʾ�ĸ����Զ�ͬ����

    The lifetime of this memory is tied to the lifetime of the I/O
    Queue object, and we register an optional destructor callback
    to release any private allocations, and/or resources.
    ���ڴ����������I/O���ж������������أ�����ע����һ����ѡ��
    ���������ص����ͷ�����˽�з����/����Դ��

Arguments:

    Device - Handle to a framework device object.
             ����豸������

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
    // ����ȱʡ���У��Ա�ʹ��WdfDeviceConfigureRequestDispatchingת����������
    // ��δ��������ת���������ڴ˴����ɡ�
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential
        );

    // ע����ж�д�ص�
    queueConfig.EvtIoRead   = EchoEvtIoRead;
    queueConfig.EvtIoWrite  = EchoEvtIoWrite;

    //
    // Fill in a callback for destroy, and our QUEUE_CONTEXT size
    // ��дҪ���ٵĻص����Լ����ǵ�QUEUE_CONTEXT��С
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&queueAttributes, QUEUE_CONTEXT);

    //
    // Set synchronization scope on queue and have the timer to use queue as
    // the parent object so that queue and timer callbacks are synchronized
    // with the same lock.
    // �ڶ���������ͬ�������򣬲��ü�ʱ������������������
    // �Ա�ʹ����ͬ����ͬ�����кͼ�ʱ���ص���
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
    // �ӷ��صĶ��о����ȡ���ǵ����������������ڴ�
    queueContext = QueueGetContext(queue);
    queueContext->WriteMemory = NULL;
    queueContext->Timer = NULL;
    queueContext->CurrentRequest = NULL;
    queueContext->CurrentStatus = STATUS_INVALID_DEVICE_REQUEST;

    //
    // Create the Queue timer
    // �������м�ʱ��
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
    I/O���ص�

Routine Description:

    This event is called when the framework receives IRP_MJ_READ request.
    It will copy the content from the queue-context buffer to the request buffer.
    If the driver hasn't received any write request earlier, the read returns zero.
    ����յ�IRP_MJ_READ����ʱ�����ô��¼����������ݴӶ��������Ļ��������Ƶ����󻺳�����
    �����������֮ǰδ�յ��κ�д�������ȡ�����㡣

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.
             ������I/O��������Ŀ�ܶ��ж���

    Request - Handle to a framework request object.
              ������������

    Length  - number of bytes to be read.
              The default property of the queue is to not dispatch
              zero lenght read & write requests to the driver and
              complete is with status success. So we will never get
              a zero length request.
              Ҫ��ȡ���ֽ�����
              ���е�Ĭ�������ǲ����㳤�ȵĶ�д������ɸ���������
              �����״̬�ɹ�����ˣ�������Զ�����յ�����Ϊ�������

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
    // û�����ݿɶ�ȡ
    //
    if ((queueContext->WriteMemory == NULL)) {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, (ULONG_PTR)0L);
        return;
    }

    //
    // Read what we have
    // ������ʱ��
    //
    WdfMemoryGetBuffer(queueContext->WriteMemory, &writeMemoryLength);
    _Analysis_assume_(writeMemoryLength > 0);

    if (writeMemoryLength < Length) {
        Length = writeMemoryLength;
    }

    //
    // Get the request memory
    // ��ȡ�����ڴ�
    //
    Status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("EchoEvtIoRead Could not get request memory buffer 0x%x\n", Status));
        WdfVerifierDbgBreakPoint();
        WdfRequestCompleteWithInformation(Request, Status, 0L);

        return;
    }

    // Copy the memory out
    // �����ڴ�
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
    // ���ô�����Ϣ
    WdfRequestSetInformation(Request, (ULONG_PTR)Length);

    // Mark the request is cancelable
    // ��������Ϊ��ȡ��
    WdfRequestMarkCancelable(Request, EchoEvtRequestCancel);

    // Defer the completion to another thread from the timer dpc
    // �����ʱ��Ӽ�ʱ��dpc�Ƴٵ���һ���߳�
    queueContext->CurrentRequest = Request;
    queueContext->CurrentStatus = Status;

    return;
}

/*
Function:
    EchoEvtIoWrite
    I/Oд�ص�

Routine Description:

    This event is invoked when the framework receives IRP_MJ_WRITE request.
    This routine allocates memory buffer, copies the data from the request to it,
    and stores the buffer pointer in the queue-context with the length variable
    representing the buffers length. The actual completion of the request
    is defered to the periodic timer dpc.
    ������յ�IRP_MJ_WRITE����ʱ�������ô��¼��������̷����ڴ滺�����������ݴ�����
    ���Ƶ��û�������Ȼ�󽫻�����ָ��洢�ڶ����������У�����length������ʾ��������
    ���ȡ������ʵ����ɽ��ӳٵ����ڼ�ʱ��dpc��

Arguments:

    Queue -  Handle to the framework queue object that is associated with the I/O request.
             ��I/O������صĿ�ܶ��ж�����

    Request - Handle to a framework request object.
              ������������

    Length  - number of bytes to be read.
              The default property of the queue is to not dispatch
              zero lenght read & write requests to the driver and
              complete is with status success. So we will never get
              a zero length request.
              Ҫ��ȡ���ֽ���
              ���е�Ĭ�������ǲ����㳤�ȵĶ�д������ɸ���������
              �����״̬�ɹ��� ��ˣ�������Զ�����յ�����Ϊ�������

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
    // ��ȡ�ڴ滺����
    Status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("EchoEvtIoWrite Could not get request memory buffer 0x%x\n",
            Status));
        WdfVerifierDbgBreakPoint();
        WdfRequestComplete(Request, Status);
        return;
    }

    // Release previous buffer if set
    // ��������ͷ�ǰһ��������
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
    // �����ڴ�
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
    // ���ô�����Ϣ
    WdfRequestSetInformation(Request, (ULONG_PTR)Length);

    // Specify the request is cancelable
    // ָ�������ȡ��
    WdfRequestMarkCancelable(Request, EchoEvtRequestCancel);

    // Defer the completion to another thread from the timer dpc
    // �����ʱ��Ӽ�ʱ��dpc�Ƴٵ���һ���߳�
    queueContext->CurrentRequest = Request;
    queueContext->CurrentStatus = Status;

    return;
}

/*
Function:
    EchoEvtRequestCancel
    ����ȡ��

Routine Description:

    Called when an I/O request is cancelled after the driver has marked
    the request cancellable. This callback is automatically synchronized
    with the I/O callbacks since we have chosen to use frameworks Device
    level locking.
    �����������������ȡ����ȡ��I/O����ʱ���á���������ѡ��ʹ�ÿ���豸
    ������������˸ûص�����I/O�ص��Զ�ͬ����

Arguments:

    Request - Request being cancelled.
              ����ȡ��

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
    // ͨ�������֮ǰ����WdfRequestMarkCancelable��Queue��Request��FALSE����
    // �����ڷ���״̬== STATUS_CANCELLED������£�������WdfRequestComplete��
    // �ɵ��÷���DPCͬ��ͬ����ɵ����в�������ѵġ�
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
    �������ٻص�����

Routine Description:

    This is called when the Queue that our driver context memory
    is associated with is destroyed.
    �������ǵ��������������Ĵ洢�������Ķ��б�����ʱ�������ô˷�����

Arguments:

    Context - Context that's being freed.
              ���ͷŵ������ġ�

Return Value:

    VOID
*/
VOID EchoEvtIoQueueContextDestroy(WDFOBJECT Object)
{
    KdPrint(("EchoEvtIoQueueContextDestroy\n"));

    PQUEUE_CONTEXT queueContext = QueueGetContext(Object);

    //
    // Release any resources pointed to in the queue context.
    // �ͷŶ�����������ָ���������Դ��
    //
    // The body of the queue context will be released after
    // this callback handler returns
    // �ûص�������򷵻غ󣬽��ͷŶ��������ĵ�����
    //

    //
    // If Queue context has an I/O buffer, release it
    // ������������ľ���I/O�����������ͷ���
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
    ������ʱ��, ��EchoQueueInitialize���á�

Routine Description:

    Subroutine to create timer. By associating the timerobject with
    the queue, we are basically telling the framework to serialize the queue
    callbacks with the timer callback. By doing so, we don't have to worry
    about protecting queue-context structure from multiple threads accessing
    it simultaneously.
    ������ʱ���������̡� ͨ����timer�������������������ǻ��������ڸ��߿��
    ����ʱ���ص�����лص����л��� ���������ǲ��ص��ı������������Ľṹ����
    ����߳�ͬʱ���ʵ�Ӱ�졣

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
    // ��ΪWDF�������ڱ������𣨵���UMDF�ص��ļ�����ʹ�������Զ�ʱ����
    // ���Դ���һ���������Զ�ʱ���� ���������ʼ���ڼ�ʱ���ص�������������ʱ����
    //
    // WDF_TIMER_CONFIG_INIT sets AutomaticSerialization to TRUE by default.
    // WDF_TIMER_CONFIG_INIT Ĭ������½�AutomaticSerialization����ΪTRUE��
    //
    WDF_TIMER_CONFIG_INIT(&timerConfig, EchoEvtTimerFunc);

    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttributes);
    timerAttributes.ParentObject = Queue; // Synchronize with the I/O Queue
                                          // ��I/O����ͬ��
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
    ��ʱ���ص�

Routine Description:

    This is the TimerDPC the driver sets up to complete requests.
    This function is registered when the WDFTIMER object is created, and
    will automatically synchronize with the I/O Queue callbacks
    and cancel routine.
    ����������������Ϊ��������TimerDPC������WDFTIMER����ʱ��ע��˺�����
    �ú������Զ���I/O���лص���ȡ������ͬ����

Arguments:

    Timer - Handle to a framework Timer object.
            ���Timer������

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
    // DPC���Զ�ͬ���������������������ȷ���������������������Ͳ�����ֳ�ͻ��
    //
    Request = queueContext->CurrentRequest;
    if (Request != NULL) {

        //
        // Attempt to remove cancel status from the request.
        // ���Դ�������ɾ��ȡ��״̬��
        //
        // The request is not completed if it is already cancelled
        // since the EchoEvtIoCancel function has run, or is about to run
        // and we are racing with it.
        // �������EchoEvtIoCancel���������л�Ҫ���в�������������֮������
        // ��������ѱ�ȡ�������������δ��ɡ�
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
    // ����WDF�����������Լ�ʱ���ڱ�����������Զ�ͬ�����������������ʱ��
    //
    WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(TIMER_PERIOD));

    return;
}

