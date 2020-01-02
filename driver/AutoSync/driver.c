/*

Copyright (c) 1990-2000  Microsoft Corporation

Module Name:

	driver.c

Abstract:

	This driver demonstrates use of a default I/O Queue, its
	request start events, cancellation event, and a synchronized DPC.
	������������ʾ��Ĭ��I/O���У�������ʼ�¼���ȡ���¼���ͬ��DPC���÷���

	To demonstrate asynchronous operation, the I/O requests are not completed
	immediately, but stored in the drivers private data structure, and a timer
	will complete it next time the Timer callback runs.
	Ϊ����ʾ�첽������I/O���󲻻�������ɣ����Ǵ洢����������˽�����ݽṹ�У�
	��������һ��Timer�ص�����ʱ����ʱ������ɸ�����

	During the time the request is waiting for the timer callback to run, it is
	made cancellable by the call WdfRequestMarkCancelable. This
	allows the test program to cancel the request and exit instantly.
	������ȴ���ʱ���ص������ڼ䣬����ͨ������WdfRequestMarkCancelable����ȡ����
	��ʹ���Գ������ȡ�����������˳���

	This rather complicated set of events is designed to demonstrate
	the driver frameworks synchronization of access to a device driver
	data structure, and a pointer which can be a proxy for device hardware
	registers or resources.
	�����൱���ӵ��¼�ּ����ʾ���������ܶ��豸�����������ݽṹ�ķ����Լ�ָ���ͬ����
	��ָ�������Ϊ�豸Ӳ���Ĵ�������Դ�Ĵ���

	This common data structure, or resource is accessed by new request
	events arriving, the Timer callback that completes it, and cancel processing.
	�����������¼����������Timer�ص���ȡ������󣬼��ɷ��ʴ�ͨ�����ݽṹ����Դ��

	Notice the lack of specific lock/unlock operations.
	��ע�⣬ȱ���ض�������/����������

	Even though this example utilizes a serial queue, a parallel queue
	would not need any additional explicit synchronization, just a
	strategy for managing multiple requests outstanding.
	��ʹ��ʾ��ʹ�ô��ж��У����ж���Ҳ����Ҫ�κ���������ʽͬ�����������ڹ�����δ������Ĳ��ԡ�

--*/

#include "driver.h"

/*
Function:
	DriverEntry
	����������ں���

Routine Description:
	DriverEntry initializes the driver and is the first routine called by the
	system after the driver is loaded. DriverEntry specifies the other entry
	points in the function driver, such as EvtDevice and DriverUnload.
	DriverEntry��ʼ���������򣬲���������������غ�ϵͳ���õĵ�һ�����̡� 
	DriverEntryָ���������������е�������ڵ㣬����EvtDevice��DriverUnload��

Parameters Description:

	DriverObject - represents the instance of the function driver that is loaded
	into memory. DriverEntry must initialize members of DriverObject before it
	returns to the caller. DriverObject is allocated by the system before the
	driver is loaded, and it is released by the system after the system unloads
	the function driver from memory.
	DriverObject - ��ʾ�Ѽ��ص��ڴ��еĹ������������ʵ���� 
	DriverEntry�ڷ��ظ�������֮ǰ�����ʼ��DriverObject�ĳ�Ա�� 
	DriverObject�ڼ�����������֮ǰ��ϵͳ���䣬����ϵͳ���ڴ���ж�ع��������������ϵͳ�ͷš�

	RegistryPath - represents the driver specific path in the Registry.
	The function driver can use the path to store driver related data between
	reboots. The path does not store hardware instance specific data.
	RegistryPath - ����ע������ض������������·���� 
	���������������ʹ��·������������֮��洢������������ص����ݡ� 
	��·�����洢�ض���Ӳ��ʵ�������ݡ�

Return Value:

	STATUS_SUCCESS if successful,
	STATUS_UNSUCCESSFUL otherwise.

*/
NTSTATUS DriverEntry(
	IN PDRIVER_OBJECT  DriverObject,
	IN PUNICODE_STRING RegistryPath
)
{
	LOG("DriverEntry\n");

	WDF_DRIVER_CONFIG config;
	NTSTATUS status;

	// ע���豸�ϵ��Ļص�����EchoEvtDeviceAdd��
	WDF_DRIVER_CONFIG_INIT(
		&config,
		EchoEvtDeviceAdd
	);

	LOG("WdfDriverCreate\n");

	// �����������������ܵ������������
	status = WdfDriverCreate(
		DriverObject,
		RegistryPath,
		WDF_NO_OBJECT_ATTRIBUTES,
		&config,
		WDF_NO_HANDLE);
	if (!NT_SUCCESS(status)) {
		LOG("Error: WdfDriverCreate failed 0x%x\n", status);
		return status;
	}

#if DBG
	// ����ģʽ��ӡ��������汾
	EchoPrintDriverVersion();
#endif

	return status;
}

/*
Function:
	EchoEvtDeviceAdd
	�豸�ϵ�ص�

Routine Description:

	EvtDeviceAdd is called by the framework in response to AddDevice
	call from the PnP manager. We create and initialize a device object to
	represent a new instance of the device.
	һ���豸�ϵ磬PNP�������ͻᷢ����ӦIRP��KMDF�յ���IRP�󣬾ͻ����EchoEvtDeviceAdd����ص�������

Arguments:

	Driver - Handle to a framework driver object created in DriverEntry
	         ������DriverEntry�д����Ŀ�������������

	DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.
	             ָ���ܷ����WDFDEVICE_INIT�ṹ��ָ�롣

Return Value:

	NTSTATUS
*/
NTSTATUS EchoEvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	LOG("EchoEvtDeviceAdd\n");

	NTSTATUS status;

	UNREFERENCED_PARAMETER(Driver);

	// �ص�������Ҫ������EchoDeviceCreate()
	status = EchoDeviceCreate(DeviceInit);

	return status;
}

/*
Function:
	EchoPrintDriverVersion
	��ӡ�����汾

Routine Description:

   This routine shows how to retrieve framework version string and
   also how to find out to which version of framework library the
   client driver is bound to.
   ��������ʾ��μ�����ܰ汾�ַ������Լ�����ҳ��ͻ�����������󶨵��ĸ��汾�Ŀ�ܿ⡣

Arguments:

Return Value:

	NTSTATUS
*/
NTSTATUS EchoPrintDriverVersion( )
{
	LOG("EchoPrintDriverVersion\n");

	NTSTATUS status;
	WDFSTRING string;
	UNICODE_STRING us;
	WDF_DRIVER_VERSION_AVAILABLE_PARAMS ver;

	//
	// 1) Retreive version string and print that in the debugger.
	//    �����汾�ַ������ڵ������д�ӡ��
	//
	status = WdfStringCreate(NULL, WDF_NO_OBJECT_ATTRIBUTES, &string);
	if (!NT_SUCCESS(status)) {
		LOG("Error: WdfStringCreate failed 0x%x\n", status);
		return status;
	}

	status = WdfDriverRetrieveVersionString(WdfGetDriver(), string);
	if (!NT_SUCCESS(status)) {
		//
		// No need to worry about delete the string object because
		// by default it's parented to the driver and it will be
		// deleted when the driverobject is deleted when the DriverEntry
		// returns a failure status.
		// ���赣��ɾ���ַ���������ΪĬ������¸��ַ�����������������ĸ�����
		// ���ҵ�DriverEntry����ʧ��״̬ʱɾ����driverObjectʱ��������ɾ����
		//
		LOG("Error: WdfDriverRetrieveVersionString failed 0x%x\n", status);
		return status;
	}

	WdfStringGetUnicodeString(string, &us);
	LOG("Echo Sample %wZ\n", &us);

	WdfObjectDelete(string);
	string = NULL; // To avoid referencing a deleted object.
	               // Ϊ�˱���������ɾ���Ķ���

	//
	// 2) Find out to which version of framework this driver is bound to.
	//    �ҳ�����������󶨵��ĸ��汾�Ŀ�ܡ�
	//
	WDF_DRIVER_VERSION_AVAILABLE_PARAMS_INIT(&ver, 1, 0);
	if (WdfDriverIsVersionAvailable(WdfGetDriver(), &ver) == TRUE) {
		LOG("Yes, framework version is 1.0\n");
	}
	else {
		LOG("No, framework verison is not 1.0\n");
	}

	return STATUS_SUCCESS;
}

