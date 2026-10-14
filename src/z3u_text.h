#ifndef CFC2_Z3U_TEXT_H
#define CFC2_Z3U_TEXT_H
#include <windows.h>
#include "text_channel.h"

/* Alpha 3's positioned text-record queue (ordinary branch): GAME OVER,
 * CONTINUE?, GIVE UP?, QUIT GAME / YES / NO, SURVIVAL MODE, TEAM VS, NEXT,
 * WINS, SCORE, CLEAR TIME, CONGRATULATIONS!!, the credits and copyright.
 * Evidence: docs/port-tasks/game-text.md. Font 4 draws picture words from
 * tile codes and is not spoken. */

/* Game thread, from the Alpha 3 frame hook AFTER the original update returns:
 * every screen handler draws the queue (0x14087E640) inside the update, so it
 * holds exactly what this frame drew. */
void z3u_text_capture(void);

/* Game thread, from the hook on the attract walker 0x1408797D0, BEFORE it
 * draws and clears the queue (the opening screens print through it). Adds
 * the in-use records to the frame z3u_text_capture() then publishes. */
void z3u_text_collect(void);

/* Style of a typewriter message row (story text), outside the fonts' 0..4. */
#define Z3U_STYLE_MESSAGE 16

/* Pure: joins a message entry's lines (line NUL indent line NUL ... until a
 * zero indent) with spaces. Returns the length, 0 if unterminated or too long. */
SIZE_T z3u_message_join(const unsigned char *entry, SIZE_T len, char *out, SIZE_T cap);

/* Worker. Root of the admitted Alpha 3 block, or 0 when it is not running. */
void z3u_text_observe(ULONGLONG root);

/* Worker. Registers the ` key source; call once at startup, in priority order. */
void z3u_text_register(void);

/* Pure decode of one record's text, exposed for the offline test. Returns
 * FALSE for anything it cannot render faithfully: font 4's picture words,
 * any font other than 0 to 3, and bytes outside printable ASCII. */
BOOL z3u_text_decode(int font, const char *raw, wchar_t *out, SIZE_T cap);
#endif
