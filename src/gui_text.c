/*
 * cGUIObjText -- capture a live instance and find where its string lives.
 * See gui_text.h for the offline derivation this file rests on.
 */

#include "gui_text.h"
#include "log.h"
#include "memsafe.h"

#include "../libs/minhook/include/MinHook.h"

#include <stdio.h>
#include <string.h>

/* The update method's signature as far as we can tell: a this-call taking one
 * further argument.
 *
 * ⛔⛔ THE SIGNATURE TRAP THIS PROJECT ALREADY PAID FOR. menu_cursor's detour
 * declared FOUR arguments where the real helper took SIX, so the original read
 * its 5th and 6th out of stack slots our frame never filled -- and the 5th was
 * the cursor SFX selector, which the user HEARD change. Ten passing tests
 * missed it, because each asked what the hook REPORTED and none asked what it
 * PASSED ON.
 *
 * ⭐ Here the reading is different, and safer: 0x1402f9720 uses only rcx and
 * rdx before its first call (mov rcx,rdx / mov rbx,rdx / mov rdi,rcx at
 * 0x1402f9750-0x1402f9765), and it never touches r8/r9 or any inbound stack
 * slot. Two arguments is the whole signature. Verified by reading the
 * instructions, not by assuming the common case.
 *
 * ⚠ The return value is passed through untouched whatever we conclude, so even
 * if this reading were wrong in some way we have not seen, the game's control
 * flow does not depend on us. */
typedef void *(*pfn_update)(void *self, void *arg);

static pfn_update g_real_update;

/* The constructor: `cGUIObjText *ctor(cGUIObjText *this)` -- one argument, and
 * it returns `this` (`mov rax,rbx` before the epilogue). */
typedef void *(*pfn_ctor)(void *self);
static pfn_ctor g_real_ctor;

static volatile LONG g_calls;
static volatile LONG g_distinct;
static volatile LONG g_faults;
/* Instances whose vptr was not this class's -- see gt_poll_and_log(). A
 * nonzero count means the hook target is wrong, not that the game misbehaved. */
static volatile LONG g_wrong_class;
/* Calls whose object was a DIFFERENT class -- expected and normal for a shared
 * helper, so counted separately from g_wrong_class (which means the filter let
 * one through and is a BUG). */
static volatile LONG g_other_class;
/* Calls whose object WAS ours. ⭐ The number that says the filter is working:
 * a large g_other_class with g_ours == 0 means we are hooked to something this
 * class never reaches. */
static volatile LONG g_ours;
/* ⭐⭐ Constructions seen. THE decisive number: cGUIObjText objects that were
 * actually built. It answers a question no update-method hook can, because
 * every such hook confounds "wrong method" with "no objects exist". */
static volatile LONG g_ctors;

/* The most recently seen instance, published for the worker thread.
 *
 * ⚠ A sequence counter, not a flag: the hook runs on the game's render thread
 * and the reader on ours. A plain "new data" boolean could be cleared between
 * the hook's write and the reader's read and lose the capture. The reader
 * snapshots and compares the sequence either side of its copy; if they differ
 * the hook wrote during the copy and the snapshot is discarded rather than
 * reported torn. Same discipline as menu_cursor. */
static volatile LONG  g_seq;
static void          *g_last_self;
static void          *g_last_caller;
static LONG           g_taken_seq;

static BOOL              g_installed;
static gt_install_status g_why = GT_INSTALL_NOT_TRIED;

gt_install_status gt_install_why(void) { return g_why; }

/* --- the detour ----------------------------------------------------------
 *
 * ⚠⚠ IT DEREFERENCES NOTHING. It records a pointer VALUE and calls the
 * original. That is the entire body, and it is deliberate: this runs on the
 * game's threads, and this project has already taken the game down by reading
 * through a caller-supplied pointer inside a hook. Recording a value cannot
 * fault. All inspection happens later, on our worker thread, where a fault is
 * caught by the guard and costs us a log line instead of the process.
 *
 * ⚠ The original is called UNCONDITIONALLY and its result returned untouched.
 */
static void *detour_body(pfn_update real, void *self, void *arg, void *caller)
{
    InterlockedIncrement(&g_calls);

    /* ⭐⭐ THE FILTER THAT MAKES A SHARED HELPER USABLE.
     *
     * This helper runs for several GUI classes -- that is WHY it runs per
     * element per frame, and why it is the right hook. But it means most calls
     * are not ours. Publishing every one would fill the channel with
     * OutlineFonts and the worker would rarely see a cGUIObjText.
     *
     * So read ONE qword -- the vptr -- and publish only our class's objects.
     *
     * ⚠⚠ THIS IS THE ONLY DEREFERENCE IN THE DETOUR, and it is the narrowest
     * possible one: 8 bytes at offset 0 of a pointer the game is CURRENTLY
     * making a virtual call through. If [self+0] were unreadable the game
     * would already have crashed reaching us. It is still guarded, because
     * "the game would have crashed" is an argument, and this project has been
     * wrong about exactly this kind of argument before -- a caller's pointer
     * is untrusted even when the reasoning says otherwise.
     *
     * ⭐ Everything wider than these 8 bytes stays where it was: on the worker
     * thread, under the guard, off the game's threads. */
    /* ⚠⚠ TWO LAYERS, NOT ONE. The fault guard alone is not enough and this was
     * proven here, not reasoned about: with only MEM_GUARDED around the read,
     * the offline suite died with 0xC0000005 on the test's synthetic pointer.
     * The guard is a vectored handler, and a fault taken on a thread whose
     * frame it cannot unwind still takes the process down.
     *
     * ⭐ So ask the memory manager FIRST (mem_readable_run), and keep the guard
     * as the second layer for the TOCTOU window -- another thread can free the
     * region between the query and the read. This is exactly the pairing
     * menu_cursor's cursor_is_safe() + MEM_GUARDED uses, and the same reason.
     *
     * ⚠ Checked every call, never cached: a GUI object is freed on a screen
     * change and a pointer that passed a moment ago is not a pointer that
     * still passes. */
    ULONGLONG vptr = 0;
    BOOL faulted = FALSE;
    if (!self || ((ULONG_PTR)self & 7u) ||
        mem_readable_run(self, sizeof(vptr)) < sizeof(vptr)) {
        InterlockedIncrement(&g_other_class);
        return real(self, arg);
    }
    MEM_GUARDED(faulted, vptr = *(volatile ULONGLONG *)self);
    if (faulted || vptr != GT_VTABLE_ADDR) {
        if (faulted)
            InterlockedIncrement(&g_faults);
        else
            InterlockedIncrement(&g_other_class);
        return real(self, arg);
    }

    InterlockedIncrement(&g_ours);

    /* Publish. Bump, write, bump: a reader seeing the same even value either
     * side of its copy knows the copy was clean. */
    InterlockedIncrement(&g_seq);
    g_last_self   = self;
    g_last_caller = caller;
    InterlockedIncrement(&g_seq);

    return real(self, arg);
}

/* ⚠ __builtin_return_address(0) is the GAME's return address: MinHook patches
 * the target with a jmp and pushes nothing, so the address the CPU pushed for
 * the call is still the one on the stack. GCC has no _ReturnAddress().
 *
 * ⚠ Recorded and compared only, never dereferenced -- unlike a caller-SUPPLIED
 * pointer, our own return address is trustworthy, but it still buys nothing to
 * treat it as memory.
 *
 * ⛔ NOT normalised to a call site here, unlike menu_cursor. This method is
 * VTABLE-dispatched (call qword ptr [rax+0x28]), so the byte five back is not
 * 0xE8 and the "ret - 5" rule menu_cursor uses does not apply. Applying it
 * anyway would manufacture a plausible-looking address that is not a call
 * site. The raw value is logged as what it is. */
static void *hook_update(void *self, void *arg)
{
    return detour_body(g_real_update, self, arg,
                       __builtin_return_address(0));
}

/* The constructor detour -- the BIRTH probe.
 *
 * ⚠⚠ NO VPTR FILTER HERE, and that is deliberate rather than an oversight.
 * At ctor entry the object is raw allocation: the factory writes the vptr only
 * AFTER this returns. Filtering on it would reject every object and manufacture
 * exactly the false zero this probe exists to escape.
 *
 * ⭐ Safety is unchanged in the part that matters: the detour still
 * DEREFERENCES NOTHING. It counts, records the pointer, and calls the original.
 * The worker re-checks the vptr later -- by which time the factory has written
 * it -- so the identity guarantee is deferred, not dropped.
 *
 * ⚠ We do NOT publish to the capture channel before the original runs: at that
 * moment the object's fields are uninitialised, and a worker poll landing in
 * between would inspect garbage and report it as a finding. Call through
 * first, then publish. */
static void *hook_ctor(void *self)
{
    InterlockedIncrement(&g_ctors);

    void *r = g_real_ctor(self);

    InterlockedIncrement(&g_seq);
    g_last_self   = self;
    g_last_caller = __builtin_return_address(0);
    InterlockedIncrement(&g_seq);

    return r;
}

/* --- the inspector -------------------------------------------------------
 *
 * Given the bytes of one instance, say where the string is.
 *
 * ⭐⭐ IT REPORTS WHAT IT FOUND, AND SEPARATELY WHETHER IT CONCLUDED ANYTHING.
 * "no conclusion" is -1, a distinct return value -- not offset 0, and not an
 * all-zero struct. Those must never look alike (memory:
 * inconclusive-must-be-a-return-value).
 *
 * ⚠ The two candidate offsets are checked FIRST and named as candidates, but
 * they get no special authority: a candidate that does not resolve to text
 * loses to any other offset that does. The offline accessors made them worth
 * looking at; only the live bytes decide.
 *
 * `resolve` turns a pointer field's value into readable bytes. In the game it
 * is a guarded read of foreign memory; in a test it reads the harness's own
 * buffer. Passing it in is what lets the SHIPPING inspector run in the test
 * without the test re-deriving what the inspector does. */
typedef SIZE_T (*pfn_resolve)(void *ctx, ULONGLONG addr, char *out, SIZE_T max);

/* Does this look like text a human would read? Deliberately strict: a run of
 * printable ASCII, at least 2 characters, NUL-terminated within the window.
 *
 * ⚠ Strictness here is safety, not taste. A loose test turns every pointer to
 * arbitrary bytes into a "string" and buries the real field in noise -- and
 * this project has twice mistaken a 4-byte coincidence for evidence. */
static BOOL looks_like_text(const char *s, SIZE_T n, int *out_len)
{
    SIZE_T i = 0;
    while (i < n && s[i] != '\0') {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7e)
            return FALSE;
        i++;
    }
    if (i < 2 || i >= n)        /* too short, or no terminator in the window */
        return FALSE;
    if (out_len)
        *out_len = (int)i;
    return TRUE;
}

/* UTF-16 is the other real possibility: the shell is a Japanese-authored
 * engine and the GMD holds UTF-8, but a widened copy in the element would be
 * unsurprising. Checked explicitly rather than assumed absent. */
static BOOL looks_like_wide_text(const char *s, SIZE_T n, int *out_len)
{
    SIZE_T i = 0;
    while (i + 1 < n) {
        unsigned char lo = (unsigned char)s[i], hi = (unsigned char)s[i + 1];
        if (lo == 0 && hi == 0)
            break;
        if (hi != 0)            /* non-Latin-1: stop claiming, stay strict */
            return FALSE;
        if (lo < 0x20 || lo > 0x7e)
            return FALSE;
        i += 2;
    }
    if (i < 4 || i + 1 >= n)
        return FALSE;
    if (out_len)
        *out_len = (int)(i / 2);
    return TRUE;
}

/* The shared inspector body. Returns TRUE if it examined the whole object;
 * *text_offset gets the winning offset or -1.
 *
 * ⭐⭐ IT PRINTS A POSITIVE COUNT OF WHAT IT EXAMINED. "0 candidates" from a
 * sweep that examined 0 fields and "0 candidates" from one that examined 64
 * are completely different findings, and a log that cannot tell them apart is
 * how "0 failed" gets read as "0 checked". */
static BOOL inspect_body(const void *obj, SIZE_T len,
                         pfn_resolve resolve, void *ctx,
                         int *text_offset, BOOL log_it)
{
    const unsigned char *b = (const unsigned char *)obj;
    int    best        = -1;
    int    best_len    = 0;
    BOOL   best_wide   = FALSE;
    BOOL   best_is_cand = FALSE;
    int    examined    = 0;      /* pointer-shaped fields actually resolved */
    int    hits        = 0;

    if (text_offset)
        *text_offset = -1;
    if (len < 8)
        return FALSE;

    for (SIZE_T off = 0; off + 8 <= len; off += 8) {
        ULONGLONG v;
        memcpy(&v, b + off, 8);

        /* A pointer field worth resolving. Zero and small integers are not
         * addresses; neither is anything above the user-mode ceiling. */
        if (v < 0x10000 || v > 0x7fffffffffffULL)
            continue;

        char buf[128];
        SIZE_T got = resolve(ctx, v, buf, sizeof(buf));
        examined++;
        if (got == 0)
            continue;

        int  slen = 0;
        BOOL wide = FALSE;
        if (looks_like_text(buf, got, &slen)) {
            wide = FALSE;
        } else if (looks_like_wide_text(buf, got, &slen)) {
            wide = TRUE;
        } else {
            continue;
        }

        hits++;
        BOOL is_cand = (off == GT_CAND_A || off == GT_CAND_B);
        if (log_it)
            log_line("gt:   +0x%03x -> 0x%llx  %s\"%.60s\" (%d chars)%s",
                     (unsigned)off, v, wide ? "L" : "", buf, slen,
                     is_cand ? "   <- OFFLINE CANDIDATE" : "");

        /* ⭐ A candidate offset wins ties, but never beats a longer string at
         * another offset. The offline accessors earned these two offsets a
         * look, not a verdict. */
        if (best < 0 ||
            (is_cand && !best_is_cand && slen >= best_len) ||
            (slen > best_len && (is_cand || !best_is_cand))) {
            best = (int)off; best_len = slen;
            best_wide = wide; best_is_cand = is_cand;
        }
    }

    /* Also look for the string stored INLINE rather than behind a pointer --
     * a fixed char array in the object is just as likely as a pointer, and a
     * sweep that only followed pointers would miss it completely. */
    for (SIZE_T off = 0; off + 4 <= len; off += 4) {
        int slen = 0;
        if (looks_like_text((const char *)b + off, len - off, &slen) &&
            slen >= 3) {
            if (log_it)
                log_line("gt:   +0x%03x INLINE \"%.60s\" (%d chars)",
                         (unsigned)off, (const char *)b + off, slen);
            hits++;
            if (best < 0) { best = (int)off; best_len = slen; }
            /* Skip past what we just read, so one string is not reported
             * again at every offset inside itself. */
            off += (SIZE_T)slen & ~3u;
        }
    }

    if (log_it)
        log_line("gt: examined %d pointer-shaped field(s) in %u bytes; "
                 "%d resolved to text; conclusion: %s",
                 examined, (unsigned)len, hits,
                 best < 0 ? "NONE -- no offset holds readable text"
                          : "see the winning offset below");
    if (log_it && best >= 0)
        log_line("gt: ⭐ best candidate is +0x%03x (%s%d chars)%s",
                 (unsigned)best, best_wide ? "wide, " : "", best_len,
                 best_is_cand ? " -- and it is one of the two offsets the "
                                "offline accessors pointed at"
                              : " -- ⚠ NOT one of the offline candidates");

    if (text_offset)
        *text_offset = best;
    return TRUE;
}

/* --- resolving a pointer in the live game -------------------------------- */

/* ⚠⚠ EVERY read is VirtualQuery-checked and guarded, EVERY time. A GUI object
 * is freed when its screen changes; a pointer that passed a check a moment ago
 * is not a pointer that is still valid. Caching a check here would be exactly
 * the mistake that killed the game once already. */
static SIZE_T resolve_live(void *ctx, ULONGLONG addr, char *out, SIZE_T max)
{
    (void)ctx;
    const void *p = (const void *)(ULONG_PTR)addr;

    SIZE_T run = mem_readable_run(p, max);
    if (run == 0)
        return 0;
    if (run > max)
        run = max;

    /* ⚠ mem_copy is itself guarded, but count a fault so a partial sweep can
     * never be reported as a complete one. A scan that says "found nothing"
     * while faults are nonzero was a PARTIAL scan and must say so. */
    if (!mem_copy(out, p, run)) {
        InterlockedIncrement(&g_faults);
        return 0;
    }
    return run;
}

BOOL gt_poll_and_log(void)
{
    if (!g_installed)
        return FALSE;

    /* Snapshot the published capture, discarding a torn read. */
    LONG  s1 = InterlockedCompareExchange(&g_seq, 0, 0);
    void *self   = g_last_self;
    void *caller = g_last_caller;
    LONG  s2 = InterlockedCompareExchange(&g_seq, 0, 0);
    if (s1 != s2 || (s1 & 1))
        return FALSE;                 /* the hook wrote during our copy */
    if (s1 == g_taken_seq || !self)
        return FALSE;                 /* nothing new since last time */
    g_taken_seq = s1;

    /* Read the whole object under the guard, ONCE, then work on our copy.
     * ⭐ One guarded block for 528 bytes, not one per field: read_cstr once did
     * a VirtualQuery+setjmp per CHARACTER and cost 172 ms where a block read
     * cost 3.8. Same lesson, applied before it bites this time. */
    unsigned char obj[GT_CLASS_SIZE];
    if (mem_readable_run(self, sizeof(obj)) < sizeof(obj) ||
        !mem_copy(obj, self, sizeof(obj))) {
        InterlockedIncrement(&g_faults);
        log_line("gt: instance %p is no longer fully readable -- skipped. "
                 "(Expected sometimes: GUI objects are freed on a screen "
                 "change, which is why the pointer is re-checked every time "
                 "and never cached.)", self);
        return FALSE;
    }

    InterlockedIncrement(&g_distinct);

    ULONGLONG vptr;
    memcpy(&vptr, obj, 8);
    log_line("gt: --- cGUIObjText instance %p (called from %p) ---",
             self, caller);
    /* ⭐ A POSITIVE identity check on the copy, not just a plausible shape.
     * If the vptr is not this class's vtable, the object is not what the hook
     * promised and every offset below would be measured against the wrong
     * layout -- say so loudly rather than quietly reporting nonsense. */
    if (vptr != GT_VTABLE_ADDR) {
        /* ⛔⛔ REFUSE, do not merely warn. This started as a warning followed
         * by the full dump, and the first live run showed why that is wrong:
         * the object was an nGUI::OutlineFont and the dump was 41 lines of
         * Adobe glyph names, with BOTH "candidate" offsets holding
         * plausible-looking strings. Nothing there was a menu label, and a
         * reader skimming for an offset could easily have taken one.
         *
         * ⭐ If the code prints a warning, something must FAIL on it -- this
         * project's own rule. So the wrong class produces a diagnosis and NO
         * offsets at all, which is the honest output: we did not measure
         * cGUIObjText, so we have nothing to say about its layout. */
        InterlockedIncrement(&g_wrong_class);
        log_line("gt: ⛔ vptr is 0x%llx%s, NOT cGUIObjText's 0x%llx -- this "
                 "object is NOT the class we hooked for. REFUSING to dump its "
                 "offsets: they would describe the wrong layout. Fix the hook "
                 "target; a shared helper reached by a direct call receives "
                 "its callers' own objects.", vptr,
                 vptr == GT_VTABLE2_ADDR ? " (nGUI::OutlineFont)" : "",
                 GT_VTABLE_ADDR);
        return FALSE;
    }
    log_line("gt: vptr CONFIRMED 0x%llx -- this really is a cGUIObjText.",
             vptr);

    int off = -1;
    inspect_body(obj, sizeof(obj), resolve_live, NULL, &off, TRUE);

    LONG faults = InterlockedCompareExchange(&g_faults, 0, 0);
    if (faults)
        log_line("gt: ⚠ %ld guarded read(s) have faulted since install, so "
                 "this sweep was PARTIAL -- a negative result here is not "
                 "evidence that no offset holds text.", faults);
    return TRUE;
}

/* --- install -------------------------------------------------------------
 *
 * The expected first bytes of 0x1402fd6d0:
 *     push rbx; sub rsp,0x30   = 40 53 48 83 ec 30
 * SIX bytes across two whole instructions, so MinHook's five-byte patch ends
 * on a clean boundary. ⭐ And find_callers.py --jmp found zero branches into
 * addr+1..+4, so nothing can land inside the patch.
 *
 * ⭐⭐ THE PROPERTY THAT MATTERS MOST is not visible in these bytes: this
 * function has ZERO direct callers, so the vtable is the only way to reach it.
 * The previous target had a perfectly good prologue and was still wrong,
 * because it was a shared helper three sites called directly. A prologue check
 * confirms WHICH CODE we patched; it says nothing about WHOSE OBJECTS arrive.
 *
 * The prologue is re-checked here at runtime because the exe is Enigma-packed
 * and these addresses hold ciphertext until unpack completes; hooking early
 * would patch encrypted bytes. */
static const BYTE GT_PROLOGUE[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x30 };

/* ⛔⛔ THE CTOR NEEDS ITS OWN. It is `push rbx; sub rsp,0x20` -- the SAME shape
 * as the update method but a different frame size, so the last byte is 0x20,
 * not 0x30.
 *
 * The first version reused GT_PROLOGUE for both, and the ctor hook silently
 * declined to install for exactly that one byte. It cost a run. ⭐ It cost
 * only ONE run because the refusal was loud and the stats carried
 * `ctor_hooked`, so the log said "this zero measures nothing" instead of
 * "0 constructed" -- the difference between a wasted run and a wrong
 * conclusion. Two functions never share a prologue constant just because they
 * start with the same two instructions. */
static const BYTE GT_CTOR_PROLOGUE[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20 };

static BOOL check_target(void)
{
    void *p = (void *)(ULONG_PTR)GT_UPDATE_ADDR;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log_line("gt: 0x%llx is not committed executable memory -- REFUSING "
                 "to hook. Nothing was written.", GT_UPDATE_ADDR);
        g_why = GT_INSTALL_BAD_PROTECT;
        return FALSE;
    }

    BYTE got[sizeof(GT_PROLOGUE)];
    if (!mem_copy(got, p, sizeof(got)) ||
        memcmp(got, GT_PROLOGUE, sizeof(got)) != 0) {
        log_line("gt: the bytes at 0x%llx are not the expected prologue "
                 "(%02x %02x %02x %02x %02x) -- the offline model does NOT "
                 "match this build. REFUSING.",
                 GT_UPDATE_ADDR, got[0], got[1], got[2], got[3], got[4]);
        g_why = GT_INSTALL_BAD_PROLOGUE;
        return FALSE;
    }

    /* ⭐⭐ THE UNIQUENESS CLAIM, RE-CHECKED IN THIS PROCESS. The whole reason
     * no type check is needed in the detour is that this function address sits
     * in exactly one vtable slot -- cGUIObjText's. That was established from a
     * dump. Confirm the half of it we can confirm cheaply here: the class's
     * own vtable really does hold this address in the expected slot. If the
     * build moved, we would otherwise hook something and attribute its
     * instances to the wrong class. */
    /* ⭐ Checked against the INSTANCE vtable -- the one an object actually
     * carries, and the one the detour's filter tests. The DTI's descriptor
     * vtable (0x141056350) is a different, two-slot table; confusing the two
     * is half of what went wrong in the first four runs. */
    ULONGLONG slot = 0;
    const void *sp = (const void *)(ULONG_PTR)
                     (GT_VTABLE_ADDR + GT_VTABLE_SLOT * 8);
    if (!mem_copy(&slot, sp, sizeof(slot)) || slot != GT_UPDATE_ADDR) {
        log_line("gt: cGUIObjText's instance vtable slot %d holds 0x%llx, not "
                 "the "
                 "expected 0x%llx -- the class layout does not match the "
                 "offline model. REFUSING.",
                 GT_VTABLE_SLOT, slot, GT_UPDATE_ADDR);
        g_why = GT_INSTALL_BAD_VTABLE;
        return FALSE;
    }
    return TRUE;
}

BOOL gt_install(void)
{
    if (g_installed)
        return TRUE;

    if (!check_target())
        return FALSE;

    log_line("gt: identity CONFIRMED -- 0x%llx carries the expected prologue "
             "and sits in cGUIObjText's vtable slot %d, which is what makes "
             "every captured instance certainly of this class.",
             GT_UPDATE_ADDR, GT_VTABLE_SLOT);

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        log_line("gt: MH_Initialize failed (%d).", (int)st);
        g_why = GT_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }

    void *t = (void *)(ULONG_PTR)GT_UPDATE_ADDR;
    if (MH_CreateHook(t, (void *)&hook_update,
                      (void **)&g_real_update) != MH_OK) {
        log_line("gt: MH_CreateHook failed.");
        g_why = GT_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }
    if (MH_EnableHook(t) != MH_OK) {
        log_line("gt: MH_EnableHook failed; removing.");
        MH_RemoveHook(t);
        g_why = GT_INSTALL_MINHOOK_FAILED;
        return FALSE;
    }

    /* ⭐⭐ The CONSTRUCTOR hook -- the birth probe. See the header.
     *
     * ⚠ Its failure is NOT fatal to the install: the update hook above is
     * already live and useful, and a run with one of the two is better than no
     * run. But it is logged loudly, because the ctor count is the number that
     * makes a zero interpretable -- without it, "update ran 0 times" cannot be
     * told apart from "no objects were ever created". */
    void *c = (void *)(ULONG_PTR)GT_CTOR_ADDR;
    BYTE cpro[sizeof(GT_CTOR_PROLOGUE)];
    if (!mem_copy(cpro, c, sizeof(cpro)) ||
        memcmp(cpro, GT_CTOR_PROLOGUE, sizeof(cpro)) != 0) {
        /* ⭐ PRINT WHAT WE SAW, not just that it mismatched. The first version
         * of this line said only "does not carry the expected prologue", and
         * the cause was a single byte (0x20 vs 0x30) that the message could
         * have shown. A refusal that does not say what it saw makes the reader
         * re-derive the disassembly to find a one-byte bug. */
        log_line("gt: ⚠ the ctor at 0x%llx carries %02x %02x %02x %02x %02x "
                 "%02x, not the expected %02x %02x %02x %02x %02x %02x -- NOT "
                 "hooking it. The construction count will be absent, so a zero "
                 "update count cannot be told apart from 'no objects exist'.",
                 GT_CTOR_ADDR,
                 cpro[0], cpro[1], cpro[2], cpro[3], cpro[4], cpro[5],
                 GT_CTOR_PROLOGUE[0], GT_CTOR_PROLOGUE[1], GT_CTOR_PROLOGUE[2],
                 GT_CTOR_PROLOGUE[3], GT_CTOR_PROLOGUE[4],
                 GT_CTOR_PROLOGUE[5]);
    } else if (MH_CreateHook(c, (void *)&hook_ctor,
                             (void **)&g_real_ctor) != MH_OK ||
               MH_EnableHook(c) != MH_OK) {
        MH_RemoveHook(c);
        g_real_ctor = NULL;
        log_line("gt: ⚠ could not hook the ctor at 0x%llx; construction count "
                 "will be absent.", GT_CTOR_ADDR);
    } else {
        log_line("gt: hooked the cGUIObjText CONSTRUCTOR at 0x%llx. It has "
                 "exactly ONE caller (the factory 0x%llx), so this counts "
                 "EVERY object of this class that is ever built -- which is "
                 "what tells a wrong hook target apart from a class the shell "
                 "never instantiates.", GT_CTOR_ADDR, GT_FACTORY_ADDR);
    }

    g_installed = TRUE;
    g_why = GT_INSTALL_OK;
    log_line("gt: hooked cGUIObjText's update method at 0x%llx. The detour "
             "records the instance pointer and DEREFERENCES NOTHING; all "
             "reading happens on the worker thread under the fault guard.",
             GT_UPDATE_ADDR);
    return TRUE;
}

void gt_remove(void)
{
    if (!g_installed)
        return;
    void *t = (void *)(ULONG_PTR)GT_UPDATE_ADDR;
    MH_DisableHook(t);
    MH_RemoveHook(t);

    /* ⚠ Only if it was actually installed. g_real_ctor is the record of that:
     * the ctor hook is allowed to fail without failing the install, so
     * unhooking unconditionally would ask MinHook to remove a hook that was
     * never created. */
    if (g_real_ctor) {
        void *c = (void *)(ULONG_PTR)GT_CTOR_ADDR;
        MH_DisableHook(c);
        MH_RemoveHook(c);
        g_real_ctor = NULL;
    }

    g_installed = FALSE;
    log_line("gt: hooks removed.");
}

void gt_get_stats(gt_stats *out)
{
    memset(out, 0, sizeof(*out));
    out->installed = g_installed;
    out->calls     = InterlockedCompareExchange(&g_calls, 0, 0);
    out->distinct  = InterlockedCompareExchange(&g_distinct, 0, 0);
    out->faults    = InterlockedCompareExchange(&g_faults, 0, 0);
    out->wrong_class = InterlockedCompareExchange(&g_wrong_class, 0, 0);
    out->other_class = InterlockedCompareExchange(&g_other_class, 0, 0);
    out->ours        = InterlockedCompareExchange(&g_ours, 0, 0);
    out->ctors       = InterlockedCompareExchange(&g_ctors, 0, 0);
    out->ctor_hooked = (g_real_ctor != NULL);
}

/* --- test seams ----------------------------------------------------------
 *
 * ⛔⛔ The stand-in returns a value the TEST chose; it does not compute one
 * plausibly. A stand-in that re-derives what the shipping code derives makes a
 * broken mechanism and a working one indistinguishable -- this project has
 * shipped that mistake twice. */
static void *g_sim_ret;

static void *fake_original(void *self, void *arg)
{
    (void)self; (void)arg;
    return g_sim_ret;
}

void *gt_test_body(void *self, void *arg)
{
    /* A distinctive value the test can recognise, so "passed through" is
     * provable rather than merely plausible. */
    g_sim_ret = (void *)(ULONG_PTR)0xABCDEF0012345678ULL;
    return detour_body(fake_original, self, arg, (void *)0x140001234ULL);
}

void *gt_test_last_captured(void)
{
    return g_last_self;
}

/* Expose the prologue constants so a test can pin them. `which`: 0 = the
 * update method, 1 = the ctor.
 *
 * ⭐ Returns the SHIPPING constants, not copies -- a test against a duplicate
 * of the values would pass while the values the install actually compares
 * against were wrong, which is precisely the bug this exists to catch. */
const BYTE *gt_test_prologue(int which, int *len)
{
    if (which == 0) {
        if (len) *len = (int)sizeof(GT_PROLOGUE);
        return GT_PROLOGUE;
    }
    if (len) *len = (int)sizeof(GT_CTOR_PROLOGUE);
    return GT_CTOR_PROLOGUE;
}

/* The stand-in "original" ctor. ⛔ It returns a value the TEST recognises
 * rather than computing a plausible one -- a stand-in that re-derives what the
 * shipping code derives leaves a broken mechanism indistinguishable from a
 * working one. */
static void *fake_ctor(void *self)
{
    (void)self;
    return g_sim_ret;
}

void *gt_test_ctor_body(void *self)
{
    g_sim_ret = (void *)(ULONG_PTR)0xC0FFEE0011223344ULL;
    pfn_ctor saved = g_real_ctor;
    g_real_ctor = fake_ctor;
    void *r = hook_ctor(self);
    g_real_ctor = saved;
    return r;
}

/* Resolve a pointer inside the harness's own buffer. The test builds an object
 * whose "pointer" fields hold addresses within a buffer it owns, so the
 * SHIPPING inspector walks real pointer fields without touching foreign
 * memory. */
typedef struct { const unsigned char *base; ULONGLONG lo, hi; } test_ctx;

static SIZE_T resolve_test(void *ctx, ULONGLONG addr, char *out, SIZE_T max)
{
    test_ctx *t = (test_ctx *)ctx;
    if (addr < t->lo || addr >= t->hi)
        return 0;
    SIZE_T avail = (SIZE_T)(t->hi - addr);
    if (avail > max) avail = max;
    memcpy(out, t->base + (addr - t->lo), avail);
    return avail;
}

BOOL gt_test_inspect(const void *buf, SIZE_T len, int *text_offset)
{
    /* The harness's buffer stands in for the process's address space: a
     * pointer field holding `lo + k` resolves to byte k of the buffer. */
    test_ctx ctx;
    ctx.base = (const unsigned char *)buf;
    ctx.lo   = 0x100000ULL;
    ctx.hi   = ctx.lo + len;
    return inspect_body(buf, len, resolve_test, &ctx, text_offset, FALSE);
}

void gt_test_reset(void)
{
    g_calls = g_distinct = g_faults = g_wrong_class = 0;
    g_other_class = g_ours = g_ctors = 0;
    g_seq = 0; g_taken_seq = 0;
    g_last_self = NULL; g_last_caller = NULL;
}
