/* See mjus_story.h. */
#include "mjus_story.h"
#include "mjus_story_text.h"
#include "text_channel.h"
#include "speech.h"
#include "log.h"
#include <string.h>

static int rd32(const unsigned char *p)
{
    int v;
    memcpy(&v, p, sizeof v);
    return v;
}

void mjus_story_read(const unsigned char *root, mjus_story_view *v)
{
    int scene = root[MJUS_STORY_SCENE_OFF];
    v->japanese = root[MJUS_STORY_LANG_OFF] == 0;
    for (int side = 0; side < 2; side++) {
        const unsigned char *rec = root + MJUS_STORY_REC_OFF + side * MJUS_STORY_REC_STRIDE;
        int state = rd32(rec + MJUS_STORY_STATE);
        int msg = rd32(rec + MJUS_STORY_MESSAGE);
        BOOL sane = msg >= 0 && msg < 256;
        v->shown[side] = state == 1 && sane ? scene << 8 | msg : MJUS_STORY_NONE;
        v->closing[side] = state == 2;
    }
    v->intro_school = MJUS_STORY_NONE;
    for (int i = 0; i < MJUS_STORY_TASK_COUNT; i++) {
        const unsigned char *t = root + MJUS_STORY_TASKS + i * MJUS_STORY_TASK_STRIDE;
        if (!(t[0] | t[1]) || t[5] != MJUS_STORY_ROUTINE) continue;
        /* Step 1 on: the pictures are loaded and fading in. */
        if (t[7] == MJUS_STORY_PART_INTRO && t[8] >= 1)
            v->intro_school = root[MJUS_STORY_SCHOOL_OFF];
        break;
    }
}

int mjus_narration_pages(int school, int pages[3])
{
    int n = 0;
    if (school < 0 || school > 6) return 0;
    if (school < 6) pages[n++] = 0;           /* the shared opening */
    pages[n++] = 2 * school + 1;
    pages[n++] = 2 * school + 2;
    return n;
}

const wchar_t *mjus_narration_text(int page, BOOL japanese)
{
    if (page < 0 || page >= MJUS_NARRATION_PAGES) return NULL;
    return japanese ? mjus_narration_ja[page] : mjus_narration_en[page];
}

const wchar_t *mjus_story_lookup(int key, BOOL japanese)
{
    const mjus_story_line *t = japanese ? mjus_story_ja : mjus_story_en;
    int lo = 0, hi = (japanese ? MJUS_STORY_JA_COUNT : MJUS_STORY_EN_COUNT) - 1;
    if (key < 0) return NULL;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (t[mid].key == key) return t[mid].text;
        if (t[mid].key < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

void mjus_story_reset(mjus_story_edges *e)
{
    e->last[0] = e->last[1] = MJUS_STORY_NONE;
}

int mjus_story_new(const mjus_story_edges *e, const mjus_story_view *v, int keys[2], int sides[2])
{
    int n = 0;
    for (int side = 0; side < 2; side++) {
        if (v->shown[side] == MJUS_STORY_NONE || v->shown[side] == e->last[side]) continue;
        keys[n] = v->shown[side];
        sides[n++] = side;
    }
    return n;
}

void mjus_story_commit(mjus_story_edges *e, const mjus_story_view *v, int side)
{
    e->last[side] = v->shown[side];
}

void mjus_story_settle(mjus_story_edges *e, const mjus_story_view *v)
{
    for (int side = 0; side < 2; side++)
        if (v->shown[side] == MJUS_STORY_NONE && !v->closing[side])
            e->last[side] = MJUS_STORY_NONE;
}

/* ---- live ------------------------------------------------------------------ */

static tc_queue g_queue;
/* Narration has its own queue, drained first, so a skipped intro can be
 * dropped without touching dialogue queued after it. */
static tc_queue g_nar;
static int g_nar_next = -1;                 /* pages of this intro queued; -1 = none */
static volatile LONG g_cut;                 /* intros ended (worker drops narration) */
static mjus_story_edges g_edges = { { MJUS_STORY_NONE, MJUS_STORY_NONE } };
static ULONGLONG g_root;
/* For the ` key: what the last frame showed, and when. */
static volatile LONG g_shown[2] = { MJUS_STORY_NONE, MJUS_STORY_NONE };
static volatile LONG g_japanese;
static volatile LONG g_intro = MJUS_STORY_NONE;
static volatile LONG64 g_seen_at;
static volatile LONG g_missing, g_queued, g_full;

void mjus_story_frame(ULONGLONG root)
{
    mjus_story_view v;
    int keys[2], sides[2], n;
    if (!root || root >= 0x00007FFF00000000ull) return;
    if (root != g_root) {                 /* a new session: nothing carries over */
        g_root = root;
        mjus_story_reset(&g_edges);
        g_nar_next = -1;
    }
    mjus_story_read((const unsigned char *)(ULONG_PTR)root, &v);
    if (v.intro_school != MJUS_STORY_NONE) {
        int pages[3], count = mjus_narration_pages(v.intro_school, pages);
        if (g_nar_next < 0) g_nar_next = 0;
        while (g_nar_next < count &&
               tc_queue_push(&g_nar, mjus_narration_text(pages[g_nar_next], v.japanese)))
            g_nar_next++;                     /* a refused push retries next frame */
    } else if (g_nar_next >= 0) {
        g_nar_next = -1;                      /* the intro ended: skipped or finished */
        InterlockedIncrement(&g_cut);
    }
    n = mjus_story_new(&g_edges, &v, keys, sides);
    for (int i = 0; i < n; i++) {
        const wchar_t *text = mjus_story_lookup(keys[i], v.japanese);
        if (!text) {
            InterlockedIncrement(&g_missing);       /* counted; logged by the worker */
        } else if (tc_has_letters(text)) {
            if (!tc_queue_push(&g_queue, text)) {   /* busy or full: retry next frame */
                InterlockedIncrement(&g_full);
                continue;
            }
            InterlockedIncrement(&g_queued);
        }
        /* A wordless picture ("......") is a pause, not a line: nothing to say. */
        mjus_story_commit(&g_edges, &v, sides[i]);
    }
    mjus_story_settle(&g_edges, &v);
    InterlockedExchange(&g_shown[0], v.shown[0]);
    InterlockedExchange(&g_shown[1], v.shown[1]);
    InterlockedExchange(&g_japanese, v.japanese);
    InterlockedExchange(&g_intro, v.intro_school);
    InterlockedExchange64(&g_seen_at, (LONG64)GetTickCount64());
}

void mjus_story_tick(void)
{
    static LONG logged_missing, logged_full;
    LONG missing = InterlockedCompareExchange(&g_missing, 0, 0);
    LONG full = InterlockedCompareExchange(&g_full, 0, 0);
    if (missing != logged_missing) {
        log_line("mjus-story: ⚠ %ld shown message(s) have no transcription", missing);
        logged_missing = missing;
    }
    if (full != logged_full) {
        log_line("mjus-story: queue busy %ld time(s); lines retried", full);
        logged_full = full;
    }
    /* An intro that ended takes its narration with it: unspoken screens are
     * dropped and the one being read is stopped, so a skip is not talked over.
     * Only if narration was spoken lately -- otherwise there is nothing of
     * ours to stop, and stopping would cut someone else's speech. */
    {
        static LONG seen_cut;
        static ULONGLONG nar_spoke_at;
        LONG cut = InterlockedCompareExchange(&g_cut, 0, 0);
        if (cut != seen_cut) {
            seen_cut = cut;
            tc_queue_clear(&g_nar);
            if (nar_spoke_at && GetTickCount64() - nar_spoke_at < 90000) {
                speech_stop();
                log_line("mjus-story: intro ended; narration stopped");
            }
            nar_spoke_at = 0;
        }
        if (tc_queue_drain(&g_nar, "mjus-narration")) {
            nar_spoke_at = GetTickCount64();
            return;                           /* dialogue waits behind narration */
        }
    }
    tc_queue_drain(&g_queue, "mjus-story");
}

/* ` key: the message(s) on screen now. Inactive once frames stop arriving, so
 * a stale line is never read back after leaving the game. */
static tc_read_result read_current(void)
{
    wchar_t out[TC_SAY_CAP];
    LONG64 at = InterlockedCompareExchange64(&g_seen_at, 0, 0);
    BOOL japanese = InterlockedCompareExchange(&g_japanese, 0, 0) != 0;
    size_t len = 0;
    if (!at || GetTickCount64() - (ULONGLONG)at > 500) return TC_READ_INACTIVE;
    out[0] = 0;
    {
        int pages[3], count = mjus_narration_pages(InterlockedCompareExchange(&g_intro, 0, 0), pages);
        for (int i = 0; i < count; i++) {
            const wchar_t *t = mjus_narration_text(pages[i], japanese);
            size_t n = t ? wcslen(t) : 0;
            if (!n || len + n + 2 >= TC_SAY_CAP) continue;
            if (len) out[len++] = L' ';
            memcpy(out + len, t, (n + 1) * sizeof *t);
            len += n;
        }
    }
    for (int side = 0; side < 2; side++) {
        const wchar_t *t = mjus_story_lookup(InterlockedCompareExchange(&g_shown[side], 0, 0), japanese);
        size_t n;
        if (!t || !tc_has_letters(t)) continue;
        n = wcslen(t);
        if (len + n + 2 >= TC_SAY_CAP) break;
        if (len) out[len++] = L' ';
        memcpy(out + len, t, (n + 1) * sizeof *t);
        len += n;
    }
    if (!len) return TC_READ_INACTIVE;     /* no dialogue: let the next source answer */
    return tc_say(out, TRUE, "mjus-story read") ? TC_READ_SPOKE : TC_READ_REFUSED;
}

void mjus_story_register(void) { tc_register("Project Justice story", read_current); }
