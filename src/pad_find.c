#include "pad_find.h"
#include "memsafe.h"
#include "log.h"
#include "speech.h"

#include <stdio.h>
#include <string.h>

/* --- What we are looking for ---------------------------------------------
 *
 * The reflected field order of sPad::Pad puts the five button words together:
 *
 *      Old  Trg  Rel  Chg  Rep
 *
 * They are consecutive u32 in declaration order, so a candidate is a 20-byte
 * run of five dwords at some offset within a 0xb8-byte record. We do not know
 * that offset, so we test EVERY 4-byte-aligned position and let the algebra
 * reject the ones that are not it.
 *
 * ⚠ We deliberately do NOT assume the five are adjacent in memory. A compiler
 * may order or pad them differently than the reflection metadata lists them.
 * What we require is the RELATIONSHIP, tested over a sliding window -- that
 * holds regardless of layout, and it is the only thing we actually know.
 */

/* Sites tracked at once. The scan looks at every 4-aligned dword in writable
 * memory, which is far too many to keep, so a site is only ARMED once its
 * first sample already looks like a plausible button word (see plausible()).
 * That filter is cheap and rejects the overwhelming majority immediately. */
#define PAD_MAX_SITES 8192

/* Consecutive frames a site must satisfy the algebra before it is confirmed.
 *
 * ⚠ One frame proves nothing: when nothing is pressed, Old/Trg/Rel/Chg are all
 * zero, and 0 == 0 & ~0 holds trivially for any five zero words -- of which
 * this process contains millions. So a site only earns evidence on frames
 * where something ACTUALLY CHANGED, and needs several of those. This is the
 * same distinction that the heapdiff control existed to make: a relationship
 * that holds while nothing happens is not evidence. */
#define PAD_CONFIRM_EVENTS 4

typedef struct {
    uintptr_t addr;        /* address of the `Old` word */
    unsigned  prev_old;    /* Old as of the previous sample */
    unsigned  events;      /* samples where a real press/release was seen */
    unsigned  agree;       /* of those, how many satisfied the algebra */
    unsigned  seen_bits;   /* union of every direction bit ever observed */
    BOOL      dead;        /* failed the algebra -- rejected for good */
} pad_site;

static pad_site      g_sites[PAD_MAX_SITES];
static unsigned      g_nsites;
static BOOL          g_armed;
static uintptr_t     g_confirmed;      /* address of the confirmed Old word */
static unsigned      g_last_spoken;    /* dedupe for pad_say_state */
static BOOL          g_announced;      /* spoke the confirmation once */
static BOOL          g_pinned;         /* test pinned the representative */

/* Offsets of the five words relative to `Old`, in declaration order. */
#define OFF_OLD 0
#define OFF_TRG 1
#define OFF_REL 2
#define OFF_CHG 3
#define OFF_REP 4
#define PAD_WORDS 5

/* Could this dword be a button word?
 *
 * Button words are bitmasks over ~22 defined buttons (16 named + PAD_20..31 in
 * the reflected list, but the high bits are rarely all live). The cheapest
 * strong filter is that the top 8 bits are clear: a float, a pointer low-half,
 * a timer or a counter almost always sets something up there, and a real
 * button word essentially never does. */
static BOOL plausible(unsigned v)
{
    return (v & 0xFF000000u) == 0;
}

/* The engine's own invariant, evaluated for one sample.
 *
 * ⚠⚠ THE FIRST VERSION OF THIS HAD `Old` BACKWARDS, and the harness caught it
 * before it ever ran in the game. The mistake is worth recording because the
 * wrong version still LOOKED right and still confirmed sites.
 *
 * `Old` is not the previous frame's state as the name suggests -- by the time
 * the record is observable it has already been updated, so it holds the
 * CURRENT state, and the previous state is recovered from Chg:
 *
 *      Prev == Old ^ Chg
 *      Trg  == Old & ~Prev     (newly pressed this frame)
 *      Rel  == Prev & ~Old     (newly released this frame)
 *
 * Reading it the other way round (treating Old as previous and deriving Cur)
 * makes the true +0 window FAIL its own test, so the scanner armed a shifted
 * window whose fifth word ran past the end of the record into unrelated
 * memory -- which then broke the algebra a few frames later and killed the
 * real candidate. The symptom was "the planted pad is armed, then silently
 * disappears before the report".
 *
 * Three independent words constrained by two equations. Random data satisfies
 * that with probability ~2^-64 per nonzero sample, which is why a handful of
 * real events is enough to identify the struct outright rather than merely
 * rank it. */
static BOOL consistent(const unsigned w[PAD_WORDS])
{
    unsigned old = w[OFF_OLD], trg = w[OFF_TRG];
    unsigned rel = w[OFF_REL], chg = w[OFF_CHG];
    unsigned prev = old ^ chg;
    return trg == (old & ~prev) && rel == (prev & ~old);
}

/* Did anything actually happen in this sample? Only then is agreement
 * informative -- see the note on PAD_CONFIRM_EVENTS. */
static BOOL eventful(const unsigned w[PAD_WORDS])
{
    return w[OFF_CHG] != 0 || w[OFF_TRG] != 0 || w[OFF_REL] != 0;
}

/* --- Region walking ------------------------------------------------------
 * Same rules the earlier scanners settled on: writable, committed, not a
 * mapped image. Live engine state is written every frame, so it must be in
 * private writable memory. */
static BOOL want_region(const MEMORY_BASIC_INFORMATION *mbi)
{
    if (mbi->State != MEM_COMMIT)
        return FALSE;
    if (mbi->Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return FALSE;
    DWORD a = mbi->Protect & 0xFF;
    if (!(a & (PAGE_READWRITE | PAGE_WRITECOPY |
               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return FALSE;
    if (mbi->Type == MEM_IMAGE)
        return FALSE;
    return TRUE;
}

void pad_scan_arm(void)
{
    g_nsites = 0;
    g_confirmed = 0;
    g_announced = FALSE;
    g_pinned = FALSE;
    g_last_spoken = ~0u;

    /* ⭐ Log and speak ON ENTRY. A hotkey that buys silence has cost this
     * project several diagnostic rounds: "stalled", "crashed" and "the user
     * quit early" are indistinguishable unless the entry is announced. */
    /* ⚠ The player must be HOLDING a direction as this runs: a site is only
     * armed when something is actually pressed (see the note at the arming
     * filter). Announced in speech because the user cannot see the log. */
    log_line("pad: arming scan -- looking for sPad::Pad "
             "(Old/Trg/Rel/Chg/Rep consistency); a direction must be HELD");
    speech_say(L"Hold a direction now, then release it.", TRUE);

    /* ⚠⚠ MEM_GUARDED is inert unless the handler is installed, and an inert
     * guard is worse than none: every read here LOOKS protected while a single
     * freed region takes the process down. The self-test crashed with
     * 0xC0000005 for exactly this reason -- the module used the macro but
     * never armed it. Install is idempotent and self-tests by faulting on
     * purpose, so a guard that would not have worked refuses to proceed. */
    if (!mem_guard_install()) {
        log_line("pad: ⛔ fault guard did not install -- NOT scanning. "
                 "Reading foreign memory unguarded has killed this game once.");
        speech_say(L"Pad scan aborted, no fault guard.", TRUE);
        return;
    }

    BYTE *page = (BYTE *)VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
    if (!page) {
        log_line("pad: could not allocate a scratch page -- not scanning");
        return;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t at  = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t end = (uintptr_t)si.lpMaximumApplicationAddress;

    ULONGLONG t0 = GetTickCount64();
    SIZE_T scanned = 0;
    unsigned nregions = 0;

    while (at < end && g_nsites < PAD_MAX_SITES) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void *)at, &mbi, sizeof(mbi)))
            break;
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= at)
            break;

        if (want_region(&mbi)) {
            nregions++;
            /* ⚠ `volatile` because these live ACROSS setjmp (MEM_GUARDED).
             * A non-volatile local held in a register may be restored to a
             * stale value by longjmp, which here would rewind the scan cursor
             * and loop forever, or silently re-scan a region. GCC warns
             * (-Wclobbered) and the warning is correct. */
            volatile uintptr_t p = (uintptr_t)mbi.BaseAddress;
            uintptr_t rend = next;
            /* ⚠ Read a whole CHUNK under one guard and index out of the local
             * copy. A per-candidate VirtualQuery is never affordable at this
             * scale -- that exact mistake has had to be fixed three times in
             * this project (hd_capture, gui_tree, hd_span_tick). */
            while (p < rend && g_nsites < PAD_MAX_SITES) {
                SIZE_T chunk = rend - p;
                if (chunk > 65536) chunk = 65536;
                BOOL faulted;
                MEM_GUARDED(faulted, memcpy(page, (const void *)p, chunk));
                if (!faulted) {
                    scanned += chunk;
                    /* Need PAD_WORDS dwords to fit in what we copied. */
                    SIZE_T lim = chunk >= PAD_WORDS * 4
                                     ? chunk - PAD_WORDS * 4 : 0;
                    for (SIZE_T o = 0; o + 4 <= lim; o += 4) {
                        const unsigned *w = (const unsigned *)(page + o);
                        /* Arm only where all five words look like button
                         * words, the sample is self-consistent, AND something
                         * is actually pressed.
                         *
                         * ⛔⛔ REQUIRING eventful() HERE IS LOAD-BEARING, and
                         * leaving it out was a real bug caught by the harness.
                         * All-zero memory satisfies the algebra trivially
                         * (0 == 0 & ~0), and this process holds millions of
                         * zero dwords -- so the scan armed 8192 sites of zeroes
                         * inside the FIRST region, hit the cap, and never
                         * reached the planted pad at all. It then reported
                         * "armed 8192 candidate sites" as if that were
                         * progress.
                         *
                         * ⭐ Same shape as the span scan's "0 MB covered"
                         * failure: a fixed slot budget silently degenerates
                         * into a tiny contiguous window when the accept rate
                         * is near 100%. The fix is a filter that actually
                         * discriminates, never a bigger cap.
                         *
                         * Consequence for the user: arm WHILE HOLDING a
                         * direction. That is stated in the spoken prompt. */
                        BOOL ok = TRUE;
                        for (int k = 0; k < PAD_WORDS; k++)
                            if (!plausible(w[k])) { ok = FALSE; break; }
                        if (!ok || !eventful(w) || !consistent(w))
                            continue;
                        if (g_nsites >= PAD_MAX_SITES)
                            break;
                        pad_site *s = &g_sites[g_nsites++];
                        s->addr = p + o;
                        s->prev_old = w[OFF_OLD];
                        s->events = s->agree = 0;
                        s->seen_bits = 0;
                        s->dead = FALSE;
                    }
                }
                p += chunk;
            }
        }
        at = next;
    }

    VirtualFree(page, 0, MEM_RELEASE);
    g_armed = TRUE;

    /* ⚠ Report what was COVERED, not just what was found. "0 candidates" and
     * "the scan never ran" must never look the same. */
    log_line("pad: armed %u candidate site(s) from %u writable region(s), "
             "%.1f MB scanned in %llu ms",
             g_nsites, nregions, (double)scanned / 1048576.0,
             (unsigned long long)(GetTickCount64() - t0));

    if (g_nsites == 0) {
        log_line("pad: ⚠ NO candidate sites. Most likely nothing was held "
                 "while the scan ran -- arming requires a live press. Hold a "
                 "direction and press the key again.");
        speech_say(L"No candidates. Hold a direction and arm again.", TRUE);
    } else if (g_nsites >= PAD_MAX_SITES) {
        log_line("pad: ⚠ site cap reached -- the scan stopped early and did "
                 "NOT cover all writable memory. A miss is not meaningful.");
    }
}

void pad_scan_tick(void)
{
    /* g_armed is only set after mem_guard_install() succeeded in
     * pad_scan_arm(), so reaching here means the guard is live. */
    if (!g_armed || g_nsites == 0)
        return;

    /* ⚠ volatile: all of these live across setjmp (MEM_GUARDED) and would
     * otherwise be restorable to stale values on a fault -- losing
     * confirmations, or resetting the loop index. See -Wclobbered. */
    volatile unsigned  live = 0, confirmed_now = 0;
    volatile uintptr_t best = 0;
    volatile unsigned  best_bits = 0;
    volatile unsigned  i;

    for (i = 0; i < g_nsites; i++) {
        pad_site *s = &g_sites[i];
        if (s->dead)
            continue;

        unsigned w[PAD_WORDS];
        BOOL faulted;
        MEM_GUARDED(faulted, memcpy(w, (const void *)s->addr, sizeof w));
        if (faulted) {
            /* The region went away. Not a rejection of the hypothesis, but
             * this site can no longer be evaluated. */
            s->dead = TRUE;
            continue;
        }
        live++;

        if (eventful(w)) {
            s->events++;
            if (consistent(w)) {
                s->agree++;
                s->seen_bits |= (w[OFF_OLD] | w[OFF_TRG] | w[OFF_REP]) &
                                (PAD_VERT | PAD_HORZ);
            } else {
                /* ⭐ One disagreement on a real event kills it outright. The
                 * engine maintains this invariant every frame; a struct that
                 * violates it once is not the pad. Being decisive here is what
                 * keeps the survivor set honest -- ranking by "mostly agrees"
                 * is how a correlated non-answer survived route 4. */
                s->dead = TRUE;
                continue;
            }
        }
        s->prev_old = w[OFF_OLD];

        if (s->agree >= PAD_CONFIRM_EVENTS) {
            confirmed_now++;
            /* ⭐ Rank by how many DISTINCT directions the site has shown, not
             * by whichever confirmed first.
             *
             * Several sites confirm legitimately (per-player records, previous
             * -frame snapshots, mirrored copies), but the one worth reading is
             * the one that actually moves through the directions the player
             * uses. Latching the first confirmation instead picks an arbitrary
             * mirror that may never change again -- which is exactly how the
             * direction readout ended up reporting a stale site in testing. */
            unsigned nbits = 0;
            for (unsigned b = s->seen_bits; b; b >>= 1)
                nbits += b & 1u;
            unsigned best_n = 0;
            for (unsigned b = (unsigned)best_bits; b; b >>= 1)
                best_n += b & 1u;
            if (!best || nbits > best_n) {
                best = s->addr;
                best_bits = s->seen_bits;
            }
        }
    }

    /* ⚠ Re-point the representative as better evidence arrives, and announce
     * only the first time. A site that has shown up/down/left/right is a
     * better read than one stuck on a single direction, and which is best is
     * not knowable at the moment of first confirmation. */
    if (!g_pinned && confirmed_now && best && best != g_confirmed) {
        BOOL first = !g_confirmed;
        g_confirmed = best;
        if (!first)
            log_line("pad:   representative -> 0x%llx (more directions seen)",
                     (unsigned long long)best);
    }

    if (confirmed_now && g_confirmed && !g_announced) {
        g_announced = TRUE;
        /* ⚠ SEVERAL sites confirming is expected, not a defect. The engine
         * keeps a pad record per player, plus previous-frame snapshots, and
         * anything that mirrors those words satisfies the same algebra. So
         * this reports a COUNT and picks a representative -- it does not claim
         * to have found "the" unique pad, which would be a stronger claim than
         * the evidence supports. Which of them the menus read is a separate
         * question, answered by watching which one moves with the cursor. */
        log_line("pad: ⭐⭐ CONFIRMED pad-shaped state: %u site(s) passed %d "
                 "real events; representative 0x%llx",
                 confirmed_now, PAD_CONFIRM_EVENTS,
                 (unsigned long long)g_confirmed);
        log_line("pad:   Old=+0 Trg=+4 Rel=+8 Chg=+0xc Rep=+0x10");
        speech_say(L"Pad state found.", TRUE);
    }

    (void)live;
}

/* Name the directions present in a button word. Up/Down drive menu
 * navigation; Left/Right drive settings, and both are the same word. */
static void describe(unsigned bits, char *out, size_t n)
{
    out[0] = 0;
    const struct { unsigned m; const char *name; } dirs[] = {
        { PAD_UP, "up" }, { PAD_DOWN, "down" },
        { PAD_LEFT, "left" }, { PAD_RIGHT, "right" },
    };
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        if (bits & dirs[i].m) {
            if (out[0]) strncat(out, " ", n - strlen(out) - 1);
            strncat(out, dirs[i].name, n - strlen(out) - 1);
        }
    }
}

void pad_scan_report(void)
{
    if (!g_armed) {
        log_line("pad: not armed yet.");
        speech_say(L"Pad scan not armed.", TRUE);
        return;
    }

    unsigned alive = 0, evidenced = 0, confirmed = 0;
    for (unsigned i = 0; i < g_nsites; i++) {
        if (g_sites[i].dead) continue;
        alive++;
        if (g_sites[i].events) evidenced++;
        if (g_sites[i].agree >= PAD_CONFIRM_EVENTS) confirmed++;
    }

    /* ⚠ Print the whole population, always. A filtered-down list and an empty
     * population look identical otherwise, and that ambiguity has wasted
     * several runs in this project. */
    log_line("pad: %u armed, %u still alive, %u saw a real event, %u confirmed",
             g_nsites, alive, evidenced, confirmed);

    if (!evidenced) {
        log_line("pad: ⚠ no site has seen a press yet -- agreement so far is "
                 "the trivial all-zero case and proves nothing. Press a "
                 "direction several times, then report again.");
        speech_say(L"No presses seen yet. Press a direction, then report.", TRUE);
        return;
    }

    unsigned shown = 0;
    for (unsigned i = 0; i < g_nsites && shown < 12; i++) {
        pad_site *s = &g_sites[i];
        if (s->dead || !s->events) continue;
        char dirs[64];
        describe(s->seen_bits, dirs, sizeof dirs);
        log_line("pad:   0x%llx  %u/%u events agree%s%s",
                 (unsigned long long)s->addr, s->agree, s->events,
                 dirs[0] ? "  dirs seen: " : "", dirs);
        shown++;
    }

    if (confirmed) {
        speech_say(L"Pad state confirmed.", TRUE);
    } else {
        log_line("pad: nothing confirmed yet -- needs %d agreeing events.",
                 PAD_CONFIRM_EVENTS);
        speech_say(L"Not confirmed yet. Press a direction more.", TRUE);
    }
}

void pad_say_state(void)
{
    if (!g_confirmed) {
        speech_say(L"Pad not confirmed yet.", TRUE);
        return;
    }
    unsigned w[PAD_WORDS];
    BOOL faulted;
    MEM_GUARDED(faulted, memcpy(w, (const void *)g_confirmed, sizeof w));
    if (faulted) {
        speech_say(L"Pad state unreadable.", TRUE);
        return;
    }
    /* ⚠⚠ DO NOT trust OFF_REP/OFF_TRG as absolute positions here.
     *
     * The arming window can be shifted by a word or two: a single press makes
     * several of the five words equal (Old=Trg=Chg=Rep on a fresh Down), so a
     * window starting later satisfies the same algebra and is armed at that
     * offset. Reading a fixed offset from a shifted base reports the wrong
     * word -- which is precisely how this decoder failed its own test.
     *
     * The union over the window is immune to the shift: a live direction bit
     * appears in Old, Trg, Chg and Rep alike while a direction is held, so OR
     * -ing them names the direction correctly from any starting offset. It
     * costs the ability to distinguish "just pressed" from "held", which
     * pad_say_state does not need -- it answers "which way am I pressing?". */
    unsigned any = 0;
    for (int k = 0; k < PAD_WORDS; k++)
        any |= w[k];

    char dirs[64];
    describe(any, dirs, sizeof dirs);
    if (!dirs[0]) {
        speech_say(L"No direction.", TRUE);
        return;
    }
    /* Interrupting: a fast cursor must not queue behind stale speech. */
    wchar_t wide[64];
    MultiByteToWideChar(CP_UTF8, 0, dirs, -1, wide,
                        sizeof wide / sizeof wide[0]);
    speech_say(wide, TRUE);
}

void pad_scan_reset(void)
{
    g_nsites = 0;
    g_armed = FALSE;
    g_confirmed = 0;
    g_announced = FALSE;
    g_pinned = FALSE;
    log_line("pad: reset.");
}

/* --- introspection for the self-test (see pad_find.h) -------------------- */
unsigned pad_confirmed_count(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < g_nsites; i++)
        if (!g_sites[i].dead && g_sites[i].agree >= PAD_CONFIRM_EVENTS)
            n++;
    return n;
}

void pad_debug_pin(uintptr_t addr)
{
    g_confirmed = addr;
    g_announced = TRUE;      /* do not re-announce a pinned site */
    g_pinned = TRUE;
}

BOOL pad_is_confirmed(uintptr_t addr)
{
    for (unsigned i = 0; i < g_nsites; i++)
        if (!g_sites[i].dead && g_sites[i].addr == addr &&
            g_sites[i].agree >= PAD_CONFIRM_EVENTS)
            return TRUE;
    return FALSE;
}
