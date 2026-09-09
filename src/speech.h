/*
 * Speech output for CFC2 Access.
 *
 * Routes through the user's own screen reader (NVDA, JAWS, ZDSR, Window-Eyes,
 * SAPI fallback, plus braille) via UniversalSpeech, rather than driving a TTS
 * voice directly. A blind user has tuned their screen reader's voice, rate and
 * punctuation level; bypassing it is worse than routing through it.
 */
#ifndef CFC2_SPEECH_H
#define CFC2_SPEECH_H

#include <windows.h>

/* Load UniversalSpeech.dll from the game directory. Returns FALSE if the
 * library is missing or has no usable backend; the mod stays alive either way
 * so the log still records what happened. */
BOOL speech_init(void);
void speech_shutdown(void);

/* Speak text. interrupt=TRUE cuts off whatever is being spoken (use for
 * round-state changes); FALSE appends (use for on-demand readouts). */
/* Returns TRUE only if the backend ACCEPTED the text. ⛔ Do not ignore this:
 * discarding it once meant the log showed a spoken line for every menu movement
 * while the user heard nothing, and the log was mistaken for evidence. */
BOOL speech_say(const wchar_t *text, BOOL interrupt);
void speech_stop(void);

/* Name of the backend that actually won, e.g. "NVDA" or "SAPI". Returns NULL
 * when no screen reader is active. Announce this at startup so a fallback is
 * audible rather than silent. */
const wchar_t *speech_backend(void);

#endif /* CFC2_SPEECH_H */
