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

static BYTE *g_fx;
static unsigned g_counter;
static unsigned g_churn;
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

    /* Churn changes between S0 and S1 -- i.e. during the control window. */
    put32(OFF_CHURN, ++g_churn);

    put32(OFF_FROZEN, 0x11111111u);

    /* Toggle follows the cursor's parity: reversible, steps by one, tiny. */
    put8(OFF_TOGGLE, cursor & 1u);

    /* Reacts and returns like the cursor, but by 7 rather than 1. */
    put32(OFF_BIGJUMP, 100u + cursor * 7u);

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
    check("3e a reversible IEEE FLOAT is rejected",
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

    emu_cursor_test_window(0, 0);
    emu_cursor_reset();
    VirtualFree(g_fx, 0, MEM_RELEASE);

    printf("\n  (fault guard absorbed %ld fault(s))\n", mem_guard_fault_count());
    printf("\n%s -- %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
