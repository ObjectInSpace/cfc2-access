/*
 * Locate and VERIFY the live GMD message table on the heap.
 *
 * Why the heap: the image dump established that no GMD header, no label
 * strings and no label-hash immediates exist anywhere in the mapped image.
 * The ARC member is decompressed and parsed at load, so the messages the shell
 * displays live only in heap memory at runtime.
 *
 * Why this can be conclusive rather than heuristic: all 4165 (hash1, hash2)
 * pairs are globally unique (verified), so an 8-byte structural match at the
 * right offsets within a 0x20-byte entry already identifies a label. What the
 * corroboration adds is telling the REAL table apart from a stale duplicate
 * buffer, and proving the find by following it all the way to text.
 *
 * Read-only throughout: VirtualQuery, then guarded reads. No writes, no hooks.
 */
#ifndef CFC2_GMD_FIND_H
#define CFC2_GMD_FIND_H

#include <windows.h>

typedef enum {
    GMD_FORM_UNKNOWN = 0,
    GMD_FORM_FILE_IMAGE,   /* verbatim decompressed msg.arc member */
    GMD_FORM_LIVE_TABLE,   /* rebuilt: absolute label pointers */
} gmd_form;

typedef struct {
    BYTE     *array_base;      /* entry[0] of the label array */
    gmd_form  form;
    int       probes_found;    /* how many known labels were located */
    int       probes_total;
    unsigned  derived_stride;  /* measured, not assumed */
    BOOL      geometry_exact;  /* all pairs matched 0x20*(idx_j - idx_i) */

    /* Verification results -- detection alone is not proof. */
    BOOL      label_verified;  /* followed labelOffset, re-hashed, matched */
    BOOL      text_verified;   /* resolved sectionId to the expected string */
    char      sample_label[64];
    char      sample_text[128];

    uintptr_t region_base;
    SIZE_T    region_size;
} gmd_result;

/* Scan for the table. Returns TRUE only if a candidate was found AND its
 * label chain verified -- "found something shaped right" is not success.
 * Logs its reasoning either way, distinguishing a complete scan that found
 * nothing from a truncated or fault-interrupted one. */
BOOL gmd_find(gmd_result *out);

/* --- Reading a message by index --------------------------------------------
 *
 * ⭐ sectionId == the array index (verified for all 4165 labels), and the
 * display string is the sectionId'th NUL-terminated string in the trailing
 * blob. So "give me message N" is a walk of N strings from the blob base --
 * which is exactly what a menu needs: item labels sit in CONTIGUOUS index runs
 * (see memory cfc2-menu-label-runs), so a screen's base index plus the cursor
 * index names the item.
 *
 * ⚠⚠ THE BLOB OFFSET IS NOT STABLE ACROSS RUNS. Measured at entries+0xd0000,
 * +0x140000 and +0x170000 on the same machine and build. It is resolved ONCE
 * per run by gmd_text_ready() -- never hardcoded, because a fixed offset
 * silently read garbage on run 3 and reported failure on a perfectly good
 * table.
 *
 * gmd_text_ready() must succeed before gmd_text_by_index() will return
 * anything. It is idempotent and cheap after the first call. */
BOOL gmd_text_ready(void);

/* Copy message `index` into `out`. Returns FALSE if the table or the blob has
 * not been resolved, if the walk faulted, or if the index is out of range --
 * ⭐ a return value, so "could not read" can never be spoken as if it were the
 * item's name. A caller that cannot tell those apart would announce nonsense
 * to a blind user with total confidence. */
BOOL gmd_text_by_index(int index, char *out, unsigned out_cap);

/* How many messages the resolved table holds (4165 in every shipped locale). */
int gmd_text_count(void);

#endif /* CFC2_GMD_FIND_H */
