/*
 * Name the focused move on the in-game MOVE LIST. See move_list.h for the
 * mechanism and for the three routes this replaces, each closed with evidence.
 */
#include "move_list.h"
#include "gmd_find.h"
#include "memsafe.h"

#include <string.h>

static ml_why      g_why;
static const void *g_tables_override;
static const void *g_lo_override;
static const void *g_hi_override;

ml_why ml_move_name_why(void) { return g_why; }

void ml_test_set_tables(const void *tables, const void *lo, const void *hi)
{
    g_tables_override = tables;
    g_lo_override = lo;
    g_hi_override = hi;
}

static const void *tables_addr(void)
{
    return g_tables_override ? g_tables_override
                             : (const void *)(ULONG_PTR)ML_GAME_TABLES;
}

/* ⭐ A pointer read out of game memory must land somewhere plausible before it
 * is followed. Without bounds, a garbage qword becomes an address we then
 * dereference -- and a guard is a NET for the race between the check and the
 * read, never a substitute for the check (case C of run_span_test measured a
 * guarded memcpy across a bad page going UNCAUGHT). */
static BOOL plausible(const void *p, SIZE_T need)
{
    const BYTE *lo = (const BYTE *)(g_lo_override ? g_lo_override
                                                  : (const void *)0x140000000ULL);
    const BYTE *hi = (const BYTE *)(g_hi_override ? g_hi_override
                                                  : (const void *)0x143000000ULL);
    const BYTE *b  = (const BYTE *)p;
    if (!b || b < lo || b + need > hi)
        return FALSE;
    return mem_readable_run(p, need) >= need;
}

/* Read a 4-byte int from game memory, guarded. */
static BOOL rd32(const void *p, int *out)
{
    BOOL faulted = FALSE;
    int v = 0;
    if (!plausible(p, sizeof(v)))
        return FALSE;
    MEM_GUARDED(faulted, memcpy(&v, p, sizeof(v)));
    if (faulted)
        return FALSE;
    *out = v;
    return TRUE;
}

/* Read a pointer from game memory, guarded. */
static BOOL rdptr(const void *p, const void **out)
{
    BOOL faulted = FALSE;
    const void *v = NULL;
    if (!plausible(p, sizeof(v)))
        return FALSE;
    MEM_GUARDED(faulted, memcpy(&v, p, sizeof(v)));
    if (faulted)
        return FALSE;
    *out = v;
    return TRUE;
}

/* Resolve the game index and its move table, with every step checked. */
static BOOL resolve_table(const void *screen, const void **table)
{
    int game = 0;
    const BYTE *s = (const BYTE *)screen;

    if (!rd32(s + ML_OFF_GAME, &game)) {
        g_why = ML_BAD_SCREEN;
        return FALSE;
    }
    /* ⭐ The array has EXACTLY eight valid entries and then ends -- entry 8
     * onward reads 0x0101010101010100, which is adjacent data, not a pointer.
     * So the bound is a measured fact about the array, not a guess at the
     * roster size. */
    if (game < 0 || game >= ML_GAME_COUNT) {
        g_why = ML_BAD_GAME;
        return FALSE;
    }
    const BYTE *slot = (const BYTE *)tables_addr() + (SIZE_T)game * sizeof(void *);
    const void *t = NULL;
    if (!rdptr(slot, &t) || !plausible(t, ML_RECORD_SIZE)) {
        g_why = ML_BAD_TABLE;
        return FALSE;
    }
    *table = t;
    return TRUE;
}

BOOL ml_screen_ready(const void *screen)
{
    const void *table = NULL;
    int total = 0;

    g_why = ML_OK;
    if (!screen) {
        g_why = ML_BAD_SCREEN;
        return FALSE;
    }
    if (!resolve_table(screen, &table))
        return FALSE;
    /* ⚠ The screen's own item count, used to bound the row. A move list of
     * zero or absurd length means this is not the screen we think it is. */
    if (!rd32((const BYTE *)screen + ML_OFF_TOTAL, &total)) {
        g_why = ML_BAD_SCREEN;
        return FALSE;
    }
    if (total < 1 || total > 256) {
        g_why = ML_BAD_ROW;
        return FALSE;
    }
    return TRUE;
}

BOOL ml_move_name(const void *screen, int row, char *out, int outlen)
{
    const void *table = NULL;
    int total = 0, move_id = 0, gmd_index = 0;

    g_why = ML_OK;
    if (out && outlen > 0)
        out[0] = '\0';
    if (!out || outlen <= 0 || !screen || row < 0) {
        g_why = ML_BAD_SCREEN;
        return FALSE;
    }
    /* ⛔ Refuse before touching anything if the text channel cannot answer.
     * Otherwise a perfectly-resolved index would report ML_NO_TEXT and read
     * as "the record is wrong" when the real state is "the GMD is not up yet".
     * Those are different facts and the caller distinguishes them. */
    if (!gmd_text_ready()) {
        g_why = ML_NO_GMD;
        return FALSE;
    }
    if (!resolve_table(screen, &table))
        return FALSE;

    /* ⚠ The ROW is bounded by the screen's OWN count, not by the ten rows the
     * game happens to draw. The draw cap is a display limit; the id array is
     * as long as the real list. */
    if (!rd32((const BYTE *)screen + ML_OFF_TOTAL, &total)) {
        g_why = ML_BAD_SCREEN;
        return FALSE;
    }
    if (total < 1 || total > 256 || row >= total) {
        g_why = ML_BAD_ROW;
        return FALSE;
    }
    if (!rd32((const BYTE *)screen + ML_OFF_IDS + (SIZE_T)row * 4, &move_id)) {
        g_why = ML_BAD_ROW;
        return FALSE;
    }
    /* ⛔⛔ THE BOUND THAT MATTERS MOST. The per-table record COUNT is not
     * derived (a fixed walk overruns into the neighbouring table), so this
     * cannot check the id against its table's real length. A negative or wild
     * id would index far outside the table and resolve to some unrelated GMD
     * string -- a confidently wrong move name, which is worse than silence.
     *
     * ⚠ 4096 is a DEFENSIVE ceiling, not a measured one: the largest shipped
     * per-game table is MJUS at ~398 records, so anything near this is already
     * nonsense. It is chosen to be obviously loose rather than to look precise;
     * the real protection is that the resulting GMD index is checked against
     * the live table's count below. */
    if (move_id < 0 || move_id > 4096) {
        g_why = ML_BAD_ID;
        return FALSE;
    }
    const BYTE *rec = (const BYTE *)table + (SIZE_T)move_id * ML_RECORD_SIZE;
    if (!rd32(rec + ML_REC_OFF_NAME, &gmd_index)) {
        g_why = ML_BAD_ID;
        return FALSE;
    }
    /* ⭐ THE REAL GUARD: the index must be inside the LIVE message table. This
     * is what makes a bad move_id harmless rather than plausible -- an index
     * from the wrong place is overwhelmingly likely to fall outside 0..4164,
     * and if it does not, the corpus check in the caller still applies. */
    int count = gmd_text_count();
    if (count <= 0 || gmd_index < 0 || gmd_index >= count) {
        g_why = ML_BAD_INDEX;
        return FALSE;
    }
    if (!gmd_text_by_index(gmd_index, out, (unsigned)outlen) || !out[0]) {
        g_why = ML_NO_TEXT;
        return FALSE;
    }
    return TRUE;
}
