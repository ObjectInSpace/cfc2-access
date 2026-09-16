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

    /* rom_index_from_name() returns 0 when the name matched nothing, and the
     * table has exactly 18 entries. Anything else is not an index this field
     * can legitimately hold. ⛔ Do NOT clamp: a clamped id would name a
     * different game's characters, confidently and wrongly. */
    if (v == 0)
        return EMU_NOT_RUNNING;
    if (v > 18)
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
 * ⚠ The two numberings genuinely differ and the difference is not a formula:
 * z3uu/a3uu sit at ROM slots 17-18, far from z3u/a3u at 7-8, so "divide by two"
 * is right for the first sixteen and wrong for the last two. That is exactly
 * the shape of near-miss this project has shipped before (a 7-row list fitted a
 * formula 17/19 and was still wrong), so it is a TABLE, not arithmetic.
 *
 * game: 0 CVS1  1 CVS2  2 CFJ  3 Z3U  4 PWS1  5 PWS2  6 MJUS  7 STG2 */
static const struct { const char *name; int game; int rev; } g_rom_map[18] = {
    /*  1 */ { "cvs1",  0, 0 },  /*  2 */ { "cvs1u", 0, 1 },
    /*  3 */ { "cvs2",  1, 0 },  /*  4 */ { "cvs2u", 1, 1 },
    /*  5 */ { "cfj",   2, 0 },  /*  6 */ { "cfe",   2, 1 },
    /*  7 */ { "z3u",   3, 0 },  /*  8 */ { "a3u",   3, 1 },
    /*  9 */ { "pws1",  4, 0 },  /* 10 */ { "pws1u", 4, 1 },
    /* 11 */ { "pws2",  5, 0 },  /* 12 */ { "pws2u", 5, 1 },
    /* 13 */ { "mjus",  6, 0 },  /* 14 */ { "pjus",  6, 1 },
    /* 15 */ { "stg2",  7, 0 },  /* 16 */ { "pswd",  7, 1 },
    /* 17 */ { "z3uu",  3, 2 },  /* 18 */ { "a3uu",  3, 3 },
};

BOOL emu_game_from_rom_index(int rom_index, int *game_out, int *rev_out)
{
    if (game_out) *game_out = -1;
    if (rev_out)  *rev_out  = -1;

    if (rom_index < 1 || rom_index > 18)
        return FALSE;

    if (game_out) *game_out = g_rom_map[rom_index - 1].game;
    if (rev_out)  *rev_out  = g_rom_map[rom_index - 1].rev;
    return TRUE;
}

const char *emu_rom_name_for_index(int rom_index)
{
    if (rom_index < 1 || rom_index > 18)
        return NULL;
    return g_rom_map[rom_index - 1].name;
}
