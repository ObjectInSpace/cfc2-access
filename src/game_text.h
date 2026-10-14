#ifndef CFC2_GAME_TEXT_H
#define CFC2_GAME_TEXT_H
#include <windows.h>
#include "text_channel.h"

/* The worker half every emulated game's on-screen text reader shares: take
 * the frame its capture published, decode each row with the game's own
 * decoder, drop repeats within the frame, speak new lines with letters (the
 * blinking-prompt re-arm), answer the ` key with the whole frame, and log
 * each refused row once with its bytes.
 *
 * What stays in the GAME's file: where its text lives and when to copy it
 * (the capture, on the game thread, publishing to `publisher` below), and
 * how its bytes read (the decoder). Porting another game's text adds those
 * two things and one game_text_source; it does not copy this policy.
 * Readers: cvs1_text.c, cfj_text.c, z3u_text.c. */

/* Decodes one captured row into speech text; FALSE when the row cannot be
 * rendered faithfully (it is then logged as refused, unless quiet()). */
typedef BOOL (*game_text_decode_fn)(int style, const char *raw, wchar_t *out, SIZE_T cap);
/* Optional: TRUE for refused rows that are expected and not worth a log
 * line (CFJ's style 0, which its renderer never draws). */
typedef BOOL (*game_text_quiet_fn)(const tc_row *row);

typedef struct {
    /* Set by the game. */
    const char *tag;                  /* log and speech source, "cfj-text"   */
    const char *name;                 /* the ` key source name, "CFJ"        */
    game_text_decode_fn decode;
    game_text_quiet_fn quiet;         /* may be NULL                         */
    /* The capture publishes here (game thread); the rest is worker state. */
    tc_publisher publisher;
    tc_textdedup dedup;
    ULONGLONG active_root;
    unsigned consumed;
    unsigned refused_hash[32], refused_used;
} game_text_source;

#define GAME_TEXT_SOURCE(tag_, name_, decode_, quiet_) \
    { .tag = (tag_), .name = (name_), .decode = (decode_), .quiet = (quiet_), \
      .publisher = { .lock = SRWLOCK_INIT } }

/* Worker. Root of the admitted game block, or 0 when the game is not running.
 * A new root forgets what was spoken and discards the old session's frame. */
void game_text_observe(game_text_source *s, ULONGLONG root);

/* Worker, from the game's ` key callback: reads the whole current frame. */
tc_read_result game_text_read_current(game_text_source *s);
#endif
