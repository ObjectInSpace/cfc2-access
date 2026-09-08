/*
 * Who calls GetAsyncKeyState?
 *
 * WHY THIS EXISTS
 * ---------------
 * The IAT probe answered the standing question: the shell POLLS the keyboard
 * via user32!GetAsyncKeyState (slot 0x140FDC720), and that answer is a
 * trustworthy negative as well as a positive -- 272 of 289 slots resolved into
 * loaded modules, and the entire raw-input family was absent.
 *
 * So the shell's input reader is a routine inside the exe that calls
 * GetAsyncKeyState every frame. Finding WHERE it is turns "which menu item is
 * selected" from a memory search into ordinary disassembly: the selection state
 * is derived from those reads, so it is written near the caller, and this
 * project has established which of those two approaches works.
 *
 *   OK  Find the routine that WRITES the state; read the offsets out of the
 *       instructions. This cracked sPad completely.
 *   NO  Search 1.4 GB for something input-shaped. Three false positives; the
 *       look-alikes are abundant and indistinguishable as data.
 *
 * WHAT IT PRODUCES
 * ----------------
 * The DISTINCT set of return addresses, with a repeat count each -- not a call
 * log. GetAsyncKeyState is called every frame, so a log would be a flood, and a
 * flood is not evidence. What matters is the handful of distinct call sites,
 * which then go straight into the offline riprefs index for disassembly.
 *
 * ⚠⚠ THE TRAP THIS FILE EXISTS TO AVOID: WE ARE A CALLER OURSELVES
 * mod.c calls GetAsyncKeyState at nine sites, every 100 ms (the F7 hotkey and
 * the arrow-key edge capture). Those calls hit this hook like any other, so
 * without a filter the FIRST "input reader" discovered would be us -- a
 * self-inflicted false positive of exactly the kind that has cost this project
 * three prior findings. Callers inside our own module are counted separately
 * and never reported as game call sites.
 *
 * ⭐ That separate count is deliberate, not a discard. If self_calls is zero,
 * the hook is not actually intercepting anything -- we KNOW we call it nine
 * times per second. So self_calls doubles as the hook's own liveness proof:
 * "0 game callers" only means something when self_calls is large.
 *
 * ⚠⚠ A CALLER'S RETURN ADDRESS IS UNTRUSTED
 * It is a value read out of the live stack of a thread we do not control. It is
 * VirtualQuery-checked before being recorded and before any attempt to classify
 * it. See memory hooked-function-pointers-are-untrusted: a detour in this
 * project once dereferenced a caller's pointer and killed the game in under a
 * second.
 *
 * ⚠ THE FIRST CODE IN THIS PROJECT THAT WRITES TO GAME MEMORY
 * Everything shipped so far is read-only. A detour is not, so:
 *   - the OFF switch ships WITH the experiment, not after it bites. Create
 *     `cfc2_hook.off` next to the exe to disable it without rebuilding.
 *   - the hook body takes no locks, allocates nothing, and does no I/O. It
 *     runs on the game's render thread every frame; anything blocking there is
 *     a hitch or a deadlock. Recording is a bounded table plus atomics, and
 *     the log is written later, from our own worker thread.
 */
#ifndef CFC2_CALLER_TRACE_H
#define CFC2_CALLER_TRACE_H

#include <windows.h>

/* Bounded, because the hook must never allocate. Far more than the handful of
 * distinct sites expected -- if it ever fills, that is itself the finding
 * (the table overflowed => the caller set is not small => our model is wrong),
 * so overflow is COUNTED and reported, never silently dropped. */
#define CT_MAX_SITES 64

typedef struct {
    void  *ret;        /* return address: a live code address in the exe */
    LONG   hits;       /* how many times this site called through */
    DWORD  first_ms;   /* GetTickCount at first sighting */
    DWORD  last_ms;
    int    last_vk;    /* the vk argument of the most recent call */
    LONG   distinct_vks; /* bitmap-ish count of how many different vks it asked
                          * about; a real input reader polls MANY keys, a
                          * one-key site is probably a hotkey check */
} ct_site;

typedef struct {
    BOOL  installed;      /* did the detour actually go in? */
    LONG  total_calls;    /* every call seen, ours included */
    LONG  self_calls;     /* calls from inside OUR module -- the liveness proof */
    LONG  foreign_calls;  /* calls from anywhere else */
    LONG  unreadable;     /* return addresses that failed VirtualQuery */
    LONG  overflow;       /* distinct sites that did not fit in the table */
    int   n_sites;
    ct_site sites[CT_MAX_SITES];
} ct_report;

/* Install the detour on user32!GetAsyncKeyState.
 * Returns FALSE if MinHook failed or the target could not be resolved -- in
 * which case nothing was written to game memory and nothing is concluded. */
BOOL ct_install(void);

/* Remove the detour. Safe to call if install failed. */
void ct_remove(void);

/* Snapshot the table. Safe to call from the worker thread while the hook is
 * live; the copy may catch a site mid-update, which costs at most a stale count
 * and never a torn pointer (pointers are published with a release store after
 * their slot is filled). */
void ct_snapshot(ct_report *out);

/* Write the current findings to the log, sorted by hit count. */
void ct_report_log(void);

#endif /* CFC2_CALLER_TRACE_H */
