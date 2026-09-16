/*
 * emu_ram -- reach the emulated arcade machine's RAM. See emu_ram.h for how
 * the anchors were derived (offline, from the instructions, zero launches).
 *
 * This file is READ-ONLY with respect to the game: no hooks, no writes, no
 * calls into game code. Hooking has crashed this game before, and stages 1-4
 * of the in-game work need no hook at all.
 */

#include "emu_ram.h"
#include "memsafe.h"

#include <string.h>

/* Redirectable for the offline suite, exactly as rom_table.c does it. The
 * shipping code never calls the redirect; the default IS the real constant. */
static ULONGLONG g_state_slot = EMU_STATE_SLOT;

void emu_ram_test_redirect(ULONGLONG state_slot)
{
    g_state_slot = state_slot ? state_slot : EMU_STATE_SLOT;
}

const char *emu_status_name(emu_status s)
{
    switch (s) {
    case EMU_OK:          return "OK";
    case EMU_NOT_RUNNING: return "NOT_RUNNING";
    case EMU_UNREADABLE:  return "UNREADABLE";
    case EMU_UNVERIFIED:  return "UNVERIFIED";
    }
    return "?";
}

/* Every foreign read goes through mem_readable_run() FIRST and then a guarded
 * copy. ⛔ The guard is a NET for the race between the check and the copy, not
 * a replacement for the check: a guarded memcpy over a bad page is NOT
 * reliably caught -- this project has the crash that proves it. */
static BOOL read_qword(ULONGLONG addr, ULONGLONG *out)
{
    if (!mem_readable((const void *)(ULONG_PTR)addr, sizeof(*out)))
        return FALSE;
    return mem_copy(out, (const void *)(ULONG_PTR)addr, sizeof(*out));
}

static BOOL read_dword(ULONGLONG addr, unsigned *out)
{
    if (!mem_readable((const void *)(ULONG_PTR)addr, sizeof(*out)))
        return FALSE;
    return mem_copy(out, (const void *)(ULONG_PTR)addr, sizeof(*out));
}

emu_status emu_state(ULONGLONG *out)
{
    ULONGLONG st = 0;

    if (out)
        *out = 0;

    /* ⚠ A slot we cannot READ and a slot holding NULL are different facts.
     * The first means we never looked; the second is a real answer ("no game
     * is loaded"). Collapsing them is how a tool ends up reporting a confident
     * negative it never earned. */
    if (!read_qword(g_state_slot, &st))
        return EMU_UNREADABLE;

    if (st == 0)
        return EMU_NOT_RUNNING;

    /* The state object must itself be readable memory. A non-null value that
     * points nowhere means the slot held something stale or we are wrong about
     * the slot -- report UNVERIFIED, never pass it upward as a live object. */
    if (!mem_readable((const void *)(ULONG_PTR)st, 0x60))
        return EMU_UNVERIFIED;

    if (out)
        *out = st;
    return EMU_OK;
}

emu_status emu_arena(ULONGLONG *base_out, SIZE_T *size_out)
{
    ULONGLONG st = 0, arena = 0;
    emu_status s;
    MEMORY_BASIC_INFORMATION mbi;

    if (base_out) *base_out = 0;
    if (size_out) *size_out = 0;

    s = emu_state(&st);
    if (s != EMU_OK)
        return s;

    if (!read_qword(st + EMU_OFF_ARENA, &arena))
        return EMU_UNREADABLE;
    if (arena == 0)
        return EMU_NOT_RUNNING;

    /* The constructor asks for 0x1000 alignment. A value that is not page
     * aligned is not the pointer that instruction stored, whatever else it
     * might be. This is a cheap, specific falsifier -- not a plausibility
     * judgement. */
    if (arena & 0xFFFULL)
        return EMU_UNVERIFIED;

    if (!mem_readable((const void *)(ULONG_PTR)arena, 0x1000))
        return EMU_UNVERIFIED;

    /* Ask the memory manager how big the region actually is rather than
     * trusting the 256 MB constant from the constructor: the allocator may
     * sub-allocate, and a size we assumed would be a size we never measured. */
    if (VirtualQuery((LPCVOID)(ULONG_PTR)arena, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (size_out)
            *size_out = mbi.RegionSize;
    }

    if (base_out)
        *base_out = arena;
    return EMU_OK;
}

emu_status emu_rom_index(int *out)
{
    ULONGLONG st = 0;
    unsigned v = 0;
    emu_status s;

    if (out)
        *out = 0;

    s = emu_state(&st);
    if (s != EMU_OK)
        return s;

    if (!read_dword(st + EMU_OFF_ROM_INDEX, &v))
        return EMU_UNREADABLE;

    /* ⛔⛔ THE INDEX IS 0-BASED, AND READING IT AS 1-BASED SHIFTED EVERY GAME.
     * Caught by the cross-check on the first live run, which is the entire
     * reason that check exists. Read the loop at 0x1400b6250 carefully:
     *
     *     r10d = 0                       ; the counter starts at ZERO
     *     rax  = [0x141208b00]           ; entry [0] is loaded BEFORE the loop
     *     ...compare...                  ; and compared with r10d still 0
     *     match -> eax = r10d            ; so "cvs1" returns 0, not 1
     *     miss  -> rax = [rbx + r9*8 + 8]; only THEN advance to entry [1]
     *
     * The pre-loop load of entry [0] is what makes it look 1-based at a
     * glance: the `+8` in the loop body advances PAST the entry already
     * consumed, it does not index from one.
     *
     * ⚠ CONSEQUENCE: 0 is a VALID index (cvs1), not "nothing matched". The
     * first version returned NOT_RUNNING for it, which would have made the mod
     * silent for exactly one game -- invisible unless someone tested CVS1
     * specifically, and indistinguishable from "no game loaded" when they did.
     * The real "no game" signal is a null state slot, which emu_state()
     * already reports.
     *
     * ⛔ Do NOT clamp an out-of-range value: a clamped id would name a
     * different game's characters, confidently and wrongly. */
    if (v > 17)
        return EMU_UNVERIFIED;

    if (out)
        *out = (int)v;
    return EMU_OK;
}

emu_status emu_rom_name(char *out, unsigned cap)
{
    ULONGLONG st = 0, sp = 0;
    emu_status s;
    SIZE_T run;
    unsigned i;
    char tmp[32];

    if (!out || cap == 0)
        return EMU_UNREADABLE;
    out[0] = '\0';

    s = emu_state(&st);
    if (s != EMU_OK)
        return s;

    if (!read_qword(st + EMU_OFF_ROM_NAME, &sp))
        return EMU_UNREADABLE;
    if (sp == 0)
        return EMU_NOT_RUNNING;

    /* Bounded by what is actually readable, then by our own buffer. The ROM
     * names are 3-5 characters; anything long is not one of them. */
    run = mem_readable_run((const void *)(ULONG_PTR)sp, sizeof(tmp));
    if (run == 0)
        return EMU_UNVERIFIED;
    if (run > sizeof(tmp))
        run = sizeof(tmp);

    memset(tmp, 0, sizeof(tmp));
    if (!mem_copy(tmp, (const void *)(ULONG_PTR)sp, run))
        return EMU_UNREADABLE;

    /* Require a NUL within the run: an unterminated blob is not a C string,
     * and copying it out would hand the caller whatever followed it. */
    for (i = 0; i < run; i++)
        if (tmp[i] == '\0')
            break;
    if (i == run)
        return EMU_UNVERIFIED;
    if (i == 0)
        return EMU_NOT_RUNNING;

    if (i >= cap)
        i = cap - 1;
    memcpy(out, tmp, i);
    out[i] = '\0';
    return EMU_OK;
}

/* The 18 ROM records in table order, mapped to the canonical 0..7 game index
 * used by move_list.h and roster_table.h.
 *
 * ⭐ INDEX 0 IS cvs1 -- the array index IS the value rom_index_from_name()
 * returns. Confirmed against LIVE EVIDENCE, not reasoned from the listing: the
 * game reported index 3 while its own name string said 'cvs2u', and only the
 * 0-based reading reproduces that pairing.
 *
 * ⚠ The two numberings genuinely differ and the difference is not a formula:
 * z3uu/a3uu sit at ROM slots 16-17, far from z3u/a3u at 6-7, so "divide by two"
 * is right for the first sixteen and wrong for the last two. That is exactly
 * the shape of near-miss this project has shipped before (a 7-row list fitted a
 * formula 17/19 and was still wrong), so it is a TABLE, not arithmetic.
 *
 * game: 0 CVS1  1 CVS2  2 CFJ  3 Z3U  4 PWS1  5 PWS2  6 MJUS  7 STG2 */
static const struct { const char *name; int game; int rev; } g_rom_map[18] = {
    /*  0 */ { "cvs1",  0, 0 },  /*  1 */ { "cvs1u", 0, 1 },
    /*  2 */ { "cvs2",  1, 0 },  /*  3 */ { "cvs2u", 1, 1 },
    /*  4 */ { "cfj",   2, 0 },  /*  5 */ { "cfe",   2, 1 },
    /*  6 */ { "z3u",   3, 0 },  /*  7 */ { "a3u",   3, 1 },
    /*  8 */ { "pws1",  4, 0 },  /*  9 */ { "pws1u", 4, 1 },
    /* 10 */ { "pws2",  5, 0 },  /* 11 */ { "pws2u", 5, 1 },
    /* 12 */ { "mjus",  6, 0 },  /* 13 */ { "pjus",  6, 1 },
    /* 14 */ { "stg2",  7, 0 },  /* 15 */ { "pswd",  7, 1 },
    /* 16 */ { "z3uu",  3, 2 },  /* 17 */ { "a3uu",  3, 3 },
};

BOOL emu_game_from_rom_index(int rom_index, int *game_out, int *rev_out)
{
    if (game_out) *game_out = -1;
    if (rev_out)  *rev_out  = -1;

    if (rom_index < 0 || rom_index > 17)
        return FALSE;

    if (game_out) *game_out = g_rom_map[rom_index].game;
    if (rev_out)  *rev_out  = g_rom_map[rom_index].rev;
    return TRUE;
}

const char *emu_rom_name_for_index(int rom_index)
{
    if (rom_index < 0 || rom_index > 17)
        return NULL;
    return g_rom_map[rom_index].name;
}

/* --- Finding guest RAM inside the arena ---------------------------------- */

/* Published CVS2 (NAOMI) prior art. Used as a SHAPE to verify, never as an
 * address: these are Flycast guest addresses and CFC2 rebases guest RAM into
 * its own arena. What survives rebasing is the offsets WITHIN the struct and
 * the stride BETWEEN the two players. */
#define CVS2_STRIDE        0x5CCu     /* P2 = P1 + this                     */
#define CVS2_HP_OFF        0x00u      /* health, u16, at the struct base    */
#define CVS2_MAXHP_OFF     0x04u      /* max_health, u16                    */
#define CVS2_GUEST_P1      0x2419A8u  /* P1 health's offset in guest RAM    */

/* A page at a time, so a 256 MB sweep costs ~65k reads rather than 65 million.
 * ⭐ A per-item VirtualQuery is never affordable at scale -- every scanner in
 * this project has hit that wall, and three had to be rewritten for it. */
#define SCAN_PAGE 0x1000u

static ULONGLONG g_win_base;    /* test override; 0 = derive from the game */
static SIZE_T    g_win_size;

void emu_ram_test_window(ULONGLONG base, SIZE_T size)
{
    g_win_base = base;
    g_win_size = size;
}

/* Does a player pair live at `p1` within the buffer starting at `buf_addr`?
 * Pure arithmetic on already-copied bytes: no foreign reads happen here, so it
 * cannot fault and needs no guard of its own. */
static BOOL pair_matches(const unsigned char *buf, SIZE_T buflen,
                         SIZE_T off, unsigned *hp_out, unsigned *max_out)
{
    unsigned hp1, mx1, hp2, mx2;
    SIZE_T p2 = off + CVS2_STRIDE;

    /* Both structs must lie wholly inside the buffer, or we would be reading
     * a neighbour's bytes and calling the result a match. */
    if (p2 + CVS2_MAXHP_OFF + 2 > buflen)
        return FALSE;

    hp1 = (unsigned)(buf[off + CVS2_HP_OFF] | (buf[off + CVS2_HP_OFF + 1] << 8));
    mx1 = (unsigned)(buf[off + CVS2_MAXHP_OFF] | (buf[off + CVS2_MAXHP_OFF + 1] << 8));
    hp2 = (unsigned)(buf[p2 + CVS2_HP_OFF] | (buf[p2 + CVS2_HP_OFF + 1] << 8));
    mx2 = (unsigned)(buf[p2 + CVS2_MAXHP_OFF] | (buf[p2 + CVS2_MAXHP_OFF + 1] << 8));

    /* ⛔ The all-zero case satisfies "health <= max_health" trivially and is
     * the single most abundant pattern in a 256 MB arena. Requiring a nonzero,
     * shared, plausible cap is what makes this a signature rather than a
     * tautology -- a filter every look-alike passes finds only look-alikes. */
    if (mx1 == 0 || mx1 != mx2)
        return FALSE;
    if (mx1 > 0x2000)              /* a health cap, not a pointer or a count */
        return FALSE;
    if (hp1 > mx1 || hp2 > mx2)
        return FALSE;

    /* ⛔⛔ BOTH PLAYERS AT ZERO HEALTH IS NOT A LIVE MATCH, and the offline
     * suite caught this: a decoy planted 4 bytes away still matched, because
     * reading one dword LOW lands a zero in the health slot and a real value
     * in the cap slot -- self-consistent, and meaningless.
     *
     * In a live match at least one fighter has health above zero (a double KO
     * is a single frame, and we are not scanning for that). Requiring it costs
     * nothing real and removes an enormous class of zero-adjacent coincidences
     * that a 256 MB arena supplies in abundance.
     *
     * ⚠ This is also why the fixture's OWN misalignment mattered: if a decoy
     * four bytes off the plant can satisfy the signature, so can any of the
     * millions of near-zero dwords in the real arena. */
    if (hp1 == 0 && hp2 == 0)
        return FALSE;

    if (hp_out)  *hp_out  = hp1;
    if (max_out) *max_out = mx1;
    return TRUE;
}

emu_status emu_ram_find(emu_ram_hit *out, unsigned cap, unsigned *n_out,
                        SIZE_T *scanned_out)
{
    ULONGLONG base = 0, addr;
    SIZE_T size = 0, scanned = 0;
    unsigned n = 0;
    emu_status s;
    /* One page plus the tail of a pair, so a struct straddling a page boundary
     * is still seen whole. Without this overlap the scan has a blind seam
     * every 4 KB -- a false negative that looks exactly like a clean sweep. */
    static unsigned char buf[SCAN_PAGE + CVS2_STRIDE + 8];

    if (n_out)       *n_out = 0;
    if (scanned_out) *scanned_out = 0;

    if (g_win_base) {
        base = g_win_base;
        size = g_win_size;
    } else {
        s = emu_arena(&base, &size);
        if (s != EMU_OK)
            return s;
        /* ⚠ VirtualQuery reported 511 MB for a 256 MB request, so the region
         * is NOT our allocation. Bound the sweep by what the constructor
         * actually asked for; scanning a neighbour's memory would produce
         * matches that belong to something else entirely. */
        if (size > (SIZE_T)EMU_ARENA_SIZE)
            size = (SIZE_T)EMU_ARENA_SIZE;
    }

    if (size == 0)
        return EMU_UNREADABLE;

    for (addr = base; addr + CVS2_STRIDE + 8 < base + size; addr += SCAN_PAGE) {
        SIZE_T want = SCAN_PAGE + CVS2_STRIDE + 8;
        SIZE_T run, off;

        if (addr + want > base + size)
            want = (SIZE_T)(base + size - addr);

        /* ⛔ mem_readable_run() FIRST, then the guarded copy. The fault guard
         * is a net for the race between the two, never a replacement for the
         * check -- a guarded memcpy over a bad page is not reliably caught,
         * and this project has the crash that proves it. */
        run = mem_readable_run((const void *)(ULONG_PTR)addr, want);
        if (run < CVS2_STRIDE + 8)
            continue;                  /* a hole; the next page may be fine */

        if (!mem_copy(buf, (const void *)(ULONG_PTR)addr, run))
            continue;

        scanned += run;

        /* Step 4 bytes: the player struct is dword-aligned in every published
         * layout, and a byte-wise walk costs 4x for no extra coverage of a
         * naturally aligned field. */
        for (off = 0; off + CVS2_STRIDE + 8 <= run; off += 4) {
            unsigned hp = 0, mx = 0;
            if (!pair_matches(buf, run, off, &hp, &mx))
                continue;

            if (out && n < cap) {
                out[n].p1         = addr + off;
                out[n].health     = hp;
                out[n].max_health = mx;
                /* INFERRED, and labelled as such in the header: if this really
                 * is P1's health, guest RAM starts this far below it. */
                out[n].guest_base = (addr + off) - CVS2_GUEST_P1;
            }
            n++;
            if (n >= cap && out)
                break;
        }
        if (out && n >= cap)
            break;
    }

    if (n_out)       *n_out = n;
    if (scanned_out) *scanned_out = scanned;

    /* ⚠ A sweep that examined nothing is not a negative. Saying so is a RETURN
     * VALUE, not a logged warning, so a caller cannot mistake "never looked"
     * for "looked and found none". */
    if (scanned == 0)
        return EMU_UNREADABLE;

    return EMU_OK;
}
