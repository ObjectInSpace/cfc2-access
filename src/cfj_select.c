/* See cfj_select.h. The CFJ adapter for the shared selection channel:
 * native reads, gates and decoding here; settling, grouping, retries and
 * speech belong to selection_channel. No hooks, no game-memory writes. */
#include "cfj_select.h"
#include "selection_channel.h"
#include "mod_text.h"
#include "character_name_table.h"
#include "memsafe.h"
#include "match_audio.h"
#include "cfj_frame.h"
#include "cfj_text.h"
#include "audio.h"
#include "log.h"
#include "frame_liveness.h"
#include <string.h>
#include <stdio.h>

/* Select number -> native character ID: the game's own table at 0x1412BB8F0,
 * read by the commit at 0x14080BE8B into the fighter record (+0x1EC18). The
 * tick checks these bytes against the running image before trusting them. */
static const BYTE g_native_id[CFJ_SELECT_COUNT] = {
    0, 2, 3, 4, 10, 11, 16, 17, 18, 19, 20, 21, 22, 31, 24, 25, 26, 27, 28, 29, 44, 45, 34,
};

/* ---- decoding (pure, on the owned copy) --------------------------------- */

/* Phase 2 is the select screen (table 0x1412BBBB0 entry 2 = 0x14080C140) and
 * sub-state 0 is picking (its jump table entry 0 calls 0x14080BB40). Only a
 * human side is read: the commit itself tests R+0x2197C against the fighter's
 * side bit, and the CPU's record holds a default that is never on screen. */
static BOOL selecting(const cfj_select_sample *s, unsigned side)
{
    WORD picked;
    memcpy(&picked, s->sides + 0x10 * side + 0x08, 2);
    /* Two picks made: the cursor driver 0x140808E70 stops at count 2. */
    return s->phase[0] == 2 && s->phase[1] == 0 && (s->humans >> side & 1) && picked < 2;
}

int cfj_select_decode(const cfj_select_sample *s, unsigned side)
{
    int slot, number;
    unsigned id;
    if (!s || side > 1 || !selecting(s, side)) return -1;
    /* Hover writer 0x14080AABE: record+0x0E+slot = select number, where slot
     * is record+0x06 (0 first pick, 1 second). 0xFF is "nothing yet". */
    slot = (signed char)s->sides[0x10 * side + 0x06];
    if (slot < 0 || slot > 1) return -2;
    number = (signed char)s->sides[0x10 * side + 0x0E + (unsigned)slot];
    if (number < 0 || number >= (int)CFJ_SELECT_COUNT) return -2;
    id = g_native_id[number];
    if (!character_name(2, id, s->variant)) return -2;
    return (int)id;
}

/* Stage select: select sub-state 4 (jump table 0x14080C73C entry 4 =
 * 0x14080C3E8) calls 0x14080CD70 per fighter. Left/right step the signed word
 * R+0x26E00 (the first side record's +0x00) modulo 11 (0x14080CEC3,
 * 0x14080CF4C), skipping 7..9 while their unlock bits are clear; confirm
 * takes 0 as a random roll (rng % 10, 0x14080CDE6) and 1..10 through the
 * stage table 0x1412BB4A0. Live 2026-10-15: Versus 0 <-> 1, Training 0 <-> 10
 * (left from 0 wraps).
 *
 * Names. The screen draws them as pictures: CFE's string pool (around
 * 0x141084310) holds only coin, warning and credit text, and neither the
 * shell's text nor the ROM blob names a stage. So each entry is named
 * through its music. Entry v is stage byte[2v] - 3 of 0x1412BB4A0 (session
 * +0x68C); the arcade plays stage s's music as code s + 9 (0x1407D4628 ->
 * 0x1407CBE70), bank index table[s + 9] of 0x1412B1F40 (0x1407D1C90 -> shell
 * 0x14006D470); that file's museum track (music.arc snd_jam_MP, titles
 * SOUND_CFJ_nn) is the stage's song, and the songs were paired with stages
 * from the Japanese game-music wiki (Severe Way Myanmar, Goes a Syoten-Gai!
 * Japan, At Ease!? New York, Prospered Fatherland Ruins, Night of Hong Kong
 * Hong Kong); The Jungle of a Jungle is the Jungle, To Kill Time the one
 * left, Under World. Checks: the arcade forces stage 7 for the final fight
 * (0x1407D3858, "Infinity Chamber") and 9 for Shin Akuma (0x1407D3876,
 * "Desperate Struggle"), 8 is the fallback ("Training Stage"); and the menu
 * order comes out exactly as Japanese Wikipedia lists the stages. 2026-10-15. */
static const wchar_t *const g_stage[11] = {
    L"Random stage", L"Myanmar", L"Japan", L"New York", L"Under World", L"Ruins",
    L"Jungle", L"Hong Kong", L"Infinity Chamber", L"Infinity Chamber Remains",
    L"Training Zone",
};
/* ja.wikipedia.org/wiki/CAPCOM_FIGHTING_Jam, same order. */
static const wchar_t *const g_stage_jpn[11] = {
    L"", L"ミャンマー", L"ジャパン", L"ニューヨーク", L"アンダーワールド", L"ルーインズ",
    L"ジャングル", L"ホンコン", L"インフィニティ・チャンバー", L"インフィニティ・チャンバー・リメインズ",
    L"トレーニングゾーン",
};
static const BYTE g_stage_menu[22] = { 2,0, 9,0, 3,0, 8,0, 4,0, 5,0, 6,0, 7,0, 10,0, 12,0, 11,0 };

int cfj_stage_decode(const cfj_select_sample *s)
{
    short v;
    if (!s || s->phase[0] != 2 || s->phase[1] != 4) return -1;
    memcpy(&v, s->sides, 2);
    return v >= 0 && v <= 10 ? v : -2;
}

void cfj_selection_copy(const cfj_select_sample *s,
                        game_selection_choice out[GAME_SELECTION_OWNERS])
{
    int stage;
    if (!out) return;
    memset(out, 0, sizeof(*out) * GAME_SELECTION_OWNERS);
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i)
        out[i].status = GAME_SELECTION_INACTIVE;
    for (unsigned side = 0; side < 2; ++side) {
        int id = cfj_select_decode(s, side);
        if (id < 0) {
            out[side].status = id == -1 ? GAME_SELECTION_INACTIVE : GAME_SELECTION_UNAVAILABLE;
            continue;
        }
        out[side].status = GAME_SELECTION_VALID;
        out[side].kind = 0;
        out[side].value = (unsigned)id;
        /* The second pick is a new visit: its cursor starts on a character
         * and that first highlight is spoken even if it matches the last. */
        out[side].visit = s->sides[0x10 * side + 0x06];
        swprintf(out[side].label, GAME_SELECTION_LABEL, L"%hs",
                 character_name(2, (unsigned)id, s->variant));
    }
    if ((stage = cfj_stage_decode(s)) != -1) {
        game_selection_choice *o = &out[CFJ_STAGE_OWNER];
        if (stage < 0) { o->status = GAME_SELECTION_UNAVAILABLE; return; }
        o->status = GAME_SELECTION_VALID;
        o->kind = CFJ_KIND_STAGE;
        o->value = (unsigned)stage;
        /* Japanese Wikipedia's own names for the Japanese language; the
         * English ones otherwise (Random follows the language). */
        swprintf(o->label, GAME_SELECTION_LABEL, L"%ls",
                 !stage ? mt(MT_RANDOM_STAGE) :
                 mt_language() == 0 ? g_stage_jpn[stage] : g_stage[stage]);
    }
}

void cfj_select_observe(const cfj_select_sample *s, ULONGLONG root, unsigned rom)
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
    /* Compatibility identity only, as for CVS1 and CVS2: root + ROM. */
    if (!channel.session.owned || root != owner_root || rom != owner_rom) {
        if (!selection_channel_begin(&channel, 2, rom, "cfj-select")) return;
        owner_root = root;
        owner_rom = rom;
    }
    cfj_selection_copy(s, choices);
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < 2; ++p) if (accepted & (1u << p))
        log_line("cfj-select: accepted side=%u id=%u", p + 1, choices[p].value);
    if (accepted & (1u << CFJ_STAGE_OWNER))
        log_line("cfj-select: accepted stage entry %u", choices[CFJ_STAGE_OWNER].value);
}

/* ---- acquisition (worker thread) ---------------------------------------- */

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out, (const void *)(ULONG_PTR)address, size); }

#define SESSION_SLOT 0x141C14F98ULL   /* emulator session singleton          */
#define SHELL_SLOT   0x141C14F90ULL   /* shell game-select object (+0x1AC game, +0x1B0 version) */
#define ROOT_SLOT    0x1422F3A80ULL   /* CFJ published root, setup 0x1407CB660 */
#define ID_TABLE     0x1412BB8F0ULL   /* select number -> native ID          */
#define PHASE_ENTRY  0x1412BBBC0ULL   /* phase table entry 2                 */
#define SELECT_FN    0x14080C140ULL
#define POLL_MS      33u              /* faster than a 60 fps frame; see cvs2_select_tick */

/* Code this adapter's offsets and names depend on, checked ONCE (`.text` is
 * immutable here). Bytes from the reviewed dump, as for CVS1. */
static const struct { ULONGLONG address; BYTE bytes[16]; unsigned n; } g_sig[] = {
    { 0x140809D2DULL, { 0x0f,0xb6,0x88,0x60,0x19,0x02,0x00 }, 7 },         /* phase R+0x21960    */
    { 0x140809D3BULL, { 0xff,0x94,0xc8,0xb0,0xbb,0x2b,0x01 }, 7 },         /* phase table call   */
    { 0x14080C184ULL, { 0x0f,0xb6,0x82,0x61,0x19,0x02,0x00,0x83,0xf8,0x06 }, 10 }, /* sub-state */
    { 0x140808E9FULL, { 0x66,0x83,0xbc,0xf8,0x08,0x6e,0x02,0x00,0x02 }, 9 }, /* pick count 2 */
    { 0x14080AAB2ULL, { 0x48,0x0f,0xbe,0x84,0x0b,0x06,0x6e,0x02,0x00 }, 9 }, /* slot +0x06    */
    { 0x14080AABEULL, { 0x40,0x88,0xb4,0x08,0x0e,0x6e,0x02,0x00 }, 8 },    /* hover +0x0E store  */
    { 0x14080BE61ULL, { 0x0f,0xb6,0x91,0x7c,0x19,0x02,0x00 }, 7 },         /* human mask         */
    { 0x14080BE8BULL, { 0x42,0x0f,0xb6,0x8c,0x3a,0xf0,0xb8,0x2b,0x01 }, 9 }, /* ID table read */
    { 0x14004B4D1ULL, { 0x83,0xff,0x2e }, 3 },                             /* name fn: CFJ limit */
    { 0x14004B4DAULL, { 0x49,0x8b,0x84,0xc4,0x40,0x69,0x1f,0x01 }, 8 },    /* name table         */
    { 0x14007FF39ULL, { 0x8b,0x88,0xb0,0x01,0x00,0x00 }, 6 },              /* version at +0x1B0  */
    { 0x14080C43FULL, { 0xe8,0x2c,0x09,0x00,0x00 }, 5 },                   /* sub 4 -> stage menu */
    { 0x14080CEBEULL, { 0x6b,0xc2,0x0b,0x2b,0xc8,0x66,0x41,0x89,0x89,0x00,0x6e,0x02,0x00 }, 13 }, /* stage mod 11 */
    { 0x14080CDC5ULL, { 0x74,0x1f }, 2 },                                  /* entry 0 = random   */
};

/* The two data facts the decode rests on: the ID table and the select entry. */
static BOOL data_matches(void)
{
    BYTE table[CFJ_SELECT_COUNT], menu[sizeof g_stage_menu];
    ULONGLONG entry;
    /* The stage names rest on the menu's order (0x1412BB4A0). */
    return read_at(table, ID_TABLE, sizeof table) && !memcmp(table, g_native_id, sizeof table) &&
           read_at(&entry, PHASE_ENTRY, 8) && entry == SELECT_FN &&
           read_at(menu, 0x1412BB4A0ULL, sizeof menu) && !memcmp(menu, g_stage_menu, sizeof menu);
}

/* Match-audio ownership for CFJ, worker only, as cvs1_audio_route: a new
 * epoch per presentation owner (root + ROM); the frame hook feeds only an
 * epoch that names game 2. The update did not run while paused in the
 * capture (the block froze), so the worker turns the beacons off after
 * STALL_MS without frames. */
#define STALL_MS 250u
static void cfj_audio_route(ULONGLONG root, unsigned rom)
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
        epoch = match_audio_begin(2);
        owner_root = root;
        owner_rom = rom;
        last_change = now;
        silenced = FALSE;
    }
    frames = cfj_frame_count();
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

void cfj_select_tick(void)
{
    static int guard_ready, sig_ok;
    static DWORD last_poll;
    static cfj_select_sample sample;
    BYTE header[0x60], published[0x0C];
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
                log_line("cfj-select: ⛔ code at 0x%llx differs from the audited build -- "
                         "CFJ selection speech is disabled.", g_sig[i].address);
                return;
            }
        }
        if (!data_matches()) {
            sig_ok = -1;
            log_line("cfj-select: ⛔ the select ID table or phase entry differs from the "
                     "audited build -- CFJ selection speech is disabled.");
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
    if (game != 2 || (rom != 4 && rom != 5)) goto inactive;

    if (!read_at(published, session + 0x1B0, sizeof published)) goto inactive;
    memcpy(&root, published, 8);
    memcpy(&size, published + 8, 4);
    if (!root || root > 0x00007fff00000000ULL || size != CFJ_STATE_SIZE ||
        !read_at(&again, ROOT_SLOT, 8) || again != root)
        goto inactive;

    if (!read_at(&shell, SHELL_SLOT, 8) || !shell ||
        !read_at(&shell_game, shell + 0x1AC, 4) || shell_game != 2 ||
        !read_at(&sample.variant, shell + 0x1B0, 4) || sample.variant > 1)
        goto inactive;

    if (!read_at(sample.phase, root + CFJ_PHASE_OFF, sizeof sample.phase) ||
        !read_at(&sample.humans, root + CFJ_HUMANS_OFF, 1) ||
        !read_at(sample.sides, root + CFJ_SIDES_OFF, sizeof sample.sides))
        goto inactive;
    /* Bracket: the block and session must still be the ones we validated. */
    if (!read_at(&again, ROOT_SLOT, 8) || again != root ||
        !read_at(&again, SESSION_SLOT, 8) || again != session)
        goto inactive;
    {
        static unsigned logged = ~0u;
        unsigned pair = rom * 2 + sample.variant;
        if (pair != logged) {
            logged = pair;
            log_line("cfj-select: admitted ROM %u (%s), shell game version %u",
                     rom, rom == 5 ? "cfe" : "cfj", sample.variant);
        }
    }
    cfj_audio_route(root, rom);
    /* Only while the update runs: a quit game's block stays frozen on its
     * last screen and passes every gate above (frame_liveness.h). A stall
     * departs, so a resumed or restarted select speaks its highlight. */
    {
        static frame_liveness live;
        if (frame_liveness_live(&live, cfj_frame_count(), GetTickCount64(), STALL_MS))
            cfj_select_observe(&sample, root, rom);
        else
            cfj_select_observe(NULL, 0, 0);
    }
    cfj_text_observe(root);
    return;
inactive:
    cfj_audio_route(0, 0);
    cfj_select_observe(NULL, 0, 0);
    cfj_text_observe(0);
}
