/* See cvs2_select.h. The CVS2 adapter for the shared channels: native reads,
 * gates and decoding here; settling, grouping, retries, transition policy,
 * cue policy and speech belong to the shared layers. No hooks, no
 * game-memory writes, no keyboard history, no guest-address assumption.
 * CVS2 native-state evidence: docs/cvs2-state-access-trace.md. */
#include "cvs2_select.h"
#include "selection_channel.h"
#include "match_audio.h"
#include "transition_channel.h"
#include "character_name_table.h"
#include "frame_liveness.h"
#include "cvs2_frame.h"
#include "cvs2_story.h"
#include "cvs2_text.h"
#include "text_channel.h"
#include "mod_text.h"
#include "memsafe.h"
#include "audio.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

/* Character names are the shell's own (0x14004B2F0, CVS2 = game 1, limit
 * 0x30), in the regional spelling the shell's game-version setting selects,
 * as every other adapter does (docs/port-tasks/character-names.md). They
 * replaced a Flycast-derived list whose region-neutral Claw / Dictator /
 * Boxer the game itself never shows. */
static void name_of(wchar_t *out, size_t cap, unsigned id, unsigned variant)
{
    const char *n = character_name(1, id, variant);
    swprintf(out, cap, L"%hs", n ? n : "");
}
/* Groove index 0..5 (B+0x5D0), the order of the per-groove meter cap table
 * 0x1406C4138 and the reviewed Flycast list. */
static const wchar_t *const g_groove[6] = {
    L"C Groove", L"A Groove", L"P Groove", L"S Groove", L"N Groove", L"K Groove"
};
/* Reviewed Flycast stage IDs; native stage 11 is explicitly randomized by
 * 0x1406F35B7. Names are not yet checked on a live stage-select screen. */
static const wchar_t *stage_names[12] = {
    L"Aomori",L"Shanghai",L"Nairobi",L"Kinderdijk",L"London",L"Barentsburg",
    L"New York",L"Osaka",L"Osaka Tower Rooftop",L"Osaka In Flames",
    L"Training",L"Random"
};

/* Handler indices from the six tables referenced by 0x141294400.
 * 0x1406F27B0 = groove; 0x1406F2AB0 = character. */
static BOOL alternate_active(const BYTE *s, unsigned p, BOOL groove)
{
    static const int groove_step[6] = {0,1,0,1,-1,1};
    static const int character_step[6] = {1,2,1,2,0,2};
    unsigned b=0x1eba8+p*0x678, mode=s[0x1ffef];
    if (mode >= 6 || memcmp(s+0x78,"\x02\x01\x01\x01",4) ||
        s[0x9c] || !s[0x1a0] || s[0x1ffd0] != 2 ||
        !(s[0x1ffec] & (1u<<p)) || s[b+0x5ec] != p || s[b+4] != 1 ||
        s[0x1ffe4+p] || !(s[0x1ffda] | s[0x1ffdb])) return FALSE;
    return (int)s[0x1ffd4+p] == (groove ? groove_step[mode] : character_step[mode]);
}

int cvs2_stage_decode(const BYTE *s, SIZE_T size)
{
    unsigned p;
    if (!s || size != CVS2_STATE_SIZE) return -1;
    if (memcmp(s+0x78,"\x02\x01\x01\x01",4) || s[0x9c] || !s[0x1a0] ||
        s[0x1ffd0] != 2 || (s[0x1ffef] != 1 && s[0x1ffef] != 3) ||
        !(s[0x1ffda] | s[0x1ffdb])) return -1;
    for (p=0; p<2; p++) {
        unsigned b=0x1eba8+p*0x678, other=0x1eba8+(1-p)*0x678;
        if ((s[0x1ffec] & (1u<<p)) && s[b+0x5ec] == p &&
            s[0x1ffd4+p] == 0 && s[b+4] == 3 &&
            s[other+4] <= 3 && !s[0x1ffe4+p])
            return s[0x64c] < 12 ? s[0x64c] : -2;
    }
    return -1;
}

int cvs2_groove_decode(const BYTE *s, SIZE_T size, unsigned p)
{
    unsigned b, groove;
    if (!s || size != CVS2_STATE_SIZE || p > 1) return -1;
    b = 0x1eba8+p*0x678;
    if (s[0x1a0]) {
        if (!alternate_active(s,p,TRUE)) return -1;
        groove=s[b+0x5d0];
        return groove < 6 ? (int)groove : -2;
    }
    if (memcmp(s+0x78,"\x02\x01\x01\x01",4) || s[0x9c] ||
        s[0x1a0] || s[0x17c] != s[0x1ffec] ||
        !(s[0x17c] & (1u << p)) || s[0x1ffd0] != 2 ||
        s[0x1ffd8] > 2 || s[0x1ffd4+p] != 1) return -1;
    b = 0x1eba8+p*0x678;
    if (s[b+0x5ec] != p) return -2;
    groove = s[b+0x5d0];
    return groove < 6 ? (int)groove : -2;
}

/* 0x1412A0210, consumed by 0x140735B30. Values here are the displayed
 * ratios (the game stores them minus one). The third entry is unused for
 * two-member teams. */
static const unsigned ratios[6][3] = {
    {2,2,0},{3,1,0},{1,3,0},{2,1,1},{1,2,1},{1,1,2}
};

int cvs2_ratio_decode(const BYTE *s, SIZE_T size, unsigned p)
{
    unsigned b, count, choice, i;
    if (!s || size != CVS2_STATE_SIZE || p > 1) return -1;
    b = 0x1eba8+p*0x678;
    if (s[0x1a0]) {
        /* 0x1406F3200: four individual ratios, default index 1. Unlike
         * team ratio selection, this handler does not test the menu timer. */
        static const int steps[6] = {2,3,-1,3,-1,3};
        unsigned mode=s[0x1ffef];
        if (mode >= 6 || memcmp(s+0x78,"\x02\x01\x01\x01",4) ||
            s[0x9c] || s[0x1ffd0] != 2 ||
            !(s[0x1ffec] & (1u<<p)) || s[b+0x5ec] != p ||
            (int)s[0x1ffd4+p] != steps[mode] || s[b+4] != 1 ||
            s[0x1ffe4+p]) return -1;
        choice=s[0x1ffe6+p];
        if (mode == 3) return choice < 4 ? 6+(int)choice : -2;
        /* 0x1406F2F60: the same team layouts as Arcade. */
        if (s[0x1a3] || !(s[0x1ffda] | s[0x1ffdb]) ||
            s[b+0x5d1] == 1) return -1;
    } else if (memcmp(s+0x78,"\x02\x01\x01\x01",4) || s[0x9c] ||
        s[0x17c] != s[0x1ffec] || !(s[0x17c] & (1u << p)) ||
        s[0x1ffd0] != 10 || s[0x1ffd2] != 1 ||
        s[0x1ffd4+p] != 2 || s[0x1ffe8] == 0x30 ||
        !(s[0x1ffdc] | s[0x1ffdd])) return -1;
    b = 0x1eba8+p*0x678;
    count = s[b+0x5d1]; choice = s[0x1ffe6+p];
    if (s[b+0x5ec] != p || count < 2 || count > 3 || choice > 2) return -2;
    for (i=0; i<count; i++) if (s[b+0x5c0+i] >= 48) return -2;
    return (int)((count-2)*3+choice);
}

int cvs2_select_decode(const BYTE *s, SIZE_T size, unsigned p)
{
    unsigned b, col, row, id, step;
    if (!s || size != CVS2_STATE_SIZE || p > 1) return -1;
    b = 0x1eba8+p*0x678;
    if (s[0x1a0]) {
        if (!alternate_active(s,p,FALSE)) return -1;
        /* Alternate grid uses 12 columns and its own table. The handler
         * publishes the resolved cell ID here before writing B+1. */
        id=s[0x1ffd6+p];
        return id < 48 && s[b+1] == id ? (int)id : -2;
    }
    step = s[0x1ffd4+p];
    if (memcmp(s + 0x78, "\x02\x01\x01\x01", 4) || s[0x9c] ||
        s[0x1a0] || s[0x17c] != s[0x1ffec] ||
        !(s[0x17c] & (1u << p)) || s[0x1ffd0] != 7 ||
        s[0x1ffd8] > 2 || (step != 1 && step != 4 && step != 7) ||
        s[0x1ffe4+p])
        return -1;
    b = 0x1eba8 + p * 0x678;
    col = s[0x1ffde + p*2]; row = s[0x1ffdf + p*2];
    if (s[b+0x5ec] != p || col >= 16 || row >= 8) return -2;
    id = s[0x1fff0+row*16+col];
    if (id >= 48 || s[b+1] != id) return -2;
    return (int)id;
}

void cvs2_selection_copy(const BYTE *s, SIZE_T size, unsigned variant,
                         game_selection_choice out[GAME_SELECTION_OWNERS])
{
    unsigned p;
    int stage;
    if (!out) return;
    memset(out, 0, sizeof(*out) * GAME_SELECTION_OWNERS);
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i)
        out[i].status = GAME_SELECTION_INACTIVE;
    if (!s || size != CVS2_STATE_SIZE) return;
    /* The stage menu is shared: spoken once on its own owner. While it is
     * open the sides say nothing, as before. */
    stage = cvs2_stage_decode(s, size);
    if (stage == -2) out[CVS2_STAGE_OWNER].status = GAME_SELECTION_UNAVAILABLE;
    if (stage >= 0) {
        game_selection_choice *o = &out[CVS2_STAGE_OWNER];
        o->status = GAME_SELECTION_VALID;
        o->kind = CVS2_KIND_STAGE;
        o->value = (unsigned)stage;
        /* "Stage: " is the collection's own word; Random is too. The other
         * stage names are places, spoken as the game names them. */
        swprintf(o->label, GAME_SELECTION_LABEL, L"%ls%ls", mt(MT_STAGE_PREFIX),
                 stage == 11 ? mt(MT_RANDOM) : stage_names[stage]);
        return;
    }
    for (p = 0; p < 2; p++) {
        game_selection_choice *o = &out[p];
        unsigned b = 0x1eba8 + p * 0x678;
        int v;
        /* A new team slot is a new selection even if the intervening
         * confirmation animation was missed or its highlight is unchanged. */
        o->visit = s[0x1ffd4 + p];
        if ((v = cvs2_select_decode(s, size, p)) != -1) {
            if (v < 0) { o->status = GAME_SELECTION_UNAVAILABLE; continue; }
            o->kind = CVS2_KIND_CHARACTER;
            if (s[0x1a0]) o->visit += 256u * s[b + 0x5d1];
            name_of(o->label, GAME_SELECTION_LABEL, (unsigned)v, variant);
        } else if ((v = cvs2_groove_decode(s, size, p)) != -1) {
            if (v < 0) { o->status = GAME_SELECTION_UNAVAILABLE; continue; }
            o->kind = CVS2_KIND_GROOVE;
            swprintf(o->label, GAME_SELECTION_LABEL, L"%ls", g_groove[v]);
        } else if ((v = cvs2_ratio_decode(s, size, p)) != -1) {
            if (v < 0) { o->status = GAME_SELECTION_UNAVAILABLE; continue; }
            if (v >= 6) {
                /* The user identifies this as handicap. The native storage is
                 * shared with ratios, but the visible heading is not verified. */
                o->kind = CVS2_KIND_LEVEL;
                v -= 6;
                mt_fmt_n(o->label, GAME_SELECTION_LABEL, MT_LEVEL_N, v + 1, 0);
            } else {
                unsigned i, n = s[b + 0x5d1];
                o->kind = CVS2_KIND_RATIO;
                swprintf(o->label, GAME_SELECTION_LABEL, L"%ls", mt(MT_RATIOS));
                for (i = 0; i < n; i++) {
                    wchar_t name[64], entry[80];
                    name_of(name, 64, s[b + 0x5c0 + i], variant);
                    swprintf(entry, 80, L"%ls%ls %u", i ? L", " : L"", name, ratios[v][i]);
                    wcscat(o->label, entry);
                }
            }
        } else {
            o->visit = 0;
            continue;
        }
        o->status = GAME_SELECTION_VALID;
        o->value = (unsigned)v;
    }
}

void cvs2_select_observe(const BYTE *s, SIZE_T size, unsigned variant,
                         ULONGLONG root, unsigned rom)
{
    static selection_channel channel;
    static ULONGLONG previous_root;
    static unsigned previous_rom;
    game_selection_choice choices[GAME_SELECTION_OWNERS];
    if (!s || size != CVS2_STATE_SIZE || !root) {
        selection_channel_depart(&channel);
        previous_root = 0;
        return;
    }
    /* Compatibility identity only: preserves the existing root-based reset.
     * It does NOT detect same-address replacement or protect native lifetime. */
    if (!channel.session.owned || root != previous_root || rom != previous_rom) {
        if (!selection_channel_begin(&channel, 1, rom, "cvs2-select")) return;
        previous_root = root;
        previous_rom = rom;
    }
    cvs2_selection_copy(s, size, variant, choices);
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < GAME_SELECTION_OWNERS; ++p) if (accepted & (1u << p))
        log_line("cvs2-select: accepted owner=%u kind=%u value=%u", p + 1,
                 choices[p].kind, choices[p].value);
}

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out,(const void *)(ULONG_PTR)address,size); }

/* ⛔⛔ THIS TICK RAN AT 100 Hz AND DELAYED THE USER'S SPEECH.
 *
 * The user reported keypress announcements lagging again. Counted rather than
 * profiled, which is this project's rule (CLAUDE.md: "before any loop over
 * memory, COUNT THE SYSCALLS it will make"):
 *
 *     10 read_at() per tick      -> >= 1000 VirtualQuery per SECOND
 *     137067 bytes per tick      -> ~13 MB/s of guarded memcpy
 *
 * That is the FIFTH time this project has hit the VirtualQuery wall, and the
 * first four all presented the same way: the worker is alive but too busy to
 * notice a cursor movement promptly, so speech trails the keypress.
 *
 * ⭐ THE THREE FIXES, none of which weakens a guard:
 *
 * 1. THE SIGNATURE CHECK IS HOISTED. It reads 11 bytes of `.text` at a fixed
 *    address to confirm the audited build. `.text` is immutable here (ASLR
 *    off, no relocations, Enigma decrypts once before we ever run), so its
 *    answer cannot change between ticks. Checking it 100 times a second is
 *    100 times more often than it can possibly differ. Now checked ONCE and
 *    latched; a failure latches too, so a wrong build still never decodes.
 *
 * 2. THE 134 KB COPY IS GATED BEHIND THE CHEAP CHECKS. It already sat behind
 *    them textually, but C's `||` chain put it in the SAME expression as the
 *    pointer comparisons, so it ran whenever those passed -- which is every
 *    tick of an active match, i.e. exactly when speech matters most. Split
 *    out so the arithmetic decides before 134 KB moves.
 *
 * 3. THE STATE COPY IS RATE-LIMITED TO 30 Hz. ⚠ This is the one that needs
 *    justifying, because a slower poll is what caused a past defect here
 *    (six arrow presses between two 2 s polls, the index returned to its
 *    start, nothing was said).
 *
 *    ⭐ 33 ms is not that. It is FASTER than the game's own 60 fps frame, so
 *    no selection state can appear and vanish between two samples: the menu
 *    holds a highlight for as long as the player stands on it, which is
 *    hundreds of milliseconds at minimum. The failure mode above needed a
 *    2000 ms gap, sixty times larger.
 *
 *    ⚠ And no gauge comes through here -- cvs2_frame.c feeds health, the
 *    super meter, beacons and the clock on the game's own frame hook
 *    precisely so this poll's rate cannot make them stale.
 *
 * ⛔ What was NOT done: dropping mem_readable() before the memcpy. That was
 * tried in this project and the test written to prove it safe CRASHED -- a
 * guarded memcpy across a page boundary is NOT caught (case C in
 * run_span_test.ps1). The fault guard is a net for the race between the check
 * and the copy, never a replacement for the check. Every bulk read here still
 * calls mem_copy(). */
#define SELECT_POLL_MS 33u

#define SESSION_SLOT 0x141c14f98ULL   /* emulator session singleton          */
#define SHELL_SLOT   0x141c14f90ULL   /* shell game-select object (+0x1AC game, +0x1B0 version) */
#define ROOT_SLOT    0x1422debd0ULL   /* CVS2 published root                 */
#define PHASE_SLOT   0x1422dec30ULL   /* == root + 0x78                      */
/* A frame counter unchanged this long means the update is not running:
 * paused, or a quit game's frozen block (frame_liveness.h). */
#define STALL_MS 250u

/* Match-audio ownership, worker only, as every adapter does it: a new epoch
 * per presentation owner -- the same compatibility identity the selection
 * channel uses (root + ROM), so leaving CVS2, a relocated block or a ROM
 * change silences every cue the old owner fed, including frame-thread writes
 * racing the switch (audio.c drops any write stamped with a replaced epoch).
 * Not a native lifetime claim. The frame hook feeds only an epoch naming
 * game 1; when its update stops running the beacons are turned off after
 * STALL_MS, since a stopped hook can no longer report a pause itself. */
static void cvs2_audio_route(ULONGLONG root, unsigned rom)
{
    static uint64_t epoch;
    static ULONGLONG owner_root;
    static unsigned owner_rom;
    static LONG last_frames;
    static ULONGLONG last_change;
    static BOOL silenced;
    ULONGLONG now = GetTickCount64();
    LONG frames;
    if (!root) {
        if (epoch) match_audio_depart(epoch);
        epoch = 0;
        return;
    }
    if (!epoch || root != owner_root || rom != owner_rom ||
        match_audio_current() != epoch) {
        epoch = match_audio_begin(1);
        owner_root = root;
        owner_rom = rom;
        last_change = now;
        silenced = FALSE;
    }
    frames = cvs2_frame_count();
    if (frames != last_frames) {
        last_frames = frames;
        last_change = now;
        silenced = FALSE;
    } else if (epoch && !silenced && now - last_change >= STALL_MS) {
        for (unsigned p = 0; p < 2; p++)
            audio_beacon_set_owned(epoch, p, FALSE, 0.0f, 1.0f);
        silenced = TRUE;
    }
}

void cvs2_select_tick(void)
{
    static BYTE state[CVS2_STATE_SIZE];
    static int guard_ready;
    /* 0 = not yet checked, 1 = this is the audited build, -1 = it is not. */
    static int sig_ok;
    static DWORD last_poll;
    BYTE header[0x60];
    ULONGLONG session, arena, context, pointers[4], root, phase, again, shell;
    unsigned game, rom, shell_game, variant;
    DWORD now;

    if (!guard_ready) {
        if (!mem_guard_install()) return;
        guard_ready = 1;
    }

    /* ⭐ ONCE. See note 1 above: `.text` cannot change between ticks, so this
     * answer is a property of the build, not of the moment. */
    if (sig_ok == 0) {
        BYTE sig[11];
        const BYTE expected[11] =
            {0x48,0x8b,0x48,0x10,0x48,0x89,0x0d,0x90,0x48,0xc3,0x01};
        if (!read_at(sig,0x1406aa335ULL,sizeof sig))
            return;              /* could not look: not a negative, retry */
        sig_ok = memcmp(sig,expected,sizeof sig) == 0 ? 1 : -1;
        if (sig_ok < 0)
            log_line("cvs2-select: ⛔ the bytes at 0x1406aa335 differ from the "
                     "audited build -- refusing to decode this build's state. "
                     "Nothing will be spoken from the emulated game.");
    }
    if (sig_ok < 0) return;

    /* ⭐ ONE 8-byte read decides whether anything at all is worth doing.
     *
     * ⚠ This read stays on EVERY tick on purpose, and it is the only one that
     * does. Leaving a game nulls the session slot, and the gauges must be
     * silenced promptly when that happens -- a tone holding over the screen
     * that follows is exactly the "cue at the wrong moment" failure this
     * project keeps guarding against. Throttling the INACTIVE path would
     * reintroduce it. Throttling the ACTIVE path costs nothing, because an
     * active match is precisely when there is nothing new to decide. */
    if (!read_at(&session,SESSION_SLOT,8) || !session)
        goto inactive;

    /* Active. Everything below -- eight more guarded reads and a 134 KB copy
     * -- happens at SELECT_POLL_MS, not at the 10 ms tick.
     *
     * ⚠ Returning here is NOT "inactive": nothing is reset and nothing is
     * said, because the state we already published is still current. */
    now = GetTickCount();
    if (last_poll && now - last_poll < SELECT_POLL_MS)
        return;
    last_poll = now;

    if (!read_at(header,session,sizeof header))
        goto inactive;
    memcpy(&arena,header,8); memcpy(&game,header+8,4); memcpy(&rom,header+0x44,4);
    if (game != 1 || (rom != 2 && rom != 3) ||
        !read_at(&context,0x142c79b40ULL,8) ||
        !read_at(pointers,context,sizeof pointers) ||
        !read_at(&root,ROOT_SLOT,8) || !read_at(&phase,PHASE_SLOT,8))
        goto inactive;
    if (arena > 0x00007fff00000000ULL || context != arena+0x8000000 ||
        pointers[0] != arena || pointers[1] != arena+0x8400000 ||
        pointers[2] != root || phase != root+0x78 ||
        root < arena+0xc000000 || root > arena+0x10000000-CVS2_STATE_SIZE)
        goto inactive;

    /* The shell's game-select object, as every adapter reads it: the game
     * it names, and the game-version setting the shell's name function
     * picks regional names by. */
    if (!read_at(&shell,SHELL_SLOT,8) || !shell ||
        !read_at(&shell_game,shell+0x1AC,4) || shell_game != 1 ||
        !read_at(&variant,shell+0x1B0,4) || variant > 1)
        goto inactive;

    if (!read_at(state,root,sizeof state) ||
        !read_at(&again,ROOT_SLOT,8) || again != root ||
        !read_at(&again,SESSION_SLOT,8) || again != session)
        goto inactive;
    {
        /* Evidence for listening: the ROM loaded beside the version the
         * names follow, once per pair. */
        static unsigned logged = ~0u;
        unsigned pair = rom * 2 + variant;
        if (pair != logged) {
            logged = pair;
            log_line("cvs2-select: admitted ROM %u (%s), shell game version %u",
                     rom, rom == 3 ? "cvs2u" : "cvs2", variant);
        }
    }
    cvs2_audio_route(root,rom);
    /* Only while the update runs: a quit game's block stays frozen on its
     * last screen and passes every gate above (frame_liveness.h). A stall
     * departs, so a resumed or restarted select speaks its highlight. */
    {
        static frame_liveness live;
        if (frame_liveness_live(&live, cvs2_frame_count(), GetTickCount64(), STALL_MS))
            cvs2_select_observe(state,sizeof state,variant,root,rom);
        else
            cvs2_select_observe(NULL,0,0,0,0);
    }
    cvs2_announce_observe(state,sizeof state,variant,root);
    cvs2_quote_observe(state,sizeof state,root);
    cvs2_story_observe(state,sizeof state,root);
    cvs2_text_observe(root);
    return;
inactive:
    last_poll = 0;
    cvs2_audio_route(0,0);
    cvs2_select_observe(NULL,0,0,0,0);
    cvs2_announce_observe(NULL,0,0,0);
    cvs2_quote_observe(NULL,0,0);
    cvs2_story_observe(NULL,0,0);
    cvs2_text_observe(0);
}

/* ---- In-match announcements --------------------------------------------
 * Speech OUTSIDE the fight only: team/groove when a match starts, and the
 * fighter + its ratio when a round starts. The audio design forbids narrating
 * DURING play (a screen reader is too slow and masks the game's own sound);
 * these fire at natural pauses, which is the case that rule was not about.
 * Gauges stay cues. */

/* Formatting, grouping and acceptance-gated latching are shared --
 * src/transition_channel.c. This adapter keeps CVS2's phase gate, names,
 * grooves, team slots and the ratio-budget rule below. */
static transition_channel g_an;

/* Ratio for team slot `i`, as DISPLAYED. The game stores ratio-1, and the
 * layout table already holds displayed values, so B+0x5CC..E needs +1. */
/* ⭐⭐ DOES THIS TEAM ACTUALLY HAVE RATIOS?
 *
 * ⛔ NOT a question of team size. The user corrected me twice here: a plain
 * 3-on-3 team battle has NO ratios (every fighter is equal), but Ratio Match
 * with a 3-member team DOES -- the live hp-full capture stores 1/1/2. Gating
 * on "size < 2", or on "size < 3", would have been wrong in one direction or
 * the other.
 *
 * The real invariant is CVS2's ratio BUDGET: a genuine allocation always
 * spends exactly 4 points across the team (3 members 1+1+2, 2 members 2+2 /
 * 3+1 / 1+3, 1 member 4). That matches the traced layout table at
 * 0x1412A0210. A mode without ratios leaves the field holding something that
 * does not sum to 4 -- the 1v1 training capture stores 1/1/1, which sums to 2
 * over one member and would have been announced as "ratio 2".
 *
 * So: announce ratios only when the numbers form a real allocation. This is
 * the same principle as refusing to speak a gauge whose ceiling is unknown --
 * say nothing rather than a number the player does not actually have. */
static int an_has_ratios(const BYTE *s, unsigned b)
{
    unsigned n = s[b + 0x5d1], i, sum = 0;
    if (n < 1 || n > 3) return 0;
    for (i = 0; i < n; i++) {
        unsigned r = s[b + 0x5cc + i];
        if (r == 255 || r > 3) return 0;
        sum += r + 1;
    }
    return sum == 4;
}

static unsigned an_ratio(const BYTE *s, unsigned b, unsigned i)
{
    unsigned r;
    if (!an_has_ratios(s, b)) return 0;
    r = s[b + 0x5cc + i];
    return r == 255 ? 0 : r + 1;
}

/* ⭐ WHO IS "YOU"? 0x1FFEC is the joined-player mask the selection handlers
 * test per side (`s[0x1ffec] & (1u<<p)`). In-match it reads 0x1 in every
 * Arcade capture (P1 human vs CPU) and 0x0 in every Training capture, where
 * it identifies nobody. So exactly ONE bit names the local side; anything else
 * (Training, two humans) is unknown and speaks "Player 1/2" rather than
 * assuming the listener is P1. */
static transition_ownership an_ownership(const BYTE *s, unsigned p)
{
    unsigned mask = s[0x1ffec] & 3u;
    if (mask != 1u && mask != 2u) return TRANSITION_OWNER_UNKNOWN;
    return (mask & (1u << p)) ? TRANSITION_OWNER_LOCAL : TRANSITION_OWNER_OPPONENT;
}

void cvs2_announce_observation(const BYTE *s, unsigned variant, transition_observation *o)
{
    unsigned p, i;
    memset(o, 0, sizeof *o);
    o->team_ready = true;
    for (p = 0; p < 2; p++) {
        unsigned b = 0x1eba8 + p * 0x678, n = s[b + 0x5d1];
        transition_side *side = &o->side[p];
        int id = s[b + 1];
        side->present = true;
        side->player = p + 1;
        side->ownership = an_ownership(s, p);
        /* The introduction needs a real team and groove; until then only
         * fighter changes speak, as before. */
        if (n < 1 || n > 3 || s[b + 0x5d0] >= 6) {
            o->team_ready = false;
        } else {
            swprintf(side->style, TRANSITION_LABEL, L"%ls", g_groove[s[b + 0x5d0]]);
            side->count = n;
            for (i = 0; i < n; i++) {
                unsigned m = s[b + 0x5c0 + i];
                if (m >= 48) continue;
                side->member[i].valid = true;
                side->member[i].id = m;
                side->member[i].ratio = an_ratio(s, b, i);
                name_of(side->member[i].name, TRANSITION_LABEL, m, variant);
            }
        }
        if (id >= 0 && id < 48) {
            unsigned slot;
            /* Which team slot is this fighter, so the ratio matches. */
            for (slot = 0; slot < n && slot < 3; slot++)
                if ((int)s[b + 0x5c0 + slot] == id) break;
            side->current.valid = true;
            side->current.id = (unsigned)id;
            side->current.ratio = slot < 3 ? an_ratio(s, b, slot) : 0;
            name_of(side->current.name, TRANSITION_LABEL, (unsigned)id, variant);
        }
    }
}

void cvs2_announce_observe(const BYTE *s, SIZE_T size, unsigned variant, ULONGLONG root)
{
    static ULONGLONG owner_root;
    transition_observation o;

    if (!s || size != CVS2_STATE_SIZE || !root) {
        transition_channel_depart(&g_an);
        owner_root = 0;
        return;
    }
    /* A new state block is a new presentation owner: re-announce everything
     * (the same compatibility identity the other CVS2 channels use). */
    if (!g_an.begun || root != owner_root) {
        transition_channel_begin(&g_an, "cvs2-announce");
        owner_root = root;
    }
    /* Only in an actual match. Phase bytes 02 01 02 02; selection screens and
     * quotes use other phases. Latches survive between matches, so the same
     * teams are not re-introduced but a new opponent's team is. */
    if (memcmp(s + 0x78, "\x02\x01\x02\x02", 4)) return;
    cvs2_announce_observation(s, variant, &o);
    transition_channel_offer(&g_an, &o);
}

/* ---- Win quotes --------------------------------------------------------- */
/* Native draw buffers, traced in docs/cvs2-win-quote-reader.md. No table lookup
 * or pointer chasing: these bytes already belong to the guarded state copy. */
/* ⭐ The "two identical reads settle it" policy is shared -- tc_settle in
 * src/text_channel.h. This reader keeps only the CVS2 decode. */
static tc_settle g_wq;

static unsigned quote_word(const BYTE *s, unsigned off)
{
    return s[off] | ((unsigned)s[off+1] << 8);
}

/* Each line is the game's own text: ASCII on the US ROM and Shift-JIS on
 * the Japanese one, whose quote table (cvs2 data around 0x1A2440, e.g.
 * "ここからが本当の勝負だ！") is plain NUL-terminated CP932. So a line is
 * decoded as CP932, which leaves ASCII unchanged; control bytes or an
 * invalid sequence still refuse the whole quote rather than misread it. */
BOOL cvs2_quote_decode(const BYTE *s, SIZE_T size, wchar_t *out, SIZE_T capacity)
{
    unsigned mode, lines, line, used=0;
    if (!out || capacity < 161) return FALSE;
    out[0]=0;
    if (!s || size != CVS2_STATE_SIZE ||
        memcmp(s+0x78,"\x02\x01\x02\x03\x02",5) ||
        (s[0x20f50] != 3 && s[0x20f50] != 4)) return FALSE;
    mode=quote_word(s,0x20f3e);
    lines=quote_word(s,0x20f40);
    if (mode > 2 || lines < 1 || lines > 2) return FALSE;
    if (mode == 0) lines=2; /* two-speaker branch always draws both */
    for (line=0; line<lines; line++) {
        unsigned n=quote_word(s,0x20f54+line*2), i;
        const BYTE *text=s+0x20f5c+line*0x50;
        if (!n || n >= 0x50 || quote_word(s,0x20f58+line*2) != n ||
            text[n]) { out[0]=0; return FALSE; }
        {
            char raw[0x50];
            wchar_t wide[0x50];
            SIZE_T w;
            for (i=0; i<n; i++) {
                if (text[i] < 32 || text[i] == 127) { out[0]=0; return FALSE; }
                raw[i]=(char)text[i];
            }
            raw[n]=0;
            if (!tc_decode_cp(932, raw, wide, 0x50)) { out[0]=0; return FALSE; }
            w=wcslen(wide);
            if (used + w + 2 > capacity) { out[0]=0; return FALSE; }
            if (line && used && out[used-1] != L' ' && wide[0] != L' ' && wide[0] < 0x80)
                out[used++]=L' ';
            memcpy(out+used, wide, w*sizeof(wchar_t));
            used+=(unsigned)w;
        }
    }
    out[used]=0;
    return used != 0;
}

void cvs2_quote_observe(const BYTE *s, SIZE_T size, ULONGLONG root)
{
    wchar_t text[161];
    if (!s || size != CVS2_STATE_SIZE || !root ||
        memcmp(s+0x78,"\x02\x01\x02\x03",4) || s[0x7c] < 2) {
        tc_settle_reset(&g_wq);
        return;
    }
    if (!cvs2_quote_decode(s,size,text,161)) {
        /* A failed decode breaks the pair, so a torn read cannot pair with a
         * later identical one across the interruption. */
        tc_settle_break(&g_wq);
        return;
    }
    if (!tc_settle_ready(&g_wq,root,text)) return;
    if (tc_say(text,FALSE,"cvs2-quote")) tc_settle_mark(&g_wq);
}
