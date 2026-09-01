/*
 * Runtime observation of CFC2's CRC-32 primitives.
 *
 * Static tracing took us as far as it goes. It CONFIRMED the hash primitive at
 * 0x1402ef1e0 by execution, but its six direct callers are all graphics-side
 * (sampler/render-state descriptors), and the second CRC implementation at
 * 0x1405a4a10 has NO direct callers at all -- it is reached through a pointer.
 * MTFramework wraps CRC in a hasher object with a vtable, so the call graph is
 * simply not visible in the bytes.
 *
 * So stop inferring and observe. Detour the primitives, and for every call
 * record the return address and the buffer being hashed. Navigating a menu
 * then tells us directly which code hashes GMD-looking labels -- the caller
 * set we could not reach statically.
 *
 * This is the same discipline the rest of the project runs on: the buffer
 * contents are ground truth (we know all 4165 labels), so a hit is an
 * identification rather than a resemblance.
 */
#ifndef CFC2_HASH_WATCH_H
#define CFC2_HASH_WATCH_H

#include <windows.h>

/* Install the detours. Safe to call only after the image is unpacked.
 * Returns TRUE if at least one hook was installed. */
BOOL hash_watch_install(void);

/* Log what has been observed so far: distinct callers, and a sample of the
 * strings each one hashed. Called periodically so evidence appears in the log
 * without the user having to trigger anything. */
void hash_watch_report(void);

void hash_watch_shutdown(void);

#endif /* CFC2_HASH_WATCH_H */
