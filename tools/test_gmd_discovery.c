/* Exercise the production resolver against real guarded virtual allocations.
 * Restrict only the address-space enumeration; reads and verification are real. */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static BYTE *test_base;
static SIZE_T test_size;
static unsigned queries;
static void test_system_info(LPSYSTEM_INFO info)
{
    GetSystemInfo(info);
    info->lpMinimumApplicationAddress = test_base;
    info->lpMaximumApplicationAddress = test_base + test_size;
}
static SIZE_T test_query(LPCVOID p, PMEMORY_BASIC_INFORMATION m, SIZE_T n)
{
    ++queries;
    return VirtualQuery(p, m, n);
}
#define GetSystemInfo test_system_info
#define VirtualQuery test_query
#define GMD_HINT_FILE ""   /* never read or write a hint file here */
#include "../src/gmd_find.c"
#undef GetSystemInfo
#undef VirtualQuery

void log_line(const char *fmt, ...) { (void)fmt; }
static int fails, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static unsigned max_queries;
static double max_ms;
static BOOL poll_ready(void)
{
    LARGE_INTEGER start, stop, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    queries = 0;
    BOOL ready = gmd_text_ready();
    QueryPerformanceCounter(&stop);
    double ms = 1000.0 * (stop.QuadPart - start.QuadPart) / freq.QuadPart;
    if (ms > max_ms) max_ms = ms;
    if (queries > max_queries) max_queries = queries;
    return ready;
}
static BOOL resolve(void)
{
    for (unsigned i = 0; i < 3000; ++i) {
        if (poll_ready()) return TRUE;
        /* Skip only the between-pass cooldown for these offline controls. */
        g_discovery.retry_at = 0;
    }
    return FALSE;
}

static void plant(BYTE *base)
{
    BYTE *labels = base + 0x30000;
    SIZE_T pos = 0;
    for (int i = 0; i < GMD_NPROBES; ++i) {
        const gmd_probe *p = &g_gmd_probes[i];
        BYTE *e = base + ENT_SIZE * p->index;
        unsigned sid = p->index;
        UINT64 ptr = (UINT64)(uintptr_t)(labels + pos);
        memcpy(e, &sid, 4);
        memcpy(e + 4, &p->hash1, 4);
        memcpy(e + 8, &p->hash2, 4);
        memcpy(e + 16, &ptr, 8);
        strcpy((char *)labels + pos, p->label);
        pos += strlen(p->label) + 1;
    }
    strcpy((char *)base + 0x120000, "Japonais [jp]"); /* stale first hint */
    strcpy((char *)base + 0x1b0000, "Japanese [jp]"); /* moved English */
}

int main(void)
{
    test_size = 32 * 1024 * 1024;
    test_base = VirtualAlloc(NULL, test_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!test_base) return 2;
    memset(test_base, 0x22, test_size);
    BYTE *table = test_base + 16 * 1024 * 1024;
    memset(table, 0, 0x200000);
    plant(table);
    gmd_test_set_expected_language("eng");
    BOOL ready = poll_ready();
    CHECK(!ready); /* one poll must yield before walking the whole allocation */
    CHECK(queries <= 16);
    unsigned polls = 1;
    while (!ready && polls < 2000) {
        ready = poll_ready();
        ++polls;
    }
    CHECK(ready);
    CHECK(max_queries <= 16);
    CHECK(g_text_blob == table + 0x1b0000);
    CHECK(gmd_text_language() && strcmp(gmd_text_language(), "eng") == 0);

    /* A real language change must clear old output and eventually select the
     * other blob, not cache the first resolution or erase discovery forever. */
    gmd_test_set_expected_language("fre");
    CHECK(gmd_text_recheck_language());
    CHECK(gmd_text_count() == 0);
    CHECK(resolve());
    CHECK(g_text_blob == table + 0x120000);

    /* Change target language in the middle of a candidate search. */
    gmd_text_invalidate();
    gmd_test_set_expected_language("eng");
    for (unsigned i = 0; i < 1000 && !g_discovery.candidate; ++i) poll_ready();
    CHECK(g_discovery.candidate == table);
    CHECK(!poll_ready()); /* first hint is French */
    gmd_test_set_expected_language("fre");
    CHECK(resolve());
    CHECK(g_text_blob == table + 0x120000);

    /* Reject a candidate freed/protected between scan and blob selection. */
    gmd_text_invalidate();
    for (unsigned i = 0; i < 1000 && !g_discovery.candidate; ++i) poll_ready();
    CHECK(g_discovery.candidate == table);
    DWORD old;
    CHECK(VirtualProtect(table, 0x200000, PAGE_NOACCESS, &old));
    CHECK(!poll_ready());
    CHECK(gmd_text_count() == 0 && g_discovery.candidate == NULL);
    CHECK(VirtualProtect(table, 0x200000, old, &old));
    CHECK(resolve());

    /* Guard pages are skipped without consuming the guard. */
    gmd_text_invalidate();
    CHECK(VirtualProtect(test_base, 0x10000, PAGE_READWRITE | PAGE_GUARD, &old));
    CHECK(resolve());
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(test_base, &m, sizeof(m)) && (m.Protect & PAGE_GUARD));
    CHECK(VirtualProtect(test_base, 0x10000, PAGE_READWRITE, &old));

    /* No table: finish the whole pass, back off, then find one appearing on
     * a later pass. A restart-at-zero-on-every-poll implementation starves. */
    gmd_text_invalidate();
    memset(table, 0, 0x200000);
    for (unsigned i = 0; i < 1000 && !g_discovery.retry_at; ++i)
        CHECK(!poll_ready());
    CHECK(g_discovery.retry_at != 0);
    CHECK(!poll_ready() && queries == 0);
    plant(table);
    g_discovery.retry_at = 0;
    CHECK(resolve());

    /* Long Spanish strings must be traversed in blocks, and the original
     * per-string length bound must still reject malformed data. */
    BYTE *spanish = table + 0x220000;
    memset(spanish, 'x', 29 * 8192 + 64);
    for (int i = 0; i < 29; ++i) spanish[(i + 1) * 8192 - 1] = 0;
    strcpy((char *)spanish + 29 * 8192, "Aceptar");
    CHECK(skip_strings(spanish, 29) == spanish + 29 * 8192);
    spanish[8191] = 'x';
    CHECK(skip_strings(spanish, 29) == NULL);
    /* A REMEMBERED table address (cfc2_text_hint.txt in the DLL) is tried
     * first: it resolves in a handful of polls instead of a full scan. */
    {
        unsigned n = 0;
        gmd_test_set_expected_language("eng");
        gmd_text_invalidate();
        /* Invalidation re-initialises discovery, which reloads the address
         * from the hint file (disabled here), so set it after that. */
        CHECK(!poll_ready());
        g_discovery.candidate = NULL;
        g_discovery.remembered = (uintptr_t)table;
        g_discovery.cursor = g_discovery.minimum;       /* the scan starts over */
        g_discovery.scanned = 0;
        CHECK(!poll_ready());
        /* Taken on the FIRST poll, before any scan step; the remaining polls
         * are the language-offset walk (this fixture's English sits outside
         * the hint list; the live game's 0xD0000 is fourth in it). */
        CHECK(g_discovery.candidate == table && g_discovery.from_hint);
        CHECK(g_discovery.scanned == 0 || g_discovery.cursor == g_discovery.minimum);
        while (n < 400 && !poll_ready()) ++n;
        printf("remembered address resolved after %u more polls\n", n);
        CHECK(g_text_blob == table + 0x1b0000 && g_discovery.from_hint);
        /* A stale address (nothing valid there) never earns trust: the scan
         * still finds the table. */
        gmd_text_invalidate();
        CHECK(!poll_ready());
        g_discovery.candidate = NULL;
        g_discovery.remembered = (uintptr_t)(test_base + 0x1000);
        g_discovery.from_hint = FALSE;
        g_discovery.cursor = g_discovery.minimum;
        CHECK(resolve());
        CHECK(g_text_blob == table + 0x1b0000 && !g_discovery.from_hint);
        g_discovery.remembered = 0;
    }
    CHECK(max_queries <= 16);
    printf("discovery: %d checks, %d failures, first resolution %u polls, "
           "max %u scanner queries/poll, max %.3f ms/poll (observed)\n",
           checks, fails, polls, max_queries, max_ms);
    VirtualFree(test_base, 0, MEM_RELEASE);
    return fails ? 1 : 0;
}
