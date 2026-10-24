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

/* ---- Per-cue volume -----------------------------------------------------
 * ⭐ ONE LEVEL PER CUE FAMILY, not per voice. The families are what the
 * listener can name ("the beacons are too loud"); P1 and P2 share a level
 * because they are the same cue heard from two sides, and turning one player
 * down would make the pan readout lie about which side is which.
 *
 * Levels are PERCENT 0..100 and multiply the cue's designed gain, so 100 is
 * exactly the balance tuned by ear and 0 is silence. They stack under the
 * master volume from cfc2_audio.ini; master stays a file setting.
 *
 * ⚠ The step is 25 and the cycle WRAPS: 100 -> 0 -> 25 -> 50 -> 75 -> 100.
 * So from the default one press up mutes the cue. That is the user's design
 * (2026-09-19): muting a cue must cost one keypress, not four, because the
 * reason to mute is usually that the cue is in the way right now. */
/* ⚠ THE ORDER IS THE KEY ORDER. F5 F6 F7 F8 map to these four in sequence,
 * so the enum is not free to be reordered -- mod.c indexes it by (vk - VK_F5).
 * The pairing is deliberate: the two gauges first (they share a mental
 * category), then the beacons, then the tick. */
typedef enum {
    AUDIO_CUE_HEALTH = 0,   /* F5 -- the health gauge, both players  */
    AUDIO_CUE_SUPER,        /* F6 -- the super meter, both players   */
    AUDIO_CUE_BEACON,       /* F7 -- the two position beacons        */
    AUDIO_CUE_TICK,         /* F8 -- the round-clock tick            */
    AUDIO_CUE_COUNT
} audio_cue;

#define AUDIO_VOL_STEP 25

/* Current level, percent. Out-of-range cue returns 0. */
int  audio_cue_volume(audio_cue c);

/* Set a level directly, clamped to 0..100 and rounded to nothing -- the
 * caller owns the step policy. Returns the level actually stored. */
int  audio_cue_set_volume(audio_cue c, int percent);

/* One step up (`up`) or down, wrapping at both ends. Returns the new level.
 * ⭐ Wrapping in BOTH directions, so shift from 0 lands on 100 -- a blind
 * user cannot see that a key has stopped doing anything, and a key that
 * silently no-ops at an end reads as a broken hotkey. */
int  audio_cue_step_volume(audio_cue c, BOOL up);

/* The cue's spoken name, for the announcement the hotkey makes. */
const wchar_t *audio_cue_name(audio_cue c);

/* Persist all four levels to <game_dir>\cfc2_audio.ini. Called after a
 * change so the balance survives a restart; failure is logged, not fatal. */
void audio_save_volumes(void);

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

/* ---- Beacons ------------------------------------------------------------
 * ⭐⭐ THE CORE OF THE DESIGN (memory cfc2-audio-design-decision): each
 * player has a PERSISTENT looping cue that sounds continuously during a
 * match. Pan = the fighter's screen X, pitch = stance. The two beacons have
 * different base pitches so "which one is me" is carried by tone, not
 * position -- pan alone is ambiguous when both fighters are near centre.
 *
 * ⛔ DO NOT confuse this with gauge pan. Gauge pan is FIXED by owner (P1
 * left, P2 right) to mirror the HUD bars; beacon pan MOVES with the fighter.
 * Both fighters can be on the right of the stage while P1's gauge tone still
 * comes from the left, exactly as the screen looks.
 *
 * `pan` is -1 hard left .. +1 hard right; `stance` multiplies the base pitch
 * (knocked down 0.70, crouching 0.85, standing 1.00, airborne 1.15+). Both
 * are clamped, not rejected. Calling this marks the beacon live.
 *
 * ⚠ Lock-free: called from the game's frame thread, where taking the mixer
 * lock is what made the health tone crackle. */
void audio_beacon_set(unsigned player, float pan, float stance);

/* Leaving a match. The "never silent" rule governs play; outside a match
 * there is no fighter to locate and a droning tone over the menus is noise. */
void audio_beacons_off(void);

/* The stance ladder, as pitch multipliers -- ordered low to high, so one
 * axis carries the fighter's vertical state continuously.
 *
 * Driven from CVS2's own stance byte (B+0x59A: 0 grounded, 1 airborne,
 * 2 crouching), with AIR..AIR_TOP interpolated by actual height so a jump
 * reads as a rising-then-falling arc rather than a single "airborne" step.
 *
 * ⛔ DOWN is defined but NOT yet driven: no capture was taken while knocked
 * down, so that state's encoding is unverified. Anything unrecognised is
 * treated as STAND rather than guessed. */
#define AUDIO_STANCE_DOWN    0.70f
#define AUDIO_STANCE_CROUCH  0.85f
#define AUDIO_STANCE_STAND   1.00f
#define AUDIO_STANCE_AIR     1.15f
#define AUDIO_STANCE_AIR_TOP 1.45f

/* ---- One-shot cues ------------------------------------------------------
 * A short percussive tick, centre-panned, for discrete events (the round
 * clock). Unlike a gauge this is fire-and-forget: it plays once and ends.
 * Overlap is deliberate -- a new request while one is sounding starts another
 * rather than stealing it, because the user asked for onsets to land at the
 * right moment, not for sounds to be exclusive. */
void audio_tick(void);

/* ---- Positional pings ---------------------------------------------------
 * Short one-shots for things lying in an arena (Power Stone's stones, items
 * and liftable props), scheduled by pickup_cues.c. One TIMBRE per family
 * says what kind of thing it is; `variant` steps the pitch a semitone per
 * type inside the family; `pan` is its screen position; `gain` 0..1 is the
 * caller's nearness. Volume follows the beacon level (F7): these are the
 * positional cue in the games that use them.
 *
 * ⚠ Lock-free and stamped with the epoch, exactly like the clock tick: called
 * from the game's frame thread, dropped by the mixer if the owner changed. */
typedef enum {
    AUDIO_PING_STONE = 0,   /* bell: bright inharmonic partials, long ring  */
    AUDIO_PING_FOOD,        /* sine that falls in pitch: a soft "bloop"     */
    AUDIO_PING_ITEM,        /* sine plus its fifth, short: a clear blip     */
    AUDIO_PING_PROP,        /* low triangle thud, quieter by design         */
    AUDIO_PING_FAMILIES
} audio_ping_family;
void audio_ping_owned(uint64_t epoch, audio_ping_family family, int variant,
                      float pan, float gain);

/* ---- Legacy-path owner routing ------------------------------------------
 * Used by match_audio, never by an adapter directly. begin/stop run on the
 * worker. begin accepts only a strictly increasing nonzero epoch and silences
 * everything the previous owner left (gauges fade, beacons off, its armed
 * ticks dropped). stop acts only on the CURRENT epoch, so a late stop from a
 * replaced owner is ignored. Once any epoch has been claimed, the unowned
 * setters above are rejected.
 *
 * The *_owned setters are ignored unless their epoch is current and running.
 * Gauges check under the mixer lock. Beacons and ticks stay lock-free for the
 * frame thread; they are stamped with the epoch and the mixer drops a stamp
 * that is not current, so a write that loses a race with a switch is silent.
 * Players 0 and 1 only: four-owner routing is the Power Stone phase. */
BOOL audio_owner_begin(uint64_t epoch);
void audio_owner_stop(uint64_t epoch);
void audio_gauge_set_owned(uint64_t epoch, audio_gauge g, unsigned player,
                           int value, int maxv);
void audio_gauge_reset_owned(uint64_t epoch, audio_gauge g, unsigned player);
/* live FALSE silences that one beacon and returns it to its HUD placement. */
void audio_beacon_set_owned(uint64_t epoch, unsigned player, BOOL live,
                            float pan, float stance);
void audio_tick_owned(uint64_t epoch);

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
#define AUDIO_PARTICIPANTS 4
#define AUDIO_SCENE_TTL_MS 250

/* Optional shared presentation path. Existing CVS2 setters remain two-player.
 * Slots are stable presentation identities, never filtered-list ordinals.
 * The caller must establish visible ownership/projection before publication.
 * Gauges use HUD pan; beacons use screen pan. Missing fields silence that cue.
 * Generation changes reset comparison history (round/actor/resource changes).
 * Extra beacon identities are provisional until four-player listening checks.
 */
typedef struct {
    BOOL valid;
    int value, maximum;
    float hud_pan;
    uint64_t generation;
} audio_scene_gauge;
typedef struct {
    BOOL present, beacon_valid;
    uint64_t incarnation;
    float screen_pan, stance;
    audio_scene_gauge gauges[AUDIO_GAUGE_COUNT];
} audio_scene_participant;
typedef struct {
    uint64_t epoch, sequence;
    audio_scene_participant participants[AUDIO_PARTICIPANTS];
} audio_scene;

/* Worker/router only: strictly increasing nonzero epochs; claims output from
 * legacy producers, clears pending scene/history. Never called by CVS2 today.
 * Publication is bounded and nonblocking; FALSE means not accepted (retry with
 * a fresh complete snapshot). Duplicate/old sequence numbers never renew TTL.
 * stop retains ownership and rejects further publication until a newer begin.
 * No automatic fallback to legacy producers when stopped or expired.
 */
BOOL audio_scene_begin(uint64_t epoch);
BOOL audio_scene_publish(const audio_scene *scene);
void audio_scene_stop(uint64_t epoch);
void audio_render_block_for_test(int16_t *out);

#endif /* CFC2_AUDIO_H */
