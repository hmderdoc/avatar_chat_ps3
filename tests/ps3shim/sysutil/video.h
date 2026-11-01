#ifndef SHIM_VIDEO_H
#define SHIM_VIDEO_H
#include "../ppu-types.h"
#define VIDEO_BUFFER_FORMAT_XRGB 0
typedef struct { u8 resolution; u8 scanMode; u8 conversion; u8 aspect; u8 reserved[2]; u16 refreshRates; } videoDisplayMode;
typedef struct { u8 state; u8 colorSpace; u8 reserved[6]; videoDisplayMode displayMode; } videoState;
typedef struct { u16 width; u16 height; } videoResolution;
typedef struct { u8 resolution; u8 format; u8 aspect; u8 reserved[9]; u32 pitch; } videoConfiguration;
s32 videoGetState(s32 videoOut, s32 deviceIndex, videoState *state);
s32 videoGetResolution(s32 resolutionId, videoResolution *resolution);
s32 videoConfigure(s32 videoOut, videoConfiguration *config, void *option, s32 waitForEvent);
#endif
