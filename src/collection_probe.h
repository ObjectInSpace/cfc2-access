#ifndef CFC2_COLLECTION_PROBE_H
#define CFC2_COLLECTION_PROBE_H
#include <windows.h>

/* Optional diagnostics only. Caller requires cfc2_collection_probe.on and
 * honors cfc2_hook.off. No speech/audio or game-state writes. Hooks record
 * bounded events; only the worker writes the capture file. */
BOOL collection_probe_install(void);
void collection_probe_tick(void);
#endif
