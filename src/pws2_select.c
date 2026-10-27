/* See pws2_select.h. The Power Stone 2 adapter for the shared selection
 * channel, after pws1_select.c: native reads and gates here; decoding in
 * pws_select_decode.c; settling, grouping, retries and speech belong to
 * selection_channel. No hooks, no game-memory writes. */
#include "pws2_select.h"
#include "selection_channel.h"
#include "memsafe.h"
#include "match_audio.h"
#include "pws2_frame.h"
#include "log.h"
#include "frame_liveness.h"
#include "mod_text.h"
#include "speech.h"
#include "audio.h"
#include "character_name_table.h"
#include <string.h>

void pws2_select_observe(const pws_select_sample *s, ULONGLONG root, unsigned rom)
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
        if (!selection_channel_begin(&channel, 5, rom, "pws2-select")) return;
        owner_root = root;
        owner_rom = rom;
    }
    pws_selection_copy(s, choices);
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < GAME_SELECTION_OWNERS; p++) if (accepted & (1u << p))
        log_line("pws2-select: accepted side=%u id=%u", p + 1, choices[p].value);
}

/* ---- pickup announcements (worker) --------------------------------------- */

/* What kind of thing an item code is, in words. The game names no items as
 * text, so the kind is what can be said. */
pickup_kind pws2_pickup_word(unsigned code)
{
    audio_ping_family f;
    int v;
    if (!pws2_pickup_kind(code, &f, &v)) return PICKUP_ITEM;
    switch (f) {
    case AUDIO_PING_STONE: return PICKUP_STONE;
    case AUDIO_PING_FOOD:  return PICKUP_FOOD;
    case AUDIO_PING_PROP:  return PICKUP_THROWABLE;
    default:               return code == 0xC6 ? PICKUP_TREASURE : PICKUP_ITEM;
    }
}

/* Speak each pending pickup: "Falcon, food". Setting (F9): 2 every fighter,
 * 1 players only, 0 off. `admitted` false drains silently. */
void pws2_select_speak_pickups(BOOL admitted, unsigned rom)
{
    pws2_pickup_event e;
    while (pws2_pickup_pop(&e)) {
        wchar_t say[512];
        /* Only the current owner's, and only while the game is admitted. */
        if (!admitted || e.epoch != match_audio_current()) continue;
        if (e.kind != PWS2_EVENT_PICKUP) {
            /* The stage select: "Stage select. Stage: 1, left. ..." as the
             * fighter lands; "Stage: 3" once one is chosen. Not under the F9
             * pickup setting. The stages have no names in the game's text;
             * numbered 1 to 5, left to right as the game numbers them. */
            static const mt_id dir[8] = { MT_DIR_AHEAD, MT_DIR_AHEAD_RIGHT, MT_DIR_RIGHT, MT_DIR_BEHIND_RIGHT,
                                          MT_DIR_BEHIND, MT_DIR_BEHIND_LEFT, MT_DIR_LEFT, MT_DIR_AHEAD_LEFT };
            wchar_t stage[64], part[128];
            if (e.kind == PWS2_EVENT_STAGE_CHOSEN) {
                swprintf(say, 512, L"%ls%u", mt(MT_STAGE_PREFIX), e.code + 1);
            } else {
                swprintf(say, 512, L"%ls.", mt(MT_STAGE_SELECT));
                for (unsigned k = 0; k < 5; k++) {
                    swprintf(stage, 64, L"%ls%u", mt(MT_STAGE_PREFIX), k + 1);
                    mt_fmt(part, 128, MT_PICKUP, stage, mt(dir[(e.code >> (3 * k)) & 7]), NULL);
                    wcsncat(say, L" ", 511 - wcslen(say));
                    wcsncat(say, part, 511 - wcslen(say));
                    wcsncat(say, L".", 511 - wcslen(say));
                }
            }
            speech_say(say, FALSE);
            log_line("pws2-select: stage event %u code 0x%x", e.kind, e.code);
            continue;
        }
        if (pickup_say(character_name(5, e.id, rom == 11 ? 1 : 0), pws2_pickup_word(e.code), e.human))
            log_line("pws2-select: pickup fighter=%u id=%u code=0x%x", e.fighter, e.id, e.code);
    }
}

/* ---- acquisition (worker thread) ---------------------------------------- */

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out, (const void *)(ULONG_PTR)address, size); }

#define SESSION_SLOT 0x141C14F98ULL   /* emulator session singleton          */
#define SHELL_SLOT   0x141C14F90ULL   /* shell game-select object (+0x1AC game) */
#define ROOT_SLOT    0x14262F0B0ULL   /* PWS2 published root                  */
#define POLL_MS      33u              /* faster than a 60 fps frame; see cvs2_select_tick */
#define ACTOR(p)     (0x40818u + (p) * 0x42B8u)
#define STEP_SELECT  1

/* Code this adapter's offsets depend on, checked ONCE (`.text` is immutable
 * here). Bytes from the reviewed dump; spans as in pws-selection-manifest. */
static const struct { ULONGLONG address; BYTE bytes[23]; unsigned n; } g_sig[] = {
    { 0x140A48D46ULL, { 0x80,0xb8,0x2b,0x2d,0x05,0x00,0x00,0x0f,0xb6,0x98,0xd9,0x2b,0x05,0x00,0x75,0x51,0x80,0xb8,0x2e,0x2d,0x05,0x00,0x00 }, 23 },   /* gates, nested phase R+0x52BD9 */
    { 0x140A48D7BULL, { 0xe8,0xc0,0x20,0x01,0x00,0xe8,0xab,0x3c,0x00,0x00 }, 10 },   /* nested 2 -> 0x140a4ca30 */
    { 0x140A4CA3FULL, { 0x0f,0xb6,0x91,0xdc,0x2b,0x05,0x00 }, 7 },   /* game phase R+0x52BDC */
    { 0x140A4CB48ULL, { 0x0f,0xb6,0x81,0xdd,0x2b,0x05,0x00 }, 7 },   /* step R+0x52BDD */
    { 0x140A4CDBCULL, { 0x67,0xcd,0xa4,0x00 }, 4 },   /* step table entry 1 */
    { 0x140A4CD67ULL, { 0xe8,0x54,0xc5,0x01,0x00 }, 5 },   /* .. -> select 0x140a692c0 */
    { 0x140A692CBULL, { 0x0f,0xb6,0x8a,0xde,0x2b,0x05,0x00 }, 7 },   /* sub-phase R+0x52BDE */
    { 0x140A69668ULL, { 0xf6,0x82,0x49,0x4b,0x05,0x00,0x01 }, 7 },   /* participation R+0x54B49 */
    { 0x140A6B135ULL, { 0x0f,0xbe,0x8c,0x02,0xd2,0x29,0x05,0x00 }, 8 },   /* side state R+0x529D2 */
    { 0x140A6B5EAULL, { 0x42,0x89,0x8c,0xb8,0x78,0x05,0x00,0x00 }, 8 },   /* commit to shell+0x578 */
};

/* Match-audio ownership for PWS2, worker only, as pws1_audio_route: a new
 * epoch per presentation owner (root + ROM); the frame hook feeds only an
 * epoch that names game 5. */
#define STALL_MS 250u
static void pws2_audio_route(ULONGLONG root, unsigned rom)
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
        epoch = match_audio_begin(5);
        owner_root = root;
        owner_rom = rom;
    }
}

void pws2_select_tick(void)
{
    static int guard_ready, sig_ok;
    static DWORD last_poll;
    static pws_select_sample sample;
    BYTE header[0x60], published[0x0C], window[PWS2_WINDOW_SIZE], mask;
    ULONGLONG session, root, shell, again;
    unsigned game, rom, size, shell_game;
    DWORD now;

    if (!guard_ready) {
        if (!mem_guard_install()) return;
        guard_ready = 1;
    }
    if (sig_ok == 0) {
        for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++) {
            BYTE got[23];
            if (!read_at(got, g_sig[i].address, g_sig[i].n)) return;   /* retry later */
            if (memcmp(got, g_sig[i].bytes, g_sig[i].n)) {
                sig_ok = -1;
                log_line("pws2-select: ⛔ code at 0x%llx differs from the audited build -- "
                         "Power Stone 2 selection speech is disabled.", g_sig[i].address);
                return;
            }
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
    if (game != 5 || (rom != 10 && rom != 11)) goto inactive;

    if (!read_at(published, session + 0x1B0, sizeof published)) goto inactive;
    memcpy(&root, published, 8);
    memcpy(&size, published + 8, 4);
    if (!root || root > 0x00007fff00000000ULL || size != PWS2_STATE_SIZE ||
        !read_at(&again, ROOT_SLOT, 8) || again != root)
        goto inactive;

    if (!read_at(&shell, SHELL_SLOT, 8) || !shell ||
        !read_at(&shell_game, shell + 0x1AC, 4) || shell_game != 5)
        goto inactive;

    if (!read_at(window, root + PWS2_WINDOW_OFF, sizeof window) ||
        !read_at(&mask, root + 0x54B49, 1))
        goto inactive;

    /* The same fields pws_select_from_state takes from a whole block. */
    memset(&sample, 0, sizeof sample);
    sample.game = 5;
    sample.variant = rom == 11 ? 1 : 0;
    memcpy(sample.phase, window + (0x52BD8 - PWS2_WINDOW_OFF), sizeof sample.phase);
    sample.gates[0] = window[0x52D2B - PWS2_WINDOW_OFF];
    sample.gates[1] = window[0x52D2E - PWS2_WINDOW_OFF];
    sample.mask = mask;
    /* Fighters (slot +1, highlighted ID +2, shuffle +0x4258) only on the
     * select step: eight small reads there, none during a fight. */
    if (sample.phase[5] == STEP_SELECT) {
        for (unsigned p = 0; p < 4; p++) {
            BYTE head[3];
            if (!read_at(head, root + ACTOR(p), sizeof head) ||
                !read_at(&sample.shuffle[p], root + ACTOR(p) + 0x4258, 4))
                goto inactive;
            sample.state[p] = window[0x529D2 + p * 0x98 - PWS2_WINDOW_OFF];
            sample.slot[p] = head[1];
            sample.cursor[p] = head[2];
        }
    }
    /* Bracket: the block and session must still be the ones we validated. */
    if (!read_at(&again, ROOT_SLOT, 8) || again != root ||
        !read_at(&again, SESSION_SLOT, 8) || again != session)
        goto inactive;
    {
        static unsigned logged = ~0u;
        if (rom != logged) {
            logged = rom;
            log_line("pws2-select: admitted ROM %u (%s)", rom, rom == 11 ? "pws2u" : "pws2");
        }
    }
    pws2_audio_route(root, rom);
    pws2_select_speak_pickups(TRUE, rom);
    /* Only while the update runs: a quit game's block stays frozen on its
     * last screen and passes every gate above (frame_liveness.h). */
    {
        static frame_liveness live;
        if (frame_liveness_live(&live, pws2_frame_count(), GetTickCount64(), STALL_MS))
            pws2_select_observe(&sample, root, rom);
        else
            pws2_select_observe(NULL, 0, 0);
    }
    return;
inactive:
    pws2_select_speak_pickups(FALSE, 0);
    pws2_audio_route(0, 0);
    pws2_select_observe(NULL, 0, 0);
}
