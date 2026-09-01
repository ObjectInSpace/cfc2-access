#ifndef CFC2_MOD_H
#define CFC2_MOD_H

#include <windows.h>

/* Called from DllMain on PROCESS_ATTACH. Must do almost nothing itself —
 * it only spawns the worker thread, because DllMain runs under the loader
 * lock and LoadLibrary from there deadlocks. */
void mod_start(HINSTANCE self);
void mod_stop(void);

#endif /* CFC2_MOD_H */
