#include "probe_hash.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

/* Fault protection under MinGW.
 *
 * We are calling addresses whose signature is a GUESS, inside a packed
 * process, so a fault is an expected outcome of the experiment rather than a
 * bug -- it must be recorded and survived, not allowed to kill the game.
 * MSVC's __try/__except is unavailable here, so a vectored exception handler
 * plus longjmp gives the equivalent: catch the fault, unwind back to the call
 * site, log it, continue with the next candidate.
 *
 * The handler is deliberately narrow. It only acts while g_guard_armed is set
 * (i.e. inside one of our own probe calls) and only on the fault classes a bad
 * call actually produces; anything else is passed through to the game's own
 * handlers untouched. */
static jmp_buf        g_guard;
static volatile LONG  g_guard_armed;
static void          *g_veh;

static LONG CALLBACK guard_veh(EXCEPTION_POINTERS *ep)
{
    if (!InterlockedCompareExchange(&g_guard_armed, 0, 0))
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_DATATYPE_MISALIGNMENT)
        return EXCEPTION_CONTINUE_SEARCH;

    InterlockedExchange(&g_guard_armed, 0);
    longjmp(g_guard, (int)code);
    return EXCEPTION_CONTINUE_SEARCH;   /* not reached */
}

/* Run one call under the guard. Sets *faulted and leaves *out untouched on a
 * fault. Written as a macro because setjmp's buffer must belong to the frame
 * that is still live when longjmp fires. */
#define GUARDED_CALL(out, faulted, expr)                                      \
    do {                                                                      \
        (faulted) = FALSE;                                                    \
        if (setjmp(g_guard) == 0) {                                           \
            InterlockedExchange(&g_guard_armed, 1);                           \
            (out) = (expr);                                                   \
            InterlockedExchange(&g_guard_armed, 0);                           \
        } else {                                                              \
            (faulted) = TRUE;                                                 \
        }                                                                     \
    } while (0)

/* Candidate CRC-32 sites from tools/trace_crc.py, as FUNCTION ENTRY points
 * (found with tools/disasm.py --entry). These are live addresses and stay
 * valid across runs: ASLR is off and the exe carries no relocations.
 *
 * Note what each one already looks like from disassembly, so a surprising
 * result can be checked against the expectation rather than rationalized:
 *  - 0x1400ff460  CRC + an 8-byte-stride bucket walk that ALLOCATES on miss.
 *                 Reads as a hash-set INSERT, not a message lookup.
 *  - 0x1402ef1e0  a clean standalone crc32(seed, buf, len): eax=0xFFFFFFFF
 *                 init, dword-at-a-time unrolled, byte tail. Most likely the
 *                 reusable primitive the GMD hasher would call.
 *  - 0x1402608c0  larger function, 0x428-byte frame -- probably a formatter
 *                 that hashes something incidentally.
 */
typedef struct {
    const char *note;
    uintptr_t   addr;
} candidate;

static const candidate g_candidates[] = {
    { "hashset-insert?",  0x1400ff460 },
    { "crc32 primitive?", 0x1402ef1e0 },
    { "big-frame fn",     0x1402608c0 },
    { "crc site 17ca29",  0x14017c9e0 },
    { "crc site 29bfec",  0x14029bf90 },
};
#define NCAND (int)(sizeof(g_candidates) / sizeof(g_candidates[0]))

/* Known-good values from the verified offline extraction. */
static const char  K_LABEL[] = "MENU_TITLE_00";
#define K_PLAIN 0xec548ce8u   /* ~crc32(label)     -- the bucket source */
#define K_HASH1 0x68317889u   /* ~crc32(label * 2) */
#define K_RAW   0x13ab7317u   /* zlib crc32(label) -- finalized, not inverted */

/* ⭐ CONFIRMED 2026-09-01 by running it: 0x1402ef1e0 with signature
 *      unsigned crc32(unsigned *seed, const void *buf, unsigned len)
 * and *seed = 0xFFFFFFFF returns exactly ~crc32(label). That is the GMD label
 * hash primitive, established against a value known in advance rather than
 * inferred from the shape of the code.
 *
 * The confirmation is re-run below over several labels, because ONE matching
 * value is a coincidence away from meaningless -- a function returning a
 * constant, or one we happened to feed a lucky input, would pass a single
 * check. Different labels of different lengths must all agree. */
#define CFC2_CRC32 0x1402ef1e0

typedef unsigned (__fastcall *fn_crc32)(unsigned *seed, const void *buf,
                                        unsigned len);

/* label, expected ~crc32(label). Generated from the verified extraction. */
static const struct { const char *label; unsigned expect; } g_vectors[] = {
    { "MENU_TITLE_00",      0xec548ce8u },
    { "STATS_CVS2_39_NAME", 0x6e6b4bdau },
};
#define NVEC (int)(sizeof(g_vectors) / sizeof(g_vectors[0]))

/* The shapes we are willing to try. A wrong guess at the signature is the
 * likeliest reason a correct function fails to match, so try the plausible
 * ones rather than concluding from a single arrangement. */
typedef unsigned (__fastcall *fn_buf_len)(const void *buf, unsigned len);
typedef unsigned (__fastcall *fn_seed_buf_len)(unsigned seed, const void *buf,
                                               unsigned len);
typedef unsigned (__fastcall *fn_pseed_buf_len)(unsigned *seed,
                                                const void *buf, unsigned len);
typedef unsigned (__fastcall *fn_buf_len_seed)(const void *buf, unsigned len,
                                               unsigned seed);

static const char *classify(unsigned got)
{
    if (got == K_PLAIN) return "*** MATCHES ~crc32(label) -- the GMD hash ***";
    if (got == K_HASH1) return "*** MATCHES ~crc32(label*2) -- GMD hash1 ***";
    if (got == K_RAW)   return "matches plain finalized crc32 (not inverted)";
    if (got == ~K_RAW)  return "matches ~crc32 variant";
    return "no match";
}

/* Each call is wrapped: we are invoking an address whose signature is a guess,
 * inside a packed process. A fault here must be a logged data point, not a
 * crash that takes the game down with it. */
static void try_one(const char *note, uintptr_t addr, const char *shape,
                    unsigned got, BOOL faulted)
{
    if (faulted) {
        log_line("hashprobe: 0x%llx [%s] %-16s -> FAULTED",
                 (unsigned long long)addr, note, shape);
        return;
    }
    log_line("hashprobe: 0x%llx [%s] %-16s -> 0x%08x  %s",
             (unsigned long long)addr, note, shape, got, classify(got));
}

int probe_hash_candidates(void)
{
    const unsigned len = (unsigned)strlen(K_LABEL);
    /* volatile: these live across setjmp/longjmp. Without it the compiler may
     * cache them in registers that longjmp restores to stale values, which
     * would silently corrupt the loop counter and the match tally -- the
     * -Wclobbered warning is a real defect here, not noise. */
    volatile int matches = 0;

    log_line("--- hash candidate probe ---");
    log_line("hashprobe: label=\"%s\" len=%u", K_LABEL, len);
    log_line("hashprobe: expecting ~crc32=0x%08x  ~crc32(x2)=0x%08x  "
             "crc32=0x%08x", K_PLAIN, K_HASH1, K_RAW);

    g_veh = AddVectoredExceptionHandler(1, guard_veh);
    if (!g_veh) {
        log_line("hashprobe: ABORTED -- cannot install the fault guard. "
                 "Calling guessed signatures unguarded would crash the game.");
        return 0;
    }

    /* SELF-TEST the guard before trusting it with real calls. A fault handler
     * that silently does not work looks identical to one that was never
     * needed -- right up until it takes the game down. Deliberately fault, and
     * require the guard to catch it; if it does not, we never get here and the
     * missing "guard OK" line is itself the diagnosis. */
    {
        volatile unsigned got = 0;
        volatile BOOL faulted = FALSE;
        /* Held in a volatile pointer so the optimizer cannot see a constant
         * null-ish address and "helpfully" fold the deref into a trap. */
        static volatile unsigned *bad;
        bad = (volatile unsigned *)(uintptr_t)0x10;
        GUARDED_CALL(got, faulted, *bad);
        (void)got;   /* only the fault matters here, not the value */
        if (faulted) {
            log_line("hashprobe: guard self-test OK (deliberate fault caught)");
        } else {
            log_line("hashprobe: ABORTED -- guard self-test did NOT fault; "
                     "the handler is not working, so real calls are unsafe.");
            RemoveVectoredExceptionHandler(g_veh);
            g_veh = NULL;
            return 0;
        }
    }

    for (volatile int i = 0; i < NCAND; i++) {
        uintptr_t a = g_candidates[i].addr;
        const char *note = g_candidates[i].note;
        volatile unsigned got = 0;
        unsigned seed;
        volatile BOOL faulted;

        GUARDED_CALL(got, faulted, ((fn_buf_len)a)(K_LABEL, len));
        try_one(note, a, "(buf,len)", got, faulted);
        if (!faulted && (got == K_PLAIN || got == K_HASH1)) matches++;

        GUARDED_CALL(got, faulted, ((fn_seed_buf_len)a)(0, K_LABEL, len));
        try_one(note, a, "(0,buf,len)", got, faulted);
        if (!faulted && (got == K_PLAIN || got == K_HASH1)) matches++;

        GUARDED_CALL(got, faulted,
                     ((fn_seed_buf_len)a)(0xFFFFFFFFu, K_LABEL, len));
        try_one(note, a, "(~0,buf,len)", got, faulted);
        if (!faulted && (got == K_PLAIN || got == K_HASH1)) matches++;

        /* (unsigned *seed, buf, len) -- the shape 0x1400ff460 hints at, where
         * rcx is a context/table pointer rather than a scalar. */
        seed = 0xFFFFFFFFu;
        GUARDED_CALL(got, faulted,
                     ((fn_pseed_buf_len)a)(&seed, K_LABEL, len));
        try_one(note, a, "(&seed,buf,len)", got, faulted);
        if (!faulted && (got == K_PLAIN || got == K_HASH1)) matches++;

        GUARDED_CALL(got, faulted,
                     ((fn_buf_len_seed)a)(K_LABEL, len, 0xFFFFFFFFu));
        try_one(note, a, "(buf,len,~0)", got, faulted);
        if (!faulted && (got == K_PLAIN || got == K_HASH1)) matches++;
    }

    /* --- Confirm the identified primitive over several vectors ----------
     * One matching value does not establish a function's identity. Require
     * every vector to agree, and require a deliberately WRONG expectation to
     * fail -- otherwise a comparison bug that always reports success would
     * read exactly like a confirmation. */
    {
        volatile int ok = 0, bad = 0;
        for (volatile int v = 0; v < NVEC; v++) {
            volatile unsigned got = 0;
            volatile BOOL faulted;
            unsigned seed = 0xFFFFFFFFu;
            const char *lab = g_vectors[v].label;
            unsigned exp = g_vectors[v].expect;

            GUARDED_CALL(got, faulted,
                         ((fn_crc32)CFC2_CRC32)(&seed, lab,
                                                (unsigned)strlen(lab)));
            if (faulted) {
                log_line("hashprobe: confirm \"%s\" -> FAULTED", lab);
                bad++;
            } else if (got == exp) {
                log_line("hashprobe: confirm \"%s\" -> 0x%08x OK", lab, got);
                ok++;
            } else {
                log_line("hashprobe: confirm \"%s\" -> 0x%08x MISMATCH "
                         "(expected 0x%08x)", lab, got, exp);
                bad++;
            }
        }

        /* Negative control: the same call with a corrupted expectation must
         * NOT be reported as a match. */
        {
            volatile unsigned got = 0;
            volatile BOOL faulted;
            unsigned seed = 0xFFFFFFFFu;
            GUARDED_CALL(got, faulted,
                         ((fn_crc32)CFC2_CRC32)(&seed, "MENU_TITLE_00", 13));
            log_line("hashprobe: negative control -- got 0x%08x, deliberately "
                     "wrong expectation 0xDEADBEEF, equal=%s",
                     got, (!faulted && got == 0xDEADBEEFu) ? "YES (BUG!)"
                                                           : "no (correct)");
        }

        log_line("hashprobe: CONFIRM crc32@0x%llx -- %d/%d vectors OK, %d bad",
                 (unsigned long long)CFC2_CRC32, ok, NVEC, bad);
        if (ok == NVEC && bad == 0)
            log_line("hashprobe: ⭐ GMD label hash primitive CONFIRMED at "
                     "0x%llx  sig: u32(u32 *seed=~0, const void *buf, u32 len)",
                     (unsigned long long)CFC2_CRC32);
    }

    RemoveVectoredExceptionHandler(g_veh);
    g_veh = NULL;

    /* A POSITIVE COUNT of what was examined, not just of what passed:
     * "0 matched" and "nothing ran" must not look the same in the log. */
    log_line("hashprobe: %d candidates x 5 signatures = %d calls, %d matched",
             NCAND, NCAND * 5, matches);
    return matches;
}
