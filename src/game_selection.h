#ifndef CFC2_GAME_SELECTION_H
#define CFC2_GAME_SELECTION_H
#include "game_session.h"
#include <wchar.h>

/* Copied selection observations only, externally serialized worker use.
 * Native labels/visits are supplied by adapters, never guessed here. This
 * component is shared by the DLL and replay tests, not a lifecycle authority. */
#define GAME_SELECTION_OWNERS 4
#define GAME_SELECTION_LABEL 256
#define GAME_SELECTION_TEXT (GAME_SELECTION_OWNERS * (GAME_SELECTION_LABEL + 2))
#define GAME_SELECTION_MAX_AGE_MS 500
enum game_selection_status { GAME_SELECTION_UNAVAILABLE, GAME_SELECTION_INACTIVE,
                             GAME_SELECTION_VALID };
typedef struct {
    enum game_selection_status status;
    uint64_t visit;
    unsigned kind, value;
    wchar_t label[GAME_SELECTION_LABEL];
} game_selection_choice;
typedef struct {
    game_session_token token;
    uint64_t captured_ms;
    bool complete;
    game_selection_choice owner[GAME_SELECTION_OWNERS];
} game_selection_observation;
typedef struct {
    uint64_t generation, revision, seen_sequence;
    game_selection_choice pending[GAME_SELECTION_OWNERS];
    game_selection_choice accepted[GAME_SELECTION_OWNERS];
    uint64_t visit[GAME_SELECTION_OWNERS];
    unsigned visited;
    game_selection_observation latest;
    unsigned ready;
    bool in_output;
} game_selection;

/* Zero-initialize once. Each distinct publication can contribute only one
 * settling sample. UNAVAILABLE breaks settling but retains accepted history;
 * INACTIVE also rearms that owner. Incomplete/expired samples break settling.
 * New revision retains accepted semantic history; new generation clears it.
 * Per-owner labels must be nonempty and terminated within the fixed array. */
bool game_selection_offer(game_selection *p, const game_session *session,
                          const game_selection_observation *observation,
                          uint64_t now_ms);
/* One grouped utterance, no FIFO of obsolete highlights. Production binding
 * must use tc_say(text, TRUE, source); this callback makes backend rejection
 * and reentrant lifecycle changes testable without a speech device. Adapter
 * code must not supply its own speech implementation. Returns accepted mask.
 * Caller must serialize output with revocation; post-call validation cannot
 * retract speech already accepted by a backend. */
typedef bool (*game_selection_emit)(void *context, const wchar_t *text);
unsigned game_selection_drain(game_selection *p, game_session *session,
                              uint64_t now_ms, game_selection_emit emit,
                              void *context);
#endif
