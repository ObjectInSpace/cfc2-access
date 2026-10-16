/* See transition_channel.h. */

#include "transition_channel.h"
#include "text_channel.h"
#include "mod_text.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Bounded append. A truncated utterance is still bounded and terminated. */
static void add(wchar_t *buf, size_t cap, const wchar_t *fmt, ...)
{
    size_t n = wcslen(buf);
    va_list ap;
    if (n + 1 >= cap) return;
    va_start(ap, fmt);
    vswprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    buf[cap - 1] = 0;
}

void transition_side_label(const transition_side *s, wchar_t *out, size_t cap)
{
    if (!out || !cap) return;
    switch (s->ownership) {
    case TRANSITION_OWNER_LOCAL:    swprintf(out, cap, L"%ls", mt(MT_YOU)); break;
    case TRANSITION_OWNER_OPPONENT: swprintf(out, cap, L"%ls", mt(MT_OPPONENT)); break;
    default: {
        wchar_t n[12];
        swprintf(n, 12, L"%u", s->player);
        mt_fmt(out, cap, MT_PLAYER_N, n, NULL, NULL);
        break;
    }
    }
}

void transition_channel_begin(transition_channel *c, const char *source)
{
    if (!c) return;
    unsigned spoken = c->spoken, refused = c->refused;
    memset(c, 0, sizeof *c);
    c->spoken = spoken; c->refused = refused;
    c->begun = true;
    snprintf(c->source, sizeof c->source, "%s", source ? source : "transition");
}

void transition_channel_depart(transition_channel *c)
{
    if (!c) return;
    unsigned spoken = c->spoken, refused = c->refused;
    memset(c, 0, sizeof *c);
    c->spoken = spoken; c->refused = refused;
}

static void member_text(wchar_t *buf, size_t cap, const transition_member *m,
                        const wchar_t *ratio_sep)
{
    if (m->ratio) {
        wchar_t r[12], part[TRANSITION_TEXT];
        swprintf(r, 12, L"%u", m->ratio);
        /* ", " is the current fighter's form, " " the team list's. */
        mt_fmt(part, TRANSITION_TEXT, ratio_sep[0] == L',' ? MT_MEMBER_RATIO : MT_MEMBER_RATIO_LIST,
               m->name, r, NULL);
        add(buf, cap, L"%ls", part);
    } else add(buf, cap, L"%ls", m->name);
}

/* The grouped introduction, e.g. "You: C Groove, Ryu ratio 1, Ken ratio 1.
 * Opponent: K Groove, Maki ratio 2". Empty when there is nothing to say. */
static void intro_text(const transition_observation *o, wchar_t *buf, size_t cap)
{
    bool any = false;
    buf[0] = 0;
    for (unsigned p = 0; p < TRANSITION_OWNERS; p++) {
        const transition_side *s = &o->side[p];
        wchar_t label[TRANSITION_LABEL];
        bool first = true;
        if (!s->present || !s->count) continue;
        transition_side_label(s, label, TRANSITION_LABEL);
        add(buf, cap, L"%ls%ls: ", any ? L". " : L"", label);
        if (s->style[0]) add(buf, cap, L"%ls, ", s->style);
        for (unsigned i = 0; i < s->count && i < TRANSITION_TEAM_MAX; i++) {
            if (!s->member[i].valid) continue;
            if (!first) add(buf, cap, L", ");
            member_text(buf, cap, &s->member[i], L" ");
            first = false;
        }
        any = true;
    }
}

bool transition_channel_offer(transition_channel *c,
                              const transition_observation *o)
{
    wchar_t line[TRANSITION_TEXT];
    if (!c || !c->begun || !o) return false;

    /* ---- the introduction, once per composition -------------------------- */
    if (o->team_ready) {
        intro_text(o, line, TRANSITION_TEXT);
        if (line[0] && (!c->introduced || wcscmp(line, c->intro))) {
            if (!tc_say(line, TRUE, c->source)) { c->refused++; return false; }
            c->spoken++;
            c->introduced = true;
            wcscpy(c->intro, line);
            /* The fighters on screen were just introduced; do not repeat them
             * as a change immediately afterwards. */
            for (unsigned p = 0; p < TRANSITION_OWNERS; p++) {
                const transition_member *cur = &o->side[p].current;
                c->announced[p] = o->side[p].present && cur->valid;
                c->announced_id[p] = cur->id;
            }
            return true;
        }
    }

    /* ---- whichever active fighter changed, grouped ------------------------ */
    bool include[TRANSITION_OWNERS] = { false };
    bool any = false;
    line[0] = 0;
    for (unsigned p = 0; p < TRANSITION_OWNERS; p++) {
        const transition_side *s = &o->side[p];
        if (!s->present || !s->current.valid) continue;
        if (c->announced[p] && c->announced_id[p] == s->current.id) continue;
        if (any) add(line, TRANSITION_TEXT, L". ");
        member_text(line, TRANSITION_TEXT, &s->current, L", ");
        include[p] = any = true;
    }
    if (!any) return false;
    /* ⭐ Latch only on acceptance. A refusal leaves every change owed, and the
     * next offer recomputes from the newest observation -- so an obsolete
     * fighter is never spoken late. */
    if (!tc_say(line, TRUE, c->source)) { c->refused++; return false; }
    c->spoken++;
    for (unsigned p = 0; p < TRANSITION_OWNERS; p++) if (include[p]) {
        c->announced[p] = true;
        c->announced_id[p] = o->side[p].current.id;
    }
    return true;
}
