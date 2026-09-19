/*
 * Audio cue layer for CFC2 Access.
 *
 * Synthesizes every cue procedurally and renders it through winmm waveOut on
 * a thread this module owns. Nothing here touches the game's audio engine,
 * its memory, or any hook: callers decide WHAT to play on the worker thread,
 * this module only turns parameters into samples. MTFramework's own mixer is
 * irrelevant to us -- Windows mixes per-process streams.
 *
 * ⛔ NO DUCKING (user decision, 2026-09-19). Cues do not lower themselves for
 * speech. Do not reintroduce a speech-ducking path; speech.c and audio.c stay
 * uncoupled.
 *
 * ⭐ WRITTEN FRESH, not ported. The Detroit Become Able audio layer was
 * reviewed and rejected as a base: its voices are hardcoded around navigation
 * guidance (one beacon with distance->tempo, one wall hiss, one monophonic
 * earcon slot) and only the beacon is panned. CFC2 needs N independent voices
 * each with its OWN timbre, pan and pitch. What was worth keeping is the
 * knowledge, not the code: ~23 ms buffers, smooth every gain or you get zipper
 * noise, and no-op cleanly when the device will not open.
 *
 * THE DESIGN THIS SERVES (memory cfc2-audio-design-decision):
 *  - one distinct TIMBRE per gauge type -- timbre says WHICH gauge;
 *  - PAN is fixed by owner -- P1 hard left, P2 hard right -- so the same tone
 *    set serves both players and the listener can ignore one side natively;
 *  - PITCH rises as a gauge fills and falls as it empties;
 *  - ⭐ a tone sounds ONLY WHILE THE VALUE IS CHANGING. A static gauge is
 *    silent. That is what makes 5+ gauges affordable.
 */
#ifndef CFC2_AUDIO_H
#define CFC2_AUDIO_H

#include <windows.h>
#include <stdint.h>

/* Open the device and start the render thread. Safe to call once, from the
 * worker. Reads master volume from <game_dir>\cfc2_audio.ini ([audio]
 * volume=0..100, default 60). */
void audio_init(const wchar_t *game_dir);
void audio_shutdown(void);
BOOL audio_ready(void);

/* Master on/off for all mod-generated cues. Speech is never affected. */
BOOL audio_toggle_enabled(void);

/* ---- Gauges -------------------------------------------------------------
 * A gauge is identified by TYPE (which picks the timbre) and OWNER (which
 * picks the pan). Callers report the current value and its range; the mixer
 * decides whether anything is audible, using the change rule above.
 *
 * ⚠ Call audio_gauge_set() every tick with the CURRENT value, changed or not.
 * The mixer needs to see a value settle in order to go silent. */
/* ⛔ NO STUN GAUGE. I added one as a "reserved" slot without checking whether
 * CVS2 shows stun to the player. It does NOT: dizzy is a STATE (you see stars
 * and the game plays its own sound), but no bar displays accumulated stun.
 * The published CVS2 field schema lists health, char_id, groove, ratio, team
 * and stage -- no stun field at all. An on-screen stun bar is a Street
 * Fighter III/IV convention, not a CVS2 one.
 *
 * ⭐⭐ This is not merely a missing feature, it would be a WRONG one: a stun
 * cue would hand the listener information a SIGHTED player does not have.
 * That inverts the parity principle the whole design rests on -- judge a cue
 * against what the sighted player actually perceives. Do not add it back for
 * CVS2. Other games in the collection have their own meters (Groove, Plasma
 * Power, Guts); those are per-game and need their own evidence. */
typedef enum {
    AUDIO_GAUGE_SUPER = 0,   /* super/power meter -- B+0x226, per-groove cap */
    AUDIO_GAUGE_HEALTH,      /* B+0x16C, max B+0x170                         */
    AUDIO_GAUGE_COUNT
} audio_gauge;

/* value/maxv describe the gauge now. player is 0 (P1, left) or 1 (P2, right).
 * maxv <= 0 or an out-of-range player is ignored rather than fatal. */
void audio_gauge_set(audio_gauge g, unsigned player, int value, int maxv);

/* Forget a gauge's history -- use on round/character change so a reset does
 * not sound like a drain. Silences the voice without a fade artifact. */
void audio_gauge_reset(audio_gauge g, unsigned player);
void audio_gauge_reset_all(void);

/* ---- One-shot cues ------------------------------------------------------
 * A short percussive tick, centre-panned, for discrete events (the round
 * clock). Unlike a gauge this is fire-and-forget: it plays once and ends.
 * Overlap is deliberate -- a new request while one is sounding starts another
 * rather than stealing it, because the user asked for onsets to land at the
 * right moment, not for sounds to be exclusive. */
void audio_tick(void);

/* ---- Test seam ----------------------------------------------------------
 * ⭐ Renders one block into `out` (AUDIO_BLOCK frames, interleaved stereo)
 * WITHOUT any device. The offline suite drives the whole mixer through this,
 * so the DSP is testable on a machine with no sound card. */
/* ⭐ 256 frames (5.8 ms), NOT 1024. The user reported the health tone lagging
 * behind the damage, and the measured chain was ~117 ms: worker poll 20 ms +
 * waveOut QUEUE 4x23.2 = 92.9 ms + gain ramp ~4 ms. The queue dominated --
 * a value written now is only heard after every ALREADY-QUEUED buffer plays
 * out, so queue depth IS latency. 4 x 256 = 23.2 ms of queue instead of 92.9.
 *
 * ⚠ Keep FOUR buffers. Cutting the COUNT would trade underrun safety for the
 * same latency that shrinking each buffer buys more cheaply. */
#define AUDIO_BLOCK 256
#define AUDIO_SR    44100
void audio_render_block_for_test(int16_t *out);

#endif /* CFC2_AUDIO_H */
