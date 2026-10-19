/* See mjus_select.h. The Project Justice adapter for the shared selection
 * channel: native reads, gates and decoding here; settling, grouping, retries
 * and speech belong to selection_channel. No hooks, no game-memory writes. */
#include "mjus_select.h"
#include "selection_channel.h"
#include "mod_text.h"
#include "character_name_table.h"
#include "memsafe.h"
#include "match_audio.h"
#include "mjus_frame.h"
#include "mjus_text.h"
#include "audio.h"
#include "log.h"
#include "frame_liveness.h"
#include <string.h>
#include <stdio.h>

/* ---- decoding (pure, on the owned copy) --------------------------------- */

/* The select screens all run in the task whose routine is 6 (0x140BA1230).
 * Its +7 screen state and +8 step:
 *   state 1, step 1: the Story/Free menu. Left/right toggle R+0x22F0
 *                    (0x140BA145E: xor 1); confirm takes 0 to the school list
 *                    (0x140BC9840) and 1 to free select (0x140BCA240).
 *   state 2, step 1: the side record's +0x2A holds that choice (0x140BA12F2,
 *                    phase-1 handler 0x140B9E51A reads it back). 0: the school
 *                    list (0x140BC98D0; up/down step R+0x22F0 modulo the count
 *                    R+0x22F1, 6 or 7). 1: free character select. */
static BOOL on_screen(const mjus_select_sample *s, BYTE state)
{
    return s->task_state == state && s->task_step == 1;
}

/* The side that drives the shared menus: the school list reads
 * R+0x2029 - 1 (0x140BC9BB5..0x140BC9BC5), i.e. the one human side. */
static int menu_side(const mjus_select_sample *s)
{
    return s->humans == 1 ? 0 : s->humans == 2 ? 1 : -1;
}

int mjus_mode_decode(const mjus_select_sample *s)
{
    if (!s || !on_screen(s, 1) || s->menu[2] != 0) return -1;
    return s->menu[0] & 1;
}

int mjus_school_decode(const mjus_select_sample *s)
{
    int side;
    if (!s || !on_screen(s, 2) || s->menu[2] != 0) return -1;
    if ((side = menu_side(s)) < 0 || s->sides[side][0x2A] != 0) return -1;
    if (s->menu[1] != 6 && s->menu[1] != 7) return -2;
    return s->menu[0] < s->menu[1] ? s->menu[0] : -2;
}

int mjus_select_decode(const mjus_select_sample *s, unsigned side)
{
    const BYTE *r;
    unsigned slot, id;
    /* R+0x2029 bit 0 = side 1, bit 1 = side 2 (0x140C342E8, 0x140C34310). */
    if (!s || side > 1 || !on_screen(s, 2) || !(s->humans >> side & 1)) return -1;
    r = s->sides[side];
    if (r[0x2A] != 1) return -1;                 /* the school list, not free select */
    /* Slot +0x17 counts picks (0x140C342EE bounds it at 3); the highlight
     * for the slot being picked is +0x18 + slot (0x140C342FC). The value is
     * the native character ID: the cursor starts on 1 Batsu, and three live
     * picks committed exactly the highlighted IDs (mjus-selection.md). */
    slot = r[0x17];
    if (slot >= 3) return -1;
    id = r[0x18 + slot];
    if (id >= MJUS_ID_LIMIT || !character_name(6, id, s->variant) ||
        !character_name(6, id, s->variant)[0])
        return -2;
    return (int)id;
}

/* Story-chapter teammate choice (0x140C40E20, run by the story scene, not
 * the routine-6 task). R+0x225E is the choosing side, 1 or 2; R+0x2260 the
 * count of offered characters and R+0x2261.. their native IDs. The cursor
 * R+0x230C steps over them, wrapping at the count and skipping any whose
 * picked flag R+0x230D+i is set (0x140C40FD0..0x140C4100D); 0xFE means the
 * choice has not started (0x140C40EBC). Confirm copies R+0x2261+cursor into
 * the team at R+0x21CC (0x140C41390) and counts it in R+0x230B. R+0x22F2
 * stays 0 until the choice is over. Live: three captures, two confirmed,
 * each team equal to the entries the cursor stood on. */
/* Picks so far, from the picked flags: they change in the same frame as the
 * cursor's jump to the next free entry, while the count R+0x230B follows a
 * frame later (live, 156.76 -> 156.87 s). */
unsigned mjus_team_picked(const mjus_select_sample *s)
{
    unsigned n = 0, count = s->team[2];
    for (unsigned i = 0; i < count && 2 + i < MJUS_PICK_SIZE; i++) n += s->pick[2 + i] != 0;
    return n;
}

int mjus_team_decode(const mjus_select_sample *s, unsigned side)
{
    unsigned count, cursor, id;
    if (!s || side > 1 || s->phase != 1 || s->menu[2] != 0) return -1;
    if (s->team[0] != side + 1 || !(s->humans >> side & 1)) return -1;
    cursor = s->pick[1];
    if (cursor == 0xFE) return -1;
    count = s->team[2];
    if (count == 0 || count > MJUS_TEAM_SIZE - 3 || count > MJUS_PICK_SIZE - 2 || cursor >= count)
        return -2;
    if (s->pick[2 + cursor]) return -1;          /* already picked: the cursor is moving on */
    id = s->team[3 + cursor];
    if (id >= MJUS_ID_LIMIT || !character_name(6, id, s->variant) ||
        !character_name(6, id, s->variant)[0])
        return -2;
    return (int)id;
}

/* School names: the collection's own story list (STATS_MJUS_30..40) names
 * them, in English for every language but Japanese. Order: the story table
 * 0x1417B2870 is indexed school*2 + route; schools 2, 4 and 6 have one route
 * (Pacific, Justice, Darkside), 0 branches at chapter 2 (Taiyo, confirmed
 * live), 1 at chapter 3 (Gorin, whose branch is the chapter 2 team), 3 and 5
 * at chapter 4 (Gedo, Seijyun). School 5 offered Akira, Zaki and Yurika as
 * teammates live: Seijyun, which fixes Gedo at 3. */
static const wchar_t *const g_school[7] = {
    L"Taiyo High School", L"Gorin High School", L"Pacific High School",
    L"Gedo High School", L"Justice High School", L"Seijyun High School",
    L"Darkside Student Congress",
};
static const wchar_t *const g_school_jpn[7] = {
    L"太陽学園", L"五輪高校", L"パシフィックH.S.", L"外道高校",
    L"ジャスティス学園", L"聖純女学院", L"暗黒生徒会",
};

void mjus_selection_copy(const mjus_select_sample *s,
                         game_selection_choice out[GAME_SELECTION_OWNERS])
{
    int v;
    if (!out) return;
    memset(out, 0, sizeof(*out) * GAME_SELECTION_OWNERS);
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i)
        out[i].status = GAME_SELECTION_INACTIVE;
    for (unsigned side = 0; side < 2; ++side) {
        int id = mjus_select_decode(s, side);
        unsigned visit = s->sides[side][0x17];
        if (id == -1) {
            /* The story's teammate choice; its visits are counted apart from
             * free select's so the first highlight of each pick is new. */
            id = mjus_team_decode(s, side);
            visit = 0x10u + mjus_team_picked(s);
        }
        if (id < 0) {
            out[side].status = id == -1 ? GAME_SELECTION_INACTIVE : GAME_SELECTION_UNAVAILABLE;
            continue;
        }
        out[side].status = GAME_SELECTION_VALID;
        out[side].kind = MJUS_KIND_CHARACTER;
        out[side].value = (unsigned)id;
        /* Each pick is a new visit: its first highlight is spoken. */
        out[side].visit = visit;
        swprintf(out[side].label, GAME_SELECTION_LABEL, L"%hs",
                 character_name(6, (unsigned)id, s->variant));
    }
    {
        game_selection_choice *o = &out[MJUS_MENU_OWNER];
        if ((v = mjus_mode_decode(s)) >= 0) {
            o->status = GAME_SELECTION_VALID;
            o->kind = MJUS_KIND_MODE;
            o->value = (unsigned)v;
            o->visit = 1;
            swprintf(o->label, GAME_SELECTION_LABEL, L"%ls", mt(v ? MT_FREE_MODE : MT_STORY_MODE));
        } else if ((v = mjus_school_decode(s)) != -1) {
            if (v < 0) { o->status = GAME_SELECTION_UNAVAILABLE; return; }
            o->status = GAME_SELECTION_VALID;
            o->kind = MJUS_KIND_SCHOOL;
            o->value = (unsigned)v;
            o->visit = 2;
            swprintf(o->label, GAME_SELECTION_LABEL, L"%ls",
                     mt_language() == 0 ? g_school_jpn[v] : g_school[v]);
        }
    }
}

void mjus_select_observe(const mjus_select_sample *s, ULONGLONG root, unsigned rom)
{
    static selection_channel channel;
    static ULONGLONG owner_root;
    static unsigned owner_rom;
    game_selection_choice choices[GAME_SELECTION_OWNERS];
    if (!s || !root) {
        selection_channel_depart(&channel);
        owner_root = 0;
        return;
    }
    if (!channel.session.owned || root != owner_root || rom != owner_rom) {
        if (!selection_channel_begin(&channel, 6, rom, "mjus-select")) return;
        owner_root = root;
        owner_rom = rom;
    }
    mjus_selection_copy(s, choices);
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < 2; ++p) if (accepted & (1u << p))
        log_line("mjus-select: accepted side=%u id=%u", p + 1, choices[p].value);
    if (accepted & (1u << MJUS_MENU_OWNER))
        log_line("mjus-select: accepted %s %u",
                 choices[MJUS_MENU_OWNER].kind == MJUS_KIND_MODE ? "mode" : "school",
                 choices[MJUS_MENU_OWNER].value);
}

/* ---- acquisition (worker thread) ---------------------------------------- */

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out, (const void *)(ULONG_PTR)address, size); }

#define SESSION_SLOT 0x141C14F98ULL   /* emulator session singleton          */
#define SHELL_SLOT   0x141C14F90ULL   /* shell game-select object (+0x1AC game, +0x1B0 version) */
#define ROOT_SLOT    0x142996830ULL   /* MJUS published root, setup 0x140B930D0 */
#define TASK_TABLE   0x141787260ULL   /* task routine table                  */
#define POLL_MS      33u

/* Code this adapter's offsets depend on, checked ONCE (`.text` is immutable). */
static const struct { ULONGLONG address; BYTE bytes[16]; unsigned n; } g_sig[] = {
    { 0x140B93516ULL, { 0xbb,0x08,0x20,0x00,0x00,0x66,0xff,0x80,0x02,0x20,0x00,0x00 }, 12 }, /* update */
    { 0x140B93B84ULL, { 0x48,0x8d,0x98,0x30,0x3f,0x09,0x00 }, 7 },            /* task table R+0x93F30 */
    { 0x140B93BCCULL, { 0x0f,0xb6,0x43,0x05,0xff,0x54,0xc5,0x00 }, 8 },       /* routine byte +5      */
    { 0x140BA1277ULL, { 0x0f,0xb6,0x53,0x07 }, 4 },                           /* screen state +7      */
    { 0x140BA145EULL, { 0x80,0xb0,0xf0,0x22,0x00,0x00,0x01 }, 7 },            /* Story/Free toggle    */
    { 0x140BC9B3FULL, { 0x88,0x88,0xf1,0x22,0x00,0x00 }, 6 },                 /* school count         */
    { 0x140BC9C0EULL, { 0x41,0x88,0x84,0x10,0x2b,0x25,0x00,0x00 }, 8 },       /* school confirm       */
    { 0x140C342EEULL, { 0x0f,0xb6,0x82,0x17,0x25,0x00,0x00,0x3c,0x03 }, 9 },  /* slot, bound 3        */
    { 0x140C342FCULL, { 0x44,0x0f,0xb6,0x8c,0x10,0x18,0x25,0x00,0x00 }, 9 },  /* side 1 highlight     */
    { 0x140C34327ULL, { 0x44,0x0f,0xb6,0xac,0x08,0x98,0x25,0x00,0x00 }, 9 },  /* side 2 highlight     */
    { 0x14004B4D1ULL, { 0x83,0xff,0x2e }, 3 },                                /* name fn (shared)     */
    { 0x140C40FD0ULL, { 0xfe,0x82,0x0c,0x23,0x00,0x00 }, 6 },                 /* team cursor step     */
    { 0x140C41005ULL, { 0x80,0xbc,0x10,0x0d,0x23,0x00,0x00,0x00 }, 8 },       /* picked flag skip     */
    { 0x140C40EBCULL, { 0xc6,0x80,0x0c,0x23,0x00,0x00,0xfe }, 7 },            /* cursor idle 0xFE     */
    { 0x140C41390ULL, { 0x43,0x0f,0xb6,0x84,0x08,0x61,0x22,0x00,0x00 }, 9 },  /* commit entry -> team */
};

static BOOL data_matches(void)
{
    ULONGLONG entry;
    return read_at(&entry, TASK_TABLE + 8 * MJUS_SELECT_ROUTINE, 8) && entry == 0x140BA1230ULL;
}

/* Match-audio ownership for MJUS, worker only, as cfj_audio_route. */
#define STALL_MS 250u
static void mjus_audio_route(ULONGLONG root, unsigned rom)
{
    static uint64_t epoch;
    static ULONGLONG owner_root;
    static unsigned owner_rom;
    static LONG last_frames;
    static ULONGLONG last_change;
    static BOOL silenced;
    ULONGLONG now = GetTickCount64();
    LONG frames;
    if (!root) {
        if (epoch) match_audio_depart(epoch);
        epoch = 0;
        return;
    }
    if (!epoch || root != owner_root || rom != owner_rom ||
        match_audio_current() != epoch) {
        epoch = match_audio_begin(6);
        owner_root = root;
        owner_rom = rom;
        last_change = now;
        silenced = FALSE;
    }
    frames = mjus_frame_count();
    if (frames != last_frames) {
        last_frames = frames;
        last_change = now;
        silenced = FALSE;
    } else if (epoch && !silenced && now - last_change >= STALL_MS) {
        for (unsigned p = 0; p < 2; p++)
            audio_beacon_set_owned(epoch, p, FALSE, 0.0f, 1.0f);
        silenced = TRUE;
    }
}

void mjus_select_tick(void)
{
    static int guard_ready, sig_ok;
    static DWORD last_poll;
    static mjus_select_sample sample;
    BYTE header[0x60], published[0x0C], tasks[MJUS_TASK_COUNT * MJUS_TASK_STRIDE];
    ULONGLONG session, root, shell, again;
    unsigned game, rom, size, shell_game;
    DWORD now;

    if (!guard_ready) {
        if (!mem_guard_install()) return;
        guard_ready = 1;
    }
    if (sig_ok == 0) {
        for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++) {
            BYTE got[16];
            if (!read_at(got, g_sig[i].address, g_sig[i].n)) return;   /* retry later */
            if (memcmp(got, g_sig[i].bytes, g_sig[i].n)) {
                sig_ok = -1;
                log_line("mjus-select: ⛔ code at 0x%llx differs from the audited build -- "
                         "Project Justice selection speech is disabled.", g_sig[i].address);
                return;
            }
        }
        if (!data_matches()) {
            sig_ok = -1;
            log_line("mjus-select: ⛔ the task routine table differs from the audited build -- "
                     "Project Justice selection speech is disabled.");
            return;
        }
        sig_ok = 1;
    }
    if (sig_ok < 0) return;

    now = GetTickCount();
    if (last_poll && now - last_poll < POLL_MS) return;
    last_poll = now;

    if (!read_at(&session, SESSION_SLOT, 8) || !session ||
        !read_at(header, session, sizeof header))
        goto inactive;
    memcpy(&game, header + 8, 4);
    memcpy(&rom, header + 0x44, 4);
    if (game != 6 || (rom != 12 && rom != 13)) goto inactive;

    if (!read_at(published, session + 0x1B0, sizeof published)) goto inactive;
    memcpy(&root, published, 8);
    memcpy(&size, published + 8, 4);
    if (!root || root > 0x00007fff00000000ULL || size != MJUS_STATE_SIZE ||
        !read_at(&again, ROOT_SLOT, 8) || again != root)
        goto inactive;

    if (!read_at(&shell, SHELL_SLOT, 8) || !shell ||
        !read_at(&shell_game, shell + 0x1AC, 4) || shell_game != 6 ||
        !read_at(&sample.variant, shell + 0x1B0, 4) || sample.variant > 1)
        goto inactive;

    if (!read_at(tasks, root + MJUS_TASKS_OFF, sizeof tasks) ||
        !read_at(&sample.phase, root + MJUS_PHASE_OFF, 1) ||
        !read_at(sample.team, root + MJUS_TEAM_OFF, sizeof sample.team) ||
        !read_at(sample.pick, root + MJUS_PICK_OFF, sizeof sample.pick) ||
        !read_at(sample.menu, root + MJUS_MENU_OFF, sizeof sample.menu) ||
        !read_at(&sample.humans, root + MJUS_HUMANS_OFF, 1) ||
        !read_at(sample.sides[0], root + MJUS_SIDE_OFF, sizeof sample.sides[0]) ||
        !read_at(sample.sides[1], root + MJUS_SIDE_OFF + MJUS_SIDE_STRIDE, sizeof sample.sides[1]))
        goto inactive;
    /* The routine-6 task, wherever the scheduler placed it (live: slot 5). */
    sample.task_state = sample.task_step = 0xFF;
    for (unsigned i = 0; i < MJUS_TASK_COUNT; i++) {
        const BYTE *t = tasks + i * MJUS_TASK_STRIDE;
        if ((t[0] | t[1]) && t[5] == MJUS_SELECT_ROUTINE) {
            sample.task_state = t[7];
            sample.task_step = t[8];
        }
    }
    if (!read_at(&again, ROOT_SLOT, 8) || again != root ||
        !read_at(&again, SESSION_SLOT, 8) || again != session)
        goto inactive;
    {
        static unsigned logged = ~0u;
        unsigned pair = rom * 2 + sample.variant;
        if (pair != logged) {
            logged = pair;
            log_line("mjus-select: admitted ROM %u (%s), shell game version %u",
                     rom, rom == 13 ? "pjus" : "mjus", sample.variant);
        }
    }
    mjus_audio_route(root, rom);
    {
        static frame_liveness live;
        if (frame_liveness_live(&live, mjus_frame_count(), GetTickCount64(), STALL_MS))
            mjus_select_observe(&sample, root, rom);
        else
            mjus_select_observe(NULL, 0, 0);
    }
    mjus_text_observe(root);
    return;
inactive:
    mjus_audio_route(0, 0);
    mjus_select_observe(NULL, 0, 0);
    mjus_text_observe(0);
}
