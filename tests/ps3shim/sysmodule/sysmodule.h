#ifndef SHIM_SYSMODULE_H
#define SHIM_SYSMODULE_H
#include "../ppu-types.h"
enum { SYSMODULE_NET = 0, SYSMODULE_AUDIO = 0x11 };
s32 sysModuleLoad(u16 id); s32 sysModuleUnload(u16 id);
#endif
