/*
 * CVS1 per-frame match audio: health, maximum health and fighter position on
 * the frame they change, decoded into a match_audio sample. The cue policy is
 * shared (src/match_audio.c); this file only reads CVS1's own fields.
 *
 * Fields, each from a labelled capture AND the code that writes it
 * (docs/port-tasks/cvs1-match-fields.md). Actor = R + 0x3FB8 + side*0x700:
 *   +0x1FC health, signed 16-bit (damage 0x1405B817D floors it at 0)
 *   +0x200 maximum health (0x1405B8224 resets health to 3/10 of it)
 *   +0xB4  X, +0xB8 height, signed 16.16 (motion routine 0x1405EA130)
 *
 *   +0x2D1 power gauge, cap 144 or 100 by groove flag +0x2C1
 *   P+0x6C round clock (P = R + 0x3E58)
 *
 * Not decoded, so not sounded: crouch and knockdown. Unknown stance is
 * treated as standing, never guessed.
 */
#ifndef CFC2_CVS1_FRAME_H
#define CFC2_CVS1_FRAME_H

#include <windows.h>
#include "match_audio.h"

BOOL cvs1_frame_install(void);
void cvs1_frame_remove(void);
/* Detour entries so far; the worker uses it to notice a stalled frame. */
LONG cvs1_frame_count(void);
void cvs1_frame_log_stats(void);

/* Decode CVS1 state at `root` (the published primary block). Pure arithmetic
 * on the bytes; in_match false outside phase 02 01 02 02. */
void cvs1_frame_decode(const unsigned char *root, match_audio_sample *m);

#endif
