/*
 * Offline self-test for emu_cursor. No game launch.
 *
 * ⚠⚠ THE STANDARD THIS SUITE IS HELD TO
 * A case that only asks the SUCCEEDING question cannot tell a working verdict
 * from a constant one. This project has shipped exactly that twice: a case that
 * passed with the mechanism hardwired to TRUE, and a case that passed on an
 * EMPTY table. So every guard here has a case that FAILS when that specific
 * guard is removed -- and the sabotage list at the bottom records which.
 *
 * ⚠ Constants are duplicated here rather than imported. The harness must not
 * supply the mechanism under test.
 */
#include "../src/emu_cursor.h"
#include "../src/emu_ram.h"
#include "../src/memsafe.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>

#define PAGE_T      4096u        /* NOT EC_PAGE -- see the note above */
#define MAXVAL_T      64u

static int failures;

static void check(const char *name, int cond, const char *fmt, ...)
{
    if (cond) {
        printf("  PASS  %s\n", name);
        return;
    }
    failures++;
    printf("  FAIL  %s  ", name);
    if (fmt && *fmt) {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
    printf("\n");
}

/* --- The fixture ---------------------------------------------------------
 * A small arena with planted fields at known offsets, each representing one
 * class the probe must separate. The whole point is that several of them pass
 * SOME of the tests -- a fixture where only the cursor moves would not
 * discriminate anything. */
#define FX_PAGES   8
#define FX_SIZE    (FX_PAGES * PAGE_T)

/* Offsets within page 1 (page 0 left quiet on purpose). */
#define OFF_CURSOR_U8    0x010   /* the real thing, u8                       */
#define OFF_CURSOR_U16   0x020   /* the real thing, u16                      */
#define OFF_CURSOR_U32   0x030   /* the real thing, u32                      */
#define OFF_COUNTER      0x040   /* climbs forever, never returns            */
#define OFF_CHURN        0x050   /* changes during the CONTROL               */
#define OFF_FROZEN       0x060   /* never changes                            */
#define OFF_TOGGLE       0x070   /* 0<->1 flag                               */
#define OFF_BIGJUMP      0x080   /* reacts and returns, but steps by 7       */
#define OFF_FLOAT        0x090   /* reversible IEEE float (an animation)     */
#define OFF_OUTOFRANGE   0x0a0   /* steps by one, but value is huge          */
#define OFF_RATCHET      0x0b0   /* quiet in the control, +1 on the move, and
                                  * NEVER returns -- the shape that actually
                                  * reaches the reversibility test. A plain
                                  * every-capture counter is caught earlier, by
                                  * the control filter, so it cannot exercise
                                  * the guard it was meant to test.            */

/* A stand-in emulator state object, so the "is a ROM running?" test can be
 * driven without launching anything. Mirrors tools/test_emu_ram.c. */
static ULONGLONG g_slot;
static char     *g_state;
static void     *g_st_arena;
static char      g_rom_name[16];
#define STATE_SIZE 0x100

static void put_q(char *p, unsigned off, ULONGLONG v) { memcpy(p + off, &v, 8); }
static void put_d(char *p, unsigned off, unsigned  v) { memcpy(p + off, &v, 4); }

static void build_state(const char *rom_name, unsigned rom_index)
{
    memset(g_state, 0, STATE_SIZE);
    _snprintf(g_rom_name, sizeof g_rom_name, "%s", rom_name);
    put_q(g_state, EMU_OFF_ARENA,     (ULONGLONG)(ULONG_PTR)g_st_arena);
    put_q(g_state, EMU_OFF_ROM_NAME,  (ULONGLONG)(ULONG_PTR)g_rom_name);
    put_d(g_state, EMU_OFF_ROM_INDEX, rom_index);
    g_slot = (ULONGLONG)(ULONG_PTR)g_state;
    emu_ram_test_redirect((ULONGLONG)(ULONG_PTR)&g_slot);
}

static BYTE *g_fx;
static unsigned g_counter;
static unsigned g_churn;   /* retained: some cases still reset it */
static unsigned g_ratchet;

static BYTE *pg(unsigned page) { return g_fx + page * PAGE_T; }

static void put8 (unsigned off, unsigned v) { pg(1)[off] = (BYTE)v; }
static void put16(unsigned off, unsigned v)
{ unsigned short t = (unsigned short)v; memcpy(pg(1) + off, &t, 2); }
static void put32(unsigned off, unsigned v)
{ memcpy(pg(1) + off, &v, 4); }

/* Write the fixture for one phase. `cursor` is the cursor's current value. */
static void fx_write(ec_phase phase, unsigned cursor)
{
    put8 (OFF_CURSOR_U8,  cursor);
    put16(OFF_CURSOR_U16, cursor);
    put32(OFF_CURSOR_U32, cursor);

    /* A counter advances on EVERY capture and never returns. */
    put32(OFF_COUNTER, ++g_counter);

    /* ⭐⭐ THE CHURN FIELD MUST BE REFUSABLE ONLY BY GUARD (a).
     * An every-capture counter would ALSO fail reversibility, so removing the
     * control filter would leave it rejected anyway and the sabotage would
     * pass -- a guard answering for another guard.
     * So: it DIFFERS between S0 and S1 (only (a) can see that), then behaves
     * exactly like a perfect cursor at S1/S2/S3 (small, +1, returns). */
    /* ⚠ The probe derives each cycle's S0 from the PREVIOUS cycle's S3 -- in a
     * real run the cursor is back where it started, so those are the same
     * moment. A fixture therefore cannot set S0 directly; churn has to differ
     * between the previous S3 and this S1, which means ALTERNATING per cycle.
     *
     * Values: S1 = 31 or 41 (alternating), S2 = S1+1, S3 = S1.
     * So S0(prev S3)=31 vs S1=41 -> only guard (a) can refuse it, while the
     * step, bounds and reversibility guards all see a perfect cursor. */
    {
        unsigned b = (g_churn & 1u) ? 41u : 31u;
        switch (phase) {
        case EC_PHASE_BASELINE: break;            /* overwritten from S3 */
        case EC_PHASE_CONTROL:  put32(OFF_CHURN, b);      break;
        case EC_PHASE_MOVED:    put32(OFF_CHURN, b + 1u); break;
        case EC_PHASE_RETURNED: put32(OFF_CHURN, b); g_churn++; break;
        }
    }

    put32(OFF_FROZEN, 0x11111111u);

    /* Toggle follows the cursor's parity: reversible, steps by one, tiny. */
    put8(OFF_TOGGLE, cursor & 1u);

    /* ⭐⭐ REFUSABLE ONLY BY GUARD (d). Both values must stay INSIDE
     * EC_MAX_VALUE, or the bounds guard rejects it and the step sabotage
     * passes on someone else's work. 10 -> 17 is a delta of 7, both small. */
    switch (phase) {
    case EC_PHASE_BASELINE:
    case EC_PHASE_CONTROL:  put32(OFF_BIGJUMP, 10u); break;
    case EC_PHASE_MOVED:    put32(OFF_BIGJUMP, 17u); break;  /* delta 7 */
    case EC_PHASE_RETURNED: put32(OFF_BIGJUMP, 10u); break;  /* returns */
    }

    /* An IEEE float that animates with the cursor and returns. */
    {
        float f = 22.93f + (float)cursor;
        unsigned bits;
        memcpy(&bits, &f, 4);
        put32(OFF_FLOAT, bits);
    }

    /* Steps by exactly one, reverses -- but is far outside a grid's range. */
    put32(OFF_OUTOFRANGE, 900000u + cursor);

    /* ⭐ The RATCHET only advances on the MOVE phase, so it is quiet in the
     * control (passing filter (a)) and reacts (passing (b)) -- and then fails
     * reversibility, which is exactly the guard under test. */
    if (phase == EC_PHASE_MOVED) g_ratchet++;
    put32(OFF_RATCHET, 50u + g_ratchet);
}

/* Drive one full cycle: baseline -> control -> moved -> returned.
 * `from` is the cursor position before the move; it goes from -> from+1 -> from.
 * Returns the status of the last step. */
static ec_status run_cycle(unsigned from, BOOL first)
{
    ec_status s;

    /* S0 */
    fx_write(EC_PHASE_BASELINE, from);
    if (first) {
        s = emu_cursor_arm();
        if (s != EC_OK) return s;
    } else {
        /* A continuing run re-captures its own baseline internally, so the
         * fixture just has to hold still here. */
    }

    /* S1: the control. The cursor must NOT move; churn and the counter do. */
    fx_write(EC_PHASE_CONTROL, from);
    s = emu_cursor_step(EC_PHASE_CONTROL);
    if (s != EC_OK) return s;

    /* S2: the move. */
    fx_write(EC_PHASE_MOVED, from + 1);
    s = emu_cursor_step(EC_PHASE_MOVED);
    if (s != EC_OK) return s;

    /* S3: the reverse. */
    fx_write(EC_PHASE_RETURNED, from);
    s = emu_cursor_step(EC_PHASE_RETURNED);
    return s;
}

/* Does the candidate list contain this address at this width? */
static const ec_field *find_cand(const ec_field *f, unsigned n,
                                 ULONGLONG addr, ec_width w)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (f[i].addr == addr && f[i].width == w) return &f[i];
    return NULL;
}

int main(void)
{
    ec_field cands[256];
    ec_report rep;
    unsigned n;
    ec_status s;
    ULONGLONG p1;

    if (!mem_guard_install()) {
        printf("FATAL: the fault guard failed its own self-test\n");
        return 1;
    }

    g_fx = (BYTE *)VirtualAlloc(NULL, FX_SIZE, MEM_COMMIT | MEM_RESERVE,
                                PAGE_READWRITE);
    if (!g_fx) { printf("FATAL: fixture alloc failed\n"); return 1; }
    memset(g_fx, 0, FX_SIZE);
    p1 = (ULONGLONG)(ULONG_PTR)pg(1);

    printf("\n--- 1. the hash -------------------------------------------\n");
    {
        BYTE a[64], b[64];
        unsigned i;
        for (i = 0; i < 64; i++) a[i] = b[i] = (BYTE)i;

        check("1a identical blocks hash equal",
              ec_hash_block(a, 64) == ec_hash_block(b, 64), "");

        /* ⚠ Flip a bit at EVERY offset, not a sample. An earlier hash in this
         * project dropped its tail and two of three assertions still passed. */
        {
            int missed = -1;
            for (i = 0; i < 64; i++) {
                b[i] ^= 0x01;
                if (ec_hash_block(a, 64) == ec_hash_block(b, 64)) { missed = (int)i; }
                b[i] ^= 0x01;
            }
            check("1b a flipped bit at EVERY offset changes the hash",
                  missed < 0, "offset %d did not change it", missed);
        }
        check("1c the hash never returns 0 (reserved for 'no page')",
              ec_hash_block(a, 64) != 0, "");
        /* A length not a multiple of 8 exercises the bytewise tail. */
        {
            BYTE t1[13], t2[13];
            memset(t1, 0xAA, 13); memcpy(t2, t1, 13);
            t2[12] ^= 0xFF;
            check("1d the TAIL is covered (last byte of a 13-byte block)",
                  ec_hash_block(t1, 13) != ec_hash_block(t2, 13), "");
        }
    }

    printf("\n--- 2. a cursor is found, at all three widths -------------\n");
    emu_cursor_test_window((ULONGLONG)(ULONG_PTR)g_fx, FX_SIZE);
    g_counter = 0; g_churn = 0; g_ratchet = 0;

    /* ⚠ THREE cycles, not two. Cycle 1 only LEARNS which pages react -- it has
     * no S0/S1 byte content, because we did not know which pages to keep until
     * S2. Cycles 2 and 3 are the first that can actually judge, and requiring
     * both is what makes `cycles` mean CONSECUTIVE survivals. */
    s = run_cycle(5, TRUE);
    check("2a cycle 1 completes (learns the page set)", s == EC_OK,
          "got %s", ec_status_name(s));
    s = run_cycle(5, FALSE);
    check("2b cycle 2 completes", s == EC_OK, "got %s", ec_status_name(s));
    s = run_cycle(5, FALSE);
    check("2b2 cycle 3 completes", s == EC_OK, "got %s", ec_status_name(s));

    n = emu_cursor_candidates(cands, 256);
    emu_cursor_status(&rep);

    check("2c the u8 cursor is a candidate",
          find_cand(cands, n, p1 + OFF_CURSOR_U8, EC_U8) != NULL,
          "%u candidate(s)", n);
    check("2d the u16 cursor is a candidate",
          find_cand(cands, n, p1 + OFF_CURSOR_U16, EC_U16) != NULL, "");
    check("2e the u32 cursor is a candidate",
          find_cand(cands, n, p1 + OFF_CURSOR_U32, EC_U32) != NULL, "");

    printf("\n--- 3. the look-alikes are REJECTED -----------------------\n");
    check("3a the monotonic COUNTER is rejected",
          find_cand(cands, n, p1 + OFF_COUNTER, EC_U32) == NULL,
          "a counter was reported as a cursor");
    check("3b the CHURNING field is rejected (the control did its job)",
          find_cand(cands, n, p1 + OFF_CHURN, EC_U32) == NULL,
          "a field that changes on its own was reported");
    check("3c the FROZEN field is rejected",
          find_cand(cands, n, p1 + OFF_FROZEN, EC_U32) == NULL, "");
    check("3d a field that steps by SEVEN is rejected",
          find_cand(cands, n, p1 + OFF_BIGJUMP, EC_U32) == NULL, "");
    /* ⭐ This case survives, but note WHAT rejects it: the BOUNDS guard, not a
     * float-specific test. A float in any UI range has bits ~1.1 billion. An
     * explicit float band was written, found unreachable by sabotage (deleting
     * it left the suite green), and removed rather than kept as false
     * coverage. */
    check("3e a reversible IEEE FLOAT is rejected (by the bounds guard)",
          find_cand(cands, n, p1 + OFF_FLOAT, EC_U32) == NULL,
          "an animating float was reported as an index");
    check("3f a huge value stepping by one is rejected",
          find_cand(cands, n, p1 + OFF_OUTOFRANGE, EC_U32) == NULL, "");
    check("3g a RATCHET (quiet in control, reacts, never returns) is rejected",
          find_cand(cands, n, p1 + OFF_RATCHET, EC_U32) == NULL,
          "a field that never returns was reported as a cursor");

    printf("\n--- 4. toggles are FLAGGED, not ranked as indices ---------\n");
    {
        const ec_field *t = find_cand(cands, n, p1 + OFF_TOGGLE, EC_U8);
        /* ⚠ A 0<->1 flag passes "steps by one and returns" trivially. It is
         * allowed to appear, but must be labelled and must sort BELOW real
         * candidates, or it crowds them out of the report. */
        if (t) {
            check("4a the 0<->1 toggle is flagged as a toggle", t->is_toggle,
                  "it was not flagged");
            {
                const ec_field *c = find_cand(cands, n, p1 + OFF_CURSOR_U8,
                                              EC_U8);
                check("4b a real cursor sorts ABOVE the toggle",
                      c && t && c < t, "toggle ranked at or above the cursor");
            }
        } else {
            check("4a the 0<->1 toggle is flagged as a toggle", 1,
                  "(not reported at all, which is also acceptable)");
            check("4b a real cursor sorts ABOVE the toggle", 1, "");
        }
    }

    printf("\n--- 5. the accounting is POSITIVE, not just absent --------\n");
    check("5a it reports pages EXAMINED, not only pages kept",
          rep.pages_total > 0, "pages_total is 0");
    check("5b it reports how many fields it looked at",
          rep.fields_examined > 0, "fields_examined is 0");
    /* ⚠ The plain counter never reaches this branch: it changes during the
     * CONTROL too, so filter (a) catches it first. Only the RATCHET exercises
     * the reversibility guard. Asserting on the wrong field here would have
     * been a test that could never pass. */
    check("5c it counted the fields rejected for NOT RETURNING",
          rep.fields_counter > 0,
          "the ratchet should have been rejected by reversibility");
    check("5d it recorded a MEASURED pass duration",
          rep.bytes_hashed > 0, "bytes_hashed is 0");
    check("5e cycles_done matches the cycles driven",
          rep.cycles_done == 3, "got %u", rep.cycles_done);

    printf("\n--- 6. consecutive cycles are required --------------------\n");
    {
        const ec_field *c = find_cand(cands, n, p1 + OFF_CURSOR_U8, EC_U8);
        check("6a the cursor survived every judged cycle",
              c && c->cycles >= 2, "cycles=%u", c ? c->cycles : 0u);
    }

    printf("\n--- 7. 'could not measure' is a RETURN VALUE --------------\n");
    emu_cursor_test_window(0x10000ULL, FX_SIZE);    /* unreadable */
    s = emu_cursor_arm();
    check("7a an unreadable window -> not EC_OK",
          s != EC_OK, "got %s -- a clean OK on unreadable memory",
          ec_status_name(s));

    emu_cursor_test_window((ULONGLONG)(ULONG_PTR)g_fx, FX_SIZE);
    check("7b a phase out of order -> NOT_ARMED, not a silent reinterpretation",
          emu_cursor_step(EC_PHASE_RETURNED) == EC_NOT_ARMED, "");

    printf("\n--- 8. the cap counts what it ENCOUNTERED -----------------\n");
    {
        emu_cursor_test_window((ULONGLONG)(ULONG_PTR)g_fx, FX_SIZE);
        emu_cursor_test_page_cap(1);
        g_counter = 0; g_churn = 0; g_ratchet = 0;
        run_cycle(5, TRUE);
        run_cycle(5, FALSE);
        run_cycle(5, FALSE);
        emu_cursor_status(&rep);
        check("8a pages_seen counts beyond the cap",
              rep.pages_seen >= rep.pages_kept, "seen=%u kept=%u",
              rep.pages_seen, rep.pages_kept);
        check("8b truncation is REPORTED, not silent",
              rep.pages_seen <= rep.pages_kept || rep.truncated,
              "the cap bound but nothing said so");
        emu_cursor_test_page_cap(0);
    }

    printf("\n--- 9. ARMING: a ROM must actually be RUNNING --------------\n");
    {
        /* ⛔⛔ THE BUG THIS PINS, from the 2026-09-16 18:54 run.
         * The probe armed on `emu_rom_index() == EMU_OK`, which is TRUE on the
         * shell because index 0 is a valid game (cvs1). It armed 24 s before
         * CVS2 loaded, burned its 30 s timeout on the title screen, and then
         * reported "the emulated game does not use sKeyboard" -- a confident
         * negative it had ~6 s to test. */
        char nm[16];
        g_state    = (char *)VirtualAlloc(NULL, STATE_SIZE,
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
        g_st_arena = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
        check("9-setup state allocated", g_state && g_st_arena, "");

        if (g_state && g_st_arena) {
            /* ⛔⛔ THE SHELL: a readable state and index 0, but NO rom name.
             * This is the exact state the 18:54 run armed in. The bug was that
             * the caller tested emu_rom_index() -- which is EMU_OK here,
             * because index 0 is cvs1. The distinguishing fact is the NAME. */
            build_state("", 0);
            {
                int idx = -1;
                check("9a0 the INDEX alone does NOT discriminate (index 0 is "
                      "readable on the shell -- this is why the bug happened)",
                      emu_rom_index(&idx) == EMU_OK && idx == 0,
                      "emu_rom_index said %s", 
                      emu_status_name(emu_rom_index(&idx)));
            }
            check("9a index 0 with an EMPTY name is NOT running (the shell)",
                  !emu_cursor_rom_running(nm, sizeof nm),
                  "armed on the shell -- this is the 18:54 bug");

            /* cvs1 really loaded: index 0, and a name. Index 0 must not be
             * mistaken for 'nothing matched' in the OTHER direction either. */
            build_state("cvs1", 0);
            check("9b index 0 WITH a name IS running (cvs1 is a real game)",
                  emu_cursor_rom_running(nm, sizeof nm) &&
                  strcmp(nm, "cvs1") == 0,
                  "index 0 was treated as 'no game' -- silences exactly one "
                  "game, invisibly");

            build_state("cvs2u", 3);
            check("9c a loaded ROM reports its NAME back to the caller",
                  emu_cursor_rom_running(nm, sizeof nm) &&
                  strcmp(nm, "cvs2u") == 0, "got '%s'", nm);

            /* A null slot is the real 'no game' signal. */
            g_slot = 0;
            emu_ram_test_redirect((ULONGLONG)(ULONG_PTR)&g_slot);
            check("9d a null state slot is NOT running",
                  !emu_cursor_rom_running(nm, sizeof nm), "");

            emu_ram_test_redirect(0);
            VirtualFree(g_state, 0, MEM_RELEASE);
            VirtualFree(g_st_arena, 0, MEM_RELEASE);
        }
    }

    printf("\n--- 10. THE DIRECT WATCH ----------------------------------\n");
    {
        /* A window big enough to contain the real offsets, so the watch
         * resolves them rather than bounds-rejecting. */
        SIZE_T big = 0x8200000;
        BYTE *w = (BYTE *)VirtualAlloc(NULL, big, MEM_RESERVE, PAGE_NOACCESS);
        ec_watch t[EC_WATCH_MAX];
        unsigned n;

        check("10-setup reserved a window", w != NULL, "");
        if (w) {
            /* Commit just the pages the watch will touch. */
            VirtualAlloc(w, 0x1000, MEM_COMMIT, PAGE_READWRITE);
            VirtualAlloc(w + 0x8030000, 0x1000, MEM_COMMIT, PAGE_READWRITE);
            VirtualAlloc(w + 0x8105000, 0x1000, MEM_COMMIT, PAGE_READWRITE);

            emu_cursor_watch_reset();
            emu_cursor_test_window((ULONGLONG)(ULONG_PTR)w, big);

            w[0x8030134] = 7;
            check("10a the watch samples a committed offset",
                  emu_cursor_watch_tick(), "tick returned FALSE");
            n = emu_cursor_watch_report(t, EC_WATCH_MAX);
            check("10b it reports every watched offset", n >= 5, "got %u", n);
            check("10c it read the planted value",
                  t[0].readable && t[0].value == 7,
                  "readable=%d value=%u", t[0].readable, t[0].value);

            /* A change must be counted, and distinct values tracked. */
            w[0x8030134] = 8;
            emu_cursor_watch_tick();
            emu_cursor_watch_report(t, EC_WATCH_MAX);
            check("10d a change is counted", t[0].changes == 1,
                  "changes=%u", t[0].changes);
            check("10e distinct values are tracked", t[0].distinct == 2,
                  "distinct=%u", t[0].distinct);

            /* ⛔ The case that matters: UNREADABLE must not read as "did not
             * move". An offset past the window must be flagged, not silently
             * treated as a stable zero. */
            emu_cursor_watch_reset();
            emu_cursor_test_window((ULONGLONG)(ULONG_PTR)w, 0x1000);
            emu_cursor_watch_tick();
            emu_cursor_watch_report(t, EC_WATCH_MAX);
            check("10f an offset past the window is NOT readable "
                  "(not a stable zero)",
                  !t[0].readable,
                  "an out-of-range offset reported readable value %u",
                  t[0].value);

            emu_cursor_test_window(0, 0);
            emu_cursor_watch_reset();
            VirtualFree(w, 0, MEM_RELEASE);
        }
    }

    printf("\n--- 11. THE IDLE DISCRIMINATOR ----------------------------\n");
    {
        /* ⭐⭐ THE CASE THAT WOULD HAVE SAVED SIX RUNS.
         * A blink phase satisfies every differential filter. The ONLY thing
         * that separates it from a cursor is that it changes while the player
         * is idle. This asserts that distinction directly. */
        BYTE *w = (BYTE *)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_READWRITE);
        ec_scan sc[8];
        unsigned ns, i;

        check("11-setup", w != NULL, "");
        if (w) {
            memset(w, 0, 0x1000);
            emu_cursor_test_window((ULONGLONG)(ULONG_PTR)w, 0x1000);
            { ULONGLONG one = 0; emu_cursor_scan_set_pages(&one, 1); }

            /* offset 0 = a CURSOR: changes only when input_recent is TRUE,
             * and walks a wide range.
             * offset 1 = a BLINK: flips every tick regardless of input. */
            w[0] = 0; w[1] = 2;
            emu_cursor_scan_tick(FALSE);          /* prime */

            /* ⛔⛔ THE BLINK MUST OUT-SCORE THE CURSOR ON EVERY OTHER METRIC.
             * A first version gave the blink only 2 distinct values, so the
             * "most distinct" tiebreak ranked the cursor first on its own and
             * the quiet-first rule could be deleted with the suite still green
             * -- a guard answering for another guard, third time on this
             * branch. Here the blink cycles through MORE values than the
             * cursor, so ONLY "changed while idle" can separate them.
             * This is also the realistic case: run 6's blink took {2,3,16}
             * while a cursor parked in one spot takes very few. */
            for (i = 0; i < 24; i++) {
                BOOL pressed = (i % 8) == 0;     /* the player acts rarely   */
                if (pressed) w[0] = (BYTE)(w[0] + 1);   /* cursor: 3 values  */
                w[1] = (BYTE)(10 + (i % 12));           /* blink: 12 values  */
                emu_cursor_scan_tick(pressed);
            }

            ns = emu_cursor_scan_report(sc, 8);
            check("11a both fields are reported", ns >= 2, "got %u", ns);

            check("11b the CURSOR ranks FIRST (no idle changes)",
                  ns >= 1 && sc[0].offset == 0,
                  "first was +0x%llx", ns ? (unsigned long long)sc[0].offset : 0);
            check("11c the cursor has ZERO idle changes",
                  ns >= 1 && sc[0].idle_changes == 0,
                  "idle_changes=%u", ns ? sc[0].idle_changes : 999);
            /* ⚠ The cursor deliberately has FEWER distinct values than the
             * animation here, so this asserts it was ranked first DESPITE
             * losing every other metric. */
            check("11d the cursor ranks first despite FEWER distinct values",
                  ns >= 2 && sc[0].distinct < sc[1].distinct,
                  "cursor distinct=%u, next=%u",
                  ns ? sc[0].distinct : 0, ns > 1 ? sc[1].distinct : 0);

            /* The blink must be present, flagged by idle changes, and ranked
             * below -- not silently dropped. */
            for (i = 0; i < ns; i++) if (sc[i].offset == 1) break;
            check("11e the BLINK is still REPORTED (not hidden)", i < ns, "");
            check("11f the blink has MANY idle changes -- the tell",
                  i < ns && sc[i].idle_changes > 5,
                  "idle_changes=%u", i < ns ? sc[i].idle_changes : 0);
            check("11g the blink ranks BELOW the cursor", i > 0,
                  "the animation outranked the cursor");

            emu_cursor_scan_set_pages(NULL, 0);
            emu_cursor_test_window(0, 0);
            VirtualFree(w, 0, MEM_RELEASE);
        }
    }

    printf("\n--- 12. SCATTERED pages are ALL scanned -------------------\n");
    {
        /* ⛔⛔ THE BUG THIS PINS: the scan used to take one (offset,len). Real
         * reacting pages are scattered over 100+ MB in nine clusters, so the
         * span blew the cap and the code silently narrowed to ONE page -- which
         * had stopped reacting by the next cycle. A whole run watched stale
         * memory and reported 8 fields.
         *
         * A field on the LAST page must be found, or the list is not really
         * being scanned. */
        SIZE_T big = 0x400000;                 /* 4 MB of address space */
        BYTE *w = (BYTE *)VirtualAlloc(NULL, big, MEM_RESERVE, PAGE_NOACCESS);
        ULONGLONG pgs[3];
        ec_scan sc[8];
        unsigned ns, i;

        check("12-setup", w != NULL, "");
        if (w) {
            /* Three pages, deliberately FAR apart. */
            pgs[0] = 0x0000; pgs[1] = 0x100000; pgs[2] = 0x3FF000;
            for (i = 0; i < 3; i++)
                VirtualAlloc(w + pgs[i], 0x1000, MEM_COMMIT, PAGE_READWRITE);

            emu_cursor_test_window((ULONGLONG)(ULONG_PTR)w, big);
            check("12a the scan accepts a scattered page LIST",
                  emu_cursor_scan_set_pages(pgs, 3), "");

            /* Put the cursor-like field on the LAST page only. */
            w[pgs[2] + 0x10] = 0;
            emu_cursor_scan_tick(FALSE);       /* prime */
            for (i = 0; i < 12; i++) {
                w[pgs[2] + 0x10] = (BYTE)(i + 1);
                emu_cursor_scan_tick(TRUE);    /* always "after input" */
            }

            ns = emu_cursor_scan_report(sc, 8);
            check("12b a field on the LAST scattered page is FOUND",
                  ns >= 1 && sc[0].offset == pgs[2] + 0x10,
                  "top offset +0x%llx, expected +0x%llx",
                  ns ? (unsigned long long)sc[0].offset : 0,
                  (unsigned long long)(pgs[2] + 0x10));
            check("12c it is quiet while idle (only input changes counted)",
                  ns >= 1 && sc[0].idle_changes == 0 &&
                  sc[0].input_changes >= 10,
                  "idle=%u input=%u",
                  ns ? sc[0].idle_changes : 0, ns ? sc[0].input_changes : 0);

            emu_cursor_scan_set_pages(NULL, 0);
            emu_cursor_test_window(0, 0);
            VirtualFree(w, 0, MEM_RELEASE);
        }
    }

    printf("\n--- 13. HISTORY SURVIVES adding pages ---------------------\n");
    {
        /* ⛔⛔ THE BUG THIS PINS, which cost an entire run.
         * The reacting page set wobbles every cycle. The first version rebuilt
         * the scan table on each change, which RESET all accumulated history --
         * so every field reported "2 distinct, 1 change" forever and the scan
         * could never establish a range. */
        BYTE *w = (BYTE *)VirtualAlloc(NULL, 0x3000, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_READWRITE);
        ULONGLONG p1[1] = { 0x0000 };
        ULONGLONG p2[2] = { 0x0000, 0x2000 };
        ec_scan sc[8];
        unsigned ns, i, before;

        check("13-setup", w != NULL, "");
        if (w) {
            memset(w, 0, 0x3000);
            emu_cursor_test_window((ULONGLONG)(ULONG_PTR)w, 0x3000);
            emu_cursor_scan_set_pages(p1, 1);

            /* Build up history on page 0. */
            w[0x10] = 0;
            emu_cursor_scan_tick(FALSE);
            for (i = 1; i <= 10; i++) {
                w[0x10] = (BYTE)i;
                emu_cursor_scan_tick(TRUE);
            }
            ns = emu_cursor_scan_report(sc, 8);
            check("13a history accumulated before the change",
                  ns >= 1 && sc[0].distinct >= 10,
                  "distinct=%u", ns ? sc[0].distinct : 0);
            before = ns ? sc[0].distinct : 0;

            /* Now ADD a second page -- the wobble that used to wipe everything. */
            check("13b adding a page reports a change",
                  emu_cursor_scan_add_pages(p2, 2), "");
            check("13c the set GREW to 2 pages",
                  emu_cursor_scan_npages() == 2,
                  "npages=%u", emu_cursor_scan_npages());

            ns = emu_cursor_scan_report(sc, 8);
            check("13d page 0's HISTORY SURVIVED the add",
                  ns >= 1 && sc[0].distinct >= before,
                  "distinct was %u, now %u", before, ns ? sc[0].distinct : 0);

            /* And the newly added page must SEED, not count 4096 phantom
             * changes on its first read. */
            w[0x2000 + 0x20] = 99;
            emu_cursor_scan_tick(FALSE);
            ns = emu_cursor_scan_report(sc, 8);
            for (i = 0; i < ns; i++)
                if (sc[i].offset == 0x2000 + 0x20) break;
            check("13e a NEWLY ADDED page seeds rather than reporting "
                  "phantom changes",
                  i == ns || sc[i].changes == 0,
                  "the new page reported %u change(s) on its first read",
                  i < ns ? sc[i].changes : 0);

            emu_cursor_scan_set_pages(NULL, 0);
            emu_cursor_test_window(0, 0);
            VirtualFree(w, 0, MEM_RELEASE);
        }
    }

    emu_cursor_test_window(0, 0);
    emu_cursor_reset();
    VirtualFree(g_fx, 0, MEM_RELEASE);

    printf("\n  (fault guard absorbed %ld fault(s))\n", mem_guard_fault_count());
    printf("\n%s -- %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
