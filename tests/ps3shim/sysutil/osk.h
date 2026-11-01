#ifndef SHIM_OSK_H
#define SHIM_OSK_H
#include "../ppu-types.h"
#define OSK_10KEY_PANEL 1
#define OSK_FULLKEY_PANEL 2
#define OSK_PANEL_TYPE_DEFAULT 0
#define OSK_PROHIBIT_RETURN 2
#define OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER 0x400
#define OSK_LAYOUTMODE_VERTICAL_ALIGN_BOTTOM 0x1000
typedef enum { OSK_OK, OSK_CANCELED, OSK_ABORT, OSK_NO_TEXT } oskInputFieldResult;
typedef enum { OSK_DEVICE_PAD, OSK_DEVICE_KB } oskInputDevice;
typedef struct { f32 x, y; } oskPoint;
typedef struct { u32 allowedPanels; u32 firstViewPanel; oskPoint controlPoint; s32 prohibitFlags; } oskParam;
typedef struct { u16 *message; u16 *startText; s32 maxLength; } oskInputFieldInfo;
typedef struct { u32 res; s32 len; u16 *str; } oskCallbackReturnParam;
#include "../sys/memory.h"
s32 oskLoadAsync(sys_mem_container_t container, const oskParam *param, const oskInputFieldInfo *inputInfo);
s32 oskUnloadAsync(oskCallbackReturnParam *param);
s32 oskAbort(void);
s32 oskSetLayoutMode(s32 mode);
s32 oskSetKeyLayoutOption(u32 flags);
s32 oskSetInitialInputDevice(oskInputDevice input);
#endif
