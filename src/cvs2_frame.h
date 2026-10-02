/*
 * Frame-accurate health readout for CVS2.
 *
 * WHY A HOOK. The 10 ms worker poll samples asynchronously, so a health change
 * is seen up to a frame or two after it happens -- the user reported the tone
 * lagging the damage, and no amount of buffer tuning fixes a stale READ. The
 * only way to be keyed to the frame is to read at a fixed point INSIDE the
 * frame, every frame.
 *
 * THE TARGET. 0x1406AA7A0 is CVS2's per-frame update: it increments the frame
 * counter at `inc dword ptr [rax+0x18]` (0x1406AA7BB) and then runs the input
 * edge detection the state trace documents. It has EXACTLY ONE caller
 * (0x1406A9A19), so it is a single per-frame entry point, and it takes no
 * arguments -- it resolves the root pointer itself. Nothing to get wrong about
 * a signature, unlike the hit-handler probe that hooked a cold path.
 *
 * ⛔⛔ THE DETOUR RUNS ON THE GAME'S FRAME THREAD. It does the minimum: read
 * the root pointer, read four u16s, store them in a lock-free slot, return.
 *   - NO VirtualQuery. `cvs2_hit.c` called mem_readable() twice per invocation,
 *     i.e. two syscalls per hit on the render thread. That is the wall this
 *     project has hit FOUR times (gmd_find, heapdiff, span scan, arena sweep).
 *   - NO locks, NO allocation, NO logging, NO speech.
 *   - Bounds are checked with plain arithmetic against the known block size.
 *
 * ⚠ The values are published for the AUDIO layer, which is lock-free and
 * wait-free by construction (a seqlock). Speech never runs here.
 *
 * Installed whenever the audio device opens; `cfc2_hook.off` disables it
 * like every other hook.
 */
#ifndef CFC2_FRAME_H
#define CFC2_FRAME_H

#include <windows.h>

BOOL cvs2_frame_install(void);
void cvs2_frame_remove(void);

/* Frames seen, and frames on which a health value actually changed. Liveness
 * before findings: "no tones" means nothing unless we know the detour ran. */
void cvs2_frame_log_stats(void);

#endif /* CFC2_FRAME_H */
