#include "pad_watch.h"
#include "memsafe.h"
#include "log.h"
#include "speech.h"

#include <stdio.h>
#include <string.h>

/* Two snapshots: one taken while a direction is held, one while nothing is.
 * Comparing them is the whole method -- a byte that differs between the two
 * states is reacting to the keyboard, and everything else is subtracted. */
static BYTE     *g_held;        /* snapshot while a direction was held    */
static BYTE     *g_free;        /* snapshot while nothing was held        */
static unsigned  g_held_dirs;   /* which direction the held snapshot used */
static SIZE_T    g_span;        /* bytes actually captured                */
static BOOL      g_reported;

static uintptr_t singleton(void)
{
    HMODULE base = GetModuleHandleW(NULL);
    if (!base)
        return 0;
    uintptr_t slot = (uintptr_t)base + PAD_SINGLETON_RVA;
    uintptr_t obj = 0;
    BOOL faulted;
    MEM_GUARDED(faulted, obj = *(uintptr_t *)slot);
    return faulted ? 0 : obj;
}

/* Capture the object into `dst`. Returns bytes captured, 0 on failure.
 *
 * ⚠ Reads in page-sized chunks under one guard each, so a partially committed
 * object yields a partial capture rather than nothing -- the layout is unknown
 * and the tail may simply not be mapped. */
static SIZE_T capture(BYTE *dst, uintptr_t obj)
{
    /* volatile: lives across setjmp, so longjmp could restore a stale value
     * and silently truncate or loop the capture (-Wclobbered). */
    volatile SIZE_T got = 0;
    while (got < PAD_WATCH_SPAN) {
        SIZE_T chunk = PAD_WATCH_SPAN - (SIZE_T)got;
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

void pad_watch_tick(unsigned dirs)
{
    if (g_reported)
        return;

    /* Need one snapshot of each state; take whichever is still missing. */
    if (dirs && g_held)
        return;
    if (!dirs && g_free)
        return;

    uintptr_t obj = singleton();
    if (!obj) {
        static BOOL said;
        if (!said) {
            said = TRUE;
            log_line("pad: singleton at image+0x%x is still NULL -- the input "
                     "system has not been constructed yet.", PAD_SINGLETON_RVA);
        }
        return;
    }

    if (!mem_guard_install()) {
        log_line("pad: fault guard did not install -- not watching.");
        return;
    }

    BYTE **slot = dirs ? &g_held : &g_free;
    if (!*slot) {
        *slot = (BYTE *)VirtualAlloc(NULL, PAD_WATCH_SPAN,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!*slot) {
            log_line("pad: could not allocate a snapshot buffer.");
            return;
        }
    }

    SIZE_T got = capture(*slot, obj);
    if (!got) {
        log_line("pad: could not read the singleton object at 0x%llx.",
                 (unsigned long long)obj);
        return;
    }
    if (!g_span || got < g_span)
        g_span = got;

    if (dirs) {
        g_held_dirs = dirs;
        log_line("pad: captured HELD state (dirs 0x%02x) from 0x%llx, "
                 "%llu bytes", dirs, (unsigned long long)obj,
                 (unsigned long long)got);
    } else {
        log_line("pad: captured RELEASED state from 0x%llx, %llu bytes",
                 (unsigned long long)obj, (unsigned long long)got);
    }

    if (g_held && g_free) {
        pad_watch_report();
        g_reported = TRUE;
    }
}

void pad_watch_report(void)
{
    if (!g_held || !g_free) {
        log_line("pad: need both a held and a released capture -- hold a "
                 "direction for a moment, then let go.");
        speech_say(L"Need both states.", TRUE);
        return;
    }

    /* Which direction bits the held snapshot should show. Either the D-pad
     * bit or the stick bit may carry it, never both, so accept either. */
    unsigned want = g_held_dirs;

    unsigned diffs = 0, dirlike = 0;
    log_line("pad: comparing %llu bytes; held dirs = 0x%02x",
             (unsigned long long)g_span, want);

    for (SIZE_T i = 0; i < g_span; i++) {
        if (g_held[i] == g_free[i])
            continue;
        diffs++;
        /* ⚠ Print EVERY differing byte, not only the ones matching the
         * expected shape. When the expected shape does not appear, what DID
         * change is the only lead left -- and suppressing it turns a useful
         * negative into a dead end. */
        if (diffs <= 64)
            log_line("pad:   diff +0x%-6llx  held 0x%02x  released 0x%02x",
                     (unsigned long long)i, g_held[i], g_free[i]);
        /* ⭐ The shape we are looking for: nonzero while held, zero while
         * released, and the nonzero value is one of the direction bits. That
         * is a button byte, read off the machine rather than inferred. */
        BYTE h = g_held[i], f = g_free[i];
        if (f == 0 && (h & want) == h && h != 0) {
            dirlike++;
            if (dirlike <= 40)
                log_line("pad:   ⭐ +0x%-6llx  held 0x%02x  released 0x%02x",
                         (unsigned long long)i, h, f);
        }
    }

    log_line("pad: %u byte(s) differed; %u look like direction bits.",
             diffs, dirlike);

    if (!dirlike) {
        /* ⚠ Say what a null result means. A quiet "0 found" reads the same as
         * a broken tool, and that ambiguity has cost several runs here. */
        log_line("pad: ⚠ nothing matched the button-byte shape. Either the "
                 "records are outside the first 0x%x bytes, the two captures "
                 "were taken in the same state, or the pad data lives behind "
                 "another pointer rather than inline.", PAD_WATCH_SPAN);
        if (diffs > 2000)
            log_line("pad:   (%u bytes differed, so the captures WERE taken "
                     "in different states -- the object is just busy.)", diffs);
        speech_say(L"No button bytes found. Check the log.", TRUE);
    } else {
        speech_say(L"Found candidate button bytes.", TRUE);
    }
}

/* Read the two confirmed button bytes and name the direction they report.
 *
 * This is the end-to-end check the whole route was for: press a direction,
 * hear it. No searching, no filtering -- a fixed offset from a fixed pointer,
 * both established before this ran. */
void pad_watch_say(void)
{
    uintptr_t obj = singleton();
    if (!obj) {
        speech_say(L"Pad object not ready.", TRUE);
        return;
    }
    BYTE stick = 0, dpad = 0;
    BOOL f1, f2;
    MEM_GUARDED(f1, stick = *(BYTE *)(obj + PAD_STICK_OFFSET));
    MEM_GUARDED(f2, dpad  = *(BYTE *)(obj + PAD_DPAD_OFFSET));
    if (f1 || f2) {
        speech_say(L"Pad bytes unreadable.", TRUE);
        return;
    }

    /* Either byte may carry the direction; OR them and name what is set. */
    unsigned bits = (unsigned)stick | (unsigned)dpad;
    const wchar_t *word = NULL;
    if (bits & PAD_UP)         word = L"up";
    else if (bits & PAD_DOWN)  word = L"down";
    else if (bits & PAD_LEFT)  word = L"left";
    else if (bits & PAD_RIGHT) word = L"right";

    log_line("pad: stick=0x%02x dpad=0x%02x -> %ls", stick, dpad,
             word ? word : L"(none)");
    speech_say(word ? word : L"none", TRUE);
}
