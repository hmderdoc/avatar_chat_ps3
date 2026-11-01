#ifndef SHIM_RSX_H
#define SHIM_RSX_H
#include "../ppu-types.h"
typedef struct gcmContextData gcmContextData;
gcmContextData *rsxInit(u32 cmdSize, u32 ioSize, void *ioAddress);
void *rsxMemalign(u32 alignment, u32 size);
s32 rsxAddressToOffset(void *ptr, u32 *offset);
void rsxFlushBuffer(gcmContextData *ctx);
void rsxFinish(gcmContextData *ctx, u32 ref);
#define GCM_FLIP_VSYNC 1
s32 gcmSetDisplayBuffer(u8 id, u32 offset, u32 pitch, u32 width, u32 height);
void gcmSetFlipMode(u32 mode);
s32 gcmSetFlip(gcmContextData *ctx, u8 id);
void gcmSetWaitFlip(gcmContextData *ctx);
s32 gcmGetFlipStatus(void);
void gcmResetFlipStatus(void);
#endif
