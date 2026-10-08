#include "game_session.h"

static void invalidate(game_session *s)
{
    s->eligible = false;
    s->published = s->consumed = 0;
    if (s->issued.revision == UINT64_MAX) s->exhausted = true;
    else ++s->issued.revision;
}

bool game_session_begin(game_session *s, unsigned adapter, unsigned rom)
{
    if (!s) return false;
    invalidate(s);
    s->owned = false;
    if (s->exhausted || s->issued.generation == UINT64_MAX) {
        s->exhausted = true;
        return false;
    }
    ++s->issued.generation;
    s->issued.adapter = adapter;
    s->issued.rom = rom;
    s->owned = true;
    return true;
}

void game_session_suspend(game_session *s)
{
    if (s) invalidate(s);
}

bool game_session_begin_if_current(game_session *s, uint64_t generation,
                                   uint64_t revision, unsigned adapter, unsigned rom)
{
    if (!s || s->issued.generation != generation || s->issued.revision != revision)
        return false;
    return game_session_begin(s, adapter, rom);
}

void game_session_depart(game_session *s)
{
    if (!s) return;
    invalidate(s);
    s->owned = false;
}

bool game_session_resume(game_session *s, uint64_t generation, uint64_t revision)
{
    if (!s || !s->owned || s->exhausted ||
        generation != s->issued.generation || revision != s->issued.revision)
        return false;
    s->eligible = true;
    return true;
}

bool game_session_capture(game_session *s, game_session_token *token)
{
    if (!s || !token || !s->owned || !s->eligible || s->exhausted) return false;
    if (s->issued.sequence == UINT64_MAX) {
        s->exhausted = true;
        invalidate(s);
        return false;
    }
    ++s->issued.sequence;
    *token = s->issued;
    return true;
}

static bool current(const game_session *s, const game_session_token *token)
{
    return s && token && s->owned && s->eligible && !s->exhausted &&
        token->generation == s->issued.generation &&
        token->revision == s->issued.revision &&
        token->adapter == s->issued.adapter && token->rom == s->issued.rom &&
        token->sequence && token->sequence <= s->issued.sequence;
}

bool game_session_publish(game_session *s, const game_session_token *token)
{
    if (!current(s, token) || token->sequence <= s->published) return false;
    s->published = token->sequence;
    return true;
}

bool game_session_can_output(const game_session *s, const game_session_token *token)
{
    return current(s, token) && token->sequence == s->published &&
        token->sequence > s->consumed;
}

bool game_session_accept(game_session *s, const game_session_token *token,
                         bool backend_accepted)
{
    if (!backend_accepted || !game_session_can_output(s, token)) return false;
    s->consumed = token->sequence;
    return true;
}
