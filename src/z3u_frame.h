/*
 * Street Fighter Alpha 3 per-frame match audio: health, super gauge, fighter
 * position and the round clock on the frame they change, decoded into a
 * match_audio sample. The cue policy is shared (src/match_audio.c); this file
 * only reads Alpha 3's own fields (ordinary branch, ROMs z3u/a3u).
 *
 * Fields, each from a labelled capture and the code that uses it
 * (docs/port-tasks/z3u-match-fields.md). Player object = R + 0x1B0 + side*0x550:
 *   +0x88  health, signed 16-bit, maximum 0x90 (round setup 0x14086CE72;
 *          damage 0x1408B110C floors it at 0; -1 at a KO)
 *   +0x1EE super gauge, maximum 0x90 (an X-ism super needs exactly 0x90,
 *          0x140888475; the match setup fills it to 0x90, 0x14089A21B)
 *   +0x10  X, +0x14 Y, signed 16.16; ground is Y = 40 (landing 0x1408C51A8)
 *   R+0xC55A camera left edge: screen X = +0x12 - R+0xC55A (0x140872550)
 *   R+0x28/+0x2A/+0x2C words 1, 2, 3 in a match; R+0x2E round stage
 *   R+0xF1 round clock (capture only; its decrement is not located)
 *
 * Not decoded, so not sounded: crouch, knockdown and the guard meter.
 */
#ifndef CFC2_Z3U_FRAME_H
#define CFC2_Z3U_FRAME_H

#include <windows.h>
#include "match_audio.h"

BOOL z3u_frame_install(void);
void z3u_frame_remove(void);
/* Detour entries so far; the worker uses it to notice a stalled frame. */
LONG z3u_frame_count(void);
void z3u_frame_log_stats(void);

/* Decode Alpha 3 state at `root` (the published primary block). Pure
 * arithmetic on the bytes; in_match false outside the match screen. */
void z3u_frame_decode(const unsigned char *root, match_audio_sample *m);

#endif
