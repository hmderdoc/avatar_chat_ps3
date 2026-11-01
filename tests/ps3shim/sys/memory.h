#ifndef SHIM_SYS_MEMORY_H
#define SHIM_SYS_MEMORY_H
#include <stddef.h>
#include "../ppu-types.h"
typedef u32 sys_mem_container_t;
s32 sysMemContainerCreate(sys_mem_container_t *container, size_t size);
s32 sysMemContainerDestroy(sys_mem_container_t container);
#endif
