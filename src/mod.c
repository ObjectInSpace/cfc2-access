/*
 * CFC2 Access — mod core.
 *
 * Stage 0 (does an injected DLL survive Enigma? can we speak? can we change
 * page protection?) is PASSED and its findings are recorded. This file now
 * serves PHASE 1.3: capture the unpacked image so the shell's GMD text-lookup
 * function can be located with ordinary offline tools.
 *
 * Why a dump rather than a live pattern scan: the exe on disk is entropy-8.00
 * ciphertext, so static analysis of it is worthless, while a live scan can only
 * ever show what we thought to log in advance. Dumping the decrypted image once
 * converts an intractable problem into a normal one -- and because ASLR is off
 * and the exe carries no relocations, every address found in that dump is valid
 * in every future run. The same dump serves the later emulator-RAM work.
 */

#include "mod.h"
#include "log.h"
#include "speech.h"
#include "cvs2_select.h"
#include "cvs1_select.h"
#include "cfj_select.h"
#include "z3u_select.h"
#include "mjus_select.h"
#include "stg2_select.h"
#include "cvs2_hit.h"
#include "audio.h"
#include "cvs2_frame.h"
#include "cvs1_frame.h"
#include "cvs1_text.h"
#include "cfj_text.h"
#include "z3u_text.h"
#include "cfj_frame.h"
#include "z3u_frame.h"
#include "mjus_frame.h"
#include "stg2_frame.h"
#include "pws1_frame.h"
#include "pws1_select.h"
#include "pws2_frame.h"
#include "pws2_select.h"
#include "mjus_text.h"
#include "mjus_story.h"
#include "cvs2_story.h"
#include "cvs2_text.h"
#include "text_channel.h"
#include "collection_probe.h"
#include "image.h"
#include "gmd_find.h"
#include "pad_watch.h"
#include "kbd_state.h"
#include "emu_cursor.h"
#include "gui_text.h"
#include "menu_cursor.h"
#include "menu_speak.h"   /* menu_speak_set_discovery() */
#include "emu_ram.h"      /* the emulated machine's state (world 2)         */
#include "mod_text.h"     /* the mod's own words, in the game's language    */
#include "gmd_find.h"     /* gmd_text_language()                            */

#include <stdio.h>

/* --- The emulator probe (world 2, stage 1) -------------------------------
 * Read-only: two pointer reads and some dwords. No hooks, no writes, no calls
 * into game code. Hooking has crashed this game before, and nothing in stages
 * 1-4 of the in-game work needs one.
 *
 * ⭐⭐ WHAT A NEGATIVE LOOKS LIKE, stated up front so a null result cannot be
 * misread as a broken probe -- the distinction this project has paid for more
 * than once:
 *
 *   "no game is loaded"   -> the slot is null. A REAL answer; expected on the
 *                            title screen and in every shell menu.
 *   "COULD NOT READ"      -> we never looked. NOT a negative; the anchor or
 *                            the memory is wrong and nothing may be concluded.
 *   "UNVERIFIED"          -> we looked, found something, and it FAILED its own
 *                            content check. The anchor is suspect.
 *
 * ⚠ The falsifiable check is the CROSS-CHECK: the ROM index cached at
 * state+0x44 is produced by the game's own rom_index_from_name() over the
 * table at 0x141208b00, so the name it implies must equal the live string at
 * state+0x30. If those two disagree, the field map is wrong -- and the probe
 * says so rather than printing a plausible line. */
static BOOL g_emu_probe_on;

/* ⭐ The guest-RAM hunt. Only meaningful for CVS2 (the signature is that game's
 * published player layout), and only DURING A MATCH -- on a menu screen the
 * fighters do not exist and finding nothing is the CORRECT answer, not a
 * failure. The log says so, because "expected negative" and "broken scanner"
 * are otherwise the same line.
 *
 * ⚠ Stays quiet while it keeps finding nothing: a sweep every 10 s that
 * reported every time would flood the log and bury the round that matters.
 * It speaks on a CHANGE in what it found, and once at the start. */
static void emu_watch_report_log(void);

/* ⭐⭐ THE DECISIVE TEST, and now the PRIMARY one. Four play sessions went into
 * the relational scan without producing a usable conclusion, because
 * "P2 == P1 + 0x5CC and health <= max" is satisfied by huge amounts of
 * ordinary data. This asks a falsifiable question instead, using the published
 * CVS2 full-health constant (144), and one run answers it either way. */
static void emu_cap_probe_log(void)
{
    /* ⛔⛔ THIS RAN ONCE AND THAT WAS THE BUG. `static int done` fired the
     * probe at the FIRST moment a game was detected -- i.e. the loading
     * screen, before the fighters exist in RAM. The user was genuinely in a
     * match and the probe had already spent its single shot 2 s after the ROM
     * registered.
     *
     * ⭐ Same error as the identity-vs-scan cadence two rounds ago: a one-shot
     * report cannot observe a state that arrives LATER. Re-run, and speak only
     * when the answer CHANGES. */
    static unsigned last_paired = 0xFFFFFFFFu;
    unsigned seen = 0, paired = 0;
    ULONGLONG first = 0;
    SIZE_T scanned = 0;
    emu_status s;

    s = emu_ram_probe_cap(144, &seen, &paired, &first, &scanned);
    if (s != EMU_OK) {
        log_line("emu: CAP PROBE could not run (%s) -- says nothing.",
                 emu_status_name(s));
        return;
    }
    if (paired == last_paired)
        return;                     /* nothing new to say */
    last_paired = paired;

    log_line("emu: === CAP PROBE: swept %llu MB for the published CVS2 full-"
             "health value 144 ===",
             (unsigned long long)(scanned / (1024 * 1024)));
    log_line("emu:    %u occurrence(s) of 144; %u of them have a SECOND 144 "
             "exactly 0x5CC later.", seen, paired);

    /* ⛔⛔ "paired > 0" IS NOT CONFIRMATION, and the first version said it was.
     * 144 is a common byte value, so pairs occur by chance in proportion to how
     * many 144s exist. A run reported 15530 occurrences with 13 pairs -- which
     * is the CHANCE rate, not a finding. Only a pair rate far above chance, or
     * a pair that BEHAVES like health, means anything.
     *
     * Report the rate and let it be judged; never announce a conclusion the
     * arithmetic does not support. */
    if (paired > 0) {
        /* Expected pairs if 144s were placed independently: every occurrence
         * has ~ (seen / total_u16_slots) chance of another 144 at +0x5CC.
         * Approximated conservatively as seen^2 / (scanned/2). */
        double slots = (double)scanned / 2.0;
        double expect = slots > 0 ? ((double)seen * (double)seen) / slots : 0.0;
        log_line("emu: %u pair(s) at 0x5CC; ~%.1f expected BY CHANCE from "
                 "%u occurrences in %llu MB.",
                 paired, expect, seen,
                 (unsigned long long)(scanned / (1024 * 1024)));
        if ((double)paired > expect * 4.0 && paired >= 8)
            log_line("emu: ⭐⭐ that is well ABOVE chance -- the 0x5CC stride "
                     "is worth pursuing. First pair at 0x%llx.",
                     (unsigned long long)first);
        else
            log_line("emu: ⚠ that is CONSISTENT WITH CHANCE -- it does NOT "
                     "confirm the stride. ⭐ Note the probe only means "
                     "anything DURING A MATCH; on a menu there are no "
                     "fighters to find.");
    }
    else if (seen > 0)
        log_line("emu: ⛔ 144 occurs but NEVER 0x5CC apart. The health VALUE "
                 "may be right and the STRIDE is wrong for this build -- the "
                 "player struct is a different size here.");
    else
        log_line("emu: ⛔⛔ 144 does NOT occur anywhere in the arena. If this "
                 "was logged during a live match, the published CVS2 layout "
                 "does NOT apply to CFC2's build and the prior-art route is "
                 "dead -- the layout must be derived from scratch.");
}

/* ⭐⭐⭐ THE MAPPING TEST -- a prediction with a KNOWN RIGHT ANSWER, which is
 * what every probe before it lacked. We hold bytes from the shipped ROM whose
 * OWN pointers say they live at guest 0x0CE80000; NAOMI RAM starts at
 * 0x0C000000. If the mapping is linear they are at arena + 0xE80000 -- and if
 * it is not, the sweep finds them anyway and reports the real base. */
static void emu_guest_map_log(void)
{
    /* Re-runs like the cap probe, and speaks only when the ANSWER changes --
     * the fingerprint may be staged early and relocated once the machine is
     * actually running. A one-shot check would see only the staging copy. */
    static ULONGLONG last_at = 1;
    ULONGLONG at = 0, implied = 0;
    BOOL predicted = FALSE;
    SIZE_T scanned = 0;
    emu_status s;

    s = emu_guest_map(&at, &implied, &predicted, &scanned);

    if (s == EMU_UNREADABLE) {
        log_line("emu: MAPPING TEST could not sweep -- says nothing.");
        return;
    }
    if (at == last_at)
        return;
    last_at = at;

    if (s == EMU_NOT_RUNNING) {
        log_line("emu: ⛔ MAPPING TEST -- the ROM fingerprint is NOT in the "
                 "arena (swept %llu MB). The emulator either transforms the "
                 "data on load or keeps guest RAM somewhere else.",
                 (unsigned long long)(scanned / (1024 * 1024)));
        return;
    }

    log_line("emu: ⭐⭐⭐ MAPPING TEST -- ROM fingerprint FOUND at 0x%llx",
             (unsigned long long)at);
    if (predicted)
        log_line("emu: ✅ it is exactly where the LINEAR model predicted "
                 "(arena + 0xE80000). guest->host = host - 0x%llx, so ANY "
                 "published CVS2 guest address is now readable.",
                 (unsigned long long)0x0C000000ULL);
    else
        /* ⛔⛔ THIS LINE USED TO OVERCLAIM, and it read exactly like a finding.
         *
         * It announced an "implied guest-RAM base" from the fingerprint's
         * address. But the 2026-09-16 run found the fingerprint at
         * arena+0x20D800, which is EXACTLY entry[11]'s own AFS file offset --
         * i.e. it re-found the loaded FILE image, which `host = arena +
         * afs_offset` already predicted. The "implied base" it then derived was
         * 0x71BE800, BELOW the arena base, which is impossible for a sub-range.
         *
         * A number that cannot be true must not be printed as a result. */
    {
        ULONGLONG ab = 0; SIZE_T asz = 0;
        (void)emu_arena(&ab, &asz);
        log_line("emu: ⚠ found at 0x%llx = arena + 0x%llx. ⛔ Before treating "
                 "this as guest RAM, check that offset against the AFS table: "
                 "the ROM blob is loaded VERBATIM at the arena base, so a "
                 "fingerprint at arena+<its own AFS offset> is the FILE copy, "
                 "not guest RAM. An implied base BELOW the arena proves it. "
                 "Implied base would be 0x%llx.",
                 (unsigned long long)at,
                 (unsigned long long)(ab ? at - ab : 0),
                 (unsigned long long)implied);
    }
}

/* --- The in-game cursor probe -------------------------------------------
 *
 * Driven by the user simply navigating: LEFT/RIGHT arrow edges advance the
 * phase machine. Nothing is spoken; this establishes whether a cursor value is
 * readable at all, which is the prerequisite for everything downstream.
 *
 * ⚠ The log always states what the NEXT press will do. The user cannot see
 * internal state, and ambiguous presses have cost several runs in this project.
 */
static BOOL     g_cursor_on;
static BOOL     g_cursor_armed;
static DWORD    g_cursor_armed_at;
static unsigned g_cursor_edges;
static BOOL     g_cursor_gave_up;
static char     g_cursor_rom[32];
static unsigned g_cursor_waited_note;
static unsigned g_cursor_reads;
static unsigned g_cursor_unreadable;
static unsigned g_cursor_other;
static int      g_cursor_axis;
static unsigned g_cursor_watch_logged;
static BOOL     g_scan_len_set;
static DWORD    g_last_input_ms;
static DWORD    g_scan_report_ms;
static unsigned g_spr_pairs;
static BOOL     g_pt_done;
static BOOL     g_ram_done;
static DWORD    g_ram_last;
static unsigned g_pt_tries;
static DWORD    g_pt_progress_ms;
static BOOL     g_spr_pending;
static DWORD    g_spr_at;

/* ⭐⭐ THE DISPLAY-LIST DIFF (run 20). See the long note at the capture site.
 *
 * ⚠ 64 KB per slot because run 19 measured the REAL content at 152 words, all
 * at the very start; everything past it was 0xCDCD fill. 2 MB per slot would
 * write 32 MB of fill to carry ~600 bytes of signal.
 *
 * ⚠ 16 slots at ~1 per tick spans several seconds of play, which is enough for
 * the user to cross a dozen characters. The SERIES is the measurement -- a
 * single capture, however large, cannot show a change. */
#define DL_OFFSET   0x8900000ULL
#define DL_LEN      0x10000
#define DL_SLOTS    16
static BOOL     g_dl_armed;
static unsigned g_dl_slot;
static DWORD    g_dl_last;

/* How long to wait for ANY arrow edge before concluding the emulated game does
 * not use sKeyboard. 30 s of navigation is far more than enough. */
#define CURSOR_EDGE_TIMEOUT_MS  30000

static void cursor_probe_report(void)
{
    ec_field f[12];
    ec_report r;
    unsigned n, i;

    emu_cursor_status(&r);
    n = emu_cursor_candidates(f, 12);

    log_line("cursor: cycle %u done in %lu ms -- %u page(s) of %u reacted, "
             "%u kept, %u field(s) examined, %u returned, %u rejected as "
             "counters, %u toggle(s).%s",
             r.cycles_done, (unsigned long)r.last_pass_ms,
             r.pages_reacted, r.pages_total, r.pages_kept,
             r.fields_examined, r.fields_returned, r.fields_counter,
             r.fields_toggle,
             r.truncated ? "  ⚠ A CAP BOUND -- see pages_seen." : "");

    /* ⛔ Log WHERE the reacting pages are, not just how many. Six runs
     * reported only a count, which made the probe's one reusable output
     * invisible and left the continuous scan with nowhere to point. */
    {
        ULONGLONG pg[32];
        unsigned np = emu_cursor_pages(pg, 32), k;
        if (np) {
            char line[512]; int used = 0;
            for (k = 0; k < np && used < 440; k++)
                used += _snprintf(line + used, sizeof line - used,
                                  "+0x%llx ", (unsigned long long)pg[k]);
            line[used > 0 ? used : 0] = '\0';
            log_line("cursor: reacting page offsets (%u of %u): %s",
                     np, r.pages_kept, line);
        }
    }

    /* ⭐ AIM THE CONTINUOUS SCAN at the reacting pages.
     *
     * ⛔ An earlier version took a single (offset,len) range and, when the
     * pages turned out to be scattered over 100+ MB, silently fell back to ONE
     * page -- which had stopped reacting by the next cycle. It watched a stale
     * page for a whole run. The pages are a LIST, so pass a LIST: all 31
     * observed pages are 124 KB, about 3% of a tick's budget, so there was
     * never a need to narrow at all.
     *
     * ⚠ Re-aimed whenever the set CHANGES, not once. The reacting set shifts
     * between cycles (the first cycle's +0x8000000 was gone by the second), and
     * a scan pinned to a stale set reports on memory nothing is using. */
    {
        ULONGLONG pg[64];
        unsigned np = emu_cursor_pages(pg, 64);
        /* ⭐ ADD, never replace. The reacting set wobbles every cycle, and
         * rebuilding the table on each wobble reset all accumulated history --
         * which made a whole run report "2 distinct, 1 change" on every field.
         * The set only grows; 124 KB of pages costs ~0.14 ms per tick. */
        if (np && emu_cursor_scan_add_pages(pg, np)) {
            g_scan_len_set = TRUE;
            log_line("cursor: continuous scan now covers %u page(s). It "
                     "samples every 20 ms and separates IDLE changes from "
                     "input-driven ones -- animation changes while you do "
                     "nothing, a cursor does not. History is KEPT across "
                     "updates.", emu_cursor_scan_npages());
        }
    }

    if (r.cycles_done < 2) {
        log_line("cursor: cycle 1 only LEARNS which pages react -- it has no "
                 "before-bytes yet. Keep navigating; cycle 2 is the first that "
                 "can judge.");
        return;
    }

    if (n == 0) {
        /* ⛔⛔ "NO candidate survived" IS NOT A NEGATIVE WHEN NOTHING MOVED.
         *
         * A whole run reported ten of these as "a real negative for this
         * window" while the log ALSO showed that not one LEFT/RIGHT edge had
         * ever arrived: the cycles compared two captures of a STATIONARY
         * screen, so they tested nothing. An un-navigated screen and a
         * navigated screen with no cursor in it produced the SAME line, which
         * is the one thing this report must never do.
         *
         * A cycle needs at least one forward AND one reverse press to mean
         * anything, i.e. 2 edges. Below that, say "measured nothing" and say
         * why -- an inconclusive is a RETURN VALUE, not a negative. */
        if (g_cursor_edges < 2) {
            log_line("cursor: ⚠ MEASURED NOTHING -- only %u directional "
                     "edge(s) seen since arming. The 4-phase test needs a "
                     "forward press AND the reverse on the same axis; with "
                     "fewer, the captures compared are of an UNMOVED screen. "
                     "This is NOT evidence the cursor is absent. Navigate "
                     "character select with LEFT and RIGHT (or UP/DOWN).",
                     g_cursor_edges);
            return;
        }
        log_line("cursor: ⛔ NO candidate survived %u cycle(s), with %u "
                 "directional edge(s) seen. That is a real negative for this "
                 "window: %u field(s) were examined at three widths and none "
                 "was quiet in the control, changed by 1..16 AND returned on "
                 "the reverse.",
                 r.cycles_done, g_cursor_edges, r.fields_examined);
        return;
    }

    log_line("cursor: --- %u candidate(s), best first ---", n);
    for (i = 0; i < n; i++) {
        log_line("cursor:   0x%llx  u%u  %u -> %u (delta %+d)  "
                 "survived %u cycle(s)%s",
                 (unsigned long long)f[i].addr, (unsigned)f[i].width * 8u,
                 f[i].v_base, f[i].v_moved, f[i].delta, f[i].cycles,
                 f[i].is_toggle ? "  ⚠ 0/1 TOGGLE -- a flag, not an index"
                                : "");
    }
    if (r.cycles_done >= 3 && !f[0].is_toggle)
        log_line("cursor: ⭐⭐ 0x%llx has tracked the cursor for %u "
                 "consecutive cycles. THE CURSOR IS READABLE.",
                 (unsigned long long)f[0].addr, f[0].cycles);
}

static void cursor_probe_tick(void)
{
    char rom[32];
    kbd_edges e;
    BOOL left, right;
    ec_status s;
    ec_phase want;

    /* ⚠ The WATCH runs even after the cycle probe has given up, and even
     * while it is armed. It is independent of presses, phases and timeouts --
     * that independence is the whole point. Gating it behind any of them would
     * reproduce the bug that made run 2 silent. */
    if (emu_cursor_rom_running(NULL, 0)) {
        /* ⚠ The "after" capture lives HERE, above every early return.
         * Placed next to its "before" trigger it sat behind six returns
         * including the give-up gate, so the pair could silently never
         * complete -- the same shape of bug that made run 2 report nothing. */
        /* ⭐⭐ THE PAGE-TABLE FINDER, once, a few seconds after the ROM
         * loads so the emulator has built its tables. One pass; nothing is
         * spoken; the player does nothing. */
        /* ⚠ RETRY UNTIL IT FINDS ONE, then stop. A ONE-SHOT sweep on a
         * timer is the exact shape that has failed repeatedly this session:
         * if it happens to run before the emulator has built its tables, it
         * finds nothing and never looks again -- and a premature negative is
         * indistinguishable from a real one.
         *
         * ⭐ It also removes any dependence on how long the player takes to
         * reach a particular screen. The table is built when the EMULATOR
         * starts, not when character select appears, but nothing here has to
         * assume that: it simply keeps looking until it succeeds.
         *
         * Every attempt is LOGGED with its number, so "swept 3 times, found
         * nothing" can never be read as "never tried". */
        /* ⭐⭐ THE GUEST-RAM LOCATOR: sample the WHOLE arena, once.
         *
         * Eleven runs of differential probing produced only rendering data, so
         * stop hunting the cursor and locate guest RAM instead -- that makes
         * every published CVS2 address readable by arithmetic.
         *
         * ⭐ Sampling on a GRID rather than at my guesses. I have twice picked
         * a "promising" offset and been wrong; a 4 KB slice every 4 MB covers
         * all 256 MB for 256 KB of disk, and the detector decides offline.
         * That removes my judgement from the search entirely.
         *
         * ⚠ Fires once, a few seconds after the ROM loads, and writes ONE file
         * so the run stays cheap. Nothing is spoken; the player does nothing. */
        /* ⛔⛔ A TIMER IS NOT A READINESS TEST -- this fired 17 SECONDS EARLY.
         *
         * Run 16 captured at 23:33:26 while CVS2 did not load until 23:33:43.
         * 63 of 64 slices were 100% ZEROS and the 64th held unit-circle
         * constants (0.7071, 0.9239, 0.866) -- the SHELL's own 3D maths. The
         * arena POINTER resolved, so every guard passed, but the emulator had
         * not populated it.
         *
         * ⚠ 'armed' means a ROM NAME is readable, which happens well before
         * the ROM is actually resident. That is the same class of error as
         * arming on the shell in run 1: a proxy for readiness, not readiness.
         *
         * ⭐ THE FIX IS A CONTENT TEST, not a longer wait. An empty arena is
         * all zeros; a populated one is not. Require a sampled slice to be
         * substantially non-zero before capturing anything, and RETRY until it
         * is -- so how long loading takes cannot matter. */
        if (!g_ram_done && GetTickCount() - g_ram_last > 3000) {
            ULONGLONG off;
            unsigned wrote = 0;
            BYTE probe[4096];
            unsigned nz = 0, i;

            g_ram_last = GetTickCount();

            /* Sample where the ROM image is known to live (the blob loads at
             * the arena base) and require real content. */
            if (!emu_cursor_peek(0x1000000ULL, probe, sizeof probe)) {
                log_line("ram: arena not readable yet -- retrying.");
                goto ram_done_check;
            }
            for (i = 0; i < sizeof probe; i++) if (probe[i]) nz++;
            if (nz * 100u / sizeof probe < 10u) {
                log_line("ram: arena is %u%% non-zero at +16MB -- the emulator "
                         "has not populated it yet. Retrying in 3 s. (A TIMER "
                         "fired 17 s early once; this waits for CONTENT.)",
                         nz * 100u / (unsigned)sizeof probe);
                goto ram_done_check;
            }
            g_ram_done = TRUE;
            for (off = 0; off + 0x1000 <= 0x10000000ULL; off += 0x400000ULL) {
                char tag[32];
                _snprintf(tag, sizeof tag, "%03u",
                          (unsigned)(off / (1024 * 1024)));
                if (emu_cursor_capture_ram(off, 0x1000, tag)) wrote++;
            }
            log_line("ram: sampled the arena on a 4 MB grid -- %u slice(s) of "
                     "4 KB written, arena CONFIRMED populated (%u%% non-zero "
                     "at +16MB). Analyse OFFLINE with the SH-4 detector.",
                     wrote, nz * 100u / (unsigned)sizeof probe);

            /* ⭐⭐ AND THE WHOLE PROCESS, not just the arena.
             *
             * Run 17's negative covers the arena only, and the arena is one
             * constructor's 256 MB out of ~1726 MB committed. Nothing has ever
             * proven the emulator keeps guest RAM there -- that assumption has
             * been carried, untested, since the anchor was found.
             *
             * ⚠ Gated behind the SAME content check, so it cannot fire before
             * the emulator has populated anything. */
            /* ⛔ RETIRED. Run 18 ran this over all 1726 MB of committed
             * memory: 441 slices, every one 'not code', with a positive
             * control passing through the same harness. The question "is
             * guest RAM outside the arena?" is ANSWERED. Re-running it costs
             * ~1.7 MB of disk and several seconds for a known negative. */
            /* emu_cursor_sample_process(0x400000, 0x1000); */

            /* ⛔⛔ RETIRED -- AND IT WAS NEVER A FRAMEBUFFER.
             *
             * This block captured 2 MB at arena+0x8900000 and +0xa400000 on the
             * strength of run 10's conclusion that the first is "ARGB1555 pixel
             * data: 438 distinct colours, alpha set throughout". Run 19 read the
             * full 2 MB and refuted it:
             *
             *     +0x8900000   99.99% is 0xCDCD -- MSVC never-written heap fill
             *     +0xa400000   100% ZEROS, one distinct byte value
             *
             * ⛔⛔ 0xCDCD HAS BIT 15 SET IN 100% OF WORDS, exactly like
             * ARGB1555. Run 10 sampled 32 KB of a fill region and I read the
             * fill's shape as a pixel format. The "second copy matching at shift
             * -0x18" was two fill regions matching, not two frames.
             *
             * ⚠ And I had ALREADY DOCUMENTED that trap in run 17 -- then walked
             * into it, because run 10's conclusion was written down and I
             * treated the note as evidence. A recorded conclusion is not a
             * measurement; re-derive it when you sample the same place again.
             *
             * There is no image here, so there is no outline to find, and the
             * whole pixel route is closed. See memory cfc2-not-a-framebuffer. */

            /* ⭐⭐⭐ WHAT IS ACTUALLY THERE: A DISPLAY LIST -- and it is better.
             *
             * The 152 words of REAL content at the very start of +0x8900000
             * decode cleanly as render commands:
             *
             *     floats +-64, +-47      quad VERTEX COORDINATES (128x94 sprite)
             *     floats 1.0, 0.75, 0.269  colour / UV multipliers
             *     u32 0x0CE80010, 0x0CE80018, 0x0CE80038, 0x0CC00000
             *
             * ⭐⭐ Those u32s are NAOMI GUEST ADDRESSES (0x0C...... is the
             * uncached view of guest system RAM), and 0x0CE80000 is the address
             * an earlier session pinned for AFS entry[11]'s texture table.
             *
             * ⭐ A display list NAMES WHAT IS BEING DRAWN. A framebuffer would
             * have required finding a coloured outline in pixels and hoping the
             * buffer was linear rather than twiddled. Here, if the selection
             * highlight is a quad, its vertex coordinates give the screen
             * position DIRECTLY -- and the grid geometry is already measured
             * (5x10, 64px cells, 74px pitch, origin 180,32). If its TEXTURE
             * pointer changes with the cursor, that names the character outright.
             *
             * ⭐⭐ AND THIS IS A DIFF, NOT A SEARCH. Every route that failed
             * hunted something cursor-shaped in an unknown place. This watches a
             * KNOWN 64 KB window change while the player moves -- anchor on what
             * CHANGES, identify last. The diff is against 152 words, not 91 MB.
             *
             * ⚠ 64 KB, not 2 MB: the real content was 152 words and everything
             * past it was fill. A 2 MB capture per slot would write 32 MB for
             * ~600 bytes of signal.
             *
             * ⚠ SERIAL SLOTS, NOT ONE FILE. A single overwritten capture cannot
             * show a change; and the probe must sample on the GAME'S schedule,
             * not the user's -- so this takes a slot every tick regardless of
             * keypresses, and the offline diff finds which words move. Slots are
             * numbered so "wrote 12 slots" can never read as "wrote nothing". */
            g_dl_armed = TRUE;
            log_line("dl: display-list capture ARMED at arena+0x%llx (%u KB per "
                     "slot, %u slots). ⚠ NOT a framebuffer -- run 19 refuted "
                     "that. Move the cursor on CHARACTER SELECT while these "
                     "write, then diff them OFFLINE.",
                     (unsigned long long)DL_OFFSET, (unsigned)(DL_LEN / 1024),
                     (unsigned)DL_SLOTS);
        ram_done_check: ;
        }

        /* ⭐⭐ THE SLOT WRITER -- one capture per 300 ms, unconditionally.
         *
         * ⚠ NOT gated on a keypress. A probe must sample on the GAME'S
         * schedule, not the user's: an event-triggered sampler cannot bracket a
         * change it did not predict, and that error has cost this project four
         * play sessions. It writes on a clock and the offline diff decides.
         *
         * ⚠ 300 ms, not every tick. The user moves roughly every 600 ms, so
         * successive slots land on either side of a movement; faster just
         * doubles the disk for the same evidence.
         *
         * ⚠ Stops at DL_SLOTS. A wraparound overwriting slot 0 would make an
         * old capture look fresh -- the exact confusion that a series exists to
         * avoid. A bounded recorder must report its own saturation. */
        if (g_dl_armed && g_dl_slot < DL_SLOTS &&
            GetTickCount() - g_dl_last > 300) {
            char tag[32];
            g_dl_last = GetTickCount();
            _snprintf(tag, sizeof tag, "dl%02u", g_dl_slot);
            if (emu_cursor_capture_ram(DL_OFFSET, DL_LEN, tag)) {
                g_dl_slot++;
                if (g_dl_slot == DL_SLOTS)
                    log_line("dl: ⭐ %u slot(s) written, capture COMPLETE "
                             "(cfc2_ram_dl00.bin .. dl%02u.bin). Diff them "
                             "OFFLINE: a word that moves with the cursor is the "
                             "highlight. ⚠ If you were NOT on character select "
                             "for these, the series says nothing -- rerun.",
                             (unsigned)DL_SLOTS, (unsigned)(DL_SLOTS - 1));
            } else {
                /* ⚠ A failed read is NOT an empty slot. Say so, or "8 slots"
                 * silently means something different than it appears to. */
                log_line("dl: ⚠ could not read arena+0x%llx for slot %u -- "
                         "NOTHING written. This says nothing about its "
                         "contents.",
                         (unsigned long long)DL_OFFSET, g_dl_slot);
            }
        }

        /* ⭐⭐ THE PAGE-TABLE SWEEP -- budgeted, resumable, and retried.
         *
         * ⚠ 25 ms per tick, not one blocking pass. MEASURED: 1937 ms for
         * 529 MB, and the live process has ~1.5 GB writable, so a full sweep
         * is ~5 SECONDS. Six retries unbudgeted would be a 30-second freeze,
         * and this project has already shipped a 114 s stall and a 37 s one.
         *
         * ⭐ Resuming is SAFE here and that is a real distinction, not a
         * convenience: a page table is a STATIC structure, so reading it
         * across ticks cannot blend two moments. A memory SNAPSHOT cannot be
         * resumed for exactly that reason. Same mechanism, opposite verdict,
         * because the thing being read is different.
         *
         * ⚠ RETRIED until it finds something, so it cannot matter how long
         * the player takes to reach any particular screen, and an early
         * premature sweep cannot become a permanent false negative. */
        if (0 && !g_pt_done) {   /* ⛔ RETIRED: run 13 answered it -- no flat
                                 * pointer table exists (every stride 0). */
            static ec_ptrun runs[12];
            SIZE_T sc = 0;
            BOOL pass_done = FALSE;
            unsigned nr = emu_cursor_find_pagetable(runs, 12, &sc, 25,
                                                    &pass_done);
            /* ⚠⚠ PROGRESS, unconditionally. Run 11 produced NOT ONE
             * `pagetable:` line -- no pass ever completed -- and the log gave
             * no way to tell a stalled sweep from one that never started. A
             * probe that cannot say where it is costs a whole session. */
            if (!pass_done && GetTickCount() - g_pt_progress_ms > 2000) {
                g_pt_progress_ms = GetTickCount();
                log_line("pagetable: sweeping... %llu MB examined this pass, "
                         "%u run(s) held, resume at 0x%llx.",
                         (unsigned long long)(sc / (1024 * 1024)), nr,
                         (unsigned long long)emu_cursor_pt_resume());
            }
            if (pass_done) {
                unsigned k;
                g_pt_tries++;
                /* Stop on SUCCESS, or once enough full passes have run that a
                 * negative means something rather than being premature. */
                if (nr || g_pt_tries >= 4) g_pt_done = TRUE;

                log_line("pagetable: pass %u complete -- swept %llu MB of "
                         "writable memory; %u run(s) of >=8 consecutive "
                         "in-arena pointers.",
                         g_pt_tries, (unsigned long long)(sc / (1024 * 1024)),
                         nr);
                for (k = 0; k < nr; k++)
                    log_line("pagetable:   at 0x%llx  %u entries  "
                             "stride 0x%llx  targets arena+0x%llx .. +0x%llx%s",
                             (unsigned long long)runs[k].addr, runs[k].count,
                             (unsigned long long)runs[k].stride,
                             (unsigned long long)runs[k].first_tgt,
                             (unsigned long long)runs[k].last_tgt,
                             runs[k].stride ? "  <== CONSTANT STRIDE: a "
                                              "mapping table" : "");
                if (!nr && g_pt_done)
                    log_line("pagetable: ⛔ no run found in %u full pass(es). "
                             "Either the emulator uses no flat pointer table, "
                             "or it stores OFFSETS rather than pointers. ⚠ The "
                             "memory WAS examined each pass, so this is a real "
                             "negative -- not 'never tried'.", g_pt_tries);
                else if (!nr)
                    log_line("pagetable: nothing yet -- sweeping again (the "
                             "emulator may still have been setting up).");
            }
        }

        if (g_spr_pending && GetTickCount() - g_spr_at >= 100) {
            if (emu_cursor_capture_sprites("after")) g_spr_pairs++;
            g_spr_pending = FALSE;
        }

    /* ⭐⭐ THE DIRECT WATCH RUNS FIRST, AND IT NEEDS NO PRESSES AT ALL.
     *
     * Run 5 named an address (arena + 0x8030134, 3 -> 2 on a cursor move). The
     * cycle machinery below exists to FIND an unknown address; confirming a
     * known one needs a byte read, not a 828 ms snapshot pass -- and the cycle
     * probe structurally CANNOT confirm it, because its capture is far wider
     * than the one-frame change it is trying to bracket.
     *
     * So: sample the named offsets every tick and log changes. The user simply
     * plays; if the value walks 0..47 on character select, it is the cursor. */
    {
        static unsigned last[EC_WATCH_MAX];
        static BOOL     have_last;
        ec_watch w[EC_WATCH_MAX];
        unsigned n, i;

        /* ⭐⭐ THE CONTINUOUS SCAN. Independent of presses -- it samples every
         * tick and separates changes that happened while IDLE from those that
         * followed input. That one distinction is what the differential probe
         * was structurally blind to, and it is what rejects a blink phase. */
        if (g_scan_len_set) {
            DWORD now = GetTickCount();
            BOOL recent = (now - g_last_input_ms) < 400;
            emu_cursor_scan_tick(recent);

            /* Report every 5 s. ⚠ Unconditionally, including the population
             * counts -- "no candidate" and "nothing moved" must never look the
             * same, which has cost this project real runs. */
            if (now - g_scan_report_ms > 5000) {
                ec_scan sc[8];
                unsigned ns = emu_cursor_scan_report(sc, 8), k;

                g_scan_report_ms = now;

                {
                    /* ⛔ Report the POPULATION, not the report cap. Printing
                     * `ns` said "8 field(s)" for three runs because the caller
                     * asked for 8 -- a cap masquerading as a measurement. */
                    unsigned pm = 0, pq = 0, pbd = 0;
                    emu_cursor_scan_population(&pm, &pq, &pbd);
                    log_line("scan: %u page(s) watched; %u byte(s) moved at "
                             "all; %u of those changed ONLY after input; best "
                             "input-only field has %u distinct value(s). "
                             "(showing top %u)",
                             emu_cursor_scan_npages(), pm, pq, pbd, ns);
                    /* ⭐⭐ THE USER'S OBSERVATION, ENCODED. They report that
                     * GROOVE select is "one press = one item, a straight
                     * list", while CHARACTER select moves analog (one press
                     * can jump several cells, several presses can move none).
                     *
                     * So a groove index is a small dense 0..5 field that moves
                     * only on input -- a much sharper target than the 0..47
                     * character cursor, and it needs NO index->name mapping
                     * because the order IS C,A,P,S,N,K. Call it out by name. */
                    for (k = 0; k < ns; k++) {
                        if (sc[k].idle_changes) continue;
                        if (sc[k].vmin <= 1 && sc[k].vmax >= 3 &&
                            sc[k].vmax <= 8 && sc[k].distinct >= 3)
                            log_line("scan: ⭐⭐ +0x%llx spans %u..%u with %u "
                                     "distinct values and NEVER moves on its "
                                     "own -- GROOVE-SHAPED (6 items: C A P S "
                                     "N K).",
                                     (unsigned long long)sc[k].offset,
                                     sc[k].vmin, sc[k].vmax, sc[k].distinct);
                    }
                    if (pq && pbd < 3)
                        log_line("scan: ⚠ input-only fields exist but none "
                                 "takes 3+ values -- consistent with flags or "
                                 "sprite attributes, not a list index.");
                }
                for (k = 0; k < ns && k < 6; k++)
                    log_line("scan:   +0x%llx  val %u  range %u..%u  "
                             "%u distinct  %u change(s) [%u idle, %u input]%s",
                             (unsigned long long)sc[k].offset, sc[k].value,
                             sc[k].vmin, sc[k].vmax, sc[k].distinct,
                             sc[k].changes, sc[k].idle_changes,
                             sc[k].input_changes,
                             (sc[k].idle_changes == 0 && sc[k].distinct >= 5)
                               ? "  <== CURSOR-SHAPED: many values, never moves"
                                 " on its own"
                               : (sc[k].idle_changes ? "  (animation)"
                                  : (sc[k].distinct >= 2
                                     ? "  <== input-only, few values"
                                     : "")));
            }
        }

        if (emu_cursor_watch_tick()) {
            n = emu_cursor_watch_report(w, EC_WATCH_MAX);
            for (i = 0; i < n; i++) {
                if (!w[i].readable) continue;
                if (have_last && w[i].value == last[i]) continue;
                if (have_last && g_cursor_watch_logged < 200) {
                    g_cursor_watch_logged++;
                    log_line("watch: +0x%llx = %u (was %u, delta %+d) "
                             "[range %u..%u, %u distinct, %u change(s)]",
                             (unsigned long long)w[i].offset, w[i].value,
                             last[i], (int)w[i].value - (int)last[i],
                             w[i].vmin, w[i].vmax, w[i].distinct,
                             w[i].changes);
                }
                last[i] = w[i].value;
            }
            if (!have_last) {
                for (i = 0; i < n; i++) last[i] = w[i].value;
                have_last = TRUE;
                for (i = 0; i < n; i++)
                    log_line("watch: +0x%llx resolved to 0x%llx, initial "
                             "value %u%s",
                             (unsigned long long)w[i].offset,
                             (unsigned long long)w[i].addr, w[i].value,
                             w[i].readable ? "" :
                             "  (NOT READABLE -- says nothing about the field)");
            }
        }
    }
    }

    if (g_cursor_gave_up) return;

    /* ⛔⛔ ARM ONLY WHEN A ROM IS ACTUALLY RUNNING -- and this cost a run.
     *
     * The first version armed on `emu_rom_index() != EMU_OK`. But index 0 is a
     * VALID game (cvs1), so that is TRUE on the shell as well: the probe armed
     * at 18:54:22 with `rom_index=0 name=''`, CVS2 did not load until 18:54:46,
     * and the 30 s timeout expired at 18:54:52. It spent 24 of its 30 seconds
     * on the title screen and then reported "the emulated game does not take
     * keyboard through sKeyboard" -- a confident negative it had six seconds to
     * test.
     *
     * ⚠ The project's own memory already said "index 0 is a VALID game, not
     * 'nothing matched'". Knowing a trap is not the same as encoding it. */
    if (!emu_cursor_rom_running(rom, sizeof rom)) {
        /* Not in a game. If we were armed, the player left -- drop the state
         * rather than carry a half-finished cycle across the boundary. */
        if (g_cursor_armed) {
            log_line("cursor: left the emulated game -- probe disarmed. It "
                     "will re-arm when a ROM loads.");
            emu_cursor_reset();
            g_cursor_armed = FALSE;
            g_cursor_rom[0] = '\0';
        }
        return;
    }

    /* A DIFFERENT ROM means a different machine and a different arena layout;
     * carrying candidates across would compare two games' memory. */
    if (g_cursor_armed && strcmp(rom, g_cursor_rom) != 0) {
        log_line("cursor: ROM changed '%s' -> '%s' -- re-arming.",
                 g_cursor_rom, rom);
        emu_cursor_reset();
        g_cursor_armed = FALSE;
    }

    if (!g_cursor_armed) {
        s = emu_cursor_arm();
        if (s != EC_OK) {
            log_line("cursor: could not arm -- %s. ⚠ This says nothing about "
                     "the game.", ec_status_name(s));
            g_cursor_gave_up = TRUE;
            return;
        }
        g_cursor_armed = TRUE;
        g_cursor_edges = 0;
        g_cursor_waited_note = 0;
        g_cursor_reads = 0;
        g_cursor_unreadable = 0;
        g_cursor_other = 0;
        /* ⭐ The clock starts HERE, with the ROM confirmed loaded -- so the
         * timeout measures time INSIDE the emulated game. The first version
         * started it at arm time on the shell and burned 80% of the window
         * before the game existed. */
        g_cursor_armed_at = GetTickCount();
        _snprintf(g_cursor_rom, sizeof g_cursor_rom, "%s", rom);
        log_line("cursor: ARMED -- '%s' is running. Navigate the menu with "
                 "LEFT and RIGHT; each press advances the probe. Nothing is "
                 "spoken.", rom);
        return;
    }

    /* ⛔⛔ THE TIMEOUT IS EVALUATED FIRST, ON EVERY TICK.
     *
     * Run 2 (19:05) armed correctly on 'cvs2u', logged "still waiting" at 10 s
     * -- and then went SILENT for the remaining 23 s of the session. The 30 s
     * verdict never fired even though the run outlasted it.
     *
     * Cause: `if (!kbd_poll_edges(&e)) return;` and `if (e.first) return;` both
     * skipped the timeout. Once kbd_poll_edges() started failing every tick,
     * the probe waited forever and said nothing -- a silent probe again, the
     * very thing the "still waiting" line was added to prevent. Adding one
     * status line does not help if the code path that would print the verdict
     * is unreachable.
     *
     * ⭐ And the two silences are DIFFERENT FINDINGS:
     *     could not read the keyboard state  -> says something about the path
     *     read it fine, but no arrow edges   -> says the game ignores it
     * Collapsing them into one line would repeat this project's oldest mistake. */
    {
        DWORD waited = GetTickCount() - g_cursor_armed_at;
        BOOL  got    = kbd_poll_edges(&e);

        if (!got) g_cursor_unreadable++;
        else      g_cursor_reads++;

        /* ⚠⚠ A HEARTBEAT, not a one-shot. Run 2 printed its 10 s line and
         * then went silent for 23 s with no verdict, and NOTHING in the log
         * could say why -- the builder was demonstrably still running
         * (+1357 calls after arming), so the obvious explanation was wrong and
         * there was no evidence left to choose between the rest.
         *
         * A probe that cannot say what it is doing costs a whole play session
         * to re-run. This reports every 10 s, unconditionally, until it
         * concludes. */
        if (g_cursor_edges == 0 && waited > 10000 &&
            waited / 10000 > g_cursor_waited_note) {
            g_cursor_waited_note = (unsigned)(waited / 10000);
            log_line("cursor: still waiting for a LEFT/RIGHT edge -- %u s "
                     "inside '%s'. %u tick(s) read the keyboard state OK, "
                     "%u could NOT read it.",
                     (unsigned)(waited / 1000), g_cursor_rom,
                     g_cursor_reads, g_cursor_unreadable);
        }

        if (g_cursor_edges == 0 && waited > CURSOR_EDGE_TIMEOUT_MS) {
            if (g_cursor_reads == 0)
                log_line("cursor: ⛔ COULD NOT READ the sKeyboard state on ANY "
                         "of %u tick(s) in %u s inside '%s'. ⚠ NOTHING WAS "
                         "MEASURED -- this is not 'the game ignores the "
                         "keyboard'. The state block is unreadable while the "
                         "emulator runs, which is itself a clue about where "
                         "in-game input lives.",
                         g_cursor_unreadable, (unsigned)(waited / 1000),
                         g_cursor_rom);
            else
                log_line("cursor: ⛔ NO ARROW EDGES in %u s inside '%s', but "
                         "the state WAS readable on %u tick(s) (%u not). So "
                         "sKeyboard is alive and simply does not carry the "
                         "emulated game's input. ⚠ This is a real negative "
                         "about the PATH, not about a cursor. Next step is the "
                         "emulator's own input handling in the disassembly.",
                         (unsigned)(waited / 1000), g_cursor_rom,
                         g_cursor_reads, g_cursor_unreadable);
            g_cursor_gave_up = TRUE;
            return;
        }

        if (!got)     return;   /* could not read: not a negative        */
        if (e.first)  return;   /* seeded, not observed                  */
    }

    /* ⭐⭐ ALL FOUR DIRECTIONS, because a grid cursor moves on either axis.
     *
     * ⚠⚠ AND A CORRECTION TO MY OWN READING OF RUN 3. I saw UP/DOWN in the
     * "saw key code" lines, saw no LEFT/RIGHT there, and concluded the probe
     * was watching the wrong axis. The user said plainly they had moved left
     * and right -- and they were right.
     *
     * Those lines only print for keys that are NOT left/right. LEFT and RIGHT
     * took the other branch and DROVE THE PHASE MACHINE; a full cycle completed
     * at 19:12:55, which nothing but left/right could have caused. Their
     * absence from that list was EXPECTED, not evidence of absence -- I read a
     * filtered list as if it were a complete one.
     *
     * Accepting UP/DOWN as well is still right (a grid moves both ways), but
     * the reason is coverage, not a broken axis. */
    /* ⭐ ANY key edge marks "the player just acted", which is what the
     * continuous scan needs to separate idle changes from input-driven ones.
     * Recorded before the direction filter, because a press that is not an
     * arrow still means the player was not idle. */
    {
        int b;
        for (b = 0; b < KBD_BANK_WORDS; b++)
            if (e.pressed[b]) { g_last_input_ms = GetTickCount(); break; }
    }

    /* ⭐⭐ SPRITE CAPTURE: a BEFORE/AFTER pair around one cursor move.
     *
     * Fires automatically on the first few direction presses -- no hotkey, no
     * phases, no protocol for the player to hold in mind. Every earlier attempt
     * failed by doing something clever live; this writes bytes and stops.
     *
     * "before" is captured on the press itself (the game has not acted yet at
     * 20 ms resolution), "after" on the next tick that sees the move landed.
     * If the bracketing is slightly off it does not matter: the diff of two
     * captures a moment apart still isolates what moved, and the analysis is
     * offline where it can be checked. */
    if (g_spr_pairs < 3 &&
        (kbd_is_set(e.pressed, KBDC_LEFT)  || kbd_is_set(e.pressed, KBDC_RIGHT) ||
         kbd_is_set(e.pressed, KBDC_UP)    || kbd_is_set(e.pressed, KBDC_DOWN))) {
        if (!g_spr_pending) {
            if (emu_cursor_capture_sprites("before")) {
                g_spr_pending = TRUE;
                g_spr_at = GetTickCount();
            }
        }
    }
    left  = kbd_is_set(e.pressed, KBDC_LEFT)  || kbd_is_set(e.pressed, KBDC_UP);
    right = kbd_is_set(e.pressed, KBDC_RIGHT) || kbd_is_set(e.pressed, KBDC_DOWN);

    /* ⭐ Log ANY key that arrives, not just arrows.
     *
     * Run 2 established that sKeyboard is alive during the emulated game (the
     * builder ran +1357 times after arming) yet no LEFT/RIGHT edge reached us.
     * The obvious next question is whether the game takes menu input on some
     * OTHER code -- a gamepad-mapped key, WASD, Enter -- and that question is
     * answered for free by printing what we DO see. Without this, "no arrows"
     * and "no keys at all" are the same log line, and they mean different
     * things. Bounded so a held key cannot flood the log. */
    if (!left && !right) {
        int i, b;
        for (b = 0; b < KBD_BANK_WORDS && g_cursor_other < 24; b++) {
            if (!e.pressed[b]) continue;
            for (i = 0; i < 32 && g_cursor_other < 24; i++) {
                if (!(e.pressed[b] & (1u << i))) continue;
                g_cursor_other++;
                log_line("cursor: saw key code 0x%02x (not LEFT 0x%02x / "
                         "RIGHT 0x%02x). sKeyboard IS delivering input here.",
                         b * 32 + i, KBDC_LEFT, KBDC_RIGHT);
            }
        }
        return;                 /* the timeout above handles "nothing ever
                                 * arrives" */
    }

    {
        /* Latch the axis on the forward press; require the reverse on it. */
        int axis = (kbd_is_set(e.pressed, KBDC_UP) ||
                    kbd_is_set(e.pressed, KBDC_DOWN)) ? 1 : 0;
        if (emu_cursor_next_phase() == EC_PHASE_CONTROL)
            g_cursor_axis = axis;               /* starting a fresh cycle */
        else if (axis != g_cursor_axis)
            return;    /* a press on the other axis mid-cycle: ignore it,
                        * it would not return the cursor to its start */
    }

    g_cursor_edges++;
    want = emu_cursor_next_phase();

    /* The phase machine wants: CONTROL (a quiet capture), then MOVED (after a
     * forward press), then RETURNED (after the reverse). We take the control
     * capture on the press itself -- immediately BEFORE the game has acted on
     * it -- which is the closest we can get to "the moment before the move". */
    if (want == EC_PHASE_CONTROL) {
        s = emu_cursor_step(EC_PHASE_CONTROL);
        if (s == EC_OK) s = emu_cursor_step(EC_PHASE_MOVED);
    } else {
        s = emu_cursor_step(want);
    }

    if (s != EC_OK) {
        log_line("cursor: step %s -> %s. ⚠ Could not measure; this is a return "
                 "value, not a finding.", ec_phase_name(want),
                 ec_status_name(s));
        return;
    }

    if (emu_cursor_next_phase() == EC_PHASE_CONTROL)
        cursor_probe_report();            /* a cycle just completed */
}

static void emu_scan_report(int beat)
{
    /* Ask the decisive question first, once. */
    emu_guest_map_log();
    emu_cap_probe_log();

    static unsigned last_n = 0xFFFFFFFFu;
    static int      said_fail;

    emu_ram_hit hits[16];
    unsigned nh = 0;
    SIZE_T scanned = 0;
    emu_status s = emu_ram_find(hits, 16, &nh, &scanned);

    if (s != EMU_OK) {
        if (!said_fail) {
            said_fail = 1;
            log_line("emu: guest-RAM scan %s -- it did NOT run to completion, "
                     "so this says NOTHING about the game. Not a negative.",
                     emu_status_name(s));
        }
        return;
    }
    said_fail = 0;

    /* ⭐ Sample EVERY sweep, before the change-gate below. The candidate COUNT
     * goes quiet as soon as the set is stable, but the whole point is watching
     * those addresses change over time -- gating the sample on the count would
     * stop collecting evidence exactly when the match starts producing it. */
    emu_watch_sample(hits, nh < 16 ? nh : 16);

    /* The behaviour report is the finding; print it on a slower cadence so it
     * accumulates real history between lines. */
    if (beat % 15 == 0)
        emu_watch_report_log();

    if (nh == last_n && beat != 3)
        return;
    last_n = nh;

    /* ⚠ Report KB, not MB. The first live run printed "examined 0 MB" beside
     * 16 hits -- a contradiction produced by integer division, which made a
     * truncated sweep look like an impossible one. A unit that rounds the
     * interesting case to zero is the wrong unit. */
    if (nh == 0) {
        log_line("emu: guest-RAM scan examined %llu KB, found NO CVS2 player "
                 "pair. ⚠ That is EXPECTED outside a match (the fighters do "
                 "not exist yet). During a live round it would mean the "
                 "signature or the 0x5CC stride is wrong.",
                 (unsigned long long)(scanned / 1024));
        return;
    }

    {
        unsigned k, show = nh < 3 ? nh : 3;
        /* ⚠ QUIETER ON PURPOSE. The per-candidate dump was the bulk of the
         * log and none of it was ever actionable -- the CAP PROBE above is
         * the line that decides anything. Three examples are enough to see
         * the shape; the behaviour report is what earns attention. */
        log_line("emu: guest-RAM scan examined %llu KB, found %u candidate "
                 "pair(s) at stride 0x5CC (see CAP PROBE for what matters):",
                 (unsigned long long)(scanned / 1024), nh);
        for (k = 0; k < show && k < 16; k++)
            log_line("emu:    P1=0x%llx  hp %u / %u",
                     (unsigned long long)hits[k].p1,
                     hits[k].health, hits[k].max_health);

        /* ⭐ The diagnostic that would have named the first run's failure
         * immediately: hits packed within a few bytes of each other are ONE
         * region matching at consecutive alignments, not many fighters. */
        if (nh >= 2 && hits[nh < 16 ? nh - 1 : 15].p1 - hits[0].p1 < 0x1000)
            log_line("emu: ⚠ ALL candidates lie within 4 KB of one another -- "
                     "that is ONE region matching repeatedly, not %u separate "
                     "player structs. Treat it as a single lead at best.", nh);

        /* ⚠⚠ Say plainly what this is NOT. A candidate list is not a finding,
         * and this project has already mistaken one for the other -- a pointer
         * pair that survived 13 trials with clean controls turned out to be a
         * monotonic allocation counter. */

    }
}

/* ⭐⭐ The behaviour report -- the half that actually identifies guest RAM.
 * A NORMAL MATCH supplies everything this needs; training mode is not
 * required. Health diverging between players, falling in irregular steps, and
 * jumping back to the cap at a round transition are three things filler cannot
 * do, and the game produces all of them on its own. */
static void emu_watch_report_log(void)
{
    emu_watch w[16];
    unsigned n = emu_watch_report(w, 16);
    unsigned i, show = n < 6 ? n : 6;
    unsigned convincing = 0;

    if (n == 0)
        return;

    /* ⛔⛔ THE BAR WAS FAR TOO LOW AND THE HEADLINE OVERCLAIMED. The live run
     * announced "4 candidates DROP and DIVERGE -- that is health behaving like
     * health" about four values that had changed exactly ONCE in five samples
     * and then frozen, two of them reading hp 65526/256 (health ABOVE its own
     * maximum -- 0xFFF6, i.e. -10 as int16).
     *
     * A field written once at load satisfies "it dropped" and "they differ".
     * Only SUSTAINED activity distinguishes a fight from initialisation. */
    for (i = 0; i < n; i++)
        if (w[i].changes >= 3 && w[i].drops >= 2 && w[i].diverged &&
            !w[i].impossible)
            convincing++;

    /* ⚠ THE LIVE SWEEP MANAGED ~1 MB WHERE THE BENCHMARK DOES 256 MB IN 31 ms.
     * That gap is a fact about the real arena that no offline benchmark can
     * supply -- mine modelled both a dense and a sparse arena and BOTH finish
     * in one sweep, so the real one is shaped like neither. Rather than guess
     * a third time, report the arena's actual region layout once and read it.
     * (benchmark-drifts-from-the-code: I benchmarked something easier than
     * reality and believed the number.) */
    {
        static int said_layout;
        if (!said_layout) {
            ULONGLONG ab = 0, p;
            SIZE_T asz = 0;
            said_layout = 1;
            if (emu_arena(&ab, &asz) == EMU_OK) {
                MEMORY_BASIC_INFORMATION mbi;
                int regions = 0;
                ULONGLONG committed = 0;
                p = ab;
                while (p < ab + EMU_ARENA_SIZE && regions < 400) {
                    if (VirtualQuery((LPCVOID)(ULONG_PTR)p, &mbi, sizeof(mbi))
                            != sizeof(mbi))
                        break;
                    regions++;
                    if (mbi.State == MEM_COMMIT)
                        committed += mbi.RegionSize;
                    if (regions <= 8)
                        log_line("emu: region %2d: base=0x%llx size=%llu KB "
                                 "state=%lx protect=%lx",
                                 regions,
                                 (unsigned long long)(ULONG_PTR)mbi.BaseAddress,
                                 (unsigned long long)(mbi.RegionSize / 1024),
                                 (unsigned long)mbi.State,
                                 (unsigned long)mbi.Protect);
                    p = (ULONGLONG)(ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
                }
                log_line("emu: ARENA LAYOUT -- %d region(s) in the first "
                         "256 MB, %llu MB committed.",
                         regions, (unsigned long long)(committed / (1024*1024)));
            }
        }
    }

    {
        SIZE_T cov = 0; unsigned passes = 0;
        emu_ram_coverage(&cov, &passes);
        log_line("emu: COVERAGE -- %llu MB of the current pass, %u full "
                 "pass(es) completed. %s",
                 (unsigned long long)(cov / (1024 * 1024)), passes,
                 passes ? "The arena HAS been swept end to end."
                        : "⚠ The arena has NOT yet been swept once; a negative "
                          "here means NOT LOOKED YET.");
    }

    log_line("emu: --- behaviour of %u tracked candidate(s), best first ---", n);
    for (i = 0; i < show; i++)
        log_line("emu:    0x%llx  hp %u/%u  samples %u  changes %u  drops %u  "
                 "resets %u  diverged %u%s",
                 (unsigned long long)w[i].p1, w[i].last_h1, w[i].max_health,
                 w[i].samples, w[i].changes, w[i].drops, w[i].resets,
                 w[i].diverged,
                 w[i].impossible ? "  ⛔ hp EXCEEDED max: not health" : "");

    if (convincing == 0)
        log_line("emu: ⚠ NO candidate shows SUSTAINED health behaviour "
                 "(3+ changes, 2+ drops, divergence, never above its own max). "
                 "A value that moved once and froze is initialisation, not a "
                 "fight. ⚠ Check the COVERAGE line before concluding anything: "
                 "if only a few %% of the arena has been swept, this is 'not "
                 "looked yet', NOT 'not there'.");
    else
        log_line("emu: ⭐⭐ %u candidate(s) show SUSTAINED health behaviour. "
                 "A `resets` count above zero means one also returned to "
                 "EXACTLY its cap at a round change -- the reversibility test "
                 "passed.", convincing);
}

static void emu_probe_report(int beat)
{
    static int last_rom = -1;
    static int said_absent;

    ULONGLONG st = 0, arena = 0;
    SIZE_T size = 0;
    emu_status s_st, s_ar, s_ix;
    int rom = 0, game = -1, rev = -1;
    char live[32];

    s_st = emu_state(&st);
    if (s_st != EMU_OK) {
        /* Only say it once per state, or a quiet session becomes a wall of
         * identical lines -- and the ONE interesting transition gets buried. */
        if (!said_absent) {
            said_absent = 1;
            if (s_st == EMU_NOT_RUNNING)
                log_line("emu: no game is loaded (state slot is null). This is "
                         "the expected, CORRECT reading in the shell menus -- "
                         "not a failure to read.");
            else
                log_line("emu: ⚠ %s -- the state slot at 0x%llx could NOT be "
                         "read. Nothing may be concluded from this; it is not "
                         "a negative.",
                         emu_status_name(s_st), (unsigned long long)EMU_STATE_SLOT);
        }
        last_rom = -1;
        return;
    }
    said_absent = 0;

    s_ix = emu_rom_index(&rom);
    if (s_ix != EMU_OK) {
        log_line("emu: state=0x%llx resolved, but the ROM index is %s -- the "
                 "field map may be wrong.",
                 (unsigned long long)st, emu_status_name(s_ix));
        return;
    }

    /* Report on CHANGE. Re-printing an unchanged ROM every 10 s would bury the
     * transition that actually matters (entering or leaving a game). */
    /* ⚠ The IDENTITY lines report on change only -- re-printing an unchanged
     * ROM every 10 s would bury the transition that matters. But the guest-RAM
     * SCAN must keep running: the fighters only exist during a match, which
     * begins long after the ROM was selected, so a change-only scan would look
     * exactly once, on a menu screen, and correctly find nothing.
     *
     * ⭐ Same lesson as the report cadence that covered 2 of 22 movements:
     * pick a schedule the user's actual session can reach. Here that means
     * identity on the edge, scan on the beat. */
    {
        BOOL first_sight = (rom != last_rom) || (beat == 3);
        last_rom = rom;
        if (!first_sight) {
            /* Not a new ROM -- but still sweep for the fighters, and say
             * nothing unless something is found or the sweep failed. */
            int g2 = -1;
            if (emu_game_from_rom_index(rom, &g2, NULL) && g2 == 1)
                emu_scan_report(beat);
            return;
        }
    }

    s_ar = emu_arena(&arena, &size);

    live[0] = '\0';
    (void)emu_rom_name(live, sizeof(live));

    if (!emu_game_from_rom_index(rom, &game, &rev))
        game = rev = -1;

    log_line("emu: state=0x%llx rom_index=%d name='%s' -> game=%d rev=%d",
             (unsigned long long)st, rom, live, game, rev);

    /* ⭐⭐ THE CROSS-CHECK. Two independent sources: the live string the game
     * stored, and the name our table says that index means. Agreement is
     * evidence; disagreement means the map is wrong and must be said plainly
     * rather than papered over with the value that happens to look right. */
    {
        const char *expect = emu_rom_name_for_index(rom);
        if (!expect)
            log_line("emu: ⚠ rom_index %d is outside the 18-record table.", rom);
        else if (live[0] && strcmp(live, expect) != 0)
            log_line("emu: ⛔⛔ MISMATCH -- state+0x30 says '%s' but rom_index "
                     "%d means '%s'. The field map is WRONG; do not build on "
                     "these offsets.", live, rom, expect);
        else if (live[0])
            log_line("emu: ✅ cross-check OK -- the live name and the index "
                     "agree on '%s'.", expect);
        else
            log_line("emu: ⚠ the live name string was unreadable, so the "
                     "cross-check did NOT run (the index alone is unconfirmed).");
    }

    if (s_ar == EMU_OK)
        log_line("emu: arena=0x%llx region=%llu MB (the constructor asks for "
                 "%llu MB). ⚠ This is the whole arena, NOT guest RAM -- NAOMI "
                 "system RAM is 16 MB and lives somewhere INSIDE it.",
                 (unsigned long long)arena,
                 (unsigned long long)(size / (1024 * 1024)),
                 (unsigned long long)(EMU_ARENA_SIZE / (1024 * 1024)));
    else
        log_line("emu: arena is %s", emu_status_name(s_ar));

    if (game == 1)
        emu_scan_report(beat);
}

static HANDLE g_thread;
static volatile LONG g_quit;

/* --- Route 5: read the engine's own pad record ---------------------------
 * Routes 1-4 are closed with evidence (see CLAUDE.md), and route 5's first
 * four attempts failed too. What finally worked was reading the answer out of
 * the DISASSEMBLY rather than searching memory for something pad-shaped:
 * the sPad instance pointer, the field offsets and the array stride all come
 * from instructions, not from pattern matching.
 *
 * The mod's job is now only to capture bytes; all comparison happens offline.
 * See src/pad_watch.h for why that division matters.
 *
 * ⚠ Hotkey choice: F7 is ordinary, not a toggle, not an NVDA modifier. The
 * real hazard from [[hotkey-fallback-codes-eat-arrows]] is narrow -- NumLock
 * -off numpad VKs literally ARE the arrow VKs -- and F7 has no such aliasing.
 * An earlier pass excluded all 70 keys in the game's KB_* table and landed on
 * Scroll Lock, a toggle key that never registered in-game, costing two runs. */

/* ⛔ THE MANUAL PAD-DUMP HOTKEY IS GONE (2026-09-19, user's call: "we don't
 * need that, I don't think we ever even used it").
 *
 * It was F7, and it was a manual override for a route that is CLOSED with
 * evidence: every sPad record has `Kind = 0`, so the dispatcher at
 * 0x1401880a0 returns immediately and the pad update never runs. The shell
 * does not read the keyboard through sPad, so there is nothing a forced
 * capture can reveal that pad_auto_capture() has not already shown.
 *
 * ⭐ Removing it is what freed F5-F8 as one physical four-pack for the cue
 * volume keys. `pad_auto_capture()` still runs under cfc2_pad.on, so the
 * diagnostic itself is intact -- only the unused manual trigger is gone.
 *
 * ⚠ Keep any new hotkey clear of F12: that is Steam's default screenshot key,
 * and a key used during ordinary play must not also snap a screenshot. */

/* Was `vk` pressed since the last check? Edge-triggered, so holding the key
 * acts once rather than flooding.
 *
 * ⚠ Shift is read SEPARATELY by the caller, not folded in here: the edge must
 * belong to the cue key itself, so that shifting mid-hold cannot manufacture a
 * second press. */
static BOOL key_pressed(int vk)
{
    /* GetAsyncKeyState's low bit reports "pressed since last call", but it is
     * shared process-wide; test the high bit against remembered state instead
     * so this cannot be disturbed by the game polling the same key. */
    static BOOL was_down[256];
    BOOL down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    BOOL edge = down && !was_down[vk & 0xFF];
    was_down[vk & 0xFF] = down;
    return edge;
}

/* --- Per-cue volume hotkeys ----------------------------------------------
 * F5 health · F6 super meter · F7 beacons · F8 clock tick.
 * Plain key steps UP, Shift+key steps DOWN, both wrapping on
 * 0/25/50/75/100. From the 100 default one press mutes -- the user's design.
 *
 * ⚠ THE KEY CHOICE WAS CHECKED, not assumed (user asked directly):
 *  - the mod's only other hotkey was the pad dump on F7, now removed;
 *  - CFC2 binds no function keys -- its 16 keyboard-settings rows
 *    (g_kbd_settings_rows in menu_speak.c) are gameplay actions on letters
 *    and arrows;
 *  - the `KB_F10` seen in gui_markup.c and markup.py is an EXAMPLE in a
 *    comment about the mechanical KB_ naming rule, not a binding;
 *  - F12 is deliberately avoided: it is Steam's default screenshot key.
 *
 * ⚠ These are NOT gated on the audio device opening. A key that silently
 * does nothing is exactly the failure a blind user cannot diagnose. The
 * handler announces the level either way.
 *
 * ⭐ SPEECH IS THE ONLY FEEDBACK. There is no on-screen indicator, so an
 * unspoken change is an invisible one; the announcement interrupts, because
 * holding a key through three steps should say where it LANDED, not queue
 * four sentences. */
static void volume_hotkeys_tick(void)
{
    for (int c = 0; c < AUDIO_CUE_COUNT; c++) {
        if (!key_pressed(VK_F5 + c))
            continue;
        /* Read shift at the moment of the edge. */
        BOOL down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        int pct = audio_cue_step_volume((audio_cue)c, !down);

        wchar_t say[96];
        /* "beacons 50 percent" / "health muted" -- the word MUTED rather than
         * "0 percent", because 0 is the state the user most needs to
         * recognise instantly and a number is easy to mishear. */
        /* Spoken in the game's language; audio_cue_name() stays English for
         * the log line below. Order is audio_cue's. */
        static const mt_id cue_word[AUDIO_CUE_COUNT] = {
            MT_CUE_HEALTH, MT_CUE_SUPER, MT_CUE_BEACONS, MT_CUE_TICK };
        if (pct == 0)
            mt_fmt(say, 96, MT_MUTED, mt(cue_word[c]), NULL, NULL);
        else {
            wchar_t n[16];
            _snwprintf(n, 16, L"%d", pct);
            n[15] = 0;
            mt_fmt(say, 96, MT_PERCENT, mt(cue_word[c]), n, NULL);
        }
        speech_say(say, TRUE);

        /* Persist so the balance survives a restart. ⚠ A profile write is a
         * file I/O on the worker thread, but it happens only on a KEYPRESS --
         * a handful of times a session, never in a loop. That is the test this
         * project applies before any I/O near the poll: count how often it
         * runs, not how fast it is. */
        audio_save_volumes();
        log_line("audio: %ls volume -> %d%%",
                 audio_cue_name((audio_cue)c), pct);
    }
}


/* --- Power Stone settings key --------------------------------------------
 * F9 cycles pickup announcements (every fighter, players only, off);
 * Shift+F9 toggles off-screen sounds. User, 2026-10-27, "in case it is too
 * busy". Checked: the mod uses F5-F8 (+Shift) and `; CFC2 binds no function
 * keys; Steam takes F12; the capture tools moved their snapshot key from F9
 * to F11; F10 is Windows' menu key. Spoken and saved like the volume keys. */
static void power_stone_hotkeys_tick(void)
{
    BOOL shift;
    wchar_t say[128];
    if (!key_pressed(VK_F9)) return;
    shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shift) {
        int on = audio_option_cycle(AUDIO_OPT_OFFSCREEN);
        wcsncpy(say, mt(on ? MT_OFFSCREEN_ON : MT_OFFSCREEN_OFF), 127);
        log_line("options: off-screen sounds %s", on ? "on" : "off");
    } else {
        static const mt_id word[3] = { MT_PICKUPS_OFF, MT_PICKUPS_PEOPLE, MT_PICKUPS_ALL };
        int mode = audio_option_cycle(AUDIO_OPT_PICKUP_SPEECH);
        wcsncpy(say, mt(word[mode]), 127);
        log_line("options: pickup announcements %d", mode);
    }
    say[127] = 0;
    speech_say(say, TRUE);
}


/* --- Probe: can we change .text page protection? -------------------------
 * This is the capability Enigma specifically takes away, and the one every
 * later stage depends on (MinHook needs it to write trampolines). Test it
 * directly rather than inferring it from whether a hook "seemed" to work.
 * We flip a page to RWX and immediately restore it — no bytes are written. */
static BOOL probe_can_protect(void)
{
    HMODULE base = GetModuleHandleW(NULL);
    if (!base) {
        log_line("probe: GetModuleHandleW(NULL) failed");
        return FALSE;
    }

    /* Walk the PE headers to find the first executable section. */
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt =
        (IMAGE_NT_HEADERS *)((BYTE *)base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);

    void *target = NULL;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            target = (BYTE *)base + sec[i].VirtualAddress;
            break;
        }
    }
    if (!target) {
        log_line("probe: no executable section found");
        return FALSE;
    }

    DWORD old = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) {
        log_line("probe: VirtualProtect DENIED (err %lu) -- Enigma is "
                 "blocking page protection changes; the bypass is needed",
                 GetLastError());
        return FALSE;
    }
    DWORD tmp = 0;
    VirtualProtect(target, 16, old, &tmp);
    log_line("probe: VirtualProtect OK (target %p, old prot 0x%lx) -- "
             "detours should be installable",
             target, old);
    return TRUE;
}

/* Is <leaf> present next to the game exe? Used to gate experiments behind an
 * explicit switch, so a half-finished one never becomes the reason the game
 * will not start -- a hook here once did exactly that. */
static BOOL opt_in_file_present(const char *leaf)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return FALSE;
    char *slash = strrchr(path, '\\');
    if (!slash)
        return FALSE;
    slash[1] = '\0';
    strncat(path, leaf, MAX_PATH - strlen(path) - 1);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* Log the section map as it stands right now. Cheap, and it is the context
 * every later finding gets interpreted against. */
static void report_sections(const char *when)
{
    image_section secs[CFC2_MAX_SECTIONS];
    int n = image_scan(secs, CFC2_MAX_SECTIONS);
    log_line("image [%s]: base=%p, %d readable sections", when,
             (void *)image_base(), n);
    for (int i = 0; i < n; i++)
        log_line("image [%s]:   rva=0x%-8lx size=%-9lu %s distinct=%d/256 "
                 "prot=0x%-4lx %s",
                 when, secs[i].rva, secs[i].size,
                 secs[i].executable ? "code" : "data",
                 secs[i].distinct, secs[i].protect,
                 secs[i].executable
                     ? (secs[i].unpacked ? "UNPACKED" : "packed")
                     : "");
}

static DWORD WINAPI worker(LPVOID param)
{
    (void)param;

    log_init();
    log_line("=== CFC2 Access -- Phase 1.3: unpacked image dump ===");
    /* ⛔⛔ THE BUILD STAMP. Twice now a run has been analysed against source the
     * game was not running: once because `.\build.ps1` was used without
     * -Deploy, once because the switch file was missing too. Both times the log
     * looked exactly like a FAILED FEATURE -- bare positions, no diagnostics --
     * and the first cost a full round trip before the DLL timestamps were
     * checked.
     *
     * ⭐ A log that cannot say which build wrote it cannot be trusted to report
     * on any change. This is the cheapest possible fix: the compiler's own
     * timestamps, which cannot be stale by construction. Compare it against the
     * build time before concluding ANYTHING from a run. See memory
     * [[a-log-cannot-show-code-written-after-it]]. */
    log_line("build: compiled " __DATE__ " " __TIME__
             "  -- ⚠ if this predates the change you are testing, the game is "
             "running a STALE DLL (build.ps1 -Deploy).");
    log_line("worker thread started, pid=%lu", GetCurrentProcessId());

    BOOL spoke = speech_init();

    /* Before: the image as Enigma hands it to us -- mostly ciphertext. This
     * pair of reports is the evidence that the wait below did something. */
    report_sections("t0");

    /* THE TIMING RULE. .text is ciphertext at DllMain and decrypts shortly
     * after -- measured 531 ms, NOT the ~10 s claimed until 2026-09-08.
     * Everything after this point depends on the wait having succeeded, so a
     * timeout is a hard stop, not a warning we scroll past. */
    BOOL unpacked = image_wait_for_unpack(60000);
    report_sections("unpacked");

    BOOL can_protect = probe_can_protect();

    /* ⭐ THE OFF SWITCH SHIPS WITH THE EXPERIMENT, not after it bites.
     *
     * The pad scan is read-only (VirtualQuery + guarded reads, no hooks, no
     * calls into game code), so it is far lower risk than the detour that
     * broke this game before. But "lower risk" is not "no risk", and the cost
     * of the switch is one file check. Create `cfc2_pad.on` next to the exe
     * to disable it without rebuilding. */
    /* ⛔ OFF BY DEFAULT NOW (was on-unless-disabled). sPad is inert for the
     * SHELL -- Kind=0 on all five records, so the update never runs and every
     * field reads zero. That is a proven negative, not a tooling failure.
     *
     * ⚠ Kept rather than deleted because the IN-GAME work is still ahead, and
     * controller input during an emulated match may take a different path. Turn
     * it on with `cfc2_pad.on`.
     *
     * The per-poll cost this removes is real: nine GetAsyncKeyState calls every
     * 100 ms, feeding a record known to be all zeros. */
    BOOL pad_enabled = opt_in_file_present("cfc2_pad.on");

    /* ⚠ The caller trace is the FIRST thing in this project that writes to
     * game memory (a MinHook detour on user32!GetAsyncKeyState). Its off
     * switch therefore ships WITH it, not after it bites -- a detour in this
     * game once made it refuse to start, and the recovery was a rebuild. With
     * `cfc2_hook.off` next to the exe the mod is read-only again, exactly as
     * every shipped version until now. */
    BOOL hook_enabled = !opt_in_file_present("cfc2_hook.off");

    /* --- The IAT probe -------------------------------------------------
     * The one open question is how the shell reads the keyboard: sPad is inert
     * (Kind = 0 on all five records), and four OFFLINE attempts to answer it
     * failed -- the last because the IAT holds resolved addresses that cannot
     * be matched to names from a dump taken when user32 sat at another base.
     * From inside the process it is a table walk. Run it once, here, where the
     * module set has settled. Read-only; see iat_probe.h. */
    /* ⛔ RETIRED. It answered its one question -- which input API the shell
     * uses -- and the answer was later CORRECTED by disassembly anyway:
     * GetAsyncKeyState is the mouse only, the keyboard arrives via DirectInput.
     * Re-running it cannot teach us anything new, and it cost ~35 log lines
     * every launch. Source kept in tree for the next "who calls X" question. */


    /* --- The caller trace ----------------------------------------------
     * The IAT probe answered WHICH api; this answers WHO calls it. Install
     * only after unpack: the shell's code does not exist as code until then,
     * and hooking user32 before the game has started polling would just
     * accumulate our own calls.
     *
     * ⚠ Installed even though we are also a caller -- see caller_trace.h. Our
     * calls are the hook's liveness proof, not noise to be avoided. */
    /* ⭐ The caller trace is RETIRED -- it did its job. It found exactly one
     * call site inside the exe (0x140253860), and disassembling around it
     * resolved the entire input model offline: sKeyboard, its vtable, the
     * update method, the state builder and the bank layout. Leaving it hooked
     * would cost a per-frame detour for information we already have.
     * It stays in the tree, tested, for the next time a caller question comes
     * up; `ct_install()` is simply no longer called.
     *
     * ⚠ It also corrected the model that produced it: GetAsyncKeyState is the
     * MOUSE path only (5 buttons). The keyboard arrives via DirectInput
     * GetDeviceState inside the builder we now hook instead. */
    BOOL hook_ok = FALSE;
    /* ⚠ The per-change keyboard LOG is opt-in; the hook itself still installs,
     * because the periodic snapshot below reports liveness from it. */
    BOOL kbd_log_on = opt_in_file_present("cfc2_kbd.on");

    /* ⭐ The in-game cursor probe. OFF unless asked for: each arrow press costs
     * a full hash pass over the arena (~100 ms, measured and logged), which is
     * a handful of dropped frames. Acceptable while deliberately probing;
     * not something to impose on ordinary play. */
    g_cursor_on = opt_in_file_present("cfc2_cursor.on");
    if (unpacked && hook_enabled) {
        hook_ok = kbd_install();
        if (!hook_ok)
            log_line("kbd: install FAILED -- nothing was written to game "
                     "memory, and no key state is available.");
    } else if (unpacked) {
        log_line("kbd: DISABLED by cfc2_hook.off next to the exe. Delete that "
                 "file to re-enable the keyboard state watcher.");
    }

    /* --- The menu cursor ------------------------------------------------
     * ⭐⭐ Route 7's answer. Every shell menu moves its selection through one of
     * two generic helpers (0x14007cdc0 vertical, 0x14007c9f0 horizontal), and
     * both receive the SCREEN OBJECT and a POINTER TO THE CURSOR as arguments.
     * Hooking them yields the selected index, the range, and therefore the item
     * count -- with no memory search at all. See menu_cursor.h.
     *
     * ⚠ This also corrects a claim recorded across four closed routes: that no
     * flat 0..N-1 selection index exists in this game. One does; it is a field
     * of the per-screen object, at a different address in every menu, which is
     * exactly why a scan for a single global integer could never find it.
     *
     * Shares cfc2_hook.off, so one file still makes the whole mod read-only. */
    BOOL mc_ok = FALSE;
    if (unpacked && hook_enabled) {
        mc_ok = mc_install();
        if (!mc_ok)
            log_line("mc: install FAILED (reason %d) -- nothing was written to "
                     "game memory, and no menu movement is available.",
                     (int)mc_install_why());
    } else if (unpacked) {
        log_line("mc: DISABLED by cfc2_hook.off next to the exe.");
    }

    /* --- The GUI text reader --------------------------------------------
     * ⭐⭐ The one thing that unblocks most of what is left. ~14 shell screens
     * speak position only, all for the SAME reason: their labels are assigned
     * to GUI text elements at RUNTIME, so no GMD or code search can name them.
     * The control that proved it: the Options screen's labels ARE known (GMD
     * 170..174) and 170 appears nowhere in its function.
     *
     * So let the game hand us an instance instead -- the same move that
     * cracked sKeyboard. See gui_text.h for why slot 5 of cGUIObjText's vtable
     * is the target, and for the uniqueness check that makes every captured
     * instance certainly of this class.
     *
     * ⚠⚠ OPT-IN, and deliberately so. This is the riskiest thing in the
     * project: it reads 528 bytes through a game-supplied pointer. The detour
     * itself dereferences NOTHING -- all reading is deferred to our worker
     * thread under the fault guard -- but the read is still UNPROVEN, and an
     * unproven read on a per-frame path gets a switch until it is proven.
     *
     * ⭐ That gate comes OFF once the string offset is known and the read is
     * targeted. A guard outlives its bug otherwise: the discovery sweep's gate
     * stayed on for days after the cost that justified it was fixed, keeping
     * the only screen-naming tool off in every real session.
     *
     * Shares cfc2_hook.off, so one file still makes the whole mod read-only. */
    /* ⛔ The DISCOVERY candidate sweep: a development aid, OFF by default.
     * It stalled the user's session twice (37 s, then 11.9 s) and has named
     * zero shipped screens. Unknown screens are still logged without it.
     * See menu_speak_set_discovery(). */
    {
        BOOL disc_on = opt_in_file_present("cfc2_discover.on");
        menu_speak_set_discovery(disc_on);
        if (disc_on)
            log_line("mcspeak: ⚠ cfc2_discover.on present -- the DISCOVERY "
                     "candidate sweep is ENABLED. It runs on the poll thread "
                     "and has stalled sessions before; remove the file when "
                     "you are done naming a screen.");
    }

    if (unpacked) cvs2_hit_install();
    if (unpacked && hook_enabled) cvs2_story_install();
    if (unpacked && hook_enabled) cvs2_text_install();
    /* CVS1's, CFJ's and Alpha 3's text is captured by their frame hooks; these only register the ` sources. */
    if (unpacked && hook_enabled) cvs1_text_register();
    if (unpacked && hook_enabled) cfj_text_register();
    if (unpacked && hook_enabled) z3u_text_register();
    /* The story source first: during dialogue it is the line on screen; outside it is inactive. */
    if (unpacked && hook_enabled) mjus_story_register();
    if (unpacked && hook_enabled) mjus_text_register();
    if (unpacked && hook_enabled) {
        if (opt_in_file_present("cfc2_lifetime_probe.on")) collection_probe_install_lifetime();
        else if (opt_in_file_present("cfc2_collection_probe.on")) collection_probe_install();
    }

    /* ⭐ The shell registers LAST, so a running game answers the ` key first and the
     * menus are the fallback. Order here IS the priority order, which is why
     * the registry keeps it explicit rather than sorting by anything clever.
     *
     * ⚠ NOT gated on a hook switch: the shell source repeats what the mod
     * already said, so it needs no hook, and a key that silently does nothing
     * because of an unrelated file is exactly what a blind user cannot
     * diagnose (the same rule as the per-cue volume keys). */
    menu_speak_register_reader();

    /* Gauge cues. Always on: they are part of the mod, not an experiment (they
     * were opt-in via cfc2_audio.on while tuned by ear, which left a release
     * zip without them). The mixer no-ops if the device will not open, and
     * speech is unaffected either way (there is deliberately NO ducking).
     * Each cue family can be muted from its own volume key. */
    if (unpacked) {
        wchar_t dir[MAX_PATH];
        DWORD dn = GetModuleFileNameW(NULL, dir, MAX_PATH);
        if (dn > 0 && dn < MAX_PATH) {
            wchar_t *slash = wcsrchr(dir, (wchar_t)0x5C);
            if (slash) *slash = 0;
            audio_init(dir);
        }
        log_line("audio: %s", audio_ready() ? "cues ON" : "device unavailable");
        log_line("audio: volume keys -- F5 health %d%%, F6 super %d%%, "
                 "F7 beacons %d%%, F8 clock tick %d%%. The key steps UP and "
                 "shift+key steps DOWN, wrapping 0/25/50/75/100, so one press "
                 "from 100%% mutes. Saved to cfc2_audio.ini.",
                 audio_cue_volume(AUDIO_CUE_HEALTH),
                 audio_cue_volume(AUDIO_CUE_SUPER),
                 audio_cue_volume(AUDIO_CUE_BEACON),
                 audio_cue_volume(AUDIO_CUE_TICK));
        /* Health must be read ON the game's frame, not on our poll. */
        if (audio_ready() && hook_enabled) {
            cvs2_frame_install();
            /* CVS1, CFJ and Alpha 3 cues through the same shared controller
             * (cvs1_frame.h, cfj_frame.h, z3u_frame.h). */
            cvs1_frame_install();
            cfj_frame_install();
            z3u_frame_install();
            mjus_frame_install();
            stg2_frame_install();
            pws1_frame_install();
            pws2_frame_install();
        }
    }

    /* Always on: the quit/consent dialogs, the hints view and per-row captions
     * all ride on this hook (see below). It was opt-in via cfc2_guitext.on,
     * which shipped a release with those dialogs silent. */
    BOOL gt_ok = FALSE;
    if (unpacked && hook_enabled) {
        gt_ok = gt_install();
        if (!gt_ok)
            log_line("gt: install FAILED (reason %d) -- nothing was written to "
                     "game memory, and no GUI text element is available.",
                     (int)gt_install_why());
    } else if (unpacked) {
        log_line("gt: DISABLED by cfc2_hook.off next to the exe.");
    }

    /* ⭐⭐ TWO THINGS RODE ON ONE FLAG, AND THEY HAVE OPPOSITE AUDIENCES.
     *
     * `gt_ok` gated 18 call sites. Three of them are SHIPPING SPEECH that a
     * blind player depends on:
     *
     *     menu_speak_check_modal()   the quit / consent dialogs
     *     menu_speak_check_hint()    the hints view
     *     gt_poll_and_log()          feeds gt_take_caption(), i.e. the
     *                                per-row captions menu speech announces
     *
     * The other fifteen are development diagnostics -- focus reports, instance
     * dumps, layout and movelist probes, stats lines.
     *
     * ⛔ Because one file switched both, the only way to stop the diagnostics
     * flooding the log was to turn the hook off, which SILENCED THE QUIT
     * DIALOGS. That is the trade I hit turning `cfc2_guitext.on` off, and the
     * reason I turned it straight back on.
     *
     * ⭐ So: `gt_ok` now means "the hook is live, shipping speech works", and
     * `gt_diag` means "also print the development reports". Diagnostics are
     * OFF by default, which is the direction this project has settled on every
     * other time it asked -- a diagnostic that does not run is an inconvenience
     * to the developer; one that stalls the user's speech is a broken mod.
     *
     * ⭐⭐ AND DECOUPLING IS THE SAFER DEFAULT HERE, not merely the quieter
     * one. gt_focus_report() CLEARS the caption window (see the ordering note
     * at its call site). While it ran unconditionally it was competing with
     * menu_speak for the same captions; with diagnostics off it cannot discard
     * a caption that has not been spoken yet. */
    BOOL gt_diag = gt_ok && opt_in_file_present("cfc2_guitext_diag.on");
    if (gt_ok)
        log_line("gt: hook live -- modal prompts, the hints view and per-row "
                 "captions are ACTIVE. Development reports are %s%s",
                 gt_diag ? "ON (cfc2_guitext_diag.on)" : "off",
                 gt_diag ? ". ⚠ They print per-beat reports and clear the "
                           "caption window; remove that file for ordinary play."
                         : " -- create cfc2_guitext_diag.on to enable them.");

    /* ⭐ The live-text TRANSCRIPT is one of the development reports, so it
     * rides on the same flag rather than carrying a second file of its own.
     * Measured: 612 of one session's 942 `gt:` lines were that one line, ~50 ms
     * apart, because the emulated game redraws NUMBERS and its "log only
     * distinct strings" filter passes every one.
     *
     * ⚠ gt_poll_and_log() itself still runs unconditionally -- it is what
     * feeds gt_take_caption(), and captions are shipping speech. Only the log
     * line inside it is switched. */
    gt_text_log_enable(gt_diag);

    /* ⭐ The focus differ rides on the gt hook -- without the instance set
     * there is nothing to diff -- so it is gated on gt_ok as well as its own
     * file. See gui_text.h for what it measures and why it is not route 3. */
    if (gt_diag) {
        BOOL fd_on = opt_in_file_present("cfc2_focusdiff.on");
        gt_focus_diff_enable(fd_on);
        if (fd_on)
            gt_focus_diff_report();
    }

    /* ⭐⭐ The MOVE LIST membership probe. Also gated on gt_ok: without the
     * instance set there are no widgets to count, so it would report a
     * confident zero about a census it never took.
     *
     * ⚠ The file check lives HERE, not in gui_text.c -- mod.c owns
     * opt_in_file_present(), which resolves the EXE's directory. The two
     * probes that check a bare relative path in gui_text.c work only because
     * the CWD happens to be the game dir; that is not a pattern to copy. */
    if (gt_diag) {
        BOOL ml_on = opt_in_file_present("cfc2_movelist.on");
        gt_movelist_enable(ml_on);
        if (ml_on)
            log_line("mod: cfc2_movelist.on -- the move-list membership probe "
                     "is ENABLED. It counts widgets per cursor movement and "
                     "reports every 10 s. Nothing is spoken.");

        /* ⚠ OPT-IN, like every other diagnostic here. A settings screen is
         * walked deliberately to collect its values; there is no reason to
         * sample one during ordinary play. The lesson that turned the
         * discovery sweep off by default applies: ask whether the expensive
         * thing needs to run AT ALL before making it cheap. */
        /* ⛔⛔ WITHDRAWN: the census CRASHED THE USER'S GAME (see the call site
         * in the poll loop for the evidence and what to investigate). The
         * switch is still read so a run SAYS the feature is withheld rather
         * than appearing to work and silently recording nothing -- a silent
         * no-op is exactly the state a blind user cannot distinguish from a
         * broken one. */
        /* ⭐ The snapshot: opt-in, nothing spoken, one report per screen. */
        BOOL snap_on = opt_in_file_present("cfc2_snap.on");
        gt_snapshot_enable(snap_on);
        if (snap_on)
            log_line("mod: cfc2_snap.on -- the screen snapshot is ENABLED. It "
                     "logs each screen's row labels and values ONCE, on the "
                     "first cursor movement there. Walk in, press a key, walk "
                     "out. Nothing is spoken.");

        if (opt_in_file_present("cfc2_census.on"))
            log_line("mod: ⛔ cfc2_census.on is present but the settings "
                     "census is WITHDRAWN -- it crashed the game in ~1.5 s on "
                     "2026-09-15 (confirmed by isolation; mechanism NOT yet "
                     "established). Nothing is being recorded. Delete the file "
                     "to silence this line.");
        gt_settings_census_enable(FALSE);
    }

    /* ⭐ THE EMULATOR PROBE (world 2, stage 1). Opt-in, READ-ONLY, nothing
     * spoken, nothing hooked. It answers one question the dump cannot: does
     * the anchor derived offline actually resolve in a live process, and does
     * the ROM it names match the game the user chose?
     *
     * ⚠ It is gated because it is a development aid. The lesson that made the
     * discovery sweep opt-in applies: a diagnostic that does not run is an
     * inconvenience to the developer; one that stalls the user's speech is a
     * broken mod. This one is two pointer reads, so the cost is nil -- but it
     * also has nothing to say to a player, which is the real reason to gate it. */
    g_emu_probe_on = opt_in_file_present("cfc2_emu.on");
    if (g_emu_probe_on)
        log_line("mod: cfc2_emu.on -- the EMULATOR probe is ENABLED. Start any "
                 "game and the log reports the emulator state object, the ROM "
                 "it says is running, and the arena. Nothing is spoken.");

    /* ⚠ OPT-IN. The offline tools (disasm.py, find_callers.py,
     * build_riprefs.py, analyze_dump.py) all read this dump, and it is the
     * machinery that cracked sPad, sKeyboard and the cursor chokepoint -- so it
     * is kept, not deleted. But .text is stable across runs (ASLR off, no
     * relocations) and one dump already sits on disk, so rewriting 60 MB every
     * launch buys nothing.
     *
     * ⚠ The DATA sections are NOT stable, and build_riprefs.py refuses a stale
     * index rather than answering wrongly -- so a fresh dump IS occasionally
     * needed. Create `cfc2_dump.on` next to the exe to take one. */
    int dumped = 0;
    if (unpacked && opt_in_file_present("cfc2_dump.on")) {
        dumped = image_dump();
    } else {
        log_line("dump: SKIPPED -- image never unpacked. Any dump taken now "
                 "would be ciphertext that silently looks like a valid file.");
    }

    if (spoke) {
        const wchar_t *backend = speech_backend();
        wchar_t msg[256];
        /* Speak the outcome: the user is blind and running this themselves,
         * and the whole point of the run is to learn whether the dump worked.
         * Making them alt-tab to a log for the headline would be silly. */
        /* ⭐ SAY WHAT MATTERS NOW. This used to announce the IAT probe's
         * findings, which was the right headline when the run existed to answer
         * "which input API". That question is long settled, and the probe is
         * retired. What a user needs to hear today is simply whether the mod
         * can name menu items -- speech backend, and whether the message table
         * resolved. */
        /* The game's language is not known yet this early (the message
         * table resolves later), and an unknown language speaks English. */
        mt_fmt(msg, 256, unpacked ? MT_READY : MT_NOT_UNPACKED,
               backend ? backend : mt(MT_NONE), NULL, NULL);
        speech_say(msg, TRUE);
    }

    log_line("phase 1.3 result: speech=%s protect=%s unpacked=%s "
             "sections_dumped=%d",
             spoke ? "YES" : "NO", can_protect ? "YES" : "NO",
             unpacked ? "YES" : "NO", dumped);

    /* Heartbeat: if Enigma tears the thread down later in startup, the log
     * stopping is itself the finding. */
    int beat = 0;
    if (unpacked && !pad_enabled)
        log_line("pad: OFF -- cfc2_pad.on is absent. sPad is inert for the "
                 "shell (Kind=0, update never runs); create that file only "
                 "for in-game work.");
    if (unpacked && pad_enabled) {
        /* Starts itself and re-seeds until a press makes the record visible.
         * Nothing is asked of the player except to navigate. */
        log_line("pad: dumping the sPad INSTANCE at image+0x%x on every arrow "
                 "EDGE. The mod does NO analysis -- it writes cfc2_pad_NN.bin "
                 "plus a line in cfc2_pad_index.txt recording which arrow keys "
                 "were down. Comparison happens offline. Just navigate.",
                 PAD_INSTANCE_RVA);
    }

    while (!InterlockedCompareExchange(&g_quit, 0, 0)) {
        /* Poll the hotkey at 100 ms so a press is never missed, but only do
         * the (expensive) work on an edge. The 2 s beat below is unchanged. */
        /* ⭐⭐ 200 x 10 ms, not 20 x 100 ms -- the 2 s beat is unchanged.
         *
         * ⛔ USER: "it is impractical for me to wait... there are hundreds of
         * options in there. You will have to make it so that it reads each one
         * as soon as I find it with no delay, while I hold the button down."
         *
         * The movement poll WAS the delay. A keypress moves the cursor on the
         * render thread instantly, but nothing speaks until this loop notices --
         * 0-100 ms, averaging 50. Held on a direction through a 40-item grid
         * that is a stutter behind every single item.
         *
         * ⚠ 20 ms is a FIFTH of the old worst case and still 20x slower than
         * the render thread, so the cheap per-tick work (a sequence compare and
         * a modal check) stays cheap. Everything expensive is gated on the 2 s
         * beat below, which still fires every 100 ticks exactly as before.
         *
         * ⛔ Speech does NOT move to the detour. That runs on the render thread
         * and speech_say() calls into NVDA -- a stall there is a frozen game,
         * which this project has already paid for twice. The poll is the right
         * place; it just had to be faster. */
        /* ⚠ 200 ticks, not 100: the tick is now 10 ms, so the count had to
         * double to keep the outer beat at 2 s. Halving Sleep() alone would
         * have silently made every "2 s" report fire at 1 s -- including the
         * heartbeat, whose whole job is measuring how long things take. */
        for (int t = 0; t < 200; t++) {
            if (InterlockedCompareExchange(&g_quit, 0, 0))
                break;
            /* ⭐ 10 ms, halved from 20: it is the first term in the gauge
             * latency chain (poll + waveOut queue + gain ramp). The user heard
             * the health tone lag the damage; the queue was the main cost and
             * is fixed in audio.h, but the poll is the part that decides how
             * stale the VALUE is before it is ever handed to the mixer. */
            Sleep(10);

            /* ⭐ Cue volume keys, on the 10 ms tick so a press is never
             * missed. Cost when nothing is pressed is four GetAsyncKeyState
             * calls -- the same order as the arrow polling already here. */
            volume_hotkeys_tick();
            power_stone_hotkeys_tick();
            /* ⭐ ` (backtick/grave) = read the current text, and it means the
             * same thing everywhere: the shell repeats the focused row, a
             * running game reads its own screen. mod.c no longer names any
             * single reader -- sources register themselves with the shared
             * channel, so porting another arcade title does not touch this.
             *
             * ⚠ VK_OEM_3 is POSITIONAL, not the character: it is the key left
             * of `1` on a US layout, which is where the user reaches for it by
             * feel. On some layouts that key produces a different character, so
             * the log and docs name it by POSITION as well as by the character `, rather than
             * telling a user to press a character their keyboard may not have
             * there.
             *
             * ⚠⚠ CHECKED, not assumed (CLAUDE.md: the first two hotkey
             * proposals here were both wrong):
             *   - Steam's defaults are Shift+Tab (overlay) and F12
             *     (screenshot). Backtick is neither.
             *   - CFC2's own 16 keyboard-settings rows are gameplay actions on
             *     letters and arrows; no OEM/punctuation key among them.
             *   - the KB_* token vocabulary in gui_markup.c names arrows,
             *     modifiers and navigation keys only -- no grave. (KB_F10/KB_A
             *     there are the comment EXAMPLE, not bindings.)
             *   - MTFramework exposes no dev console here, which is the usual
             *     claimant for this key. */
            if (key_pressed(VK_OEM_3)) {
                DWORD foreground_pid = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
                if (foreground_pid == GetCurrentProcessId()) tc_request_read();
            }

            /* Follow the game's language for the mod's own words (mod_text.h).
             * Once a second: a language change is rare, and the read is a
             * guarded copy of one string. */
            {
                static ULONGLONG last_lang;
                ULONGLONG now_lang = GetTickCount64();
                if (!last_lang || now_lang - last_lang >= 1000) {
                    last_lang = now_lang;
                    mt_update_language(gmd_text_language());
                }
            }
            /* Read actual CVS2 selection on the speech worker, never a hook. */
            cvs2_select_tick();
            /* CVS1, CFJ and Alpha 3 highlights through the same shared channel;
             * each tick admits only its own game, so at most one of them speaks. */
            cvs1_select_tick();
            cfj_select_tick();
            z3u_select_tick();
            mjus_select_tick();
            stg2_select_tick();
            pws1_select_tick();
            pws2_select_tick();
            mjus_story_tick();
            collection_probe_tick();

            /* ⚠ AFTER cvs2_select_tick(), not beside the keypress. That tick is
             * what validates the session and tells each reader whether its game
             * is live; asking a source to read before it has been told would let
             * the ` key answer "not active" for a game that is in fact on screen. */
            if (tc_read_requested()) tc_read_current();
            cvs2_gauges_log_stats();
            cvs2_frame_log_stats();
            cvs1_frame_log_stats();
            cfj_frame_log_stats();
            z3u_frame_log_stats();
            mjus_frame_log_stats();
            stg2_frame_log_stats();
            pws1_frame_log_stats();
            pws2_frame_log_stats();
            cvs2_hit_poll_and_log();

            /* ⭐⭐⭐ THE MODAL CHECK RUNS AT 100 ms, NOT ON THE 2 s BEAT.
             *
             * ⛔⛔ MEASURED FAILURE, user-reported as "I just didn't hear the
             * Yes or No when moving": on the 2 s beat the user pressed arrows
             * SIX times between two polls, the index went 0 -> 1 and back, and
             * the guard -- correctly -- saw no change and said nothing. The
             * announcement only arrived when a press happened to land near a
             * poll boundary.
             *
             * A selection the user moves several times a second cannot be
             * watched on a 2 s clock. Same lesson as the layout probe and the
             * focus differ: the cadence has to fit what the screen DOES.
             *
             * ⚠ Cheap enough to run at this rate: it walks the focus record
             * (bounded, 48 entries) and returns immediately on every screen
             * that has no "Yes" widget, which is all of them but this dialog. */
            if (gt_ok)
                menu_speak_check_modal();

            /* ⭐⭐⭐ THE HINTS VIEW, on the same fast poll and for the same
             * reason: it produces NO cursor movement, so the movement path is
             * blind to it. Measured in the 02:38 run -- the hint was on screen
             * for 4 seconds and the mod said nothing.
             *
             * ⚠ Cheap at this rate: it walks the bounded focus record and
             * returns on the first string that is not an achievement
             * description, which is every screen but this one. */
            if (gt_ok)
                menu_speak_check_hint();

            /* ⭐⭐ THE IN-GAME CURSOR PROBE, driven by the user simply
             * navigating. Opt-in via `cfc2_cursor.on`; speaks nothing.
             *
             * ⚠ On the 20 ms tick, never the 2 s beat: a measured failure in
             * this project had SIX arrow presses land between two 2 s polls
             * with the index returning to its start, so the guard correctly saw
             * no change and said nothing.
             *
             * ⛔ Uses kbd_poll_edges(), NOT the engine's Trg bank. Trg is set
             * for one frame (~16 ms); polling it from here caught roughly one
             * press in six. kbd_poll_edges derives edges from successive `cur`
             * reads, which is correct at any poll rate. */
            if (g_cursor_on && hook_ok)
                cursor_probe_tick();

            /* Watch the verified static path continuously. The last open
             * question -- is that field the selection? -- is then answered by
             * simply navigating the menus, with no protocol for the user to
             * follow and get wrong. Two reads and a compare. */
            /* ⭐⭐ CAPTURE ON THE TRANSITION, not while a key is held.
             *
             * ⚠ This corrects a flaw that probably produced FALSE NEGATIVES in
             * every earlier attempt, and the user spotted it by asking why
             * holding was necessary at all.
             *
             * The old scanners gated on `player_dirs != 0` in TWO places -- the
             * all-zero rejection at seed time and an early return in the tick
             * -- so the pad record had to be nonzero at the exact instant a
             * given address was sampled. The seed sweep takes ~600 ms to cross
             * 1.4 GB and reaches any one address at a single moment in that
             * window, while a keypress lasts ~150 ms. The odds of coinciding
             * were poor even with continuous pressing, and the user was told to
             * "just navigate". A null result from those runs therefore says
             * almost nothing.
             *
             * Two further reasons holding was the wrong instruction:
             *   - holding a direction in a menu triggers AUTO-REPEAT, so the
             *     cursor scrolls and the menu state churns between captures;
             *   - `Trg` is set for a single frame, so a capture taken while a
             *     key is held reads Trg = 0 and measures only On/Old.
             *
             * So the mod now watches the keys itself and captures on the EDGE:
             * the first poll where a direction goes down (Trg still live), and
             * the first poll where it goes up. The user just navigates
             * normally -- no holding, no two-key press, nothing to remember.
             * A measurement that needs the user to remember something is the
             * wrong measurement. */
            /* Read the shell's own decoded key state. Logs only on a change,
             * so the log shows navigation rather than a per-frame flood.
             * ⭐ This is a two-read compare against a pointer the game handed
             * us, not a search -- the whole point of the offline work. */
            /* ⛔⛔ OFF BY DEFAULT (cfc2_kbd.on). This is a DIAGNOSTIC that
             * speaks nothing, and it was the single largest source of log I/O
             * during play: 92 of 210 lines in the 02:38 run, one file write per
             * key change, on the 20 ms poll.
             *
             * ⭐ Its question is ANSWERED and recorded: sKeyboard's state block
             * is this+0x138, the banks are cur/old/Trg/Rel/Chg, and the shell
             * reads DirectInput rather than GetAsyncKeyState. Nothing in the
             * speech path consumes it -- it exists for the in-game work ahead.
             *
             * ⚠ The periodic kbd_snapshot() summary on the slow beat is kept:
             * it is four lines a run and it is the liveness context that makes
             * a null result interpretable. */
            if (hook_ok && kbd_log_on)
                kbd_poll_and_log();

            /* ⭐ The menu cursor. The hook records movements on the game's own
             * thread as they happen, so unlike a poll of a live field this
             * cannot miss a move by sampling between frames.
             *
             * ⚠ But it keeps only the MOST RECENT movement, not a queue: if the
             * user holds a direction and the cursor travels several items
             * between two polls, the intermediate positions are lost and only
             * the latest is reported. That is the right behaviour for speech
             * (announcing every skipped item would lag badly behind the user),
             * but it means the log is not a complete movement history. Said
             * plainly here so a gap in the log is not read as a missed hook. */
            /* Resolve the message blob incrementally, with bounded work per
             * poll so speech and probe draining keep running. It cannot be done at
             * startup: the table is built during load, and the blob's offset
             * MOVES between runs, so it must be found in this process. Until
             * it resolves, movements still speak POSITION, never a guess. */
            if (mc_ok)
                gmd_text_ready();

            if (mc_ok)
                mc_poll_and_log();

            /* ⭐ Inspect at most one new instance per poll. The hook sees
             * every text element every frame; dumping them all would bury the
             * log and cost far more than the finding is worth. One per 100 ms
             * still walks through the elements on screen quickly, because the
             * hook keeps only the most recent and the game cycles through
             * them. */
            if (gt_ok)
                gt_poll_and_log();

            /* ⭐⭐ THE MOVE LIST PROBE SAMPLES AT 100 ms, NOT ON THE 2 s BEAT.
             *
             * The user moves through the move list every ~0.6 s (measured), so
             * a 2 s beat would lose 3-6 movements per beat -- the same failure
             * the modal check hit above, and the reason offline pairing against
             * the 6.6 s layout dump failed 172 of 175 times. At 100 ms this is
             * ~6x oversampled; what it still misses it COUNTS, so a thin
             * sample cannot masquerade as a complete one.
             *
             * ⚠ mc_peek_rest() does NOT consume, so this cannot steal a
             * movement from mc_poll_and_log() above -- which would make the
             * user's speech drop movements to feed a diagnostic.
             *
             * ⭐ The JOIN happens here, not inside gui_text: that module must
             * not depend on menu_cursor (linking the two into the gui_text
             * suite pulls in menu_speak and the GMD reader). mod.c already owns
             * both, so it reads the cursor and hands the answer over. */
            if (gt_diag && mc_ok) {
                mc_move mlm;
                if (mc_peek_rest(&mlm)) {
                    int mltot = mlm.max_index - mlm.min_index + 1;
                    /* 0x140080250 is the move list's VERTICAL axis -- the one
                     * whose total's meaning is the open question. */
                    gt_movelist_probe(mlm.screen, mltot,
                                      mlm.caller == (void *)0x140080250ULL);

                    /* ⭐⭐ THE SETTINGS CENSUS, on the same movement and for the
                     * same reason the probe above runs at 100 ms: the value
                     * cursor moves every ~0.2-0.5 s and the 2 s layout dump
                     * caught ONE value per row out of nine.
                     *
                     * ⚠ The columns are the SETTINGS-SCREEN geometry measured
                     * across 16 dumps (labels X=320, values X=835, 80px pitch),
                     * which Training Options and Game Settings share. Passed in
                     * rather than baked into gui_text: a column X has been
                     * written down as 1265, then 1268, and measured as 633 on a
                     * neighbouring screen -- three values for one number is the
                     * argument against ever hardcoding one.
                     *
                     * ⛔ It records what the game DREW next to each label. It
                     * does not decide which corpus run those values are, or
                     * which tab is showing -- that join stays offline where the
                     * corpus can check it. */
                    /* ⭐⭐ THE SCREEN SNAPSHOT: one report per SCREEN, so a
                     * capture costs a single keypress instead of a 3-second
                     * pause. 320/835 are the settings-screen geometry as a
                     * HINT only -- gt_snapshot() discovers the real columns and
                     * falls back to the leftmost populated one, which is what
                     * CVS1's Game Settings needs (it draws nothing at 320).
                     *
                     * ⚠ Unlike the withdrawn census this does NOT run per
                     * movement: it is silent until a different screen object
                     * appears, which is a human-speed event. */
                    gt_snapshot(mlm.screen, 320.0f, 835.0f, mltot);

                    /* ⛔⛔ THE CENSUS IS NOT CALLED. It CRASHED THE USER'S GAME.
                     *
                     * Confirmed by isolation on 2026-09-15: same build, same
                     * hooks, `cfc2_census.on` removed -> no crash; present ->
                     * dead in ~1.5 s. Two crashes, 0xc0000409 (stack buffer
                     * overrun) then 0xc0000005, both faulting inside the game.
                     *
                     * ⚠ THE MECHANISM IS NOT ESTABLISHED. It compiles clean at
                     * -O2 -Wall -Wextra -Wclobbered, every offline case passes,
                     * and three readings of the code produced three different
                     * unproven theories. Re-enabling it on the strength of a
                     * plausible fix would be the fourth.
                     *
                     * ⭐ It walked the instance table 15 times per sample
                     * (1 + 2 per row), taking and RELEASING g_inst_busy between
                     * every call, on the 100 ms poll, while the ctor hook adds
                     * and evicts instances from the game's own thread -- and
                     * gt_column_item() hands back `.text` POINTERS INTO that
                     * table which outlive the lock. That is the shape to
                     * investigate first, offline, before this is called again.
                     *
                     * ⛔ A diagnostic that crashes the user's game is worth
                     * less than no diagnostic. The code and its cases stay for
                     * the investigation; the CALL does not ship.
                     * See [[fix-the-users-bug-not-the-tooling]]. */
                }
            }

            if (unpacked && pad_enabled) {
                unsigned dirs = 0;
                if (GetAsyncKeyState(VK_UP)    & 0x8000) dirs |= PAD_UP;
                if (GetAsyncKeyState(VK_DOWN)  & 0x8000) dirs |= PAD_DOWN;
                if (GetAsyncKeyState(VK_LEFT)  & 0x8000) dirs |= PAD_LEFT;
                if (GetAsyncKeyState(VK_RIGHT) & 0x8000) dirs |= PAD_RIGHT;
                pad_auto_capture(dirs);
            }

            /* ⛔ The manual F7 force-capture was removed -- see the note at
             * key_pressed(). pad_auto_capture() above still captures on the
             * arrow-key EDGE, which is the path that ever produced anything. */
        }
        beat++;

        /* ⭐ The emulator probe reports on CHANGE, plus once early for
         * liveness. ⚠ A fixed beat schedule has already missed a whole run in
         * this project (beats 10/30/60 covered 2 of 22 movements), and "which
         * ROM is loaded" changes only when the user starts or leaves a game --
         * so the interesting moments are edges, not ticks. */
        if (g_emu_probe_on && (beat == 3 || beat % 5 == 0))
            emu_probe_report(beat);

        /* ⚠ Every 5 beats (10 s), not every 30 (60 s). A session ended with
         * the log stopping mid-navigation and the user reporting the mod had
         * "stopped reading" -- and the heartbeat could not distinguish a dead
         * worker from a quiet one, because the next beat was 40 s away. A
         * liveness signal that sparse cannot answer the question it exists
         * for. */
        if (beat <= 3 || beat % 5 == 0)
            log_line("heartbeat %d (alive %d s)", beat, beat * 2);

        /* Periodic keyboard-state summary: how many times the builder ran and
         * whether the instance was captured. The per-change lines above carry
         * the actual key data; this is the liveness context they need. */
        if (hook_ok && (beat == 3 || beat == 10 || beat == 30 ||
                        beat % 60 == 0)) {
            kbd_capture cap;
            kbd_snapshot(&cap);
            log_line("kbd: builder ran %ld times; instance=%p state=%p "
                     "offset=%s",
                     cap.calls, cap.manager, cap.state,
                     cap.state ? (cap.offset_ok ? "0x138 CONFIRMED"
                                                : "⚠ UNEXPECTED")
                               : "(not captured)");
        }

        /* ⭐⭐ BOTH COUNTS, periodically. "0 instances inspected" is
         * meaningless on its own: with calls == 0 the hook is not
         * intercepting and NOTHING can be concluded, which is a different
         * finding from "inspected elements and none held text". This project's
         * rule is that "could not measure" must never look like "measured
         * nothing". */
        /* ⛔⛔ REPORT ON CHANGE, NOT ON A SCHEDULE.
         *
         * This used to fire only at beats 3, 10, 30 and every 60. A real run
         * ended at beat 25, so the last report was taken at beat 10 -- BEFORE
         * the user had navigated anywhere. It printed "0 calls", which was true
         * of the title screen and said nothing about the menus that followed.
         *
         * ⭐ A measurement whose timing is unrelated to the thing being
         * measured can report a confident zero about a period it never
         * observed. Reporting whenever the count MOVES ties the log to the
         * user's actions instead of to the clock, and the periodic beat is
         * kept only as a liveness floor for when nothing changes at all. */
        static LONG gt_last_calls = -1;
        gt_stats gs_probe;
        if (gt_diag)
            gt_get_stats(&gs_probe);
        /* ⚠ Track the CTOR count too. A run where objects are built but slot 13
         * never fires is the single most informative outcome available, and
         * watching only `calls` would leave it unreported. */
        static LONG gt_last_ctors = -1;
        static LONG gt_last_text = -1;
        BOOL gt_changed = gt_diag && (gs_probe.calls != gt_last_calls ||
                                    gs_probe.ctors != gt_last_ctors ||
                                    gs_probe.with_text != gt_last_text);
        if (gt_diag && (gt_changed || beat == 3 || beat == 10 || beat == 30 ||
                      beat % 60 == 0)) {
            gt_stats gs;
            gt_get_stats(&gs);
            gt_last_calls = gs.calls;
            gt_last_ctors = gs.ctors;
            gt_last_text  = gs.with_text;
            /* ⛔⛔ THE SETTER'S COUNT WAS COLLECTED AND NEVER PRINTED, and it
             * cost a run. The line below was built around the retired ring
             * sweep, so a run could report "0 held TEXT" while saying nothing
             * about whether the text hook had fired even once -- which is the
             * one number that separates "never called" from "called and
             * filtered". Adding a stat to the struct is not adding it to the
             * REPORT; this project's own rule is that a count nobody prints is
             * a count nobody has. */
            log_line("gt: %ld CONSTRUCTED%s; setText ran %ld time(s), %ld "
                     "string(s) captured; %ld guarded read(s) faulted.%s%s%s",
                     gs.ctors,
                     gs.ctor_hooked ? "" : " (⚠ CTOR NOT HOOKED -- this zero "
                                           "measures nothing)",
                     gs.settext_calls, gs.with_text, gs.faults,
                     /* ⭐⭐ The decisive readings, stated in the log so they do
                      * not have to be reconstructed later from raw counts. */
                     (gs.ctor_hooked && gs.ctors == 0)
                         ? "  ⛔⛔ ZERO CONSTRUCTED: the shell never builds this "
                           "class. The cGUIObjText route is CLOSED -- decisive, "
                           "not a missed hook."
                         : "",
                     (gs.ctors > 0 && gs.settext_calls == 0)
                         ? "  ⛔ Objects exist but setText NEVER RAN: the class "
                           "is right and 0x1402f0b50 is not how its text is "
                           "assigned. Find the other writer."
                         : "",
                     (gs.settext_calls > 0 && gs.with_text == 0)
                         ? "  ⛔ setText ran but captured NOTHING: it is firing "
                           "for objects the vptr filter rejects, or the string "
                           "argument is unreadable. NOT a class problem."
                         : "");

            /* ⭐ WHICH overload. Printed whenever any of them has fired, so a
             * nonzero total can never leave the reader guessing which of the
             * four the shell actually uses -- the exact gap that made the
             * previous run unreadable. */
            if (gs.settext_calls > 0)
                log_line("gt:   by overload: cGUIObjText.25=%ld .26=%ld  "
                         "cGUIObjMessage.25=%ld .26=%ld",
                         gs.settext_each[0], gs.settext_each[1],
                         gs.settext_each[2], gs.settext_each[3]);
        }

        /* ⭐⭐ THE FOCUS RECORD -- the open question this build exists to
         * answer: do per-ROW captions flow through setText as the cursor moves?
         *
         * ⚠ On the SLOW beats only, never on gt_changed. The record is a whole
         * ordered transcript, so printing it every time a counter moved would
         * reprint dozens of lines several times a second and bury the log --
         * the exact failure the retired ring sweep caused (1235 refusal lines
         * in one run). It also refuses to conclude when the cursor side never
         * notified it; see gt_focus_report(). */
        /* ⛔ BEATS 10/30/60 MISSED THE RUN. The 22:50 session ended after beat
         * 15, so exactly ONE report fired -- at beat 10, covering 2 of the 22
         * movements the user actually made. The screens of interest were never
         * dumped, and `0 dropped` was measured while only 16 strings existed,
         * so it said nothing about the later ones.
         *
         * ⭐ This is the same shape as the caller trace's old schedule, which
         * a real run outran in exactly the same way. Report on a cadence the
         * user's actual play can reach: every 5 beats (10 s) once there is
         * something to say, plus the early one. A repeat costs a few lines and
         * a missed report costs a launch. */
        /* ⚠⚠ ORDERING DEPENDENCY: this CLEARS the focus window, and
         * mc_poll_and_log() above is what consumes a caption to speak it. That
         * call must stay EARLIER in the loop than this one -- reversing them
         * would let a report discard the caption for a movement that has not
         * been spoken yet, which reads as "captions randomly stop working".
         * They are ~150 lines apart, so the dependency is stated here rather
         * than left to be noticed. */
        /* ⭐ The focus differ samples PER MOVEMENT, not per beat -- a beat
         * cadence would miss most movements. It no-ops unless the cursor moved
         * since the last poll, and unless cfc2_focusdiff.on exists. It runs
         * HERE, on the worker, because the cursor hook must stay two atomics. */
        /* ⛔⛔ AND IT ALSO SAMPLES ON TEXT CHANGE. Per-movement alone was
         * useless for exactly the screen it was most needed on: the quit
         * dialogs produce ZERO cursor movements (measured -- 3 and 8 arrow
         * presses, no movement logged), so a movement-gated differ records
         * nothing there however long the dialog is open. Sending the user to
         * capture it would have wasted the run.
         *
         * ⭐ setText's counter ticks when the screen REDRAWS, which is what a
         * modal does when its selection changes. Same clock the layout probe
         * now uses, and the same lesson: gate on the signal the target screen
         * actually produces, not the one the other screens happen to share. */
        if (gt_diag) {
            static LONG fd_last_sets;
            gt_stats fs;
            gt_get_stats(&fs);
            gt_focus_diff_note_move();
            if (fs.settext_calls != fd_last_sets) {
                fd_last_sets = fs.settext_calls;
                gt_focus_diff_note_text();
            }
        }

        if (gt_diag && (beat == 10 || beat % 5 == 0))
            gt_focus_report();

        /* ⭐⭐ FOLLOW A LANGUAGE CHANGED IN OPTIONS > SYSTEM.
         *
         * ⛔ USER: "Ensure that it will pick the correct tables for all of the
         * languages when its language is selected from settings." The blob is
         * resolved once and gmd_text_ready() returns early forever after, so
         * switching language mid-session left every item name coming from the
         * OLD language -- and nothing looked wrong, because the label hashes
         * are language-independent and every structural check still passed.
         *
         * ⚠ On the SLOW beat (10 s), never the 100 ms poll: this reads
         * config.ini from disk, and a language change is a human-speed event.
         * The common case costs one indexed read plus that file read; only a
         * genuine disagreement pays for the full re-search. */
        if (mc_ok && (beat == 10 || beat % 5 == 0))
            gmd_text_recheck_language();

        /* ⭐⭐ The INSTANCE SET -- step 1 of the GUI-instance route. Logged
         * only, never spoken; see gt_instance_report(). Same cadence as the
         * focus record so one run carries both, and on the slow beats only
         * because it prints one line per live object. */
        if (gt_diag && (beat == 10 || beat % 5 == 0))
            gt_instance_report();

        /* ⭐⭐⭐ MODAL PROMPTS -- the quit dialogs. NOT gated on a switch: this
         * is shipping speech, not a diagnostic.
         *
         * ⚠ It runs on the poll loop rather than off a movement because these
         * dialogs produce NO cursor movements at all; everything else in the
         * speech path is movement-driven and therefore blind to them.
         * menu_speak_check_modal() matches the live text against the two known
         * prompt strings and announces once per appearance, so polling it costs
         * a string compare. */
        if (gt_ok)
            menu_speak_check_modal();

        /* ⭐⭐ The MODAL SELECTION HUNT (cfc2_modal.on, absent = off). Same
         * redraw clock as the other probes -- a dialog is open for ~2 s and a
         * beat cadence misses it entirely, which already cost one run. */
        if (gt_diag)
            gt_modal_probe();

        /* ⭐⭐ The LAYOUT PROBE (cfc2_layout.on, absent = off). Looks for the
         * field that orders objects by SCREEN POSITION.
         *
         * ⛔⛔ IT FIRES ON TEXT CHANGE, NOT ON A TIMER, and that is the whole
         * point. On the 10-beat cadence it MISSED the quit dialog completely:
         * the probe sampled at 13:11:49.420, the dialog drew "No" at
         * 13:11:49.730, and the next sample was ten seconds later -- long after
         * the dialog had closed. A ~2-second dialog cannot be caught by a
         * ~10-second poll, and the user paid a real run to discover that.
         *
         * ⭐ setText's call count is the right clock: it ticks exactly when the
         * screen redraws, which is exactly when a dialog appears. Sampling on
         * CHANGE rather than on time is the same lesson as "report cadence must
         * fit REAL play" -- the beat schedule was picked for menus, and a modal
         * lives on a different timescale.
         *
         * ⚠ Still rate-limited: at most once per beat, so a busy screen (the
         * music player redraws its timer constantly) cannot flood the log. */
        if (gt_diag) {
            static LONG last_sets;
            gt_stats ls;
            gt_get_stats(&ls);
            if (ls.settext_calls != last_sets || beat == 10 || beat % 5 == 0) {
                last_sets = ls.settext_calls;
                gt_layout_report();
            }
        }

        /* ⭐ The move-list probe REPORTS on the slow beats even though it
         * SAMPLES every 100 ms -- the split gt_focus_diff already uses. Its
         * output is an aggregate histogram of a handful of lines regardless of
         * how many movements were sampled, so 10 s cannot flood a log whose
         * every line costs an _wfopen/fclose. One line per movement would be
         * hundreds of file opens.
         *
         * ⚠ Every 5 beats plus an early one -- the cadence mod.c earned after
         * beats 10/30/60 missed an entire run. */
        if (gt_diag && (beat == 10 || beat % 5 == 0))
            gt_movelist_report();

        /* ⛔ The census report is not called either: with the sampler withdrawn
         * it would print only its INCONCLUSIVE line every 10 s, which is noise
         * that reads like a finding. */

        /* ⛔ THE SEPARATE RESCAN IS GONE. It cost EIGHT SECONDS scanning
         * 1.3 GB, and it duplicated work. gmd_text_ready() now advances a
         * bounded, resumable scan, caches the table, and is retried by this
         * poll loop until it succeeds. Two independent retry schedules over the
         * same expensive scan is one too many.
         *
         * ⚠ What is NOT lost: the retry behaviour. gmd_text_ready() returns
         * FALSE until the shell has loaded its message file, and the poll loop
         * keeps calling it -- so a table that is not ready at beat 3 is still
         * picked up later, which was the whole point of retrying. */

    }

    log_line("worker exiting");
    return 0;
}

void mod_start(HINSTANCE self)
{
    (void)self;
    /* DllMain runs under the loader lock: spawn and return immediately.
     * No LoadLibrary, no file I/O, nothing that can take another lock. */
    g_thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
}

void mod_stop(void)
{
    InterlockedExchange(&g_quit, 1);
    /* Do not wait on the thread here: DLL_PROCESS_DETACH also holds the
     * loader lock, and the worker may be inside a call that needs it. */
    if (g_thread) {
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    speech_shutdown();
}
