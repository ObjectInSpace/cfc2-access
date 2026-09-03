/*
 * Offline self-test for the GUI layout finder.
 *
 * Plants a REAL layout's name pool (extracted from the shipped menu.arc) in
 * this process's heap and requires gui_find() to locate it AND name the right
 * screen. Without this, "no layout identified" in the game would be
 * indistinguishable from a scanner that cannot find anything at all.
 *
 * Four checks, because each catches a different way of being wrong:
 *
 *  1. POSITIVE   -- plant main_menu_2's pool; the finder must name main_menu_2.
 *  2. DISCRIMINATION -- plant a DIFFERENT layout; the finder must name THAT
 *     one. A detector that always answers "main_menu_2" passes check 1.
 *  3. SUBSTRING TRAP -- plant `result`'s pool, which contains the texture path
 *     "ui\1_menu\14_netWork\tex\icon_status_btl_BM_NOMIP". A bare (undelimited)
 *     search for lobby_2's anchor matches inside it. The finder must still say
 *     `result`, never lobby_2. This is the bug the delimited search exists to
 *     prevent, and only a planted trap can prove it stays fixed.
 *  4. NEGATIVE   -- wipe the plant; the finder must report absence, not a
 *     leftover answer.
 *
 * Build+run:  see tools/run_gui_test.ps1
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>

#include "../src/gui_find.h"
#include "../src/gui_probes.h"

void log_init(void) {}
void log_shutdown(void) {}
void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

static int failures;

static void check(const char *what, int ok)
{
    printf("  %-38s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

/* Load a raw pool blob dumped by tools/dump_gui_pool.py. */
static BYTE *load(const char *path, SIZE_T *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    BYTE *buf = (BYTE *)VirtualAlloc(NULL, (SIZE_T)len + 0x1000,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!buf) { fprintf(stderr, "alloc failed\n"); exit(2); }

    /* ⚠ Read UNBUFFERED. fread() copies through the CRT's internal FILE
     * buffer, which lives on the process heap and still holds the pool bytes
     * after fclose(). The scanner then finds that leftover copy and the
     * negative control fails -- observed exactly that, and it was the TEST
     * leaking, not the finder misbehaving.
     *
     * Worth noting what it demonstrates: corroboration does NOT distinguish a
     * live pool from a stale copy of one. Both carry all the same names. */
    setvbuf(f, NULL, _IONBF, 0);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "short read\n"); exit(2);
    }
    fclose(f);
    *len_out = (SIZE_T)len;
    return buf;
}

static void run_case(const char *label, const char *path,
                     const char *expect_layout)
{
    SIZE_T len;
    BYTE *plant = load(path, &len);
    printf("\n---- %s: planted %llu bytes at %p, expecting \"%s\" ----\n",
           label, (unsigned long long)len, (void *)plant, expect_layout);

    gui_result r;
    BOOL ok = gui_find(&r);

    printf("  found=%d layout=%s corroborating=%d/%d states=%d\n",
           (int)ok, r.layout_name ? r.layout_name : "(none)",
           r.corroborating, r.corroborating_of, r.states_found);

    check("gui_find returned success", ok);
    check("named the RIGHT layout",
          ok && r.layout_name && strcmp(r.layout_name, expect_layout) == 0);
    check("corroborated by >= 2 names", ok && r.corroborating >= 2);
    check("scan examined something", r.bytes_scanned > 0);

    /* Negative control for this plant: wipe it and require absence. */
    memset(plant, 0, len);
    VirtualFree(plant, 0, MEM_RELEASE);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <main_menu_2.pool> <option_2.pool> <result.pool>\n",
                argv[0]);
        return 2;
    }

    printf("=== GUI finder self-test ===\n");
    printf("%d layouts in the probe table\n", GUI_NLAYOUTS);

    run_case("POSITIVE", argv[1], "main_menu_2");
    run_case("DISCRIMINATION", argv[2], "option_2");

    /* The substring trap. `result` contains lobby_2's anchor as the tail of a
     * texture path; answering "lobby_2" here means the delimited search has
     * regressed to a bare one. */
    run_case("SUBSTRING TRAP", argv[3], "result");

    /* ⚠ The end-to-end trap above is NOT sufficient on its own.
     *
     * Measured: deliberately rewriting gui_find_delimited() as a bare memcmp
     * left every check above PASSING, because `result` matches 6/6 of its own
     * corroborators and so outscores lobby_2's lone anchor hit regardless.
     * A trap that cannot fail proves nothing, so the delimiting is asserted
     * DIRECTLY here on the exact bytes that collide. */
    printf("\n---- SUBSTRING TRAP (direct) ----\n");
    {
        SIZE_T len;
        BYTE *pool = load(argv[3], &len);   /* result's pool */
        const char *victim = "icon_status_btl_BM_NOMIP";

        /* The bytes ARE present -- as the tail of a texture path. */
        BOOL bare_present = FALSE;
        SIZE_T n = strlen(victim);
        for (SIZE_T i = 0; i + n <= len; i++) {
            if (memcmp(pool + i, victim, n) == 0) { bare_present = TRUE; break; }
        }
        check("the colliding bytes really are present", bare_present);

        /* ...but not as a whole NUL-delimited name. */
        check("delimited search REJECTS the substring",
              gui_find_delimited(pool, len, victim) == (SIZE_T)-1);

        /* Positive control on the same function: a real name IS found, so a
         * function that always returned "absent" cannot pass this pair. */
        check("delimited search still finds a real name",
              gui_find_delimited(pool, len, "a_child_icon_rank_new")
                  != (SIZE_T)-1);

        memset(pool, 0, len);
        VirtualFree(pool, 0, MEM_RELEASE);
    }

    /* MULTI-RESIDENCY. In-game, `command` was reported while the player was in
     * ordinary menus -- correctly: its pool really was in memory. The shell
     * keeps several layouts loaded, so the finder must ENUMERATE them, not
     * answer with the first hit and stop. Plant two at once and require both.
     */
    printf("\n---- MULTI-RESIDENCY: two layouts planted at once ----\n");
    {
        SIZE_T l1, l2;
        BYTE *a = load(argv[1], &l1);   /* main_menu_2 */
        BYTE *b = load(argv[2], &l2);   /* option_2    */

        gui_result r;
        gui_find(&r);
        printf("  nresident=%d ninteractive=%d best=%s\n",
               r.nresident, r.ninteractive,
               r.layout_name ? r.layout_name : "(none)");

        int saw_main = 0, saw_opt = 0;
        for (int i = 0; i < r.nresident; i++) {
            if (strcmp(r.resident[i].name, "main_menu_2") == 0) saw_main = 1;
            if (strcmp(r.resident[i].name, "option_2") == 0)    saw_opt = 1;
        }
        check("found BOTH planted layouts", saw_main && saw_opt);
        check("reported >= 2 resident", r.nresident >= 2);
        /* The ranking must not silently drop the others. */
        check("best pick is one of the planted", r.layout_name &&
              (strcmp(r.layout_name, "main_menu_2") == 0 ||
               strcmp(r.layout_name, "option_2") == 0));

        memset(a, 0, l1); memset(b, 0, l2);
        VirtualFree(a, 0, MEM_RELEASE);
        VirtualFree(b, 0, MEM_RELEASE);
    }

    printf("\n---- NEGATIVE control (nothing planted) ----\n");
    {
        gui_result r;
        BOOL ok = gui_find(&r);
        printf("  found=%d layout=%s scanned=%llu KB faults=%ld\n",
               (int)ok, r.layout_name ? r.layout_name : "(none)",
               (unsigned long long)(r.bytes_scanned / 1024),
               r.faults_absorbed);
        check("reports absence with nothing planted", !ok);
        check("still reports a positive scan count", r.bytes_scanned > 0);
    }

    printf("\n%s (%d failures)\n",
           failures ? "FAILURES" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
