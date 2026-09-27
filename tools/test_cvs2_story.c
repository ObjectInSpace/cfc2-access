/* Synthetic renderer calls plus the actual native credits stream.
 * This does NOT represent a live boss clear or validate hook ABI at runtime. */
#include "../src/text_channel.c"
#include "../src/cvs2_story.c"
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
static BYTE fixture[CREDIT_SIZE];
static BYTE state[CVS2_STATE_SIZE];
static wchar_t said[TEXT_CAP];
static int calls, accepted = 1, substitutions;
BOOL mem_copy(void *dst, const void *src, SIZE_T n)
{
    if ((ULONG_PTR)src == CREDIT_BASE) {
        assert(n == CREDIT_SIZE);
        memcpy(dst, fixture, n);
    } else memcpy(dst, src, n);
    return TRUE;
}
BOOL speech_say(const wchar_t *s, BOOL interrupt)
{
    assert(!interrupt);
    ++calls;
    wcscpy(said, s);
    return accepted;
}
void log_line(const char *format, ...) { (void)format; }
MH_STATUS WINAPI MH_CreateHook(LPVOID a, LPVOID b, LPVOID *c)
{ (void)a; (void)b; (void)c; return MH_ERROR_UNSUPPORTED_FUNCTION; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID a) { (void)a; return MH_OK; }
MH_STATUS WINAPI MH_EnableHook(LPVOID a) { (void)a; return MH_OK; }
MH_STATUS WINAPI MH_DisableHook(LPVOID a) { (void)a; return MH_OK; }
static void substitute(char *s, int x, int y)
{
    (void)x; (void)y;
    if (!strcmp(s, "Winner: placeholder")) {
        strcpy(s, "Winner: Ryu");
        ++substitutions;
    }
}
static void draw(int entry, int palette, const float *position)
{
    char a[80] = "Utilizing skill and power, Ryu";
    char b[80] = "triumphs once again.";
    (void)palette; (void)position;
    if (entry == 243) strcpy(a, "Winner: placeholder");
    line_detour(a, 0, 0);
    if (entry != 243) line_detour(b, 0, 24);
}
static void poll(void) { cvs2_story_observe(state, sizeof state, 1); }
static void pointer(SIZE_T offset)
{
    ULONGLONG p = CREDIT_BASE + offset;
    memcpy(state + 0x21650, &p, sizeof p);
}
int main(void)
{
    FILE *f = fopen("tools/fixtures/cvs2-credits-stream.bin", "rb");
    SIZE_T start = 0, i, lines = 0, second = 0, third = 0;
    wchar_t text[TEXT_CAP];
    assert(f);
    assert(fread(fixture, 1, sizeof fixture, f) == sizeof fixture);
    assert(fgetc(f) == EOF); fclose(f);
    for (i = 0; i < sizeof fixture; ++i) {
        if (fixture[i] != 0xFE) continue;
        assert(cvs2_credit_line(fixture + start, i + 1 - start, text, TEXT_CAP));
        if (text[0]) {
            if (!lines) { assert(!wcscmp(text,L"CAPCOM VS. SNK 2")); second = i+1; }
            if (lines == 1) { assert(!wcscmp(text,L"STAFF")); third = i+1; }
            ++lines;
        }
        start = i+1;
    }
    assert(lines == 172);
    assert(!wcscmp(text, L"")); /* Last F1-only animation line. */
    assert(!cvs2_credit_line(fixture, 10, text, TEXT_CAP));
    assert(!cvs2_credit_line(fixture+1, second-1, text, 2));
    real_story = draw; real_line = substitute;
    poll();
    story_detour(48, 0, NULL); poll();
    assert(calls == 1 && !wcscmp(said,L"Utilizing skill and power, Ryu triumphs once again."));
    for (i = 0; i < 1000; ++i) { story_detour(48, 0, NULL); poll(); }
    assert(calls == 1);
    story_detour(49, 0, NULL);
    accepted = 0; poll(); assert(calls == 2);
    accepted = 1; poll(); assert(calls == 3);
    poll(); assert(calls == 3);
    story_detour(243, 0, NULL); poll();
    assert(calls == 4 && substitutions == 1 && !wcscmp(said,L"Winner: Ryu"));
    cvs2_story_observe(NULL,0,0); poll();
    story_detour(48, 0, NULL); poll(); assert(calls == 5);
    memcpy(state+0x78,"\x02\x01\x04\x02\x01",5);
    pointer(0); poll(); assert(calls == 5);
    pointer(second-1); poll(); assert(calls == 5); /* Incomplete native draw. */
    pointer(third); poll(); assert(calls == 6 && !wcscmp(said,L"CAPCOM VS. SNK 2"));
    poll(); assert(calls == 7 && !wcscmp(said,L"STAFF"));
    poll(); assert(calls == 7);
    state[0x7c] = 2; poll();
    state[0x7c] = 1; pointer(third); poll();
    assert(calls == 8 && !wcscmp(said,L"STAFF")); /* Attach mid-roll. */
    pointer(CREDIT_SIZE+10); poll(); assert(calls == 8);
    cvs2_story_observe(NULL,0,0);
    puts("PASS: story page capture, name substitution, deduplication, reset, speech retry; native credits decoding and cursor replay");
    return 0;
}
