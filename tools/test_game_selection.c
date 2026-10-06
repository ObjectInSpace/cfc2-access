#include "../src/game_selection.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures, calls;
static bool accept_output = true, revoke_output, reenter_output;
static wchar_t said[GAME_SELECTION_TEXT];
static game_session session;
static game_selection presenter;
static game_selection_observation observation;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; \
    printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static bool emit(void *context, const wchar_t *text)
{
    CHECK(context == &session);
    ++calls;
    wcscpy(said, text);
    if (reenter_output) {
        CHECK(!game_selection_offer(&presenter, &session, &observation, 1000));
        CHECK(!game_selection_drain(&presenter, &session, 1000, emit, context));
    }
    if (revoke_output) game_session_suspend(&session);
    return accept_output;
}
static unsigned drain(uint64_t now)
{ return game_selection_drain(&presenter, &session, now, emit, &session); }
static void owner(unsigned adapter, unsigned rom)
{
    CHECK(game_session_begin(&session, adapter, rom));
    CHECK(game_session_resume(&session, session.issued.generation, session.issued.revision));
    memset(&observation, 0, sizeof observation);
    observation.complete = true;
    observation.captured_ms = 1000;
}
static void choice(unsigned slot, unsigned value, uint64_t visit, const wchar_t *label)
{
    game_selection_choice *c = &observation.owner[slot];
    memset(c, 0, sizeof *c);
    c->status = GAME_SELECTION_VALID;
    c->value = value;
    c->visit = visit;
    wcscpy(c->label, label);
}
static bool offer(uint64_t now)
{
    CHECK(game_session_capture(&session, &observation.token));
    CHECK(game_session_publish(&session, &observation.token));
    return game_selection_offer(&presenter, &session, &observation, now);
}
static void settle(void)
{
    CHECK(offer(1000));
    CHECK(!drain(1000));
    CHECK(offer(1000));
}
int main(void)
{
    /* Labels are synthetic. These do not certify a native ID/name mapping. */
    owner(1, 3);
    choice(0, 10, 1, L"First"); choice(1, 20, 1, L"Second");
    CHECK(offer(1000));
    CHECK(!game_selection_offer(&presenter, &session, &observation, 1000));
    CHECK(!drain(1000) && calls == 0); /* Same snapshot cannot settle itself. */
    CHECK(offer(1000));
    accept_output = false;
    CHECK(!drain(1000) && calls == 1);
    CHECK(!drain(1000) && calls == 2);
    accept_output = true;
    reenter_output = true;
    CHECK(drain(1000) == 3);
    reenter_output = false;
    CHECK(!wcscmp(said, L"First. Second"));
    CHECK(!drain(1000) && calls == 3);
    CHECK(offer(1000)); CHECK(!drain(1000));

    /* A newer choice replaces rejected output; it needs its own settling. */
    choice(0, 11, 1, L"Obsolete"); settle();
    accept_output = false; CHECK(!drain(1000));
    choice(0, 12, 1, L"Replacement"); settle();
    accept_output = true; CHECK(drain(1000) == 1);
    CHECK(!wcscmp(said, L"Replacement"));
    /* Caller changes cannot mutate the retained copied observation. */
    choice(0, 13, 1, L"Copied"); settle();
    wcscpy(observation.owner[0].label, L"Changed outside");
    CHECK(drain(1000) == 1 && !wcscmp(said, L"Copied"));
    choice(0, 13, 1, L"Copied");

    /* Restore retains accepted semantic history, not a pending half-sample. */
    game_session_suspend(&session);
    CHECK(game_session_resume(&session, session.issued.generation, session.issued.revision));
    settle(); CHECK(!drain(1000));
    choice(0, 13, 2, L"Copied"); settle(); CHECK(drain(1000) == 1);
    observation.owner[0].status = GAME_SELECTION_UNAVAILABLE;
    CHECK(offer(1000));
    choice(0, 13, 2, L"Copied"); settle(); CHECK(!drain(1000));
    observation.owner[0].status = GAME_SELECTION_INACTIVE;
    CHECK(offer(1000));
    choice(0, 13, 2, L"Copied"); settle(); CHECK(drain(1000) == 1);

    /* New revision between the first and second sample breaks settling. */
    choice(0, 14, 2, L"Restore pending"); CHECK(offer(1000));
    game_session_suspend(&session);
    CHECK(game_session_resume(&session, session.issued.generation, session.issued.revision));
    settle(); CHECK(drain(1000) == 1);
    /* Incomplete, old and future timestamps break settling, never output. */
    choice(0, 15, 2, L"Fresh"); CHECK(offer(1000));
    observation.complete = false; CHECK(!offer(1000)); CHECK(!drain(1000));
    observation.complete = true; settle(); CHECK(drain(1000) == 1);
    choice(0, 16, 2, L"Expiry"); settle();
    unsigned before = calls;
    CHECK(!drain(1501) && calls == before);
    CHECK(offer(1000)); CHECK(!drain(1000));
    CHECK(!offer(1501)); CHECK(!offer(999));
    settle(); CHECK(drain(1500) == 1); /* Exact age boundary. */

    /* Invalid labels do not silence an independent valid owner. */
    choice(0, 17, 2, L"Bad");
    wmemset(observation.owner[0].label, L'x', GAME_SELECTION_LABEL);
    choice(1, 21, 1, L"Independent"); settle();
    CHECK(drain(1000) == 2 && !wcscmp(said, L"Independent"));
    observation.owner[0].label[0] = 0;
    CHECK(offer(1000)); CHECK(!drain(1000));

    /* Lifecycle changes during backend execution cannot mark stale output. */
    choice(1, 22, 1, L"Revoked"); settle();
    revoke_output = true; CHECK(!drain(1000)); revoke_output = false;
    CHECK(presenter.accepted[1].value == 21);
    before = calls;
    CHECK(!drain(1000) && calls == before);
    CHECK(game_session_resume(&session, session.issued.generation, session.issued.revision));
    settle(); CHECK(drain(1000) == 2);
    game_selection_observation stale = observation;
    owner(0, 1);
    CHECK(!game_selection_offer(&presenter, &session, &stale, 1000));
    choice(1, 22, 1, L"Revoked"); settle(); CHECK(drain(1000) == 2);

    /* Four maximal labels stay bounded, with one grouped output. */
    owner(0, 1);
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i) {
        choice(i, i, 1, L"X");
        wmemset(observation.owner[i].label, L'A' + i, GAME_SELECTION_LABEL - 1);
        observation.owner[i].label[GAME_SELECTION_LABEL - 1] = 0;
    }
    settle(); CHECK(drain(1000) == 15);
    CHECK(wcslen(said) == 4 * 255 + 6);
    CHECK(!game_selection_offer(NULL, &session, &observation, 1000));
    CHECK(!game_selection_drain(&presenter, &session, 1000, NULL, NULL));
    printf("game_selection: %u checks, %u failures (synthetic copied observations)\n", checks, failures);
    return failures ? 1 : 0;
}
