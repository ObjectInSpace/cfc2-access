#ifndef CFC2_MOD_TEXT_H
#define CFC2_MOD_TEXT_H
/* The words the mod itself speaks, in the collection's 14 menu languages.
 *
 * The game's own text (menus, ROM text) is read as it is; this covers only
 * the words the MOD supplies -- positions, select-screen labels, cue names,
 * key messages. They follow the language set in Options > System, detected
 * by gmd_text_language().
 *
 * Two sources:
 *   - words the collection already translates are copied from its message
 *     tables by tools/make_mod_text_gmd.py (src/mod_text_gmd.h), so they match
 *     what the game shows;
 *   - the rest are the mod's own translations (mod_text.c), best effort,
 *     corrected from player reports.
 *
 * Templates use {0}, {1}, {2} so each language can order its words. Names of
 * fighters, grooves, isms and stages are proper nouns and stay as the game
 * names them. Worker thread only, except mt() which any thread may read. */
#include <windows.h>
#include <stddef.h>

/* Language order everywhere in this module (the msg.arc table names). */
#define MT_LANGS { "jpn", "eng", "fre", "ita", "ger", "spa", "oth", "kor", \
                   "pol", "por", "rus", "chT", "chS", "ara" }
#define MT_NLANG 14
#define MT_ENGLISH 1

typedef enum {
    /* From the collection's own text (mod_text_gmd.h). */
    MT_NORMAL, MT_TURBO, MT_RANDOM, MT_OPPONENT, MT_PLAYER_N, MT_STAGE_PREFIX,
    /* Menu positions. */
    MT_POS,            /* "{0} of {1}"            */
    MT_ROW_N,          /* "row {0}"               */
    MT_LEVEL,          /* "{0}, of {1}": a slider's value and maximum */
    MT_OR,             /* "{0} or {1}"            */
    /* Select screens. */
    MT_RANDOM_STAGE,   /* "Random stage"          */
    MT_TEAM_RATIO,     /* "Team ratio {0} of {1}" */
    MT_RATIO,          /* "Ratio {0} of {1}"      */
    MT_MEMBER_RATIO,   /* "{0}, ratio {1}"        */
    MT_MEMBER_RATIO_LIST, /* "{0} ratio {1}": inside a comma-separated team list */
    MT_RATIOS,         /* "Ratios: "              */
    MT_LEVEL_N,        /* "Level {0}"             */
    MT_MODE,           /* "{0} mode"              */
    MT_MODE_NORMAL, MT_MODE_MAZI, MT_MODE_SAIKYO, MT_MODE_CLASSICAL,
    MT_ORDER,          /* "{0} {1}": fighter, place */
    MT_FIRST, MT_SECOND, MT_THIRD, MT_FOURTH,
    MT_YOU,
    MT_STORY_MODE, MT_FREE_MODE,   /* Project Justice's first menu */
    /* Audio cue volume keys. */
    MT_CUE_HEALTH, MT_CUE_SUPER, MT_CUE_BEACONS, MT_CUE_TICK,
    MT_MUTED,          /* "{0} muted"             */
    MT_PERCENT,        /* "{0} {1} percent"       */
    /* Startup. */
    MT_READY,          /* "Access mod ready. Backend {0}." */
    MT_NOT_UNPACKED,
    MT_NONE,
    /* The read-current-text key. */
    MT_TEXT_NO_READER, MT_TEXT_NOTHING, MT_TEXT_STALE, MT_TEXT_INACTIVE,
    MT_COUNT
} mt_id;

/* The text for `id` in the current language (English when unknown). */
const wchar_t *mt(mt_id id);
/* The same in a given language index (tests). */
const wchar_t *mt_in(mt_id id, int lang);
/* Fill {0}..{2} of `id`'s template. Missing arguments read as empty. */
void mt_fmt(wchar_t *out, size_t cap, mt_id id,
            const wchar_t *a0, const wchar_t *a1, const wchar_t *a2);
/* Convenience: {0} and {1} as numbers. */
void mt_fmt_n(wchar_t *out, size_t cap, mt_id id, int n0, int n1);
/* UTF-8 in, UTF-8 out, for the narrow menu speech path. */
void mt_fmt8(char *out, size_t cap, mt_id id,
             const char *a0, const char *a1, const char *a2);

/* Worker: follow the game's language (mod.c passes gmd_text_language()
 * about once a second). NULL or unknown keeps the current language. */
void mt_update_language(const char *code);
/* Index of a language code, or -1. Test seam: mt_set_language(code). */
int mt_language_index(const char *code);
void mt_set_language(const char *code);
int mt_language(void);
#endif
