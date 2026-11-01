#ifndef SHIM_KB_H
#define SHIM_KB_H
#include "../ppu-types.h"
#define MAX_KEYBOARDS 7
#define KB_RAWDAT 0x8000
#define KB_CODETYPE_ASCII 1
#define KB_RMODE_INPUTCHAR 0
#define KB_RAWKEY_ENTER 0x28
#define KB_RAWKEY_ESCAPE 0x29
#define KB_RAWKEY_BS 0x2A
#define KB_RAWKEY_TAB 0x2B
#define KB_RAWKEY_F1 0x3A
#define KB_RAWKEY_F2 0x3B
#define KB_RAWKEY_F3 0x3C
#define KB_RAWKEY_HOME 0x4A
#define KB_RAWKEY_PAGE_UP 0x4B
#define KB_RAWKEY_DELETE 0x4C
#define KB_RAWKEY_END 0x4D
#define KB_RAWKEY_PAGE_DOWN 0x4E
#define KB_RAWKEY_RIGHT_ARROW 0x4F
#define KB_RAWKEY_LEFT_ARROW 0x50
#define KB_RAWKEY_DOWN_ARROW 0x51
#define KB_RAWKEY_UP_ARROW 0x52
#define KB_RAWKEY_KPAD_ENTER 0x58
typedef struct { u8 max; u8 connected; u8 info; u8 status[MAX_KEYBOARDS]; } KbInfo;
typedef struct { u32 led; u32 mkey; s32 nb_keycode; u16 keycode[62]; } KbData;
s32 ioKbInit(u32 max);
s32 ioKbEnd(void);
s32 ioKbGetInfo(KbInfo *info);
s32 ioKbRead(u32 port, KbData *data);
s32 ioKbSetCodeType(u32 port, u32 type);
s32 ioKbSetReadMode(u32 port, u32 mode);
#endif
