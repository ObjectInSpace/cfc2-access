/*
 * Project Justice per-frame match audio: health, guts gauge, fighter screen
 * side and height, decoded into a match_audio sample. The cue policy is
 * shared (src/match_audio.c); this file only reads MJUS's own fields.
 *
 * Fields, from labelled captures and the code that uses each
 * (docs/port-tasks/mjus-match-fields.md). Fighter = R + 0x3B48 + side*0x608
 * (0x140B93055: imul 0x608, lea rcx,[rbx+0x3B48]):
 *   +0x2DE health, signed 16-bit, maximum 200 (0x140B9304A: mov ebp,0xC8)
 *   +0x3CC guts gauge progress, +0x3CD level 0..5 (capped at 5, 0x140C41B10)
 *   +0xD0 world position (3 floats), projected through the game's own camera
 *          (ctx+0x1F812C view, +0x1F816C projection) for the beacon pan
 *   +0xF0  height above the floor (float), for the jump pitch
 *   +0x2FF bit 0 facing: set = on the right, input left/right swapped
 *          (0x140B98A1A); the test's independent check on the projection
 *   R+0x21C0 game phase: 2 = fighting (dispatcher 0x140B9E1F2)
 *
 * No clock cue: the user asked for none (the game already sounds it).
 * Not decoded, so not sounded: crouch and knockdown.
 */
#ifndef CFC2_MJUS_FRAME_H
#define CFC2_MJUS_FRAME_H

#include <windows.h>
#include "match_audio.h"

#define MJUS_FRAME_FN 0x140B93500ull

BOOL mjus_frame_install(void);
void mjus_frame_remove(void);
LONG mjus_frame_count(void);
void mjus_frame_log_stats(void);

/* Decode MJUS state at `root` (the published block) with the game's camera
 * `cam` (16 floats view at ctx+0x1F812C, then 16 projection at +0x1F816C), or
 * NULL for none: then no beacon. Pure arithmetic; in_match false outside
 * phase 2. */
void mjus_frame_decode(const unsigned char *root, const float cam[32], match_audio_sample *m);
/* Horizontal screen position of a world point through `cam`, -1..+1 across
 * the screen; FALSE when `cam` is not a perspective camera or the point is
 * behind it. */
BOOL mjus_project_x(const float cam[32], const float pt[3], float *ndc_x);

#endif
