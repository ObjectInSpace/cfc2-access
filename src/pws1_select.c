/* See pws1_select.h. The Power Stone adapter for the shared selection
 * channel: native reads and gates here; decoding in pws_select_decode.c;
 * settling, grouping, retries and speech belong to selection_channel. No
 * hooks, no game-memory writes. */
#include "pws1_select.h"
#include "selection_channel.h"
#include "memsafe.h"
#include "match_audio.h"
#include "pws1_frame.h"
#include "audio.h"
#include "log.h"
#include "frame_liveness.h"
#include "gmd_find.h"
#include <string.h>

/* ---- stage select (Versus) ----------------------------------------------
 * Phase R+0x27E5D = 3 with sub-state R+0x27E5E = 1 and its step R+0x27E5F =
 * 1 is the stage select (live, 2026-10-24: 1 while choosing, 2 once chosen).
 * The cursor is the word R+0x27FC2, 0..9 with wrap (0x1409A710F up,
 * 0x1409A717B down, bounds 0x1409A716C / 0x1409A71D7); 0x1409A82AB maps it
 * through the table at 0x14109C540 to the stage ID kept at R+0x27FD1. Live:
 * cursor 6 gave ID 1, cursor 7 ID 9.
 *
 * Stage ID = the home stage of the fighter with that native ID: the movement
 * skips cursors 2 and 7 when stages are locked (0x14261E3F9), and those map
 * to IDs 8 and 9, Kraken's and Valgas's -- the two boss stages -- and the
 * collection's own soundtrack lists the stage themes in fighter-ID order.
 * The names are those themes' titles (SOUND_PWS1_*), so they follow the
 * game's language; the leading track number is not spoken. */
static const BYTE g_stage_ids[10] = { 3, 0, 8, 7, 2, 6, 1, 9, 5, 4 };
/* GMD label index of each stage's theme, by stage ID: Londo, Mutsu, Tong-An,
 * Manches, Dawnvolta, Dullstown, Oedo, Mahdad, Skullhaven, Avalon Is. 1. */
static const int g_stage_label[10] = { 3816, 3817, 3818, 3819, 3820, 3823, 3824, 3825, 3827, 3828 };

int pws1_stage_decode(BYTE phase, BYTE sub, BYTE step, int cursor)
{
    if (phase != 3 || sub != 1 || step != 1) return -1;
    if (cursor < 0 || cursor >= (int)sizeof g_stage_ids) return -2;
    return g_stage_ids[cursor];
}

BOOL pws1_stage_name(int id, wchar_t *out, unsigned cap)
{
    char text[256];
    const char *p;
    if (id < 0 || id >= (int)(sizeof g_stage_label / sizeof g_stage_label[0]) || !out || !cap) return FALSE;
    if (!gmd_text_by_index(g_stage_label[id], text, sizeof text) || !text[0]) return FALSE;
    /* "07. Londo Stage" -> "Londo Stage" */
    p = text;
    while (*p >= '0' && *p <= '9') p++;
    if (p != text && *p == '.') { p++; while (*p == ' ') p++; } else p = text;
    if (!*p) return FALSE;
    return MultiByteToWideChar(CP_UTF8, 0, p, -1, out, (int)cap) > 0;
}

void pws1_select_observe(const pws_select_sample *s, int stage, ULONGLONG root, unsigned rom)
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
    /* Compatibility identity only, as for the other adapters: root + ROM. The
     * root moved on each return to select in the capture, and each new screen
     * opened on a highlight that must be announced. */
    if (!channel.session.owned || root != owner_root || rom != owner_rom) {
        if (!selection_channel_begin(&channel, 4, rom, "pws1-select")) return;
        owner_root = root;
        owner_rom = rom;
    }
    pws_selection_copy(s, choices);
    /* The stage select has one cursor; it speaks as owner 1 once both
     * fighters are confirmed (their sides are then inactive). */
    if (stage != -1) {
        memset(&choices[0], 0, sizeof choices[0]);
        choices[0].status = GAME_SELECTION_UNAVAILABLE;
        if (stage >= 0 && pws1_stage_name(stage, choices[0].label, GAME_SELECTION_LABEL)) {
            choices[0].status = GAME_SELECTION_VALID;
            choices[0].kind = 1;
            choices[0].value = (unsigned)stage;
        }
    }
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < 2; ++p) if (accepted & (1u << p))
        log_line("pws1-select: accepted side=%u id=%u", p + 1, choices[p].value);
}

/* ---- acquisition (worker thread) ---------------------------------------- */

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out, (const void *)(ULONG_PTR)address, size); }

#define SESSION_SLOT 0x141C14F98ULL   /* emulator session singleton          */
#define SHELL_SLOT   0x141C14F90ULL   /* shell game-select object (+0x1AC game) */
#define ROOT_SLOT    0x1423188C0ULL   /* PWS1 published root                  */
#define ID_TABLE     0x14109C510ULL   /* cursor -> shell ID, 0x1409A0F83      */
#define POLL_MS      33u              /* faster than a 60 fps frame; see cvs2_select_tick */

/* Code this adapter's offsets and names depend on, checked ONCE (`.text` is
 * immutable here). Bytes from the reviewed dump. */
static const struct { ULONGLONG address; BYTE bytes[21]; unsigned n; } g_sig[] = {
    { 0x140976BBFULL, { 0x0f,0xb6,0x91,0x58,0x7e,0x02,0x00 }, 7 },                 /* running R+0x27E58 */
    { 0x140976CF8ULL, { 0x80,0xb9,0x8b,0x7f,0x02,0x00,0x00,0x75,0x45,
                        0x80,0xb9,0x8e,0x7f,0x02,0x00,0x00,0x75,0x3c }, 18 },      /* both gates zero */
    { 0x140976E2CULL, { 0x0f,0xb6,0x82,0x5d,0x7e,0x02,0x00 }, 7 },                 /* phase R+0x27E5D */
    { 0x14097700BULL, { 0xe8,0xd0,0xf7,0x02,0x00 }, 5 },                           /* phase 2 -> select */
    { 0x1409A67FCULL, { 0x41,0x0f,0xb6,0x90,0x5e,0x7e,0x02,0x00 }, 8 },            /* sub R+0x27E5E */
    { 0x1409A69A1ULL, { 0x41,0x0f,0xb6,0x88,0xf1,0x7f,0x02,0x00 }, 8 },            /* owner mask */
    { 0x1409A097AULL, { 0x4c,0x0f,0xbe,0x49,0x03 }, 5 },                           /* slot actor+3 */
    { 0x1409A0990ULL, { 0x45,0x0f,0xbe,0x94,0x10,0xc2,0x7d,0x02,0x00 }, 9 },       /* side state */
    { 0x1409A0F7AULL, { 0x49,0x0f,0xbe,0x84,0x10,0xcc,0x7d,0x02,0x00,0x42,0x0f,0xb6,0x84,
                        0x30,0x10,0xc5,0x09,0x01,0x88,0x43,0x04 }, 21 },           /* cursor -> ID */
    { 0x1409A10C9ULL, { 0x0f,0xbe,0x53,0x04,0x89,0x94,0x88,0x78,0x05,0x00,0x00 }, 11 }, /* to shell+0x578 */
    { 0x1409A82ABULL, { 0x48,0x0f,0xbf,0x81,0xc2,0x7f,0x02,0x00,0x0f,0xb6,0x84,0x30,0x40,0xc5,0x09,0x01 }, 16 }, /* stage table */
    { 0x1409A710FULL, { 0x66,0xff,0x80,0xc2,0x7f,0x02,0x00 }, 7 },                 /* stage cursor up */
    { 0x1409A716CULL, { 0x66,0x83,0xb8,0xc2,0x7f,0x02,0x00,0x09,0x7e,0x77 }, 10 }, /* .. at most 9 */
    { 0x1409A71D7ULL, { 0x66,0x83,0xb8,0xc2,0x7f,0x02,0x00,0x00,0x7d,0x0c }, 10 }, /* .. at least 0 */
};
static const BYTE g_ids[11] = { 8, 0, 6, 2, 4, 3, 5, 7, 1, 9, 10 };
#define STAGE_TABLE  0x14109C540ULL   /* stage cursor -> stage ID, 0x1409A82AB */

/* Match-audio ownership for PWS1, worker only, as stg2_audio_route: a new
 * epoch per presentation owner (root + ROM); the frame hook feeds only an
 * epoch that names game 4. There are no continuous cues to silence on a
 * stalled frame: pings are one-shots fired from the frame hook itself, and
 * only on frames the game simulates. */
#define STALL_MS 250u
static void pws1_audio_route(ULONGLONG root, unsigned rom)
{
    static uint64_t epoch;
    static ULONGLONG owner_root;
    static unsigned owner_rom;
    if (!root) {
        if (epoch) match_audio_depart(epoch);
        epoch = 0;
        return;
    }
    if (!epoch || root != owner_root || rom != owner_rom ||
        match_audio_current() != epoch) {
        epoch = match_audio_begin(4);
        owner_root = root;
        owner_rom = rom;
    }
}

void pws1_select_tick(void)
{
    static int guard_ready, sig_ok;
    static DWORD last_poll;
    static pws_select_sample sample;
    BYTE header[0x60], published[0x0C], window[PWS1_WINDOW_SIZE], actor[2][4];
    DWORD shuffle[2];
    ULONGLONG session, root, shell, again;
    unsigned game, rom, size, shell_game;
    int stage;
    DWORD now;

    if (!guard_ready) {
        if (!mem_guard_install()) return;
        guard_ready = 1;
    }
    if (sig_ok == 0) {
        BYTE ids[sizeof g_ids], stages[sizeof g_stage_ids];
        for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++) {
            BYTE got[21];
            if (!read_at(got, g_sig[i].address, g_sig[i].n)) return;   /* retry later */
            if (memcmp(got, g_sig[i].bytes, g_sig[i].n)) {
                sig_ok = -1;
                log_line("pws1-select: ⛔ code at 0x%llx differs from the audited build -- "
                         "Power Stone selection speech is disabled.", g_sig[i].address);
                return;
            }
        }
        if (!read_at(ids, ID_TABLE, sizeof ids)) return;
        if (memcmp(ids, g_ids, sizeof ids)) {
            sig_ok = -1;
            log_line("pws1-select: ⛔ the cursor ID table differs from the audited build -- "
                     "Power Stone selection speech is disabled.");
            return;
        }
        if (!read_at(stages, STAGE_TABLE, sizeof stages)) return;
        if (memcmp(stages, g_stage_ids, sizeof stages)) {
            sig_ok = -1;
            log_line("pws1-select: ⛔ the stage table differs from the audited build -- "
                     "Power Stone selection speech is disabled.");
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
    if (game != 4 || (rom != 8 && rom != 9)) goto inactive;

    if (!read_at(published, session + 0x1B0, sizeof published)) goto inactive;
    memcpy(&root, published, 8);
    memcpy(&size, published + 8, 4);
    if (!root || root > 0x00007fff00000000ULL || size != PWS1_STATE_SIZE ||
        !read_at(&again, ROOT_SLOT, 8) || again != root)
        goto inactive;

    if (!read_at(&shell, SHELL_SLOT, 8) || !shell ||
        !read_at(&shell_game, shell + 0x1AC, 4) || shell_game != 4)
        goto inactive;

    /* Fighters at R+0x18428 + side*0x7890: slot byte +3, shuffle +0x7874. */
    if (!read_at(window, root + PWS1_WINDOW_OFF, sizeof window) ||
        !read_at(actor[0], root + 0x18428, 4) || !read_at(&shuffle[0], root + 0x18428 + 0x7874, 4) ||
        !read_at(actor[1], root + 0x1FCB8, 4) || !read_at(&shuffle[1], root + 0x1FCB8 + 0x7874, 4))
        goto inactive;
    /* Bracket: the block and session must still be the ones we validated. */
    if (!read_at(&again, ROOT_SLOT, 8) || again != root ||
        !read_at(&again, SESSION_SLOT, 8) || again != session)
        goto inactive;

    /* The same fields pws_select_from_state takes from a whole block. */
    memset(&sample, 0, sizeof sample);
    sample.game = 4;
    sample.variant = rom == 9 ? 1 : 0;
    memcpy(sample.phase, window + (0x27E58 - PWS1_WINDOW_OFF), sizeof sample.phase);
    sample.gates[0] = window[0x27F8B - PWS1_WINDOW_OFF];
    sample.gates[1] = window[0x27F8E - PWS1_WINDOW_OFF];
    sample.mask = window[0x27FF1 - PWS1_WINDOW_OFF];
    for (unsigned p = 0; p < 2; p++) {
        sample.state[p] = window[0x27DC2 + p * 0x78 - PWS1_WINDOW_OFF];
        sample.cursor[p] = window[0x27DCC + p * 0x78 - PWS1_WINDOW_OFF];
        sample.slot[p] = actor[p][3];
        sample.shuffle[p] = shuffle[p];
    }
    {
        WORD cursor;
        memcpy(&cursor, window + (0x27FC2 - PWS1_WINDOW_OFF), 2);
        stage = sample.gates[0] || sample.gates[1] ? -1 :
                pws1_stage_decode(window[0x27E5D - PWS1_WINDOW_OFF], window[0x27E5E - PWS1_WINDOW_OFF],
                                  window[0x27E5F - PWS1_WINDOW_OFF], (short)cursor);
    }
    {
        static unsigned logged = ~0u;
        if (rom != logged) {
            logged = rom;
            log_line("pws1-select: admitted ROM %u (%s)", rom, rom == 9 ? "pws1u" : "pws1");
        }
    }
    pws1_audio_route(root, rom);
    /* Only while the update runs: a quit game's block stays frozen on its
     * last screen and passes every gate above (frame_liveness.h). */
    {
        static frame_liveness live;
        if (frame_liveness_live(&live, pws1_frame_count(), GetTickCount64(), STALL_MS))
            pws1_select_observe(&sample, stage, root, rom);
        else
            pws1_select_observe(NULL, -1, 0, 0);
    }
    return;
inactive:
    pws1_audio_route(0, 0);
    pws1_select_observe(NULL, -1, 0, 0);
}
