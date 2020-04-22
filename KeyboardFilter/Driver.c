#include <wdm.h>
#include <ntddk.h>
#include <ntddkbd.h>
#include <ntstrsafe.h>

static ULONG KeyCount = 0;

static USHORT DisabledKey[] =  {
	0x0001, 0x003b, 0x003c, 0x003d,
	0x003e, 0x003f, 0x0040, 0x0041,
	0x0042, 0x0043, 0x0044, 0x0057,
	0x0058, 0x001d, 0x0038, 0x005b
};

static NTSTATUS
DriverDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp);

static VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject);

static NTSTATUS
PassIrp(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp);


static NTSTATUS
HandleCreateClose(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp);

static NTSTATUS
HandleIoCtrl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp);

static NTSTATUS
HandlePower(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp);

static NTSTATUS
HandlePnP(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp);

static NTSTATUS
HandleRead(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp);

static NTSTATUS
CreateAndAttachDevice(
	_In_ PDRIVER_OBJECT sourceDriver,
	_In_ PDRIVER_OBJECT targetDriver);

static VOID
DetachAndDeleteDevice(
	_In_ PDRIVER_OBJECT driver);

static BOOLEAN
IsDisabledKey(USHORT key);

static NTSTATUS
getDriverByName(
	_In_ PUNICODE_STRING driverName,
	_Out_ PDRIVER_OBJECT *driver);

NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT  DriverObject,
	_In_ PUNICODE_STRING RegistryPath)
{
	DbgPrint("Driver Entry\n");

	if (DriverObject == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	ULONG i;
	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		DriverObject->MajorFunction[i] = DriverDispatch;
	}
	DriverObject->DriverUnload = DriverUnload;

	UNICODE_STRING drvName;
	RtlInitUnicodeString(&drvName, L"\\Driver\\kbdclass");
	PDRIVER_OBJECT kbdDriver;
	NTSTATUS status = getDriverByName(&drvName, &kbdDriver);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	ObDereferenceObject(kbdDriver);

	return CreateAndAttachDevice(DriverObject, kbdDriver);
}

static NTSTATUS
DriverDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp)
{
	PIO_STACK_LOCATION stackLoc = IoGetCurrentIrpStackLocation(Irp);
	if (stackLoc == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	switch (stackLoc->MajorFunction) {
	case IRP_MJ_POWER:
		return HandlePower(DeviceObject, Irp);

	case IRP_MJ_PNP:
		return HandlePnP(DeviceObject, Irp);

	case IRP_MJ_READ:
		return HandleRead(DeviceObject, Irp);

	default:
		return PassIrp(DeviceObject, Irp);
	}
}

static VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject)
{
	DbgPrint("Driver Unload\n");

	while (KeyCount > 0);
	DetachAndDeleteDevice(DriverObject);

	DbgPrint("Driver Unload 1\n");

}

static NTSTATUS
PassIrp(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp)
{
	if (DeviceObject == NULL || Irp == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(DeviceObject->DeviceObjectExtension->AttachedTo, Irp);
}

static NTSTATUS
HandleCreateClose(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp)
{
	if (DeviceObject == NULL || Irp == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
}

static NTSTATUS
HandleIoCtrl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp)
{
	if (DeviceObject == NULL || Irp == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	PIO_STACK_LOCATION loaction = IoGetCurrentIrpStackLocation(Irp);
	if (loaction == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	ULONG ctrlCode = loaction->Parameters.DeviceIoControl.IoControlCode;
	ULONG inLen = loaction->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outLen = loaction->Parameters.DeviceIoControl.OutputBufferLength;
	PVOID buf = Irp->AssociatedIrp.SystemBuffer;

	Irp->IoStatus.Information = 0;
	if (loaction->Parameters.DeviceIoControl.IoControlCode == 111) {
		Irp->IoStatus.Status = STATUS_SUCCESS;
	} else {
		Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	}
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
}

static NTSTATUS
HandlePower(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp)
{
	if (DeviceObject == NULL || Irp == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	PoStartNextPowerIrp(Irp);
	IoSkipCurrentIrpStackLocation(Irp);

	return PoCallDriver(DeviceObject->DeviceObjectExtension->AttachedTo, Irp);
}

static NTSTATUS
HandlePnP(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp)
{
	if (DeviceObject == NULL || Irp == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	PIO_STACK_LOCATION stackLoc = IoGetCurrentIrpStackLocation(Irp);
	if (stackLoc->MinorFunction == IRP_MN_REMOVE_DEVICE) {
		NTSTATUS status = PassIrp(DeviceObject, Irp);
		DetachAndDeleteDevice(DeviceObject->DriverObject);
		return status;
	} else {
		return PassIrp(DeviceObject, Irp);
	}
}

static NTSTATUS
ReadComplete(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp,
	_In_ PVOID Context)
{
	if (NT_SUCCESS(Irp->IoStatus.Status)) {
		/*
		PUCHAR buf = Irp->AssociatedIrp.SystemBuffer;
		ULONG len = Irp->IoStatus.Information;
		ULONG i;

		DbgPrint("key code:");
		for (i = 0; i < len; i++) {
			DbgPrint(" [%d]%02x", i, buf[i]);
		}
		DbgPrint("\n");
		*/

		///*
		ULONG i;
		PKEYBOARD_INPUT_DATA inputData = (PKEYBOARD_INPUT_DATA) Irp->AssociatedIrp.SystemBuffer;
		ULONG size = Irp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA);
		DbgPrint("size: %d\n", size);

		for (i = 0; i < size; i++) {
			DbgPrint("UnitId: %04x, MakeCode: %04x, Flags: %04x\n",
				inputData[i].UnitId,
				inputData[i].MakeCode,
				inputData[i].Flags);

			if (IsDisabledKey(inputData[i].MakeCode)) {
				DbgPrint("Key Disabled\n");
				inputData[i].MakeCode = 0x002a; //left shift
			}
		}
		//*/
	}

	KeyCount--;

	if (Irp->PendingReturned) {
		IoMarkIrpPending(Irp);
	}

	return Irp->IoStatus.Status;
}

static NTSTATUS
HandleRead(
	_In_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp)
{
	if (Irp->CurrentLocation == 1) {
		DbgPrint("Dispatch encountered bogus current location\n");
		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return Irp->IoStatus.Status;
	}

	KeyCount++;

	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp, ReadComplete, NULL, TRUE, TRUE, TRUE);
	return IoCallDriver(DeviceObject->DeviceObjectExtension->AttachedTo, Irp);
}

static NTSTATUS
CreateAndAttachDevice(
	_In_ PDRIVER_OBJECT sourceDriver,
	_In_ PDRIVER_OBJECT targetDriver)
{
	if (sourceDriver == NULL || targetDriver == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	PDEVICE_OBJECT targetDevice;
	ULONG i = 0;
	for (targetDevice = targetDriver->DeviceObject; targetDevice != NULL; targetDevice = targetDevice->NextDevice) {
		PDEVICE_OBJECT sourceDevice;
		WCHAR name[32] = { 0 };
		UNICODE_STRING devName;
		RtlStringCchPrintfW(name, sizeof(name), L"\\Device\\MyDev%d", i++);
		RtlInitUnicodeString(&devName, name);

		NTSTATUS status = IoCreateDevice(
					sourceDriver,
					0,
					&devName,
					targetDevice->DeviceType,
					targetDevice->Characteristics,
					FALSE,
					&sourceDevice);
		if (!NT_SUCCESS(status)) {
			DetachAndDeleteDevice(sourceDriver);
			return status;
		}

		PDEVICE_OBJECT prevTopDev = IoAttachDeviceToDeviceStack(sourceDevice, targetDevice);
		if (prevTopDev == NULL) {
			DetachAndDeleteDevice(sourceDriver);
			return STATUS_UNSUCCESSFUL;
		}

		sourceDevice->DeviceType = prevTopDev->DeviceType;
		sourceDevice->Characteristics = prevTopDev->Characteristics;
		sourceDevice->StackSize = prevTopDev->StackSize + 1;
		sourceDevice->Flags |= prevTopDev->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
	}

	return STATUS_SUCCESS;
}

static VOID
DetachAndDeleteDevice(
	_In_ PDRIVER_OBJECT driver)
{
	if (driver == NULL) {
		return;
	}

	PDEVICE_OBJECT device;
	for (device = driver->DeviceObject; device != NULL; device = device->NextDevice) {
		IoDetachDevice(device->DeviceObjectExtension->AttachedTo);
		IoDeleteDevice(device);
	}
}

static BOOLEAN
IsDisabledKey(USHORT key)
{
	ULONG size = sizeof(DisabledKey) / sizeof(DisabledKey[0]);
	ULONG i;

	for (i = 0; i < size; i++) {
		if (key == DisabledKey[i]) {
			return TRUE;
		}
	}

	return FALSE;
}

extern POBJECT_TYPE *IoDriverObjectType;
extern NTSTATUS
ObReferenceObjectByName(
	_In_ PUNICODE_STRING ObjectName,
	_In_ ULONG Attributes,
	_In_opt_ PACCESS_STATE AccessState,
	_In_opt_ ACCESS_MASK DesiredAccess,
	_In_ POBJECT_TYPE ObjectType,
	_In_ KPROCESSOR_MODE AccessMode,
	_Inout_opt_ PVOID ParseContext,
	_Out_ PVOID *Object);

static NTSTATUS
getDriverByName(
	_In_ PUNICODE_STRING driverName,
	_Out_ PDRIVER_OBJECT *driver)
{
	if (driverName == NULL || driver == NULL) {
		return STATUS_INVALID_PARAMETER;
	}

	return ObReferenceObjectByName(
		driverName,
		OBJ_CASE_INSENSITIVE,
		NULL,
		0,
		*IoDriverObjectType,
		KernelMode,
		NULL,
		(PVOID *) driver);
}
