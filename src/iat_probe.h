/*
 * Which input API does the MTFramework shell actually call?
 *
 * WHY THIS EXISTS
 * ---------------
 * Route 5 proved the shell does NOT read the keyboard through sPad: every one
 * of the five pad records has Kind = 0, so the dispatcher at 0x1401880a0
 * returns immediately and On/Old/Trg/Rel/Chg/Rep stay zero forever. That is a
 * fact about the game, not a tooling limitation -- so the shell reads input
 * some other way, and until we know WHICH way, every route to "which menu item
 * is selected" is a guess.
 *
 * Four offline attempts to answer this failed (see CLAUDE.md). The last one
 * failed for a reason that names the fix: the IAT holds RESOLVED addresses, and
 * user32 sat at a different base when the dump was taken, so no offline value
 * can be matched to a name. The answer needs a RUNTIME fact -- and inside the
 * process, where this mod already runs, it is a dozen lines.
 *
 * WHAT IT DOES
 * ------------
 * Resolve a list of candidate input APIs by name from the live modules, then
 * walk the game's import table and report, for each slot, which named API it
 * points at. The reply is a NAME, not an address, and it distinguishes the
 * three possible worlds:
 *
 *   - GetAsyncKeyState / GetKeyboardState  -> polled keyboard. The shell's
 *     selection is derived from those calls; hook one and read the caller.
 *   - GetRawInputData / RegisterRawInputDevices -> raw input. Different hook,
 *     and the state lives in the game's own decoded buffer.
 *   - PeekMessage / GetMessage / DispatchMessage -> window messages. WM_KEYDOWN
 *     is then the chokepoint and a subclass is enough.
 *   - XInputGetState / DirectInput -> gamepad path; the keyboard is elsewhere.
 *
 * Each of those has a different and much shorter path to the selection than
 * more disassembly does.
 *
 * ⭐ WHY IT WALKS A KNOWN BLOCK RATHER THAN SEARCHING
 * The IAT was located OFFLINE first, from the cross-reference index: 272
 * distinct slots are reached by `call qword ptr [rip+...]`, and they occupy a
 * single tight run 0x140fdc000-0x140fdc900. So the probe reads 289 qwords at a
 * known address instead of scanning 1.4 GB for something import-shaped.
 * Searching memory for a recognisable shape is precisely what produced this
 * project's three false positives; it is not repeated here.
 *
 * ⚠ The block bounds are a CLAIM about this build, so the probe verifies it
 * rather than trusting it: it reports how many slots it examined and how many
 * fell inside loaded modules. "0 matched" and "0 examined" must never look
 * alike -- see the counters in iat_probe_result.
 *
 * Read-only throughout: VirtualQuery-guarded reads, no writes, no hooks, no
 * calls into game code. Nothing here can destabilise the game.
 */
#ifndef CFC2_IAT_PROBE_H
#define CFC2_IAT_PROBE_H

#include <windows.h>

/* The import block, located offline from the riprefs index (see above).
 * Live addresses: ASLR is off and the exe has no relocations. */
#define CFC2_IAT_RVA_LO  0xfdc000
#define CFC2_IAT_RVA_HI  0xfdc908

typedef struct {
    /* ⭐ POSITIVE counts of what was examined, not just what was found.
     * A run reporting "no input API imported" is only meaningful alongside
     * evidence that it actually looked at the slots. */
    int slots_examined;   /* qwords read from the IAT block */
    int slots_readable;   /* of those, actually committed and readable */
    int slots_resolved;   /* of those, pointing inside a loaded module */
    int apis_sought;      /* candidate API names looked up */
    int apis_available;   /* of those, present in a loaded module */
    int matches;          /* IAT slots identified as a candidate API */

    /* Which families were seen. These are the actual finding. */
    BOOL polled_keyboard; /* GetAsyncKeyState / GetKeyState / GetKeyboardState */
    BOOL raw_input;       /* GetRawInputData / RegisterRawInputDevices */
    BOOL window_messages; /* PeekMessage / GetMessage / TranslateMessage */
    BOOL xinput;          /* XInputGetState */
    BOOL directinput;     /* DirectInput8Create */
} iat_probe_result;

/* Run the probe and write a full report to the log. Returns FALSE only if the
 * IAT block could not be read at all (in which case nothing is concluded).
 * Safe to call more than once; the import table does not move. */
BOOL iat_probe_run(iat_probe_result *out);

/* Test-only: point the probe at a synthetic import block instead of the game's.
 * NULL (the default) means "use the real block". This exists so the self-test
 * drives the SHIPPING function rather than a parallel copy of its logic -- a
 * test of a reimplementation proves nothing about the code that runs in-game. */
extern void *g_iat_test_lo;
extern void *g_iat_test_hi;

#endif /* CFC2_IAT_PROBE_H */
