/*++

Copyright (c) 1990-2000  Microsoft Corporation

Module Name:

	driver.c

Abstract:

	This driver demonstrates use of a default I/O Queue, its
	request start events, cancellation event, and a synchronized DPC.
	该驱动程序演示了默认I/O队列，其请求开始事件，取消事件和同步DPC的用法。

	To demonstrate asynchronous operation, the I/O requests are not completed
	immediately, but stored in the drivers private data structure, and a timer
	will complete it next time the Timer callback runs.
	为了演示异步操作，I/O请求不会立即完成，而是存储在驱动程序私有数据结构中，
	并且在下一次Timer回调运行时，计时器将完成该请求。

	During the time the request is waiting for the timer callback to run, it is
	made cancellable by the call WdfRequestMarkCancelable. This
	allows the test program to cancel the request and exit instantly.
	在请求等待计时器回调运行期间，可以通过调用WdfRequestMarkCancelable将其取消。
	这使测试程序可以取消请求并立即退出。

	This rather complicated set of events is designed to demonstrate
	the driver frameworks synchronization of access to a device driver
	data structure, and a pointer which can be a proxy for device hardware
	registers or resources.
	这组相当复杂的事件旨在演示驱动程序框架对设备驱动程序数据结构的访问以及指针的同步，
	该指针可以作为设备硬件寄存器或资源的代理。

	This common data structure, or resource is accessed by new request
	events arriving, the Timer callback that completes it, and cancel processing.
	到达新请求事件，完成它的Timer回调并取消处理后，即可访问此通用数据结构或资源。

	Notice the lack of specific lock/unlock operations.
	请注意，缺少特定的锁定/解锁操作。

	Even though this example utilizes a serial queue, a parallel queue
	would not need any additional explicit synchronization, just a
	strategy for managing multiple requests outstanding.
	即使此示例使用串行队列，并行队列也不需要任何其他的显式同步，仅是用于管理多个未决请求的策略。

--*/

#include "driver.h"

/*
Routine Description:
	DriverEntry initializes the driver and is the first routine called by the
	system after the driver is loaded. DriverEntry specifies the other entry
	points in the function driver, such as EvtDevice and DriverUnload.
	DriverEntry初始化驱动程序，并且是驱动程序加载后系统调用的第一个例程。 
	DriverEntry指定功能驱动程序中的其他入口点，例如EvtDevice和DriverUnload。

Parameters Description:

	DriverObject - represents the instance of the function driver that is loaded
	into memory. DriverEntry must initialize members of DriverObject before it
	returns to the caller. DriverObject is allocated by the system before the
	driver is loaded, and it is released by the system after the system unloads
	the function driver from memory.
	DriverObject - 表示已加载到内存中的功能驱动程序的实例。 
	DriverEntry在返回给调用者之前必须初始化DriverObject的成员。 
	DriverObject在加载驱动程序之前由系统分配，并在系统从内存中卸载功能驱动程序后由系统释放。

	RegistryPath - represents the driver specific path in the Registry.
	The function driver can use the path to store driver related data between
	reboots. The path does not store hardware instance specific data.
	RegistryPath-代表注册表中特定于驱动程序的路径。 
	功能驱动程序可以使用路径在重新启动之间存储与驱动程序相关的数据。 
	该路径不存储特定于硬件实例的数据。

Return Value:

	STATUS_SUCCESS if successful,
	STATUS_UNSUCCESSFUL otherwise.
*/
NTSTATUS DriverEntry(
	IN PDRIVER_OBJECT  DriverObject,
	IN PUNICODE_STRING RegistryPath
)
{
	KdPrint(("DriverEntry\n"));

	WDF_DRIVER_CONFIG config;
	NTSTATUS status;

	WDF_DRIVER_CONFIG_INIT(&config,
		EchoEvtDeviceAdd
	);

	KdPrint(("WdfDriverCreate\n"));
	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		WDF_NO_OBJECT_ATTRIBUTES,
		&config,
		WDF_NO_HANDLE);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Error: WdfDriverCreate failed 0x%x\n", status));
		return status;
	}

#if DBG
	EchoPrintDriverVersion();
#endif

	return status;
}

/*
Routine Description:

	EvtDeviceAdd is called by the framework in response to AddDevice
	call from the PnP manager. We create and initialize a device object to
	represent a new instance of the device.
	框架调用EvtDeviceAdd来响应来自PnP管理器的AddDevice调用。
	我们创建并初始化一个设备对象以表示该设备的新实例。

Arguments:

	Driver - Handle to a framework driver object created in DriverEntry
	         处理在DriverEntry中创建的框架驱动程序对象

	DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.
	             指向框架分配的WDFDEVICE_INIT结构的指针。

Return Value:

	NTSTATUS
*/
NTSTATUS EchoEvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	KdPrint(("EchoEvtDeviceAdd\n"));

	NTSTATUS status;

	UNREFERENCED_PARAMETER(Driver);

	KdPrint(("EchoDeviceCreate\n"));
	status = EchoDeviceCreate(DeviceInit);

	return status;
}

/*
Routine Description:

   This routine shows how to retrieve framework version string and
   also how to find out to which version of framework library the
   client driver is bound to.

Arguments:

Return Value:

	NTSTATUS
*/
NTSTATUS EchoPrintDriverVersion( )
{
	KdPrint(("EchoPrintDriverVersion\n"));

	NTSTATUS status;
	WDFSTRING string;
	UNICODE_STRING us;
	WDF_DRIVER_VERSION_AVAILABLE_PARAMS ver;

	//
	// 1) Retreive version string and print that in the debugger.
	//
	status = WdfStringCreate(NULL, WDF_NO_OBJECT_ATTRIBUTES, &string);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Error: WdfStringCreate failed 0x%x\n", status));
		return status;
	}

	status = WdfDriverRetrieveVersionString(WdfGetDriver(), string);
	if (!NT_SUCCESS(status)) {
		//
		// No need to worry about delete the string object because
		// by default it's parented to the driver and it will be
		// deleted when the driverobject is deleted when the DriverEntry
		// returns a failure status.
		//
		KdPrint(("Error: WdfDriverRetrieveVersionString failed 0x%x\n", status));
		return status;
	}

	WdfStringGetUnicodeString(string, &us);
	KdPrint(("Echo Sample %wZ\n", &us));

	WdfObjectDelete(string);
	string = NULL; // To avoid referencing a deleted object.

	//
	// 2) Find out to which version of framework this driver is bound to.
	//
	WDF_DRIVER_VERSION_AVAILABLE_PARAMS_INIT(&ver, 1, 0);
	if (WdfDriverIsVersionAvailable(WdfGetDriver(), &ver) == TRUE) {
		KdPrint(("Yes, framework version is 1.0\n"));
	}
	else {
		KdPrint(("No, framework verison is not 1.0\n"));
	}

	return STATUS_SUCCESS;
}

