/*
 * Power Stone per-frame match audio: health, stones and the round clock
 * through the shared match_audio controller, and positional pings for the
 * things lying in the arena through pickup_cues.c. This file only reads
 * PWS1's own fields.
 *
 * Fields, each from the live capture and the code that uses it
 * (docs/port-tasks/pws1-match-fields.md). Root R = *0x1423188C0, fighter
 * A = R + 0x18428 + index*0x7890, index 0 and 1:
 *   A+0x7790 health, signed 16-bit, 240 full (refill caps 0x140986317,
 *            0x140993775); -1 is the KO (fighting ends below 0, 0x140993804)
 *   A+0x77EC stones held, 0..3 (collector 0x140986150 refuses at 3)
 *   A+0x30   X, Y (up), Z, floats; the model translation (0x1409B42CC)
 *   A+3      slot byte, 0 or 1
 *   R+0x27E5D phase: 4 = match (jump table 0x1409772D8 entry 4 -> 0x140990B20)
 *   R+0x27FAC match sub-state (table 0x141523970): 2 round intro, 3 fighting,
 *            4 and 5 round over; R+0x27FAD its step, 0 while fighting
 *   R+0x27FCD round clock, frozen while R+0x27E7D is set or R+0x27E92 = 4
 *   R+0x27F94 frame counter (0x140976AFD); stopped while paused
 *   R+0 ..   the object pool, 100 x 0x3E0: stones, items, liftable props
 *   R+0x27974 the stored view the ordinary draw loads (0x1409A8865) while
 *            R+0x280C4 = 0; a pickup's pan is its position through it and the
 *            renderer's projection (render_project.h)
 *
 * No fighter beacons (user, 2026-10-24: the game's own footsteps place the
 * fighters). Not decoded, so not sounded: the transformation timer.
 */
#ifndef CFC2_PWS1_FRAME_H
#define CFC2_PWS1_FRAME_H

#include <windows.h>
#include <stdbool.h>
#include "match_audio.h"
#include "pickup_cues.h"
#include "pickup_speech.h"

BOOL pws1_frame_install(void);
void pws1_frame_remove(void);
/* Detour entries so far; the worker uses it to notice a stalled frame. */
LONG pws1_frame_count(void);
void pws1_frame_log_stats(void);

/* Decode PWS1 state at `root`: health, stones, clock. Pure arithmetic;
 * in_match false outside the match phase's sub-states 2..5. */
void pws1_frame_decode(const unsigned char *root, match_audio_sample *m);
/* True while a round is being fought in the ordinary single view. */
bool pws1_fighting(const unsigned char *root);
uint32_t pws1_frame_counter(const unsigned char *root);
/* Horizontal screen position of world point `pt` through the stored view and
 * the renderer's projection `proj` (16 floats at ctx+0x1F816C), -1..+1.
 * false: off screen (behind the camera or past an edge), no camera, or not
 * perspective. Nothing off screen is sounded (user, 2026-10-27). */
bool pws1_project(const unsigned char *root, const float proj[16], const float pt[3], float *pan);
/* The pickups lying in the arena, by pool index, with family, type, pan and
 * distance to the nearest human fighter. `root_address` is where the block
 * lives in the game, for comparing the fighters' carried-object pointers.
 * Nothing is present unless a round is being fought. */
/* Pickup announcements: per frame, the stones gained, things lifted and food
 * eaten, as (side, pickup_kind) pairs; at most 6. Nothing while not fighting
 * nor on the first fought frame. The frame hook queues them; the worker pops
 * (event code = pickup_kind, id = the fighter's shell ID A+4). */
typedef struct { bool valid; uint64_t carried[2]; unsigned char stones[2]; short hp[2];
                 unsigned food_frames[2]; } pws1_pickup_watch;
unsigned pws1_watch_events(const unsigned char *root, uint64_t root_address, pws1_pickup_watch *w,
                           unsigned who[6], unsigned kind[6]);
bool pws1_pickup_pop(pickup_event *e);
void pws1_pickups_decode(const unsigned char *root, uint64_t root_address, const float proj[16],
                         pickup_sample out[PICKUP_SLOTS]);

#endif
