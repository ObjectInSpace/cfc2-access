#include "../src/game_session.h"
#include <stdio.h>

static unsigned checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; \
    printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static void ready(game_session *s, unsigned adapter, unsigned rom)
{
    CHECK(game_session_begin(s, adapter, rom));
    CHECK(game_session_resume(s, s->issued.generation, s->issued.revision));
}
int main(void)
{
    game_session s = {0};
    game_session_token old = {0}, a, b, altered;
    CHECK(!game_session_capture(&s, &old));
    CHECK(!game_session_resume(&s, 0, 0));
    CHECK(game_session_begin(&s, 1, 3));
    CHECK(!game_session_capture(&s, &old));
    CHECK(game_session_resume(&s, s.issued.generation, s.issued.revision));
    CHECK(game_session_capture(&s, &a));
    CHECK(!game_session_can_output(&s, &a));
    CHECK(game_session_publish(&s, &a));
    CHECK(!game_session_accept(&s, &a, false));
    CHECK(game_session_can_output(&s, &a));
    CHECK(game_session_accept(&s, &a, true));
    CHECK(!game_session_can_output(&s, &a));
    CHECK(!game_session_publish(&s, &a));
    CHECK(game_session_capture(&s, &a));
    CHECK(game_session_capture(&s, &b));
    CHECK(game_session_publish(&s, &b));
    CHECK(!game_session_publish(&s, &a));
    CHECK(!game_session_can_output(&s, &a));
    old = b;
    /* Restore at identical identity: generation stays; revision changes. */
    game_session_suspend(&s);
    CHECK(s.issued.generation == old.generation);
    CHECK(!game_session_resume(&s, old.generation, old.revision));
    CHECK(!game_session_publish(&s, &old));
    CHECK(!game_session_accept(&s, &old, true));
    CHECK(game_session_resume(&s, s.issued.generation, s.issued.revision));
    CHECK(!game_session_can_output(&s, &old));
    CHECK(game_session_capture(&s, &a));
    CHECK(a.sequence > old.sequence);
    CHECK(game_session_publish(&s, &a));
    /* Departure after backend call but before acceptance bookkeeping. */
    game_session_depart(&s);
    CHECK(!game_session_accept(&s, &a, true));
    CHECK(!game_session_resume(&s, s.issued.generation, s.issued.revision));
    ready(&s, 1, 3); /* Identical adapter/ROM is still a new owner. */
    CHECK(s.issued.generation > a.generation);
    CHECK(!game_session_publish(&s, &a));
    ready(&s, 0, 1); /* CVS1 replaces CVS2. */
    CHECK(!game_session_publish(&s, &a));
    CHECK(game_session_capture(&s, &b));
    altered = b; altered.adapter = 1;
    CHECK(!game_session_publish(&s, &altered));
    altered = b; altered.rom = 0;
    CHECK(!game_session_publish(&s, &altered));
    altered = b; ++altered.sequence;
    CHECK(!game_session_publish(&s, &altered));
    CHECK(game_session_publish(&s, &b));
    game_session_suspend(&s);
    old = s.issued;
    game_session_suspend(&s); /* Nested/later mutation defeats old completion. */
    CHECK(!game_session_resume(&s, old.generation, old.revision));
    CHECK(!game_session_capture(&s, &a));
    /* Exhaustion never wraps a counter into a previously valid token. */
    ready(&s, 0, 1);
    s.issued.sequence = UINT64_MAX;
    CHECK(!game_session_capture(&s, &a));
    CHECK(s.exhausted && !s.eligible);
    CHECK(!game_session_begin(&s, 0, 1));
    game_session revision_limit = {0};
    ready(&revision_limit, 0, 1);
    revision_limit.issued.revision = UINT64_MAX;
    game_session_suspend(&revision_limit);
    CHECK(revision_limit.exhausted && !revision_limit.eligible);
    game_session owner_limit = {0};
    owner_limit.issued.generation = UINT64_MAX;
    CHECK(!game_session_begin(&owner_limit, 0, 1));
    CHECK(owner_limit.exhausted && !owner_limit.owned);
    CHECK(!game_session_publish(NULL, &b));
    CHECK(!game_session_can_output(&s, NULL));
    printf("game_session: %u checks, %u failures (serialized synthetic state only)\n", checks, failures);
    return failures ? 1 : 0;
}
