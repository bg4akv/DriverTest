#pragma once

#include <ntddk.h>

#define I8042_CMD_CTL_DATA_TO_OUT_BUF 0xd2


UINT8
i8042GetData();

VOID
i8042PutData(UINT8 data);

UINT8
i8042GetStatus();

VOID
i8042PutCtlCmd(UINT8 cmd);

BOOLEAN
i8042ReadyForWrite();

BOOLEAN
i8042ReadyForRead();
