/*
 * Plasma Sword per-frame match audio: health, plasma gauge, fighter position
 * and the round clock on the frame they change, decoded into a match_audio
 * sample. The cue policy is shared (src/match_audio.c); this file only reads
 * STG2's own fields.
 *
 * Fields, each from the labelled capture and the code that uses it
 * (docs/port-tasks/stg2-match-fields.md). Fighter = R + side*0xA40:
 *   +0x57C health, signed 16-bit, 200 at the start of fighting (damage
 *          0x140D19DA9; below 0 on the KO frame, the overkill)
 *   +0x220 X, +0x224 height (up is negative), +0x228 Z, floats
 *   +0x428 native character ID, 1..12 (with the row flag +0x8A1 the shell
 *          ID, published at 0x140CCC783); indexes the plasma cap table
 *   R+0x4DFF0+2*side plasma fill, R+0x4DFF8+side plasma level (gain
 *          0x140CD9750: 0x90 a level, the cap from table 0x141954058)
 *   R+0x4DF33 round clock (0x140CD7301, every 60 frames), frozen while
 *          R+0x4E598 marks an infinite timer
 *   R+0x4E5EC mode: 3 round intro, 4 fighting, 11 round over
 *   R+0x4D4B4 the game's camera matrix, handed to the shared renderer; the
 *          pan is the fighter's position through it and the renderer's own
 *          view and projection (render_project.h)
 *
 * Not decoded, so not sounded: crouch, knockdown and side steps. Unknown
 * stance is treated as standing, never guessed.
 */
#ifndef CFC2_STG2_FRAME_H
#define CFC2_STG2_FRAME_H

#include <windows.h>
#include "match_audio.h"

BOOL stg2_frame_install(void);
void stg2_frame_remove(void);
/* Detour entries so far; the worker uses it to notice a stalled frame. */
LONG stg2_frame_count(void);
void stg2_frame_log_stats(void);

/* Decode STG2 state at `root` (the published primary block) with the
 * renderer's camera `cam` (16 floats view at ctx+0x1F812C, then 16 projection
 * at +0x1F816C), or NULL for none: then no beacon. Pure arithmetic; in_match
 * false outside the in-game phase's modes 3, 4, 11. */
void stg2_frame_decode(const unsigned char *root, const float cam[32], match_audio_sample *m);

#endif
