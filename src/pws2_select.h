#ifndef CFC2_PWS2_SELECT_H
#define CFC2_PWS2_SELECT_H
#include <windows.h>
#include "pws_select_decode.h"
#include "mod_text.h"
#include "pickup_speech.h"
/* Power Stone 2 (game 5, ROMs 10 pws2 / 11 pws2u) character-highlight speech
 * and match-audio ownership. The decoder is shared with PWS1
 * (pws_select_decode.c); this file is the acquisition and gating. Native
 * evidence: docs/port-tasks/pws-selection.md and pws2-match-fields.md.
 * Read-only, worker thread only. */
#define PWS2_WINDOW_OFF  0x529D2u     /* side states .. both gates, one read  */
#define PWS2_WINDOW_SIZE 0x35Du       /* R+0x529D2 .. R+0x52D2E inclusive     */
/* NULL = PWS2 not admitted: depart. */
void pws2_select_observe(const pws_select_sample *s, ULONGLONG root, unsigned rom);
void pws2_select_tick(void);
/* The words for an item code's kind (stone, food, treasure, something to
 * throw, item), for the pickup announcements. */
pickup_kind pws2_pickup_word(unsigned code);
/* Speak the pending pickups (worker; also the test seam). admitted FALSE
 * drains them silently. */
void pws2_select_speak_pickups(BOOL admitted, unsigned rom);
#endif
