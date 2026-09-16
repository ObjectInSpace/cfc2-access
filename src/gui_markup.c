/* Markup -> speech for the shell's live GUI text. See gui_markup.h. */

#include "gui_markup.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ---------------------------------------------------------------------------
 * The ICON token table.
 *
 * ⭐ GENERATED from tools/markup.py's verified vocabulary, not hand-typed --
 * that translator derives the game-specific attack names from the game's OWN
 * legend strings (HELP_MOVES_* / HELP_EXMOVE_*), so these are Capcom's English
 * names rather than invented ones.
 *
 * ⚠ The C_* attack rows carry the GENERIC reading. C_A is "Any Attack" in
 * Cybots but "Vertical Slash" in Plasma Sword, and the shell has no game
 * context at the point this runs. The per-game table stays offline in
 * markup.py, where a caller that knows the game can use it. For the shell's
 * menus -- which is all this module is wired to -- the icons that actually
 * appear are the UI ones (DECIDE, CANCEL, BTN_*), and those are unambiguous.
 *
 * ⚠ The single-character KB_ tokens (KB_A, KB_5, KB_F10) are handled by the
 * mechanical rule in gm_icon() instead of taking 40 rows here.
 * ------------------------------------------------------------------------- */
struct icon_row { const char *token; const char *say; };

static const struct icon_row ICONS[] = {
    { "BTN_A", "A" },
    { "BTN_B", "B" },
    { "BTN_LB", "LB" },
    { "BTN_LT", "LT" },
    { "BTN_RB", "RB" },
    { "BTN_RT", "RT" },
    { "BTN_SELECT", "Select" },
    { "BTN_START", "Start" },
    { "BTN_X", "X" },
    { "BTN_Y", "Y" },
    { "CANCEL", "Cancel" },
    { "CFG_A", "A" },
    { "CFG_B", "B" },
    { "CFG_L", "L" },
    { "CFG_R", "R" },
    { "CFG_X", "X" },
    { "CFG_Y", "Y" },
    { "C_2", "down" },
    /* ⭐⭐ NUMPAD MOTION NOTATION, added 2026-09-11 for the in-game MOVE LIST.
     *
     * The move list draws its inputs as these tokens -- Hadoken is
     * `<ICON C_236>+<ICON C_P>` -- and nine of them were UNKNOWN, so a blind
     * player got the move NAME and nothing about how to perform it. That is the
     * half of the screen that matters.
     *
     * ⚠ The digits are keypad directions (1=down-back ... 9=up-forward), the
     * standard fighting-game notation, so the reading is mechanical rather than
     * a guess: 236 is down, down-forward, forward.
     *
     * ⭐ Spoken as the MOTION a player performs, not as digits. "quarter circle
     * forward" is what a move list or a human would say; "two three six" would
     * be a transcription of the glyph rather than a reading of it. A sighted
     * player sees an arc, not three numbers.
     *
     * ⚠ Not in the GMD: these tokens appear in ZERO corpus strings, so the
     * corpus cannot confirm them. Numpad notation is unambiguous and
     * universal in this genre, which is why it is safe to name mechanically --
     * but it is derived from the notation, not measured from the game. */
    { "C_3", "down-forward" },
    { "C_28", "down then up" },
    { "C_2T", "charge down" },
    { "C_236", "quarter circle forward" },
    { "C_214", "quarter circle back" },
    { "C_623", "dragon punch" },
    { "C_421", "reverse dragon punch" },
    { "C_41236", "half circle forward" },
    { "C_63214", "half circle back" },
    { "C_4", "back" },
    { "C_4T", "charge back" },
    { "C_5", "neutral" },
    { "C_6", "forward" },
    { "C_8", "up" },
    /* ⭐⭐ EIGHT MORE TOKENS THAT WERE REACHING THE USER UNTRANSLATED. Surveyed
     * the live captures: 31 distinct C_* tokens appear in the move list's x=633
     * column and these 8 had no entry, so a row spoke "During Jump [C_236]
     * +punch" -- a literal bracketed token where the input belongs. That is the
     * worst place to leak one, because the input IS what the player came for.
     *
     * Read as motions, per the convention above -- a sighted player sees an arc,
     * not digits. Numpad directions: 1 down-back, 2 down, 3 down-forward,
     * 4 back, 6 forward, 8 up, 9 up-forward. `T` = charge, as C_2T/C_4T show.
     *
     * ⚠ Derived from the notation, not measured from the corpus (these appear in
     * ZERO GMD strings, like every C_* motion above), so the parity checker's
     * corpus inputs cannot exercise them -- the LIVE cases in
     * check_markup_parity.py are what pins them. */
    { "C_1", "down-back" },
    { "C_1T", "charge down-back" },
    { "C_23", "down then down-forward" },
    { "C_319", "down-forward, back, up-forward" },
    { "C_412", "back, down-back, down" },
    { "C_6248", "forward, down, down-back, up" },
    /* ⚠ These two are read as the LITERAL SWEEP, not given a genre nickname.
     * C_2141236 is 2-1-4-1-2-3-6 and C_2363214 is 2-3-6 then 3-2-1-4; neither
     * has a settled common name, and inventing one ("double quarter circle")
     * would be a claim about the input rather than a reading of it. A player can
     * execute a described sweep; a wrong nickname sends them to training mode. */
    { "C_2141236", "down, back, down, forward" },
    { "C_2363214", "quarter circle forward then half circle back" },
    { "C_A", "Any Attack" },
    { "C_A1", "Attack 1" },
    { "C_A2", "Attack 2" },
    { "C_AC", "Action" },
    { "C_AT", "Attack" },
    { "C_B", "Boost" },
    { "C_EX", "super" },
    { "C_HK", "heavy kick" },
    { "C_HP", "heavy punch" },
    { "C_J", "Jump" },
    { "C_J1", "Jump" },
    { "C_K", "kick" },
    { "C_K2", "Kick" },
    { "C_K3", "Kick" },
    { "C_LK", "light kick" },
    { "C_LP", "light punch" },
    { "C_M", "Side Step" },
    { "C_MK", "medium kick" },
    { "C_MP", "medium punch" },
    { "C_P", "punch" },
    { "C_P2", "Punch" },
    { "C_P3", "Punch" },
    { "C_S2", "Special" },
    { "C_SP", "special move" },
    { "C_START", "Start" },
    { "C_W", "Weapon" },
    { "DECIDE", "Confirm" },
    { "D_KEY", "d-pad" },
    { "JCN_A", "A" },
    { "JCN_B", "B" },
    { "JCN_L", "L" },
    { "JCN_LS", "left stick" },
    { "JCN_R", "R" },
    { "JCN_X", "X" },
    { "JCN_Y", "Y" },
    { "KB_ARROWS", "arrow keys" },
    { "KB_BACK", "Backspace" },
    { "KB_DELETE", "Delete" },
    { "KB_DOWN", "Down Arrow" },
    { "KB_END", "End" },
    { "KB_ESCAPE", "Escape" },
    { "KB_HOME", "Home" },
    { "KB_INSERT", "Insert" },
    { "KB_LCONTROL", "Left Ctrl" },
    { "KB_LEFT", "Left Arrow" },
    { "KB_LMENU", "Left Alt" },
    { "KB_LSHIFT", "Left Shift" },
    { "KB_NEXT", "Page Down" },
    { "KB_PRIOR", "Page Up" },
    { "KB_RCONTROL", "Right Ctrl" },
    { "KB_RETURN", "Enter" },
    { "KB_RIGHT", "Right Arrow" },
    { "KB_RMENU", "Right Alt" },
    { "KB_RSHIFT", "Right Shift" },
    { "KB_SPACE", "Space" },
    { "KB_TAB", "Tab" },
    { "KB_UP", "Up Arrow" },
    { "KEY_SUP", "" },
    { "LSB", "left stick" },
    { "LSRS", "both sticks" },
    { "RSB", "right stick" },
};

#define ICON_COUNT (sizeof(ICONS) / sizeof(ICONS[0]))

/* ⭐⭐ THE PER-GAME OVERRIDES, LEARNED FROM THE SCREEN'S OWN LEGEND.
 *
 * ⛔⛔ THE BUG THIS FIXES. Three tokens mean different attacks in different
 * games -- C_A is "Any Attack" in Cybots and "Vertical Slash" in Star
 * Gladiator; C_EX has NINE readings. The static table below carries the generic
 * one, and its comment argued that was safe because "the icons that actually
 * appear [in shell menus] are the UI ones". Measured in the user's sweep, that
 * is FALSE: C_A appeared live 8 times, C_B 3, C_EX 4. So the move list said
 * "Any Attack" where the screen showed Vertical Slash -- Cybots' name spoken on
 * a different game, which is a confident wrong reading, not a gap.
 *
 * ⭐ THE GAME DRAWS ITS OWN LEGEND on that screen: HELP_MOVES_STG2 is
 * "<ICON C_A>Vertical Slash<ICON C_B>Horizontal Slash…", and it is co-resident
 * with the moves in the instance set (verified: one report block held the
 * legend and all nine ambiguous uses). So the right names are READ, never
 * guessed, and they follow the game's language for free. "The game draws the
 * words; quote them."
 *
 * ⚠ Bounded and overwritten wholesale on each new legend: a stale override from
 * the previous game would be exactly the bug being fixed, one game late. */
#define GM_OVERRIDE_MAX 16
#define GM_OVERRIDE_TOK 16
#define GM_OVERRIDE_SAY 40

static struct {
    char token[GM_OVERRIDE_TOK];
    char say[GM_OVERRIDE_SAY];
} g_override[GM_OVERRIDE_MAX];
static int g_override_n;

void gm_icon_overrides_clear(void)
{
    g_override_n = 0;
}

BOOL gm_icon_override_add(const char *token, const char *say)
{
    if (!token || !*token || !say || !*say)
        return FALSE;
    if (g_override_n >= GM_OVERRIDE_MAX)
        return FALSE;
    /* ⚠ A token already present is REPLACED, not duplicated: a legend that
     * names the same token twice must not leave the first reading reachable. */
    for (int i = 0; i < g_override_n; i++) {
        if (strcmp(g_override[i].token, token) == 0) {
            lstrcpynA(g_override[i].say, say, GM_OVERRIDE_SAY);
            return TRUE;
        }
    }
    lstrcpynA(g_override[g_override_n].token, token, GM_OVERRIDE_TOK);
    lstrcpynA(g_override[g_override_n].say, say, GM_OVERRIDE_SAY);
    g_override_n++;
    return TRUE;
}

int gm_icon_override_count(void) { return g_override_n; }

/* ⭐⭐ Read a legend string into the override table.
 *
 * A legend pairs each icon with its name in THIS game:
 *     "<SIZE 58><ICON C_A></SIZE>Vertical Slash<SIZE 58><ICON C_B></SIZE>..."
 * so the rule is: a token, then the plain text up to the next tag, trimmed.
 *
 * ⛔ IT MUST DEFINE AT LEAST TWO TOKENS TO COUNT. A single "<ICON X>word" is
 * how ordinary move rows are written ("Hold <ICON C_S2> and"), and treating one
 * of those as a legend would let a MOVE's incidental wording redefine a token
 * for the whole screen -- the same class of error as the bug being fixed. The
 * real legends define 2-5 tokens in one string; HELP_EXMOVE_* define exactly
 * one, so they are deliberately NOT accepted here and C_EX keeps its generic
 * reading until a two-token legend names it.
 *
 * ⚠ Returns the number of overrides taken, 0 if this was not a legend. The
 * caller CLEARS first; a partial legend must not merge with the last game's.
 */
int gm_icon_learn_legend(const char *s)
{
    int taken = 0;
    const char *p;

    if (!s || !*s)
        return 0;

    /* First pass: count the definitions, so a non-legend changes nothing. */
    for (int pass = 0; pass < 2; pass++) {
        int found = 0;
        for (p = s; (p = strstr(p, "<ICON ")) != NULL; ) {
            char tok[GM_OVERRIDE_TOK];
            char say[GM_OVERRIDE_SAY];
            int n = 0;
            p += 6;
            while (*p && *p != '>' && n < (int)sizeof(tok) - 1)
                tok[n++] = *p++;
            tok[n] = '\0';
            if (*p != '>')
                break;
            p++;
            /* Skip any tags between the icon and its name. */
            while (*p == '<') {
                const char *close = strchr(p, '>');
                if (!close)
                    break;
                p = close + 1;
            }
            /* The name is the plain run up to the next tag. */
            n = 0;
            while (*p && *p != '<' && n < (int)sizeof(say) - 1)
                say[n++] = *p++;
            say[n] = '\0';
            /* Trim. */
            while (n > 0 && (say[n-1] == ' ' || say[n-1] == '\t'))
                say[--n] = '\0';
            const char *start = say;
            while (*start == ' ' || *start == '\t')
                start++;
            /* ⚠ A name must be WORDS. "+" and "/" join moves together
             * ("<ICON C_236>+<ICON C_A>"), and accepting those would record
             * "+" as a token's name. */
            if (!*start || !((*start >= 'A' && *start <= 'Z') ||
                             (*start >= 'a' && *start <= 'z')))
                continue;
            found++;
            if (pass == 1 && gm_icon_override_add(tok, start))
                taken++;
        }
        if (pass == 0 && found < 2)
            return 0;              /* not a legend: change nothing */
    }
    return taken;
}

const char *gm_icon(const char *token, char *tmp, SIZE_T tmpsz)
{
    if (!token || !*token)
        return NULL;

    /* ⭐ The screen's own legend WINS over the generic table. It is the game's
     * word for this token in THIS game; the table's is a fallback for screens
     * that print no legend. */
    for (int i = 0; i < g_override_n; i++) {
        if (strcmp(g_override[i].token, token) == 0)
            return g_override[i].say;
    }

    for (SIZE_T i = 0; i < ICON_COUNT; i++) {
        if (strcmp(ICONS[i].token, token) == 0)
            return ICONS[i].say;
    }

    /* The mechanical keyboard rule: KB_A -> "A", KB_F10 -> "F10", KB_5 -> "5".
     * Only for tokens the table did not name, so KB_SPACE etc. keep their
     * spoken forms above. */
    if (strncmp(token, "KB_", 3) == 0 && token[3] != '\0') {
        if (!tmp || tmpsz < 2)
            return NULL;
        strncpy(tmp, token + 3, tmpsz - 1);
        tmp[tmpsz - 1] = '\0';
        return tmp;
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * A small bounded output buffer. Every append is length-checked; the result is
 * always NUL-terminated and never overruns, so a pathological input truncates
 * rather than corrupting the stack.
 * ------------------------------------------------------------------------ */
struct sink { char *buf; SIZE_T cap; SIZE_T len; };

static void sink_put(struct sink *s, const char *p, SIZE_T n)
{
    if (!p || n == 0)
        return;
    if (s->len >= s->cap - 1)
        return;
    SIZE_T room = s->cap - 1 - s->len;
    if (n > room)
        n = room;
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

static void sink_str(struct sink *s, const char *p)
{
    if (p)
        sink_put(s, p, strlen(p));
}

static void sink_ch(struct sink *s, char c) { sink_put(s, &c, 1); }

/* Trailing whitespace / separator trim, applied in place. */
static void sink_rtrim(struct sink *s, const char *set)
{
    while (s->len > 0 && strchr(set, s->buf[s->len - 1]) != NULL)
        s->buf[--s->len] = '\0';
}

/* Is this a layout-only tag we drop wholesale? Compared case-insensitively
 * because <ltr> is lowercase while the rest are upper.
 *
 * ⚠ The default for an UNRECOGNISED tag is also to drop it (see gm_speak).
 * That is deliberate: <FONT> was unknown until the live captures arrived, and
 * speaking an unknown tag verbatim is strictly worse than dropping it. */
static BOOL tag_is_layout(const char *name, SIZE_T n)
{
    static const char *const LAYOUT[] = {
        "SIZE", "/SIZE", "RGB", "/RGB", "CNTR", "FONT", "/FONT", "LTR", "/LTR"
    };
    char up[16];
    if (n == 0 || n >= sizeof(up))
        return FALSE;
    for (SIZE_T i = 0; i < n; i++)
        up[i] = (char)toupper((unsigned char)name[i]);
    up[n] = '\0';
    for (SIZE_T i = 0; i < sizeof(LAYOUT) / sizeof(LAYOUT[0]); i++)
        if (strcmp(LAYOUT[i], up) == 0)
            return TRUE;
    return FALSE;
}

/* One parsed piece of the input: an icon, or a run of prose. */
struct piece {
    const char *icon;       /* spoken icon name, or NULL for prose  */
    char        icon_tmp[32];
    const char *text;       /* prose start (not NUL-terminated)     */
    SIZE_T      text_len;
};

#define GM_MAX_PIECES 64

/* Strip layout tags and split into alternating prose/icon pieces.
 * Returns the piece count. */
static int split_pieces(const char *in, struct piece *out, int max)
{
    int n = 0;
    const char *p = in;
    const char *prose = in;

    while (*p) {
        if (*p != '<') { p++; continue; }

        const char *close = strchr(p, '>');
        if (!close) {
            /* ⛔ UNTERMINATED TAG. This must NOT fall through to prose: doing
             * so emits raw markup ("PRESS <ICON DECIDE") to the user, which is
             * precisely what this module exists to prevent. Caught by the
             * suite's edge case, not by reasoning -- the residue check missed
             * it because none of ITS inputs had a truncated tag.
             *
             * ⚠ And truncation is not hypothetical here: the capture buffer is
             * a fixed 256 bytes (GT_TEXT_MAX), so any longer string arrives
             * cut off, mid-tag as often as not.
             *
             * Emit the prose before it and stop. */
            if (prose < p && n < max) {
                out[n].icon = NULL;
                out[n].text = prose;
                out[n].text_len = (SIZE_T)(p - prose);
                n++;
            }
            return n;
        }

        /* <ICON NAME> */
        if (strncmp(p + 1, "ICON ", 5) == 0) {
            if (prose < p && n < max) {
                out[n].icon = NULL;
                out[n].text = prose;
                out[n].text_len = (SIZE_T)(p - prose);
                n++;
            }
            if (n < max) {
                char tok[32];
                SIZE_T tl = (SIZE_T)(close - (p + 6));
                if (tl >= sizeof(tok)) tl = sizeof(tok) - 1;
                memcpy(tok, p + 6, tl);
                tok[tl] = '\0';

                out[n].text = NULL;
                out[n].text_len = 0;
                out[n].icon_tmp[0] = '\0';
                const char *say = gm_icon(tok, out[n].icon_tmp,
                                          sizeof(out[n].icon_tmp));
                if (say == NULL) {
                    /* ⭐ Unknown token stays VISIBLE. Dropping it silently
                     * destroys meaning, and [TOKEN] in the log is how a
                     * vocabulary change reports itself. */
                    _snprintf(out[n].icon_tmp, sizeof(out[n].icon_tmp) - 1,
                              "[%s]", tok);
                    out[n].icon_tmp[sizeof(out[n].icon_tmp) - 1] = '\0';
                    out[n].icon = out[n].icon_tmp;
                } else if (say != out[n].icon_tmp) {
                    out[n].icon = say;
                } else {
                    out[n].icon = out[n].icon_tmp;
                }
                n++;
            }
            p = close + 1;
            prose = p;
            continue;
        }

        /* Any other tag: layout, or unrecognised. Both are dropped. */
        {
            const char *nm = p + 1;
            SIZE_T nl = (SIZE_T)(close - nm);
            (void)tag_is_layout(nm, nl);   /* classification kept for clarity */
            if (prose < p && n < max) {
                out[n].icon = NULL;
                out[n].text = prose;
                out[n].text_len = (SIZE_T)(p - prose);
                n++;
            }
            p = close + 1;
            prose = p;
        }
    }

    if (*prose && n < max) {
        out[n].icon = NULL;
        out[n].text = prose;
        out[n].text_len = strlen(prose);
        n++;
    }
    return n;
}

/* Trim leading/trailing ASCII whitespace from a (ptr,len) run. */
static void trim(const char **s, SIZE_T *n)
{
    while (*n > 0 && isspace((unsigned char)(*s)[0])) { (*s)++; (*n)--; }
    while (*n > 0 && isspace((unsigned char)(*s)[*n - 1])) { (*n)--; }
}

/* Does `action` begin with `name` followed by a word break? The stutter test.
 * Case-insensitive, because the printed action is title-case and some spoken
 * names are not. */
static BOOL starts_with_word(const char *action, SIZE_T alen, const char *name)
{
    SIZE_T nlen = strlen(name);
    if (nlen == 0 || alen < nlen)
        return FALSE;
    for (SIZE_T i = 0; i < nlen; i++) {
        if (tolower((unsigned char)action[i]) != tolower((unsigned char)name[i]))
            return FALSE;
    }
    return (alen == nlen) || isspace((unsigned char)action[nlen]);
}

SIZE_T gm_speak(const char *in, char *out, SIZE_T outsz)
{
    struct sink s = { out, outsz, 0 };

    if (!out || outsz == 0)
        return 0;
    out[0] = '\0';
    if (!in || !*in)
        return 0;

    struct piece pieces[GM_MAX_PIECES];
    int n = split_pieces(in, pieces, GM_MAX_PIECES);
    if (n == 0)
        return 0;

    /* ⭐ Is this a BUTTON HINT STRIP or ordinary prose?
     *
     * A hint strip is a run of `<ICON button>Action` pairs, so it has at least
     * one icon that is FOLLOWED by text. Ordinary prose has its icons embedded
     * mid-sentence ("PRESS <ICON DECIDE> TO START"), where the text before the
     * first icon is a sentence rather than a caption.
     *
     * The discriminator that actually holds on the measured captures: a hint
     * strip's FIRST icon is preceded by nothing, or by a caption ending in
     * '.', and is followed immediately by its label with no space. Rather than
     * guess, both shapes go through the same pair walk below -- the difference
     * only decides whether pairs get ";" separators, and a single pair reads
     * identically either way. */
    int icon_count = 0, pair_count = 0;
    for (int i = 0; i < n; i++) {
        if (!pieces[i].icon)
            continue;
        icon_count++;
        if (i + 1 < n && !pieces[i + 1].icon) {
            const char *t = pieces[i + 1].text;
            SIZE_T tl = pieces[i + 1].text_len;
            /* Followed immediately by a word with no leading space = a label
             * bound to this button, i.e. a hint pair. */
            if (tl > 0 && !isspace((unsigned char)t[0]))
                pair_count++;
        }
    }
    BOOL hint_mode = (icon_count >= 1 && pair_count >= 1);

    if (!hint_mode) {
        /* Ordinary prose: icons become words in place, separated so they never
         * fuse with a neighbouring word and never chop the sentence. */
        for (int i = 0; i < n; i++) {
            if (pieces[i].icon) {
                /* No separator after an opening bracket or a joiner:
                 * "(<ICON C_K2> for ...)" reads "(Kick for ...)", not
                 * "( Kick for ...)". The Python oracle applies the same rule;
                 * parity is what keeps the two from drifting on it. */
                if (s.len > 0 && strchr(" ([{/+", out[s.len - 1]) == NULL)
                    sink_ch(&s, ' ');
                sink_str(&s, pieces[i].icon);
                /* A comma only when another icon follows with nothing but
                 * whitespace between -- that is what makes an input sequence
                 * read as discrete presses ("down, down, forward"). */
                BOOL next_is_icon = FALSE;
                if (i + 1 < n && pieces[i + 1].icon)
                    next_is_icon = TRUE;
                else if (i + 2 < n && pieces[i + 2].icon && !pieces[i + 1].icon) {
                    const char *t = pieces[i + 1].text;
                    SIZE_T tl = pieces[i + 1].text_len;
                    trim(&t, &tl);
                    if (tl == 0) next_is_icon = TRUE;
                }
                if (next_is_icon)
                    sink_ch(&s, ',');
            } else {
                const char *t = pieces[i].text;
                SIZE_T tl = pieces[i].text_len;
                trim(&t, &tl);
                if (tl == 0)
                    continue;
                if (s.len > 0 && out[s.len - 1] != ' ')
                    sink_ch(&s, ' ');
                sink_put(&s, t, tl);
            }
        }
        sink_rtrim(&s, " ,;");
        return s.len;
    }

    /* --- Hint strip: walk it as pairs. --- */
    {
        /* Anything before the first icon is a caption ("Choose the game..."). */
        int first_icon = 0;
        while (first_icon < n && !pieces[first_icon].icon)
            first_icon++;
        for (int i = 0; i < first_icon; i++) {
            const char *t = pieces[i].text;
            SIZE_T tl = pieces[i].text_len;
            trim(&t, &tl);
            if (tl == 0)
                continue;
            if (s.len > 0)
                sink_ch(&s, ' ');
            sink_put(&s, t, tl);
        }
        /* A lead ending in an opening bracket takes no separator before the
         * first hint: "(<ICON C_K2> for ...)" reads "(Kick for ...)", not
         * "( Kick for ...)". */
        BOOL had_lead = (s.len > 0) &&
                        strchr("([{/+", out[s.len - 1]) == NULL;
        BOOL first_hint = TRUE;

        int i = first_icon;
        while (i < n) {
            if (!pieces[i].icon) { i++; continue; }

            /* Gather this hint's button(s): a "/" or "+" gap chains them. */
            char buttons[128];
            int  button_count = 1;   /* icons in THIS hint, not words */
            buttons[0] = '\0';
            strncpy(buttons, pieces[i].icon, sizeof(buttons) - 1);
            buttons[sizeof(buttons) - 1] = '\0';

            const char *action = NULL;
            SIZE_T action_len = 0;

            for (;;) {
                if (i + 1 >= n) { i++; break; }
                if (pieces[i + 1].icon) {
                    /* Two icons with nothing between: still one hint's
                     * buttons, joined by a space. */
                    /* ⚠ KEY_SUP maps to the EMPTY string -- a deliberately
                     * suppressed icon. Appending it emitted a stray separator
                     * ("Start  Confirm"). Skip empties rather than special-
                     * casing the token, since any future suppressed icon
                     * behaves the same way. */
                    if (pieces[i + 1].icon[0] != '\0') {
                        SIZE_T bl = strlen(buttons);
                        _snprintf(buttons + bl, sizeof(buttons) - bl - 1,
                                  bl ? " %s" : "%s", pieces[i + 1].icon);
                        buttons[sizeof(buttons) - 1] = '\0';
                        button_count++;
                    }
                    i++;
                    continue;
                }
                const char *t = pieces[i + 1].text;
                SIZE_T tl = pieces[i + 1].text_len;
                const char *raw = t;
                SIZE_T raw_len = tl;
                trim(&t, &tl);

                /* ⛔ "/" = either-of, "+" = chord. NOT a pair boundary. */
                if (tl == 1 && (t[0] == '/' || t[0] == '+') &&
                    i + 2 < n && pieces[i + 2].icon) {
                    SIZE_T bl = strlen(buttons);
                    _snprintf(buttons + bl, sizeof(buttons) - bl - 1, " %s %s",
                              t[0] == '/' ? "or" : "plus", pieces[i + 2].icon);
                    buttons[sizeof(buttons) - 1] = '\0';
                    button_count++;
                    i += 2;
                    continue;
                }
                (void)raw; (void)raw_len;
                action = t;
                action_len = tl;
                i += 2;
                break;
            }

            /* ⭐ THE STUTTER: drop the action's leading copy of the button
             * name, by PREFIX and only the duplicated word. The button is
             * named after what it does, so "<ICON DECIDE>Confirm" would
             * otherwise speak as "Confirm Confirm". */
            /* ⛔ "HAS A SPACE" IS NOT "IS A COMBINATION". The first version
             * tested strrchr(buttons, ' ') == NULL to mean "single button" --
             * but plenty of single icons have MULTI-WORD names ("special
             * move", "Any Attack", "left stick"), so every one of them skipped
             * the stutter check and spoke twice: "special move Special Move".
             * Found by the parity check against the Python oracle, not by any
             * hand-picked case. Count the buttons instead. */
            if (action && action_len > 0 && button_count == 1) {
                if (starts_with_word(action, action_len, buttons)) {
                    SIZE_T nlen = strlen(buttons);
                    const char *rest = action + nlen;
                    SIZE_T rest_len = action_len - nlen;
                    trim(&rest, &rest_len);
                    if (rest_len > 0) {
                        action = rest;
                        action_len = rest_len;
                    } else {
                        /* ⭐ DE-DUPLICATION, NOT DELETION. If removing the
                         * leading copy leaves nothing, the icon and the label
                         * are the same thing -- speak the PRINTED label alone
                         * (the game's own wording and case) and drop the icon.
                         * Stripping the action instead deleted the label
                         * outright: HELP_SPMOVE spoke as "special move". */
                        buttons[0] = '\0';
                    }
                }
            }

            if (!first_hint)
                sink_str(&s, "; ");
            else if (had_lead)
                sink_ch(&s, ' ');
            first_hint = FALSE;

            sink_str(&s, buttons);
            if (action && action_len > 0) {
                /* No separator when the icon was dropped as a duplicate --
                 * `buttons` is empty and a space would lead the hint. */
                if (buttons[0] != '\0')
                    sink_ch(&s, ' ');
                sink_put(&s, action, action_len);
            }
        }
    }

    sink_rtrim(&s, " ,;");
    return s.len;
}

BOOL gm_has_residue(const char *s)
{
    if (!s)
        return FALSE;
    for (const char *p = s; *p; p++) {
        if (*p == '<' && strchr(p, '>') != NULL)
            return TRUE;
    }
    return FALSE;
}
