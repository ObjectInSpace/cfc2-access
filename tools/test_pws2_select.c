/* Replays the 2026-10-27 live Power Stone 2 capture through the shipping
 * decoder and the shared selection channel: the select screen (Falcon, one
 * cell out and back three times, confirmed) and everything after it, which
 * must stay silent. No game, no audible output. Run from the repository root.
 * Fixture: tools/make_pws2_fixtures.py. */
#include "../src/pws2_select.h"
#include "../src/pws2_frame.h"
#include "../src/audio.h"
#include "../src/character_name_table.h"
#include "pws2_fixture.h"
#include <wchar.h>

static int calls, accepts = 1;
static wchar_t last[600], said[64][256];
BOOL speech_say(const wchar_t *s, BOOL interrupt)
{
    (void)interrupt;
    if (accepts && calls < 64) wcsncpy(said[calls], s, 255);
    calls += accepts;
    wcsncpy(last, s, 599);
    return accepts;
}
void log_line(const char *fmt, ...) { (void)fmt; }
BOOL mem_guard_install(void) { return FALSE; }
BOOL mem_copy(void *d, const void *s, SIZE_T n) { (void)d; (void)s; (void)n; return FALSE; }
/* Audio routing is exercised by run_match_audio_test; the tick is not run here. */
#include <stdint.h>
uint64_t match_audio_begin(unsigned a) { (void)a; return 0; }
void match_audio_depart(uint64_t e) { (void)e; }
static uint64_t current;
uint64_t match_audio_current(void) { return current; }
LONG pws2_frame_count(void) { return 0; }
/* Pickup events and settings: a scripted queue and setting. The kinds are
 * pws2_frame.c's (stone 0xC1, food 0x10, prop 0xC3, items). */
static pws2_pickup_event queue[8];
static unsigned qn, qi;
static int option = 2;
bool pws2_pickup_pop(pws2_pickup_event *e) { if (qi >= qn) return false; *e = queue[qi++]; return true; }
bool pws2_pickup_kind(unsigned code, audio_ping_family *f, int *v)
{
    *v = 0;
    if (code == 0xC1) *f = AUDIO_PING_STONE;
    else if (code == 0x10) *f = AUDIO_PING_FOOD;
    else if (code == 0xC3) *f = AUDIO_PING_PROP;
    else if (code < 0xC0 || code == 0xC6) *f = AUDIO_PING_ITEM;
    else return false;
    return true;
}
int audio_option_get(audio_option o) { return o == AUDIO_OPT_PICKUP_SPEECH ? option : 0; }

static unsigned char *S;

/* The sample exactly as pws2_select_tick builds it from the same fields. */
static void sample_of(unsigned i, pws_select_sample *out)
{
    pws2_fixture_apply(i, S);
    assert(pws_select_from_state(5, 1, S, PWS2_FX_SIZE, out));
}

static void settle(const pws_select_sample *s, unsigned long long root)
{
    pws2_select_observe(s, root, 11);
    pws2_select_observe(s, root, 11);
}

int main(void)
{
    int seen[64], n = 0, confirmed = 0;
    unsigned first = ~0u, other = ~0u;
    pws_select_sample s;
    S = calloc(1, PWS2_FX_SIZE);
    assert(S);
    pws2_fixture_load();

    /* 1. Decode: only side 0 selects (participation mask 1); its highlights
     *    in order; nothing decodes after the confirmation. */
    for (unsigned i = 0; i < pws2_fx_count; i++) {
        int id;
        sample_of(i, &s);
        id = pws_select_decode(&s, 0);
        for (unsigned p = 1; p < 4; p++) assert(pws_select_decode(&s, p) == -1);
        if (s.state[0] == 2) confirmed = 1;
        if (confirmed) { assert(id == -1); continue; }
        if (id < 0) continue;
        if (first == ~0u) first = i;
        if (!n || seen[n - 1] != id) {
            assert(n < 64);
            seen[n++] = id;
            if (n == 2) other = i;
        }
    }
    printf("highlights:");
    for (int k = 0; k < n; k++) printf("%s %s", k ? "," : "", character_name(5, (unsigned)seen[k], 1));
    printf("\n");
    /* Falcon, then three steps out and back; the fight began with Falcon. */
    assert(n == 7 && confirmed);
    for (int k = 0; k < n; k += 2) assert(!strcmp(character_name(5, (unsigned)seen[k], 1), "Falcon"));
    assert(seen[1] == 6 && seen[3] == 8 && seen[5] == 2);

    /* 2. Spoken through the shared channel, replaying every sample: each
     *    settled highlight once, in order, nothing after the confirmation. */
    {
        unsigned long long root = pws2_fixture_root(0);
        pws2_select_observe(NULL, 0, 0);
        calls = 0;
        for (unsigned i = 0; i < pws2_fx_count; i++) {
            sample_of(i, &s);
            settle(&s, root);
        }
        printf("spoken:");
        for (int k = 0; k < calls && k < 32; k++) printf("%s %ls", k ? "," : "", said[k]);
        printf("\n");
        assert(calls == n);
        for (int k = 0; k < n; k++) {
            wchar_t want[64];
            swprintf(want, 64, L"%hs", character_name(5, (unsigned)seen[k], 1));
            assert(!wcscmp(said[k], want));
        }
        pws2_select_observe(NULL, 0, 0);
    }

    /* 3. A refused utterance is retried; a new root re-announces; the
     *    held-direction shuffle silences without forgetting. */
    {
        pws_select_sample a, b;
        unsigned long long root = pws2_fixture_root(first);
        int before;
        sample_of(first, &a);
        sample_of(other, &b);
        pws2_select_observe(NULL, 0, 0);
        before = calls;
        accepts = 0;
        settle(&a, root);
        accepts = 1;
        pws2_select_observe(&a, root, 11);
        assert(calls == before + 1 && !wcscmp(last, L"Falcon"));
        settle(&b, root);
        assert(calls == before + 2 && !wcscmp(last, L"Ayame"));
        before = calls;
        settle(&b, root + 0x1000);
        assert(calls == before + 1 && !wcscmp(last, L"Ayame"));
        {
            pws_select_sample sh = a;
            before = calls;
            sh.shuffle[0] = 1;
            settle(&sh, root + 0x1000);
            assert(calls == before);
            assert(pws_select_decode(&sh, 0) == -2);
        }
        /* Every other select byte in place but another step: not the select
         * screen (no live sample isolates the step). */
        {
            pws_select_sample step = a;
            for (unsigned v = 0; v < 256; v++) {
                step.phase[5] = (unsigned char)v;
                assert((pws_select_decode(&step, 0) >= 0) == (v == 1));
            }
        }
        /* Boss IDs are not choices. */
        {
            pws_select_sample boss = a;
            boss.cursor[0] = 12;
            assert(pws_select_decode(&boss, 0) == -2);
        }
        pws2_select_observe(NULL, 0, 0);
    }

    /* 4. Pickup announcements: "who, what kind", in the game's language; the
     *    F9 setting (every fighter / players only / off); another owner's or
     *    an unadmitted game's events dropped. Falcon is ID 0 (live). */
    {
        static const struct { unsigned id, code; bool human; const wchar_t *say; } ev[] = {
            { 0, 0xC1, true,  L"Falcon, power stone" },
            { 6, 0x10, false, L"Ayame, food" },
            { 8, 0xC3, false, L"Pete, something to throw" },
            { 2, 0x3B, false, L"Wang-Tang, item" },
            { 0, 0xC6, true,  L"Falcon, treasure" },
            { 0, 0xC2, true,  L"Falcon, item" },          /* unknown kind: item */
        };
        int before;
        current = 21;
        mt_set_language("eng");
        for (unsigned k = 0; k < 6; k++) {
            queue[0] = (pws2_pickup_event){ .epoch = 21, .fighter = k % 4, .id = ev[k].id,
                                            .code = ev[k].code, .human = ev[k].human };
            qn = 1; qi = 0;
            before = calls;
            pws2_select_speak_pickups(TRUE, 11);
            assert(calls == before + 1 && !wcscmp(last, ev[k].say));
        }
        /* Players only: a CPU's pickup is silent, a person's spoken. */
        option = 1;
        queue[0] = (pws2_pickup_event){ .epoch = 21, .id = 6, .code = 0x10, .human = false };
        queue[1] = (pws2_pickup_event){ .epoch = 21, .id = 0, .code = 0x10, .human = true };
        qn = 2; qi = 0; before = calls;
        pws2_select_speak_pickups(TRUE, 11);
        assert(calls == before + 1 && !wcscmp(last, L"Falcon, food"));
        /* Off: nothing; the queue still drains. */
        option = 0;
        qn = 2; qi = 0; before = calls;
        pws2_select_speak_pickups(TRUE, 11);
        assert(calls == before && qi == 2);
        option = 2;
        /* Another owner's event, or the game not admitted: dropped. */
        queue[0].epoch = 20; qn = 1; qi = 0; before = calls;
        pws2_select_speak_pickups(TRUE, 11);
        assert(calls == before && qi == 1);
        queue[0].epoch = 21; qn = 1; qi = 0;
        pws2_select_speak_pickups(FALSE, 11);
        assert(calls == before && qi == 1);
        /* The stage select: directions on landing, the stage once chosen;
         * spoken with pickup announcements off too. */
        option = 0;
        queue[0] = (pws2_pickup_event){ .kind = PWS2_EVENT_STAGES, .epoch = 21,
                                        .code = 6u | 7u << 3 | 0u << 6 | 1u << 9 | 2u << 12 };
        queue[1] = (pws2_pickup_event){ .kind = PWS2_EVENT_STAGE_CHOSEN, .epoch = 21, .code = 2 };
        qn = 2; qi = 0; before = calls;
        pws2_select_speak_pickups(TRUE, 11);
        assert(calls == before + 2 && !wcscmp(last, L"Stage: 3"));
        assert(calls < 64 && !wcscmp(said[calls - 2], L"Stage select. Stage: 1, left. Stage: 2, ahead left. "
                                     L"Stage: 3, ahead. Stage: 4, ahead right. Stage: 5, right."));
        option = 2;
        before = calls;
        /* Japanese: the game's own order and the language's words. */
        mt_set_language("jpn");
        queue[0] = (pws2_pickup_event){ .epoch = 21, .id = 0, .code = 0x10, .human = true };
        qn = 1; qi = 0;
        pws2_select_speak_pickups(TRUE, 10);
        assert(calls == before + 1 && wcsstr(last, L"食べ物"));
        mt_set_language("eng");
    }

    printf("PASS: %u live PWS2 samples, select highlights, confirmation, shared-channel speech/retry/re-announce, "
           "pickup announcements and their setting\n", pws2_fx_count);
    return 0;
}
