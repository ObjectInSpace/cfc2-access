/* See z3u_select.h. The Alpha 3 adapter for the shared selection channel:
 * native reads, gates and decoding here; settling, grouping, retries and
 * speech belong to selection_channel. No hooks, no game-memory writes. */
#include "z3u_select.h"
#include "selection_channel.h"
#include "mod_text.h"
#include "character_name_table.h"
#include "memsafe.h"
#include "match_audio.h"
#include "z3u_frame.h"
#include "z3u_text.h"
#include "z3u_branch.h"
#include "audio.h"
#include "log.h"
#include "frame_liveness.h"
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
static const mt_id g_speed[2] = { MT_NORMAL, MT_TURBO };   /* the collection's own words */
/* EX Mode Select (object state 1). Confirm (0x140E0BA23; ordinary
 * 0x1408AABA3) sets nothing for 0, one flag each for 1 and 2, and for 3 a
 * flag plus ism 0xFF, which skips the ism menu (live: P1 confirmed 3 and went
 * to speed; P2 confirmed 0 and went to ism). With Mode Select off the game's
 * own button codes set the same flags (0x140DFC1FF; ordinary 0x14089B71F):
 * LP+LK -> 2, MP+MK -> 1 only behind two unlock flags, HP+HK -> 3. Those are
 * Alpha 3's Saikyo, Mazi (unlocked) and Classical (no ism) codes; the names
 * are matched from that, not read from the screen, so listening confirms. */
static const mt_id g_mode[4] = { MT_MODE_NORMAL, MT_MODE_MAZI, MT_MODE_SAIKYO, MT_MODE_CLASSICAL };

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
        if (o[Z3U_ID] >= s->id_limit ||
            !character_name(3, z3u_name_index(o[Z3U_ID]), s->variant)) return -2;
        return o[Z3U_ID];
    case 1: { /* 0x1408AA8B0: EX Mode Select; up/down step the cursor 0..4,
               * skipping values the availability mask (0x1408A83A0) leaves
               * out; 4 is always left out. Live: 0..3 on both sides. */
        int mode = (signed char)o[Z3U_MODE];
        *kind = Z3U_KIND_MODE;
        return mode >= 0 && mode <= 3 ? mode : -2;
    }
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
            swprintf(out[side].label, GAME_SELECTION_LABEL, L"%ls", mt(g_speed[value]));
        else if (kind == Z3U_KIND_MODE)
            mt_fmt(out[side].label, GAME_SELECTION_LABEL, MT_MODE, mt(g_mode[value]), NULL, NULL);
        else if (value == 0xFF)
            swprintf(out[side].label, GAME_SELECTION_LABEL, L"%ls", mt(MT_RANDOM));
        else
            swprintf(out[side].label, GAME_SELECTION_LABEL, L"%hs",
                     character_name(3, z3u_name_index((unsigned)value), s->variant));
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
#define POLL_MS      33u              /* faster than a 60 fps frame; see cvs2_select_tick */
#define OBJ_READ     0x243u           /* object bytes up to the alternate mode +0x242 */

typedef struct { ULONGLONG address; BYTE bytes[24]; unsigned n; } code_sig;
typedef struct { ULONGLONG slot, target; } table_entry;

/* Code this adapter's offsets and names depend on, checked ONCE per copy
 * (`.text` is immutable here). Bytes from the reviewed dump. */
static const code_sig g_sig_ord[] = {
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
    { 0x1408AA926ULL, { 0x0f,0xbe,0x93,0x37,0x02,0x00,0x00 }, 7 },          /* mode cursor read  */
    { 0x1408AA964ULL, { 0x88,0x93,0x37,0x02,0x00,0x00 }, 6 },                /* mode cursor store */
    { 0x1408AABA3ULL, { 0x0f,0xb6,0x81,0x37,0x02,0x00,0x00 }, 7 },          /* mode confirm      */
    { 0x14089B71FULL, { 0xba,0x90,0x00,0x00,0x00,0x66,0x3b,0xc2,0x75,0x08,0xc6,0x81,0x01,0x02,0x00,0x00,0x01 }, 17 }, /* LP+LK code */
};

/* The alternate copy's same instructions, each aligned with the ordinary
 * one above by tools/branch_map.py: objects at +0x1F8/+0x778, the side at
 * +0x1D9, the random marker at +0x61, the hover ID at +0x1DA, the ism at
 * +0x20A; the grid is read through r12 = 0x1419E83F0 (lea at 0x140E0B076). */
static const code_sig g_sig_alt[] = {
    { 0x140DF6C68ULL, { 0x48,0x8d,0x58,0x28 }, 4 },                          /* task block R+0x28 */
    { 0x140DF6CEDULL, { 0x0f,0xb7,0x43,0x02,0xff,0x94,0xc6,0xb8,0x3b,0x9e,0x01 }, 11 }, /* mode +0x2A */
    { 0x140DFAEB0ULL, { 0xe9,0x3b,0xdd,0x00,0x00 }, 5 },                     /* -> 0x140E08BF0    */
    { 0x140E08C0FULL, { 0x0f,0xb7,0x41,0x2c }, 4 },                          /* screen R+0x2C     */
    { 0x140E0AB92ULL, { 0x41,0x0f,0xbe,0x41,0x59,0x4d,0x8d,0x81,0xf8,0x01,0x00,0x00,
                        0x41,0x0f,0xbe,0x49,0x69,0x49,0x8d,0xb1,0x78,0x07,0x00,0x00 }, 24 }, /* masks, objects */
    { 0x140E0ABC3ULL, { 0x0f,0xb6,0x86,0xd9,0x01,0x00,0x00,0x0f,0xa3,0xc1,0x73,0x19,
                        0x0f,0xb6,0x46,0x04 }, 16 },                         /* side bit, state   */
    { 0x140E0ABD9ULL, { 0x41,0xff,0x94,0xc4,0x80,0x85,0x9e,0x01 }, 8 },      /* state table       */
    { 0x140E0AE10ULL, { 0x0f,0xb6,0x41,0x05,0x4c,0x8d,0x05,0x8d,0xd7,0xbd,0x00 }, 11 }, /* state 0 sub */
    { 0x140E0B076ULL, { 0x4c,0x8d,0x25,0x73,0xd3,0xbd,0x00 }, 7 },           /* grid base         */
    { 0x140E0B0EEULL, { 0x42,0x0f,0xb6,0x0c,0x20 }, 5 },                     /* grid cell read    */
    { 0x140E0B13FULL, { 0x88,0x4b,0x61 }, 3 },                               /* random marker     */
    { 0x140E0B161ULL, { 0x88,0x8b,0xda,0x01,0x00,0x00 }, 6 },                /* hover store       */
    { 0x140E0B440ULL, { 0x0f,0xb6,0x41,0x05,0x4c,0x8d,0x05,0xd5,0xd1,0xbd,0x00 }, 11 }, /* state 2 sub */
    { 0x140E0B572ULL, { 0x0f,0xbe,0x81,0x0a,0x02,0x00,0x00,0xf6,0xc2,0x08 }, 10 }, /* ism, up */
    { 0x140E0B59FULL, { 0x88,0x81,0x0a,0x02,0x00,0x00 }, 6 },                /* ism store         */
    { 0x140E0B8B0ULL, { 0x0f,0xb6,0x41,0x05,0x4c,0x8d,0x05,0x95,0xcd,0xbd,0x00 }, 11 }, /* state 3 sub */
    { 0x140E0B9C1ULL, { 0x66,0x83,0x70,0x3c,0x01 }, 5 },                     /* speed toggle      */
    { 0x140E0B807ULL, { 0x0f,0xbe,0x93,0x42,0x02,0x00,0x00 }, 7 },          /* mode cursor read  */
    { 0x140E0B844ULL, { 0x88,0x93,0x42,0x02,0x00,0x00 }, 6 },                /* mode cursor store */
    { 0x140E0BA23ULL, { 0x0f,0xb6,0x81,0x42,0x02,0x00,0x00 }, 7 },          /* mode confirm      */
    { 0x140DFC1FFULL, { 0xba,0x90,0x00,0x00,0x00,0x66,0x3b,0xc2,0x75,0x08,0xc6,0x81,0x09,0x02,0x00,0x00,0x01 }, 17 }, /* LP+LK code */
};

/* Shell code both copies share: the name and style functions and the
 * version field. */
static const code_sig g_sig_shell[] = {
    { 0x14004B4E4ULL, { 0x83,0xff,0x2b,0x73,0x60,0x49,0x8d,0x04,0x7e,0x49,0x8b,0x84,
                        0xc4,0x20,0x6c,0x1f,0x01 }, 17 },                    /* name fn: Z3U case */
    { 0x14004AE6BULL, { 0x83,0xfa,0x03,0x75,0x18 }, 5 },                     /* style fn: game 3  */
    { 0x14007FF39ULL, { 0x8b,0x88,0xb0,0x01,0x00,0x00 }, 6 },                /* version at +0x1B0 */
};

/* Dispatch-table entries the gates name. */
static const table_entry g_entries_ord[] = {
    { 0x1412D7A50ULL, 0x140899E70ULL },   /* game mode 1               */
    { 0x1412DBC70ULL, 0x1408AA700ULL },   /* screen 2: select          */
    { 0x1412DBCE0ULL, 0x1408A9F80ULL },   /* object state 0: character */
    { 0x1412DBCE8ULL, 0x1408AA7E0ULL },   /* object state 1: mode      */
    { 0x1412DBCF0ULL, 0x1408AA5E0ULL },   /* object state 2: ism       */
    { 0x1412DBCF8ULL, 0x1408AA9D0ULL },   /* object state 3: speed     */
    { 0x1412DBD10ULL, 0x1408AA120ULL },   /* state 0 sub 1             */
    { 0x1412DBDA0ULL, 0x1408AA8B0ULL },   /* state 1 sub 1             */
    { 0x1412DBD88ULL, 0x1408AA650ULL },   /* state 2 sub 1             */
    { 0x1412DBDB8ULL, 0x1408AAAE0ULL },   /* state 3 sub 1             */
};
static const table_entry g_entries_alt[] = {
    { 0x1419E3BC0ULL, 0x140DFAEB0ULL },   /* game mode 1               */
    { 0x1419E8508ULL, 0x140E0B5B0ULL },   /* screen 2: select          */
    { 0x1419E8580ULL, 0x140E0AE10ULL },   /* object state 0: character */
    { 0x1419E8588ULL, 0x140E0B6B0ULL },   /* object state 1: mode      */
    { 0x1419E8590ULL, 0x140E0B440ULL },   /* object state 2: ism       */
    { 0x1419E8598ULL, 0x140E0B8B0ULL },   /* object state 3: speed     */
    { 0x1419E85B0ULL, 0x140E0AFC0ULL },   /* state 0 sub 1             */
    { 0x1419E8640ULL, 0x140E0B780ULL },   /* state 1 sub 1             */
    { 0x1419E8628ULL, 0x140E0B500ULL },   /* state 2 sub 1             */
    { 0x1419E8658ULL, 0x140E0B960ULL },   /* state 3 sub 1             */
};

/* The alternate grid 0x1419E83F0, 7 by 7: the ordinary grid with native IDs
 * 40 and 41 at the ends of row 5 and 38 and 39 in place of two of the four
 * random cells in the bottom row (names through z3u_name_index: Eagle 38,
 * Maki 39, Yun 40, Ingrid 41). Checked so that a grid this adapter has not
 * seen cannot be read with the wrong limit. */
static const BYTE g_grid_alt[49] = {
    0x80,0x80,0x40,0x0a,0x41,0x80,0x80,
    0x80,0x20,0x03,0x04,0x0b,0x21,0x80,
    0x22,0x0c,0x07,0x1a,0x08,0x02,0x17,
    0x00,0x18,0x1b,0x1d,0x1c,0x19,0x01,
    0x1e,0x0f,0x06,0x16,0x05,0x10,0x1f,
    0x28,0x23,0x0e,0x0d,0x11,0x25,0x29,
    0xff,0xff,0x26,0x09,0x27,0xff,0xff,
};

typedef struct {
    const z3u_branch *b;
    const code_sig *sig;
    unsigned nsig;
    const table_entry *entries;
    unsigned nentries;
    ULONGLONG speed_table;
    int checked;                 /* 0 not yet, 1 matches, -1 differs */
} select_branch;

static select_branch g_branches[2] = {
    { &z3u_ordinary, g_sig_ord, sizeof g_sig_ord / sizeof g_sig_ord[0],
      g_entries_ord, sizeof g_entries_ord / sizeof g_entries_ord[0], 0x1412DBDC8ULL, 0 },
    { &z3u_alternate, g_sig_alt, sizeof g_sig_alt / sizeof g_sig_alt[0],
      g_entries_alt, sizeof g_entries_alt / sizeof g_entries_alt[0], 0x1419E8668ULL, 0 },
};

/* 1 all match, 0 unreadable (retry later), -1 a difference. */
static int sigs_match(const code_sig *sig, unsigned n, const char *what)
{
    for (unsigned i = 0; i < n; i++) {
        BYTE got[24];
        if (!read_at(got, sig[i].address, sig[i].n)) return 0;
        if (memcmp(got, sig[i].bytes, sig[i].n)) {
            log_line("z3u-select: ⛔ %s code at 0x%llx differs from the audited build -- "
                     "Alpha 3 selection speech is disabled for it.", what, sig[i].address);
            return -1;
        }
    }
    return 1;
}

/* The data facts the decode rests on: the dispatch entries, the speed table,
 * the shell's style names and, for the alternate copy, its grid. */
static BOOL data_matches(const select_branch *sb)
{
    static const char *const ism[3] = { "X-ISM", "Z-ISM", "V-ISM" };
    static const BYTE speed[3] = { 0, 6, 8 };
    BYTE got[49];
    for (unsigned i = 0; i < sb->nentries; i++) {
        ULONGLONG v;
        if (!read_at(&v, sb->entries[i].slot, 8) || v != sb->entries[i].target) return FALSE;
    }
    if (!read_at(got, sb->speed_table, 3) || memcmp(got, speed, 3)) return FALSE;
    for (unsigned i = 0; i < 3; i++) {
        ULONGLONG p;
        if (!read_at(&p, 0x1411F7C28ULL + 8 * i, 8) || !read_at(got, p, 6) ||
            memcmp(got, ism[i], 6))
            return FALSE;
    }
    if (sb->b == &z3u_alternate &&
        (!read_at(got, 0x1419E83F0ULL, sizeof g_grid_alt) || memcmp(got, g_grid_alt, sizeof g_grid_alt)))
        return FALSE;
    return TRUE;
}

/* 1 trusted, 0 not yet readable, -1 refused (logged once). */
static int branch_checked(select_branch *sb)
{
    static int shell_ok;
    int r;
    if (sb->checked) return sb->checked;
    if (!shell_ok) {
        r = sigs_match(g_sig_shell, sizeof g_sig_shell / sizeof g_sig_shell[0], "shell");
        if (r == 0) return 0;
        shell_ok = r;
    }
    if (shell_ok < 0) return sb->checked = -1;
    r = sigs_match(sb->sig, sb->nsig, sb->b->name);
    if (r == 0) return 0;
    if (r > 0 && !data_matches(sb)) {
        log_line("z3u-select: ⛔ a %s select table or style name differs from the audited "
                 "build -- its selection speech is disabled.", sb->b->name);
        r = -1;
    }
    return sb->checked = r;
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
    static int guard_ready;
    static DWORD last_poll;
    static z3u_select_sample sample;
    static unsigned refused_rom = ~0u;
    BYTE header[0x60], published[0x0C], low[0x70], obj[OBJ_READ];
    ULONGLONG session, root, shell, again;
    unsigned game, rom, size, shell_game;
    const z3u_branch *b;
    select_branch *sb;
    DWORD now;

    if (!guard_ready) {
        if (!mem_guard_install()) return;
        guard_ready = 1;
    }

    now = GetTickCount();
    if (last_poll && now - last_poll < POLL_MS) return;
    last_poll = now;

    if (!read_at(&session, SESSION_SLOT, 8) || !session ||
        !read_at(header, session, sizeof header))
        goto inactive;
    memcpy(&game, header + 8, 4);
    memcpy(&rom, header + 0x44, 4);
    if (game != 3) goto inactive;
    b = z3u_branch_for_rom(rom);
    if (!b) {
        /* An Alpha 3 ROM neither copy names: say so once, not every poll. */
        if (rom != refused_rom) {
            refused_rom = rom;
            log_line("z3u-select: ⚠ Alpha 3 is running ROM %u, which no traced copy covers -- "
                     "silent.", rom);
        }
        goto inactive;
    }
    sb = &g_branches[b == &z3u_alternate];
    if (branch_checked(sb) <= 0) goto inactive;

    if (!read_at(published, session + 0x1B0, sizeof published)) goto inactive;
    memcpy(&root, published, 8);
    memcpy(&size, published + 8, 4);
    if (!root || root > 0x00007fff00000000ULL || size != b->state_size ||
        !read_at(&again, b->root_slot, 8) || again != root)
        goto inactive;

    if (!read_at(&shell, SHELL_SLOT, 8) || !shell ||
        !read_at(&shell_game, shell + 0x1AC, 4) || shell_game != 3 ||
        !read_at(&sample.variant, shell + 0x1B0, 4) || sample.variant > 1)
        goto inactive;
    sample.id_limit = b->id_limit;

    if (!read_at(low, root, sizeof low)) goto inactive;
    memcpy(sample.mode, low + 0x28, sizeof sample.mode);
    sample.speed = low[0x3C];
    sample.entered = low[0x59];
    sample.joined = low[0x69];
    for (unsigned side = 0; side < 2; side++) {
        BYTE *f = sample.obj[side];
        if (!read_at(obj, root + b->obj_off + side * b->obj_step, sizeof obj)) goto inactive;
        f[Z3U_STATE] = obj[0x04];
        f[Z3U_SUB] = obj[0x05];
        f[Z3U_SIDE] = obj[b->o_side];
        f[Z3U_ID] = obj[b->o_id];
        f[Z3U_RANDOM] = obj[b->o_random];
        f[Z3U_ISM] = obj[b->o_ism];
        f[Z3U_MODE] = obj[b->o_mode];
    }
    /* Bracket: the block and session must still be the ones we validated. */
    if (!read_at(&again, b->root_slot, 8) || again != root ||
        !read_at(&again, SESSION_SLOT, 8) || again != session)
        goto inactive;
    {
        static unsigned logged = ~0u;
        unsigned pair = rom * 2 + sample.variant;
        if (pair != logged) {
            logged = pair;
            log_line("z3u-select: admitted ROM %u (%s, %s copy), shell game version %u",
                     rom, rom == b->rom_us ? (b == &z3u_alternate ? "a3uu" : "a3u")
                                          : (b == &z3u_alternate ? "z3uu" : "z3u"),
                     b->name, sample.variant);
        }
    }
    z3u_audio_route(root, rom);
    /* Only while the update runs: a quit game's block stays frozen on its
     * last screen and passes every gate above (frame_liveness.h). A stall
     * departs, so a resumed or restarted select speaks its highlight. */
    {
        static frame_liveness live;
        if (frame_liveness_live(&live, z3u_frame_count(), GetTickCount64(), STALL_MS))
            z3u_select_observe(&sample, root, rom);
        else
            z3u_select_observe(NULL, 0, 0);
    }
    z3u_text_observe(root);
    return;
inactive:
    z3u_audio_route(0, 0);
    z3u_select_observe(NULL, 0, 0);
    z3u_text_observe(0);
}
