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
/* Samples a site must agree with the KEYBOARD before it is reported.
 *
 * This is not a shape threshold -- it counts agreements with ground truth
 * observed outside the process. A decoy has no way to track the arrow keys,
 * so a handful of agreements across changing directions is decisive where no
 * amount of pattern-matching was. */
#define PAD_AGREE_NEEDED 6

typedef struct {
    uintptr_t addr;      /* address of the `On` word */
    unsigned  agree;     /* samples where the record matched the keyboard */
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

/* Opposite directions cannot be held at once.
 *
 * ⛔⛔ THIS IS WHAT THE INVARIANT ALONE CANNOT DO, and leaving it out shipped a
 * false positive to the user. The three equations are trivially satisfied by a
 * huge family of ordinary memory: whenever On == Old, they force
 * Trg = Rel = Chg = 0, so ANY pair of equal words followed by three zeros
 * passes -- a value plus its shadow copy, then padding. That shape is
 * everywhere. My "~2^-96 per sample" estimate assumed five INDEPENDENT random
 * words and was simply wrong.
 *
 * The observed failure: the scan locked onto such a site whose value was
 * 0xFF, and the readout said "up down left right" -- all four at once.
 *
 * A physical d-pad or stick cannot report up AND down, or left AND right. That
 * is a fact about the hardware, not a heuristic, and it rejects the entire
 * degenerate family the moment its value has opposing bits set. Diagonals
 * (e.g. down+right) are legitimate and are kept. */
static BOOL sane_dirs(unsigned v)
{
    if ((v & PAD_UP) && (v & PAD_DOWN))
        return FALSE;
    if ((v & PAD_LEFT) && (v & PAD_RIGHT))
        return FALSE;
    return TRUE;
}

/* Could these be button words? Buttons occupy the low bits; a float, pointer
 * half, timer or counter almost always sets something in the top byte. */
static BOOL plausible(const unsigned w[PAD_WORDS])
{
    for (int k = 0; k < PAD_WORDS; k++)
        if (w[k] & 0xFF000000u)
            return FALSE;
    /* Both the current and previous states must be physically possible. */
    return sane_dirs(w[W_ON]) && sane_dirs(w[W_OLD]);
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
                        s->agree = 0;
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

/* Collapse a button word to one bit per DIRECTION.
 *
 * ⚠ The record sets either the D-pad bit or the stick bit for a given
 * direction, never both, while the keyboard only tells us "down". Comparing
 * raw masks therefore fails against the real pad -- 0x04 (Rdown) is not equal
 * to PAD_DOWN (0x44). Compare which directions are present instead. */
#define DIR_UP    1u
#define DIR_DOWN  2u
#define DIR_LEFT  4u
#define DIR_RIGHT 8u

static unsigned dir_set(unsigned bits)
{
    unsigned d = 0;
    if (bits & PAD_UP)    d |= DIR_UP;
    if (bits & PAD_DOWN)  d |= DIR_DOWN;
    if (bits & PAD_LEFT)  d |= DIR_LEFT;
    if (bits & PAD_RIGHT) d |= DIR_RIGHT;
    return d;
}

void pad_scan_tick(unsigned player_dirs)
{
    if (!g_scanning || g_record)
        return;

    /* Only samples where the player is actually holding a direction carry
     * information. With nothing held, every quiet site "agrees" trivially. */
    if (!player_dirs)
        return;

    /* volatile: live across setjmp, so longjmp could restore stale values. */
    volatile unsigned  alive = 0, best = 0;
    volatile uintptr_t winner = 0;
    volatile unsigned  i;

    for (i = 0; i < g_nsites; i++) {
        pad_site *s = &g_sites[i];
        if (s->dead)
            continue;

        unsigned w[PAD_WORDS];
        BOOL faulted;
        MEM_GUARDED(faulted, memcpy(w, (const void *)s->addr, sizeof w));
        if (faulted) {
            s->dead = TRUE;
            continue;
        }

        /* ⭐⭐ THE TEST: does this record say what the KEYBOARD says?
         *
         * The direction bits the player is holding must be exactly the
         * direction bits the record reports. A record that reports nothing
         * while Down is held, or reports Up while Down is held, is not the
         * pad -- whatever its internal algebra looks like.
         *
         * Compared over the direction bits only: the record legitimately
         * carries face buttons and other bits we are not driving. */
        if (dir_set(w[W_ON]) != dir_set(player_dirs)) {
            s->dead = TRUE;
            continue;
        }
        /* Still require the record to be internally coherent. */
        if (!consistent(w)) {
            s->dead = TRUE;
            continue;
        }

        s->agree++;
        alive++;
        if (s->agree > best) {
            best = s->agree;
            winner = s->addr;
        }
    }
    g_alive = alive;

    if (best >= PAD_AGREE_NEEDED) {
        unsigned n = 0;
        for (unsigned k = 0; k < g_nsites; k++)
            if (!g_sites[k].dead && g_sites[k].agree >= PAD_AGREE_NEEDED)
                n++;
        g_record = (uintptr_t)winner;
        log_line("pad: ⭐⭐ record identified at 0x%llx -- agreed with the "
                 "keyboard %u time(s); %u site(s) qualified",
                 (unsigned long long)g_record, (unsigned)best, n);
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
        if (!g_sites[k].dead && g_sites[k].agree)
            with_events++;
    if (!with_events)
        log_line("pad: no candidate has agreed with the keyboard yet -- hold "
                 "an arrow key while the menus are up, then report again.");
    else
        log_line("pad: %u candidate(s) agree so far; need %d agreements.",
                 with_events, PAD_AGREE_NEEDED);
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

BOOL pad_window_acceptable(const unsigned w[PAD_WORDS])
{
    if (!w[W_ON] && !w[W_OLD] && !w[W_TRG] && !w[W_REL] && !w[W_CHG])
        return FALSE;                      /* all-zero matches trivially */
    return plausible(w) && consistent(w);
}
