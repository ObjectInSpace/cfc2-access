/* See stg2_select.h. The Plasma Sword adapter for the shared selection
 * channel: native reads, gates and decoding here; settling, grouping, retries
 * and speech belong to selection_channel. No hooks, no game-memory writes. */
#include "stg2_select.h"
#include "selection_channel.h"
#include "character_name_table.h"
#include "memsafe.h"
#include "match_audio.h"
#include "stg2_frame.h"
#include "audio.h"
#include "log.h"
#include "frame_liveness.h"
#include <string.h>
#include <stdio.h>

/* Select number -> shell character ID. At the pick, 0x140D1EA15 reads the
 * game's two tables with the select number -- the native ID at 0x141983BD7
 * and a row flag at 0x141983BFF -- and publishes ID | 0x10 when the flag is
 * set into the shell's fighter slot (+0x578 + 4*side), the ID space of the
 * move list and of the shell's name table (0x14004B2F0). The two rows of the
 * grid reuse native IDs 1..12; the flag tells them apart. The tick checks
 * both tables against the running image before trusting this. */
static const BYTE g_native[STG2_SELECT_COUNT] = {
    0, 1, 2, 3, 5, 4, 7, 8, 6, 10, 9, 1, 2, 3, 5, 4, 7, 8, 6, 10, 9, 12, 11, 12, 11,
};
static const BYTE g_row[STG2_SELECT_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1,
};

/* ---- decoding (pure, on the owned copy) --------------------------------- */

static unsigned word_at(const stg2_select_sample *s, unsigned off)
{ WORD v; memcpy(&v, s->window + off, 2); return v; }

/* R+0 = 3 is the in-game phase (update 0x140CCCB92 dispatches 1/2/3 on it);
 * mode word R+0x4E5EC = 1 is character select (phase 3 indexes jump table
 * 0x140CDE384 with mode-1; entry 0 tail-calls 0x140D1DA20); its sub-state
 * R+0x4E5F4 = 1 is picking (0x140D1E2E0). Per side, R+0x4E470+side = 0 is
 * still choosing (the side driver 0x140D1EA80 runs the cursor 0x140D1E5A0
 * only then); the human mask R+0x4E4B9 is tested bit by bit at 0x140D1E41A. */
static BOOL selecting(const stg2_select_sample *s, unsigned side)
{
    return s->phase == 3 && word_at(s, STG2_W_MODE) == 1 && word_at(s, STG2_W_SUB) == 1 &&
           (s->window[STG2_W_HUMANS] >> side & 1) && s->window[STG2_W_STATE + side] == 0;
}

int stg2_select_decode(const stg2_select_sample *s, unsigned side)
{
    unsigned number, id;
    if (!s || side > 1 || !selecting(s, side)) return -1;
    /* Holding left or right for 60 frames (0x140D1E5EF..0x140D1E60C) sets
     * +0xAB4 and the cursor then jumps to a random cell every fourth frame
     * (0x140D1E680). Nothing is spoken until it stops. */
    if (s->roulette[side] != 0) return -2;
    /* The cursor 0x140D1E5A0 writes R+0x4E472+side through the grid tables
     * at 0x141983C17.. (0x140D1E792, 0x140D1E815) and by +-10 between rows
     * (0x140D1E84E); its random pick yields 1..22 or 1..24 (0x140D1E693). */
    number = s->window[STG2_W_NUMBER + side];
    if (number == 0 || number >= STG2_SELECT_COUNT) return -2;
    id = g_native[number] | (g_row[number] ? 0x10u : 0u);
    if (!character_name(7, id, 0)) return -2;
    return (int)id;
}

void stg2_selection_copy(const stg2_select_sample *s,
                         game_selection_choice out[GAME_SELECTION_OWNERS])
{
    if (!out) return;
    memset(out, 0, sizeof(*out) * GAME_SELECTION_OWNERS);
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i)
        out[i].status = GAME_SELECTION_INACTIVE;
    for (unsigned side = 0; side < 2; ++side) {
        int id = stg2_select_decode(s, side);
        if (id < 0) {
            out[side].status = id == -1 ? GAME_SELECTION_INACTIVE : GAME_SELECTION_UNAVAILABLE;
            continue;
        }
        out[side].status = GAME_SELECTION_VALID;
        out[side].kind = 0;
        out[side].value = (unsigned)id;
        out[side].visit = 0;   /* one pick per side */
        swprintf(out[side].label, GAME_SELECTION_LABEL, L"%hs",
                 character_name(7, (unsigned)id, 0));
    }
}

void stg2_select_observe(const stg2_select_sample *s, ULONGLONG root, unsigned rom)
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
    /* Compatibility identity only, as for the other adapters: root + ROM. */
    if (!channel.session.owned || root != owner_root || rom != owner_rom) {
        if (!selection_channel_begin(&channel, 7, rom, "stg2-select")) return;
        owner_root = root;
        owner_rom = rom;
    }
    stg2_selection_copy(s, choices);
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < 2; ++p) if (accepted & (1u << p))
        log_line("stg2-select: accepted side=%u id=%u", p + 1, choices[p].value);
}

/* ---- acquisition (worker thread) ---------------------------------------- */

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out, (const void *)(ULONG_PTR)address, size); }

#define SESSION_SLOT 0x141C14F98ULL   /* emulator session singleton          */
#define SHELL_SLOT   0x141C14F90ULL   /* shell game-select object (+0x1AC game) */
#define ROOT_SLOT    0x142BE6370ULL   /* STG2 published root, setup 0x140CCC940 */
#define NATIVE_TABLE 0x141983BD7ULL
#define ROW_TABLE    0x141983BFFULL
#define POLL_MS      33u              /* faster than a 60 fps frame; see cvs2_select_tick */

/* Code this adapter's offsets and names depend on, checked ONCE (`.text` is
 * immutable here). Bytes from the reviewed dump. */
static const struct { ULONGLONG address; BYTE bytes[16]; unsigned n; } g_sig[] = {
    { 0x140CCCB8BULL, { 0x48,0x8b,0x05,0xde,0x97,0xf1,0x01,0x0f,0xb6,0x08 }, 10 }, /* phase R+0   */
    { 0x140CDE154ULL, { 0x48,0x8b,0x0d,0x15,0x82,0xf0,0x01,0x48,0x8d,0x81,0xa0,0xe5,0x04,0x00 }, 14 }, /* W = R+0x4E5A0 */
    { 0x140CDE243ULL, { 0x0f,0xb7,0x48,0x4c,0xff,0xc9,0x83,0xf9,0x0e }, 9 },   /* mode W+0x4C  */
    { 0x140CDE268ULL, { 0x48,0x83,0xc4,0x28,0xe9,0xaf,0xf7,0x03,0x00 }, 9 },   /* mode 1 -> select */
    { 0x140D1DA24ULL, { 0x48,0x8b,0x05,0x55,0x89,0xec,0x01,0x0f,0xb7,0x48,0x54 }, 11 }, /* sub W+0x54 */
    { 0x140D1E41AULL, { 0xf6,0x81,0xb9,0xe4,0x04,0x00,0x01 }, 7 },             /* human mask   */
    { 0x140D1EA8AULL, { 0x43,0x0f,0xbe,0x94,0x08,0x70,0xe4,0x04,0x00 }, 9 },   /* side state   */
    { 0x140D1E5E7ULL, { 0x39,0x98,0xb4,0x0a,0x00,0x00 }, 6 },                  /* shuffle flag */
    { 0x140D1E66FULL, { 0x44,0x0f,0xb6,0xbc,0x15,0x72,0xe4,0x04,0x00 }, 9 },   /* select number */
    { 0x140D1EA15ULL, { 0x48,0x0f,0xbe,0x8c,0x07,0x72,0xe4,0x04,0x00,0x42,0x0f,0xbe,0x84,0x01,0xd7,0x3b }, 16 }, /* ID table */
    { 0x140D1EA25ULL, { 0x98,0x01,0x8b,0xd0,0x83,0xca,0x10,0x42,0x80,0xbc,0x01,0xff,0x3b,0x98,0x01 }, 15 }, /* | 0x10 by row */
    { 0x14004B53AULL, { 0x83,0xff,0x1d,0x73,0x0a,0x49,0x8b,0x84,0xfc,0x90,0x72,0x1f,0x01 }, 13 }, /* name fn: STG2 */
};

static BOOL data_matches(void)
{
    BYTE native[STG2_SELECT_COUNT], row[STG2_SELECT_COUNT];
    unsigned i;
    if (!read_at(native, NATIVE_TABLE, sizeof native) || memcmp(native, g_native, sizeof native) ||
        !read_at(row, ROW_TABLE, sizeof row))
        return FALSE;
    /* The code tests the row byte for nonzero (0x140D1EA2C). */
    for (i = 0; i < STG2_SELECT_COUNT; i++) if ((row[i] != 0) != g_row[i]) return FALSE;
    return TRUE;
}

/* Match-audio ownership for STG2, worker only, as cfj_audio_route: a new
 * epoch per presentation owner (root + ROM); the frame hook feeds only an
 * epoch that names game 7. The whole block froze while paused in the capture
 * (no byte changed for 3 s), so the worker turns the beacons off after
 * STALL_MS without frames. */
#define STALL_MS 250u
static void stg2_audio_route(ULONGLONG root, unsigned rom)
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
        epoch = match_audio_begin(7);
        owner_root = root;
        owner_rom = rom;
        last_change = now;
        silenced = FALSE;
    }
    frames = stg2_frame_count();
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

void stg2_select_tick(void)
{
    static int guard_ready, sig_ok;
    static DWORD last_poll;
    static stg2_select_sample sample;
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
                log_line("stg2-select: ⛔ code at 0x%llx differs from the audited build -- "
                         "Plasma Sword selection speech is disabled.", g_sig[i].address);
                return;
            }
        }
        if (!data_matches()) {
            sig_ok = -1;
            log_line("stg2-select: ⛔ the select ID tables differ from the audited build -- "
                     "Plasma Sword selection speech is disabled.");
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
    if (game != 7 || (rom != 14 && rom != 15)) goto inactive;

    if (!read_at(published, session + 0x1B0, sizeof published)) goto inactive;
    memcpy(&root, published, 8);
    memcpy(&size, published + 8, 4);
    if (!root || root > 0x00007fff00000000ULL || size != STG2_STATE_SIZE ||
        !read_at(&again, ROOT_SLOT, 8) || again != root)
        goto inactive;

    if (!read_at(&shell, SHELL_SLOT, 8) || !shell ||
        !read_at(&shell_game, shell + 0x1AC, 4) || shell_game != 7)
        goto inactive;

    if (!read_at(&sample.phase, root, 1) ||
        !read_at(sample.window, root + STG2_SELECT_OFF, sizeof sample.window) ||
        !read_at(&sample.roulette[0], root + STG2_ROULETTE_OFF, 4) ||
        !read_at(&sample.roulette[1], root + STG2_PLAYER_STEP + STG2_ROULETTE_OFF, 4))
        goto inactive;
    /* Bracket: the block and session must still be the ones we validated. */
    if (!read_at(&again, ROOT_SLOT, 8) || again != root ||
        !read_at(&again, SESSION_SLOT, 8) || again != session)
        goto inactive;
    {
        static unsigned logged = ~0u;
        if (rom != logged) {
            logged = rom;
            log_line("stg2-select: admitted ROM %u (%s)", rom, rom == 15 ? "pswd" : "stg2");
        }
    }
    stg2_audio_route(root, rom);
    /* Only while the update runs: a quit game's block stays frozen on its
     * last screen and passes every gate above (frame_liveness.h). */
    {
        static frame_liveness live;
        if (frame_liveness_live(&live, stg2_frame_count(), GetTickCount64(), STALL_MS))
            stg2_select_observe(&sample, root, rom);
        else
            stg2_select_observe(NULL, 0, 0);
    }
    return;
inactive:
    stg2_audio_route(0, 0);
    stg2_select_observe(NULL, 0, 0);
}
