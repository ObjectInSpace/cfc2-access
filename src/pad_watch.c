#include "pad_watch.h"
#include "memsafe.h"
#include "log.h"
#include "speech.h"

#include <stdio.h>
#include <string.h>

static unsigned g_seq;          /* capture number, 1-based */

/* Resolve the sPad instance pointer. Returns 0 if it is still null (the input
 * system is constructed some time after startup) or unreadable. */
static uintptr_t singleton(void)
{
    HMODULE base = GetModuleHandleW(NULL);
    if (!base)
        return 0;
    uintptr_t slot = (uintptr_t)base + PAD_INSTANCE_RVA;
    uintptr_t obj = 0;
    BOOL faulted;
    MEM_GUARDED(faulted, obj = *(uintptr_t *)slot);
    return faulted ? 0 : obj;
}

/* Copy the object into `dst`, page at a time under the fault guard.
 *
 * ⚠ Reads in chunks so a partially committed object yields a PARTIAL capture
 * rather than nothing -- the layout is unknown and the tail may simply not be
 * mapped. The actual byte count is recorded, so the analysis never assumes it
 * got everything. */
static SIZE_T capture(BYTE *dst, uintptr_t obj)
{
    /* volatile: lives across setjmp, so longjmp could restore a stale value
     * and silently truncate or loop the capture (-Wclobbered). */
    volatile SIZE_T got = 0;
    while (got < PAD_DUMP_SPAN) {
        SIZE_T chunk = PAD_DUMP_SPAN - (SIZE_T)got;
        if (chunk > 4096)
            chunk = 4096;
        BOOL faulted;
        MEM_GUARDED(faulted, memcpy(dst + (SIZE_T)got,
                                    (const void *)(obj + (SIZE_T)got), chunk));
        if (faulted)
            break;
        got += chunk;
    }
    return (SIZE_T)got;
}

/* Build a path next to the game exe. */
static BOOL game_path(char *out, size_t n, const char *leaf)
{
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return FALSE;
    char *slash = strrchr(path, '\\');
    if (!slash)
        return FALSE;
    slash[1] = '\0';
    size_t need = strlen(path) + strlen(leaf) + 1;
    if (need > n)
        return FALSE;
    memcpy(out, path, strlen(path));
    memcpy(out + strlen(path), leaf, strlen(leaf) + 1);
    return TRUE;
}

void pad_dump_now(unsigned kb_dirs)
{
    /* ⚠⚠ MEM_GUARDED is inert unless the handler is installed, and an inert
     * guard is worse than none: every read merely LOOKS protected. A previous
     * module used the macro without arming it and died with 0xC0000005.
     * Install self-tests by faulting deliberately, so a guard that would not
     * have worked refuses to proceed. */
    if (!mem_guard_install()) {
        log_line("pad: fault guard did not install -- not dumping.");
        speech_say(L"Dump aborted, no fault guard.", TRUE);
        return;
    }

    uintptr_t obj = singleton();
    if (!obj) {
        log_line("pad: sPad instance at image+0x%x is NULL -- the input "
                 "system is not constructed yet. Reach a menu and try again.",
                 PAD_INSTANCE_RVA);
        speech_say(L"Pad object not ready.", TRUE);
        return;
    }

    BYTE *buf = (BYTE *)VirtualAlloc(NULL, PAD_DUMP_SPAN,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) {
        log_line("pad: could not allocate a %d-byte capture buffer.",
                 PAD_DUMP_SPAN);
        speech_say(L"Dump failed, out of memory.", TRUE);
        return;
    }

    SIZE_T got = capture(buf, obj);
    if (!got) {
        log_line("pad: could not read the object at 0x%llx.",
                 (unsigned long long)obj);
        speech_say(L"Dump failed, unreadable.", TRUE);
        VirtualFree(buf, 0, MEM_RELEASE);
        return;
    }

    unsigned seq = ++g_seq;
    char leaf[64], path[MAX_PATH];
    snprintf(leaf, sizeof leaf, "cfc2_pad_%02u.bin", seq);
    if (!game_path(path, sizeof path, leaf)) {
        log_line("pad: could not build the dump path.");
        VirtualFree(buf, 0, MEM_RELEASE);
        return;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        log_line("pad: could not open %s for writing.", path);
        speech_say(L"Dump failed, cannot write file.", TRUE);
        VirtualFree(buf, 0, MEM_RELEASE);
        return;
    }
    size_t wrote = fwrite(buf, 1, got, f);
    fclose(f);
    VirtualFree(buf, 0, MEM_RELEASE);

    /* ⚠ Record the byte count ACTUALLY WRITTEN, not the count requested. A
     * short write with a confident log line is exactly the kind of quiet
     * mismatch that has cost this project several rounds. */
    if (wrote != got)
        log_line("pad: ⚠ short write -- captured %llu bytes but wrote %llu.",
                 (unsigned long long)got, (unsigned long long)wrote);

    /* The index is the analysis input: it says what state each capture was
     * taken in.
     *
     * ⛔⛔ TRUNCATE ON THE FIRST CAPTURE OF A RUN, never append across runs.
     *
     * This appended unconditionally while the .bin files were OVERWRITTEN by
     * each new run. After a second session the index described ten captures
     * while only the last run's files existed, so stale lines were paired with
     * bytes from a DIFFERENT run -- metadata silently attached to the wrong
     * data, which is worse than losing it outright. It was noticed only
     * because both runs happened to share an object address; with different
     * addresses it would have produced confident nonsense.
     *
     * "w" on seq 1 makes each run self-contained. */
    char ipath[MAX_PATH];
    if (game_path(ipath, sizeof ipath, "cfc2_pad_index.txt")) {
        FILE *ix = fopen(ipath, seq == 1 ? "w" : "a");
        if (ix) {
            if (seq == 1)
                fprintf(ix, "# seq, file, object, bytes, kb_dirs, tick_ms\n");
            fprintf(ix, "%u, %s, 0x%llx, %llu, 0x%02x, %llu\n",
                    seq, leaf, (unsigned long long)obj,
                    (unsigned long long)wrote, kb_dirs,
                    (unsigned long long)GetTickCount64());
            fclose(ix);
        }
    }

    log_line("pad: capture %u -> %s  object=0x%llx  bytes=%llu  keyboard=0x%02x",
             seq, leaf, (unsigned long long)obj,
             (unsigned long long)wrote, kb_dirs);

    /* Speak the sequence number so the user knows the press registered and
     * which step of the protocol they are on -- they cannot see the log. */
    wchar_t msg[64];
    _snwprintf(msg, 63, L"Capture %u", seq);
    msg[63] = 0;
    speech_say(msg, TRUE);
}

/* --- automatic edge capture ---------------------------------------------- */

void pad_auto_capture(unsigned kb_dirs)
{
    static unsigned prev;          /* mask at the previous poll */
    static BOOL     primed;        /* have the two controls been taken? */
    static unsigned taken;

    if (taken >= PAD_AUTO_MAX)
        return;

    /* ⭐ Take the two CONTROL captures first, back to back, while nothing is
     * held. Their difference is the noise floor, and taking them 100 ms apart
     * rather than seconds apart keeps that floor small -- the previous attempt
     * compared states eight seconds apart and measured elapsed time. */
    if (!primed) {
        if (kb_dirs) {
            /* Wait for a quiet moment; a control taken mid-press is not a
             * control. */
            prev = kb_dirs;
            return;
        }
        log_line("pad: taking the two control captures (nothing held).");
        pad_dump_now(0);
        pad_dump_now(0);
        taken += 2;
        primed = TRUE;
        prev = 0;
        return;
    }

    /* An edge in either direction is informative: the down-edge catches Trg
     * while it is live, the up-edge catches Rel and proves the byte returns. */
    if (kb_dirs != prev) {
        log_line("pad: %s edge (0x%02x -> 0x%02x)",
                 kb_dirs ? "press" : "release", prev, kb_dirs);
        pad_dump_now(kb_dirs);
        taken++;
        if (taken >= PAD_AUTO_MAX)
            log_line("pad: %u captures taken -- that is enough. Close the "
                     "game and run tools/analyze_pad_dumps.py.", taken);
    }
    prev = kb_dirs;
}
