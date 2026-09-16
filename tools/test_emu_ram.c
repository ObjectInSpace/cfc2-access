/*
 * Offline self-test for the emulator-state reader (src/emu_ram.c).
 *
 * ⭐⭐ WHAT THIS IS PROTECTING
 * Everything in-game hangs off one two-hop pointer path. Three things can go
 * wrong and only the first is obvious:
 *   - the read fails                      (obvious: nothing happens);
 *   - "could not look" is reported as "looked and found nothing"
 *     (NOT obvious: a confident negative that was never earned);
 *   - a bad pointer is passed upward as a live object
 *     (NOT obvious at all: we then read a stranger's memory and, downstream,
 *      speak another game's character names).
 * Most cases below exist for the second and third kinds.
 *
 * ⚠ The state object is planted synthetically -- the real one lives at
 * 0x141c14f98 inside a running game. What is tested is the PARSING, the status
 * discrimination and the index->game mapping, never the real addresses. That
 * the real anchor is correct is an in-game claim and is labelled as such.
 */

#include "../src/emu_ram.h"
#include "../src/memsafe.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;

static void check(const char *name, int cond, const char *fmt, ...)
{
    if (cond) { printf("  PASS  %s\n", name); return; }
    printf("  FAIL  %s", name);
    if (fmt && *fmt) {
        va_list ap; va_start(ap, fmt);
        printf("  "); vprintf(fmt, ap); va_end(ap);
    }
    printf("\n");
    failures++;
}

/* --- The fixture ---------------------------------------------------------
 * A synthetic emulator state object with the same field layout as the game's,
 * plus a slot holding a pointer to it (standing in for 0x141c14f98) and a
 * page-aligned "arena". */
static ULONGLONG  g_slot;          /* stands in for EMU_STATE_SLOT */
static char      *g_state;         /* the state object             */
static void      *g_arena;         /* page-aligned allocation      */
static char       g_name[16];

#define STATE_SIZE 0x100

static void put_q(char *p, unsigned off, ULONGLONG v) { memcpy(p + off, &v, 8); }
static void put_d(char *p, unsigned off, unsigned  v) { memcpy(p + off, &v, 4); }

static void build_state(const char *rom_name, unsigned rom_index)
{
    memset(g_state, 0, STATE_SIZE);
    _snprintf(g_name, sizeof(g_name), "%s", rom_name);

    put_q(g_state, EMU_OFF_ARENA,     (ULONGLONG)(ULONG_PTR)g_arena);
    put_q(g_state, EMU_OFF_ROM_NAME,  (ULONGLONG)(ULONG_PTR)g_name);
    put_d(g_state, EMU_OFF_ROM_INDEX, rom_index);

    g_slot = (ULONGLONG)(ULONG_PTR)g_state;
    emu_ram_test_redirect((ULONGLONG)(ULONG_PTR)&g_slot);
}

int main(void)
{
    ULONGLONG st = 0, base = 0;
    SIZE_T size = 0;
    emu_status s;
    int idx = 0, game = -1, rev = -1;
    char buf[32];

    printf("emu_ram self-test\n");

    if (!mem_guard_install()) {
        printf("  FAIL  fault guard did not self-test\n");
        return 1;
    }

    /* A real page-aligned allocation, so the alignment check is exercised
     * against genuine memory rather than a number we chose. */
    g_arena = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_state = (char *)VirtualAlloc(NULL, STATE_SIZE, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_READWRITE);
    if (!g_arena || !g_state) { printf("  FAIL  fixture alloc\n"); return 1; }

    /* ---- 1. the happy path ------------------------------------------- */
    build_state("cvs2", 3);

    s = emu_state(&st);
    check("1a state resolves", s == EMU_OK && st == (ULONGLONG)(ULONG_PTR)g_state,
          "got %s st=%llx", emu_status_name(s), (unsigned long long)st);

    s = emu_arena(&base, &size);
    check("1b arena resolves, page aligned",
          s == EMU_OK && base == (ULONGLONG)(ULONG_PTR)g_arena && (base & 0xFFF) == 0,
          "got %s base=%llx", emu_status_name(s), (unsigned long long)base);

    s = emu_rom_index(&idx);
    check("1c rom index reads 3", s == EMU_OK && idx == 3,
          "got %s idx=%d", emu_status_name(s), idx);

    s = emu_rom_name(buf, sizeof(buf));
    check("1d rom name reads cvs2", s == EMU_OK && strcmp(buf, "cvs2") == 0,
          "got %s '%s'", emu_status_name(s), buf);

    /* ---- 2. NOT_RUNNING is a real answer, distinct from UNREADABLE ----
     * ⭐⭐ This is the discriminating pair. A tool that cannot tell "no game
     * is loaded" from "I could not look" will eventually report a negative it
     * never earned. Both directions are asserted, because a status function
     * that returned UNREADABLE for everything would sail through case 2a
     * alone. */
    g_slot = 0;
    s = emu_state(&st);
    check("2a null slot -> NOT_RUNNING", s == EMU_NOT_RUNNING,
          "got %s", emu_status_name(s));

    emu_ram_test_redirect(0x10);          /* an address that cannot be read */
    s = emu_state(&st);
    check("2b unreadable slot -> UNREADABLE (NOT a clean negative)",
          s == EMU_UNREADABLE, "got %s", emu_status_name(s));

    /* ---- 3. a non-null pointer that leads nowhere must NOT pass ------- */
    g_slot = 0xDEAD0000ULL;               /* non-null, not mapped */
    emu_ram_test_redirect((ULONGLONG)(ULONG_PTR)&g_slot);
    s = emu_state(&st);
    check("3a garbage state pointer -> UNVERIFIED, not OK",
          s == EMU_UNVERIFIED, "got %s", emu_status_name(s));

    /* ---- 4. arena sanity --------------------------------------------- */
    build_state("cvs2", 3);
    put_q(g_state, EMU_OFF_ARENA, (ULONGLONG)(ULONG_PTR)g_arena + 1);  /* misaligned */
    s = emu_arena(&base, &size);
    check("4a misaligned arena -> UNVERIFIED",
          s == EMU_UNVERIFIED, "got %s", emu_status_name(s));

    build_state("cvs2", 3);
    put_q(g_state, EMU_OFF_ARENA, 0);
    s = emu_arena(&base, &size);
    check("4b null arena -> NOT_RUNNING", s == EMU_NOT_RUNNING,
          "got %s", emu_status_name(s));

    /* ---- 5. the ROM index must be BOUNDED, never clamped -------------
     * ⛔ A clamped id names a different game's characters -- the worst
     * failure class in this project (a confidently wrong name). */
    build_state("cvs2", 19);
    s = emu_rom_index(&idx);
    check("5a index 19 (>18) -> UNVERIFIED, not clamped to 18",
          s == EMU_UNVERIFIED && idx != 18,
          "got %s idx=%d", emu_status_name(s), idx);

    build_state("cvs2", 0);
    s = emu_rom_index(&idx);
    check("5b index 0 -> NOT_RUNNING", s == EMU_NOT_RUNNING,
          "got %s", emu_status_name(s));

    build_state("cvs2", 18);
    s = emu_rom_index(&idx);
    check("5c index 18 (the last valid one) -> OK",
          s == EMU_OK && idx == 18, "got %s idx=%d", emu_status_name(s), idx);

    /* ---- 6. an unterminated name must be refused ----------------------
     * ⚠⚠ THE FIRST VERSION OF THIS CASE WAS BLIND, and the suite caught it.
     * It memset a 16-byte static array with no NUL -- but that array sits
     * inside a much larger readable page, so mem_readable_run() legitimately
     * returned 32 bytes and the zero padding AFTER the array supplied a
     * terminator. The read "succeeded" and the case passed while testing
     * nothing.
     * The fix is to place the name so that the bytes following it are
     * genuinely UNREADABLE: at the very end of a committed page whose
     * successor is not mapped. Then "no NUL within the readable run" is a
     * real condition rather than an artefact of the fixture's padding. */
    {
        char *two = (char *)VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
        char *tail;
        check("6-setup reserved two pages", two != NULL, "");
        if (two) {
            /* Commit only the FIRST page; the second stays reserved/noaccess. */
            VirtualAlloc(two, 0x1000, MEM_COMMIT, PAGE_READWRITE);
            tail = two + 0x1000 - 8;          /* 8 bytes, then a wall */
            memset(tail, 'x', 8);

            build_state("cvs2", 3);
            put_q(g_state, EMU_OFF_ROM_NAME, (ULONGLONG)(ULONG_PTR)tail);

            s = emu_rom_name(buf, sizeof(buf));
            check("6a unterminated name (page wall after it) -> not OK",
                  s != EMU_OK, "got %s '%s'", emu_status_name(s), buf);

            /* Positive control on the SAME awkward placement: a name that
             * does terminate right before the wall must still read. Without
             * this, a function that refused every short run would pass 6a. */
            tail[3] = '\0';
            s = emu_rom_name(buf, sizeof(buf));
            check("6b terminated name at a page edge -> OK 'xxx'",
                  s == EMU_OK && strcmp(buf, "xxx") == 0,
                  "got %s '%s'", emu_status_name(s), buf);

            VirtualFree(two, 0, MEM_RELEASE);
        }
    }

    /* ---- 7. the ROM index -> game mapping ----------------------------
     * ⚠ The two numberings are NOT related by arithmetic: z3uu/a3uu sit at
     * ROM 17-18 but belong to game 3, while "divide by two" holds for the
     * first sixteen. A formula would pass 16 of 18 and be WRONG -- exactly
     * the near-miss shape this project has shipped before. */
    check("7a rom 3 (cvs2) -> game 1",
          emu_game_from_rom_index(3, &game, &rev) && game == 1 && rev == 0,
          "game=%d rev=%d", game, rev);
    check("7b rom 15 (stg2) -> game 7",
          emu_game_from_rom_index(15, &game, &rev) && game == 7 && rev == 0,
          "game=%d rev=%d", game, rev);
    check("7c rom 17 (z3uu) -> game 3, NOT game 8",
          emu_game_from_rom_index(17, &game, &rev) && game == 3 && rev == 2,
          "game=%d rev=%d", game, rev);
    check("7d rom 18 (a3uu) -> game 3",
          emu_game_from_rom_index(18, &game, &rev) && game == 3 && rev == 3,
          "game=%d rev=%d", game, rev);

    /* Out of range must REFUSE, not clamp. */
    game = rev = -99;
    check("7e rom 0 refused", !emu_game_from_rom_index(0, &game, &rev), "");
    game = rev = -99;
    check("7f rom 19 refused", !emu_game_from_rom_index(19, &game, &rev), "");

    /* Every valid index maps to a game in 0..7, and every game is reachable.
     * ⭐ This is the assertion that catches a table typo: checking a handful
     * of spot values leaves fourteen rows untested. */
    {
        int seen[8]; int i, ok = 1, all = 1;
        memset(seen, 0, sizeof(seen));
        for (i = 1; i <= 18; i++) {
            if (!emu_game_from_rom_index(i, &game, &rev) || game < 0 || game > 7)
                ok = 0;
            else
                seen[game]++;
        }
        for (i = 0; i < 8; i++)
            if (!seen[i]) all = 0;
        check("7g all 18 map into 0..7", ok, "");
        check("7h all 8 games are reachable", all, "");
        /* Z3U carries FOUR revisions; everything else exactly two. */
        check("7i game 3 (Z3U) has 4 revisions", seen[3] == 4, "got %d", seen[3]);
        check("7j game 1 (CVS2) has 2 revisions", seen[1] == 2, "got %d", seen[1]);
    }

    /* ---- 8. the name table agrees with the mapping -------------------- */
    check("8a name for rom 3 is cvs2",
          emu_rom_name_for_index(3) && strcmp(emu_rom_name_for_index(3), "cvs2") == 0, "");
    check("8b name for rom 17 is z3uu",
          emu_rom_name_for_index(17) && strcmp(emu_rom_name_for_index(17), "z3uu") == 0, "");
    check("8c out of range name is NULL", emu_rom_name_for_index(19) == NULL, "");

    /* ---- 9. the guard absorbed nothing unexpected --------------------
     * ⚠ A run that faulted is a PARTIAL run and must say so rather than
     * presenting itself as clean. */
    printf("\n  (fault guard absorbed %ld fault(s))\n", mem_guard_fault_count());

    printf("\n%s -- %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
