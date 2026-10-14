/*
 * CFJ per-frame match audio: health, power gauge, fighter position and the
 * round clock on the frame they change, decoded into a match_audio sample.
 * The cue policy is shared (src/match_audio.c); this file only reads CFJ's
 * own fields.
 *
 * Fields, each from a labelled capture and, except where noted, the code
 * that uses it (docs/port-tasks/cfj-match-fields.md).
 * Fighter = R + 0x1E678 + side*0x660:
 *   +0x16C health, signed 16-bit (damage 0x1407E6367 floors it at 0, KO -1)
 *   +0x170 maximum health (0x1407D5961: health*100/maximum)
 *   +0x54  X, +0x58 height, signed 16.16 (motion 0x14081EEA1)
 *   +0x294 power gauge, cap by gauge type +0x221 (gain 0x1407E6033)
 *   R+0xC8 phase chain, 02 01 02 02 in a match; R+0xCC round stage
 *   R+0x1D4 round clock (capture only; its decrement is not located)
 *
 * Not decoded, so not sounded: crouch and knockdown. Unknown stance is
 * treated as standing, never guessed.
 */
#ifndef CFC2_CFJ_FRAME_H
#define CFC2_CFJ_FRAME_H

#include <windows.h>
#include "match_audio.h"

BOOL cfj_frame_install(void);
void cfj_frame_remove(void);
/* Detour entries so far; the worker uses it to notice a stalled frame. */
LONG cfj_frame_count(void);
void cfj_frame_log_stats(void);

/* Decode CFJ state at `root` (the published primary block). Pure arithmetic
 * on the bytes; in_match false outside phase 02 01 02 02. */
void cfj_frame_decode(const unsigned char *root, match_audio_sample *m);

#endif
