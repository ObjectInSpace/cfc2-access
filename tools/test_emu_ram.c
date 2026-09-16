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

/* The stride the scanner looks for. ⚠ Declared here rather than exported from
 * emu_ram.h on purpose: if the implementation's constant ever changes, this
 * fixture keeps planting at the OLD stride and cases 10b/10d fail loudly. A
 * shared constant would make the test agree with the code by construction and
 * prove nothing -- the harness must not supply the mechanism under test. */
#define CVS2_STRIDE_T 0x5CCu

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

    /* ---- 1. the happy path -------------------------------------------
     * ⭐⭐ THE FIXTURE IS THE REAL OBSERVATION. The first live run reported
     * index 3 with the name string 'cvs2u', which is what proved the index is
     * 0-BASED (1-based would make 3 mean 'cvs2'). Using those exact values
     * here means the regression is pinned to evidence rather than to my
     * reading of the disassembly, which was wrong once already. */
    build_state("cvs2u", 3);

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
    check("1d rom name reads cvs2u", s == EMU_OK && strcmp(buf, "cvs2u") == 0,
          "got %s '%s'", emu_status_name(s), buf);

    /* ⭐⭐ THE LIVE CROSS-CHECK, as a case. This is the exact pairing the game
     * produced, and it is what a 1-based table gets WRONG. */
    check("1e index 3 and the live name 'cvs2u' AGREE (the 2026-09-16 run)",
          emu_rom_name_for_index(3) &&
          strcmp(emu_rom_name_for_index(3), "cvs2u") == 0,
          "index 3 means '%s'", emu_rom_name_for_index(3) ? emu_rom_name_for_index(3) : "?");

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
    build_state("cvs2u", 3);
    put_q(g_state, EMU_OFF_ARENA, (ULONGLONG)(ULONG_PTR)g_arena + 1);  /* misaligned */
    s = emu_arena(&base, &size);
    check("4a misaligned arena -> UNVERIFIED",
          s == EMU_UNVERIFIED, "got %s", emu_status_name(s));

    build_state("cvs2u", 3);
    put_q(g_state, EMU_OFF_ARENA, 0);
    s = emu_arena(&base, &size);
    check("4b null arena -> NOT_RUNNING", s == EMU_NOT_RUNNING,
          "got %s", emu_status_name(s));

    /* ---- 5. the ROM index must be BOUNDED, never clamped -------------
     * ⛔ A clamped id names a different game's characters -- the worst
     * failure class in this project (a confidently wrong name). */
    build_state("cvs1", 18);
    s = emu_rom_index(&idx);
    check("5a index 18 (>17) -> UNVERIFIED, not clamped to 17",
          s == EMU_UNVERIFIED && idx != 17,
          "got %s idx=%d", emu_status_name(s), idx);

    /* ⛔⛔ 0 IS A VALID GAME (cvs1), NOT "no game loaded". The first version
     * returned NOT_RUNNING here, which would have made the mod silent for
     * exactly one game -- invisible unless someone tested CVS1 specifically,
     * and indistinguishable from "no game" when they did. The real "no game"
     * signal is a null state slot, covered by case 2a. */
    build_state("cvs1", 0);
    s = emu_rom_index(&idx);
    check("5b index 0 is VALID (cvs1), not NOT_RUNNING",
          s == EMU_OK && idx == 0, "got %s idx=%d", emu_status_name(s), idx);

    build_state("a3uu", 17);
    s = emu_rom_index(&idx);
    check("5c index 17 (the last valid one) -> OK",
          s == EMU_OK && idx == 17, "got %s idx=%d", emu_status_name(s), idx);

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

            build_state("cvs2u", 3);
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
    check("7a rom 3 (cvs2u) -> game 1 rev 1  [the live pairing]",
          emu_game_from_rom_index(3, &game, &rev) && game == 1 && rev == 1,
          "game=%d rev=%d", game, rev);
    check("7b rom 0 (cvs1) -> game 0, and is ACCEPTED",
          emu_game_from_rom_index(0, &game, &rev) && game == 0 && rev == 0,
          "game=%d rev=%d", game, rev);
    check("7c rom 16 (z3uu) -> game 3, NOT game 8",
          emu_game_from_rom_index(16, &game, &rev) && game == 3 && rev == 2,
          "game=%d rev=%d", game, rev);
    check("7d rom 17 (a3uu) -> game 3",
          emu_game_from_rom_index(17, &game, &rev) && game == 3 && rev == 3,
          "game=%d rev=%d", game, rev);

    /* Out of range must REFUSE, not clamp. ⚠ Note 0 is NO LONGER out of
     * range -- the negative bound is -1 now. */
    game = rev = -99;
    check("7e rom -1 refused", !emu_game_from_rom_index(-1, &game, &rev), "");
    game = rev = -99;
    check("7f rom 18 refused", !emu_game_from_rom_index(18, &game, &rev), "");

    /* Every valid index maps to a game in 0..7, and every game is reachable.
     * ⭐ This is the assertion that catches a table typo: checking a handful
     * of spot values leaves fourteen rows untested. */
    {
        int seen[8]; int i, ok = 1, all = 1;
        memset(seen, 0, sizeof(seen));
        for (i = 0; i <= 17; i++) {
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
    check("8a name for rom 3 is cvs2u",
          emu_rom_name_for_index(3) && strcmp(emu_rom_name_for_index(3), "cvs2u") == 0, "");
    check("8b name for rom 16 is z3uu",
          emu_rom_name_for_index(16) && strcmp(emu_rom_name_for_index(16), "z3uu") == 0, "");
    check("8c name for rom 0 is cvs1 (NOT NULL -- 0 is valid)",
          emu_rom_name_for_index(0) && strcmp(emu_rom_name_for_index(0), "cvs1") == 0, "");
    check("8d out of range name is NULL", emu_rom_name_for_index(18) == NULL, "");

    /* ---- 10. the guest-RAM signature scan ----------------------------
     * ⭐⭐ A planted arena, because the real one needs a running match. What
     * is tested is that the SIGNATURE discriminates -- that it finds a real
     * player pair and REFUSES each look-alike. A scanner proven only to find
     * its own plant is a scanner proven to find anything. */
    {
        #define ARENA_BYTES 0x8000u
        unsigned char *ar = (unsigned char *)VirtualAlloc(
            NULL, ARENA_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        emu_ram_hit hits[8];
        unsigned nh = 0;
        SIZE_T scanned = 0;

        check("10-setup arena allocated", ar != NULL, "");
        if (ar) {
            SIZE_T plant = 0x1400;      /* dword-aligned, mid-arena */
            memset(ar, 0, ARENA_BYTES);

            /* A real-looking pair: P1 hp 96 of 144, P2 hp 144 of 144. */
            ar[plant + 0] = 96;  ar[plant + 1] = 0;
            ar[plant + 4] = 144; ar[plant + 5] = 0;
            ar[plant + CVS2_STRIDE_T + 0] = 144; ar[plant + CVS2_STRIDE_T + 1] = 0;
            ar[plant + CVS2_STRIDE_T + 4] = 144; ar[plant + CVS2_STRIDE_T + 5] = 0;

            emu_ram_test_window((ULONGLONG)(ULONG_PTR)ar, ARENA_BYTES);
            s = emu_ram_find(hits, 8, &nh, &scanned);

            check("10a scan COMPLETES and reports bytes scanned",
                  s == EMU_OK && scanned > 0,
                  "got %s scanned=%llu", emu_status_name(s),
                  (unsigned long long)scanned);
            check("10b the planted pair is FOUND",
                  nh >= 1, "n=%u", nh);
            {
                unsigned i, found = 0;
                for (i = 0; i < nh && i < 8; i++)
                    if (hits[i].p1 == (ULONGLONG)(ULONG_PTR)(ar + plant)) {
                        found = 1;
                        check("10c the hit reports the right health values",
                              hits[i].health == 96 && hits[i].max_health == 144,
                              "hp=%u max=%u", hits[i].health, hits[i].max_health);
                    }
                check("10d the hit is at the planted address", found, "");
            }

            /* ---- the DECOYS. Each defeats exactly one clause; if any is
             * reported, that clause is not doing its job. ⭐ An all-zero
             * region satisfies "health <= max_health" trivially and is the
             * most abundant pattern in a real arena -- case 10e is the one
             * that matters most. */
            memset(ar, 0, ARENA_BYTES);
            emu_ram_test_window((ULONGLONG)(ULONG_PTR)ar, ARENA_BYTES);
            s = emu_ram_find(hits, 8, &nh, &scanned);
            check("10e an ALL-ZERO arena yields NO hits (not a tautology)",
                  s == EMU_OK && nh == 0, "got %s n=%u", emu_status_name(s), nh);

            memset(ar, 0, ARENA_BYTES);
            ar[plant + 0] = 200; ar[plant + 1] = 0;   /* hp > max */
            ar[plant + 4] = 144; ar[plant + 5] = 0;
            ar[plant + CVS2_STRIDE_T + 0] = 144; ar[plant + CVS2_STRIDE_T + 1] = 0;
            ar[plant + CVS2_STRIDE_T + 4] = 144; ar[plant + CVS2_STRIDE_T + 5] = 0;
            s = emu_ram_find(hits, 8, &nh, &scanned);
            check("10f health ABOVE max is refused", s == EMU_OK && nh == 0,
                  "n=%u", nh);

            memset(ar, 0, ARENA_BYTES);
            ar[plant + 0] = 96;  ar[plant + 4] = 144;
            ar[plant + CVS2_STRIDE_T + 0] = 96;
            ar[plant + CVS2_STRIDE_T + 4] = 100;      /* caps DISAGREE */
            s = emu_ram_find(hits, 8, &nh, &scanned);
            check("10g disagreeing max_health is refused", s == EMU_OK && nh == 0,
                  "n=%u", nh);

            memset(ar, 0, ARENA_BYTES);
            ar[plant + 0] = 0x10; ar[plant + 1] = 0x20;   /* cap far too big */
            ar[plant + 4] = 0x00; ar[plant + 5] = 0x40;
            ar[plant + CVS2_STRIDE_T + 0] = 0x10; ar[plant + CVS2_STRIDE_T + 1] = 0x20;
            ar[plant + CVS2_STRIDE_T + 4] = 0x00; ar[plant + CVS2_STRIDE_T + 5] = 0x40;
            s = emu_ram_find(hits, 8, &nh, &scanned);
            check("10h an implausibly large cap is refused",
                  s == EMU_OK && nh == 0, "n=%u", nh);

            /* ⭐⭐ A pair STRADDLING a page boundary must still be found. The
             * scan reads page+stride precisely so there is no blind seam every
             * 4 KB -- and a seam would be a false negative indistinguishable
             * from a clean sweep. */
            memset(ar, 0, ARENA_BYTES);
            {
                SIZE_T strad = 0x1000 - 8;   /* P1 just before the boundary */
                ar[strad + 0] = 96;  ar[strad + 4] = 144;
                ar[strad + CVS2_STRIDE_T + 0] = 144;
                ar[strad + CVS2_STRIDE_T + 4] = 144;
                s = emu_ram_find(hits, 8, &nh, &scanned);
                check("10i a pair STRADDLING a page boundary is found",
                      s == EMU_OK && nh >= 1, "got %s n=%u",
                      emu_status_name(s), nh);
            }

            /* ⛔⛔ 10l IS THE FIRST LIVE RUN, REPRODUCED. It reported 16
             * "candidates" 4-12 bytes apart, every one reading hp 546 / 546.
             * They were a run of repeating 0x0222 words: mx != 0, mx1 == mx2,
             * hp <= mx and "not both zero" are ALL true of 0x0222 at EVERY
             * alignment, forever. The filter was not finding fighters -- it
             * was reporting that a byte pattern exists.
             *
             * A relational filter cannot exclude a constant. Only looking at
             * the CONTENT can, which is what looks_uniform() does. */
            memset(ar, 0, ARENA_BYTES);
            {
                SIZE_T u;
                for (u = 0x800; u < 0x2400; u += 2) {
                    ar[u] = 0x22; ar[u + 1] = 0x02;    /* 546, repeating */
                }
                emu_ram_test_window((ULONGLONG)(ULONG_PTR)ar, ARENA_BYTES);
                s = emu_ram_find(hits, 8, &nh, &scanned);
                check("10l a repeating 0x0222 run yields NO hits "
                      "(the first live run's false positive)",
                      s == EMU_OK && nh == 0,
                      "got %s n=%u", emu_status_name(s), nh);
            }

            /* ⭐ Positive control for 10l: a REAL pair must still be found when
             * it sits next to filler. Without this, a scanner that rejected
             * everything would pass 10l and look fixed. */
            memset(ar, 0, ARENA_BYTES);
            {
                SIZE_T u;
                for (u = 0x800; u < 0x1000; u += 2) {
                    ar[u] = 0x22; ar[u + 1] = 0x02;
                }
                /* varied, non-uniform neighbourhood around each struct */
                for (u = 0; u < 16; u++) {
                    ar[plant + 8 + u]                 = (unsigned char)(u * 7 + 1);
                    ar[plant + CVS2_STRIDE_T + 8 + u] = (unsigned char)(u * 5 + 3);
                }
                ar[plant + 0] = 96;  ar[plant + 4] = 144;
                ar[plant + CVS2_STRIDE_T + 0] = 120;
                ar[plant + CVS2_STRIDE_T + 4] = 144;
                s = emu_ram_find(hits, 8, &nh, &scanned);
                check("10m a REAL pair beside filler is still found",
                      s == EMU_OK && nh >= 1, "got %s n=%u",
                      emu_status_name(s), nh);
            }

            /* ⭐⭐ THE SWEEP MUST NOT STOP AT THE CAP. Plant more distinct
             * pairs than `cap`, spread out, and require the reported COUNT to
             * exceed cap -- proving n counts encounters while the sweep ran on.
             * The live run filled 16 slots in 0.5 MB and quit, reporting the
             * truncation as the answer. */
            memset(ar, 0, ARENA_BYTES);
            {
                unsigned made = 0;
                SIZE_T q;
                for (q = 0x100; q + CVS2_STRIDE_T + 32 < ARENA_BYTES;
                     q += CVS2_STRIDE_T + 0x40) {
                    unsigned j;
                    for (j = 0; j < 16; j++) {
                        ar[q + 8 + j]                 = (unsigned char)(j * 7 + made + 1);
                        ar[q + CVS2_STRIDE_T + 8 + j] = (unsigned char)(j * 5 + made + 3);
                    }
                    ar[q + 0] = 90;  ar[q + 4] = 144;
                    ar[q + CVS2_STRIDE_T + 0] = 130;
                    ar[q + CVS2_STRIDE_T + 4] = 144;
                    made++;
                }
                s = emu_ram_find(hits, 4, &nh, &scanned);   /* cap of only 4 */
                check("10n the sweep RUNS ON past the cap (n counts encounters)",
                      s == EMU_OK && made > 4 && nh > 4,
                      "planted %u, cap 4, reported n=%u", made, nh);
            }

            /* ⭐⭐ 10o: ONE pair must be reported ONCE, not once per alignment.
             * The sabotage sweep caught this as untested: removing the
             * post-match `off += CVS2_STRIDE` skip left the whole suite green,
             * because every other case plants pairs whose neighbouring
             * alignments happen not to match.
             *
             * A non-uniform struct can still match at several nearby offsets
             * (the health/max words need only land plausibly). Without the
             * skip, one genuine pair inflates into a cluster -- which is
             * exactly how the live run's single region became "16 candidates",
             * and the count is what a reader judges the result by. */
            memset(ar, 0, ARENA_BYTES);
            {
                /* ⚠⚠ MY FIRST FIXTURE HERE COULD NOT DISCRIMINATE, and the
                 * sabotage proved it: it produced exactly ONE matching offset,
                 * so "reported once" and "reported per alignment" gave the
                 * identical answer and the case passed either way. The code
                 * was fine; the test was.
                 *
                 * This layout cycles hp/max word pairs so FOUR consecutive
                 * dword alignments each read as a valid, non-uniform pair.
                 * With the stride skip one hit is reported; without it, four.
                 * Verified by computing the matching offsets before writing
                 * the case, rather than assuming the bytes would behave. */
                static const unsigned short w[16] = {
                    60, 110, 70, 110,  62, 112, 72, 112,
                    64, 114, 74, 114,  66, 116, 76, 116
                };
                SIZE_T q = 0x1400;
                unsigned i;
                for (i = 0; i < 16; i++) {
                    ar[q + i * 2]     = (unsigned char)(w[i] & 0xFF);
                    ar[q + i * 2 + 1] = (unsigned char)(w[i] >> 8);
                    ar[q + CVS2_STRIDE_T + i * 2]     = (unsigned char)(w[i] & 0xFF);
                    ar[q + CVS2_STRIDE_T + i * 2 + 1] = (unsigned char)(w[i] >> 8);
                }
                s = emu_ram_find(hits, 8, &nh, &scanned);
                check("10o one region is reported ONCE, not once per alignment",
                      s == EMU_OK && nh <= 2,
                      "got %s n=%u (4 means the stride skip is gone)",
                      emu_status_name(s), nh);
            }

            /* A zero-size window means we never looked -- and that must be a
             * RETURN VALUE, not a count of zero that reads like a negative. */
            emu_ram_test_window((ULONGLONG)(ULONG_PTR)ar, 0);
            s = emu_ram_find(hits, 8, &nh, &scanned);
            check("10j a zero-size window -> UNREADABLE, not a clean zero",
                  s == EMU_UNREADABLE, "got %s n=%u", emu_status_name(s), nh);

            /* ⛔⛔ 10j WAS A BLIND TEST, and the sabotage sweep caught it.
             * Deleting the `scanned == 0 -> UNREADABLE` guard left the whole
             * suite GREEN, because 10j is satisfied by the earlier
             * `size == 0` early return and never reaches the guard at all.
             * A case that passes for a different reason than the one it names
             * tests nothing.
             *
             * This case reaches it: a window with a real SIZE but pointing at
             * memory that cannot be read. Every page is skipped, so the loop
             * completes having examined nothing -- "I could not look", which
             * must NOT be reportable as "I looked and found none".
             * See probe-zero-checked-looks-like-zero-failed. */
            emu_ram_test_window(0x10000ULL, 0x8000);   /* sized, unreadable */
            s = emu_ram_find(hits, 8, &nh, &scanned);
            check("10k a sized but UNREADABLE window -> UNREADABLE, scanned 0",
                  s == EMU_UNREADABLE && scanned == 0,
                  "got %s scanned=%llu n=%u", emu_status_name(s),
                  (unsigned long long)scanned, nh);

            emu_ram_test_window(0, 0);
            VirtualFree(ar, 0, MEM_RELEASE);
        }
    }

    /* ---- 12. the BUDGET and RESUMPTION -------------------------------
     * ⛔⛔ THE LIVE RUN STALLED THE MOD FOR 114.6 SECONDS. One full-arena
     * sweep did a VirtualQuery per 4 KB page -- ~65,000 syscalls -- and froze
     * the worker thread, so speech was dead for nearly two minutes. That is a
     * broken mod, not a slow diagnostic.
     *
     * The fix is a budget plus RESUMPTION, and resumption is the part that can
     * silently destroy coverage: if the resume point does not advance, the
     * scan re-reads one slice forever and everything past it is never
     * examined -- a false negative indistinguishable from a completed sweep.
     *
     * ⚠⚠ THESE CASES USE A DETERMINISTIC BLOCK LIMIT, NOT THE CLOCK. My first
     * version sized a 64 MB fixture so that "one 25 ms sweep" would not finish
     * it -- and the sabotage proved that was luck: pinning the resume point to
     * the base left the suite GREEN, because on this machine one sweep already
     * covered the whole fixture. A timing-dependent case reports a mechanism
     * as protected when it is not, which is worse than having no case. */
    {
        SIZE_T big = 0x40000u * 6u;           /* exactly 6 scan blocks */
        unsigned char *bar = (unsigned char *)VirtualAlloc(
            NULL, big, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        emu_ram_hit hits12[8];
        unsigned nh12 = 0;

        check("12-setup arena allocated", bar != NULL, "");
        if (bar) {
            SIZE_T late = big - 0x20000;      /* in the LAST block */
            unsigned sweeps, found_late = 0, j;
            SIZE_T sc = 0, first_sc = 0;

            for (j = 0; j < 16; j++) {
                bar[late + 8 + j]                 = (unsigned char)(j * 7 + 1);
                bar[late + CVS2_STRIDE_T + 8 + j] = (unsigned char)(j * 5 + 3);
            }
            bar[late + 0] = 96;  bar[late + 4] = 144;
            bar[late + CVS2_STRIDE_T + 0] = 120;
            bar[late + CVS2_STRIDE_T + 4] = 144;

            /* ONE block per sweep: six sweeps are needed to cross the arena,
             * on every machine, regardless of speed. */
            emu_ram_test_block_limit(1);
            emu_ram_test_window((ULONGLONG)(ULONG_PTR)bar, big);

            s = emu_ram_find(hits12, 8, &nh12, &first_sc);
            check("12a one budgeted sweep covers only PART of the arena",
                  s == EMU_OK && first_sc > 0 && first_sc < big,
                  "got %s scanned=%llu of %llu", emu_status_name(s),
                  (unsigned long long)first_sc, (unsigned long long)big);

            /* The planted pair is in the LAST block, so only a scan that
             * actually RESUMES can ever reach it. */
            for (sweeps = 0; sweeps < 12 && !found_late; sweeps++) {
                unsigned k;
                s = emu_ram_find(hits12, 8, &nh12, &sc);
                if (s != EMU_OK) break;
                for (k = 0; k < nh12 && k < 8; k++)
                    if (hits12[k].p1 == (ULONGLONG)(ULONG_PTR)(bar + late))
                        found_late = 1;
            }
            check("12b RESUMPTION reaches a pair in the LAST block",
                  found_late, "not found after %u further sweep(s)", sweeps);

            /* Rewind must actually rewind: the next sweep starts at the base
             * again, so with a one-block budget it cannot see the far pair. */
            emu_ram_scan_rewind();
            s = emu_ram_find(hits12, 8, &nh12, &sc);
            {
                unsigned k, saw = 0;
                for (k = 0; k < nh12 && k < 8; k++)
                    if (hits12[k].p1 == (ULONGLONG)(ULONG_PTR)(bar + late))
                        saw = 1;
                check("12c rewind restarts at the base (far pair not seen)",
                      s == EMU_OK && !saw, "got %s saw=%u",
                      emu_status_name(s), saw);
            }

            emu_ram_test_block_limit(0);
            emu_ram_test_window(0, 0);
            VirtualFree(bar, 0, MEM_RELEASE);
        }
    }

    /* ---- 11. the BEHAVIOUR watcher ----------------------------------
     * ⭐⭐ This is the half that actually identifies guest RAM, and it is why
     * a NORMAL MATCH is a sufficient instrument -- no training mode needed.
     * A static sweep can only ask "does this fit the arithmetic?", which
     * filler answers yes to. The watcher asks "does it BEHAVE like health?" */
    {
        unsigned char *ar2 = (unsigned char *)VirtualAlloc(
            NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        emu_watch wr[8];
        unsigned nw;
        emu_ram_hit fake[2];

        check("11-setup arena allocated", ar2 != NULL, "");
        if (ar2) {
            SIZE_T a = 0x100;                    /* the "real" pair   */
            SIZE_T b = 0x2000;                   /* the filler pair   */
            ULONGLONG addr_a = (ULONGLONG)(ULONG_PTR)(ar2 + a);
            ULONGLONG addr_b = (ULONGLONG)(ULONG_PTR)(ar2 + b);
            unsigned i;

            emu_watch_reset();
            memset(ar2, 0, 0x4000);

            /* Both start at full health and both FIT the arithmetic. */
            #define SETPAIR(base, h1, h2) do {                                \
                ar2[(base) + 0] = (unsigned char)((h1) & 0xFF);               \
                ar2[(base) + 1] = (unsigned char)((h1) >> 8);                 \
                ar2[(base) + 4] = 144; ar2[(base) + 5] = 0;                   \
                ar2[(base) + CVS2_STRIDE_T + 0] = (unsigned char)((h2) & 0xFF); \
                ar2[(base) + CVS2_STRIDE_T + 1] = (unsigned char)((h2) >> 8); \
                ar2[(base) + CVS2_STRIDE_T + 4] = 144;                        \
                ar2[(base) + CVS2_STRIDE_T + 5] = 0;                          \
            } while (0)

            SETPAIR(a, 144, 144);
            SETPAIR(b, 144, 144);
            /* ⚠⚠ THE FILLER IS ADOPTED FIRST, DELIBERATELY. An earlier
             * version listed the real pair first, and the sabotage sweep
             * showed that made 11b worthless: with watch_score() hardwired to
             * 0 the suite stayed GREEN, because a stable sort simply preserved
             * insertion order and the real pair was already at the front.
             * The case was passing on fixture order, not on ranking.
             *
             * With the filler inserted first, "best first" can only be
             * achieved by actually scoring behaviour. */
            fake[0].p1 = addr_b; fake[0].health = 144; fake[0].max_health = 144;
            fake[1].p1 = addr_a; fake[1].health = 144; fake[1].max_health = 144;
            emu_watch_sample(fake, 2);

            /* Play a "round": A's health diverges and falls; B never moves. */
            SETPAIR(a, 120, 144); emu_watch_sample(fake, 2);
            SETPAIR(a,  96, 131); emu_watch_sample(fake, 2);
            SETPAIR(a,  51, 131); emu_watch_sample(fake, 2);
            /* Round ends: both return to the cap -- reversibility, supplied by
             * the game rather than by asking the player for anything. */
            SETPAIR(a, 144, 144); emu_watch_sample(fake, 2);

            nw = emu_watch_report(wr, 8);
            check("11a both candidates are tracked", nw == 2, "n=%u", nw);

            /* The real one must RANK FIRST -- that is the whole product. */
            check("11b the behaving pair ranks FIRST",
                  nw >= 1 && wr[0].p1 == addr_a,
                  "first was 0x%llx, wanted 0x%llx",
                  (unsigned long long)(nw ? wr[0].p1 : 0),
                  (unsigned long long)addr_a);

            check("11c it recorded DROPS", nw >= 1 && wr[0].drops >= 2,
                  "drops=%u", nw ? wr[0].drops : 0);
            check("11d it recorded DIVERGENCE",
                  nw >= 1 && wr[0].diverged >= 2,
                  "diverged=%u", nw ? wr[0].diverged : 0);
            check("11e it recorded a RESET to the cap (the round change)",
                  nw >= 1 && wr[0].resets >= 1,
                  "resets=%u", nw ? wr[0].resets : 0);

            /* ⭐ THE DISCRIMINATING ASSERTION. Filler was sampled just as often
             * and fits the arithmetic identically -- it must score ZERO on
             * every behavioural axis. Without this, a watcher that credited
             * everything would pass 11b-11e. */
            for (i = 0; i < nw; i++) {
                if (wr[i].p1 != addr_b)
                    continue;
                check("11f the FILLER pair was sampled as often",
                      wr[i].samples == wr[0].samples,
                      "filler %u vs real %u", wr[i].samples, wr[0].samples);
                check("11g the FILLER pair shows NO drops, NO divergence",
                      wr[i].drops == 0 && wr[i].diverged == 0 &&
                      wr[i].changes == 0,
                      "drops=%u diverged=%u changes=%u",
                      wr[i].drops, wr[i].diverged, wr[i].changes);
            }

            emu_watch_reset();
            nw = emu_watch_report(wr, 8);
            check("11h reset clears the table", nw == 0, "n=%u", nw);

            #undef SETPAIR
            VirtualFree(ar2, 0, MEM_RELEASE);
        }
    }

    /* ---- 9. the guard absorbed nothing unexpected --------------------
     * ⚠ A run that faulted is a PARTIAL run and must say so rather than
     * presenting itself as clean. */
    printf("\n  (fault guard absorbed %ld fault(s))\n", mem_guard_fault_count());

    printf("\n%s -- %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
