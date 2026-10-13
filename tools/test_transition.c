/*
 * Offline test for the shared transition announcer (src/transition_channel.c)
 * through the real text_channel/tc_say. Speech is mocked so acceptance and
 * refusal are controlled. Synthetic labels: these certify the policy, not any
 * game's name map.
 *
 * Sabotages (python tools\sabotage_transition.py), each seen to fail:
 *  - fighter latch moved before tc_say       -> 3 (refusal retried)
 *  - intro keyed on "introduced" only        -> 8 (next opponent's team)
 *  - ownership label always "You"            -> 6 (unknown speaks the number)
 *  - depart leaves the channel begun         -> 4 (departure cancels)
 */
#include "../src/transition_channel.h"
#include "../src/text_channel.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", (m)); fails++; } \
                         else printf("ok: %s\n", (m)); } while (0)

static int calls, accepts = 1;
static wchar_t last[TRANSITION_TEXT];
BOOL speech_say(const wchar_t *s, BOOL interrupt)
{ (void)interrupt; calls++; wcsncpy(last, s, TRANSITION_TEXT - 1); return accepts; }
void log_line(const char *fmt, ...) { (void)fmt; }

static void member(transition_member *m, unsigned id, const wchar_t *name, unsigned ratio)
{ m->valid = true; m->id = id; m->ratio = ratio; wcsncpy(m->name, name, TRANSITION_LABEL - 1); }

/* Two-side match: P1 local (Ryu, Ken), P2 opponent (Maki), each with a style. */
static transition_observation base(void)
{
    transition_observation o;
    memset(&o, 0, sizeof o);
    o.team_ready = true;
    o.side[0].present = true; o.side[0].player = 1;
    o.side[0].ownership = TRANSITION_OWNER_LOCAL;
    wcscpy(o.side[0].style, L"C Groove");
    o.side[0].count = 2;
    member(&o.side[0].member[0], 0, L"Ryu", 2);
    member(&o.side[0].member[1], 1, L"Ken", 2);
    member(&o.side[0].current, 0, L"Ryu", 2);
    o.side[1].present = true; o.side[1].player = 2;
    o.side[1].ownership = TRANSITION_OWNER_OPPONENT;
    wcscpy(o.side[1].style, L"K Groove");
    o.side[1].count = 1;
    member(&o.side[1].member[0], 36, L"Maki", 0);
    member(&o.side[1].current, 36, L"Maki", 0);
    return o;
}

int main(void)
{
    transition_channel c;
    transition_observation o;
    int before;
    memset(&c, 0, sizeof c);

    /* 0. Not begun: nothing can speak. */
    o = base();
    CHECK(!transition_channel_offer(&c, &o) && calls == 0, "0: an un-begun channel is silent");

    /* 1. Introduction once. */
    transition_channel_begin(&c, "test");
    CHECK(transition_channel_offer(&c, &o), "1: introduction accepted");
    CHECK(!wcscmp(last, L"You: C Groove, Ryu ratio 2, Ken ratio 2. Opponent: K Groove, Maki"),
          "1: grouped introduction in team order, ratios where they exist");
    before = calls;
    transition_channel_offer(&c, &o);
    CHECK(calls == before, "1: the same teams are not re-introduced");

    /* 2. A fighter change speaks just that fighter. */
    member(&o.side[0].current, 1, L"Ken", 2);
    transition_channel_offer(&c, &o);
    CHECK(calls == before + 1 && !wcscmp(last, L"Ken, ratio 2"), "2: changed fighter with its ratio");
    transition_channel_offer(&c, &o);
    CHECK(calls == before + 1, "2: and only once");

    /* 3. A refusal stays owed; a newer change supersedes the obsolete one. */
    member(&o.side[1].current, 37, L"Kyosuke", 0);
    accepts = 0; before = calls;
    CHECK(!transition_channel_offer(&c, &o) && calls == before + 1, "3: refusal reported");
    accepts = 1;
    CHECK(transition_channel_offer(&c, &o) && !wcscmp(last, L"Kyosuke"),
          "3: a refused change is retried on the next offer");
    member(&o.side[0].current, 0, L"Ryu", 2);
    accepts = 0; transition_channel_offer(&c, &o);           /* Ryu refused */
    member(&o.side[0].current, 1, L"Ken", 2);                /* back to Ken */
    accepts = 1; before = calls;
    transition_channel_offer(&c, &o);
    CHECK(calls == before, "3: an obsolete refused change is never spoken late");

    /* 4. Departure cancels anything pending; begin re-announces. */
    o = base();
    wcscpy(o.side[1].style, L"A Groove");                     /* new intro owed */
    accepts = 0; transition_channel_offer(&c, &o); accepts = 1;
    transition_channel_depart(&c);
    before = calls;
    CHECK(!transition_channel_offer(&c, &o) && calls == before, "4: departure cancels the pending intro");
    transition_channel_begin(&c, "test");
    transition_channel_offer(&c, &o);
    CHECK(calls == before + 1 && wcsstr(last, L"A Groove") != NULL, "4: a new owner introduces again");

    /* 5. Unavailable data. */
    {
        transition_observation u = base();
        u.side[0].present = false; u.side[1].present = false;
        transition_channel_begin(&c, "test");
        before = calls;
        CHECK(!transition_channel_offer(&c, &u) && calls == before, "5: absent sides say nothing");
        u = base();
        u.team_ready = false;
        transition_channel_offer(&c, &u);
        CHECK(calls == before + 1 && !wcscmp(last, L"Ryu, ratio 2. Maki"),
              "5: without a ready team, fighter changes still speak");
        u.side[0].current.valid = false;
        member(&u.side[1].current, 40, L"Todoh", 0);
        before = calls;
        transition_channel_offer(&c, &u);
        CHECK(calls == before + 1 && !wcscmp(last, L"Todoh"), "5: an unknown fighter is skipped, not guessed");
    }

    /* 6. Ownership unknown: player numbers, never "You". */
    o = base();
    o.side[0].ownership = o.side[1].ownership = TRANSITION_OWNER_UNKNOWN;
    transition_channel_begin(&c, "test");
    transition_channel_offer(&c, &o);
    CHECK(wcsstr(last, L"You") == NULL && wcsstr(last, L"Opponent") == NULL &&
          !wcsncmp(last, L"Player 1: ", 10) && wcsstr(last, L". Player 2: ") != NULL,
          "6: unknown ownership speaks Player 1 / Player 2");
    o.side[1].ownership = TRANSITION_OWNER_LOCAL;            /* P2 is the player */
    o.side[0].ownership = TRANSITION_OWNER_OPPONENT;
    transition_channel_begin(&c, "test");
    transition_channel_offer(&c, &o);
    CHECK(!wcsncmp(last, L"Opponent: ", 10) && wcsstr(last, L". You: K Groove") != NULL,
          "6: a local P2 is You, on its own side");

    /* 7. Four-member team, four owners, no style. */
    {
        transition_observation f;
        memset(&f, 0, sizeof f);
        f.team_ready = true;
        for (unsigned p = 0; p < TRANSITION_OWNERS; p++) {
            f.side[p].present = true; f.side[p].player = p + 1;
            f.side[p].count = p == 0 ? 4 : 1;
            member(&f.side[p].member[0], 100 + p, L"A", 0);
        }
        member(&f.side[0].member[1], 2, L"B", 0);
        member(&f.side[0].member[2], 3, L"C", 0);
        member(&f.side[0].member[3], 4, L"D", 0);
        transition_channel_begin(&c, "test");
        transition_channel_offer(&c, &f);
        CHECK(!wcscmp(last, L"Player 1: A, B, C, D. Player 2: A. Player 3: A. Player 4: A"),
              "7: four members and four owners, no style label invented");
        /* Bounded: maximal labels everywhere cannot overflow. */
        for (unsigned p = 0; p < TRANSITION_OWNERS; p++) {
            f.side[p].count = TRANSITION_TEAM_MAX;
            for (unsigned i = 0; i < TRANSITION_TEAM_MAX; i++) {
                wchar_t big[TRANSITION_LABEL];
                wmemset(big, L'x', TRANSITION_LABEL - 1); big[TRANSITION_LABEL - 1] = 0;
                member(&f.side[p].member[i], i, big, 3);
            }
            wmemset(f.side[p].style, L'y', TRANSITION_LABEL - 1);
            f.side[p].style[TRANSITION_LABEL - 1] = 0;
        }
        transition_channel_offer(&c, &f);
        CHECK(wcslen(last) < TRANSITION_TEXT, "7: maximal labels stay bounded");
    }

    /* 8. The next opponent's team is a new composition: introduced. */
    o = base();
    transition_channel_begin(&c, "test");
    transition_channel_offer(&c, &o);
    member(&o.side[1].member[0], 40, L"Todoh", 0);
    member(&o.side[1].current, 40, L"Todoh", 0);
    before = calls;
    transition_channel_offer(&c, &o);
    CHECK(calls == before + 1 && wcsstr(last, L"Opponent: K Groove, Todoh") != NULL,
          "8: a new opponent team is introduced, not just named");
    transition_channel_offer(&c, &o);
    CHECK(calls == before + 1, "8: and the on-screen fighters are not repeated");

    if (fails) { printf("%d FAILED\n", fails); return 1; }
    puts("PASS: transitions -- intro once per composition, fighter changes, refusal retry, "
         "obsolete cancel, departure, unavailable data, ownership labels, four members/owners");
    return 0;
}
