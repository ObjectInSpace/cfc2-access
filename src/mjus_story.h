/*
 * mjus_story -- Project Justice story dialogue as speech.
 *
 * The game draws every story line as a picture (one 128x128 cell per message,
 * AFS entry 34 Japanese / 35 English), so there is no string to hook. The
 * pictures were transcribed into data/mjus_story/ and generated into
 * src/mjus_story_text.h; this module only decides WHICH line is on screen.
 *
 * That decision is read off the root block R (*0x142996830), from the fields
 * the scene interpreter 0x140C367E0 writes (opcode 5 -> 0x140C36784):
 *
 *   R+0x2259                 scene ID (picks the dialogue pack piece)
 *   R+0x2600 + side*0x5C     one message record per speaker side (0, 1):
 *        +0x24  1 shown, 2 closing, 0 none     (0x140C36B30 writes the 2)
 *        +0x2C  message index = the cell in that scene's piece
 *   R+0x206C                 0 = Japanese pictures, else English
 *                            (0x140B9B7E0; pack 0x140BA7F37)
 *
 * A side's record turning to "shown" with a new (scene, message) is one new
 * line. Both sides are tracked separately because one speaker's record stays
 * "closing" while the other speaks.
 *
 * STORY-INTRO NARRATION. Story mode is task routine 7 (0x140BA16D0); its task
 * record (16 at R+0x93F30, stride 0x18: +0 state word, +5 routine) has +7 the
 * part -- 1 intro (0x140BCB930), 2 dialogue scenes (0x140BCBAD0), 3 the rest
 * (0x140BA5440) -- and +8 the step (intro: 0 load, 1 fade in, 2 shown, 3 fade
 * out). The intro loads narration screen 0 for schools 0..5, then the school's
 * own two: screens 2*school+1, 2*school+2 (school R+0x2238; Darkside, 6, gets
 * 13 and 14 only). Read off 0x140BCB9C4..0x140BCBA05.
 */
#ifndef CFC2_MJUS_STORY_H
#define CFC2_MJUS_STORY_H

#include <windows.h>

#define MJUS_STORY_SCENE_OFF  0x2259
#define MJUS_STORY_LANG_OFF   0x206C
#define MJUS_STORY_REC_OFF    0x2600
#define MJUS_STORY_REC_STRIDE 0x5C
#define MJUS_STORY_STATE      0x24
#define MJUS_STORY_MESSAGE    0x2C
#define MJUS_STORY_SCHOOL_OFF 0x2238
#define MJUS_STORY_TASKS      0x93F30
#define MJUS_STORY_TASK_COUNT 16
#define MJUS_STORY_TASK_STRIDE 0x18
#define MJUS_STORY_ROUTINE    7
#define MJUS_STORY_PART_INTRO 1
#define MJUS_STORY_SPAN       (MJUS_STORY_TASKS + MJUS_STORY_TASK_COUNT * MJUS_STORY_TASK_STRIDE)

#define MJUS_STORY_NONE (-1)

/* What one frame shows: per side, (scene << 8) | message, or MJUS_STORY_NONE;
 * whether the pictures are the Japanese set; and the school whose intro
 * narration is up, or MJUS_STORY_NONE. Pure: offline tests feed captured
 * root bytes (at least MJUS_STORY_SPAN of them). */
typedef struct {
    int shown[2];
    int closing[2];     /* the record says "closing": keep, do not repeat */
    BOOL japanese;
    int intro_school;
} mjus_story_view;

void mjus_story_read(const unsigned char *root, mjus_story_view *v);

/* The transcribed line for a key, or NULL (an untranscribed key, or a key
 * whose picture holds no words). */
const wchar_t *mjus_story_lookup(int key, BOOL japanese);

/* The narration screens a school's intro shows, in order; returns how many
 * (0 for an unknown school). */
int mjus_narration_pages(int school, int pages[3]);
const wchar_t *mjus_narration_text(int page, BOOL japanese);

/* Edge state: the last key spoken per side. */
typedef struct { int last[2]; } mjus_story_edges;

void mjus_story_reset(mjus_story_edges *e);
/* New lines this frame, in side order. Does NOT commit: call
 * mjus_story_commit for each one actually queued, so a full queue retries. */
int  mjus_story_new(const mjus_story_edges *e, const mjus_story_view *v, int keys[2], int sides[2]);
void mjus_story_commit(mjus_story_edges *e, const mjus_story_view *v, int side);
/* After the new lines are handled: forget a side whose record emptied, so the
 * same line shown again later (a repeated scene) is spoken again. */
void mjus_story_settle(mjus_story_edges *e, const mjus_story_view *v);

/* Game thread, from the MJUS frame hook after the update ran. */
void mjus_story_frame(ULONGLONG root);
/* Worker: speak queued lines, in order, advancing only on acceptance. */
void mjus_story_tick(void);
/* Worker: register the ` key source (before "Project Justice"). */
void mjus_story_register(void);

#endif
