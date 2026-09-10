/* Captured GUI text -> the GMD label that produced it. See prompt_id.h. */

#include "prompt_id.h"
#include "prompt_table.h"

#include <string.h>
#include <ctype.h>

int prompt_table_size(void)
{
    return PROMPT_ROW_COUNT;
}

/* Compare two strings ignoring leading/trailing whitespace on BOTH sides.
 *
 * ⚠ Interior whitespace is compared exactly. The Select Game strip contains a
 * deliberate double space ("play.  <ICON ...>") and collapsing runs would make
 * distinct strings compare equal -- widening a matcher until it matches is how
 * this project produced its false positives. */
static BOOL eq_trimmed(const char *a, const char *b)
{
    const char *ae, *be;

    if (!a || !b)
        return FALSE;
    while (*a && isspace((unsigned char)*a)) a++;
    while (*b && isspace((unsigned char)*b)) b++;

    ae = a + strlen(a);
    be = b + strlen(b);
    while (ae > a && isspace((unsigned char)ae[-1])) ae--;
    while (be > b && isspace((unsigned char)be[-1])) be--;

    if ((ae - a) != (be - b))
        return FALSE;
    return memcmp(a, b, (size_t)(ae - a)) == 0;
}

prompt_id prompt_identify(const char *captured)
{
    prompt_id out;
    out.result = PROMPT_ID_NONE;
    out.label  = NULL;
    out.index  = -1;

    if (!captured)
        return out;

    /* A blank or near-blank capture identifies nothing. The corpus's one such
     * placeholder (HELP_BUTTON_SPACE, literally " ") is excluded from the
     * table by the generator; this guards the input side. */
    {
        const char *p = captured;
        int visible = 0;
        while (*p) {
            if (!isspace((unsigned char)*p))
                visible++;
            p++;
        }
        if (visible < 3)
            return out;
    }

    for (int i = 0; i < PROMPT_ROW_COUNT; i++) {
        if (!eq_trimmed(captured, PROMPT_ROWS[i].text))
            continue;

        if (PROMPT_ROWS[i].flags & PROMPT_AMBIGUOUS) {
            /* ⛔ Several screens share this text. Report that; do NOT return
             * the representative label, which would be a confident wrong
             * answer -- and a wrong name is indistinguishable from a right one
             * to a blind user. */
            out.result = PROMPT_ID_AMBIGUOUS;
            out.label  = NULL;
            out.index  = -1;
            return out;
        }

        out.result = PROMPT_ID_FOUND;
        out.label  = PROMPT_ROWS[i].label;
        out.index  = PROMPT_ROWS[i].index;
        return out;
    }
    return out;
}
