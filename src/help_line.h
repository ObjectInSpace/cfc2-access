/* help_line.h -- the shell's help line, as the MESSAGE the game sets.
 *
 * Every shell screen sets its bottom help line ("Play offline.", "Change key
 * bindings.") with 0x140051b30(manager, message index): the refresh routine
 * picks the message for the focused row, the setter looks it up in the same
 * message table the menus use (0x140054cf0) and draws it. Hooking the setter
 * gives the row's description exactly, in the current language, at the moment
 * the game chooses it -- replacing the caption channel's corpus match and
 * movement-stamp attribution.
 *
 * ⚠ The hook records the message index and a sequence number, nothing else.
 * It dereferences no pointer; the text is looked up later on the worker. */
#ifndef HELP_LINE_H
#define HELP_LINE_H

#include <windows.h>

#define HELP_LINE_SETTER_ADDR 0x140051b30ULL

BOOL help_line_install(void);
BOOL help_line_installed(void);

/* The latest help message index and the number of times one has been set.
 * FALSE before the first set. */
BOOL help_line_latest(int *msg, LONG *seq);

/* Test seam: record a set as the hook would. */
void help_line_test_set(int msg);
void help_line_test_reset(void);

#endif
