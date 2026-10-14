/* See z3u_select.h. The Alpha 3 adapter for the shared selection channel:
 * native reads, gates and decoding here; settling, grouping, retries and
 * speech belong to selection_channel. No hooks, no game-memory writes. */
#include "z3u_select.h"
#include "selection_channel.h"
#include "character_name_table.h"
#include "memsafe.h"
#include "match_audio.h"
#include "z3u_frame.h"
#include "z3u_text.h"
#include "audio.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

/* Ism names. The shell's style-name function 0x14004AE30 names game 3's
 * styles 0 X-ISM, 1 Z-ISM, 2 V-ISM (table 0x1411F7C28; checked at runtime).
 * The game's own value is object+0x202 = style - 1. The US release calls the
 * middle style A-ISM, so the US game version says that instead. */
static const wchar_t *const g_ism[2][3] = {
    { L"X-ism", L"Z-ism", L"V-ism" },
    { L"X-ism", L"A-ism", L"V-ism" },
};
/* Speed menu: the cursor indexes 0x1412DBDC8 to the game speed, 0 -> 6 and
 * 1 -> 8 in the capture, so 1 is the faster setting. */
static const wchar_t *const g_speed[2] = { L"Normal", L"Turbo" };

/* ---- decoding (pure, on the owned copy) --------------------------------- */

static unsigned word_at(const BYTE *b) { return (unsigned)b[0] | (unsigned)b[1] << 8; }

int z3u_select_decode(const z3u_select_sample *s, unsigned side, unsigned *kind)
{
    const BYTE *o;
    if (!s || side > 1 || !kind) return -1;
    o = s->obj[side];
    /* The game task (R+0x28) dispatches on R+0x2A through 0x1412D7A48; entry 1
     * reaches 0x1408A7F50, which dispatches on R+0x2C; entry 2 is the select
     * screen 0x1408AA700. */
    if (!word_at(s->mode) || word_at(s->mode + 2) != 1 || word_at(s->mode + 4) != 2) return -1;
    /* 0x1408A9D33: a side's select state runs only when its bit, object+0x1D1,
     * is set in R+0x59 | R+0x69. Anything else is the CPU side's default. */
    if (o[Z3U_SIDE] != side || !((s->entered | s->joined) >> side & 1)) return -1;
    /* State object+4 through 0x1412DBCE0; each state's object+5 sub-state 1
     * is the choosing routine. */
    if (o[Z3U_SUB] != 1) return -1;
    switch (o[Z3U_STATE]) {
    case 0:   /* 0x1408AA120: hover writer 0x1408AA2FD stores the grid cell */
        *kind = Z3U_KIND_CHARACTER;
        /* A random cell sets object+0x59 (0x1408AA2DB) and the ID is re-rolled
         * every frame: the cell is spoken, not the roll. */
        if (o[Z3U_RANDOM]) return 0xFF;
        if (o[Z3U_ID] >= Z3U_ID_LIMIT || !character_name(3, o[Z3U_ID], s->variant)) return -2;
        return o[Z3U_ID];
    case 2: { /* 0x1408AA650: up/down step object+0x202 within -1..1 */
        int ism = (signed char)o[Z3U_ISM];
        *kind = Z3U_KIND_ISM;
        return ism >= -1 && ism <= 1 ? ism + 1 : -2;
    }
    case 3:   /* 0x1408AAAE0: up/down toggle bit 0 of R+0x3C */
        *kind = Z3U_KIND_SPEED;
        return s->speed <= 1 ? s->speed : -2;
    default:
        return -1;
    }
}

void z3u_selection_copy(const z3u_select_sample *s,
                        game_selection_choice out[GAME_SELECTION_OWNERS])
{
    if (!out) return;
    memset(out, 0, sizeof(*out) * GAME_SELECTION_OWNERS);
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i)
        out[i].status = GAME_SELECTION_INACTIVE;
    for (unsigned side = 0; side < 2; ++side) {
        unsigned kind = 0;
        int value = z3u_select_decode(s, side, &kind);
        if (value < 0 || s->variant > 1) {
            out[side].status = value == -1 ? GAME_SELECTION_INACTIVE : GAME_SELECTION_UNAVAILABLE;
            continue;
        }
        out[side].status = GAME_SELECTION_VALID;
        out[side].kind = kind;
        out[side].value = (unsigned)value;
        if (kind == Z3U_KIND_ISM)
            swprintf(out[side].label, GAME_SELECTION_LABEL, L"%ls", g_ism[s->variant][value]);
        else if (kind == Z3U_KIND_SPEED)
            swprintf(out[side].label, GAME_SELECTION_LABEL, L"%ls", g_speed[value]);
        else if (value == 0xFF)
            swprintf(out[side].label, GAME_SELECTION_LABEL, L"Random");
        else
            swprintf(out[side].label, GAME_SELECTION_LABEL, L"%hs",
                     character_name(3, (unsigned)value, s->variant));
    }
}

void z3u_select_observe(const z3u_select_sample *s, ULONGLONG root, unsigned rom)
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
    /* Compatibility identity only, as for CVS1, CVS2 and CFJ: root + ROM. */
    if (!channel.session.owned || root != owner_root || rom != owner_rom) {
        if (!selection_channel_begin(&channel, 3, rom, "z3u-select")) return;
        owner_root = root;
        owner_rom = rom;
    }
    z3u_selection_copy(s, choices);
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < 2; ++p) if (accepted & (1u << p))
        log_line("z3u-select: accepted side=%u kind=%u value=%u", p + 1,
                 choices[p].kind, choices[p].value);
}

/* ---- acquisition (worker thread) ---------------------------------------- */

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out, (const void *)(ULONG_PTR)address, size); }

#define SESSION_SLOT 0x141C14F98ULL   /* emulator session singleton          */
#define SHELL_SLOT   0x141C14F90ULL   /* shell game-select object (+0x1AC game, +0x1B0 version) */
#define ROOT_SLOT    0x1422FB960ULL   /* Alpha 3 published root, setup 0x14086CFC0 */
#define POLL_MS      33u              /* faster than a 60 fps frame; see cvs2_select_tick */
#define OBJ_READ     0x203u           /* object bytes up to +0x202            */

/* Code this adapter's offsets and names depend on, checked ONCE (`.text` is
 * immutable here). Bytes from the reviewed dump. */
static const struct { ULONGLONG address; BYTE bytes[24]; unsigned n; } g_sig[] = {
    { 0x140895C68ULL, { 0x48,0x8d,0x58,0x28 }, 4 },                          /* task block R+0x28 */
    { 0x140895CEDULL, { 0x0f,0xb7,0x43,0x02,0xff,0x94,0xc6,0x48,0x7a,0x2d,0x01 }, 11 }, /* mode +0x2A */
    { 0x140899E70ULL, { 0xe9,0xdb,0xe0,0x00,0x00 }, 5 },                     /* -> 0x1408A7F50    */
    { 0x1408A7F6FULL, { 0x0f,0xb7,0x41,0x2c }, 4 },                          /* screen R+0x2C     */
    { 0x1408A9D02ULL, { 0x41,0x0f,0xbe,0x41,0x59,0x4d,0x8d,0x81,0xb0,0x01,0x00,0x00,
                        0x41,0x0f,0xbe,0x49,0x69,0x49,0x8d,0xb1,0x00,0x07,0x00,0x00 }, 24 }, /* masks, objects */
    { 0x1408A9D33ULL, { 0x0f,0xb6,0x86,0xd1,0x01,0x00,0x00,0x0f,0xa3,0xc1,0x73,0x19,
                        0x0f,0xb6,0x46,0x04 }, 16 },                         /* side bit, state   */
    { 0x1408A9D49ULL, { 0x41,0xff,0x94,0xc4,0xe0,0xbc,0x2d,0x01 }, 8 },      /* state table       */
    { 0x1408A9F80ULL, { 0x0f,0xb6,0x41,0x05,0x4c,0x8d,0x05,0x7d,0x1d,0xa3,0x00 }, 11 }, /* state 0 sub */
    { 0x1408AA295ULL, { 0x42,0x0f,0xb6,0x8c,0x28,0x90,0xbb,0x2d,0x01 }, 9 }, /* grid cell read    */
    { 0x1408AA2DBULL, { 0x88,0x4b,0x59 }, 3 },                               /* random marker     */
    { 0x1408AA2FDULL, { 0x88,0x8b,0xd2,0x01,0x00,0x00 }, 6 },                /* hover store       */
    { 0x1408AA5E0ULL, { 0x0f,0xb6,0x41,0x05,0x4c,0x8d,0x05,0x95,0x17,0xa3,0x00 }, 11 }, /* state 2 sub */
    { 0x1408AA6C2ULL, { 0x0f,0xbe,0x81,0x02,0x02,0x00,0x00,0xf6,0xc2,0x08 }, 10 }, /* ism, up */
    { 0x1408AA6EFULL, { 0x88,0x81,0x02,0x02,0x00,0x00 }, 6 },                /* ism store         */
    { 0x1408AA9D0ULL, { 0x0f,0xb6,0x41,0x05,0x4c,0x8d,0x05,0xd5,0x13,0xa3,0x00 }, 11 }, /* state 3 sub */
    { 0x1408AAB3EULL, { 0x66,0x83,0x70,0x3c,0x01 }, 5 },                     /* speed toggle      */
    { 0x14004B4E4ULL, { 0x83,0xff,0x2b,0x73,0x60,0x49,0x8d,0x04,0x7e,0x49,0x8b,0x84,
                        0xc4,0x20,0x6c,0x1f,0x01 }, 17 },                    /* name fn: Z3U case */
    { 0x14004AE6BULL, { 0x83,0xfa,0x03,0x75,0x18 }, 5 },                     /* style fn: game 3  */
    { 0x14007FF39ULL, { 0x8b,0x88,0xb0,0x01,0x00,0x00 }, 6 },                /* version at +0x1B0 */
};

/* Dispatch-table entries the gates name, the speed table and the shell's
 * style names. */
static const struct { ULONGLONG slot, target; } g_entries[] = {
    { 0x1412D7A50ULL, 0x140899E70ULL },   /* game mode 1               */
    { 0x1412DBC70ULL, 0x1408AA700ULL },   /* screen 2: select          */
    { 0x1412DBCE0ULL, 0x1408A9F80ULL },   /* object state 0: character */
    { 0x1412DBCF0ULL, 0x1408AA5E0ULL },   /* object state 2: ism       */
    { 0x1412DBCF8ULL, 0x1408AA9D0ULL },   /* object state 3: speed     */
    { 0x1412DBD10ULL, 0x1408AA120ULL },   /* state 0 sub 1             */
    { 0x1412DBD88ULL, 0x1408AA650ULL },   /* state 2 sub 1             */
    { 0x1412DBDB8ULL, 0x1408AAAE0ULL },   /* state 3 sub 1             */
};

static BOOL data_matches(void)
{
    static const char *const ism[3] = { "X-ISM", "Z-ISM", "V-ISM" };
    static const BYTE speed[3] = { 0, 6, 8 };
    BYTE got[8];
    for (unsigned i = 0; i < sizeof g_entries / sizeof g_entries[0]; i++) {
        ULONGLONG v;
        if (!read_at(&v, g_entries[i].slot, 8) || v != g_entries[i].target) return FALSE;
    }
    if (!read_at(got, 0x1412DBDC8ULL, 3) || memcmp(got, speed, 3)) return FALSE;
    for (unsigned i = 0; i < 3; i++) {
        ULONGLONG p;
        if (!read_at(&p, 0x1411F7C28ULL + 8 * i, 8) || !read_at(got, p, 6) ||
            memcmp(got, ism[i], 6))
            return FALSE;
    }
    return TRUE;
}

/* Match-audio ownership for Alpha 3, worker only, as cfj_audio_route: a new
 * epoch per presentation owner (root + ROM); the frame hook feeds only an
 * epoch that names game 3. The whole block froze while paused in the
 * capture (the update did not run), so the worker turns the beacons off
 * after STALL_MS without frames. */
#define STALL_MS 250u
static void z3u_audio_route(ULONGLONG root, unsigned rom)
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
        epoch = match_audio_begin(3);
        owner_root = root;
        owner_rom = rom;
        last_change = now;
        silenced = FALSE;
    }
    frames = z3u_frame_count();
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

void z3u_select_tick(void)
{
    static int guard_ready, sig_ok;
    static DWORD last_poll;
    static z3u_select_sample sample;
    BYTE header[0x60], published[0x0C], low[0x70], obj[OBJ_READ];
    ULONGLONG session, root, shell, again;
    unsigned game, rom, size, shell_game;
    DWORD now;

    if (!guard_ready) {
        if (!mem_guard_install()) return;
        guard_ready = 1;
    }
    if (sig_ok == 0) {
        for (unsigned i = 0; i < sizeof g_sig / sizeof g_sig[0]; i++) {
            BYTE got[24];
            if (!read_at(got, g_sig[i].address, g_sig[i].n)) return;   /* retry later */
            if (memcmp(got, g_sig[i].bytes, g_sig[i].n)) {
                sig_ok = -1;
                log_line("z3u-select: ⛔ code at 0x%llx differs from the audited build -- "
                         "Alpha 3 selection speech is disabled.", g_sig[i].address);
                return;
            }
        }
        if (!data_matches()) {
            sig_ok = -1;
            log_line("z3u-select: ⛔ a select table or style name differs from the "
                     "audited build -- Alpha 3 selection speech is disabled.");
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
    if (game != 3 || (rom != 6 && rom != 7)) goto inactive;

    if (!read_at(published, session + 0x1B0, sizeof published)) goto inactive;
    memcpy(&root, published, 8);
    memcpy(&size, published + 8, 4);
    if (!root || root > 0x00007fff00000000ULL || size != Z3U_STATE_SIZE ||
        !read_at(&again, ROOT_SLOT, 8) || again != root)
        goto inactive;

    if (!read_at(&shell, SHELL_SLOT, 8) || !shell ||
        !read_at(&shell_game, shell + 0x1AC, 4) || shell_game != 3 ||
        !read_at(&sample.variant, shell + 0x1B0, 4) || sample.variant > 1)
        goto inactive;

    if (!read_at(low, root, sizeof low)) goto inactive;
    memcpy(sample.mode, low + 0x28, sizeof sample.mode);
    sample.speed = low[0x3C];
    sample.entered = low[0x59];
    sample.joined = low[0x69];
    for (unsigned side = 0; side < 2; side++) {
        BYTE *f = sample.obj[side];
        if (!read_at(obj, root + Z3U_OBJ_OFF + side * Z3U_OBJ_STEP, sizeof obj)) goto inactive;
        f[Z3U_STATE] = obj[0x04];
        f[Z3U_SUB] = obj[0x05];
        f[Z3U_SIDE] = obj[0x1D1];
        f[Z3U_ID] = obj[0x1D2];
        f[Z3U_RANDOM] = obj[0x59];
        f[Z3U_ISM] = obj[0x202];
    }
    /* Bracket: the block and session must still be the ones we validated. */
    if (!read_at(&again, ROOT_SLOT, 8) || again != root ||
        !read_at(&again, SESSION_SLOT, 8) || again != session)
        goto inactive;
    {
        static unsigned logged = ~0u;
        unsigned pair = rom * 2 + sample.variant;
        if (pair != logged) {
            logged = pair;
            log_line("z3u-select: admitted ROM %u (%s), shell game version %u",
                     rom, rom == 7 ? "a3u" : "z3u", sample.variant);
        }
    }
    z3u_audio_route(root, rom);
    z3u_select_observe(&sample, root, rom);
    z3u_text_observe(root);
    return;
inactive:
    z3u_audio_route(0, 0);
    z3u_select_observe(NULL, 0, 0);
    z3u_text_observe(0);
}
