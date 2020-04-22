#include <ntddk.h>
#include "I8042.h"


#define  DELAY_ONE_MICROSECOND  (-10)
#define  DELAY_ONE_MILLISECOND (DELAY_ONE_MICROSECOND*1000)
#define  DELAY_ONE_SECOND (DELAY_ONE_MILLISECOND*1000)

#define MAKE_UINT32(low, high) ((UINT32) (((UINT16)((UINT32)(low) & 0xffff)) | ((UINT32) ((UINT16)((UINT32)(high) & 0xffff))) << 16))
#define LO16_OF_32(data) ((UINT16) (((UINT32) data) & 0xffff))
#define HI16_OF_32(data) ((UINT16) (((UINT32) data) >> 16))


#define IDT_INDEX 0x93


#pragma pack(push,1)
typedef struct _IDTR {
	UINT16 limit;
	UINT32 base;
} IDTR, *PIDTR;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _IDT_ENTRY {
	UINT16 offset_low;
	UINT16 selector;
	UINT8  reserved : 5;
	UINT8  zero1 : 3;
	UINT8  type : 1; //0: interrupt, 1: trap
	UINT8  zero2 : 1;
	UINT8  gate_size : 1;
	UINT8  zero3 : 1;
	UINT8  zero4 : 1;
	UINT8  dpl : 2;
	UINT8  present : 1;
	UINT16 offset_high;
} IDT_ENTRY, *PIDT_ENTRY;
#pragma pack(pop)


static PVOID OriginalRoutines[MAXIMUM_PROCESSORS+1];
static PIDT_ENTRY IDTBases[MAXIMUM_PROCESSORS+1];

static PIDT_ENTRY
GetIDTBase();

static NTSTATUS
GetIDTBases(
	_Out_ PIDT_ENTRY *IDTBases,
	_In_  ULONG size);

static VOID
UserFilter();

static VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject);

static VOID
InterruptRoutine();


static VOID
BackupOriginalRoutines(
	_In_ PIDT_ENTRY *IDTBases,
	_Out_ PVOID *OriginalRoutines,
	_In_ ULONG index);

static VOID
HookRoutine(
	_Inout_ PIDT_ENTRY *IDTBases,
	_In_ PVOID *OriginalRoutines,
	_In_ PVOID NewRoutine,
	_In_ ULONG index);

static VOID
UNHookRoutine(
	_Inout_ PIDT_ENTRY *IDTBases,
	_In_ PVOID *OriginalRoutines,
	_In_ ULONG index);

static VOID
WriteProtectOn();

static VOID
WriteProtectOff();

static PULONG
GetKiProcessorBlock();


NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT  DriverObject,
	_In_ PUNICODE_STRING RegistryPath)
{
	DbgPrint("Driver Entry\n");

	DriverObject->DriverUnload = DriverUnload;

	RtlZeroMemory(IDTBases, sizeof(IDTBases));
	NTSTATUS status = GetIDTBases(IDTBases, MAXIMUM_PROCESSORS);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	RtlZeroMemory(OriginalRoutines, sizeof(OriginalRoutines));
	BackupOriginalRoutines(IDTBases, OriginalRoutines, IDT_INDEX);

	HookRoutine(IDTBases, OriginalRoutines, UserFilter, IDT_INDEX);

	return STATUS_SUCCESS;
}

static VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject)
{
	DbgPrint("Driver Unload\n");

	//LARGE_INTEGER interval;

	//HookInt93(FALSE);
	UNHookRoutine(IDTBases, OriginalRoutines, IDT_INDEX);

	//interval.QuadPart = (5 * 1000 * DELAY_ONE_MILLISECOND);
	//KeDelayExecutionThread(KernelMode, FALSE, &interval);

	DbgPrint("Driver Unload 1\n");
}


static VOID
UserFilter()
{
	DbgPrint("UserFilter()");

	if (!i8042ReadyForRead()) {
		DbgPrint("i8042 not ready for read");
		return;
	}

	static UINT8 sch_pre = 0;
	UINT8 sch  = i8042GetData();

	DbgPrint("scan code = %02x\n", sch);

	if (sch_pre != sch) {
		i8042PutCtlCmd(I8042_CMD_CTL_DATA_TO_OUT_BUF);

		if (i8042ReadyForWrite()) {
			i8042PutData(sch);
			sch_pre = sch;
		}
	}
}


static VOID
__declspec(naked) InterruptRoutine()
{
	__asm {
		pushad
		pushfd
		push fs

		call UserFilter

		push fs
		popfd
		popad
		//jmp ProcOld;

	}
}

#pragma pack(1)
__declspec(naked) void IntRoutine()
{
	__asm {
		pushfd
		pushad
		push fs

		mov ebx, 30H  // Set FS to PCR.
		mov fs, bx

		//call MyUserFilter

		pop fs
		popad
		popfd
		//jmp ulAddress
	}
}
#pragma pack()


static VOID
BackupOriginalRoutines(
	_In_ PIDT_ENTRY *IDTBases,
	_Out_ PVOID *OriginalRoutines,
	_In_ ULONG index)
{
	if (IDTBases == NULL || OriginalRoutines == NULL) {
		return;
	}

	for (; *IDTBases != NULL; IDTBases++, OriginalRoutines++) {
		PIDT_ENTRY IDTEntry = *IDTBases + index;
		*OriginalRoutines = (PVOID) MAKE_UINT32(IDTEntry->offset_low, IDTEntry->offset_high);
	}
}

static VOID
HookRoutine(
	_Inout_ PIDT_ENTRY *IDTBases,
	_In_ PVOID *OriginalRoutines,
	_In_ PVOID NewRoutine,
	_In_ ULONG index)
{
	DbgPrint("HookRoutine()\n");

	if (IDTBases == NULL || OriginalRoutines == NULL) {
		return;
	}

	for (; *IDTBases != NULL && *OriginalRoutines != NULL; IDTBases++, OriginalRoutines++) {
		PIDT_ENTRY IDTEntry = *IDTBases + index;
		PVOID currentRoutine = (PVOID) MAKE_UINT32(IDTEntry->offset_low, IDTEntry->offset_high);

		DbgPrint(" current routine: %08x\n", currentRoutine);
		DbgPrint("original routine: %08x\n", *OriginalRoutines);
		DbgPrint("    new  routine: %08x\n", NewRoutine);

		if (currentRoutine == NewRoutine) {
			continue;
		}

		WriteProtectOff();
		IDTEntry->offset_low = LO16_OF_32(NewRoutine);
		IDTEntry->offset_high = HI16_OF_32(NewRoutine);
		WriteProtectOn();
	}
}


static VOID
UNHookRoutine(
	_Inout_ PIDT_ENTRY *IDTBases,
	_In_ PVOID *OriginalRoutines,
	_In_ ULONG index)
{
	DbgPrint("UNHookRoutine()\n");

	if (IDTBases == NULL || OriginalRoutines == NULL) {
		return;
	}

	for (; *IDTBases != NULL && *OriginalRoutines != NULL; IDTBases++, OriginalRoutines++) {
		PIDT_ENTRY IDTEntry = *IDTBases + index;
		PVOID currentRoutine = (PVOID) MAKE_UINT32(IDTEntry->offset_low, IDTEntry->offset_high);

		DbgPrint(" current routine: %08x\n", currentRoutine);
		DbgPrint("original routine: %08x\n", *OriginalRoutines);
		if (currentRoutine == *OriginalRoutines) {
			continue;
		}

		WriteProtectOff();
		IDTEntry->offset_low = LO16_OF_32(*OriginalRoutines);
		IDTEntry->offset_high = HI16_OF_32(*OriginalRoutines);
		WriteProtectOn();
	}
}

static VOID
WriteProtectOn()
{
	__asm {
		mov  eax, cr0
		or eax, 10000h
		mov  cr0, eax
		sti
	}
}

static VOID
WriteProtectOff()
{
	__asm {
		cli
		mov  eax, cr0
		and  eax, not 10000h //bit16: 0
		mov  cr0, eax
	}
}

static PIDT_ENTRY
GetIDTBase()
{
	IDTR idtr;
	_asm sidt idtr
	return (PIDT_ENTRY) idtr.base;
}

static NTSTATUS
GetIDTBases(
	_Out_ PIDT_ENTRY *IDTBases,
	_In_  ULONG size)
{
	if (IDTBases == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	DbgPrint("KiProcessorBlock: ");
	PULONG KiProcessorBlock = GetKiProcessorBlock();
	if (KiProcessorBlock == NULL) {
		DbgPrint("NULL\n");
		return STATUS_UNSUCCESSFUL;
	}
	DbgPrint("%08x\n", KiProcessorBlock);

	ULONG i = 0;
	while (*KiProcessorBlock != NULL && i < size) {
		IDTBases[i++] = *(PIDT_ENTRY *) (*KiProcessorBlock - 0x120 + 0x38);
		KiProcessorBlock++;
	}

	return STATUS_SUCCESS;
}

static PULONG
GetKiProcessorBlock()
{
	PULONG KiProcessorBlock = NULL;
	KeSetSystemAffinityThread(1);

	_asm {
		push eax
		mov  eax, FS:[0x34]
		add  eax, 20h
		mov  eax, [eax]
		mov  eax, [eax]
		mov  eax, [eax + 218h]
		mov  KiProcessorBlock, eax
		pop  eax
	}

	KeRevertToUserAffinityThread();
	return KiProcessorBlock;
}

