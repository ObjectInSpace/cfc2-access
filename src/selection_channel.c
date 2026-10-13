#include "selection_channel.h"
#include "text_channel.h"
#include <string.h>

bool selection_channel_begin(selection_channel *c, unsigned adapter,
                             unsigned rom, const char *source)
{
    if (!c || c->presenter.in_output) return false;
    size_t length = 0;
    if (source) while (length < sizeof c->source && source[length]) ++length;
    if (!length || length == sizeof c->source) {
        selection_channel_depart(c);
        return false;
    }
    if (!game_session_begin(&c->session, adapter, rom)) return false;
    memcpy(c->source, source, length + 1);
    /* Eligibility here is for owned copied labels on this worker. Native
     * acquisition remains the adapter's separate responsibility. */
    return game_session_resume(&c->session, c->session.issued.generation,
                               c->session.issued.revision);
}

void selection_channel_depart(selection_channel *c)
{
    if (c && c->session.owned) game_session_depart(&c->session);
}

bool selection_channel_offer(selection_channel *c,
    const game_selection_choice choices[GAME_SELECTION_OWNERS],
    bool complete, uint64_t captured_ms, uint64_t now_ms)
{
    game_selection_observation o = {0};
    if (!c || c->presenter.in_output ||
        !game_session_capture(&c->session, &o.token)) return false;
    o.captured_ms = captured_ms;
    o.complete = complete && choices;
    if (choices) memcpy(o.owner, choices, sizeof o.owner);
    return game_session_publish(&c->session, &o.token) &&
           game_selection_offer(&c->presenter, &c->session, &o, now_ms);
}

static bool emit(void *context, const wchar_t *text)
{
    selection_channel *c = context;
    return tc_say(text, TRUE, c->source);
}

unsigned selection_channel_drain(selection_channel *c, uint64_t now_ms)
{
    return c ? game_selection_drain(&c->presenter, &c->session, now_ms, emit, c) : 0;
}
