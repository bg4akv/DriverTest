#include <ntddk.h>
#include "I8042.h"

#define I8042_PORT_CMD  0x64
#define I8042_PORT_DATA 0x60

#define I8042_STATUS_OUT_BUF_FULL 0x01
#define I8042_STATUS_IN_BUF_FULL  0x02


UINT8
i8042GetData()
{
	UINT8 data;

	__asm in al, I8042_PORT_DATA;
	__asm mov data, al;

	return data;
}

VOID
i8042PutData(UINT8 data)
{
	__asm mov al, data
	__asm out I8042_PORT_DATA, al
}

UINT8
i8042GetStatus()
{
	UINT8 status;

	_asm in al, I8042_PORT_CMD
	_asm mov status, al

	return status;
}

VOID
i8042PutCtlCmd(UINT8 cmd)
{
	_asm mov al, cmd
	_asm out I8042_PORT_CMD, al
}

BOOLEAN
i8042ReadyForRead()
{
	int i;

	for (i = 0; i < 100; i++) {
		if (i8042GetStatus() & I8042_STATUS_OUT_BUF_FULL) {
			return TRUE;
		}
		KeStallExecutionProcessor(50);
	}

	return FALSE;
}

BOOLEAN
i8042ReadyForWrite()
{
	int i;

	for (i = 0; i < 100; i++) {
		if (!(i8042GetStatus() & I8042_STATUS_IN_BUF_FULL)) {
			return TRUE;
		}
		KeStallExecutionProcessor(50);
	}

	return FALSE;
}
