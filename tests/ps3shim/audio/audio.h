#ifndef SHIM_AUDIO_H
#define SHIM_AUDIO_H
#include "../ppu-types.h"
#define AUDIO_BLOCK_SAMPLES 256
#define AUDIO_PORT_2CH 2
#define AUDIO_PORT_INITLEVEL 0x1000
#define AUDIO_BLOCK_16 16
typedef struct { u64 numChannels; u64 numBlocks; u64 attrib; f32 level; } audioPortParam;
typedef struct { u32 readIndex; u32 status; u64 channelCount; u64 numBlocks; u32 portSize; u32 audioDataStart; } audioPortConfig;
s32 audioInit(void); s32 audioQuit(void);
s32 audioPortOpen(audioPortParam *param, u32 *portNum);
s32 audioPortStart(u32 portNum); s32 audioPortStop(u32 portNum); s32 audioPortClose(u32 portNum);
s32 audioGetPortConfig(u32 portNum, audioPortConfig *config);
#endif
