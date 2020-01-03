/*++

Copyright (c) Microsoft Corporation

Module Name:

    EchoApp.cpp

Abstract:

    An application to exercise the WDF "echo" sample driver.
    ������WDF"echo"ʾ�����������ϵ�Ӧ�ó���

Environment:

    user mode only
    ���û�ģʽ

--*/

#include <DriverSpecs.h>
_Analysis_mode_(_Analysis_code_type_user_code_)

#define INITGUID

#include <windows.h>
#include <strsafe.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <winioctl.h>
#include "public.h"

#define NUM_ASYNCH_IO   100
#define BUFFER_SIZE     (40*1024)

#define READER_TYPE   1
#define WRITER_TYPE   2

#define MAX_DEVPATH_LENGTH    256

#define IOCTL_CODE_TEST CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

BOOLEAN G_bPerformAsyncIo;        // �Ƿ�ʹ���첽I/O
BOOLEAN G_bLimitedLoops;          // �Ƿ�����ѭ��
ULONG   G_nAsyncIoLoopsNum;       // �첽ѭ������
WCHAR   G_szDevicePath[MAX_DEVPATH_LENGTH];

ULONG AsyncIo(PVOID threadParameter);

BOOLEAN PerformWriteReadTest(
    IN HANDLE hDevice,
    IN ULONG  testLength
    );

BOOL GetDevicePath(
    IN  LPGUID interfaceGuid,
    _Out_writes_(bufLen) PWCHAR devicePath,
    _In_ size_t bufLen
    );

#define LOG printf

//
// ��ں���
//
int __cdecl main(_In_ int argc, _In_reads_(argc) char* argv[])
{
    LOG("main\n");

    HANDLE  hDevice = INVALID_HANDLE_VALUE;
    HANDLE  th1 = NULL;
    BOOLEAN result = TRUE;

    if (argc > 1)  {
        if(!_strnicmp (argv[1], "-Async", 6) ) {
            G_bPerformAsyncIo = TRUE;
            if (argc > 2) {
                G_nAsyncIoLoopsNum = atoi(argv[2]);
                G_bLimitedLoops = TRUE;
            }
            else {
                G_bLimitedLoops = FALSE;
            }
        }
        else {
            LOG("Usage:\n");
            LOG("    Echoapp.exe         --- Send single write and read request synchronously\n");
            LOG("    Echoapp.exe -Async  --- Send reads and writes asynchronously without terminating\n");
            LOG("    Echoapp.exe -Async <number> --- Send <number> reads and writes asynchronously\n");
            LOG("Exit the app anytime by pressing Ctrl-C\n");
            result = FALSE;
            goto exit;
        }
    }

    //
    // ����GUID��ȡ�豸·��
    //
    if ( !GetDevicePath(
            (LPGUID) &GUID_DEVINTERFACE_ECHO,
            G_szDevicePath,
            sizeof(G_szDevicePath)/sizeof(G_szDevicePath[0])) )
    {
        result = FALSE;
        goto exit;
    }

    LOG("DevicePath: %ws\n", G_szDevicePath);

    //
    // ���������豸
    //
    hDevice = CreateFile(G_szDevicePath,
                         GENERIC_READ|GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        LOG("Failed to open device. Error %d\n",GetLastError());
        OutputDebugStringA("Failed to open device. Error\n");
        result = FALSE;
        goto exit;
    }

    LOG("Opened device successfully\n");
    OutputDebugStringA("Opened device successfully\n");

    if(G_bPerformAsyncIo) {

        LOG("Starting AsyncIo\n");

        //
        // Create a reader thread
        // ����һ�����߳�
        //
        th1 = CreateThread(NULL,                   // Default Security Attrib.
                           0,                      // Initial Stack Size,
                           (LPTHREAD_START_ROUTINE) AsyncIo, // Thread Func
                           (LPVOID)READER_TYPE,
                           0,                      // Creation Flags
                           NULL);                  // Don't need the Thread Id.

        if (th1 == NULL) {
            LOG("Couldn't create reader thread - error %d\n", GetLastError());
            result = FALSE;
            goto exit;
        }

        //
        // Use this thread for peforming write.
        // ʹ�ô��߳�ִ��д������
        //
        result = (BOOLEAN)AsyncIo((PVOID)WRITER_TYPE);

    }
    else {
        //
        // Write pattern buffers and read them back, then verify them
        // д��ģʽ�����������أ�Ȼ�������֤
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
// ����ģ�⻺����
//
PUCHAR CreatePatternBuffer(IN ULONG length)
{
    unsigned int i;
    PUCHAR p, pBuf;

    pBuf = (PUCHAR)malloc(length);
    if( pBuf == NULL ) {
        LOG("Could not allocate %d byte buffer\n",length);
        return NULL;
    }

    p = pBuf;

    // �ڻ�������д������
    for(i=0; i < length; i++ ) {
        *p = (UCHAR)i;
        p++;
    }

    return pBuf;
}

//
// ��֤ģ�⻺����
//
BOOLEAN VerifyPatternBuffer(
	_In_reads_bytes_(length) PUCHAR pBuffer,
	_In_ ULONG length)
{
    unsigned int i;
    PUCHAR p = pBuffer;

    for( i=0; i < length; i++ ) {

        if( *p != (UCHAR)(i & 0xFF) ) {
            LOG("Pattern changed. SB 0x%x, Is 0x%x\n",
                   (UCHAR)(i & 0xFF), *p);
            return FALSE;
        }

        p++;
    }

    return TRUE;
}

//
// ִ�ж�д����
//
BOOLEAN PerformWriteReadTest(
    IN HANDLE hDevice,
    IN ULONG length
    )
{
    ULONG  bytesReturned =0;
    PUCHAR writeBuffer = NULL,
           readBuffer = NULL;
    BOOLEAN result = TRUE;

    // ����д������
    writeBuffer = CreatePatternBuffer(length);
    if( writeBuffer == NULL ) {

        result = FALSE;
        goto Cleanup;
    }

    // ������������
    readBuffer = (PUCHAR)malloc(length);
    if( readBuffer == NULL ) {

        LOG("PerformWriteReadTest: Could not allocate %d "
               "bytes ReadBuffer\n",length);

         result = FALSE;
         goto Cleanup;

    }

    // ��ģ�⻺����д�������豸
    bytesReturned = 0;

    if (!WriteFile (hDevice,
            writeBuffer,
            length,
            &bytesReturned,
            NULL)) {

        LOG ("PerformWriteReadTest: WriteFile failed: "
                "Error %d\n", GetLastError());

        result = FALSE;
        goto Cleanup;

    }
    else {

        if( bytesReturned != length ) {

            LOG("bytes written is not test length! Written %d, "
                   "SB %d\n",bytesReturned, length);

            result = FALSE;
            goto Cleanup;
        }

        LOG ("%d Pattern Bytes Written successfully\n",
                bytesReturned);
    }

    bytesReturned = 0;

    // �������豸��ȡ���ݵ���������
    if ( !ReadFile (hDevice,
            readBuffer,
            length,
            &bytesReturned,
            NULL)) {

        LOG ("PerformWriteReadTest: ReadFile failed: "
                "Error %d\n", GetLastError());

        result = FALSE;
        goto Cleanup;

    }
    else {

        if( bytesReturned != length ) {

            LOG("bytes Read is not test length! Read %d, "
                   "SB %d\n",bytesReturned, length);

             //
             // Note: Is this a Failure Case??
             // ע�⣺���Ƿ���ʧ�ܰ���
             //
            result = FALSE;
            goto Cleanup;
        }

        LOG ("%d Pattern Bytes Read successfully\n",bytesReturned);
    }

    // ��֤��ȡ�������Ƿ���ģ��һ��
    if( !VerifyPatternBuffer(readBuffer, length) ) {

        LOG("Verify failed\n");

        result = FALSE;
        goto Cleanup;
    }

    LOG("Pattern Verified successfully\n");

    // IOCONTROL����
    ULONG nOutput = 0;
    if (!DeviceIoControl(
        hDevice,
        IOCTL_CODE_TEST,
        NULL,
        0,
        NULL,
        0,
        &nOutput,
        NULL)
        )
    {
        printf("ERROR: DeviceIoControl returns %0x.", GetLastError());

        result = FALSE;
        goto Cleanup;
    }

Cleanup:

    //
    // Free writeBuffer if non NULL.
    // �����NULL�����ͷ�WriteBuffer��
    //
    if (writeBuffer) {
        free (writeBuffer);
    }

    //
    // Free readBuffer if non NULL
    // �����NULL�����ͷ�ReadBuffer
    //
    if (readBuffer) {
        free (readBuffer);
    }

    return result;
}

//
// �첽IO
//
ULONG AsyncIo(PVOID threadParameter)
{
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    HANDLE hCompletionPort = NULL;
    OVERLAPPED *pOvList = NULL;
    PUCHAR     buf = NULL;
    ULONG      numberOfBytesTransferred;
    OVERLAPPED *completedOv;
    ULONG_PTR  i;
    ULONG      ioType = (ULONG)(ULONG_PTR)threadParameter;
    ULONG_PTR  key;
    ULONG   error;
    BOOLEAN result = TRUE;
    ULONG maxPendingRequests = NUM_ASYNCH_IO;
    ULONG remainingRequestsToSend = 0;
    ULONG remainingRequestsToReceive = 0;

    // ���������豸
    hDevice = CreateFile(G_szDevicePath,
                     GENERIC_WRITE|GENERIC_READ,
                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                     NULL,
                     OPEN_EXISTING,
                     FILE_FLAG_OVERLAPPED,
                     NULL );


    if (hDevice == INVALID_HANDLE_VALUE) {
        LOG("Cannot open %ws error %d\n", G_szDevicePath, GetLastError());
        result = FALSE;
        goto Error;
    }

    // ����I/O�ɶ˿ڲ�������ָ�����ļ���������
    // ���򿪵��ļ������ʵ����I/O��ɶ˿����������ʹ���̽����йظ��ļ�������첽I/O������ɵ�֪ͨ��
    hCompletionPort = CreateIoCompletionPort(hDevice, NULL, 1, 0);
    if (hCompletionPort == NULL) {
        LOG("Cannot open completion port %d \n",GetLastError());
        result = FALSE;
        goto Error;
    }

    //
    // We will only have NUM_ASYNCH_IO or G_nAsyncIoLoopsNum pending at any
    // time (whichever is less)
    // �κ�ʱ������ֻ����NUM_ASYNCH_IO��G_AsyncIoLoopsNum�������Խ�����Ϊ׼��
    //
    if (G_bLimitedLoops == TRUE) {
        remainingRequestsToReceive = G_nAsyncIoLoopsNum;
        if (G_nAsyncIoLoopsNum > NUM_ASYNCH_IO) {
            //
            // After we send the initial NUM_ASYNCH_IO, we will have additional
            // (G_nAsyncIoLoopsNum - NUM_ASYNCH_IO) I/Os to send
            // ���ͳ�ʼ��NUM_ASYNCH_IO�����ǽ���������G_AsyncIoLoopsNum-NUM_ASYNCH_IO����I/O����
            //
            maxPendingRequests = NUM_ASYNCH_IO;
            remainingRequestsToSend = G_nAsyncIoLoopsNum - NUM_ASYNCH_IO;
        }
        else {
            maxPendingRequests = G_nAsyncIoLoopsNum;
            remainingRequestsToSend = 0;
        }
    }

    pOvList = (OVERLAPPED *)malloc(maxPendingRequests * sizeof(OVERLAPPED));
    if (pOvList == NULL) {
        LOG("Cannot allocate overlapped array \n");
        result = FALSE;
        goto Error;
    }

    buf = (PUCHAR)malloc(maxPendingRequests * BUFFER_SIZE);
    if (buf == NULL) {
        LOG("Cannot allocate buffer \n");
        result = FALSE;
        goto Error;
    }

    ZeroMemory(pOvList, maxPendingRequests * sizeof(OVERLAPPED));
    ZeroMemory(buf, maxPendingRequests * BUFFER_SIZE);

    //
    // Issue asynch I/O
    // �����첽I / O
    //

    for (i = 0; i < maxPendingRequests; i++) {
        if (ioType == READER_TYPE) {
            // ���豸
            if ( ReadFile( hDevice,
                      buf + (i* BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      &pOvList[i]) == 0) {

                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    LOG(" %dth Read failed %d \n", (ULONG) i, GetLastError());
                    result = FALSE;
                    goto Error;
                }
            }

        }
        else {
            // д�豸
            if ( WriteFile( hDevice,
                      buf + (i* BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      &pOvList[i]) == 0) {
                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    LOG(" %dth Write failed %d \n", (ULONG) i, GetLastError());
                    result = FALSE;
                    goto Error;
                }
            }
        }
    }

    //
    // Wait for the I/Os to complete. If one completes then reissue the I/O
    // �ȴ�I/O��ɡ������ɣ������·���I/O��
    //
    WHILE (1) {

        if ( GetQueuedCompletionStatus(hCompletionPort, &numberOfBytesTransferred, &key, &completedOv, INFINITE) == 0) {
            LOG("GetQueuedCompletionStatus failed %d\n", GetLastError());
            result = FALSE;
            goto Error;
        }

        //
        // Read successfully completed. If we're doing unlimited I/Os then Issue another one.
        // ��ȡ�ɹ���ɡ� �������Ҫ���������Ƶ�I / O���򷢳���һ��I / O��
        //
        if (ioType == READER_TYPE) {

            i = completedOv - pOvList;
            LOG("Number of bytes read by request number %Id is %d\n", i, numberOfBytesTransferred);

            //
            // If we're done with the I/Os, then exit
            // ������������I/O�����˳�
            //
            if (G_bLimitedLoops == TRUE) {
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
                    LOG("%Idth Read failed %d \n", i, GetLastError());
                    result = FALSE;
                    goto Error;
                }
            }
        }
        else {

            i = completedOv - pOvList;

            LOG("Number of bytes written by request number %Id is %d\n", i, numberOfBytesTransferred);

            //
            // If we're done with the I/Os, then exit
            // ������������I/O�����˳�
            //
            if (G_bLimitedLoops == TRUE) {
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

                    LOG("%Idth write failed %d \n", i, GetLastError());
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
// ����GUID��ȡ�豸·��
//
BOOL GetDevicePath(
    _In_ LPGUID interfaceGuid,
    _Out_writes_(bufLen) PWCHAR devicePath,
    _In_ size_t bufLen
    )
{
    CONFIGRET cr = CR_SUCCESS;
    PWSTR deviceInterfaceList = NULL;
    ULONG deviceInterfaceListLength = 0;
    PWSTR nextInterface;
    HRESULT hr = E_FAIL;
    BOOL bRet = TRUE;

    cr = CM_Get_Device_Interface_List_Size(
                &deviceInterfaceListLength,
                interfaceGuid,
                NULL,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) {
        LOG("Error 0x%x retrieving device interface list size.\n", cr);
        goto clean0;
    }

    if (deviceInterfaceListLength <= 1) {
        bRet = FALSE;
        LOG("Error: No active device interfaces found.\n"
            " Is the sample driver loaded?");
        goto clean0;
    }

    deviceInterfaceList = (PWSTR)malloc(deviceInterfaceListLength * sizeof(WCHAR));
    if (deviceInterfaceList == NULL) {
        LOG("Error allocating memory for device interface list.\n");
        goto clean0;
    }
    ZeroMemory(deviceInterfaceList, deviceInterfaceListLength * sizeof(WCHAR));

    cr = CM_Get_Device_Interface_List(
                interfaceGuid,
                NULL,
                deviceInterfaceList,
                deviceInterfaceListLength,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) {
        LOG("Error 0x%x retrieving device interface list.\n", cr);
        goto clean0;
    }

    nextInterface = deviceInterfaceList + wcslen(deviceInterfaceList) + 1;
    if (*nextInterface != UNICODE_NULL) {
        LOG("Warning: More than one device interface instance found. \n"
            "Selecting first matching device.\n\n");
    }

    hr = StringCchCopy(devicePath, bufLen, deviceInterfaceList);
    if (FAILED(hr)) {
        bRet = FALSE;
        LOG("Error: StringCchCopy failed with HRESULT 0x%x", hr);
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

