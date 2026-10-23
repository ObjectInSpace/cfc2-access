/* Synthetic branch tests and shared-presenter integration, not live acceptance.
 * Optional arguments: game (4/5), variant (0/1), uncompressed captured root.
 * That mode runs exactly this decoder on a real snapshot without speech. */
#include "../src/pws_select_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned char root[0x8CD40];

static pws_select_sample sample(unsigned game)
{
    pws_select_sample s;
    memset(root, 0, sizeof root);
    if (game == 4) {
        root[0x27E58] = 1; root[0x27E5D] = 2; root[0x27E5E] = 2;
        root[0x27FF1] = 3;
        for (unsigned p = 0; p < 2; p++) {
            root[0x27DC2 + p*0x78] = 2;
            root[0x27DCC + p*0x78] = 1 + p;
            root[0x18428 + p*0x7890 + 3] = p;
        }
    } else {
        root[0x52BD8] = 1; root[0x52BD9] = 2; root[0x52BDC] = 1;
        root[0x52BDD] = 1; root[0x52BDE] = 1; root[0x52BDF] = 2;
        root[0x54B49] = 15;
        for (unsigned p = 0; p < 4; p++) {
            root[0x529D2 + p*0x98] = 1;
            root[0x40818 + p*0x42B8 + 1] = p;
            root[0x40818 + p*0x42B8 + 2] = p;
        }
    }
    CHECK(pws_select_from_state(game, 1, root, game == 4 ? 0x428F0 : sizeof root, &s));
    return s;
}

static bool accept = true;
static unsigned emitted;
static wchar_t last[GAME_SELECTION_TEXT];
static bool emit(void *context, const wchar_t *text)
{
    (void)context;
    emitted++;
    wcscpy(last, text);
    return accept;
}

static void offer(game_session *session, game_selection *presenter,
                  const pws_select_sample *s)
{
    game_selection_observation o = {0};
    CHECK(game_session_capture(session, &o.token));
    o.captured_ms = 100;
    o.complete = true;
    pws_selection_copy(s, o.owner);
    CHECK(game_session_publish(session, &o.token));
    CHECK(game_selection_offer(presenter, session, &o, 100));
}

int main(int argc, char **argv)
{
    if (argc == 4) {
        pws_select_sample s;
        game_selection_choice c[4];
        FILE *f = fopen(argv[3], "rb");
        CHECK(f);
        size_t n = fread(root, 1, sizeof root, f);
        CHECK(!ferror(f) && fgetc(f) == EOF);
        fclose(f);
        CHECK(pws_select_from_state((unsigned)strtoul(argv[1], NULL, 10),
              (unsigned)strtoul(argv[2], NULL, 10), root, n, &s));
        pws_selection_copy(&s, c);
        puts("Offline candidates only; not proof of active-screen eligibility.");
        for (unsigned p = 0; p < 4; p++)
            printf("slot %u: decode=%d raw=%u state=%u shuffle=%u name=%ls\n",
                   p + 1, pws_select_decode(&s, p), s.cursor[p], s.state[p],
                   s.shuffle[p], c[p].label);
        return 0;
    }
    CHECK(argc == 1);
    const int map[] = {8,0,6,2,4,3,5,7,1,9,10};
    for (unsigned game = 4; game <= 5; game++) {
        pws_select_sample base = sample(game), s;
        unsigned count = game == 4 ? 2 : 4;
        for (unsigned p = 0; p < count; p++) {
            for (unsigned value = 0; value < 256; value++) {
                s = base; s.cursor[p] = value;
                int expected = game == 4 ? (value < 11 ? map[value] : -2) :
                    (value < 16 && value != 12 && value != 13 ? (int)value : -2);
                CHECK(pws_select_decode(&s, p) == expected);
            }
            s = base; s.shuffle[p] = 1;
            CHECK(pws_select_decode(&s, p) == -2);
            s.shuffle[p] = 0x80000000u;
            CHECK(pws_select_decode(&s, p) == -2);
            s = base; s.slot[p] = 255;
            CHECK(pws_select_decode(&s, p) == -2);
            s = base; s.mask &= ~(1u << p);
            CHECK(pws_select_decode(&s, p) == -1);
            s = base; s.state[p]++;
            CHECK(pws_select_decode(&s, p) == -1);
        }
        for (unsigned k = 0; k < 8; k++) {
            bool required = game == 4 ? (k == 0 || k == 5 || k == 6) :
                                       (k == 0 || k == 1 || k >= 4);
            if (!required) continue;
            for (unsigned v = 0; v < 256; v++) if (v != base.phase[k]) {
                s = base; s.phase[k] = v;
                CHECK(pws_select_decode(&s, 0) == -1);
            }
        }
        for (unsigned k = 0; k < 2; k++) {
            s = base; s.gates[k] = 1;
            CHECK(pws_select_decode(&s, 0) == -1);
        }
        s = base; s.variant = 0;
        game_selection_choice c[4];
        pws_selection_copy(&s, c);
        CHECK(!wcscmp(c[0].label, L"Fokker"));
        s.variant = 1; pws_selection_copy(&s, c);
        CHECK(!wcscmp(c[0].label, L"Falcon"));
        s.variant = 2; CHECK(pws_select_decode(&s, 0) == -1);
        size_t n = game == 4 ? 0x428F0 : sizeof root;
        CHECK(!pws_select_from_state(game, 0, root, n-1, &s));
        CHECK(pws_select_decode(&s, 0) == -1);
        CHECK(!pws_select_from_state(game, 0, root, n+1, &s));
        CHECK(!pws_select_from_state(game, 2, root, n, &s));
    }
    CHECK(pws_select_decode(NULL, 0) == -1);
    CHECK(!pws_select_from_state(4, 0, NULL, 0x428F0, &(pws_select_sample){0}));
    CHECK(!pws_select_from_state(0, 0, root, 0x428F0, &(pws_select_sample){0}));
    CHECK(!pws_select_from_state(4, 0, root, 0x428F0, NULL));

    /* Four distinct owners flow through the unchanged shared presenter.
     * Shuffle suppresses speech, a refused backend retries, a departed session
     * cannot emit, and confirmation rearms the same character on re-entry. */
    game_session session = {0};
    game_selection presenter = {0};
    pws_select_sample s = sample(5);
    CHECK(game_session_begin(&session, 5, 11));
    CHECK(game_session_resume(&session, session.issued.generation, session.issued.revision));
    offer(&session, &presenter, &s);
    CHECK(game_selection_drain(&presenter, &session, 100, emit, NULL) == 0);
    offer(&session, &presenter, &s);
    accept = false;
    CHECK(game_selection_drain(&presenter, &session, 100, emit, NULL) == 0);
    accept = true;
    CHECK(game_selection_drain(&presenter, &session, 100, emit, NULL) == 15);
    CHECK(!wcscmp(last, L"Falcon. Ryoma. Wang-Tang. Jack"));
    unsigned before = emitted;
    s.shuffle[3] = 1; s.cursor[3] = 15;
    offer(&session, &presenter, &s); offer(&session, &presenter, &s);
    CHECK(game_selection_drain(&presenter, &session, 100, emit, NULL) == 0);
    CHECK(emitted == before);
    s.shuffle[3] = 0;
    offer(&session, &presenter, &s); offer(&session, &presenter, &s);
    CHECK(game_selection_drain(&presenter, &session, 100, emit, NULL) == 8);
    CHECK(!wcscmp(last, L"Pride"));
    s.state[3] = 2; offer(&session, &presenter, &s);
    s.state[3] = 1; offer(&session, &presenter, &s); offer(&session, &presenter, &s);
    CHECK(game_selection_drain(&presenter, &session, 100, emit, NULL) == 8);
    s.cursor[0] = 14; offer(&session, &presenter, &s); offer(&session, &presenter, &s);
    game_session_depart(&session);
    CHECK(game_selection_drain(&presenter, &session, 100, emit, NULL) == 0);
    printf("%u Power Stone selection checks passed (synthetic; no live acceptance).\n", checks);
    return 0;
}
