#ifndef SHIM_SYS_PROCESS_H
#define SHIM_SYS_PROCESS_H
#define SYS_PROCESS_PARAM(prio, stacksize) static const int shim_process_param[2] = { prio, stacksize };
#endif
