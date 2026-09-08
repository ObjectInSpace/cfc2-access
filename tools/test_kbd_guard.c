/*
 * Does kbd_install() REFUSE when the game is not there?
 *
 * ⭐⭐ WHY THIS TINY TEST IS WORTH HAVING
 * This is the first hook this project puts on GAME code rather than a system
 * API, and it is placed at a hard-coded address derived offline. The dangerous
 * failure is not "the hook does not work" -- it is the hook going in at an
 * address that is NOT the function we think it is, on some future build or
 * before Enigma has unpacked. That writes a detour into whatever happens to be
 * there and the game executes it.
 *
 * kbd_install() therefore checks three things before writing anything:
 *   1. the target is committed, executable memory;
 *   2. the vtable slot 0x14104e718+0x30 still holds the update function;
 *   3. the class name at 0x14104e6f8 still reads "sKeyboard".
 *
 * In this harness NONE of those addresses are mapped -- there is no game. So a
 * correct kbd_install() must return FALSE and write nothing. That makes this a
 * genuine negative test: the guard is the only thing standing between a wrong
 * address and a patched byte, and here it is exercised for real rather than
 * simulated.
 *
 * ⚠ A passing run means "refused as designed", NOT "the hook works". Only an
 * in-game run can show the positive case, which is exactly why the guard has to
 * be trustworthy on its own.
 */

#include <windows.h>
#include <stdio.h>

#include "../src/kbd_state.h"
#include "../src/log.h"

int main(void)
{
    int failures = 0;
    printf("kbd_install guard test (no game present)\n\n");

    BOOL ok = kbd_install();
    if (!ok) {
        printf("  ok    kbd_install() REFUSED with no game mapped\n");
    } else {
        printf("  FAIL  kbd_install() hooked something with no game mapped\n");
        failures++;
    }

    /* ⭐⭐ THE ASSERTION THAT ACTUALLY TESTS THE GUARD.
     *
     * The check above is nearly worthless on its own: I deleted all three
     * guards and it still passed, because MinHook independently refuses an
     * unmapped address. The refusal was right; the assertion was not evidence.
     *
     * So require the reason. Our OWN protection check must be what rejected
     * this address -- if the answer is MINHOOK_FAILED, the guards did not run
     * and the thing standing between a wrong address and a patched byte is
     * MinHook's own validation, which is not what we designed for.
     *
     * SABOTAGE (verified): delete the VirtualQuery block and this flips to
     * MINHOOK_FAILED -> this case fails while the one above still passes. That
     * asymmetry is the whole point. */
    kbd_install_status why = kbd_install_why();
    static const char *names[] = {
        "NOT_TRIED", "OK", "BAD_PROTECT", "BAD_VTABLE", "BAD_NAME",
        "MINHOOK_FAILED"
    };
    printf("        (install status: %s)\n",
           why <= KBD_INSTALL_MINHOOK_FAILED ? names[why] : "?");
    if (why == KBD_INSTALL_BAD_PROTECT) {
        printf("  ok    OUR protection guard rejected it, not MinHook\n");
    } else {
        printf("  FAIL  expected BAD_PROTECT (our own guard); got %s -- the "
               "identity guards are not what is protecting this address\n",
               why <= KBD_INSTALL_MINHOOK_FAILED ? names[why] : "?");
        failures++;
    }

    /* Nothing captured, so a read must report failure rather than zeros.
     * ⭐ "could not measure" is a RETURN VALUE; all-zero banks would be
     * indistinguishable from "no keys are down". */
    DWORD w[5][KBD_BANK_WORDS];
    if (!kbd_read(w)) {
        printf("  ok    kbd_read() returns FALSE when nothing was captured\n");
    } else {
        printf("  FAIL  kbd_read() reported success with no state pointer\n");
        failures++;
    }

    kbd_capture cap;
    kbd_snapshot(&cap);
    if (!cap.installed && cap.calls == 0 && cap.state == NULL) {
        printf("  ok    snapshot reports not-installed and nothing captured\n");
    } else {
        printf("  FAIL  snapshot claims state it cannot have\n");
        failures++;
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
