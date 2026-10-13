#include "selection_channel.h"
/* CVS2 native-state evidence: docs/cvs2-state-access-trace.md.
 * No keyboard history, guest-address assumption, or game-memory writes. */
#include "cvs2_select.h"
#include "cvs2_story.h"
#include "cvs2_text.h"
#include "text_channel.h"
#include "memsafe.h"
#include "speech.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

/* Zero-based game IDs, from the reviewed Flycast Dojo cvs2.lua CHARACTER
 * list (build/prior-art/SamuelioX-cvs2.lua). Ryu, Sagat and Yun additionally
 * checked live. Claw/Boxer/Dictator retain unambiguous regional labels. */
static const wchar_t *names[54] = {
    L"Ryu", L"Ken", L"Chun-Li", L"Guile", L"Zangief", L"Dhalsim",
    L"E. Honda", L"Blanka", L"Claw", L"Sagat", L"Dictator", L"Sakura",
    L"Cammy", L"Akuma", L"Morrigan", L"Evil Ryu", L"Kyo", L"Iori",
    L"Terry", L"Ryo", L"Mai", L"Kim", L"Geese", L"Yamazaki",
    L"Raiden", L"Rugal", L"Vice", L"Benimaru", L"Yuri", L"King",
    L"Nakoruru", L"Orochi Iori", L"Boxer", L"Dan", L"Joe", L"Eagle",
    L"Maki", L"Kyosuke", L"Athena", L"Chang", L"Todoh", L"Rock",
    L"Hibiki", L"Haohmaru", L"Yun", L"Shin Akuma", L"Ultimate Rugal",
    L"Rolento", L"C Groove", L"A Groove", L"P Groove", L"S Groove",
    L"N Groove", L"K Groove"
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

void cvs2_selection_copy(const BYTE *s, SIZE_T size,
                         game_selection_choice out[GAME_SELECTION_OWNERS])
{
    int ids[2], p;
    if (!out) return;
    memset(out, 0, sizeof(*out) * GAME_SELECTION_OWNERS);
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i)
        out[i].status = GAME_SELECTION_INACTIVE;
    for (p=0; p<2; p++) {
        int groove;
        ids[p] = cvs2_select_decode(s,size,(unsigned)p);
        if (ids[p] == -1) {
            groove = cvs2_groove_decode(s,size,(unsigned)p);
            ids[p] = groove >= 0 ? 48+groove : groove;
            if (ids[p] == -1) {
                int ratio = cvs2_ratio_decode(s,size,(unsigned)p);
                ids[p] = ratio >= 6 ? 72+ratio-6 : ratio >= 0 ? 54+ratio : ratio;
            }
        }
    }
    {
        int stage=cvs2_stage_decode(s,size);
        /* Stage is shared, so announce it once even with two active cursors. */
        if (stage >= 0) { ids[0]=60+stage; ids[1]=-1; }
    }
    for (p=0; p<2; p++) {
        int id = ids[p];
        if (id < 0) {
            out[p].status = id == -1 ? GAME_SELECTION_INACTIVE : GAME_SELECTION_UNAVAILABLE;
            continue;
        }
        /* A new team slot is a new selection even if the intervening
         * confirmation animation was missed or its highlight is unchanged. */
        out[p].status = GAME_SELECTION_VALID;
        out[p].value = (unsigned)id;
        out[p].kind = id < 48 ? 0 : id < 54 ? 1 : id < 60 ? 2 : id < 72 ? 3 : 4;
        out[p].visit=s[0x1ffd4+p];
        if (s[0x1a0] && id < 48)
            out[p].visit += 256*s[0x1eba8+p*0x678+0x5d1];
        wchar_t *part = out[p].label;
        if (id >= 72) {
            /* The user identifies this as handicap. The native storage is
             * shared with ratios, but the visible heading is not verified. */
            swprintf(part,256,L"Level %d",id-72+1);
        } else if (id >= 60) {
            swprintf(part,256,L"Stage: %ls",stage_names[id-60]);
        } else if (id >= 54) {
            unsigned b=0x1eba8+p*0x678, i, n=s[b+0x5d1];
            swprintf(part,256,L"Ratios: ");
            for (i=0; i<n; i++) {
                wchar_t entry[80];
                swprintf(entry,80,L"%ls%ls %u",i ? L", " : L"",
                         names[s[b+0x5c0+i]],ratios[id-54][i]);
                wcscat(part,entry);
            }
        } else swprintf(part,256,L"%ls",names[id]);
    }
}

void cvs2_select_observe(const BYTE *s, SIZE_T size, ULONGLONG root, unsigned rom)
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
    cvs2_selection_copy(s, size, choices);
    ULONGLONG now = GetTickCount64();
    selection_channel_offer(&channel, choices, true, now, now);
    unsigned accepted = selection_channel_drain(&channel, now);
    for (unsigned p = 0; p < 2; ++p) if (accepted & (1u << p)) {
        unsigned id = choices[p].value;
        log_line("cvs2-select: accepted player=%u kind=%s id=%u", p+1,
                 id < 48 ? "character" : id < 54 ? "groove" :
                 id < 60 ? "ratio" : id < 72 ? "stage" : "individual-ratio",
                 id < 48 ? id : id < 54 ? id-48 : id < 60 ? id-54 :
                 id < 72 ? id-60 : id-72);
    }
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
 *    ⚠ And health does NOT come through here -- cvs2_frame.c feeds it on the
 *    game's own frame hook precisely so this poll's rate cannot make it
 *    stale. The super meter is the only gauge on this path, and a meter moves
 *    over seconds.
 *
 * ⛔ What was NOT done: dropping mem_readable() before the memcpy. That was
 * tried in this project and the test written to prove it safe CRASHED -- a
 * guarded memcpy across a page boundary is NOT caught (case C in
 * run_span_test.ps1). The fault guard is a net for the race between the check
 * and the copy, never a replacement for the check. Every bulk read here still
 * calls mem_copy(). */
#define SELECT_POLL_MS 33u

void cvs2_select_tick(void)
{
    static BYTE state[CVS2_STATE_SIZE];
    static int guard_ready;
    /* 0 = not yet checked, 1 = this is the audited build, -1 = it is not. */
    static int sig_ok;
    static DWORD last_poll;
    BYTE header[0x60];
    ULONGLONG session, arena, context, pointers[4], root, phase, again;
    unsigned game, rom;
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
    if (!read_at(&session,0x141c14f98ULL,8) || !session)
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
        !read_at(&root,0x1422debd0ULL,8) || !read_at(&phase,0x1422dec30ULL,8))
        goto inactive;
    if (arena > 0x00007fff00000000ULL || context != arena+0x8000000 ||
        pointers[0] != arena || pointers[1] != arena+0x8400000 ||
        pointers[2] != root || phase != root+0x78 ||
        root < arena+0xc000000 || root > arena+0x10000000-CVS2_STATE_SIZE)
        goto inactive;

    if (!read_at(state,root,sizeof state) ||
        !read_at(&again,0x1422debd0ULL,8) || again != root ||
        !read_at(&again,0x141c14f98ULL,8) || again != session)
        goto inactive;
    cvs2_select_observe(state,sizeof state,root,rom);
    cvs2_gauges_observe(state,sizeof state,root);
    cvs2_announce_observe(state,sizeof state,root);
    cvs2_quote_observe(state,sizeof state,root);
    cvs2_story_observe(state,sizeof state,root);
    cvs2_text_observe(root);
    return;
inactive:
    last_poll = 0;
    cvs2_select_observe(NULL,0,0,0);
    cvs2_gauges_observe(NULL,0,0);
    cvs2_announce_observe(NULL,0,0);
    cvs2_quote_observe(NULL,0,0);
    cvs2_story_observe(NULL,0,0);
    cvs2_text_observe(0);
}

/* ---- In-match gauges ---------------------------------------------------- */

#include "audio.h"

/* ⛔⛔⛔ B+0x16C IS HEALTH, NOT THE SUPER METER. I shipped it as the meter and
 * the user disproved it in training mode, where the variables are controlled:
 *
 *   - spending a super  -> NOTHING moved
 *   - the meter refilling -> NOTHING moved
 *   - hitting the opponent -> THEIR value dropped
 *
 * The third observation alone settles it. Nothing about a super meter makes it
 * fall when the other player is hit.
 *
 * ⭐⭐ WHY MY "CONFIRMATION" WAS WORTHLESS. I claimed the P1-moves-while-
 * P2-holds pattern proved it across 7 captures. That pattern fits health just
 * as well -- one player taking damage while the other is untouched. The test
 * discriminated "real per-player field" from "shared timer", NOT "meter" from
 * "health", and I read it as proving far more than it did.
 *
 * The instruction evidence was equally ambiguous and I treated it as decisive:
 * `imul edx,eax,0x64` (0x1406C26A0) adds in units of 100 and 0x1406C4241
 * subtracts -- I read gain/spend, but damage/recovery has exactly that shape.
 * And B+0x170 being a steady PER-CHARACTER ceiling (11520 vs 11200) is more
 * consistent with max health than a meter cap, since CVS2 vitality differs per
 * fighter. I noted that it looked like vitality early and reasoned past it.
 *
 * ⚠ A behavioural test that CANNOT tell two hypotheses apart is not evidence
 * for either. Ask what the observation would look like if the OTHER answer
 * were true -- here, identical.
 *
 * The super meter is therefore NOT located. Do not guess at it; find the
 * field that moves when a super is spent and refills over time. */
#define GA_B(p)      (0x1eba8u + (p) * 0x678u)
#define GA_HEALTH    0x16cu
/* MAX health. B+0x170, not B+0x5BC: I took 0x5BC straight from the clamp at
 * 0x1406C26E9 without checking it against the captures already in hand -- it
 * reads 0 in ALL SIX, so the "value > cap" guard rejected everything and the
 * mod was silent in-game with no log line to say why.
 *
 * Reading the branch properly: 0x1406C26BC tests a mode byte, and only when it
 * is 2 or 7 does the code reach the 0x5BC path. The NORMAL path is
 * 0x1406C26CA, `movzx eax, word ptr [rbx+0x170]`. Live: 0x170 holds steady at
 * 11520 (P1) / 11200 (P2) across every capture while health varies beneath it,
 * and equals it exactly at full -- per-character vitality, as CVS2 has.
 *
 * ⭐ An offset read off a single instruction is a HYPOTHESIS. Check it against
 * a capture before shipping -- the data was in build/cvs2-state the whole
 * time, and the same shortcut produced BOTH bugs in this file. */
#define GA_MAXHP     0x170u

/* ---- Super meter -------------------------------------------------------
 * B+0x226 (u16), found by a FOUR-WAY capture constraint: identical across
 * three full-meter captures and different in a genuinely empty one. In the
 * whole 0x678 player struct exactly ONE byte survived that filter, with P2
 * holding at 168 while P1 collapsed to 4.
 *
 * Code confirms it independently: 0x1406C40EA `add word [rbx+0x226],di` with
 * a jns and a store of 0 (drain, floors at zero), and 0x1406C40B0
 * add/cmp/clamp (gain, ceilings). Accumulate-to-cap and drain-to-zero.
 *
 * ⭐⭐ THE CAP IS PER-GROOVE, and this is why a single constant was wrong:
 * 0x1406C3E53 does `movsx rax,byte [rbx+0x22D]` / `cmp eax,5` / jump table at
 * 0x1406C4138, six cases, each loading its own ceiling:
 *
 *   0 C=0xA8(168)  1 A=0x90(144)  2 P=0xC0(192)
 *   3 S=0x64(100)  4 N=0xD8(216)  5 K=0x48(72)
 *
 * ⚠ 0x48 appears as an immediate in several places and is K-Groove's cap
 * ONLY. Hardcoding it -- or the observed 168 -- would misreport every other
 * groove. The table is indexed by B+0x22D, which agrees with B+0x5D0 (the
 * groove field the selection reader already validates) in every capture.
 *
 * ⭐ The check that makes this shippable: the table predicts 100% for all
 * three full captures and 2% for the empty one, matching the user's own
 * description. A cap that could not reproduce the measured 168 would have
 * been rejected. */
#define GA_METER     0x226u
#define GA_GROOVE    0x22du
static const unsigned meter_cap[6] = { 168, 144, 192, 100, 216, 72 };

static ULONGLONG g_ga_root;
/* ⭐ Liveness: without these the log cannot tell "never ran" from "ran and
 * everything was rejected", which is exactly the ambiguity that hid the bug
 * above. Reported by cvs2_gauges_log_stats(). */
static unsigned g_ga_fed, g_ga_rejected;

/* ⛔⛔ THIS WROTE A LOG LINE EVERY TICK -- 2741 of them in one 5-minute
 * session, one every ~25 ms, a THIRD of the whole log. Each is a file write
 * from the worker thread, and the user heard it as speech lagging keypresses.
 *
 * ⭐ THE BUG IS THE GUARD ITSELF, and it is the SAME one already fixed in
 * cvs2_frame_log_stats(): `fed` and `rejected` are incremented twice per tick
 * while a match is running, so "have the counters changed?" is TRUE on every
 * single call. A counter that changes continuously cannot also be the thing
 * that gates "has anything changed?".
 *
 * ⚠ Knowing a trap is not the same as encoding it -- that fix was written,
 * commented at length, and the identical guard was left standing here.
 *
 * Rate-limit by TIME, like the frame version, and report the RATE rather than
 * a running total: "fed 120/s" is a number a human can act on, where an
 * ever-climbing total is not. */
void cvs2_gauges_log_stats(void)
{
    static ULONGLONG last_tick;
    static unsigned last_fed, last_rej;
    ULONGLONG now = GetTickCount64();
    unsigned fed = g_ga_fed, rej = g_ga_rejected;

    if (last_tick == 0) { last_tick = now; last_fed = fed; last_rej = rej; return; }
    if (now - last_tick < 10000) return;          /* at most once per 10 s */

    /* Say nothing at all while nothing is happening: outside CVS2 both
     * counters are frozen, and a heartbeat every 10 s saying "0 and 0" is the
     * log noise this function is being fixed for. */
    if (fed != last_fed || rej != last_rej)
        log_line("gauge: fed=%u (%.0f/s) rejected=%u (%.0f/s)",
                 fed, (double)(fed - last_fed) * 1000.0 / (double)(now - last_tick),
                 rej, (double)(rej - last_rej) * 1000.0 / (double)(now - last_tick));

    last_tick = now; last_fed = fed; last_rej = rej;
}

void cvs2_gauges_observe(const BYTE *state, SIZE_T size, ULONGLONG root)
{
    unsigned p;

    /* Not in CVS2, or the block moved: forget history. A relocation must not
     * read as every gauge changing at once, and a stale value must never be
     * announced as current. */
    if (!state || size != CVS2_STATE_SIZE || root != g_ga_root) {
        g_ga_root = state ? root : 0;
        audio_gauge_reset_all();
        if (!state) return;
    }

    for (p = 0; p < 2; p++) {
        unsigned b = GA_B(p);
        unsigned hp, maxhp;
        memcpy(&hp,    state + b + GA_HEALTH, 2); hp    &= 0xffff;
        memcpy(&maxhp, state + b + GA_MAXHP,  2); maxhp &= 0xffff;

        /* ⚠ 0xFFFF is the KO/round-end sentinel, not a value -- the field
         * reads -1 in substate 5. Announcing it would sweep the tone to full
         * every time a round ends. A zero max would also divide by zero. */
        if (hp == 0xffffu || maxhp == 0 || maxhp == 0xffffu || hp > maxhp) {
            g_ga_rejected++;
            audio_gauge_reset(AUDIO_GAUGE_HEALTH, p);
            continue;
        }
        g_ga_fed++;
        /* ⛔ HEALTH IS NOT FED HERE. The frame hook (cvs2_frame.c) feeds it on
         * the frame it changes; this poll runs asynchronously and would
         * overwrite that with a value up to a frame stale, re-introducing the
         * exact lag the hook exists to remove. The read above still runs so
         * the fed/rejected counters and the KO-sentinel guard stay honest. */

        /* Super meter, scaled by this player's groove cap. */
        {
            unsigned meter, groove;
            memcpy(&meter, state + b + GA_METER, 2); meter &= 0xffff;
            groove = state[b + GA_GROOVE];
            /* An unknown groove has no known ceiling, so scaling would be a
             * guess. Stay silent rather than report a wrong fill. */
            if (groove < 6 && meter != 0xffffu && meter <= meter_cap[groove])
                audio_gauge_set(AUDIO_GAUGE_SUPER, p, (int)meter,
                                (int)meter_cap[groove]);
            else
                audio_gauge_reset(AUDIO_GAUGE_SUPER, p);
        }
    }
}

/* ---- In-match announcements --------------------------------------------
 * Speech OUTSIDE the fight only: team/groove when a match starts, and the
 * fighter + its ratio when a round starts. The audio design forbids narrating
 * DURING play (a screen reader is too slow and masks the game's own sound);
 * these fire at natural pauses, which is the case that rule was not about.
 * Gauges stay cues. */

static ULONGLONG g_an_root;
static int g_an_match_done;          /* team/groove announced this match  */
static int g_an_char[2] = {-1,-1};   /* last fighter announced, per player */
static unsigned g_an_team[2][3];     /* team that was announced           */
static int g_an_size[2] = {-1,-1};

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

void cvs2_announce_observe(const BYTE *s, SIZE_T size, ULONGLONG root)
{
    wchar_t line[600] = L"", part[200];
    unsigned p, i;
    int said = 0;

    if (!s || size != CVS2_STATE_SIZE) { g_an_root = 0; return; }

    /* A new state block is a new session: re-announce everything. */
    if (root != g_an_root) {
        g_an_root = root;
        g_an_match_done = 0;
        g_an_char[0] = g_an_char[1] = -1;
        g_an_size[0] = g_an_size[1] = -1;
    }

    /* Only in an actual match. Phase bytes 02 01 02 02 with byte 4 in the
     * in-round range; the selection screens use a different phase. */
    if (memcmp(s + 0x78, "\x02\x01\x02\x02", 4)) return;

    /* ---- match start: team order + groove, once per team composition ---- */
    if (!g_an_match_done) {
        int ok = 1;
        for (p = 0; p < 2 && ok; p++) {
            unsigned b = 0x1eba8 + p * 0x678;
            if (s[b + 0x5d1] < 1 || s[b + 0x5d1] > 3) ok = 0;
            if (s[b + 0x5d0] >= 6) ok = 0;
        }
        if (ok) {
            for (p = 0; p < 2; p++) {
                unsigned b = 0x1eba8 + p * 0x678, n = s[b + 0x5d1];
                swprintf(part, 200, L"%ls: %ls, ",
                         p == 0 ? L"You" : L"Opponent",
                         names[48 + s[b + 0x5d0]]);
                wcscat(line, part);
                for (i = 0; i < n; i++) {
                    unsigned id = s[b + 0x5c0 + i], ratio = an_ratio(s, b, i);
                    if (id >= 48) continue;
                    g_an_team[p][i] = id;
                    if (ratio)
                        swprintf(part, 200, L"%ls ratio %u%ls", names[id], ratio,
                                 i + 1 < n ? L", " : L"");
                    else
                        swprintf(part, 200, L"%ls%ls", names[id],
                                 i + 1 < n ? L", " : L"");
                    wcscat(line, part);
                }
                g_an_size[p] = (int)n;
                if (p == 0) wcscat(line, L". ");
            }
            if (tc_say(line, TRUE, "cvs2-announce")) {
                g_an_match_done = 1;
                /* The fighters now on screen are announced, so do not repeat
                 * them as a round change immediately afterwards. */
                for (p = 0; p < 2; p++)
                    g_an_char[p] = s[0x1eba8 + p * 0x678 + 1];
            }
            return;
        }
    }

    /* ---- round start: whichever fighter changed, with its ratio -------- */
    line[0] = 0;
    for (p = 0; p < 2; p++) {
        unsigned b = 0x1eba8 + p * 0x678;
        int id = s[b + 1];
        unsigned n = s[b + 0x5d1], slot;
        if (id < 0 || id >= 48) continue;
        if (g_an_char[p] == id) continue;
        /* Which team slot is this fighter, so the ratio matches. */
        for (slot = 0; slot < n && slot < 3; slot++)
            if ((int)s[b + 0x5c0 + slot] == id) break;
        {
            unsigned ratio = (slot < 3) ? an_ratio(s, b, slot) : 0;
            if (ratio)
                swprintf(part, 200, L"%ls%ls, ratio %u",
                         said ? L". " : L"", names[id], ratio);
            else
                swprintf(part, 200, L"%ls%ls", said ? L". " : L"", names[id]);
        }
        wcscat(line, part);
        g_an_char[p] = id;
        said = 1;
    }
    if (said) tc_say(line, TRUE, "cvs2-announce");
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
        for (i=0; i<n; i++)
            if (text[i] < 32 || text[i] > 126) { out[0]=0; return FALSE; }
        if (line && used && out[used-1] != L' ' && text[0] != ' ')
            out[used++]=L' ';
        for (i=0; i<n; i++) out[used++]=(wchar_t)text[i];
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
