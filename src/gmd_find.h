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

/* Synchronous OFFLINE diagnostic; do not call on the output worker.
 * Scan for the table. Returns TRUE only if a candidate was found AND its
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
 * anything. Call repeatedly from the serialized worker: unresolved calls
 * retain progress, copy at most four 64-KB chunks or inspect one language
 * candidate, and return FALSE while pending. Completed unsuccessful passes
 * retry after one second. It is idempotent and cheap once resolved. */
BOOL gmd_text_ready(void);

/* Copy message `index` into `out`. Returns FALSE if the table or the blob has
 * not been resolved, if the walk faulted, or if the index is out of range --
 * ⭐ a return value, so "could not read" can never be spoken as if it were the
 * item's name. A caller that cannot tell those apart would announce nonsense
 * to a blind user with total confidence. */
BOOL gmd_text_by_index(int index, char *out, unsigned out_cap);

/* How many messages the resolved table holds (4165 in every shipped locale). */
int gmd_text_count(void);

/* Test-only seams: plant a synthetic blob so the index can be exercised
 * offline, against the SHIPPING lookup rather than a copy of it. */
void gmd_test_set_blob(BYTE *blob, int count);
int  gmd_test_label_count(void);

/* --- Which LANGUAGE is the resolved table? --------------------------------
 *
 * ⛔⛔ THE BUG THIS EXISTS FOR. The verifier accepted ANY shipped language
 * variant -- it proved "this is a valid GMD table" and never asked "is this the
 * language the game is currently displaying". The user switched the game to
 * English and the mod kept speaking French, because a stale French table was
 * still resident and the scan found it first. Nothing failed; the wrong answer
 * was simply confident.
 *
 * ⭐ Every shipped table names ITSELF uniquely at index 0 -- "Japanese [jp]"
 * in English, "Japonais [jp]" in French, and so on for all 14. So a table can
 * be asked which language it is, with no disassembly and no live-language
 * lookup.
 *
 * Returns a short code ("eng", "fre", ...) or NULL if index 0 matches no
 * shipped table -- which itself means the resolved table is not one we
 * recognise, and must be reported rather than used. */
const char *gmd_text_language(void);

/* ⭐⭐ Re-resolve the string blob if the game's language has changed since it
 * was resolved. Returns TRUE only when a re-resolve actually happened.
 *
 * ⛔ Without this, a language selected in Options > System was never followed:
 * the blob is resolved once and gmd_text_ready() returns early thereafter, so
 * every item name kept coming from the OLD language while every structural
 * check passed (label hashes are language-independent).
 *
 * ⚠ Cheap in the common case -- one indexed read of string 0 plus a config.ini
 * read; only a genuine disagreement pays for the full re-search. A state it
 * cannot read changes NOTHING: "unknown" must never be treated as "changed". */
BOOL gmd_text_recheck_language(void);

/* Test seam: stand in for config.ini, which does not exist beside the test exe.
 * ⚠ NULL restores the real read. Without this a re-check case cannot reach the
 * comparison at all -- it returns early on "no expected language" and passes
 * whatever the code under test does. */
void gmd_test_set_expected_language(const char *code);

/* Test seam: point the live-language read at a stand-in for the app singleton
 * slot (0x141c14f90 in the game). NULL restores the real address.
 *
 * ⭐ The live read outranks config.ini: [app+0x40] is the index the game's
 * message system selects its table with (traced in gmd_find.c), so it is what
 * the screen shows. config.ini is only consulted when it cannot be read. */
void gmd_test_set_app_slot(void *const *slot);

/* Drop the cached blob and index so the next call re-resolves. Use when the
 * game may have rebuilt its table -- a language change does exactly that. */
void gmd_text_invalidate(void);

#endif /* CFC2_GMD_FIND_H */
