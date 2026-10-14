/* See cvs1_select.h. The CVS1 adapter for the shared selection channel:
 * native reads, gates and decoding here; settling, grouping, retries and
 * speech belong to selection_channel. No hooks, no game-memory writes. */
#include "cvs1_select.h"
#include "selection_channel.h"
#include "character_name_table.h"
#include "memsafe.h"
#include "match_audio.h"
#include "cvs1_frame.h"
#include "cvs1_text.h"
#include "audio.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

/* ---- decoding (pure, on the owned copy) --------------------------------- */

/* Branch-A character select, the only path with live evidence: phase prefix
 * 02 01 01 06 06 and mode P+0x48 == 0 (docs/cvs1-branch-a-selection.md), the
 * side's actor in state 4 -- the selection handler (actor-state table
 * 0x141237DC0 entry 4) -- and owning its own side. All four held for every
 * highlight in the 2026-10-13 live capture. Other modes are not claimed. */
static BOOL selecting(const cvs1_select_sample *s, unsigned side)
{
    static const BYTE prefix[5] = { 2, 1, 1, 6, 6 };
    return !memcmp(s->phase, prefix, sizeof prefix) && s->phase[0x48] == 0 &&
           s->actor[side][4] == 4 && s->owner[side] == side;
}

int cvs1_select_decode(const cvs1_select_sample *s, unsigned side)
{
    int slot;
    unsigned id;
    if (!s || side > 1 || !selecting(s, side)) return -1;
    /* Slot P+0xA0+side, highlighted ID P+0x78+4*side+slot: the hover writer
     * 0x14060B431 stores the object's native ID there as the cursor moves. */
    slot = (signed char)s->phase[0xA0 + side];
    if (slot < 0 || slot > 3) return -2;
    id = s->phase[0x78 + 4 * side + (unsigned)slot];
    /* 0xFF is "nothing highlighted"; IDs at or past CVS1's name-table limit
     * (0x23 is a traced special) have no name and are never guessed. */
    if (id == 0xFF || !character_name(0, id, s->variant)) return -2;
    return (int)id;
}

/* Groove choice, actor state 3 (table 0x141237DC0 entry 3 = 0x1405D9D40),
 * same phase and mode as the character grid, which follows it (state 4).
 * The handler toggles P+0x76+owner between 0 and 1, and on confirm copies it
 * to actor+0x62C, which 0x1405BF182 copies to the gauge's groove flag +0x2C1:
 * 0 caps the gauge at 144 (three levels), 1 at 100. The shell's groove-name
 * function 0x14004AE30 names game 0's grooves 0 CAPCOM, 1 SNK. */
static const wchar_t *const g_groove[2] = { L"Capcom Groove", L"SNK Groove" };

int cvs1_groove_decode(const cvs1_select_sample *s, unsigned side)
{
    static const BYTE prefix[5] = { 2, 1, 1, 6, 6 };
    unsigned groove;
    if (!s || side > 1 || memcmp(s->phase, prefix, sizeof prefix) || s->phase[0x48] != 0 ||
        s->actor[side][4] != 3 || s->owner[side] != side)
        return -1;
    groove = s->phase[0x76 + side];
    return groove < 2 ? (int)groove : -2;
}

void cvs1_selection_copy(const cvs1_select_sample *s,
                         game_selection_choice out[GAME_SELECTION_OWNERS])
{
    if (!out) return;
    memset(out, 0, sizeof(*out) * GAME_SELECTION_OWNERS);
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i)
        out[i].status = GAME_SELECTION_INACTIVE;
    for (unsigned side = 0; side < 2; ++side) {
        int id = cvs1_select_decode(s, side);
        if (id == -1) {
            int groove = cvs1_groove_decode(s, side);
            if (groove >= 0) {
                out[side].status = GAME_SELECTION_VALID;
                out[side].kind = 1;
                out[side].value = (unsigned)groove;
                swprintf(out[side].label, GAME_SELECTION_LABEL, L"%ls", g_groove[groove]);
                continue;
            }
            id = groove;
        }
        if (id < 0) {
            out[side].status = id == -1 ? GAME_SELECTION_INACTIVE : GAME_SELECTION_UNAVAILABLE;
            continue;
        }
        out[side].status = GAME_SELECTION_VALID;
        out[side].kind = 0;
        out[side].value = (unsigned)id;
        /* A confirmed pick advances the slot, so the same character in the
         * next team slot is a new visit and is spoken again. */
        out[side].visit = s->phase[0xA0 + side];
        swprintf(out[side].label, GAME_SELECTION_LABEL, L"%hs",
                 character_name(0, (unsigned)id, s->variant));
    }
}

void cvs1_select_observe(const cvs1_select_sample *s, ULONGLONG root, unsigned rom)
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
    /* Compatibility identity only, as for CVS2: root + ROM. It does not
     * detect same-address replacement (CVS1 has an in-place restore path). */
    if (!channel.session.owned || root != owner_root || rom != owner_rom) {
        if (!selection_channel_begin(&channel, 0, rom, "cvs1-select")) return;
        owner_root = root;
        owner_rom = rom;
    }
    cvs1_selection_copy(s, choices);
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < 2; ++p) if (accepted & (1u << p))
        log_line("cvs1-select: accepted side=%u id=%u", p + 1, choices[p].value);
}

/* ---- acquisition (worker thread) ---------------------------------------- */

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out, (const void *)(ULONG_PTR)address, size); }

#define SESSION_SLOT 0x141C14F98ULL   /* emulator session singleton          */
#define SHELL_SLOT   0x141C14F90ULL   /* shell game-select object (+0x1AC game, +0x1B0 version) */
#define ROOT_SLOT    0x1422D2B90ULL   /* CVS1 published root, setup 0x1405A6440 */
#define PHASE_SLOT   0x1422D2BA0ULL   /* == root + 0x3E58                    */
#define POLL_MS      33u              /* faster than a 60 fps frame; see cvs2_select_tick */

/* Code this adapter's offsets and names depend on, checked ONCE (`.text` is
 * immutable here). Bytes from the reviewed dump, section SHA-256 9c340b8c... */
static const struct { ULONGLONG address; BYTE bytes[16]; unsigned n; } g_sig[] = {
    { 0x14060B431ULL, { 0x42,0x88,0x44,0x81,0x78 }, 5 },              /* hover: P+0x78 store   */
    { 0x1405DA45FULL, { 0x40,0x88,0x6c,0x91,0x78 }, 5 },              /* commit: P+0x78 store  */
    { 0x1405D9C39ULL, { 0xc6,0x84,0x01,0xa0,0,0,0,0 }, 8 },           /* slot reset P+0xA0     */
    { 0x14004B340ULL, { 0x83,0xff,0x23 }, 3 },                        /* name fn: CVS1 limit   */
    { 0x14004B485ULL, { 0x49,0x8d,0x14,0x7e,0x49,0x8b,0x94,0xd4,0x40,0x66,0x1f,0x01 }, 12 }, /* name table */
    { 0x14007FF39ULL, { 0x8b,0x88,0xb0,0x01,0x00,0x00 }, 6 },         /* version at +0x1B0     */
};

/* Match-audio ownership for CVS1, worker only: a new epoch per presentation
 * owner (root + ROM, as for selection); the frame hook feeds only an epoch
 * that names game 0. Also the frame-stall guard: if the per-frame update
 * stops running (it may not run while paused), the frame thread cannot turn
 * the beacons off itself, so the worker does after STALL_MS. */
#define STALL_MS 250u
static void cvs1_audio_route(ULONGLONG root, unsigned rom)
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
        epoch = match_audio_begin(0);
        owner_root = root;
        owner_rom = rom;
        last_change = now;
        silenced = FALSE;
    }
    frames = cvs1_frame_count();
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

void cvs1_select_tick(void)
{
    static int guard_ready, sig_ok;
    static DWORD last_poll;
    static cvs1_select_sample sample;
    BYTE header[0x60], published[0x0C];
    ULONGLONG session, root, phase, shell, again;
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
                log_line("cvs1-select: ⛔ code at 0x%llx differs from the audited build -- "
                         "CVS1 selection speech is disabled.", g_sig[i].address);
                return;
            }
        }
        sig_ok = 1;
    }
    if (sig_ok < 0) return;

    /* ⭐ Everything here runs at POLL_MS, including the "am I active?" check:
     * CVS1 has no audio to silence promptly, so a 10 ms inactive path would
     * only add VirtualQuery traffic while another game is running. */
    now = GetTickCount();
    if (last_poll && now - last_poll < POLL_MS) return;
    last_poll = now;

    if (!read_at(&session, SESSION_SLOT, 8) || !session ||
        !read_at(header, session, sizeof header))
        goto inactive;
    memcpy(&game, header + 8, 4);
    memcpy(&rom, header + 0x44, 4);
    if (game != 0 || rom > 1) goto inactive;          /* rom index 0 cvs1 is VALID */

    if (!read_at(published, session + 0x1B0, sizeof published)) goto inactive;
    memcpy(&root, published, 8);
    memcpy(&size, published + 8, 4);
    if (!root || root > 0x00007fff00000000ULL || size != CVS1_STATE_SIZE ||
        !read_at(&again, ROOT_SLOT, 8) || again != root ||
        !read_at(&phase, PHASE_SLOT, 8) || phase != root + CVS1_PHASE_OFF)
        goto inactive;

    if (!read_at(&shell, SHELL_SLOT, 8) || !shell ||
        !read_at(&shell_game, shell + 0x1AC, 4) || shell_game != 0 ||
        !read_at(&sample.variant, shell + 0x1B0, 4) || sample.variant > 1)
        goto inactive;

    if (!read_at(sample.phase, root + CVS1_PHASE_OFF, CVS1_PHASE_SIZE)) goto inactive;
    for (unsigned side = 0; side < 2; side++) {
        ULONGLONG actor = root + CVS1_ACTOR_OFF + side * CVS1_ACTOR_STEP;
        if (!read_at(sample.actor[side], actor, CVS1_ACTOR_HEAD) ||
            !read_at(&sample.owner[side], actor + CVS1_ACTOR_OWNER, 1))
            goto inactive;
    }
    /* Bracket: the block and session must still be the ones we validated. */
    if (!read_at(&again, ROOT_SLOT, 8) || again != root ||
        !read_at(&again, SESSION_SLOT, 8) || again != session)
        goto inactive;
    {
        /* Evidence for the next run: the move list picks names by the shell's
         * version setting; record it beside the ROM it loaded, once per pair. */
        static unsigned logged = ~0u;
        unsigned pair = rom * 2 + sample.variant;
        if (pair != logged) {
            logged = pair;
            log_line("cvs1-select: admitted ROM %u (%s), shell game version %u",
                     rom, rom ? "cvs1u" : "cvs1", sample.variant);
        }
    }
    cvs1_audio_route(root, rom);
    cvs1_select_observe(&sample, root, rom);
    cvs1_text_observe(root);
    return;
inactive:
    cvs1_audio_route(0, 0);
    cvs1_select_observe(NULL, 0, 0);
    cvs1_text_observe(0);
}
