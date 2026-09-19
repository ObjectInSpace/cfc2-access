/* CVS2 native-state evidence: docs/cvs2-state-access-trace.md.
 * No keyboard history, guest-address assumption, or game-memory writes. */
#include "cvs2_select.h"
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

void cvs2_select_observe(const BYTE *s, SIZE_T size, ULONGLONG root)
{
    static ULONGLONG previous_root;
    static int pending[2] = {-1,-1}, spoken[2] = {-1,-1};
    static int previous_step[2] = {-1,-1};
    int ids[2], changed[2] = {0,0}, p;
    wchar_t message[600] = L"", part[256];
    if (root != previous_root) {
        pending[0] = pending[1] = spoken[0] = spoken[1] = -1;
        previous_step[0] = previous_step[1] = -1;
        previous_root = root;
    }
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
        int selection_step;
        if (id < 0) {
            pending[p] = -1;
            if (id == -1) spoken[p] = -1;
            continue;
        }
        /* A new team slot is a new selection even if the intervening
         * confirmation animation was missed or its highlight is unchanged. */
        selection_step=s[0x1ffd4+p];
        if (s[0x1a0] && id < 48)
            selection_step += 256*s[0x1eba8+p*0x678+0x5d1];
        if (previous_step[p] != selection_step) {
            pending[p] = spoken[p] = -1;
            previous_step[p] = selection_step;
        }
        if (pending[p] != id) { pending[p] = id; continue; }
        if (spoken[p] == id) continue;
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
        if (message[0]) wcscat(message,L". ");
        wcscat(message,part);
        changed[p] = 1;
    }
    /* One utterance for simultaneous changes; no queued stream of obsolete
     * P2 positions. Only record names actually accepted by the backend. */
    if (message[0] && speech_say(message,TRUE)) {
        for (p=0; p<2; p++) if (changed[p]) {
            spoken[p] = ids[p];
            log_line("cvs2-select: accepted player=%d kind=%s id=%d",p+1,
                     ids[p] < 48 ? "character" : ids[p] < 54 ? "groove" :
                     ids[p] < 60 ? "ratio" : ids[p] < 72 ? "stage" : "individual-ratio",
                     ids[p] < 48 ? ids[p] : ids[p] < 54 ? ids[p]-48 :
                     ids[p] < 60 ? ids[p]-54 : ids[p] < 72 ? ids[p]-60 : ids[p]-72);
        }
    }
}

static BOOL read_at(void *out, ULONGLONG address, SIZE_T size)
{ return mem_copy(out,(const void *)(ULONG_PTR)address,size); }

void cvs2_select_tick(void)
{
    static BYTE state[CVS2_STATE_SIZE];
    static int guard_ready;
    BYTE sig[11], header[0x60];
    const BYTE expected[11] = {0x48,0x8b,0x48,0x10,0x48,0x89,0x0d,0x90,0x48,0xc3,0x01};
    ULONGLONG session, arena, context, pointers[4], root, phase, again;
    unsigned game, rom;
    if (!guard_ready) {
        if (!mem_guard_install()) return;
        guard_ready = 1;
    }
    if (!read_at(sig,0x1406aa335ULL,sizeof sig) || memcmp(sig,expected,sizeof sig) ||
        !read_at(&session,0x141c14f98ULL,8) || !read_at(header,session,sizeof header))
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
        root < arena+0xc000000 || root > arena+0x10000000-CVS2_STATE_SIZE ||
        !read_at(state,root,sizeof state) ||
        !read_at(&again,0x1422debd0ULL,8) || again != root ||
        !read_at(&again,0x141c14f98ULL,8) || again != session)
        goto inactive;
    cvs2_select_observe(state,sizeof state,root);
    cvs2_gauges_observe(state,sizeof state,root);
    return;
inactive:
    cvs2_select_observe(NULL,0,0);
    cvs2_gauges_observe(NULL,0,0);
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

static ULONGLONG g_ga_root;
/* ⭐ Liveness: without these the log cannot tell "never ran" from "ran and
 * everything was rejected", which is exactly the ambiguity that hid the bug
 * above. Reported by cvs2_gauges_log_stats(). */
static unsigned g_ga_fed, g_ga_rejected;

void cvs2_gauges_log_stats(void)
{
    static unsigned last_fed = (unsigned)-1, last_rej = (unsigned)-1;
    if (g_ga_fed == last_fed && g_ga_rejected == last_rej) return;
    last_fed = g_ga_fed; last_rej = g_ga_rejected;
    log_line("gauge: fed=%u rejected=%u", g_ga_fed, g_ga_rejected);
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
        audio_gauge_set(AUDIO_GAUGE_HEALTH, p, (int)hp, (int)maxhp);
    }
}
