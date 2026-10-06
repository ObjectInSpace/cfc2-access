#include "game_selection.h"
#include <string.h>

static bool valid(const game_selection_choice *c)
{
    return c->status == GAME_SELECTION_VALID && c->label[0] &&
        wmemchr(c->label, L'\0', GAME_SELECTION_LABEL);
}

static bool same(const game_selection_choice *a, const game_selection_choice *b)
{
    return valid(a) && valid(b) && a->visit == b->visit &&
        a->kind == b->kind && a->value == b->value && !wcscmp(a->label, b->label);
}

static bool fresh(uint64_t captured, uint64_t now)
{
    return captured <= now && now - captured <= GAME_SELECTION_MAX_AGE_MS;
}

bool game_selection_offer(game_selection *p, const game_session *session,
                          const game_selection_observation *o, uint64_t now)
{
    if (!p || !o || p->in_output || !game_session_can_output(session, &o->token))
        return false;
    if (p->generation != o->token.generation) {
        memset(p, 0, sizeof *p);
        p->generation = o->token.generation;
    }
    if (p->revision != o->token.revision) {
        memset(p->pending, 0, sizeof p->pending);
        p->revision = o->token.revision;
        p->ready = 0;
    }
    if (o->token.sequence <= p->seen_sequence) return false;
    p->seen_sequence = o->token.sequence;
    p->latest = *o;
    p->ready = 0;
    if (!o->complete || !fresh(o->captured_ms, now)) {
        memset(p->pending, 0, sizeof p->pending);
        return false;
    }
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i) {
        const game_selection_choice *c = &o->owner[i];
        if (!valid(c)) {
            memset(&p->pending[i], 0, sizeof p->pending[i]);
            if (c->status == GAME_SELECTION_INACTIVE)
                memset(&p->accepted[i], 0, sizeof p->accepted[i]);
            continue;
        }
        if (same(c, &p->pending[i]) && !same(c, &p->accepted[i]))
            p->ready |= 1u << i;
        p->pending[i] = *c;
    }
    return true;
}

unsigned game_selection_drain(game_selection *p, game_session *session,
                              uint64_t now, game_selection_emit emit, void *context)
{
    wchar_t text[GAME_SELECTION_TEXT];
    size_t used = 0;
    if (!p || p->in_output || !emit || !p->ready ||
        !game_session_can_output(session, &p->latest.token)) return 0;
    if (!fresh(p->latest.captured_ms, now)) {
        p->ready = 0;
        memset(p->pending, 0, sizeof p->pending);
        return 0;
    }
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i) {
        if (!(p->ready & (1u << i))) continue;
        const wchar_t *label = p->latest.owner[i].label;
        size_t length = wcslen(label); /* Validated at offer; private copy. */
        if (used) { text[used++] = L'.'; text[used++] = L' '; }
        wmemcpy(text + used, label, length);
        used += length;
    }
    text[used] = 0;
    p->in_output = true;
    bool accepted = emit(context, text);
    p->in_output = false;
    if (!game_session_accept(session, &p->latest.token, accepted)) return 0;
    unsigned mask = p->ready;
    for (unsigned i = 0; i < GAME_SELECTION_OWNERS; ++i)
        if (mask & (1u << i)) p->accepted[i] = p->latest.owner[i];
    p->ready = 0;
    return mask;
}
