/*
 * Shared match-transition announcements: the team/style introduction when a
 * match starts, then the active fighter whenever it changes. Spoken at the
 * natural pauses between rounds; the in-fight design stays cues-only.
 *
 * Policy taken from CVS2's former private announcer, with two deliberate
 * corrections:
 *  - nothing is latched until tc_say ACCEPTS it, so a refused utterance is
 *    retried on the next offer instead of being lost (the old fighter-change
 *    path latched first);
 *  - the introduction is keyed on the team COMPOSITION it describes, so the
 *    next Arcade opponent's team is introduced too, not only the first match
 *    after the state block appeared.
 *
 * There is no queue. Each offer recomputes what is owed from the LATEST owned
 * observation, so a newer transition supersedes an obsolete pending one, and
 * depart/begin cancel everything pending.
 *
 * Worker only, like selection_channel. No native reads; every label is a
 * bounded copy supplied by the adapter, which keeps native IDs, name maps and
 * team rules (sizes, ratios, styles) local.
 */
#ifndef CFC2_TRANSITION_CHANNEL_H
#define CFC2_TRANSITION_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#define TRANSITION_OWNERS   4
#define TRANSITION_TEAM_MAX 4     /* CVS1 needs four; CVS2 uses up to three */
#define TRANSITION_LABEL    64
#define TRANSITION_TEXT     1024

/* Who the player is, as far as the ADAPTER has evidence. Unknown speaks the
 * player number: a guessed "You" is wrong for P2, versus and spectators. */
typedef enum {
    TRANSITION_OWNER_UNKNOWN = 0,
    TRANSITION_OWNER_LOCAL,
    TRANSITION_OWNER_OPPONENT,
} transition_ownership;

typedef struct {
    bool valid;
    unsigned id;                  /* adapter's identity, compared not spoken */
    unsigned ratio;               /* displayed ratio/level, 0 = none          */
    wchar_t name[TRANSITION_LABEL];
} transition_member;

typedef struct {
    bool present;
    unsigned player;              /* 1-based, as the game numbers players     */
    transition_ownership ownership;
    wchar_t style[TRANSITION_LABEL];   /* groove/ism/etc., empty = none        */
    unsigned count;               /* members[0..count) in team order          */
    transition_member member[TRANSITION_TEAM_MAX];
    transition_member current;    /* the fighter now active, if known         */
} transition_side;

typedef struct {
    /* Every present side's team and style are complete enough to introduce.
     * When false the introduction waits; fighter changes still speak. */
    bool team_ready;
    transition_side side[TRANSITION_OWNERS];
} transition_observation;

typedef struct {
    bool begun;
    char source[64];
    bool introduced;
    wchar_t intro[TRANSITION_TEXT];      /* last ACCEPTED introduction */
    bool announced[TRANSITION_OWNERS];   /* latch below is meaningful  */
    unsigned announced_id[TRANSITION_OWNERS];
    /* Liveness for diagnostics. */
    unsigned spoken, refused;
} transition_channel;

/* New presentation owner: forget every latch. Source is copied. */
void transition_channel_begin(transition_channel *c, const char *source);
/* Departure: nothing pending can be spoken until the next begin. */
void transition_channel_depart(transition_channel *c);
/* Offer the latest in-match observation and speak whatever is owed through
 * tc_say. Returns true if an utterance was accepted. Call again with a fresh
 * observation to retry a refusal. */
bool transition_channel_offer(transition_channel *c,
                              const transition_observation *o);

/* Exposed for tests: the side label the policy would speak. */
void transition_side_label(const transition_side *s, wchar_t *out, size_t cap);

#endif
