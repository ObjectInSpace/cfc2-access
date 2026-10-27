/*
 * Power Stone 2 per-frame match audio: health, stones and the round clock
 * through the shared match_audio controller, and positional pings for the
 * things lying in the arena through pickup_cues.c. Same shape as
 * pws1_frame.c; none of PWS1's offsets carry over.
 *
 * Fields, each from the 2026-10-27 live capture and the code that uses it
 * (docs/port-tasks/pws2-match-fields.md). Root R = *0x14262F0B0, fighter
 * A = R + 0x40818 + index*0x42B8, index 0..3:
 *   A+1      slot byte, 0..3
 *   A+0x1DC  health, float, 1000.0 full (set 0x140A66A99; food caps at it,
 *            0x140A5588E); 0 when knocked out
 *   A+0x3C   X, Y (up), Z, floats (the pickup distance 0x140A5559B)
 *   A+0x401C included in the fight
 *   A+0x4028 0 for a fighter a person plays, 1 for the CPU (score only for
 *            people, 0x140A59B11; live: 0,1,1,1 in a one-player game)
 *   A+0x4055 stones held, 0..3 (collector 0x140ADC7A3 and pickup
 *            0x140A55799 refuse a fourth)
 *   A+0x4080 the object a fighter carries (the lift, 0x140A5619D)
 *   R+0x52BD8/9/C/D  1, 2, 1, 5: the fight step (step table 0x140A4CDB8
 *            entry 5 -> 0x140A4C6D0); R+0x52D2B/E zero or nothing runs
 *   R+0x52EE5 fight mode: 1 "ready", 2 fighting, 0 intro and once decided
 *   R+0x52EE0 round clock, signed word; counts only in mode 2 (0x140A6FF4A)
 *   R+0x52D34 frame counter; stops while paused (0x140A4EA25)
 *   R+0x4B10 the object pool, 200 x 0x4C8; list 9 (+0 = 9) holds the arena's
 *            items, props and stones; +0x4B0 item code, +0x4B1 state (2 =
 *            lying, the pickup test 0x140A55522), +0x3C position, +0xB8 a
 *            parent matrix (position is then local)
 *   R+0x51D90 + i*0x308 the view the ordinary draw loads into renderer slot
 *            2 (0x140A5D6A0), i = R+0x52E88, while R+0x52FDC = 0
 *
 * No fighter beacons (user, PWS1 design). The transformation timer is not
 * sounded.
 */
#ifndef CFC2_PWS2_FRAME_H
#define CFC2_PWS2_FRAME_H

#include <windows.h>
#include <stdbool.h>
#include "match_audio.h"
#include "pickup_cues.h"
#include "pickup_speech.h"

#define PWS2_STATE_SIZE 0x8CD40u

BOOL pws2_frame_install(void);
void pws2_frame_remove(void);
/* Detour entries so far; the worker uses it to notice a stalled frame. */
LONG pws2_frame_count(void);
void pws2_frame_log_stats(void);

/* Decode PWS2 state at `root`: health and stones of all four fighters, CPU
 * too (owner = fighter index, fighter_gauges set, each panned to where it is
 * on screen through `proj`, centre without one), and the clock. in_match
 * false outside the fight step's modes 1 and 2. */
void pws2_frame_decode(const unsigned char *root, const float proj[16], match_audio_sample *m);
/* True while a round is being fought (mode 2) in the ordinary single view. */
bool pws2_fighting(const unsigned char *root);
uint32_t pws2_frame_counter(const unsigned char *root);
/* The ping family and type for an item code; false for codes that are not
 * pickups (stage fixtures, unknown codes). */
bool pws2_pickup_kind(unsigned code, audio_ping_family *family, int *variant);
/* Horizontal screen position of world point `pt` through the drawn view and
 * the renderer's projection `proj` (16 floats at ctx+0x1F816C), -1..+1.
 * false: off screen (behind the camera or past an edge), no camera, or not
 * perspective. Nothing off screen is sounded (user, 2026-10-27). */
bool pws2_project(const unsigned char *root, const float proj[16], const float pt[3], float *pan);
/* The pickups lying in the arena, by pool index. `root_address` is where the
 * block lives in the game, for the fighters' carried-object pointers.
 * Nothing is present unless a round is being fought. */
void pws2_pickups_decode(const unsigned char *root, uint64_t root_address, const float proj[16],
                         pickup_sample out[PICKUP_SLOTS]);

/* Pickup announcements: a fighter took something (touched, through the
 * pickup handler 0x140A55430, or lifted, its carried pointer A+0x4080).
 * Game thread produces, the worker pops and speaks. */
enum {
    PWS2_EVENT_PICKUP = 0,        /* fighter, id, code (item code), human     */
    PWS2_EVENT_STAGES,            /* code: stage k's direction 0..7 (ahead,
                                     ahead right, .. ahead left) in bits 3k   */
    PWS2_EVENT_STAGE_CHOSEN,      /* code: the stage, 0..4                    */
};
/* The shared event (pickup_speech.h): fighter index, its native ID, the
 * item code, human = A+0x4028 0. */
typedef pickup_event pws2_pickup_event;
bool pws2_pickup_pop(pws2_pickup_event *e);
/* Pure helpers, exposed for the tests. The arena's lying objects (0x100 |
 * code, 0 for none), by pool index; the code of the first object lying in
 * `before` and not now, or -1; the code of the carried object a fighter's
 * pointer names, or -1 when it names no pool record. */
void pws2_lying_snapshot(const unsigned char *root, unsigned short codes[200]);
int pws2_taken(const unsigned char *root, const unsigned short before[200]);
int pws2_carried_code(const unsigned char *root, uint64_t root_address, uint64_t carried);
/* Per frame: lifted things (a new carried object) and stones (the count
 * rising), as (fighter, item code) pairs; returns how many, at most 8.
 * Nothing while not fighting, nor on the first fought frame. */
typedef struct { bool valid; uint64_t carried[4]; unsigned char stones[4]; } pws2_pickup_watch;
unsigned pws2_watch_events(const unsigned char *root, uint64_t root_address, pws2_pickup_watch *w,
                           unsigned who[8], unsigned code[8]);

/* The stage select (step 3, Stage Selection: Manual): true while the landed
 * fighter walks to one of five stages; the stage chosen (0..4) once the walk
 * ends, else -1; the five spots as pings by pool index (a spot off screen
 * pings at its edge); stage k's direction from the walking person's fighter
 * as the camera faces, 0 ahead .. 7 ahead left, or -1. */
bool pws2_stage_walk(const unsigned char *root);
int pws2_stage_chosen(const unsigned char *root);
void pws2_spots_decode(const unsigned char *root, const float proj[16], pickup_sample out[PICKUP_SLOTS]);
int pws2_spot_direction(const unsigned char *root, unsigned stage);

#endif
