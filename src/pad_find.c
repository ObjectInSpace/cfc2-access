#include "pad_find.h"
#include "memsafe.h"
#include "log.h"
#include "speech.h"

#include <stdio.h>
#include <string.h>

/* Field indices within the observed window: On Old Trg Rel Chg. */
#define W_ON  0
#define W_OLD 1
#define W_TRG 2
#define W_REL 3
#define W_CHG 4

/* Candidates kept from one seeding sweep.
 *
 * With the all-zero window rejected (see the sweep), a match requires a button
 * to be down, so the real count is small -- this is headroom, not a budget the
 * scan is expected to reach. If it ever binds, the log says so and the
 * coverage claim is withdrawn rather than quietly overstated. */
#define PAD_MAX_SITES 65536

/* Distinct transitions a site must survive before the record is reported.
 *
 * ⚠ TWO IS THE MEASURED MINIMUM, not a guess. One press makes several words
 * equal (On=Trg=Chg=0x44, Old=Rel=0), so offsets +0, +4 and +16 all satisfy
 * the invariant; a second, DIFFERENT transition leaves exactly one. Three is
 * used here for margin, since the game's neighbouring pad records and analog
 * axes produce more coincidences than the offline model does. */
#define PAD_CONFIRM_EVENTS 3

typedef struct {
    uintptr_t addr;      /* address of the `On` word */
    unsigned  last_on;   /* On as of the previous sample */
    unsigned  events;    /* distinct transitions witnessed */
    BOOL      dead;
} pad_site;

static pad_site  *g_sites;      /* heap: 65536 entries is too big for BSS */
static unsigned   g_nsites;
static unsigned   g_alive;
static BOOL       g_scanning;
static uintptr_t  g_record;     /* identified `On` address, or 0 */
static BOOL       g_swept;      /* the first sweep has been announced */

/* The engine's invariant. Five observed words, three equations, nothing
 * derived -- see the header for why that matters. */
static BOOL consistent(const unsigned w[PAD_WORDS])
{
    unsigned on = w[W_ON], old = w[W_OLD];
    return w[W_TRG] == (on & ~old) &&
           w[W_REL] == (old & ~on) &&
           w[W_CHG] == (on ^ old);
}

/* Could these be button words? Buttons occupy the low bits; a float, pointer
 * half, timer or counter almost always sets something in the top byte. */
static BOOL plausible(const unsigned w[PAD_WORDS])
{
    for (int k = 0; k < PAD_WORDS; k++)
        if (w[k] & 0xFF000000u)
            return FALSE;
    return TRUE;
}

/* Live state must be written by the game, so it is in private writable
 * memory -- never a mapped image. */
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
    return mbi->Type != MEM_IMAGE;
}

void pad_scan_start(void)
{
    if (!g_sites) {
        g_sites = (pad_site *)VirtualAlloc(
            NULL, (SIZE_T)PAD_MAX_SITES * sizeof *g_sites,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_sites) {
            log_line("pad: could not allocate the candidate table");
            return;
        }
    }
    g_nsites = 0;
    g_alive = 0;
    g_record = 0;
    g_scanning = FALSE;

    /* ⚠⚠ MEM_GUARDED is inert unless the handler is installed, and an inert
     * guard is worse than none: every read merely LOOKS protected. This module
     * once used the macro without arming it and died with 0xC0000005. Install
     * self-tests by faulting deliberately, so a guard that would not have
     * worked refuses to proceed. */
    if (!mem_guard_install()) {
        log_line("pad: ⛔ fault guard did not install -- NOT scanning.");
        speech_say(L"Pad scan aborted, no fault guard.", TRUE);
        return;
    }

    /* ⭐ Announce on entry, before the sweep. A hotkey that buys silence has
     * cost this project several diagnostic rounds: "stalled", "crashed" and
     * "the user quit early" are indistinguishable otherwise.
     *
     * ⚠ Only the FIRST sweep announces. This re-seeds several times a second
     * until a press makes the record visible, and narrating every attempt
     * would bury the log and talk over the player continuously. */
    if (!g_swept) {
        log_line("pad: seeding -- scanning writable memory for the sPad::Pad "
                 "invariant (On/Old/Trg/Rel/Chg). Re-seeds until a press "
                 "makes the record visible; just navigate.");
        speech_say(L"Scanning for the pad record.", TRUE);
    }

    BYTE *buf = (BYTE *)VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
    if (!buf) {
        log_line("pad: could not allocate a scratch buffer -- not scanning");
        return;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t at = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t end = (uintptr_t)si.lpMaximumApplicationAddress;

    ULONGLONG t0 = GetTickCount64();
    SIZE_T scanned = 0;
    unsigned nregions = 0;

    while (at < end && g_nsites < PAD_MAX_SITES) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void *)at, &mbi, sizeof mbi))
            break;
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= at)
            break;

        if (want_region(&mbi)) {
            nregions++;
            /* volatile: lives across setjmp, so longjmp could otherwise
             * restore a stale value and rewind the cursor (-Wclobbered). */
            volatile uintptr_t p = (uintptr_t)mbi.BaseAddress;
            while (p < next && g_nsites < PAD_MAX_SITES) {
                SIZE_T chunk = next - (uintptr_t)p;
                if (chunk > 65536) chunk = 65536;
                BOOL faulted;
                MEM_GUARDED(faulted,
                            memcpy(buf, (const void *)(uintptr_t)p, chunk));
                if (!faulted) {
                    scanned += chunk;
                    SIZE_T lim = chunk >= PAD_WORDS * 4
                                     ? chunk - PAD_WORDS * 4 : 0;
                    for (SIZE_T o = 0; o + 4 <= lim; o += 4) {
                        const unsigned *w = (const unsigned *)(buf + o);
                        /* ⛔⛔ REJECT THE ALL-ZERO WINDOW. This is load-bearing.
                         *
                         * All-zero satisfies the invariant trivially, and a
                         * process is mostly zeros: measured 57k matches in
                         * 1.8 MB of the test harness, which extrapolates to
                         * ~45 MILLION over this game's ~1.5 GB. The candidate
                         * table would fill in the first few megabytes and
                         * never reach the pad -- the same "a fixed budget
                         * silently becomes a tiny window" failure this project
                         * has already hit twice, just relocated.
                         *
                         * ⭐ The fix is a filter that DISCRIMINATES, never a
                         * bigger cap. Nothing is lost by rejecting zeros: an
                         * idle pad carries no information either, and the
                         * sweep simply re-runs until a press makes the record
                         * visible (see the caller's re-seed loop). */
                        if (!w[W_ON] && !w[W_OLD] && !w[W_TRG] &&
                            !w[W_REL] && !w[W_CHG])
                            continue;
                        if (!plausible(w) || !consistent(w))
                            continue;
                        if (g_nsites >= PAD_MAX_SITES)
                            break;
                        pad_site *s = &g_sites[g_nsites++];
                        s->addr = (uintptr_t)p + o;
                        s->last_on = w[W_ON];
                        s->events = 0;
                        s->dead = FALSE;
                    }
                }
                p += chunk;
            }
        }
        at = next;
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    g_alive = g_nsites;
    g_scanning = TRUE;

    /* ⚠ Report COVERAGE, not just the count. "0 candidates" and "the scan
     * never really ran" must never look the same -- a fixed slot budget
     * silently becomes a tiny contiguous window when the accept rate is high,
     * which has happened in this project and reported itself as progress. */
    if (!g_swept || g_nsites)
        log_line("pad: seeded %u candidate(s) from %u region(s), %.1f MB in "
                 "%llu ms", g_nsites, nregions, (double)scanned / 1048576.0,
                 (unsigned long long)(GetTickCount64() - t0));
    g_swept = TRUE;
    if (g_nsites >= PAD_MAX_SITES)
        log_line("pad: ⚠ candidate cap reached -- coverage is INCOMPLETE, so "
                 "a failure to identify the record is not meaningful.");
    if (g_nsites == 0)
        ;   /* silent: re-seeding is the normal case between presses */
    else
        log_line("pad: now just navigate; further transitions eliminate the "
                 "coincidental matches.");
}

/* Re-seed while the sweep comes up empty.
 *
 * A candidate is only visible while a button is actually down, and the sweep
 * takes a fraction of a second, so most sweeps land in a gap between presses
 * and legitimately find nothing. Retrying is what removes the "hold a
 * direction while you press the hotkey" protocol -- ⭐ a measurement that
 * needs the user to remember something is the wrong measurement; instrument it
 * instead.
 *
 * Called from the worker loop, rate-limited so it cannot dominate the tick. */
void pad_scan_poll(void)
{
    static ULONGLONG last;
    if (!g_scanning || g_record || g_nsites)
        return;
    ULONGLONG now = GetTickCount64();
    if (now - last < 250)
        return;
    last = now;
    pad_scan_start();
}

void pad_scan_tick(void)
{
    if (!g_scanning || g_record)
        return;

    /* volatile: these live across setjmp and would otherwise be restorable to
     * stale values on a fault, losing progress or resetting the index. */
    volatile unsigned  alive = 0, best_events = 0;
    volatile uintptr_t best = 0;
    volatile unsigned  i;

    for (i = 0; i < g_nsites; i++) {
        pad_site *s = &g_sites[i];
        if (s->dead)
            continue;

        unsigned w[PAD_WORDS];
        BOOL faulted;
        MEM_GUARDED(faulted, memcpy(w, (const void *)s->addr, sizeof w));
        if (faulted) {
            s->dead = TRUE;      /* region went away; cannot be evaluated */
            continue;
        }

        /* A TRANSITION is what carries information. While nothing is pressed
         * every word is zero and the invariant holds for any five zero words,
         * so quiet samples must never count as evidence. */
        if (w[W_ON] != s->last_on) {
            if (!consistent(w)) {
                /* ⭐ One violation on a real transition is decisive: the
                 * engine maintains this every frame. Being strict is what
                 * keeps the survivor set honest -- "mostly agrees" ranking is
                 * how route 4's monotonic counter survived as long as it did. */
                s->dead = TRUE;
                continue;
            }
            s->events++;
            s->last_on = w[W_ON];
        }

        alive++;
        if (s->events > best_events) {
            best_events = s->events;
            best = s->addr;
        }
    }
    g_alive = alive;

    if (best_events >= PAD_CONFIRM_EVENTS) {
        unsigned n = 0;
        for (unsigned k = 0; k < g_nsites; k++)
            if (!g_sites[k].dead && g_sites[k].events >= PAD_CONFIRM_EVENTS)
                n++;
        g_record = (uintptr_t)best;
        /* ⚠ Several sites can legitimately reach the threshold: the engine
         * keeps a record per pad. Report the count rather than implying the
         * answer is unique. */
        log_line("pad: ⭐⭐ record identified at 0x%llx after %u distinct "
                 "transition(s); %u site(s) reached the threshold",
                 (unsigned long long)g_record, (unsigned)best_events, n);
        log_line("pad:   On=+0 Old=+4 Trg=+8 Rel=+0xc Chg=+0x10 Rep=+0x14");
        speech_say(L"Pad record found.", TRUE);
    }
}

/* Name the directions in a button word. Up/Down drive menu navigation,
 * Left/Right drive settings, and both come from this same word. */
static void describe(unsigned bits, char *out, size_t n)
{
    static const struct { unsigned m; const char *name; } dirs[] = {
        { PAD_UP, "up" }, { PAD_DOWN, "down" },
        { PAD_LEFT, "left" }, { PAD_RIGHT, "right" },
    };
    out[0] = 0;
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        if (bits & dirs[i].m) {
            if (out[0])
                strncat(out, " ", n - strlen(out) - 1);
            strncat(out, dirs[i].name, n - strlen(out) - 1);
        }
    }
}

void pad_scan_report(void)
{
    if (!g_scanning) {
        log_line("pad: not scanning.");
        speech_say(L"Pad scan not started.", TRUE);
        return;
    }

    log_line("pad: %u seeded, %u still alive, record %s",
             g_nsites, g_alive,
             g_record ? "IDENTIFIED" : "not identified yet");

    if (g_record) {
        unsigned w[PAD_WORDS];
        BOOL faulted;
        MEM_GUARDED(faulted, memcpy(w, (const void *)g_record, sizeof w));
        if (!faulted) {
            char dirs[64];
            describe(w[W_ON], dirs, sizeof dirs);
            log_line("pad:   On=0x%02x Old=0x%02x Trg=0x%02x  currently: %s",
                     w[W_ON], w[W_OLD], w[W_TRG], dirs[0] ? dirs : "(none)");
        }
        speech_say(L"Pad record identified.", TRUE);
        return;
    }

    /* ⚠ Distinguish "nothing has happened yet" from "everything was
     * eliminated". Those mean opposite things and looked identical in an
     * earlier version of this module. */
    unsigned with_events = 0;
    for (unsigned k = 0; k < g_nsites; k++)
        if (!g_sites[k].dead && g_sites[k].events)
            with_events++;
    if (!with_events)
        log_line("pad: no candidate has seen a transition yet -- press a "
                 "direction a few times, then report again.");
    else
        log_line("pad: %u candidate(s) have seen transitions; need %d "
                 "distinct ones to identify.", with_events,
                 PAD_CONFIRM_EVENTS);
    speech_say(L"Not identified yet.", TRUE);
}

void pad_say_state(void)
{
    if (!g_record) {
        speech_say(L"Pad record not found yet.", TRUE);
        return;
    }
    unsigned w[PAD_WORDS];
    BOOL faulted;
    MEM_GUARDED(faulted, memcpy(w, (const void *)g_record, sizeof w));
    if (faulted) {
        speech_say(L"Pad record unreadable.", TRUE);
        return;
    }
    char dirs[64];
    describe(w[W_ON], dirs, sizeof dirs);
    if (!dirs[0]) {
        speech_say(L"No direction.", TRUE);
        return;
    }
    wchar_t wide[64];
    MultiByteToWideChar(CP_UTF8, 0, dirs, -1, wide,
                        sizeof wide / sizeof wide[0]);
    speech_say(wide, TRUE);      /* interrupting: keep up with the cursor */
}

uintptr_t pad_record(void)
{
    return g_record;
}
