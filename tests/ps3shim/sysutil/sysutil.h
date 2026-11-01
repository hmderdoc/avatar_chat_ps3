#ifndef SHIM_SYSUTIL_H
#define SHIM_SYSUTIL_H
#include "../ppu-types.h"
#define SYSUTIL_EVENT_SLOT0 0
#define SYSUTIL_EXIT_GAME 0x0101
#define SYSUTIL_OSK_LOADED 0x0502
#define SYSUTIL_OSK_DONE 0x0503
#define SYSUTIL_OSK_UNLOADED 0x0504
#define SYSUTIL_OSK_INPUT_CANCELED 0x0506
typedef void (*sysutilCallback)(u64 status, u64 param, void *usrdata);
s32 sysUtilRegisterCallback(s32 slot, sysutilCallback cb, void *usrdata);
s32 sysUtilCheckCallback(void);
#endif
