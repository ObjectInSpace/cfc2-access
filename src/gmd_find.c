#include "gmd_find.h"
#include "gmd_probes.h"
#include "memsafe.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <psapi.h>

/* GMD v2 label entry, 0x20 bytes:
 *   +0x00 i32 sectionId   (== array index, verified for all 4165)
 *   +0x04 u32 hash1       = ~crc32(label * 2)
 *   +0x08 u32 hash2       = ~crc32(label * 3)
 *   +0x0C u32 pad         = 0xCDCDCDCD on disk
 *   +0x10 i64 labelOffset
 *   +0x18 i64 listLink
 */
#define ENT_SIZE   0x20
#define ENT_SID    0x00
#define ENT_HASH1  0x04
#define ENT_HASH2  0x08
#define ENT_PAD    0x0C
#define ENT_LOFF   0x10

#define GMD_MAGIC_LE 0x00444D47u   /* "GMD\0" */
#define GMD_VERSION  0x00010302u
#define GMD_PAD      0xCDCDCDCDu

#define SCAN_CHUNK      (64 * 1024)
/* Synchronous offline diagnostic only. Never use this budget on the speech
 * worker: the live 2026-10-08 run demonstrated repeated eight-second stalls.
 * Production discovery below retains progress and bounds work per poll. */
#define SCAN_BUDGET_MS  8000
#define MAX_CANDIDATES  8

/* --- CRC-32, our own copy -------------------------------------------------
 * Used to RE-DERIVE a label's hash from the bytes we followed a pointer to.
 * That closes the loop: we found the entry by hash, so re-computing the same
 * hash from the resolved string proves the pointer chain is real rather than
 * a coincidence. Deliberately our own implementation, not the game's, so the
 * check is independent of the code being examined. */
static unsigned g_crc_tab[256];
static BOOL     g_crc_ready;

static void crc_init(void)
{
    if (g_crc_ready)
        return;
    for (unsigned i = 0; i < 256; i++) {
        unsigned c = i;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (unsigned)(-(int)(c & 1)));
        g_crc_tab[i] = c;
    }
    g_crc_ready = TRUE;
}

/* ~crc32(buf repeated `times`) -- the GMD label hash.
 *
 * Note there is NO final inversion: the GMD hash is `~crc32(x)`, and since a
 * standard crc32 finalizes by inverting, the two cancel. What the table loop
 * leaves in `c` is already the value we want. Verified against the shipped
 * entries (e.g. STATS_PWS2_06_NAME -> h1=0x0150cb7c, h2=0x7a4a7724). */
static unsigned gmd_hash(const char *s, unsigned len, int times)
{
    crc_init();
    unsigned c = 0xFFFFFFFFu;
    for (int t = 0; t < times; t++)
        for (unsigned i = 0; i < len; i++)
            c = g_crc_tab[(c ^ (unsigned char)s[i]) & 0xFF] ^ (c >> 8);
    return c;
}

/* --- Fast rejection filter -----------------------------------------------
 * The inner loop runs over every 4-byte position of every committed private
 * region -- hundreds of millions of iterations. A 64 KB bitmap keyed on the
 * low 16 bits of hash1 rejects ~99% of positions with one load and one test,
 * which is what makes a full pass affordable. */
static BYTE g_filter[8192];    /* bitmap over hash1 & 0xFFFF */
static BYTE g_byte0[256];      /* does ANY probe have this low byte? */
static BOOL g_filter_ready;

static void filter_init(void)
{
    if (g_filter_ready)
        return;
    memset(g_filter, 0, sizeof(g_filter));
    memset(g_byte0, 0, sizeof(g_byte0));
    for (int i = 0; i < GMD_NPROBES; i++) {
        unsigned h = g_gmd_probes[i].hash1;
        unsigned k = h & 0xFFFF;
        g_filter[k >> 3] |= (BYTE)(1u << (k & 7));
        g_byte0[h & 0xFF] = 1;
    }
    g_filter_ready = TRUE;
}

/* Two-stage reject. With 64 probes at most 64 of 256 low-byte values are
 * live, so the first test alone discards ~75% of positions with a single
 * array load -- and the 16-bit bitmap then takes the survivors down to
 * ~0.1%. This is what makes a byte-stride scan of hundreds of MB viable. */
static inline BOOL filter_hit(unsigned h1)
{
    if (!g_byte0[h1 & 0xFF])
        return FALSE;
    unsigned k = h1 & 0xFFFF;
    return (g_filter[k >> 3] >> (k & 7)) & 1;
}

/* Probes are sorted by hash1, so this is a binary search. */
static int probe_by_hash1(unsigned h1)
{
    int lo = 0, hi = GMD_NPROBES - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        unsigned v = g_gmd_probes[mid].hash1;
        if (v == h1) return mid;
        if (v < h1)  lo = mid + 1;
        else         hi = mid - 1;
    }
    return -1;
}

/* --- Per-region hit collection ------------------------------------------- */
typedef struct {
    BYTE *addr[GMD_NPROBES];   /* entry base for each probe, NULL if absent */
    int   found;
} region_hits;

static unsigned rd32(const BYTE *p) { unsigned v; memcpy(&v, p, 4); return v; }
static UINT64   rd64(const BYTE *p) { UINT64 v;   memcpy(&v, p, 8); return v; }

/* Scan one already-copied chunk. `chunk_va` is the live address the copy came
 * from, so recorded addresses refer to the game's memory, not our buffer. */
static void scan_chunk(const BYTE *buf, SIZE_T len, BYTE *chunk_va,
                       region_hits *hits)
{
    if (len < ENT_SIZE)
        return;
    /* hash1 sits at entry+0x04, so a match at `off` means the entry starts at
     * off-4. Start at 4 so that subtraction stays in range.
     *
     * ⚠ STRIDE 1, NOT 4. An earlier version stepped 4 bytes on the reasoning
     * that "entries are 4-byte aligned at worst" -- which is false. In a
     * verbatim file image the entry array begins at file offset 0x2d, so every
     * hash1 field lands at 1 mod 4 and a 4-byte scan NEVER LOOKS AT ONE. The
     * offline test caught this: 64 planted probes, 0 found. In the game the
     * allocation can land at any alignment, so byte stride is the only correct
     * choice. The 64 KB filter is what keeps it affordable. */
    /* Hot loop: one unaligned 32-bit load and one bitmap test per byte.
     * Reading through a memcpy'd local (rd32) defeats vectorisation and costs
     * ~10x; read directly from the buffer, which is our own copy and known
     * readable, and let the compiler use a plain unaligned load. */
    for (SIZE_T off = 4; off + 8 <= len; off++) {
        unsigned h1;
        memcpy(&h1, buf + off, 4);
        /* Single-byte reject first: the low byte of hash1 discriminates
         * before we even form the 16-bit key. */
        if (!filter_hit(h1))
            continue;
        int k = probe_by_hash1(h1);
        if (k < 0)
            continue;
        unsigned h2;
        memcpy(&h2, buf + off + 4, 4);
        if (h2 != g_gmd_probes[k].hash2)
            continue;
        if (!hits->addr[k]) {
            hits->addr[k] = chunk_va + off - ENT_HASH1;
            hits->found++;
        }
    }
}

/* --- Geometry -------------------------------------------------------------
 * Do the found probes sit where the on-disk array would put them? Each probe's
 * file index is known, so this is an exact test, not a heuristic: for any two
 * probes, the address gap must be 0x20 * (index difference). */
static BOOL check_geometry(const region_hits *hits, BYTE **array_base,
                           unsigned *stride, int *agree, int *pairs)
{
    *agree = 0;
    *pairs = 0;
    BYTE *base_vote = NULL;
    int base_agree = 0;

    for (int i = 0; i < GMD_NPROBES; i++) {
        if (!hits->addr[i]) continue;
        for (int j = i + 1; j < GMD_NPROBES; j++) {
            if (!hits->addr[j]) continue;
            (*pairs)++;
            INT64 expect = (INT64)ENT_SIZE *
                           (g_gmd_probes[j].index - g_gmd_probes[i].index);
            INT64 actual = (INT64)(hits->addr[j] - hits->addr[i]);
            if (expect == actual)
                (*agree)++;
        }
    }

    /* Each found probe independently implies an array base. Agreement across
     * many probes is heavily over-determined -- far stronger than any single
     * match could be. */
    for (int i = 0; i < GMD_NPROBES; i++) {
        if (!hits->addr[i]) continue;
        BYTE *b = hits->addr[i] - (SIZE_T)ENT_SIZE * g_gmd_probes[i].index;
        int votes = 0;
        for (int j = 0; j < GMD_NPROBES; j++) {
            if (!hits->addr[j]) continue;
            BYTE *bj = hits->addr[j] - (SIZE_T)ENT_SIZE * g_gmd_probes[j].index;
            if (bj == b) votes++;
        }
        if (votes > base_agree) { base_agree = votes; base_vote = b; }
    }

    *array_base = base_vote;
    *stride = ENT_SIZE;
    return *pairs > 0 && *agree == *pairs;
}

/* --- Verification --------------------------------------------------------- */

/* Read a NUL-terminated string from game memory into `dst`. Guarded. */
/* Read a NUL-terminated string out of foreign memory.
 *
 * \u26d4\u26d4 THIS USED TO DO ONE GUARDED mem_copy PER BYTE, and each of those does a
 * VirtualQuery plus a setjmp. That is ~100 guarded reads for a typical label,
 * and the discovery sweep performs about 12,500 string reads -- which is why a
 * single discovery block took EIGHT SECONDS in-game and starved the mod while
 * the user was still navigating.
 *
 * \u26a0 The offline index test did not catch it: its synthetic strings are ~6
 * bytes and sit in one clean page, so 4165 lookups measured 6 ms. The real blob
 * has longer strings, and the sweep does far more reads. A benchmark whose data
 * is unlike the real data measures the wrong thing -- twice now in this file.
 *
 * So: copy a BLOCK under one guard, then find the NUL in our own memory. The
 * block is trimmed if it would cross into unreadable pages, so a string near
 * the end of a region still reads correctly rather than failing.
 */
static BOOL read_cstr(const BYTE *src, char *dst, unsigned cap)
{
    if (!dst || cap == 0)
        return FALSE;
    dst[0] = '\0';

    volatile unsigned want = cap - 1;   /* lives across the guard setjmp */

    /* Do not read past what is actually mapped: ask first, then copy once. */
    SIZE_T avail = mem_readable_run(src, want);
    if (avail == 0)
        return FALSE;
    if (avail < want)
        want = (unsigned)avail;

    BOOL faulted = FALSE;
    unsigned n = want;
    MEM_GUARDED(faulted, memcpy(dst, src, n));
    if (faulted)
        return FALSE;

    /* Terminate: either the string ended inside the block, or we cap it. */
    for (unsigned i = 0; i < n; i++) {
        if (dst[i] == '\0')
            return TRUE;
    }
    dst[want] = '\0';
    return TRUE;
}

/* Follow one entry's labelOffset to the label text and re-derive its hashes.
 * This is the closed loop that turns "found" into "proved". */
static BOOL verify_label(BYTE *array_base, const gmd_probe *pr, gmd_form form,
                         char *out, unsigned out_cap)
{
    BYTE *entry = array_base + (SIZE_T)ENT_SIZE * pr->index;
    BYTE ent[ENT_SIZE];
    if (!mem_copy(ent, entry, ENT_SIZE))
        return FALSE;

    UINT64 loff = rd64(ent + ENT_LOFF);
    const BYTE *label_at;

    if (form == GMD_FORM_LIVE_TABLE) {
        label_at = (const BYTE *)(uintptr_t)loff;      /* absolute pointer */
    } else {
        if (loff >= GMD_LABEL_SIZE)
            return FALSE;
        label_at = array_base + GMD_LABELS_FROM_ENTRIES + loff;
    }

    char buf[128];
    if (!read_cstr(label_at, buf, sizeof(buf)))
        return FALSE;

    unsigned len = (unsigned)strlen(buf);
    if (len == 0 || len > 100)
        return FALSE;

    unsigned h1 = gmd_hash(buf, len, 2);
    unsigned h2 = gmd_hash(buf, len, 3);
    if (h1 != pr->hash1 || h2 != pr->hash2)
        return FALSE;

    _snprintf(out, out_cap, "%s", buf);
    out[out_cap - 1] = '\0';
    return TRUE;
}

/* Resolve a sectionId to its display string in the trailing string blob. */
/* In a LIVE_TABLE the label is reached by absolute pointer, so the display
 * string is most likely reachable the same way -- but from WHERE in the entry
 * is not something to guess. Probe the entry's 64-bit fields for a pointer to
 * a string that matches, and report which offset worked, so the layout is
 * discovered rather than assumed. */
/* ⚠ THE STRING-BLOB DELTA IS NOT STABLE ACROSS RUNS. Measured, three times:
 *      run 1  entries+0x0d0000
 *      run 2  entries+0x140000
 *      run 3  entries+0x170000
 * Same machine, same build, same array_base (0x3B7100A0, ASLR off). The label
 * blob has held at +0x30000 so far, but the string blob moves.
 *
 * This constant was previously the ONLY way the text check located the blob,
 * so run 3 read garbage and reported text_verified=no on a table that was
 * perfectly good -- while the fallback diagnostic search found the string
 * correctly at +0x170000 a line later. Treating a measured-once offset as
 * fixed is exactly the trap the comment here used to warn about while the code
 * did it anyway.
 *
 * So the value below is only a FIRST GUESS to try before searching. The
 * search is what establishes the answer. */
#define LIVE_STRINGS_HINT 0x140000

/* 64 KB-aligned deltas to try, cheapest first, before falling back to a scan.
 * The three observed values are all 64 KB multiples. */
static const SIZE_T g_strings_hints[] = {
    /* 0x120000 first: measured as the live blob once the stale one stopped
     * being picked. The others are earlier observations, kept because the blob
     * genuinely moves between runs. */
    0x120000, 0x140000, 0x170000, 0x0d0000, 0x30000, 0x100000, 0x200000,
};
#define NSTRING_HINTS (int)(sizeof(g_strings_hints) / \
                            sizeof(g_strings_hints[0]))

/* Walk `count` NUL-terminated strings forward from `p`. */
static const BYTE *skip_strings(const BYTE *p, int count)
{
    BYTE block[1024];
    unsigned length = 0;
    /* The Spanish tie-break skips 29 strings. Reading each byte with its own
     * VirtualQuery would reintroduce the worker stall during verification. */
    while (count > 0) {
        SIZE_T n = mem_readable_run(p, sizeof(block));
        if (!n || !mem_copy(block, p, n)) return NULL;
        for (SIZE_T i = 0; i < n; ++i) {
            if (++length > 8192) return NULL;
            if (!block[i]) {
                length = 0;
                if (!--count) return p + i + 1;
            }
        }
        p += n;
    }
    return p;
}

/* Does `s` match this label's display string in ANY shipped language?
 *
 * The live table holds whichever locale the game is running in. Comparing
 * against English alone reported text_verified=no on a correct, label-verified
 * find (an Italian run read "Giapponese [jp]"). Each candidate is still an
 * exact match against a real shipped string -- this is not a loosened test.
 * Returns the matching variant's index, or -1. */
static int verify_text_variant(const gmd_verify *v, const char *s)
{
    for (int i = 0; i < v->ntext; i++) {
        if (v->text[i] && strcmp(s, v->text[i]) == 0)
            return i;
    }
    return -1;
}

/* Try to resolve the label's text assuming the string blob sits at `delta`. */
static BOOL try_strings_at(BYTE *array_base, SIZE_T delta,
                           const gmd_verify *v, char *out, unsigned out_cap)
{
    /* sectionId is preserved in the live table (measured: entry[0] has
     * sid=0), so the display string is the sectionId'th string in the blob. */
    const BYTE *p = skip_strings(array_base + delta, v->index);
    if (!p)
        return FALSE;
    char buf[256];
    if (!read_cstr(p, buf, sizeof(buf)))
        return FALSE;
    if (!out[0]) {                 /* remember the first thing we read */
        _snprintf(out, out_cap, "%s", buf);
        out[out_cap - 1] = '\0';
    }
    if (verify_text_variant(v, buf) < 0)
        return FALSE;
    _snprintf(out, out_cap, "%s", buf);
    out[out_cap - 1] = '\0';
    return TRUE;
}

static BOOL verify_text_live(BYTE *array_base, const gmd_verify *v,
                             char *out, unsigned out_cap, int *found_off)
{
    /* The blob MOVES between runs (0xd0000 / 0x140000 / 0x170000 observed),
     * so try the known deltas and then search. Reporting the delta that
     * actually worked is what keeps this honest -- a hardcoded offset failed
     * silently on run 3 and claimed the text could not be verified. */
    for (int i = 0; i < NSTRING_HINTS; i++) {
        if (try_strings_at(array_base, g_strings_hints[i], v, out, out_cap)) {
            *found_off = (int)g_strings_hints[i];
            if (g_strings_hints[i] != LIVE_STRINGS_HINT)
                log_line("gmdfind: string blob was NOT at the usual "
                         "+0x%x -- found at +0x%llx this run (it moves)",
                         LIVE_STRINGS_HINT,
                         (unsigned long long)g_strings_hints[i]);
            return TRUE;
        }
    }

    /* None of the known deltas worked. Sweep 64 KB-aligned offsets across a
     * generous window rather than giving up: the blob has always been
     * 64 KB-aligned and within a few MB of the entry array. */
    for (SIZE_T d = 0x10000; d <= 0x400000; d += 0x10000) {
        if (try_strings_at(array_base, d, v, out, out_cap)) {
            *found_off = (int)d;
            log_line("gmdfind: ⭐ string blob located by SEARCH at +0x%llx "
                     "(no known delta matched) -- add it to g_strings_hints",
                     (unsigned long long)d);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL verify_text(BYTE *array_base, const gmd_verify *v, gmd_form form,
                        char *out, unsigned out_cap)
{
    if (form != GMD_FORM_FILE_IMAGE)
        return FALSE;   /* string blob only locatable in the verbatim image */

    const BYTE *strings = array_base + GMD_STRINGS_FROM_ENTRIES;

    /* Strings are NUL-separated in section order; walk to index v->index. */
    const BYTE *p = strings;
    for (int i = 0; i < v->index; i++) {
        char c;
        unsigned guard = 0;
        do {
            if (!mem_copy(&c, p, 1))
                return FALSE;
            p++;
            if (++guard > 4096)
                return FALSE;      /* runaway: not the blob we expected */
        } while (c);
    }

    char buf[256];
    if (!read_cstr(p, buf, sizeof(buf)))
        return FALSE;

    _snprintf(out, out_cap, "%s", buf);
    out[out_cap - 1] = '\0';
    return verify_text_variant(v, buf) >= 0;
}

/* --- Classification ------------------------------------------------------- */
static gmd_form classify(BYTE *array_base, unsigned *pad_seen)
{
    /* A verbatim file image has the GMD header immediately before the entry
     * array, at -GMD_ENTRIES_AT. That is a byte-exact identification. */
    BYTE hdr[0x20];
    if (mem_copy(hdr, array_base - GMD_ENTRIES_AT, sizeof(hdr))) {
        if (rd32(hdr) == GMD_MAGIC_LE && rd32(hdr + 4) == GMD_VERSION) {
            unsigned lc = rd32(hdr + 0x14);
            if (lc == GMD_LABEL_COUNT)
                return GMD_FORM_FILE_IMAGE;
        }
    }

    /* Otherwise decide from labelOffset: a small value is a file-relative
     * offset; a committed pointer means the table was rebuilt. */
    BYTE ent[ENT_SIZE];
    if (mem_copy(ent, array_base, ENT_SIZE)) {
        *pad_seen = rd32(ent + ENT_PAD);
        UINT64 loff = rd64(ent + ENT_LOFF);
        if (loff < GMD_LABEL_SIZE)
            return GMD_FORM_FILE_IMAGE;
        if (loff > 0x10000 && mem_readable((const void *)(uintptr_t)loff, 1))
            return GMD_FORM_LIVE_TABLE;
    }
    return GMD_FORM_UNKNOWN;
}

/* --- Driver --------------------------------------------------------------- */

typedef struct {
    BYTE     *array_base;
    gmd_form  form;
    int       found;
    int       agree, pairs;
    unsigned  pad;
    uintptr_t region_base;
    SIZE_T    region_size;
} candidate;

BOOL gmd_find(gmd_result *out)
{
    memset(out, 0, sizeof(*out));
    out->probes_total = GMD_NPROBES;

    if (!mem_guard_install()) {
        log_line("gmdfind: ABORTED -- no fault guard; scanning foreign memory "
                 "unguarded is how the hook crashed the game before");
        return FALSE;
    }
    filter_init();
    crc_init();

    /* Never match our own probe table: it lives in this DLL's .rdata and
     * would otherwise be "found" as a perfect candidate. */
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)&g_gmd_probes, &self);
    uintptr_t self_lo = 0, self_hi = 0;
    if (self) {
        MODULEINFO mi;
        if (GetModuleInformation(GetCurrentProcess(), self, &mi, sizeof(mi))) {
            self_lo = (uintptr_t)mi.lpBaseOfDll;
            self_hi = self_lo + mi.SizeOfImage;
        }
    }

    /* ⚠ SKIP THIS THREAD'S STACK.
     *
     * Reading a stack guard page raises STATUS_GUARD_PAGE_VIOLATION
     * (0x80000001), not an access violation, and catching it is not an option:
     * the guard page exists so the kernel can grow the stack, and longjmp-ing
     * out of it leaves the stack in a broken state. The first version of this
     * scanner walked its own stack and died instantly with exactly that code.
     *
     * VirtualQuery's PAGE_GUARD check is not enough on its own, because
     * touching a guard page ARMS the next one down -- a scan walking up the
     * stack keeps tripping freshly-armed pages. Exclude the whole stack
     * region instead. The message table is never on our own stack anyway. */
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t stack_hi = (uintptr_t)tib->StackBase;
    uintptr_t stack_lo = (uintptr_t)tib->StackLimit;
    /* StackLimit is only the committed part; the reserved region below it
     * holds the guard page. Query the allocation to get the true extent. */
    {
        MEMORY_BASIC_INFORMATION smbi;
        if (VirtualQuery((void *)stack_lo, &smbi, sizeof(smbi)))
            stack_lo = (uintptr_t)smbi.AllocationBase;
    }
    log_line("gmdfind: excluding own stack %p..%p and own module %p..%p",
             (void *)stack_lo, (void *)stack_hi,
             (void *)self_lo, (void *)self_hi);

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    BYTE *addr = (BYTE *)si.lpMinimumApplicationAddress;
    BYTE *limit = (BYTE *)si.lpMaximumApplicationAddress;

    DWORD start = GetTickCount();
    LONG faults_before = mem_guard_fault_count();
    SIZE_T regions = 0, bytes = 0;
    BOOL truncated = FALSE;

    candidate cands[MAX_CANDIDATES];
    int ncand = 0;

    static BYTE chunk[SCAN_CHUNK];

    SIZE_T walked = 0;
    while (addr < limit) {
        walked++;
        if (GetTickCount() - start > SCAN_BUDGET_MS) {
            truncated = TRUE;
            break;
        }

        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) {
            /* Do NOT break: that abandons the rest of the address space.
             * Step forward by the allocation granularity and keep going. */
            addr += si.dwAllocationGranularity;
            continue;
        }

        /* volatile: `next` advances the region walk and lives across the
         * MEM_GUARDED longjmp below. A stale restored copy would rewind the
         * walk -- potentially forever. */
        BYTE *volatile next = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
        if (next <= addr)
            next = addr + si.dwAllocationGranularity;

        /* ⚠ PAGE_GUARD is 0x100 and PAGE_NOACCESS is 0x01, so they must be
         * tested on the UNMASKED value. Masking with 0xFF (to drop modifier
         * bits like PAGE_WRITECOMBINE before the access-type comparison) also
         * clears PAGE_GUARD -- an earlier version did exactly that, making the
         * guard-page test dead code. Mask only for the access-type check. */
        DWORD access = mbi.Protect & 0xFF;
        BOOL guarded = (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0;
        BOOL usable = mbi.State == MEM_COMMIT &&
                      mbi.Type == MEM_PRIVATE &&
                      !guarded &&
                      (access & (PAGE_READONLY | PAGE_READWRITE |
                                 PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                 PAGE_EXECUTE_READWRITE));

        BOOL is_self = self_lo && (uintptr_t)mbi.BaseAddress < self_hi &&
                       (uintptr_t)next > self_lo;
        /* Skip ANY thread stack, not just our own.
         *
         * Every thread's stack carries a guard page, and reading one raises
         * the fatal STATUS_GUARD_PAGE_VIOLATION. A stack is recognisable
         * without enumerating threads: it is a MEM_PRIVATE allocation whose
         * region carries PAGE_GUARD somewhere, so treat a guard-flagged
         * region -- and the whole allocation it belongs to -- as off limits.
         *
         * The chunk-level VirtualQuery below is the backstop for guard pages
         * that appear inside an otherwise-clean region. */
        BOOL is_stack = ((uintptr_t)mbi.BaseAddress < stack_hi &&
                         (uintptr_t)next > stack_lo) ||
                        (mbi.Protect & PAGE_GUARD) != 0;   /* unmasked: 0x100 */
        if (is_stack) {
            addr = next;
            continue;
        }

        if (usable && !is_self && mbi.RegionSize >= ENT_SIZE) {
            BYTE  *base = (BYTE *)mbi.BaseAddress;
            SIZE_T size = mbi.RegionSize;
            regions++;
            bytes += size;

            region_hits hits;
            memset(&hits, 0, sizeof(hits));

            /* Chunked: copy a piece out under the guard, then scan the copy.
             * Copying first means a region freed mid-scan costs one chunk,
             * not the pass -- and we never read game memory in a tight loop
             * that could fault on every iteration. */
            /* volatile: lives across the MEM_GUARDED setjmp/longjmp below.
             * -Wclobbered is a real defect here, not noise -- a longjmp can
             * restore a stale register copy and silently rewind the loop. */
            for (volatile SIZE_T off = 0; off < size;
                 off += SCAN_CHUNK - ENT_SIZE) {
                /* Budget checked per CHUNK, not per region: the game has
                 * single regions of hundreds of MB, so a per-region check
                 * overshoots the budget by many seconds. */
                if (GetTickCount() - start > SCAN_BUDGET_MS) {
                    truncated = TRUE;
                    break;
                }
                SIZE_T want = size - off;
                if (want > SCAN_CHUNK) want = SCAN_CHUNK;
                if (want < ENT_SIZE) break;

                /* Re-check protection for THIS chunk, not just the region.
                 * A region's Protect is the value at its base; pages inside a
                 * MEM_PRIVATE allocation can differ -- notably a thread stack,
                 * whose guard page sits inside an otherwise-readable region.
                 * Touching that raises STATUS_GUARD_PAGE_VIOLATION, which is
                 * fatal and deliberately not caught. */
                MEMORY_BASIC_INFORMATION cmbi;
                if (!VirtualQuery(base + off, &cmbi, sizeof(cmbi)))
                    continue;
                if (cmbi.State != MEM_COMMIT)
                    continue;
                if (cmbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
                    continue;

                /* Clamp to what this query already proved committed, rather
                 * than re-walking the same pages in mem_readable_run. */
                SIZE_T avail = (SIZE_T)((BYTE *)cmbi.BaseAddress +
                                        cmbi.RegionSize - (base + off));
                SIZE_T got = want < avail ? want : avail;
                if (got < ENT_SIZE)
                    continue;
                /* Copy under the fault guard, but skip mem_copy's own
                 * readability walk -- the VirtualQuery above already
                 * established it, and repeating it per chunk was a measurable
                 * share of the scan. The guard still covers the real risk:
                 * the pages going away between that query and this copy. */
                volatile BOOL cfault;
                MEM_GUARDED(cfault, memcpy(chunk, base + off, got));
                if (cfault)
                    continue;

                scan_chunk(chunk, got, base + off, &hits);
            }

            if (hits.found >= 2 && ncand < MAX_CANDIDATES) {
                BYTE *ab = NULL;
                unsigned stride = ENT_SIZE;
                int agree = 0, pairs = 0;
                check_geometry(&hits, &ab, &stride, &agree, &pairs);
                if (ab) {
                    unsigned pad = 0;
                    candidate *c = &cands[ncand++];
                    c->array_base  = ab;
                    c->found       = hits.found;
                    c->agree       = agree;
                    c->pairs       = pairs;
                    c->form        = classify(ab, &pad);
                    c->pad         = pad;
                    c->region_base = (uintptr_t)base;
                    c->region_size = size;
                }
            }
        }

        addr = next;
    }

    DWORD elapsed = GetTickCount() - start;
    LONG faults = mem_guard_fault_count() - faults_before;

    /* Positive count of what was examined. "Found nothing" and "never looked"
     * must never be the same log line. */
    /* Report KB, not MB: a few hundred KB rounds to "0 MB", which reads as
     * "scanned nothing" -- exactly the confusion these logs exist to prevent. */
    log_line("gmdfind: walked %llu regions; scanned %llu private (%llu KB) in "
             "%lu ms; %d candidate(s); guard absorbed %ld fault(s)%s",
             (unsigned long long)walked,
             (unsigned long long)regions,
             (unsigned long long)(bytes / 1024),
             elapsed, ncand, faults,
             truncated ? "; BUDGET EXHAUSTED -- PARTIAL SCAN" : "");

    if (ncand == 0) {
        if (truncated)
            log_line("gmdfind: no candidates, but the scan was TRUNCATED -- "
                     "this is NOT evidence of absence.");
        else
            log_line("gmdfind: complete scan, no candidates. Most likely the "
                     "messages are not resident yet (they load with the first "
                     "menu). Retry after navigating to a menu.");
        return FALSE;
    }

    /* Report every candidate; prefer a rebuilt live table over a file image. */
    int best = 0;
    for (int i = 0; i < ncand; i++) {
        const char *fname = cands[i].form == GMD_FORM_FILE_IMAGE ? "FILE_IMAGE"
                          : cands[i].form == GMD_FORM_LIVE_TABLE ? "LIVE_TABLE"
                          : "UNKNOWN";
        log_line("gmdfind:  candidate %d: array_base=%p form=%s "
                 "probes=%d/%d geometry=%d/%d pairs pad=0x%08x "
                 "region=%p+%lluKB",
                 i, (void *)cands[i].array_base, fname,
                 cands[i].found, GMD_NPROBES, cands[i].agree, cands[i].pairs,
                 cands[i].pad, (void *)cands[i].region_base,
                 (unsigned long long)(cands[i].region_size / 1024));
        if (cands[i].form == GMD_FORM_LIVE_TABLE &&
            cands[best].form != GMD_FORM_LIVE_TABLE)
            best = i;
    }

    candidate *c = &cands[best];
    out->array_base     = c->array_base;
    out->form           = c->form;
    out->probes_found   = c->found;
    out->derived_stride = ENT_SIZE;
    out->geometry_exact = (c->pairs > 0 && c->agree == c->pairs);
    out->region_base    = c->region_base;
    out->region_size    = c->region_size;

    /* --- Closed-loop verification --- */
    for (int i = 0; i < GMD_NPROBES && !out->label_verified; i++) {
        if (verify_label(c->array_base, &g_gmd_probes[i], c->form,
                         out->sample_label, sizeof(out->sample_label))) {
            out->label_verified = TRUE;
            log_line("gmdfind: ⭐ CLOSED LOOP -- followed labelOffset to "
                     "\"%s\", re-hashed those bytes, hashes MATCH the entry. "
                     "This is the GMD label array.", out->sample_label);
        }
    }
    if (!out->label_verified)
        log_line("gmdfind: ⚠ label chain did NOT verify -- the region matches "
                 "structurally but labelOffset does not resolve. Treat the "
                 "find as UNCONFIRMED.");

    for (int i = 0; i < GMD_NVERIFY && !out->text_verified; i++) {
        if (c->form == GMD_FORM_LIVE_TABLE) {
            int off = -1;
            if (verify_text_live(c->array_base, &g_gmd_verify[i],
                                 out->sample_text, sizeof(out->sample_text),
                                 &off)) {
                out->text_verified = TRUE;
                int vi = verify_text_variant(&g_gmd_verify[i],
                                             out->sample_text);
                log_line("gmdfind: ⭐⭐ TEXT RESOLVED (live) -- \"%s\" = "
                         "\"%s\", via a pointer at entry+0x%02x. We can READ "
                         "the shell's messages. (matched shipped variant %d "
                         "of %d%s)",
                         g_gmd_verify[i].label, out->sample_text, off,
                         vi + 1, g_gmd_verify[i].ntext,
                         vi == 0 ? ", English" : ", a non-English locale");
            }
        } else if (verify_text(c->array_base, &g_gmd_verify[i], c->form,
                               out->sample_text, sizeof(out->sample_text))) {
            out->text_verified = TRUE;
            int vi = verify_text_variant(&g_gmd_verify[i], out->sample_text);
            log_line("gmdfind: ⭐⭐ TEXT RESOLVED -- \"%s\" = \"%s\" "
                     "(matches shipped variant %d of %d%s). We can READ the "
                     "shell's messages.",
                     g_gmd_verify[i].label, out->sample_text,
                     vi + 1, g_gmd_verify[i].ntext,
                     vi == 0 ? ", English" : ", a non-English locale");
        }
    }
    /* Dump the raw entry so the live layout can be read off the log rather
     * than guessed at. The label pointer resolves but the display string does
     * not, so the string must be reached some other way -- most likely via
     * sectionId into a separate array. Print what is actually there. */
    if (out->label_verified && !out->text_verified) {
        BYTE ent[ENT_SIZE];
        const gmd_verify *v = &g_gmd_verify[0];
        BYTE *e = c->array_base + (SIZE_T)ENT_SIZE * v->index;
        if (mem_copy(ent, e, ENT_SIZE)) {
            log_line("gmdfind: [layout] entry[%d] (\"%s\") at %p:",
                     v->index, v->label, (void *)e);
            log_line("gmdfind: [layout]   +0x00 sid=%d  +0x04 h1=0x%08x  "
                     "+0x08 h2=0x%08x  +0x0c pad=0x%08x",
                     (int)rd32(ent + 0x00), rd32(ent + 0x04),
                     rd32(ent + 0x08), rd32(ent + 0x0c));
            log_line("gmdfind: [layout]   +0x10 = 0x%llx   +0x18 = 0x%llx",
                     (unsigned long long)rd64(ent + 0x10),
                     (unsigned long long)rd64(ent + 0x18));
            log_line("gmdfind: [layout]   expected display text: \"%s\"",
                     v->text);
            /* Is there an array of string pointers near the entry array? Look
             * just past the end of it, where the file keeps its bucket table. */
            /* The label blob turned out NOT to be at the file's relative
             * offset (measured: +0x30000 live vs +0x210a0 in the file), so
             * the runtime lays the blobs out its own way and the string blob
             * cannot be predicted either. Find it instead: search forward from
             * the entry array for the known display text and report the offset
             * that worked, so the layout is measured rather than assumed. */
            UINT64 lblptr = rd64(ent + 0x10);
            log_line("gmdfind: [layout]   label blob delta = +0x%llx "
                     "(file layout would be +0x%x)",
                     (unsigned long long)(lblptr - (UINT64)(uintptr_t)
                                          c->array_base),
                     GMD_LABELS_FROM_ENTRIES);

            /* Search for EVERY shipped locale's rendering of this label, not
             * just English -- otherwise this diagnostic silently reports
             * "not found" on a non-English build whose blob is right there. */
            BYTE *hay = c->array_base;
            SIZE_T span = 0x400000;      /* 4 MB window around the table */
            volatile BOOL found = FALSE;   /* lives across MEM_GUARDED longjmp */
            for (int t = 0; t < v->ntext && !found; t++) {
                const char *needle = v->text[t];
                if (!needle)
                    continue;
                SIZE_T tlen = strlen(needle);
                /* volatile: `d` lives across the MEM_GUARDED longjmp, and a
                 * fault must not resume the scan at a stale offset. */
                for (volatile SIZE_T d = 0; d + tlen < span && !found;
                     d += 0x1000) {
                    BYTE win[0x1000 + 64];
                    SIZE_T want = 0x1000 + tlen;
                    if (!mem_readable(hay + d, want))
                        continue;
                    volatile BOOL wf;
                    MEM_GUARDED(wf, memcpy(win, hay + d, want));
                    if (wf) continue;
                    for (SIZE_T i = 0; i + tlen <= want; i++) {
                        if (memcmp(win + i, needle, tlen) == 0) {
                            log_line("gmdfind: [layout]   ⭐ display text "
                                     "\"%s\" found at %p = entries+0x%llx",
                                     needle, (void *)(hay + d + i),
                                     (unsigned long long)(d + i));
                            found = TRUE;
                            break;
                        }
                    }
                }
            }
            if (!found)
                log_line("gmdfind: [layout]   display text NOT found within "
                         "4 MB after the entry array (tried %d locale "
                         "variants)", v->ntext);
        }
    }

    if (!out->text_verified && out->label_verified) {
        /* Every ASCII locale shipped in msg.arc is now in the expectation
         * table, so "it must be a non-English build" is no longer an
         * explanation on its own -- that only still applies to the locales
         * omitted for being non-ASCII (jp/kr/zh/ru/ar/es/pt). */
        log_line("gmdfind: label verified but text did not resolve. The blob "
                 "was tried at %d known deltas and then searched on 64 KB "
                 "steps to +0x400000, so a MOVED blob is already ruled out; "
                 "the likely cause is a non-ASCII locale "
                 "(jp/kr/zh/ru/ar/es/pt, not in the table). Sample read: "
                 "\"%s\"",
                 NSTRING_HINTS,
                 out->sample_text[0] ? out->sample_text : "(unreadable)");
    }

    return out->label_verified;
}

/* --- Reading a message by index ------------------------------------------
 *
 * See gmd_find.h. sectionId == array index, and the display string is the
 * sectionId'th string in the trailing blob, so this is a walk of N strings.
 *
 * ⚠⚠ The blob offset MOVES between runs (0xd0000 / 0x140000 / 0x170000 all
 * observed on the same build). It is resolved once here and cached for the
 * process, never compiled in -- a hardcoded offset already caused this project
 * to read garbage and report a good table as unverifiable.
 */
static BYTE *g_text_blob;          /* resolved base of the string blob */
static int   g_text_count;

/* What language does the game's own config.ini say it is running in?
 *
 * \u26a0 config.ini is written by the game on exit and read on start, so it is
 * authoritative for the NEXT run rather than necessarily for this instant. It
 * is still the best cheap answer: a mismatch against it is a strong signal that
 * the table we resolved is stale.
 *
 * Returns a static short code, or NULL if the file has no Language key (the
 * default state -- the game does not write one itself).
 */
/* Defined below, beside the index it frees. */
static void text_index_free(void);

/* ⚠ Set by gmd_text_recheck_language() to force a fresh read of config.ini.
 *
 * ⛔ The cache used to be permanent (`static int tried`), which meant a language
 * changed in Options > System was never noticed: the mod kept serving the blob
 * it resolved at startup for the rest of the session, with every structural
 * check passing. That is the same failure mode as the stale-blob bug this file
 * already documents, just triggered later. */
static int g_lang_recheck;

/* ⭐ Test seam: stands in for config.ini, which does not exist beside the test
 * exe.
 *
 * ⛔⛔ IT EXISTS BECAUSE A CASE PASSED FOR THE WRONG REASON. Case 6b claimed to
 * prove "an unidentifiable blob is left alone", and it passed with that guard
 * SABOTAGED -- because with no config.ini the function returned at the later
 * `!want` check before the sabotage could matter. A case that cannot reach the
 * code it names is testing something else. */
static const char *g_lang_expected_override;

void gmd_test_set_expected_language(const char *code)
{
    g_lang_expected_override = code;
}

/* ⭐⭐ THE GAME'S OWN ANSWER: which language the message system draws from.
 *
 * ⛔ USER-REPORTED (2026-10-04): a player heard the main menu in "incredibly
 * broken English". config.ini has no Language key unless the player put one
 * there, and without it the resolver fell back to the FIRST blob it recognised
 * -- a stale table is resident beside the live one, so that can be any
 * language, read aloud by an English voice.
 *
 * So read the value the game itself uses, traced offline from .text:
 *
 *     app = *(void **)0x141c14f90     ; written only by the app constructor
 *                                     ; 0x140047e5a, cleared at 0x140048635
 *     lang = *(int *)(app + 0x40)     ; 0..13, the order of the code list
 *                                     ; "jpn eng fre ..." at 0x140fe1954
 *
 * - The constructor (0x140047ed3) seeds +0x3c/+0x40 from Steam's
 *   GetCurrentGameLanguage via a jump table whose targets are exactly this
 *   0..13 order (latam -> 6 oth, brazilian -> 9 por, unsupported -> 1 eng).
 * - 0x14005a715 selects the message table with [msg + lang*8 + 0x88], lang
 *   read from app+0x40 -- the selection itself, not a correlate of it.
 * - Shell menu code compares app+0x40 against 0xd (Arabic, the RTL language)
 *   at a dozen sites, which is what a display language looks like.
 *
 * ⚠ Out of range or unreadable returns NULL, never a default: the caller then
 * falls back to config.ini, and failing that says it has nothing to check. */
#define APP_SLOT_LIVE  0x141c14f90ULL
#define APP_LANG_OFF   0x40

static const char *const g_lang_by_game_index[] = {
    "jpn", "eng", "fre", "ita", "ger", "spa", "oth",
    "kor", "pol", "por", "rus", "chT", "chS", "ara",
};
#define NLANG_GAME_INDEX \
    (int)(sizeof(g_lang_by_game_index) / sizeof(g_lang_by_game_index[0]))

static void *const *g_app_slot = (void *const *)APP_SLOT_LIVE;

/* Where the last expected language came from, for the log. */
static const char *g_lang_source = "config.ini";

void gmd_test_set_app_slot(void *const *slot)
{
    g_app_slot = slot ? slot : (void *const *)APP_SLOT_LIVE;
}

static const char *gmd_live_language(void)
{
    void *app = NULL;
    int   idx = -1;

    if (!mem_copy(&app, g_app_slot, sizeof(app)) || !app)
        return NULL;
    if (!mem_copy(&idx, (const BYTE *)app + APP_LANG_OFF, sizeof(idx)))
        return NULL;
    if (idx < 0 || idx >= NLANG_GAME_INDEX)
        return NULL;
    return g_lang_by_game_index[idx];
}

static const char *gmd_expected_language(void)
{
    const char *live = gmd_live_language();
    if (live) {
        g_lang_source = "the game's language setting";
        return live;
    }
    g_lang_source = "config.ini";

    if (g_lang_expected_override)
        return g_lang_expected_override;

    static char code[8];
    static int  tried;
    if (tried && !g_lang_recheck)
        return code[0] ? code : NULL;
    tried = 1;
    g_lang_recheck = 0;
    code[0] = '\0';

    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return NULL;
    char *slash = strrchr(path, '\\');
    if (!slash)
        return NULL;
    slash[1] = '\0';
    strncat(path, "config.ini", MAX_PATH - strlen(path) - 1);

    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (_strnicmp(p, "Language", 8) != 0) continue;
        p += 8;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        int k = 0;
        while (k < (int)sizeof(code) - 1 &&
               *p > ' ' && *p != '\r' && *p != '\n')
            code[k++] = *p++;
        code[k] = '\0';
        break;
    }
    fclose(f);
    return code[0] ? code : NULL;
}

/* Worker-only discovery. The synchronous gmd_find above is retained for
 * offline diagnostics; it must never be called by the output worker.
 * Per poll: at most 32 64-KB copies and eight address-space queries, or one
 * candidate/blob check. A 2-ms elapsed budget also yields between chunks
 * (not a hard real-time guarantee for an OS call).
 *
 * ⛔ The cap was 4 chunks (2026-10-08), well under what the 2-ms budget
 * allows: the main menu then spoke positions only, without names, for about
 * 68 s after start-up (user-reported 2026-10-13; September's synchronous scan
 * resolved in ~3 s). 32 chunks measured 0.86 ms per poll in
 * run_gmd_discovery_test. The time budget, not the chunk cap, is what keeps
 * speech responsive. */
#define DISCOVERY_CHUNKS 32
#define DISCOVERY_QUERIES 8
typedef struct {
    BOOL initialized;
    uintptr_t cursor, limit, minimum, self_lo, self_hi;
    uintptr_t region, region_end, allocation;
    DWORD protection;
    region_hits hits;
    BYTE *candidate;
    int hint;
    SIZE_T first_delta;
    char expected[8];
    ULONGLONG retry_at;
    ULONGLONG started;        /* diagnostics: time and bytes to resolution */
    unsigned long long scanned;
} text_discovery;
static text_discovery g_discovery;

static const gmd_probe *discovery_label(void)
{
    for (int i = 0; i < GMD_NPROBES; ++i)
        if (g_gmd_probes[i].index == 0) return &g_gmd_probes[i];
    return NULL;
}

static BOOL discovery_label_valid(BYTE *base)
{
    char label[128];
    const gmd_probe *probe = discovery_label();
    return probe && verify_label(base, probe, GMD_FORM_LIVE_TABLE,
                                 label, sizeof(label));
}

static void discovery_clear_region(void)
{
    g_discovery.region = g_discovery.region_end = g_discovery.allocation = 0;
    memset(&g_discovery.hits, 0, sizeof(g_discovery.hits));
}

static void discovery_init(void)
{
    SYSTEM_INFO si;
    HMODULE self = NULL;
    MODULEINFO mi;
    GetSystemInfo(&si);
    g_discovery.minimum = (uintptr_t)si.lpMinimumApplicationAddress;
    g_discovery.cursor = g_discovery.minimum;
    g_discovery.limit = (uintptr_t)si.lpMaximumApplicationAddress;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)(void *)&g_gmd_probes, &self) &&
        GetModuleInformation(GetCurrentProcess(), self, &mi, sizeof(mi))) {
        g_discovery.self_lo = (uintptr_t)mi.lpBaseOfDll;
        g_discovery.self_hi = g_discovery.self_lo + mi.SizeOfImage;
    }
    filter_init();
    crc_init();
    g_discovery.started = GetTickCount64();
    g_discovery.initialized = TRUE;
}

/* Copy into our own buffer only after a fresh VirtualQuery. No native
 * allocation metadata is trusted across polls. The fault guard covers a
 * concurrent unmap after that query, as in the existing scanner. */
static BOOL discovery_copy(BYTE *dst, uintptr_t at, SIZE_T size)
{
    volatile BOOL fault;
    MEM_GUARDED(fault, memcpy(dst, (const void *)at, size));
    return !fault;
}

static void discovery_scan_step(void)
{
    static BYTE chunk[SCAN_CHUNK];
    text_discovery *d = &g_discovery;
    LARGE_INTEGER started, now, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    unsigned chunks = 0;
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t stack_lo = (uintptr_t)tib->StackLimit;
    uintptr_t stack_hi = (uintptr_t)tib->StackBase;
    MEMORY_BASIC_INFORMATION stack;
    if (VirtualQuery((void *)stack_lo, &stack, sizeof(stack)))
        stack_lo = (uintptr_t)stack.AllocationBase;

    for (unsigned q = 0; q < DISCOVERY_QUERIES && chunks < DISCOVERY_CHUNKS; ++q) {
        QueryPerformanceCounter(&now);
        if (q && now.QuadPart - started.QuadPart >= frequency.QuadPart / 500) return;
        if (d->cursor >= d->limit) {
            d->cursor = d->minimum;
            discovery_clear_region();
            d->retry_at = GetTickCount64() + 1000;
            log_line("gmdtext: discovery pass ended without a resolved table; retry in 1 s");
            return;
        }
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)d->cursor, &m, sizeof(m))) {
            uintptr_t advance = d->limit - d->cursor;
            if (advance > 0x10000) advance = 0x10000;
            d->cursor += advance;
            discovery_clear_region();
            continue;
        }
        uintptr_t base = (uintptr_t)m.BaseAddress;
        uintptr_t end = base + m.RegionSize;
        if (end <= d->cursor) { d->cursor = d->limit; continue; }
        if (end > d->limit) end = d->limit;
        DWORD access = m.Protect & 0xff;
        BOOL usable = m.State == MEM_COMMIT && m.Type == MEM_PRIVATE &&
            !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (access == PAGE_READONLY || access == PAGE_READWRITE ||
             access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
             access == PAGE_EXECUTE_READWRITE);
        if (!usable || (base < d->self_hi && end > d->self_lo) ||
            (base < stack_hi && end > stack_lo)) {
            d->cursor = end;
            discovery_clear_region();
            continue;
        }
        /* VirtualQuery at an interior address describes the suffix starting
         * at that page, not necessarily the original region base. Keep the
         * accumulated hits while this is still the same readable suffix. */
        if (!d->region || base < d->region || base > d->cursor || d->region_end != end ||
            d->allocation != (uintptr_t)m.AllocationBase || d->protection != m.Protect) {
            discovery_clear_region();
            d->region = base;
            d->region_end = end;
            d->allocation = (uintptr_t)m.AllocationBase;
            d->protection = m.Protect;
        }
        SIZE_T count = end - d->cursor;
        if (count > sizeof(chunk)) count = sizeof(chunk);
        if (count >= ENT_SIZE) {
            ++chunks;
            d->scanned += count;
            if (discovery_copy(chunk, d->cursor, count))
                scan_chunk(chunk, count, (BYTE *)d->cursor, &d->hits);
            else memset(&d->hits, 0, sizeof(d->hits));
        }
        if (end - d->cursor > sizeof(chunk)) {
            d->cursor += sizeof(chunk) - ENT_SIZE; /* boundary overlap */
        } else {
            BYTE *array = NULL;
            unsigned stride, pad = 0;
            int agree, pairs;
            d->cursor = end;
            if (d->hits.found >= 2)
                check_geometry(&d->hits, &array, &stride, &agree, &pairs);
            if (array &&
                classify(array, &pad) == GMD_FORM_LIVE_TABLE &&
                discovery_label_valid(array)) {
                d->candidate = array;
                d->hint = 0;
                d->first_delta = 0;
                d->expected[0] = '\0';
            }
            discovery_clear_region();
            return; /* candidate processing has its own subsequent poll */
        }
    }
}

static const char *discovery_blob_language(BYTE *blob)
{
    /* The existing language reader validates shipped index-0 strings and
     * the Spain/Mexico tie-break. Never leave a trial blob publicly readable. */
    g_text_blob = blob;
    g_text_count = GMD_LABEL_COUNT;
    const char *lang = gmd_text_language();
    g_text_blob = NULL;
    g_text_count = 0;
    return lang;
}

BOOL gmd_text_ready(void)
{
    text_discovery *d = &g_discovery;
    if (g_text_blob) return TRUE;
    if (!mem_guard_install()) return FALSE;
    if (!d->initialized) discovery_init();
    if (GetTickCount64() < d->retry_at) return FALSE;
    if (!d->candidate) { discovery_scan_step(); return FALSE; }
    if (!discovery_label_valid(d->candidate)) {
        d->candidate = NULL;
        return FALSE;
    }
    const char *want = gmd_expected_language();
    if (strcmp(d->expected, want ? want : "") != 0) {
        _snprintf(d->expected, sizeof(d->expected), "%s", want ? want : "");
        d->hint = 0;
        d->first_delta = 0;
    }
    int limit = NSTRING_HINTS + (want ? 64 : 0);
    SIZE_T selected = 0;
    if (d->hint < limit) {
        SIZE_T delta = d->hint < NSTRING_HINTS ? g_strings_hints[d->hint] :
            (SIZE_T)(d->hint - NSTRING_HINTS + 1) * 0x10000;
        ++d->hint;
        const char *lang = discovery_blob_language(d->candidate + delta);
        if (lang) {
            if (!d->first_delta) d->first_delta = delta;
            if (want && strcmp(want, lang) == 0) selected = delta;
        }
        if (!selected) return FALSE;
    } else {
        /* Preserve the existing unknown/missing-language fallback, but
         * revalidate its content: it may have changed between polls. */
        if (d->first_delta && discovery_blob_language(d->candidate + d->first_delta))
            selected = d->first_delta;
        else { d->candidate = NULL; return FALSE; }
        log_line("gmdtext: no matching expected-language blob; using a verified fallback");
    }
    g_text_blob = d->candidate + selected;
    g_text_count = GMD_LABEL_COUNT;
    text_index_free();
    d->candidate = NULL;
    log_line("gmdtext: blob resolved incrementally at delta 0x%llx, language %s, "
             "%.1f s after discovery began, %llu MB scanned",
             (unsigned long long)selected, gmd_text_language(),
             (double)(GetTickCount64() - d->started) / 1000.0, d->scanned >> 20);
    return TRUE;
}


/* ⭐⭐ FOLLOW A LANGUAGE CHANGED IN OPTIONS > SYSTEM.
 *
 * ⛔ USER: "Ensure that it will pick the correct tables for all of the
 * languages when its language is selected from settings." It did not. The blob
 * was resolved once and `gmd_text_ready()` returns early forever after, so
 * switching language mid-session left every item name coming from the OLD
 * language -- and nothing looked wrong: the label hashes are
 * language-independent, so every structural check still passed. That is the
 * stale-blob bug this file already documents, arriving by a different route.
 *
 * ⭐ THE CHECK IS THE ONE ALREADY TRUSTED AT STARTUP, just repeated: ask the
 * resolved blob which language it is and compare against config.ini, which the
 * game writes when the setting changes. Agreement costs one indexed read of
 * string 0; only a DISAGREEMENT pays for a re-resolve.
 *
 * ⚠ Returns TRUE only when it actually re-resolved, so a caller can log the
 * event rather than guess. A read that cannot decide (either side NULL) changes
 * NOTHING and returns FALSE -- "unknown" must never be treated as "changed",
 * or a transient unreadable blob would drop a working table.
 */
BOOL gmd_text_recheck_language(void)
{
    const char *have, *want;

    if (!g_text_blob)
        return FALSE;                  /* nothing resolved yet: not our job */

    have = gmd_text_language();
    if (!have)
        return FALSE;                  /* cannot tell: change nothing */

    g_lang_recheck = 1;                /* force a fresh config.ini read */
    want = gmd_expected_language();
    if (!want || strcmp(want, have) == 0)
        return FALSE;                  /* agrees, or nothing to check against */

    log_line("gmdtext: ⚠ LANGUAGE CHANGED -- the resolved blob is \"%s\" but "
             "%s now says \"%s\". Re-resolving.", have, g_lang_source, want);

    memset(&g_discovery, 0, sizeof(g_discovery));
    /* Drop the resolution and let gmd_text_ready() redo the full per-language
     * search. ⚠ The index must go too: it maps indices into the OLD blob. */
    text_index_free();
    g_text_blob = NULL;
    g_text_count = 0;

    if (gmd_text_ready()) {
        const char *now = gmd_text_language();
        log_line("gmdtext: ⭐ re-resolved; the table is now \"%s\".",
                 now ? now : "(unreadable)");
        return TRUE;
    }
    log_line("gmdtext: incremental re-resolution pending; no old-language table is served");
    return TRUE;
}

int gmd_text_count(void) { return g_text_blob ? g_text_count : 0; }

/* ⛔⛔ WHY THIS IS INDEXED RATHER THAN WALKED EACH TIME.
 *
 * The first version called skip_strings() from the blob base on every lookup:
 * one GUARDED single-byte read per character, so index 250 cost thousands of
 * them. Measured in-game: FOUR lookups took ELEVEN SECONDS, on the poll thread,
 * which starved the whole mod -- a run that should have logged dozens of menu
 * movements recorded exactly one.
 *
 * ⭐ The lesson is the project's own: a per-byte guarded read is fine once and
 * ruinous in a loop. Build the offsets ONCE, in bulk, then every lookup is a
 * single bounded read.
 *
 * The index is built with big block reads instead of byte reads, so the whole
 * 4165-entry table costs a few hundred guarded copies rather than ~1.5 million.
 */
#define GMD_BLOCK 0x1000

static unsigned *g_text_off;      /* byte offset of each string from the blob */
/* The blob's extent, recorded while text_index_build() walked it.
 *
 * ⛔⛔ DIAGNOSTIC ONLY -- DO NOT USE THIS TO SKIP mem_readable_run().
 * That was tried: a "the range is already validated" fast path around the
 * guarded memcpy in gmd_text_by_index(), and it CRASHED the process. A guarded
 * memcpy over a bad page is not reliably caught (tools/test_span_safety.c
 * pins this: a byte read and a byte loop ARE caught, memcpy is not). The fault
 * guard is a net for the race between the check and the copy -- never a
 * replacement for the check. */
static const BYTE *g_text_span_lo;
static const BYTE *g_text_span_hi;
/* ⭐⭐ Set only when the WHOLE span has been proven readable in one go, which
 * is what licenses gmd_text_by_index() to skip the per-string VirtualQuery.
 * ⚠ Without this the span bounds alone would be a claim, not a check -- and
 * the log line below already ASSERTED that reads skipped VirtualQuery while
 * they still called it on every string. A message is not a mechanism. */
static BOOL g_text_span_ok;
static int       g_text_indexed;

static void text_index_free(void)
{
    if (g_text_off) {
        HeapFree(GetProcessHeap(), 0, g_text_off);
        g_text_off = NULL;
    }
    /* ⚠ The span must die with the index. A stale span would let reads skip
     * validation for a blob that has moved -- the one way this optimisation
     * could become unsafe. */
    g_text_span_lo = NULL;
    g_text_span_hi = NULL;
    g_text_span_ok = FALSE;
    g_text_indexed = 0;
}

/* Walk the blob once in blocks, recording where each string starts. */
static BOOL text_index_build(void)
{
    if (g_text_indexed)
        return TRUE;
    if (!g_text_blob || g_text_count <= 0)
        return FALSE;

    g_text_off = (unsigned *)HeapAlloc(GetProcessHeap(), 0,
                                       sizeof(unsigned) * (SIZE_T)g_text_count);
    if (!g_text_off)
        return FALSE;

    BYTE  block[GMD_BLOCK];
    SIZE_T pos = 0;            /* byte offset within the blob */
    int    found = 0;
    g_text_off[found++] = 0;   /* string 0 starts at the blob base */

    /* ⚠ A hard ceiling: a corrupt blob must not spin forever. 4165 strings of
     * plausible length fit far inside this. If the ceiling is hit the index is
     * abandoned rather than truncated silently -- a partial index would return
     * the WRONG string for high indices, which is worse than none. */
    const SIZE_T LIMIT = 4u * 1024u * 1024u;

    while (found < g_text_count && pos < LIMIT) {
        SIZE_T want = GMD_BLOCK;
        if (!mem_copy(block, g_text_blob + pos, want)) {
            /* Near the end of a region a full block may not be readable; try
             * a smaller tail before giving up. */
            want = 0;
            for (SIZE_T t = GMD_BLOCK / 2; t >= 64; t /= 2) {
                if (mem_copy(block, g_text_blob + pos, t)) { want = t; break; }
            }
            if (!want) {
                text_index_free();
                return FALSE;
            }
        }
        for (SIZE_T i = 0; i < want && found < g_text_count; i++) {
            if (block[i] == 0)
                g_text_off[found++] = (unsigned)(pos + i + 1);
        }
        pos += want;
    }

    if (found < g_text_count) {
        log_line("gmdtext: ⛔ only %d of %d string offsets were found before "
                 "the %llu-byte ceiling -- abandoning the index rather than "
                 "serving wrong text for high indices.",
                 found, g_text_count, (unsigned long long)LIMIT);
        text_index_free();
        return FALSE;
    }

    g_text_indexed = 1;

    /* ⭐⭐ CACHE THE BLOB'S VALIDATED EXTENT, so per-string reads stop calling
     * VirtualQuery at all.
     *
     * ⛔ THE BUG THIS FIXES, from a live run: classifying all 4165 strings took
     * ELEVEN AND A HALF SECONDS in-game (2026-09-09 23:52) while the same loop
     * measured 1.46 ms on a test blob. ~2.9 ms per read, ~7000x the bench.
     *
     * The cause is not the copy -- it is mem_readable_run()'s VirtualQuery on
     * EVERY read. That call walks the process's VAD tree, and this process has
     * **2336 regions over 1.6 GB** (the log says so). My microbenchmark ran in
     * a small test process with one 1 MB allocation, where the same call is
     * essentially free. Cost that depends on the whole address space cannot be
     * measured in a process that does not have one.
     *
     * ⭐ But the loop above has ALREADY walked the entire blob under the fault
     * guard, block by block, and knows exactly how far it reads cleanly. That
     * is the same validation mem_readable_run performs -- so record the span
     * once and let reads inside it skip the query.
     *
     * ⚠ This does NOT weaken the guard. Reads are still bounded to the span and
     * still run under MEM_GUARDED, so a page decommitted afterwards faults
     * safely instead of crashing. What is dropped is only the RE-ASKING of a
     * question already answered for this exact range. */
    g_text_span_lo  = g_text_blob;
    g_text_span_hi  = g_text_blob + pos;

    /* ⭐⭐⭐ VALIDATE THE WHOLE SPAN ONCE -- this is what pays for skipping the
     * per-string query. mem_readable_run() loops until it has accumulated the
     * requested run, so one call here covers every string, and it either proves
     * the entire extent readable or the fast path stays off.
     *
     * ⚠ An "ok" that is merely assumed would reintroduce the reverted crash, so
     * this is a MEASUREMENT, and the log reports which path is live. */
    {
        SIZE_T ext = (SIZE_T)(g_text_span_hi - g_text_span_lo);
        SIZE_T got = mem_readable_run(g_text_span_lo, ext);
        g_text_span_ok = (got >= ext);
        if (!g_text_span_ok)
            log_line("gmdtext: ⚠ only %llu of %llu span byte(s) are readable in "
                     "one run -- per-string reads keep the VirtualQuery. Slower, "
                     "and correct.",
                     (unsigned long long)got, (unsigned long long)ext);
    }

    /* ⭐ Report the blob's REGION SHAPE, because that is the remaining
     * hypothesis for why per-string reads cost ~2.9 ms in-game while every
     * offline benchmark says microseconds.
     *
     * mem_readable_run(src, 255) LOOPS until it has accumulated 255 readable
     * bytes. If VirtualQuery here returns a small RegionSize, that is many
     * queries per string rather than one -- a cost that depends on region SIZE,
     * which a benchmark counting regions would never show. This prints the
     * actual size so the next run answers it instead of me guessing a third
     * time. */
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(g_text_blob, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            log_line("gmdtext: blob region: base=%p size=%llu KB state=0x%lx "
                     "protect=0x%lx type=0x%lx",
                     mbi.BaseAddress,
                     (unsigned long long)(mbi.RegionSize / 1024),
                     (unsigned long)mbi.State, (unsigned long)mbi.Protect,
                     (unsigned long)mbi.Type);
        }
    }
    log_line("gmdtext: ⭐ indexed %d string offsets in one pass; blob spans "
             "%p..%p (%llu bytes). Per-string VirtualQuery: %s.",
             g_text_count, (void *)g_text_span_lo, (void *)g_text_span_hi,
             (unsigned long long)pos,
             g_text_span_ok ? "SKIPPED (span validated in one run; reads are a "
                              "guarded scalar loop)"
                            : "STILL CALLED (span not validated)");
    return TRUE;
}

BOOL gmd_text_by_index(int index, char *out, unsigned out_cap)
{
    if (!out || out_cap == 0)
        return FALSE;
    out[0] = '\0';
    if (!g_text_blob || index < 0 || index >= g_text_count)
        return FALSE;
    if (!g_text_indexed && !text_index_build())
        return FALSE;

    const BYTE *src = g_text_blob + g_text_off[index];
    char buf[256];

    /* ⭐ Inside the validated span, copy directly: the extent was checked once
     * when the index was built, and VirtualQuery here costs ~2.9 ms in this
     * process (2336 regions / 1.6 GB) -- 11.5 s to read all 4165 strings.
     *
     * ⚠ Still under the fault guard and still bounded to the span, so a page
     * decommitted since indexing faults safely rather than crashing. Only the
     * redundant re-query is skipped, never the protection. */
    /* ⛔⛔ REVERTED: an "already validated span" fast path that skipped
     * mem_readable_run() before the copy. It looked safe -- the extent HAD been
     * walked under the guard while indexing -- and it CRASHED the process.
     *
     * ⭐⭐ THE REASON MATTERS FOR EVERY GUARDED READ IN THIS PROJECT.
     * tools/test_span_safety.c reproduces it in twenty lines:
     *
     *     A. a guarded single-byte read of a PAGE_NOACCESS page -> caught.
     *     B. a guarded byte LOOP across the boundary            -> caught.
     *     C. a guarded memcpy() across the boundary             -> ⛔ CRASH.
     *
     * MinGW's optimised memcpy uses vector instructions, and a fault inside one
     * is not recoverable through this VEH + longjmp guard the way a scalar
     * access is.
     *
     * ⚠ So THE FAULT GUARD IS A NET FOR A RACE, NOT THE PRIMARY CHECK. Every
     * other guarded memcpy in this codebase (mem_copy, read_cstr, pad_watch)
     * calls mem_readable_run FIRST and only then copies; the guard exists for
     * the window between the query and the copy. My fast path deleted the
     * primary check and leaned on a net that does not hold for memcpy.
     *
     * The stall it was trying to fix is real, but it must be fixed WITHOUT
     * dropping the pre-check -- the resumable, budgeted classification in
     * menu_speak.c is what bounds it now. */
    /* ⭐⭐⭐ THE FAST PATH, DONE THE WAY CASE B ALLOWS -- a SCALAR BYTE LOOP.
     *
     * ⛔⛔ USER-REPORTED: "there's still about a second of lag per keypress",
     * AFTER the queue fix. A constant per-press cost, so a slow STEP, and this
     * is it: read_cstr() calls mem_readable_run() -- a VirtualQuery -- on every
     * string, measured at ~2.9 ms in this process (2336 regions / 1.6 GB). One
     * announcement reads a NAME and a CAPTION and the prompt table walks more,
     * so the queries stack up into most of a second.
     *
     * ⭐ WHY THIS IS SAFE WHERE THE REVERTED VERSION WAS NOT. That one skipped
     * the query AND used memcpy, and tools/test_span_safety.c shows exactly why
     * it crashed: a fault inside MinGW's VECTORISED memcpy is not recoverable
     * through the VEH+longjmp guard (case C), while a guarded SCALAR byte loop
     * IS (case B). So the fix is not "skip the check" -- it is "skip the check
     * and read the way the guard can actually protect".
     *
     * ⚠ THREE conditions, all required:
     *   1. the whole read lies inside the extent validated at index time;
     *   2. the copy is a byte loop, never memcpy or anything it can be
     *      optimised into (the pointers are volatile to keep it scalar);
     *   3. it still runs under the fault guard, so a page decommitted since
     *      indexing degrades to a refusal rather than a crash.
     * Miss any one and this becomes the bug that was reverted. */
    if (g_text_span_ok &&
        src >= g_text_span_lo && src < g_text_span_hi) {
        BOOL faulted = FALSE;
        unsigned n = 0;
        /* volatile: forbids the compiler from turning this back into a
         * vectorised copy, which is the exact hazard case C documents. */
        volatile const BYTE *p = src;
        const BYTE *limit = g_text_span_hi;
        MEM_GUARDED(faulted, {
            while (n < sizeof(buf) - 1 && (const BYTE *)p + n < limit) {
                buf[n] = (char)p[n];
                if (buf[n] == '\0')
                    break;
                n++;
            }
        });
        if (faulted)
            return FALSE;
        buf[n < sizeof(buf) ? n : sizeof(buf) - 1] = '\0';
        _snprintf(out, out_cap, "%s", buf);
        out[out_cap - 1] = '\0';
        return TRUE;
    }

    if (!read_cstr(src, buf, sizeof(buf)))
        return FALSE;
    _snprintf(out, out_cap, "%s", buf);
    out[out_cap - 1] = '\0';
    return TRUE;
}

/* --- test seams ---------------------------------------------------------- */

void gmd_test_set_blob(BYTE *blob, int count)
{
    text_index_free();
    g_text_blob  = blob;
    g_text_count = count;
}

int gmd_test_label_count(void) { return GMD_LABEL_COUNT; }

/* --- Which language is the resolved table? -------------------------------
 *
 * See gmd_find.h. Index 0 is LANG_NAME_JAPANESE, and every shipped table
 * renders it in its OWN language, uniquely. Matching it identifies the table
 * without any live-language lookup.
 *
 * A table whose index 0 matches none of them returns NULL, which the caller
 * must treat as "unknown", never as "fine" -- an unrecognised table is exactly
 * the situation that produced a confident wrong language in the first place.
 *
 * ⭐⭐ ALL FOURTEEN ARE LISTED (2026-09-11). Previously only the five
 * "ASCII-renderable" locales were, so gmd_text_language() returned NULL for
 * Japanese, Spanish x2, Korean, Portuguese, Russian, Chinese x2 and Arabic --
 * nine of fourteen. The consequence was not cosmetic: the blob resolver matches
 * the table's language against config.ini, so an unidentifiable table cannot be
 * SELECTED, and the fallback path logs "item names will be in the WRONG
 * LANGUAGE" and serves whichever blob it happened to recognise.
 *
 * ⚠ The strings are stored as UTF-8 BYTES, compared with strcmp against bytes
 * read from the blob, so the renderability of the terminal is irrelevant --
 * that was the unexamined premise behind "ASCII-renderable only". Lengths are
 * 6-23 bytes, well inside read_index0()'s 128-byte buffer.
 *
 * ⛔⛔ SPANISH COLLIDES WITH ITSELF: menu_spa and menu_oth BOTH render index 0
 * as "Japon\xc3\xa9s [jp]". menu_oth is Spanish (Mexico) -- tools/extract_gmd.py
 * maps it to spanish_mx, and LANG_NAME_SPANISH2 ("Spanish (Mexico) [es-MX]")
 * is one of the fourteen the Language row offers, so a player can select it.
 * Index 0 alone cannot tell them apart; lang_tiebreak_spa_oth() does, below.
 */
static const struct { const char *code; const char *idx0; } g_lang_by_idx0[] = {
    { "jpn", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e [jp]" },
    { "eng", "Japanese [jp]"    },
    { "fre", "Japonais [jp]"    },
    { "ita", "Giapponese [jp]"  },
    { "ger", "Japanisch [jp]"   },
    /* ⚠ spa and oth share this string -- resolved by the tiebreaker, not here.
     * Listed as "spa" so an unrecognised-language refusal never fires for a
     * Spanish table; the tiebreaker then decides WHICH Spanish. */
    { "spa", "Japon\xc3\xa9s [jp]" },
    { "kor", "\xec\x9d\xbc\xeb\xb3\xb8\xec\x96\xb4 [jp]" },
    { "pol", "Japo\xc5\x84ski [jp]" },
    { "por", "Japon\xc3\xaas [jp]" },
    { "rus", "\xd0\xaf\xd0\xbf\xd0\xbe\xd0\xbd\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9 [jp]" },
    { "chT", "\xe6\x97\xa5\xe6\x96\x87[jp]" },
    { "chS", "\xe6\x97\xa5\xe8\xaf\xad" },
    { "ara", "\xd8\xa7\xd9\x84\xd9\x8a\xd8\xa7\xd8\xa8\xd8\xa7\xd9\x86\xd9\x8a\xd8\xa9 [jp]" },
};
#define NLANG_BY_IDX0 (int)(sizeof(g_lang_by_idx0) / sizeof(g_lang_by_idx0[0]))

/* ⭐⭐ SPAIN vs MEXICO, decided at index 29 (SYS_WINDOW_YES2).
 *
 * The two Spanish tables differ at 813 of 4165 labels; index 29 is the
 * earliest, and it is a short string:
 *
 *     menu_spa  "Aceptar"
 *     menu_oth  "S\xc3\xad, acepto"
 *
 * ⚠ WHY A WALK AND NOT AN INDEX BUILD. read_index0() deliberately avoids
 * building the offset index because doing it per candidate cost TWELVE SECONDS
 * in a live session -- the probe loop tries a blob, asks its language, and
 * moves on. skip_strings() walks 29 short NUL-terminated strings under the
 * fault guard instead, which is bounded and cheap. Do not "improve" this into
 * a gmd_text_by_index() call: that rebuilds the whole index.
 *
 * Returns "spa", "oth", or NULL when the string reads as neither -- and NULL
 * must stay a refusal, not a default to "spa". Guessing which Spanish is the
 * same class of error as guessing the language.
 */
static const char *lang_tiebreak_spa_oth(void)
{
    const BYTE *at;
    char s[64];

    if (!g_text_blob)
        return NULL;
    at = skip_strings(g_text_blob, 29);
    if (!at || !read_cstr(at, s, sizeof(s)) || !s[0])
        return NULL;
    if (strcmp(s, "Aceptar") == 0)
        return "spa";
    if (strcmp(s, "S\xc3\xad, acepto") == 0)
        return "oth";
    return NULL;
}

/* Read index 0 WITHOUT building the offset index.
 *
 * ⛔⛔ Building it here cost 25 full rebuilds in one session -- the probing
 * loop tries a blob, asks its language, and moves on, so a 4 MB indexing pass
 * per candidate is pure waste. It made a discovery block take TWELVE SECONDS
 * and the mod visibly stopped keeping up.
 *
 * Index 0 is the first string in the blob, so it needs no index at all: it
 * starts at the blob base. */
static BOOL read_index0(char *out, unsigned cap)
{
    if (!g_text_blob || !out || cap == 0)
        return FALSE;
    out[0] = '\0';
    return read_cstr(g_text_blob, out, cap);
}

const char *gmd_text_language(void)
{
    char idx0[128];
    if (!read_index0(idx0, sizeof(idx0)) || !idx0[0])
        return NULL;
    for (int i = 0; i < NLANG_BY_IDX0; i++) {
        if (strcmp(idx0, g_lang_by_idx0[i].idx0) != 0)
            continue;
        /* ⭐⭐ TWO tables share the Spanish index-0 string, so a match on it is
         * not yet an identification. Ask index 29 which Spanish this is.
         *
         * ⛔ A tiebreaker that cannot read returns NULL and that NULL is
         * PROPAGATED, not softened into "spa". "Probably Spain" is the same
         * shape of guess as "probably French", which is the bug this whole
         * language check exists for: the caller compares the result against
         * config.ini, so a wrong code selects a wrong blob CONFIDENTLY, while
         * NULL merely means "unknown" and refuses. */
        if (strcmp(g_lang_by_idx0[i].code, "spa") == 0)
            return lang_tiebreak_spa_oth();
        return g_lang_by_idx0[i].code;
    }
    return NULL;
}

void gmd_text_invalidate(void)
{
    memset(&g_discovery, 0, sizeof(g_discovery));
    text_index_free();
    g_text_blob = NULL;
    g_text_count = 0;
}
