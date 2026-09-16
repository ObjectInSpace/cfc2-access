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

/* ⛔⛔⛔ THIS BLOCK SIZE, AND THE BUDGET BELOW, ARE LOAD-BEARING.
 *
 * The first full-arena sweep STALLED THE MOD FOR 114.6 SECONDS -- speech dead,
 * the game unusable. Cause: one mem_readable_run() (a VirtualQuery syscall)
 * per 4 KB page = ~65,000 syscalls per sweep, plus as many guarded copies.
 *
 * ⚠ THAT IS THE FOURTH TIME THIS PROJECT HAS HIT THE SAME WALL (gmd_find, the
 * heapdiff capture, the span scan, now this) and I wrote the comment warning
 * about it directly above the code that did it. A rule you can quote is not a
 * rule you have applied.
 *
 * Fix, the same one that worked the previous three times: ask the memory
 * manager ONCE PER REGION (a few hundred queries for the whole arena, not
 * 65,000), then read large blocks out of that region.
 *
 * ⭐ And a wall-clock BUDGET, because a budget cannot be wrong about the
 * machine it is on. A stall in the user's speech is a broken mod; a diagnostic
 * that reports PARTIAL is an inconvenience. */
#define SCAN_BLOCK     0x40000u   /* 256 KB per guarded read                */
#define SCAN_BUDGET_MS 25         /* per sweep; resumes where it left off   */

static ULONGLONG g_win_base;    /* test override; 0 = derive from the game */
static SIZE_T    g_win_size;

/* ⭐ Resume point: a budgeted sweep covers part of the arena and the NEXT
 * sweep continues from here, so full coverage is still reached -- just across
 * several beats instead of one 114-second stall. File scope so the test seam
 * can rewind it. */
static ULONGLONG g_resume;

void emu_ram_scan_rewind(void)
{
    g_resume = 0;
}

/* Deterministic stand-in for the wall-clock budget, for the offline suite. */
static unsigned g_block_limit;

void emu_ram_test_block_limit(unsigned blocks)
{
    g_block_limit = blocks;
}

/* ⚠ Also clears the resume point. Without this, a budgeted sweep's leftover
 * position would carry into the next fixture and a case would silently scan
 * only part of its own planted arena -- a false negative produced by test
 * order, which is among the hardest kinds to notice. */
void emu_ram_test_window(ULONGLONG base, SIZE_T size)
{
    g_win_base = base;
    g_win_size = size;
    emu_ram_scan_rewind();
}

/* Does a player pair live at `p1` within the buffer starting at `buf_addr`?
 * Pure arithmetic on already-copied bytes: no foreign reads happen here, so it
 * cannot fault and needs no guard of its own. */
/* Is the dword window around `off` just a repeating pattern? A uniform run
 * satisfies every relational test at every alignment, so it must be excluded
 * by CONTENT, not by arithmetic.
 *
 * ⛔⛔ THE FIRST LIVE RUN WAS ENTIRELY THIS. Sixteen "candidates" 4-12 bytes
 * apart, all reading hp 546 / 546, were one run of repeating 0x0222 words --
 * `mx != 0`, `mx1 == mx2`, `hp <= mx` and "not both zero" are ALL true of
 * 0x0222 forever, at any offset. The filter was not identifying fighters; it
 * was reporting that a byte pattern exists. */
static BOOL looks_uniform(const unsigned char *buf, SIZE_T off, SIZE_T buflen)
{
    unsigned i;
    if (off + 16 > buflen)
        return FALSE;
    /* Compare 2-byte words across a 16-byte window: if every word is identical
     * to the first, this is filler, a cleared block, or a repeating constant. */
    for (i = 2; i < 16; i += 2)
        if (buf[off + i] != buf[off] || buf[off + i + 1] != buf[off + 1])
            return FALSE;
    return TRUE;
}

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

    /* ⛔⛔ REJECT UNIFORM RUNS at either player. This is what the first live
     * run actually found: repeating 0x0222 words that satisfy every relational
     * clause above at every alignment. A relational filter cannot exclude a
     * constant -- only looking at the CONTENT can. */
    if (looks_uniform(buf, off, buflen) || looks_uniform(buf, p2, buflen))
        return FALSE;

    if (hp_out)  *hp_out  = hp1;
    if (max_out) *max_out = mx1;
    return TRUE;
}

emu_status emu_ram_find(emu_ram_hit *out, unsigned cap, unsigned *n_out,
                        SIZE_T *scanned_out)
{
    ULONGLONG base = 0, addr, end;
    SIZE_T size = 0, scanned = 0;
    unsigned n = 0;
    emu_status s;
    DWORD t0 = GetTickCount();
    unsigned blocks_done = 0;
    MEMORY_BASIC_INFORMATION mbi;
    ULONGLONG region_end = 0;
    /* One block plus the tail of a pair, so a struct straddling a block
     * boundary is still seen whole. Without this overlap the scan has a blind
     * seam every block -- a false negative that looks like a clean sweep. */
    static unsigned char buf[SCAN_BLOCK + CVS2_STRIDE + 8];

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

    end = base + size;

    /* Resume where the last budgeted sweep stopped; wrap when past the end. */
    addr = (g_resume >= base && g_resume < end) ? g_resume : base;

    for (; addr + CVS2_STRIDE + 8 < end; addr += SCAN_BLOCK) {
        SIZE_T want = SCAN_BLOCK + CVS2_STRIDE + 8;
        SIZE_T run, off;

        /* ⭐⭐ THE BUDGET. Checked in the INNERMOST loop that can run long --
         * an earlier scanner in this project put its deadline in an outer loop
         * over regions, which could not fire for minutes and voided the very
         * guarantee it existed to provide. */
        if (g_block_limit ? (blocks_done >= g_block_limit)
                          : (GetTickCount() - t0 >= SCAN_BUDGET_MS)) {
            g_resume = addr;           /* continue here next sweep */
            break;
        }
        blocks_done++;

        if (addr + want > end)
            want = (SIZE_T)(end - addr);

        /* ⭐⭐ ONE VirtualQuery PER REGION, not per block. This is the fix for
         * the 114-second stall: the arena is a handful of regions, so this
         * runs a few hundred times per full sweep rather than 65,000. */
        if (addr >= region_end) {
            if (VirtualQuery((LPCVOID)(ULONG_PTR)addr, &mbi, sizeof(mbi))
                    != sizeof(mbi)) {
                region_end = addr + SCAN_BLOCK;   /* step over and continue */
                continue;
            }
            region_end = (ULONGLONG)(ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
            if (mbi.State != MEM_COMMIT ||
                (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                /* Skip the whole unreadable region in one step. */
                addr = region_end - SCAN_BLOCK;   /* loop adds SCAN_BLOCK */
                continue;
            }
        }

        /* Do not read past the end of the region we just validated. */
        if (addr + want > region_end)
            want = (SIZE_T)(region_end - addr);
        if (want < CVS2_STRIDE + 8)
            continue;

        /* ⛔ The region query above establishes the range; the fault guard is
         * the NET for the race between that check and this copy, never a
         * replacement for it. A guarded memcpy over a bad page is not
         * reliably caught -- this project has the crash that proves it. */
        run = want;
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

            /* ⛔⛔ THE CAP MUST NOT STOP THE SWEEP. The first live run filled
             * 16 slots within ~0.5 MB and BROKE, leaving 99.8% of the arena
             * unexamined -- and then reported the truncated result as if it
             * were the answer. A cap on what you KEEP is fine; a cap that ends
             * the SEARCH turns every negative into "I stopped early", which is
             * indistinguishable from "it is not there".
             * See budget-cap-what-you-encounter and
             * sampling-gate-causes-false-negatives. */
            if (out && n < cap) {
                out[n].p1         = addr + off;
                out[n].health     = hp;
                out[n].max_health = mx;
                /* INFERRED, and labelled as such in the header: if this really
                 * is P1's health, guest RAM starts this far below it. */
                out[n].guest_base = (addr + off) - CVS2_GUEST_P1;
            }
            n++;              /* counts every ENCOUNTER, kept or not */

            /* ⭐ Skip past a matched pair rather than re-testing the next
             * dword. A uniform region (the live run hit a run of repeating
             * 0x0222) otherwise matches at EVERY alignment and manufactures a
             * cluster of "candidates" that are one region seen many times. */
            off += CVS2_STRIDE;
        }
    }

    /* Ran to the end without exhausting the budget -- start over next sweep so
     * the arena is re-examined as the match progresses. */
    if (addr + CVS2_STRIDE + 8 >= end)
        g_resume = base;

    if (n_out)       *n_out = n;
    if (scanned_out) *scanned_out = scanned;

    /* ⚠ A sweep that examined nothing is not a negative. Saying so is a RETURN
     * VALUE, not a logged warning, so a caller cannot mistake "never looked"
     * for "looked and found none". */
    if (scanned == 0)
        return EMU_UNREADABLE;

    return EMU_OK;
}

/* --- Watching candidates over time --------------------------------------
 *
 * ⭐⭐ This is what actually decides it, and it needs no special game mode.
 * A static sweep can only ask "does this fit the arithmetic?", which filler
 * answers yes to. Watching asks "does it BEHAVE like health?", which filler
 * cannot fake: divergence between the players, downward steps as hits land,
 * and a jump back to the cap when a round ends.
 *
 * ⚠ Keyed by ADDRESS and kept even after a candidate stops matching -- a pair
 * that vanishes from the sweep still has a history worth reporting, and
 * dropping it would silently discard the most interesting case (a struct that
 * is only valid mid-round). */
#define WATCH_MAX 64
static emu_watch g_watch[WATCH_MAX];
static unsigned  g_watch_n;

void emu_watch_reset(void)
{
    memset(g_watch, 0, sizeof(g_watch));
    g_watch_n = 0;
}

static emu_watch *watch_find(ULONGLONG p1)
{
    unsigned i;
    for (i = 0; i < g_watch_n; i++)
        if (g_watch[i].p1 == p1)
            return &g_watch[i];
    return NULL;
}

unsigned emu_watch_sample(const emu_ram_hit *hits, unsigned n)
{
    unsigned i;

    /* Adopt new candidates, up to the table's capacity. */
    for (i = 0; i < n && g_watch_n < WATCH_MAX; i++) {
        if (!watch_find(hits[i].p1)) {
            emu_watch *w = &g_watch[g_watch_n++];
            memset(w, 0, sizeof(*w));
            w->p1         = hits[i].p1;
            w->max_health = hits[i].max_health;
            w->last_h1    = hits[i].health;
        }
    }

    /* Re-read every tracked address, including ones not in this sweep. */
    for (i = 0; i < g_watch_n; i++) {
        emu_watch *w = &g_watch[i];
        unsigned char b[CVS2_STRIDE + 8];
        unsigned h1, h2;

        if (mem_readable_run((const void *)(ULONG_PTR)w->p1, sizeof(b)) < sizeof(b))
            continue;
        if (!mem_copy(b, (const void *)(ULONG_PTR)w->p1, sizeof(b)))
            continue;

        h1 = (unsigned)(b[CVS2_HP_OFF] | (b[CVS2_HP_OFF + 1] << 8));
        h2 = (unsigned)(b[CVS2_STRIDE + CVS2_HP_OFF] |
                        (b[CVS2_STRIDE + CVS2_HP_OFF + 1] << 8));

        w->samples++;
        if (h1 != h2)
            w->diverged++;

        if (w->samples > 1 && h1 != w->last_h1) {
            w->changes++;
            if (h1 < w->last_h1)
                w->drops++;
            /* A jump back UP to (or near) the cap is a round reset -- the
             * reversibility test, supplied by the game rather than by asking
             * the player to do anything. */
            else if (w->max_health && h1 >= w->max_health)
                w->resets++;
        }
        w->last_h1 = h1;
        w->last_h2 = h2;
    }

    return g_watch_n;
}

/* Rank: behaving like health beats merely fitting the arithmetic. */
static int watch_score(const emu_watch *w)
{
    int s = 0;
    if (w->drops)    s += 4;
    if (w->resets)   s += 4;   /* the strongest single signal */
    if (w->diverged) s += 2;
    if (w->changes)  s += 1;
    return s;
}

unsigned emu_watch_report(emu_watch *out, unsigned cap)
{
    unsigned i, j, n = 0;
    if (!out || cap == 0)
        return 0;

    for (i = 0; i < g_watch_n && n < cap; i++)
        out[n++] = g_watch[i];

    /* Insertion sort, best first. n is <= 64, so the cost is irrelevant. */
    for (i = 1; i < n; i++) {
        emu_watch key = out[i];
        j = i;
        while (j > 0 && watch_score(&out[j - 1]) < watch_score(&key)) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
    return n;
}
