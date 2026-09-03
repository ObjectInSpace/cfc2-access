/*
 * Locate the live GUI layout in memory and identify WHICH screen it is.
 *
 * Why this exists: we can already READ the shell's message text (gmd_find),
 * but not which item is focused. The shell's menu text is bound at RUNTIME --
 * a layout resource contains no GMD label hashes (verified: 0 matches across
 * all 4165 labels) -- so the focused item has to come from the GUI node tree,
 * not from the message file.
 *
 * What is anchored on: every layout carries a contiguous NUL-separated pool of
 * node and animation-state names, and we know those exactly from the shipped
 * menu.arc. `gui_probes.h` gives each layout a name that occurs in it and in
 * no other layout, so finding that string identifies the SCREEN.
 *
 * ⚠ Uniqueness is a BYTE property, not a name property. "icon_status_btl_
 * BM_NOMIP" is a parsed name only in lobby_2, but the same bytes appear in
 * `result` inside the texture path "ui\1_menu\14_netWork\tex\icon_status_btl_
 * BM_NOMIP". Every search here is therefore for the NUL-DELIMITED form
 * (\0name\0), which is how a name actually sits in the pool. gen_gui_probes.py
 * asserts the same property when it emits the header.
 *
 * Read-only throughout: VirtualQuery, then guarded reads. No writes, no hooks.
 * This is deliberate -- the hooking route (hash_watch) crashed the game once,
 * and a scanner cannot.
 */
#ifndef CFC2_GUI_FIND_H
#define CFC2_GUI_FIND_H

#include <windows.h>

/* ⛔ THIS CANNOT TELL YOU WHICH SCREEN IS DISPLAYED. Measured, not feared.
 *
 * In-game 2026-09-02: **24 of the 25 layouts are resident simultaneously**,
 * every one matching 6/6 corroborating names. The whole of menu.arc is loaded
 * up front, so "this layout's names are in memory" is a CONSTANT and carries
 * no information about what is on screen.
 *
 * The focus-state counts do not rescue it: the counts seen live match each
 * layout's STATIC count parsed from the shipped file (command 1,
 * main_menu_2 7, title_2 2). This scan re-reads file content out of memory;
 * it never observes live state.
 *
 * So this is an INVENTORY, not a screen detector. It is still useful for
 * confirming a layout is in memory and locating its pool as an entry point
 * into the instantiated GUI tree. Detecting the displayed screen needs state
 * that CHANGES -- per-node animation-state fields in the instantiated tree, or
 * the tree's current-screen pointer. Do not build screen announcements on the
 * ranking below; it ranks a property of the FILE. */
#define GUI_MAX_RESIDENT 32

typedef struct {
    int         index;           /* into g_gui_layouts */
    const char *name;
    BYTE       *pool_base;
    int         corroborating;
    int         corroborating_of;
    int         states_found;    /* focus-state names present in this pool */
    int         interactive;
} gui_resident;

typedef struct {
    BOOL      found;
    int       layout_index;      /* best candidate, -1 if none */
    const char *layout_name;     /* NULL if unidentified */

    /* Everything resident, not just the first hit. */
    gui_resident resident[GUI_MAX_RESIDENT];
    int          nresident;
    int          ninteractive;   /* how many of them model SELECT/DECIDE */
    BYTE     *pool_base;         /* first byte of the name pool in memory */
    SIZE_T    pool_span;         /* how far the pool runs, measured */

    /* Corroboration: finding ONE string proves very little. A real pool holds
     * many of the layout's names close together. */
    int       corroborating;     /* other names from the SAME layout found */
    int       corroborating_of;  /* how many were looked for */

    /* The focus-state names actually present near the anchor. Their presence
     * is what makes the layout interactive in practice, not just on paper. */
    int       states_found;

    uintptr_t region_base;
    SIZE_T    region_size;
    LONG      faults_absorbed;   /* a scan that faulted was PARTIAL */
    int       regions_scanned;   /* POSITIVE count of what was examined */
    SIZE_T    bytes_scanned;
} gui_result;

/* Scan for a loaded layout's name pool.
 *
 * Returns TRUE only if an anchor was found AND corroborated by other names
 * from the same layout -- "one string matched" is not success. Logs a
 * positive count of what was examined either way, so "nothing loaded yet"
 * can never be confused with "the scanner did not look".
 *
 * The shell loads and frees layouts as screens change, so a miss is a normal
 * transient result, not an error. Call it again. */
BOOL gui_find(gui_result *out);

/* Identify a layout from a name pool already located, e.g. in a test.
 * Exposed so the offline harness can drive the classifier directly. */
int gui_identify_pool(const BYTE *pool, SIZE_T span, int *corroborating,
                      int *looked_for);

/* Find the NUL-DELIMITED form of `name` in a buffer; (SIZE_T)-1 if absent.
 *
 * Exposed so the harness can test the delimiting DIRECTLY. Testing it only
 * through gui_identify_pool() is not enough: corroboration scoring makes the
 * right layout win anyway, so a regression to a bare substring search still
 * passes an end-to-end trap. Verified by deliberately breaking this function
 * and watching the end-to-end test keep passing. */
SIZE_T gui_find_delimited(const BYTE *buf, SIZE_T len, const char *name);

#endif /* CFC2_GUI_FIND_H */
