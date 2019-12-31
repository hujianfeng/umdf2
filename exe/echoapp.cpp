/*++

Copyright (c) Microsoft Corporation

Module Name:

    EchoApp.cpp

Abstract:

    An application to exercise the WDF "echo" sample driver.
    运行在WDF"echo"示例驱动程序上的应用程序。

Environment:

    user mode only
    仅用户模式

--*/


#include <DriverSpecs.h>
_Analysis_mode_(_Analysis_code_type_user_code_)

#define INITGUID

#include <windows.h>
#include <strsafe.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include "public.h"

#define NUM_ASYNCH_IO   100
#define BUFFER_SIZE     (40*1024)

#define READER_TYPE   1
#define WRITER_TYPE   2

#define MAX_DEVPATH_LENGTH    256

BOOLEAN G_PerformAsyncIo;      // 是否使用异步I/O
BOOLEAN G_LimitedLoops;        // 是否有限循环
ULONG G_AsyncIoLoopsNum;       // 异步循环次数
WCHAR G_DevicePath[MAX_DEVPATH_LENGTH];

ULONG AsyncIo(PVOID  ThreadParameter);

BOOLEAN PerformWriteReadTest(
    IN HANDLE hDevice,
    IN ULONG TestLength
    );

BOOL GetDevicePath(
    IN  LPGUID InterfaceGuid,
    _Out_writes_(BufLen) PWCHAR DevicePath,
    _In_ size_t BufLen
    );


//
// 入口函数
//
int __cdecl main(_In_ int argc, _In_reads_(argc) char* argv[])
{
    OutputDebugStringA("main\n");

    HANDLE  hDevice = INVALID_HANDLE_VALUE;
    HANDLE  th1 = NULL;
    BOOLEAN result = TRUE;

    if (argc > 1)  {
        if(!_strnicmp (argv[1], "-Async", 6) ) {
            G_PerformAsyncIo = TRUE;
            if (argc > 2) {
                G_AsyncIoLoopsNum = atoi(argv[2]);
                G_LimitedLoops = TRUE;
            }
            else {
                G_LimitedLoops = FALSE;
            }
        }
        else {
            printf("Usage:\n");
            printf("    Echoapp.exe         --- Send single write and read request synchronously\n");
            printf("    Echoapp.exe -Async  --- Send reads and writes asynchronously without terminating\n");
            printf("    Echoapp.exe -Async <number> --- Send <number> reads and writes asynchronously\n");
            printf("Exit the app anytime by pressing Ctrl-C\n");
            result = FALSE;
            goto exit;
        }
    }

    //
    // 根据GUID获取设备路径
    //
    if ( !GetDevicePath(
            (LPGUID) &GUID_DEVINTERFACE_ECHO,
            G_DevicePath,
            sizeof(G_DevicePath)/sizeof(G_DevicePath[0])) )
    {
        result = FALSE;
        goto exit;
    }

    printf("DevicePath: %ws\n", G_DevicePath);

    //
    // 建立驱动设备
    //
    hDevice = CreateFile(G_DevicePath,
                         GENERIC_READ|GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device. Error %d\n",GetLastError());
        result = FALSE;
        goto exit;
    }

    printf("Opened device successfully\n");
    OutputDebugStringA("Opened device successfully\n");

    if(G_PerformAsyncIo) {

        printf("Starting AsyncIo\n");

        //
        // Create a reader thread
        // 创建一个读线程
        //
        th1 = CreateThread(NULL,                   // Default Security Attrib.
                           0,                      // Initial Stack Size,
                           (LPTHREAD_START_ROUTINE) AsyncIo, // Thread Func
                           (LPVOID)READER_TYPE,
                           0,                      // Creation Flags
                           NULL);                  // Don't need the Thread Id.

        if (th1 == NULL) {
            printf("Couldn't create reader thread - error %d\n", GetLastError());
            result = FALSE;
            goto exit;
        }

        //
        // Use this thread for peforming write.
        // 使用此线程执行写操作。
        //
        result = (BOOLEAN)AsyncIo((PVOID)WRITER_TYPE);

    }
    else {
        //
        // Write pattern buffers and read them back, then verify them
        // 写入模式缓冲区并读回，然后进行验证
        //
        result = PerformWriteReadTest(hDevice, 512);
        if(!result) {
            goto exit;
        }

        result = PerformWriteReadTest(hDevice, 30*1024);
        if(!result) {
            goto exit;
        }

    }

exit:

    if (th1 != NULL) {
        WaitForSingleObject(th1, INFINITE);
        CloseHandle(th1);
    }

    if (hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice);
    }

    return ((result == TRUE) ? 0 : 1);

}

//
// 创建模拟缓冲区
//
PUCHAR CreatePatternBuffer(IN ULONG Length)
{
    unsigned int i;
    PUCHAR p, pBuf;

    pBuf = (PUCHAR)malloc(Length);
    if( pBuf == NULL ) {
        printf("Could not allocate %d byte buffer\n",Length);
        return NULL;
    }

    p = pBuf;

    // 在缓冲区中写入内容
    for(i=0; i < Length; i++ ) {
        *p = (UCHAR)i;
        p++;
    }

    return pBuf;
}

//
// 验证模拟缓冲区
//
BOOLEAN VerifyPatternBuffer(
	_In_reads_bytes_(Length) PUCHAR pBuffer,
	_In_ ULONG Length)
{
    OutputDebugStringA("VerifyPatternBuffer\n");

    unsigned int i;
    PUCHAR p = pBuffer;

    for( i=0; i < Length; i++ ) {

        if( *p != (UCHAR)(i & 0xFF) ) {
            printf("Pattern changed. SB 0x%x, Is 0x%x\n",
                   (UCHAR)(i & 0xFF), *p);
            return FALSE;
        }

        p++;
    }

    return TRUE;
}

//
// 执行读写测试
//
BOOLEAN PerformWriteReadTest(
    IN HANDLE hDevice,
    IN ULONG TestLength
    )
{
    OutputDebugStringA("PerformWriteReadTest\n");

    ULONG  bytesReturned =0;
    PUCHAR WriteBuffer = NULL,
           ReadBuffer = NULL;
    BOOLEAN result = TRUE;

    // 建立写缓冲区
    WriteBuffer = CreatePatternBuffer(TestLength);
    if( WriteBuffer == NULL ) {

        result = FALSE;
        goto Cleanup;
    }

    // 建立读缓冲区
    ReadBuffer = (PUCHAR)malloc(TestLength);
    if( ReadBuffer == NULL ) {

        printf("PerformWriteReadTest: Could not allocate %d "
               "bytes ReadBuffer\n",TestLength);

         result = FALSE;
         goto Cleanup;

    }

    // 将模拟缓冲区写入驱动设备
    bytesReturned = 0;

    if (!WriteFile (hDevice,
            WriteBuffer,
            TestLength,
            &bytesReturned,
            NULL)) {

        printf ("PerformWriteReadTest: WriteFile failed: "
                "Error %d\n", GetLastError());

        result = FALSE;
        goto Cleanup;

    }
    else {

        if( bytesReturned != TestLength ) {

            printf("bytes written is not test length! Written %d, "
                   "SB %d\n",bytesReturned, TestLength);

            result = FALSE;
            goto Cleanup;
        }

        printf ("%d Pattern Bytes Written successfully\n",
                bytesReturned);
    }

    bytesReturned = 0;

    // 从驱动设备读取数据到读缓冲区
    if ( !ReadFile (hDevice,
            ReadBuffer,
            TestLength,
            &bytesReturned,
            NULL)) {

        printf ("PerformWriteReadTest: ReadFile failed: "
                "Error %d\n", GetLastError());

        result = FALSE;
        goto Cleanup;

    }
    else {

        if( bytesReturned != TestLength ) {

            printf("bytes Read is not test length! Read %d, "
                   "SB %d\n",bytesReturned, TestLength);

             //
             // Note: Is this a Failure Case??
             // 注意：这是否是失败案例
             //
            result = FALSE;
            goto Cleanup;
        }

        printf ("%d Pattern Bytes Read successfully\n",bytesReturned);
    }

    // 验证读取的数据是否与模拟一致
    if( !VerifyPatternBuffer(ReadBuffer, TestLength) ) {

        printf("Verify failed\n");

        result = FALSE;
        goto Cleanup;
    }

    printf("Pattern Verified successfully\n");

Cleanup:

    //
    // Free WriteBuffer if non NULL.
    // 如果非NULL，则释放WriteBuffer。
    //
    if (WriteBuffer) {
        free (WriteBuffer);
    }

    //
    // Free ReadBuffer if non NULL
    // 如果非NULL，则释放ReadBuffer
    //
    if (ReadBuffer) {
        free (ReadBuffer);
    }

    return result;
}

//
// 异步IO
//
ULONG AsyncIo(PVOID ThreadParameter)
{
    OutputDebugStringA("AsyncIo\n");

    HANDLE hDevice = INVALID_HANDLE_VALUE;
    HANDLE hCompletionPort = NULL;
    OVERLAPPED *pOvList = NULL;
    PUCHAR      buf = NULL;
    ULONG     numberOfBytesTransferred;
    OVERLAPPED *completedOv;
    ULONG_PTR    i;
    ULONG   ioType = (ULONG)(ULONG_PTR)ThreadParameter;
    ULONG_PTR   key;
    ULONG   error;
    BOOLEAN result = TRUE;
    ULONG maxPendingRequests = NUM_ASYNCH_IO;
    ULONG remainingRequestsToSend = 0;
    ULONG remainingRequestsToReceive = 0;

    // 建立驱动设备
    hDevice = CreateFile(G_DevicePath,
                     GENERIC_WRITE|GENERIC_READ,
                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                     NULL,
                     OPEN_EXISTING,
                     FILE_FLAG_OVERLAPPED,
                     NULL );


    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Cannot open %ws error %d\n", G_DevicePath, GetLastError());
        result = FALSE;
        goto Error;
    }

    // 创建I/O成端口并将其与指定的文件句柄相关联
    // 将打开的文件句柄的实例与I/O完成端口相关联，可使进程接收有关该文件句柄的异步I/O操作完成的通知。
    hCompletionPort = CreateIoCompletionPort(hDevice, NULL, 1, 0);
    if (hCompletionPort == NULL) {
        printf("Cannot open completion port %d \n",GetLastError());
        result = FALSE;
        goto Error;
    }

    //
    // We will only have NUM_ASYNCH_IO or G_AsyncIoLoopsNum pending at any
    // time (whichever is less)
    // 任何时候我们只会有NUM_ASYNCH_IO或G_AsyncIoLoopsNum待处理（以较少者为准）
    //
    if (G_LimitedLoops == TRUE) {
        remainingRequestsToReceive = G_AsyncIoLoopsNum;
        if (G_AsyncIoLoopsNum > NUM_ASYNCH_IO) {
            //
            // After we send the initial NUM_ASYNCH_IO, we will have additional
            // (G_AsyncIoLoopsNum - NUM_ASYNCH_IO) I/Os to send
            // 发送初始的NUM_ASYNCH_IO后，我们将有其他（G_AsyncIoLoopsNum-NUM_ASYNCH_IO）个I/O发送
            //
            maxPendingRequests = NUM_ASYNCH_IO;
            remainingRequestsToSend = G_AsyncIoLoopsNum - NUM_ASYNCH_IO;
        }
        else {
            maxPendingRequests = G_AsyncIoLoopsNum;
            remainingRequestsToSend = 0;
        }
    }

    pOvList = (OVERLAPPED *)malloc(maxPendingRequests * sizeof(OVERLAPPED));
    if (pOvList == NULL) {
        printf("Cannot allocate overlapped array \n");
        result = FALSE;
        goto Error;
    }

    buf = (PUCHAR)malloc(maxPendingRequests * BUFFER_SIZE);
    if (buf == NULL) {
        printf("Cannot allocate buffer \n");
        result = FALSE;
        goto Error;
    }

    ZeroMemory(pOvList, maxPendingRequests * sizeof(OVERLAPPED));
    ZeroMemory(buf, maxPendingRequests * BUFFER_SIZE);

    //
    // Issue asynch I/O
    // 发出异步I / O
    //

    for (i = 0; i < maxPendingRequests; i++) {
        if (ioType == READER_TYPE) {
            // 读设备
            if ( ReadFile( hDevice,
                      buf + (i* BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      &pOvList[i]) == 0) {

                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    printf(" %dth Read failed %d \n", (ULONG) i, GetLastError());
                    result = FALSE;
                    goto Error;
                }
            }

        }
        else {
            // 写设备
            if ( WriteFile( hDevice,
                      buf + (i* BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      &pOvList[i]) == 0) {
                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    printf(" %dth Write failed %d \n", (ULONG) i, GetLastError());
                    result = FALSE;
                    goto Error;
                }
            }
        }
    }

    //
    // Wait for the I/Os to complete. If one completes then reissue the I/O
    // 等待I/O完成。如果完成，则重新发出I/O。
    //
    WHILE (1) {

        if ( GetQueuedCompletionStatus(hCompletionPort, &numberOfBytesTransferred, &key, &completedOv, INFINITE) == 0) {
            printf("GetQueuedCompletionStatus failed %d\n", GetLastError());
            result = FALSE;
            goto Error;
        }

        //
        // Read successfully completed. If we're doing unlimited I/Os then Issue another one.
        // 读取成功完成。 如果我们要进行无限制的I / O，则发出另一个I / O。
        //
        if (ioType == READER_TYPE) {

            i = completedOv - pOvList;
            printf("Number of bytes read by request number %Id is %d\n", i, numberOfBytesTransferred);

            //
            // If we're done with the I/Os, then exit
            // 如果我们完成了I/O，则退出
            //
            if (G_LimitedLoops == TRUE) {
                if ((--remainingRequestsToReceive) == 0) {
                    break;
                }

                if (remainingRequestsToSend == 0) {
                    continue;
                }
                else {
                    remainingRequestsToSend--;
                }
            }

            if ( ReadFile( hDevice,
                      buf + (i * BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      completedOv) == 0) {
                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    printf("%Idth Read failed %d \n", i, GetLastError());
                    result = FALSE;
                    goto Error;
                }
            }
        }
        else {

            i = completedOv - pOvList;

            printf("Number of bytes written by request number %Id is %d\n", i, numberOfBytesTransferred);

            //
            // If we're done with the I/Os, then exit
            // 如果我们完成了I/O，则退出
            //
            if (G_LimitedLoops == TRUE) {
                if ((--remainingRequestsToReceive) == 0) {
                    break;
                }

                if (remainingRequestsToSend == 0) {
                    continue;
                }
                else {
                    remainingRequestsToSend--;
                }
            }

            if ( WriteFile( hDevice,
                      buf + (i * BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      completedOv) == 0) {
                error = GetLastError();
                if (error != ERROR_IO_PENDING) {

                    printf("%Idth write failed %d \n", i, GetLastError());
                    result = FALSE;
                    goto Error;
                }
            }
        }
    }

Error:
    if(hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice);
    }

    if(hCompletionPort) {
        CloseHandle(hCompletionPort);
    }

    if(buf) {
        free(buf);
    }
    if(pOvList) {
        free(pOvList);
    }

    return (ULONG)result;

}

//
// 根据GUID获取设备路径
//
BOOL GetDevicePath(
    _In_ LPGUID InterfaceGuid,
    _Out_writes_(BufLen) PWCHAR DevicePath,
    _In_ size_t BufLen
    )
{
    OutputDebugStringA("GetDevicePath\n");

    CONFIGRET cr = CR_SUCCESS;
    PWSTR deviceInterfaceList = NULL;
    ULONG deviceInterfaceListLength = 0;
    PWSTR nextInterface;
    HRESULT hr = E_FAIL;
    BOOL bRet = TRUE;

    cr = CM_Get_Device_Interface_List_Size(
                &deviceInterfaceListLength,
                InterfaceGuid,
                NULL,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) {
        printf("Error 0x%x retrieving device interface list size.\n", cr);
        goto clean0;
    }

    if (deviceInterfaceListLength <= 1) {
        bRet = FALSE;
        printf("Error: No active device interfaces found.\n"
            " Is the sample driver loaded?");
        goto clean0;
    }

    deviceInterfaceList = (PWSTR)malloc(deviceInterfaceListLength * sizeof(WCHAR));
    if (deviceInterfaceList == NULL) {
        printf("Error allocating memory for device interface list.\n");
        goto clean0;
    }
    ZeroMemory(deviceInterfaceList, deviceInterfaceListLength * sizeof(WCHAR));

    cr = CM_Get_Device_Interface_List(
                InterfaceGuid,
                NULL,
                deviceInterfaceList,
                deviceInterfaceListLength,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) {
        printf("Error 0x%x retrieving device interface list.\n", cr);
        goto clean0;
    }

    nextInterface = deviceInterfaceList + wcslen(deviceInterfaceList) + 1;
    if (*nextInterface != UNICODE_NULL) {
        printf("Warning: More than one device interface instance found. \n"
            "Selecting first matching device.\n\n");
    }

    hr = StringCchCopy(DevicePath, BufLen, deviceInterfaceList);
    if (FAILED(hr)) {
        bRet = FALSE;
        printf("Error: StringCchCopy failed with HRESULT 0x%x", hr);
        goto clean0;
    }

clean0:
    if (deviceInterfaceList != NULL) {
        free(deviceInterfaceList);
    }
    if (CR_SUCCESS != cr) {
        bRet = FALSE;
    }

    return bRet;
}

