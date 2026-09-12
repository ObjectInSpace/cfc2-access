/*
 * The MOVE LIST reader: does it name the focused move, and does it REFUSE
 * everywhere a wrong name could otherwise be spoken?
 *
 * ⭐⭐ WHAT MAKES THIS TESTABLE OFFLINE. The reader walks game memory:
 * screen -> game index -> per-game table pointer -> 12-byte record -> GMD
 * index -> text. Every one of those is plain memory, so the whole chain runs
 * against a synthetic screen, a synthetic table array and a planted message
 * blob -- driving the SHIPPING lookup (gmd_text_by_index) rather than a copy.
 *
 * ⛔⛔ THE FAILURE THIS SUITE EXISTS FOR: a move_id read out of game memory
 * indexes a table whose record COUNT is not derived. An id past the end
 * resolves to some unrelated GMD string, and the reader would speak a
 * confidently wrong move name -- which for a blind user is indistinguishable
 * from a right one. Most cases below are refusals, not successes.
 */
#include "../src/move_list.h"
#include "../src/gmd_find.h"
#include "../src/memsafe.h"
#include "../src/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;

static void check(const char *name, int cond, const char *fmt, ...)
{
    if (cond) { printf("  PASS  %s\n", name); return; }
    printf("  FAIL  %s", name);
    if (fmt && *fmt) {
        va_list ap; va_start(ap, fmt);
        printf("  "); vprintf(fmt, ap); va_end(ap);
    }
    printf("\n");
    failures++;
}

/* A synthetic world: one screen, one table array, one message blob.
 *
 * ⚠ All three live in ONE VirtualAlloc so the reader's bounds check can be
 * pointed at a contiguous range -- the shipping bounds are the game's image,
 * and ml_test_set_tables() substitutes ours. */
#define WORLD  0x10000
static BYTE *world;
static BYTE *screen;      /* +0x330 game, +0x354 ids, +0x3d8 total */
static BYTE *tables;      /* 8 qword pointers */
static BYTE *recs;        /* 12-byte records */

/* The planted message blob: index 0..N NUL-terminated strings, so
 * gmd_text_by_index() resolves against the SHIPPING walk. */
static BYTE *blob;
#define NMSG 40

static void plant_world(int game, int total, const int *ids, int nids)
{
    memset(world, 0, WORLD);

    screen = world + 0x1000;
    tables = world + 0x3000;
    recs   = world + 0x4000;
    blob   = world + 0x6000;

    *(int *)(screen + ML_OFF_GAME)  = game;
    *(int *)(screen + ML_OFF_TOTAL) = total;
    for (int i = 0; i < nids; i++)
        *(int *)(screen + ML_OFF_IDS + i * 4) = ids[i];

    /* All eight game slots point at the same record array; the cases that care
     * about a BAD table overwrite one slot. */
    for (int g = 0; g < ML_GAME_COUNT; g++)
        *(const void **)(tables + g * 8) = recs;

    /* Records: record[i] names GMD index (i + 10), so a wrong record is
     * visibly a different string rather than an adjacent one. */
    for (int i = 0; i < 64; i++)
        *(int *)(recs + i * ML_RECORD_SIZE + ML_REC_OFF_NAME) = i + 10;

    /* Messages: "msg00".."msg39". */
    SIZE_T pos = 0;
    for (int i = 0; i < NMSG; i++) {
        int n = _snprintf((char *)blob + pos, 16, "msg%02d", i);
        pos += (SIZE_T)n + 1;
    }
    gmd_test_set_blob(blob, NMSG);
    ml_test_set_tables(tables, world, world + WORLD);
}

int main(void)
{
    if (!mem_guard_install()) {
        printf("FATAL: fault guard self-test failed\n");
        return 1;
    }
    world = (BYTE *)VirtualAlloc(NULL, WORLD, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_READWRITE);
    if (!world) { printf("FATAL: alloc failed\n"); return 1; }

    /* --- case 1: the happy path, end to end ---------------------------------
     * ⭐ It drives the REAL chain: game index -> table -> record -> GMD index
     * -> the shipping gmd_text_by_index(). Record 3 names index 13 = "msg13". */
    {
        int ids[] = { 7, 2, 3, 5 };
        plant_world(1, 4, ids, 4);
        char v[64] = { 0 };
        BOOL ok = ml_move_name(screen, 2, v, sizeof v);   /* row 2 -> id 3 */
        char d[120];
        snprintf(d, sizeof d, "returned %d, \"%s\", why=%d",
                 (int)ok, v, (int)ml_move_name_why());
        check("1. the focused row resolves to its move name",
              ok && strcmp(v, "msg13") == 0, d);
    }

    /* --- case 2: the ROW indexes the id array, not the record table ---------
     * ⛔ Row 0 and row 2 hold DIFFERENT ids (7 and 3). An implementation that
     * used the row directly as a record index would return msg10 for row 0 --
     * which is also a valid-looking string, so only distinct ids catch it. */
    {
        int ids[] = { 7, 2, 3, 5 };
        plant_world(1, 4, ids, 4);
        char a[64] = { 0 }, b[64] = { 0 };
        ml_move_name(screen, 0, a, sizeof a);   /* id 7  -> msg17 */
        ml_move_name(screen, 3, b, sizeof b);   /* id 5  -> msg15 */
        char d[140];
        snprintf(d, sizeof d, "row0=\"%s\" (want msg17) row3=\"%s\" (want msg15)",
                 a, b);
        check("2. the row indexes the ID ARRAY, not the record table",
              strcmp(a, "msg17") == 0 && strcmp(b, "msg15") == 0, d);
    }

    /* --- case 3: the GAME index selects the table ---------------------------
     * ⭐ Point game 4's slot at a SHIFTED record array. If the game index were
     * ignored, row 0 would still read the default table and pass. */
    {
        int ids[] = { 1 };
        plant_world(4, 1, ids, 1);
        /* game 4 -> records offset by one record, so id 1 lands on record 2. */
        *(const void **)(tables + 4 * 8) = recs + ML_RECORD_SIZE;
        char v[64] = { 0 };
        BOOL ok = ml_move_name(screen, 0, v, sizeof v);
        char d[120];
        snprintf(d, sizeof d, "returned %d, \"%s\" (want msg12), why=%d",
                 (int)ok, v, (int)ml_move_name_why());
        check("3. the game index selects which table is walked",
              ok && strcmp(v, "msg12") == 0, d);
    }

    /* --- case 4: ⛔⛔ A ROW PAST THE SCREEN'S OWN COUNT IS REFUSED ----------
     * The id array is as long as the real list, so reading row 9 of a 4-item
     * list would return whatever follows it in the screen object -- a
     * plausible small integer, hence a plausible wrong move name. */
    {
        int ids[] = { 7, 2, 3, 5 };
        plant_world(1, 4, ids, 4);
        *(int *)(screen + ML_OFF_IDS + 9 * 4) = 6;   /* a valid-looking id */
        char v[64] = { 1 };
        BOOL ok = ml_move_name(screen, 9, v, sizeof v);
        char d[120];
        snprintf(d, sizeof d, "returned %d, \"%s\", why=%d (want 0 / ML_BAD_ROW=5)",
                 (int)ok, v, (int)ml_move_name_why());
        check("4. a row past the screen's item count is REFUSED",
              !ok && v[0] == 0 && ml_move_name_why() == ML_BAD_ROW, d);
    }

    /* --- case 5: ⛔⛔ A WILD MOVE ID IS REFUSED -----------------------------
     * THE case this suite exists for. The per-table record count is not
     * derived, so a garbage id must be caught by its own range check and by the
     * GMD-index bound -- never followed into unrelated memory. */
    {
        int ids[] = { 999999 };
        plant_world(1, 1, ids, 1);
        char v[64] = { 1 };
        BOOL ok = ml_move_name(screen, 0, v, sizeof v);
        char d[140];
        snprintf(d, sizeof d, "returned %d, \"%s\", why=%d (want 0, ML_BAD_ID=6)",
                 (int)ok, v, (int)ml_move_name_why());
        check("5. a wild move id is REFUSED, not followed",
              !ok && v[0] == 0 && ml_move_name_why() == ML_BAD_ID, d);
    }

    /* --- case 6: a NEGATIVE move id is refused -----------------------------
     * ⚠ Separate from case 5: a signed read of a negative id would index
     * BACKWARDS out of the table, which a `> max` check alone misses. */
    {
        int ids[] = { -3 };
        plant_world(1, 1, ids, 1);
        char v[64] = { 1 };
        BOOL ok = ml_move_name(screen, 0, v, sizeof v);
        char d[120];
        snprintf(d, sizeof d, "returned %d, why=%d", (int)ok,
                 (int)ml_move_name_why());
        check("6. a negative move id is refused (would index backwards)",
              !ok && ml_move_name_why() == ML_BAD_ID, d);
    }

    /* --- case 7: ⛔ AN OUT-OF-RANGE GMD INDEX IS REFUSED --------------------
     * The last line of defence: an id that passes its own check can still name
     * an index outside the live message table. Record 4 is made to name index
     * 9999 while the blob holds 40 messages. */
    {
        int ids[] = { 4 };
        plant_world(1, 1, ids, 1);
        *(int *)(recs + 4 * ML_RECORD_SIZE + ML_REC_OFF_NAME) = 9999;
        char v[64] = { 1 };
        BOOL ok = ml_move_name(screen, 0, v, sizeof v);
        char d[140];
        snprintf(d, sizeof d, "returned %d, \"%s\", why=%d (want 0, ML_BAD_INDEX=7)",
                 (int)ok, v, (int)ml_move_name_why());
        check("7. a GMD index outside the live table is REFUSED",
              !ok && v[0] == 0 && ml_move_name_why() == ML_BAD_INDEX, d);
    }

    /* --- case 8: a game index outside 0..7 is refused ----------------------
     * ⭐ The array has EXACTLY eight valid pointers; entry 8 onward is adjacent
     * data reading 0x0101010101010100. Following it would dereference garbage. */
    {
        int ids[] = { 1 };
        plant_world(9, 1, ids, 1);
        char v[64] = { 1 };
        BOOL ok = ml_move_name(screen, 0, v, sizeof v);
        char d[120];
        snprintf(d, sizeof d, "returned %d, why=%d (want ML_BAD_GAME=3)",
                 (int)ok, (int)ml_move_name_why());
        check("8. a game index outside 0..7 is refused",
              !ok && ml_move_name_why() == ML_BAD_GAME, d);
    }

    /* --- case 9: a table pointer outside the bounds is refused -------------
     * ⚠ The pointer comes OUT of game memory, so it must be checked before it
     * is followed. A guard is a net for the race, not the check itself. */
    {
        int ids[] = { 1 };
        plant_world(1, 1, ids, 1);
        *(const void **)(tables + 1 * 8) = (const void *)0x1ULL;
        char v[64] = { 1 };
        BOOL ok = ml_move_name(screen, 0, v, sizeof v);
        char d[120];
        snprintf(d, sizeof d, "returned %d, why=%d (want ML_BAD_TABLE=4)",
                 (int)ok, (int)ml_move_name_why());
        check("9. a table pointer outside the image is refused",
              !ok && ml_move_name_why() == ML_BAD_TABLE, d);
    }

    /* --- case 10: ⭐⭐ NO GMD YET IS ITS OWN REASON -------------------------
     * "The text channel is not up" and "the record is wrong" are different
     * facts. Reporting the first as the second would send someone hunting a
     * data bug that does not exist -- the same reasoning as kbd_install_why(). */
    {
        int ids[] = { 3 };
        plant_world(1, 1, ids, 1);
        gmd_test_set_blob(NULL, 0);
        char v[64] = { 1 };
        BOOL ok = ml_move_name(screen, 0, v, sizeof v);
        char d[120];
        snprintf(d, sizeof d, "returned %d, why=%d (want ML_NO_GMD=1)",
                 (int)ok, (int)ml_move_name_why());
        check("10. an unresolved message table is its OWN reason",
              !ok && ml_move_name_why() == ML_NO_GMD, d);
    }

    /* --- case 11: ml_screen_ready is a POSITIVE identity check -------------
     * ⛔ Readability proves nothing -- the whole image is readable. A sane game
     * index, a followable table and a plausible count are what make it a move
     * list. An absurd count must be refused. */
    {
        int ids[] = { 1 };
        plant_world(1, 1, ids, 1);
        BOOL good = ml_screen_ready(screen);
        *(int *)(screen + ML_OFF_TOTAL) = 99999;
        BOOL bad = ml_screen_ready(screen);
        char d[140];
        snprintf(d, sizeof d, "sane=%d absurd-count=%d why=%d",
                 (int)good, (int)bad, (int)ml_move_name_why());
        check("11. ml_screen_ready accepts a sane screen and refuses an absurd one",
              good && !bad, d);
    }

    /* --- case 12: ⛔⛔ THE SCREEN IS ON THE HEAP, THE TABLES ARE IN THE IMAGE
     *
     * THE BUG THIS EXISTS FOR, and it shipped. The first version applied ONE
     * bounds check -- the image range -- to every read. The screen object is on
     * the heap (measured at 0x2C3D7DB0 in the 21:47 run, far BELOW the image
     * base), so every read of it was rejected, resolve_table() failed at its
     * first rd32(), and the whole move list spoke bare positions. The mechanism
     * was right; the bounds check refused it.
     *
     * ⚠⚠ AND THE OTHER ELEVEN CASES COULD NOT CATCH IT. They allocate ONE
     * synthetic world and point the bounds at it, so the screen and the tables
     * sat in the SAME range and the two address kinds were indistinguishable.
     * A fixture that collapses a distinction cannot test it.
     *
     * So this case plants them in SEPARATE allocations and tells the module
     * only about the table range -- exactly the live arrangement. */
    {
        BYTE *heap = (BYTE *)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
        if (!heap) { printf("FATAL: heap alloc failed\n"); return 1; }
        int ids[] = { 3 };
        plant_world(1, 1, ids, 1);               /* tables + blob in `world` */

        /* Move the SCREEN to a separate allocation, and scope the module's
         * image range to the tables only -- the screen is now outside it. */
        memset(heap, 0, 0x2000);
        BYTE *hscreen = heap + 0x100;
        *(int *)(hscreen + ML_OFF_GAME)  = 1;
        *(int *)(hscreen + ML_OFF_TOTAL) = 1;
        *(int *)(hscreen + ML_OFF_IDS)   = 3;
        ml_test_set_tables(tables, tables, world + WORLD);

        char v[64] = { 0 };
        BOOL ok = ml_move_name(hscreen, 0, v, sizeof v);
        char d[160];
        snprintf(d, sizeof d, "returned %d, \"%s\", why=%d (screen %p is outside "
                 "the table range %p..%p)", (int)ok, v, (int)ml_move_name_why(),
                 (void *)hscreen, (void *)tables, (void *)(world + WORLD));
        check("12. a screen OUTSIDE the image range still reads (it is heap)",
              ok && strcmp(v, "msg13") == 0, d);
        VirtualFree(heap, 0, MEM_RELEASE);
    }

    VirtualFree(world, 0, MEM_RELEASE);
    mem_guard_remove();

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASSED",
           failures);
    return failures ? 1 : 0;
}
