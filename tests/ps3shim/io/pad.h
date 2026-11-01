#ifndef SHIM_PAD_H
#define SHIM_PAD_H
#include "../ppu-types.h"
#define MAX_PADS 7
typedef struct { u32 max; u32 connected; u32 info; u8 status[MAX_PADS]; } padInfo;
typedef struct {
    s32 len; u32 reserved;
    u32 BTN_LEFT, BTN_DOWN, BTN_RIGHT, BTN_UP, BTN_START, BTN_R3, BTN_L3, BTN_SELECT;
    u32 BTN_SQUARE, BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE, BTN_R1, BTN_L1, BTN_R2, BTN_L2;
} padData;
s32 ioPadInit(u32 max);
s32 ioPadEnd(void);
s32 ioPadGetInfo(padInfo *info);
s32 ioPadGetData(u32 port, padData *data);
#endif
