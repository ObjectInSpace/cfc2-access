/* See audio.h for the design this implements. */

#include "audio.h"
#include "log.h"

#include <mmsystem.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ⛔⛔ 23 ms OF QUEUE WAS NOT ENOUGH, and that was the crackle.
 *
 * 4 x 5.8 ms means the device runs dry if this thread is not rescheduled
 * within 23 ms. It shares a machine with the game's render thread and our own
 * worker, so that happens. ⚠ The earlier crackle fix removed per-frame lock
 * traffic from the FEED side -- a real bug, and it helped -- but it left the
 * queue at 23 ms, so the underrun remained. Two causes, one symptom; fixing
 * the first made the second look fixed.
 *
 * The BLOCK size is deliberately unchanged: HOLD_BLOCKS and the tick decay
 * constants are calibrated in blocks, so changing AUDIO_BLOCK would silently
 * retime both.
 *
 * ⛔⛔ 12 BUFFERS (70 ms) WAS TOO MUCH ONCE POSITION BEACONS EXISTED.
 * The note here used to say latency was "irrelevant -- these are continuous
 * gauge tones, not input feedback". That reasoning was sound for gauges and
 * WRONG for a beacon: a beacon is a SPATIAL readout of something the player is
 * actively controlling, so queued audio makes the sound trail the fighter.
 * The user heard exactly that -- "a little behind or ahead of them" -- with
 * 70 ms being over four frames at 60 fps.
 *
 * ⭐ THE TRADE IS EXPLICIT. Queue depth buys dropout tolerance and costs
 * positional accuracy; those pull in opposite directions and only one of them
 * was being weighed. 8 x 5.8 ms = 46 ms still absorbs an ordinary scheduling
 * hiccup (the crackle happened at 23 ms, and the per-frame lock traffic that
 * caused it is gone) while cutting the trail by a third.
 *
 * ⚠ If crackling EVER returns, raise this before touching anything else, and
 * say so in the log -- a dropout is worse than a slightly late beacon. */
#define AUD_NBUF 8                     /* 8 x 5.8 ms = 46 ms of queue */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- device ------------------------------------------------------------ */
static HWAVEOUT g_dev;
static WAVEHDR  g_hdr[AUD_NBUF];
static int16_t  g_buf[AUD_NBUF][AUDIO_BLOCK * 2];
static HANDLE   g_event, g_thread;
static volatile LONG g_run;
static BOOL     g_ready;
static BOOL     g_enabled = TRUE;
static float    g_master = 0.60f;

/* ---- per-cue volume -----------------------------------------------------
 * Percent 0..100 per cue family, multiplying that cue's designed gain. 100 is
 * the balance tuned by ear, so an untouched install sounds exactly as before
 * this feature existed -- which is what makes the default safe to ship.
 *
 * ⚠ `volatile int`, read by the RENDER thread and written by the worker
 * thread that handles hotkeys. Aligned 4-byte loads and stores are atomic in
 * practice on x86-64, and nothing here depends on ordering against other
 * state, so this needs no lock -- and must not take one. g_lock is held by the
 * render thread every 5.8 ms; a hotkey blocking on it is the shape of the bug
 * that made the health tone crackle (see audio_beacon_set).
 *
 * ⭐ The render side applies these as a GAIN, not as a skip: a cue at 0 still
 * runs its voice and its smoother, it simply multiplies out to silence. That
 * keeps muting free of clicks and means un-muting resumes mid-tone rather
 * than restarting one. */
static volatile int g_cue_vol[AUDIO_CUE_COUNT] = { 100, 100, 100, 100 };

/* Where to persist. Captured at init so a hotkey does not need the game dir
 * threaded through it. Empty = never initialised, and saving is skipped. */
static wchar_t g_ini_path[MAX_PATH];

static float cue_gain(audio_cue c)
{
    if ((int)c < 0 || c >= AUDIO_CUE_COUNT) return 1.0f;
    return (float)g_cue_vol[c] / 100.0f;
}

int audio_cue_volume(audio_cue c)
{
    if ((int)c < 0 || c >= AUDIO_CUE_COUNT) return 0;
    return g_cue_vol[c];
}

int audio_cue_set_volume(audio_cue c, int percent)
{
    if ((int)c < 0 || c >= AUDIO_CUE_COUNT) return 0;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_cue_vol[c] = percent;
    return percent;
}

int audio_cue_step_volume(audio_cue c, BOOL up)
{
    if ((int)c < 0 || c >= AUDIO_CUE_COUNT) return 0;
    /* ⭐ Wrap on a 0..100 ring in steps of AUDIO_VOL_STEP. From the 100
     * default one press up lands on 0 -- muting costs ONE key, which is the
     * user's stated design: the reason to mute is usually that the cue is in
     * the way right now.
     *
     * ⚠ Computed on the ring size (100/step + 1 stops), not by adding and
     * testing bounds. An add-and-clamp version would STOP at each end instead
     * of wrapping, and a key that silently does nothing at an end is
     * indistinguishable from a broken hotkey to a user who cannot see the
     * level. Both directions wrap for that reason. */
    const int stops = 100 / AUDIO_VOL_STEP + 1;       /* 0,25,50,75,100 */
    int i = g_cue_vol[c] / AUDIO_VOL_STEP;
    i = up ? (i + 1) % stops : (i + stops - 1) % stops;
    g_cue_vol[c] = i * AUDIO_VOL_STEP;
    return g_cue_vol[c];
}

const wchar_t *audio_cue_name(audio_cue c)
{
    switch (c) {
    case AUDIO_CUE_HEALTH: return L"health";
    case AUDIO_CUE_SUPER:  return L"super meter";
    case AUDIO_CUE_BEACON: return L"beacons";
    case AUDIO_CUE_TICK:   return L"clock tick";
    default:               return L"unknown cue";
    }
}

/* The ini key for each cue. Kept beside audio_cue_name() so adding a cue
 * cannot leave one of the two behind. */
static const wchar_t *cue_ini_key(audio_cue c)
{
    switch (c) {
    case AUDIO_CUE_HEALTH: return L"vol_health";
    case AUDIO_CUE_SUPER:  return L"vol_super";
    case AUDIO_CUE_BEACON: return L"vol_beacon";
    case AUDIO_CUE_TICK:   return L"vol_tick";
    default:               return NULL;
    }
}

void audio_save_volumes(void)
{
    if (!g_ini_path[0]) return;
    for (int c = 0; c < AUDIO_CUE_COUNT; c++) {
        const wchar_t *key = cue_ini_key((audio_cue)c);
        if (!key) continue;
        wchar_t val[16];
        _snwprintf(val, 16, L"%d", g_cue_vol[c]);
        if (!WritePrivateProfileStringW(L"audio", key, val, g_ini_path)) {
            /* Not fatal: the level is already live in memory and the session
             * plays correctly; only persistence is lost. Logged so a "my
             * volumes reset" report has an answer in the log. */
            log_line("audio: ⚠ could not save %ls to the ini (err %lu) -- the "
                     "level is live but will not survive a restart",
                     key, GetLastError());
            return;
        }
    }
}

/* ---- voices -------------------------------------------------------------
 * One voice per (gauge, player). Everything the mixer needs lives here, so the
 * mix loop is uniform -- no named globals, no per-cue special cases. That is
 * the whole reason this was written fresh rather than adapted: the reference
 * implementation hardcoded one beacon, one hiss and one earcon slot, and
 * panned only the beacon. */
typedef struct {
    float phase;        /* oscillator phase, 0..1                          */
    float freq;         /* target frequency, Hz                            */
    /* ⛔⛔ A PLUCK NEEDS TWO RATES. `phase`/`freq` carry the TONE (hundreds
     * of Hz); these carry how often the pluck REPEATS (a few Hz). Driving
     * both from one ramp put every partial at 9-49 Hz -- inaudible, which is
     * exactly what the user heard. Keep them independent. */
    float pulse_phase;  /* 0..1 across one pluck                           */
    float pulse_rate;   /* plucks per second; 0 = not a pulsed voice       */
    float sm_freq;      /* smoothed frequency (pitch glides, never jumps)  */
    float gain;         /* target gain 0..1                                */
    float sm_gain;      /* smoothed gain -- silence is gain 0, not a flag  */
    float pan;          /* -1 left .. +1 right                             */
    float sm_gl, sm_gr; /* smoothed per-voice channel gains                */
    int   timbre;       /* which waveform generator                        */
    BOOL  active;
} voice;

/* Voice slots: the gauge voices first, then the two PERSISTENT BEACONS.
 * Beacons occupy fixed slots at the end so the mix loop stays uniform. */
#define BEACON_VOICE(p)  (AUDIO_GAUGE_COUNT * AUDIO_PARTICIPANTS + (p))
#define VOICE_COUNT      (AUDIO_GAUGE_COUNT * AUDIO_PARTICIPANTS + AUDIO_PARTICIPANTS)
static voice g_voice[VOICE_COUNT];

/* Per-gauge tone character. Timbre identifies the gauge, so these must stay
 * audibly distinct; the base frequencies are deliberately spread apart. */
typedef struct { float f_lo, f_hi; int timbre; } gauge_tone;
/* ⛔⛔ THE FIRST PAIRING WAS NOT DISTINCT ENOUGH (user-reported, 2026-09-19).
 * It was super sine 330-990 and health triangle 180-540, and it failed on BOTH
 * axes at once:
 *
 *  1. THE RANGES OVERLAPPED. 330-540 Hz was shared, so a FULL health tone and
 *     an EMPTY super tone were literally the same pitch. Pitch is supposed to
 *     encode fill; when two gauges share a band it encodes nothing.
 *  2. AT EQUAL FILL THE RATIO WAS ALWAYS 1.83 -- close to a perfect fifth, so
 *     the two tones sat in a consonant interval and FUSED into one sound
 *     rather than separating. Two simultaneous gauges is the normal case.
 *  3. Sine vs triangle is a weak timbre contrast: a triangle is a sine with
 *     quiet odd harmonics, which is subtle over a noisy fight.
 *
 * ⭐ NOW SEPARATED ON BOTH AXES, with non-overlapping registers:
 *     health  150-420 Hz  triangle       -- low and warm
 *     super   600-1500 Hz softened square -- high and reedy
 * Health tops out at 420 while super starts at 600, so no fill combination can
 * put them on the same pitch; at equal fill they are ~3.6x apart, which is not
 * a consonant interval and does not fuse.
 *
 * ⭐ WHICH IS WHICH IS NOT ARBITRARY: health is the one being watched while
 * under pressure, so it gets the LOW warm register that stays legible under
 * game audio; the super meter gets the brighter register because it is the
 * less critical of the two and a bright tone is easier to ignore.
 *
 * ⚠ The test pins these as audibly distinct. If either is retuned, re-measure
 * -- do not assume a new pair separates just because the numbers differ. */
/* ⛔⛔ TRIANGLE vs "SOFTENED SQUARE" WAS STILL TOO SIMILAR (user, 2026-09-19,
 * after hearing them in play). The softened square was only two harmonics
 * (fundamental + 1/3 third), which is a triangle with slightly more edge --
 * a difference of DEGREE within one family, not of kind. Separating tones by
 * register alone does not work when the timbres are this close.
 *
 * ⭐ SQUARE for health (user's choice, kept -- they confirmed they like it).
 *
 * ⭐ SUPER IS A TRIANGLE (user, 2026-09-19, revising their own sawtooth
 * suggestion after hearing it). Square vs saw separated by WHICH harmonics
 * exist (odd vs all), but both are harsh and they competed for attention.
 * Square vs triangle share the odd-harmonic series and differ in HOW MUCH
 * of it:
 *
 *     square   odd, 1/n     h3 0.333  h5 0.200  h7 0.143   buzzy, hollow
 *     triangle odd, 1/n^2   h3 0.111  h5 0.040  h7 0.020   soft, near-sine
 *
 * That is a far bigger brightness gap than square-vs-saw, so the two sit in
 * different perceptual layers instead of fighting: health is the one that
 * cuts through, the super meter stays easy to ignore -- which matches which
 * of the two the player is tracking under pressure.
 *
 * ⚠ The triangle needs no band-limiting (its 1/n^2 rolloff makes aliasing
 * negligible); the square does, via polyblep. */
static const gauge_tone g_tone[AUDIO_GAUGE_COUNT] = {
    /* SUPER  */ { 600.0f, 1500.0f, 1 },  /* triangle: soft, pure, high   */
    /* HEALTH */ { 150.0f,  420.0f, 2 },  /* square: hollow, woody, low   */
};

/* ---- beacons ------------------------------------------------------------
 * ⭐⭐ THE CORE OF THE USER'S DESIGN (memory cfc2-audio-design-decision), and
 * it was MISSING until 2026-09-19: "each player gets their own persistent
 * looping cue", pan = X position, pitch = stance. **The beacons NEVER go
 * silent** -- a vanishing beacon is ambiguous (dead? stunned? bug?).
 *
 * ⛔ I previously wired fighter position to the GAUGE pan, which was wrong in
 * two ways at once. Gauge pan is fixed to the HUD BY DESIGN (P1 left, P2
 * right, mirroring the on-screen bars), and gauges sound only while a value
 * CHANGES -- so a walking fighter moved nothing and made no sound. The user
 * reported hearing no new sound, which was exactly right.
 *
 * ⚠ The two layers are deliberate and must not be merged:
 *     gauge pan  = FIXED by owner   (HUD chrome)
 *     beacon pan = MOVES with the fighter (the world)
 *
 * TIMBRE. ⛔ The first version used SINES between the gauge registers and the
 * user reported them "a bit too close to the gauges". Separating by pitch and
 * waveform alone is too weak: sine, triangle and square are all steady
 * pitched harmonic tones, i.e. one family.
 *
 * ⭐ The beacons are now PLUCKS (osc timbre 3): inharmonic partials, a sharp
 * decaying envelope, repeating at a rate. That differs from the gauges in
 * KIND -- spectrum, envelope and rhythm at once -- so no pitch comparison is
 * needed to tell a beacon from a gauge.
 *
 * The two beacons differ from EACH OTHER by PULSE RATE, which is audible
 * regardless of where they sit in the stereo field (pan alone is ambiguous
 * when both fighters are near centre, exactly when precision matters):
 *
 *     P1  9 plucks/sec       P2  6.2 plucks/sec
 *
 * ⚠ Not a 2:1 ratio -- that would lock into a single rhythm and fuse. ~1.45:1
 * stays audibly two separate streams.
 *
 * STANCE multiplies the rate, so the ratio between the two players is
 * preserved at every stance:
 *     knocked down 0.70   crouching 0.85   standing 1.00   airborne 1.15+
 *
 * ⚠ Stance is NOT YET WIRED -- Y is located (B+0x58) but no capture samples a
 * jump, and the ladder also needs a state byte to separate crouch from stand.
 * Until then every beacon sits at AUDIO_STANCE_STAND, which is honest: a
 * wrong stance is worse than a constant one. */
#define BEACON_GAIN   0.18f            /* under the gauges: always-on needs
                                        * to sit below occasional cues       */
/* ⚠ TWO independent rates per beacon. Collapsing them into one put every
 * partial at 9-49 Hz -- below hearing, so the beacons were silent in game
 * while still passing amplitude and pulse-count tests.
 *   tone = the pitch you hear within each pluck (hundreds of Hz)
 *   rate = how often the pluck repeats (a few Hz)
 * P1 is higher and faster, P2 lower and slower: two cues agreeing, so the
 * players stay distinct even if one axis is masked by game audio. */
static const float g_beacon_tone[AUDIO_PARTICIPANTS] = { 700.0f, 470.0f, 930.0f, 330.0f };
static const float g_beacon_rate[AUDIO_PARTICIPANTS] = { 9.0f, 6.2f, 12.7f, 4.3f };
typedef struct {
    float pan;                          /* -1..+1, fighter's screen X        */
    float stance;                       /* pitch multiplier, see ladder      */
    BOOL  live;                         /* in a match: sound continuously    */
} beacon_state;
/* Defaults match the HUD placement so anything before a match is sensible. */
static beacon_state g_beacon[AUDIO_PARTICIPANTS] = {
    { -0.85f, 1.0f, FALSE }, { 0.85f, 1.0f, FALSE }
};

/* ---- gauge tracking -----------------------------------------------------
 * The change rule: a tone sounds only while the value is MOVING. We hold the
 * last value and a decay timer. Any change re-arms the timer; when it expires
 * the voice fades to silence.
 *
 * The timer counts BLOCKS, not samples: audio_gauge_set() is called from the
 * worker (20 ms) while the mixer runs per block (23 ms), and tying the lifetime
 * to the render side keeps it independent of how often the caller polls. */
/* ⚠ Recalibrated when AUDIO_BLOCK went 1024 -> 256. This counts BLOCKS, so
 * shrinking the block silently shortened the hold from ~140 ms to ~35 ms and
 * the tone would have become a click. 24 x 5.8 ms restores ~140 ms.
 * A constant expressed in someone else's units breaks when those units move. */
#define HOLD_BLOCKS 24                 /* ~140 ms of tone after a change */
typedef struct {
    int   last;
    BOOL  seeded;
    int   hold;
    float level;                       /* 0..1, the gauge fill fraction */
    float pan;
} gauge_state;
static gauge_state g_gauge[AUDIO_GAUGE_COUNT][AUDIO_PARTICIPANTS];

/* ⛔ The lock must exist before the FIRST caller, not only after audio_init().
 * It was originally created in audio_init() alone, so when the device failed
 * to open -- or in the offline suite, which never opens one -- every
 * audio_gauge_set() returned early and silently discarded the value. The
 * offline suite caught it: five cases that assert on SAMPLES failed, while
 * five that assert on SILENCE passed trivially. A test that only checks for
 * quiet cannot tell "correctly silent" from "never produced anything".
 *
 * InitOnceExecuteOnce, so concurrent first-callers cannot race. */
static CRITICAL_SECTION g_lock;
static volatile LONG g_scene_owned;
static BOOL g_scene_running, g_scene_pending, g_scene_reset;
static uint64_t g_scene_epoch, g_scene_sequence;
static BOOL g_scene_has_sequence;
static ULONGLONG g_scene_received;
static audio_scene g_scene;
static audio_scene_participant g_scene_previous[AUDIO_PARTICIPANTS];
static beacon_state g_scene_beacons[AUDIO_PARTICIPANTS];
static INIT_ONCE g_lock_once = INIT_ONCE_STATIC_INIT;

#ifdef AUDIO_SCENE_TEST
extern ULONGLONG audio_scene_test_now(void);
#define scene_now audio_scene_test_now
#else
#define scene_now GetTickCount64
#endif

static BOOL CALLBACK lock_init_cb(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_lock);
    return TRUE;
}
static void lock_ensure(void)
{
    InitOnceExecuteOnce(&g_lock_once, lock_init_cb, NULL, NULL);
}

BOOL audio_scene_begin(uint64_t epoch)
{
    lock_ensure();
    EnterCriticalSection(&g_lock);
    BOOL accepted = epoch && epoch > g_scene_epoch;
    if (accepted) {
        InterlockedExchange(&g_scene_owned, 1);
        g_scene_epoch = epoch;
        g_scene_running = TRUE;
        g_scene_has_sequence = FALSE;
        g_scene_pending = FALSE;
        g_scene_received = 0;
        g_scene_reset = TRUE;
    }
    LeaveCriticalSection(&g_lock);
    return accepted;
}

static BOOL scene_valid(const audio_scene *s)
{
    for (unsigned p = 0; p < AUDIO_PARTICIPANTS; p++) {
        const audio_scene_participant *a = &s->participants[p];
        if (!a->present) continue;
        if (a->beacon_valid && (!isfinite(a->screen_pan) ||
            a->screen_pan < -1 || a->screen_pan > 1 ||
            !isfinite(a->stance) || a->stance < 0.5f || a->stance > 2)) return FALSE;
        for (int g = 0; g < AUDIO_GAUGE_COUNT; g++) {
            const audio_scene_gauge *v = &a->gauges[g];
            if (v->valid && (v->maximum <= 0 || v->value < 0 ||
                v->value > v->maximum || !isfinite(v->hud_pan) ||
                v->hud_pan < -1 || v->hud_pan > 1)) return FALSE;
        }
    }
    return TRUE;
}

BOOL audio_scene_publish(const audio_scene *scene)
{
    if (!scene) return FALSE;
    /* Caller owns immutable input for this call. Copy only bounded value data. */
    audio_scene next = *scene;
    if (!scene_valid(&next)) return FALSE;
    lock_ensure();
    if (!TryEnterCriticalSection(&g_lock)) return FALSE;
    BOOL accepted = g_scene_running && next.epoch == g_scene_epoch &&
        (!g_scene_has_sequence || next.sequence > g_scene_sequence);
    if (accepted) {
        g_scene = next;
        g_scene_sequence = next.sequence;
        g_scene_has_sequence = TRUE;
        g_scene_received = scene_now();
        g_scene_pending = TRUE;
    }
    LeaveCriticalSection(&g_lock);
    return accepted;
}

void audio_scene_stop(uint64_t epoch)
{
    lock_ensure();
    EnterCriticalSection(&g_lock);
    if (epoch && epoch == g_scene_epoch) {
        g_scene_running = FALSE;
        g_scene_pending = FALSE;
        g_scene_reset = TRUE;
    }
    LeaveCriticalSection(&g_lock);
}

/* Mixer thread only, under g_lock. A full snapshot removes absent slots;
 * participant/resource replacement seeds gauges without a false drain. */
static void scene_consume(void)
{
    if (!g_scene_owned) return;
    BOOL expired = scene_now() - g_scene_received >= AUDIO_SCENE_TTL_MS;
    if (g_scene_reset || !g_scene_running || !g_scene_has_sequence ||
        expired) {
        memset(g_gauge, 0, sizeof g_gauge);
        memset(g_scene_previous, 0, sizeof g_scene_previous);
        memset(g_scene_beacons, 0, sizeof g_scene_beacons);
        /* Voice oscillators belong to this thread. Reset here, never from a
         * producer while a block is being rendered. Expiry prevents replay. */
        if (g_scene_reset) memset(g_voice, 0, sizeof g_voice);
        g_scene_reset = FALSE;
        if (!g_scene_running || !g_scene_has_sequence ||
            expired)
            g_scene_pending = FALSE;
    }
    if (!g_scene_pending) return;
    g_scene_pending = FALSE;
    for (unsigned p = 0; p < AUDIO_PARTICIPANTS; p++) {
        const audio_scene_participant *a = &g_scene.participants[p];
        audio_scene_participant *old = &g_scene_previous[p];
        BOOL replacement = !old->present || a->incarnation != old->incarnation;
        g_scene_beacons[p] = (beacon_state){ a->screen_pan, a->stance,
                                              a->present && a->beacon_valid };
        for (int g = 0; g < AUDIO_GAUGE_COUNT; g++) {
            gauge_state *st = &g_gauge[g][p];
            const audio_scene_gauge *v = &a->gauges[g];
            if (!a->present || !v->valid) {
                memset(st, 0, sizeof *st);
                continue;
            }
            if (replacement || !old->gauges[g].valid ||
                old->gauges[g].generation != v->generation ||
                old->gauges[g].maximum != v->maximum) {
                st->seeded = FALSE;
                st->hold = 0;
            }
            st->pan = v->hud_pan;
            st->level = (float)v->value / (float)v->maximum;
            if (st->seeded && st->last != v->value) st->hold = HOLD_BLOCKS;
            st->last = v->value;
            st->seeded = TRUE;
        }
        *old = *a;
    }
}

/* ---- generators --------------------------------------------------------- */
/* ⭐ PolyBLEP: a band-limited step correction.
 *
 * ⛔ A NAIVE square or saw is an infinite harmonic series, so everything
 * above Nyquist folds back as inharmonic ALIASING -- audible as grit, worst
 * at high pitch, and the super gauge reaches 1500 Hz. Summing only the
 * harmonics below Nyquist would be correct but costs up to 147 sinf() calls
 * PER SAMPLE at the low end, on the render thread. (COUNT THE WORK BEFORE
 * PUTTING IT IN A PER-SAMPLE LOOP -- this project's recurring wall.)
 *
 * PolyBLEP instead rounds off each discontinuity over one sample either
 * side, which removes most of the aliasing for a few arithmetic ops. `t` is
 * the phase, `dt` the phase increment per sample. */
static float polyblep(float t, float dt)
{
    if (dt <= 0.0f) return 0.0f;
    if (t < dt) {                       /* just after the step */
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {                /* just before the next one */
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* `dt` is the per-sample phase increment, needed only by the band-limited
 * waveforms; the others ignore it. */
static float osc(int timbre, float phase, float dt)
{
    switch (timbre) {
    case 0: return sinf(phase * 2.0f * (float)M_PI);
    case 1: {   /* triangle */
        float t = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
        return t * 2.0f - 1.0f;
    }
    /* ⭐ HEALTH -- band-limited SQUARE (user's choice, 2026-09-19).
     * Odd harmonics only: hollow and woody. Its harmonic content differs
     * from the saw in KIND (which harmonics exist), not merely in brightness,
     * which is why the two are easy to tell apart even at the same pitch. */
    case 2: {
        float v = phase < 0.5f ? 1.0f : -1.0f;
        v += polyblep(phase, dt);
        v -= polyblep(fmodf(phase + 0.5f, 1.0f), dt);
        return v * 0.7f;
    }
    /* ⭐ SUPER -- band-limited SAWTOOTH (user's choice). ALL harmonics, so
     * it is brighter and buzzier than the square at any pitch. */
    case 4: {
        float v = 2.0f * phase - 1.0f;
        v -= polyblep(phase, dt);
        return v * 0.7f;
    }
    /* ⭐ TIMBRE 3 -- THE BEACON. Deliberately NOT another sustained harmonic
     * tone. The user's report: the beacons "sound a bit too close to the
     * gauges... something very sonically different".
     *
     * ⛔ The mistake was separating them only by PITCH and waveform. Sine,
     * triangle and softened square are the same KIND of sound -- steady,
     * pitched, harmonic -- so they read as one family however far apart their
     * registers are. Distinguishing within a family is a weak contrast.
     *
     * This is a PLUCK: a bright inharmonic attack that decays into near
     * silence, repeated. Three differences from the gauges at once --
     *   - inharmonic partials (2.76x, 5.40x: bell/metal ratios, not a
     *     harmonic series), so it does not fuse with a pitched tone;
     *   - a sharp attack and decay ENVELOPE, where the gauges are flat;
     *   - a rhythmic pulse, so it occupies time differently.
     * Category, not degree. A listener never has to compare pitches to know
     * which is which.
     *
     * ⚠ `phase` here is the TONE, advancing at `freq` (hundreds of Hz). The
     * envelope comes from `pulse_phase` and is applied by the caller.
     *
     * ⚠ Timbre 3, kept as `default` so an unset voice is audible rather than
     * silent -- but the gauges now use 2 and 4, so the numbering is no longer
     * contiguous. Add new timbres with an explicit case. */
    case 3:
    default: {
        float s1 = sinf(phase * 2.0f * (float)M_PI);
        float s2 = sinf(phase * 2.0f * 2.76f * (float)M_PI) * 0.50f;
        float s3 = sinf(phase * 2.0f * 5.40f * (float)M_PI) * 0.25f;
        return (s1 + s2 + s3) * 0.85f;
    }
    }
}

/* Equal-power pan: -1..+1 into per-channel gains of constant total power. */
/* ⛔⛔ THE SINE/COSINE PAN LAW WAS PERCEPTUALLY UNEVEN (user, 2026-09-19):
 * "the panning needs to average out so that each step is equally weighted in
 * terms of the degree of pan", and the beacons felt "too wide".
 *
 * Measured, for equal steps of `pan`, the inter-channel difference the ear
 * actually localises by:
 *
 *     near centre   1.7 dB per 1/8 step
 *     near +/-0.9   6.1 dB per 1/8 step
 *     at the edge   ~100 dB   (one channel reaches exact silence)
 *
 * So a fighter's last step into the corner threw the image far harder than a
 * step across the middle, and hard-panned audio collapses into one ear. The
 * angle is linear in `pan`, but LOUDNESS DIFFERENCE is not linear in angle.
 *
 * ⭐ THIS LAW IS LINEAR IN THE THING THE EAR MEASURES. The channel difference
 * is PAN_SPAN dB per unit of pan, everywhere -- every step is worth the same
 * amount of apparent movement, by construction, which is exactly what was
 * asked for. Gains are then normalised so total power is constant, so moving
 * across the field does not change loudness.
 *
 * ⚠ PAN_SPAN also sets the WIDTH: at +/-1 the difference is +/-PAN_SPAN dB
 * rather than total silence in one ear, so the image stays in the listener's
 * head instead of splitting. 15 dB is a firmly-placed but not extreme image;
 * raise it for a wider field, lower it for a narrower one. This is the one
 * knob for "too wide"/"too narrow". */
#define PAN_SPAN 15.0f                 /* dB difference at full deflection */

static void pan_gains(float pan, float *gl, float *gr)
{
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    /* Half the span each way, so the two channels differ by PAN_SPAN dB at
     * the extremes. Negative pan = louder on the left. */
    float half = pan * (PAN_SPAN * 0.5f);
    float l = powf(10.0f, -half / 20.0f);
    float r = powf(10.0f,  half / 20.0f);
    /* Constant power: loudness must not change as the fighter crosses. */
    float n = sqrtf((l * l + r * r) * 0.5f);
    *gl = l / n;
    *gr = r / n;
}

/* ---- public API --------------------------------------------------------- */
void audio_gauge_set(audio_gauge g, unsigned player, int value, int maxv)
{
    if ((int)g < 0 || g >= AUDIO_GAUGE_COUNT || player > 1 || maxv <= 0)
        return;
    lock_ensure();

    EnterCriticalSection(&g_lock);
    if (g_scene_owned) { LeaveCriticalSection(&g_lock); return; }
    gauge_state *st = &g_gauge[g][player];
    st->pan = (player == 0) ? -0.85f : 0.85f;
    float lvl = (float)value / (float)maxv;
    if (lvl < 0.0f) lvl = 0.0f;
    if (lvl > 1.0f) lvl = 1.0f;
    st->level = lvl;

    if (!st->seeded) {
        /* First sighting must NOT sound: with no history, a value arriving is
         * not a change. Seeding silently is what stops a round start from
         * screaming every gauge at once. */
        st->last = value;
        st->seeded = TRUE;
        st->hold = 0;
    } else if (value != st->last) {
        st->last = value;
        st->hold = HOLD_BLOCKS;
    }
    LeaveCriticalSection(&g_lock);
}

void audio_gauge_reset(audio_gauge g, unsigned player)
{
    if ((int)g < 0 || g >= AUDIO_GAUGE_COUNT || player > 1) return;
    lock_ensure();
    EnterCriticalSection(&g_lock);
    if (g_scene_owned) { LeaveCriticalSection(&g_lock); return; }
    g_gauge[g][player].seeded = FALSE;
    g_gauge[g][player].hold = 0;
    LeaveCriticalSection(&g_lock);
}

void audio_gauge_reset_all(void)
{
    for (int g = 0; g < AUDIO_GAUGE_COUNT; g++)
        for (unsigned p = 0; p < AUDIO_PARTICIPANTS; p++)
            audio_gauge_reset((audio_gauge)g, p);
}

/* ---- beacon control -----------------------------------------------------
 * Called from the game's FRAME THREAD (src/cvs2_frame.c), which derives each
 * fighter's screen X from the two world positions.
 *
 * ⛔⛔ DELIBERATELY LOCK-FREE. Every gauge setter here takes g_lock, and
 * taking it from the frame thread is exactly what made the health tone
 * crackle: the render thread needs the same lock every 5.8 ms and has only
 * 23 ms of buffer. These are plain scalar stores -- the mixer may read a
 * value one block stale, which is inaudible in a pan that moves smoothly.
 *
 * ⚠ Aligned 4-byte stores are atomic in practice on x86-64. `volatile` stops
 * the compiler caching or reordering them; no barrier is needed because
 * nothing depends on their ordering against other state. */
void audio_beacon_set(unsigned player, float pan, float stance)
{
    if (player > 1 || g_scene_owned || !isfinite(pan) || !isfinite(stance)) return;
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    if (stance < 0.5f) stance = 0.5f;      /* keep it a musical multiplier */
    if (stance > 2.0f) stance = 2.0f;
    g_beacon[player].pan    = pan;
    g_beacon[player].stance = stance;
    g_beacon[player].live   = TRUE;
}

void audio_beacons_off(void)
{
    /* Leaving a match. ⭐ "Never silent" is a rule about DURING a match --
     * outside one there are no fighters to locate, and a tone droning over
     * the menus would be noise. Placement returns to the HUD default so a
     * new match does not start from the last one's position. */
    for (unsigned p = 0; p < AUDIO_PARTICIPANTS; p++) {
        g_beacon[p].live   = FALSE;
        g_beacon[p].stance = 1.0f;
        g_beacon[p].pan    = (p == 0) ? -0.85f : 0.85f;
    }
}

BOOL audio_ready(void) { return g_ready; }

BOOL audio_toggle_enabled(void)
{
    g_enabled = !g_enabled;
    return g_enabled;
}

/* ---- mixer -------------------------------------------------------------- */
static void update_voices(void)
{
    scene_consume();
    for (int g = 0; g < AUDIO_GAUGE_COUNT; g++) {
        for (unsigned p = 0; p < AUDIO_PARTICIPANTS; p++) {
            gauge_state *st = &g_gauge[g][p];
            voice *v = &g_voice[g * AUDIO_PARTICIPANTS + p];
            const gauge_tone *t = &g_tone[g];

            v->timbre = t->timbre;
            /* ⭐ Gauge pan is FIXED BY OWNER, matching the HUD: P1 bars left,
             * P2 bars right. This is deliberate and is NOT the fighter's
             * position -- the beacons carry that. Two layers, as on screen:
             * fixed UI chrome vs the moving world. */
            v->pan = st->pan;
            /* Pitch rises with fill. */
            v->freq = t->f_lo + (t->f_hi - t->f_lo) * st->level;

            /* ⭐ The user's per-cue level scales the designed gain. Applied
             * to the TARGET, so the existing smoother ramps into a volume
             * change exactly as it ramps into a hold expiring -- a level
             * change never clicks, and muting mid-tone fades rather than
             * cutting. */
            float cv = cue_gain(g == AUDIO_GAUGE_HEALTH ? AUDIO_CUE_HEALTH
                                                        : AUDIO_CUE_SUPER);
            if (st->hold > 0) {
                st->hold--;
                v->gain = 0.35f * cv;
                v->active = TRUE;
            } else {
                v->gain = 0.0f;   /* fades out; retired once truly silent */
            }
        }
    }

    /* ---- the two persistent beacons -------------------------------------
     * ⭐ Unlike a gauge, a beacon has NO hold timer and NO change rule: while
     * a match is live it sounds continuously, because that is what makes it a
     * position channel rather than an event. Its pan and pitch simply track
     * whatever the frame hook last published. */
    for (unsigned p = 0; p < AUDIO_PARTICIPANTS; p++) {
        voice *v = &g_voice[BEACON_VOICE(p)];
        beacon_state b = g_scene_owned ? g_scene_beacons[p] : g_beacon[p];
        v->timbre = 3;                         /* pluck -- see osc()        */
        v->pan    = b.pan;
        /* ⚠ For the pluck, `freq` is the PULSE RATE, not a pitch: the
         * envelope and the partials are both generated from one phase ramp,
         * so the ramp must run at the repeat rate. Stance therefore raises
         * the TONE pitch and the repeat rate together, which reads naturally
         * as "higher". */
        v->freq       = g_beacon_tone[p] * b.stance;
        v->pulse_rate = g_beacon_rate[p] * b.stance;
        if (b.live) {
            /* Both beacons share ONE level: they are the same cue heard from
             * two sides, and turning one player down would make the pan
             * readout lie about which side is which. */
            v->gain   = BEACON_GAIN * cue_gain(AUDIO_CUE_BEACON);
            v->active = TRUE;
        } else {
            v->gain = 0.0f;       /* outside a match; fades, never clicks */
        }
    }
}

/* ---- one-shot ticks -----------------------------------------------------
 * A small ring of independent decaying blips. A ring rather than one slot so
 * that ticks one second apart never cut each other off, and so a tick can
 * overlap a gauge tone without either being stolen.
 *
 * ⚠ audio_tick() is called from the game's frame thread. It takes NO lock:
 * it claims a slot with InterlockedIncrement and writes only that slot, and
 * the render thread reads slots it does not write. Taking the mixer lock here
 * is what made the health tone crackle. */
#define TICK_SLOTS 4
/* ⚠ `vol` is sampled when the tick is ARMED, not read per sample in the
 * render loop. A tick has no gain smoother -- it is a bare decaying envelope
 * -- so reading the live level mid-decay would step the amplitude of a blip
 * already sounding. Latching it means a level change takes effect on the NEXT
 * tick, which is inaudible at ~40 ms per blip and cannot produce a click. */
typedef struct { volatile LONG armed; float phase, env, vol; } tick_voice;
static tick_voice g_tick[TICK_SLOTS];
static volatile LONG g_tick_next;

void audio_tick(void)
{
    if (g_scene_owned) return;
    LONG i = InterlockedIncrement(&g_tick_next) - 1;
    tick_voice *t = &g_tick[((unsigned)i) % TICK_SLOTS];
    t->phase = 0.0f;
    t->env   = 1.0f;
    t->vol   = cue_gain(AUDIO_CUE_TICK);
    InterlockedExchange(&t->armed, 1);
}

static void render(int16_t *out)
{
    memset(out, 0, sizeof(int16_t) * AUDIO_BLOCK * 2);

    lock_ensure();
    EnterCriticalSection(&g_lock);
    update_voices();
    LeaveCriticalSection(&g_lock);

    BOOL on = g_enabled;

    for (int i = 0; i < AUDIO_BLOCK; i++) {
        float l = 0.0f, r = 0.0f;

        for (int vi = 0; vi < VOICE_COUNT; vi++) {
            voice *v = &g_voice[vi];
            if (!v->active) continue;

            float gt = on ? v->gain : 0.0f;
            /* Smooth gain and frequency: an abrupt gain change is an audible
             * click, and a jumping pitch reads as a different event rather
             * than the same gauge moving. */
            /* ⭐ FAST ATTACK, slow release. The user needs the onset keyed to
             * the frame the damage lands, so a rising gain must reach full in
             * ~1 ms (0.05 -> 50% in 14 samples); a falling one keeps the old
             * gentle ramp so the tail does not click. Symmetric smoothing put
             * ~4 ms of ramp in front of every hit, which is audible slop on a
             * game this fast. */
            v->sm_gain += (gt > v->sm_gain ? 0.05f : 0.004f) * (gt - v->sm_gain);
            /* ⚠ 0.010 restored. I changed this to 0.0006 believing it caused the
             * health crackle; measuring the smoother directly showed the
             * coefficient makes NO difference (7.2 Hz per feed at every value
             * from 0.010 to 0.0003). The cause was lock contention in the
             * frame hook. Do not re-tune this without measuring first. */
            if (v->sm_freq <= 0.0f) v->sm_freq = v->freq;
            v->sm_freq += 0.010f * (v->freq - v->sm_freq);

            float gl, gr;
            pan_gains(v->pan, &gl, &gr);
            v->sm_gl += 0.010f * (gl - v->sm_gl);
            v->sm_gr += 0.010f * (gr - v->sm_gr);

            float dt = v->sm_freq / (float)AUDIO_SR;   /* phase step, for polyblep */
            v->phase += dt;
            if (v->phase >= 1.0f) v->phase -= 1.0f;

            /* Pulsed voices (the beacons) run a SECOND, much slower ramp that
             * drives the pluck envelope. Steady voices leave pulse_rate at 0
             * and are unaffected. */
            float env = 1.0f;
            if (v->pulse_rate > 0.0f) {
                v->pulse_phase += v->pulse_rate / (float)AUDIO_SR;
                if (v->pulse_phase >= 1.0f) v->pulse_phase -= 1.0f;
                /* Sharp attack, fast decay -- most of each cycle is near
                 * silence, which is what makes it read as a pluck. */
                env = expf(-7.0f * v->pulse_phase);
            }

            float s = osc(v->timbre, v->phase, dt) * env * v->sm_gain * g_master;
            l += s * v->sm_gl;
            r += s * v->sm_gr;

            /* Retire a voice only once it is genuinely inaudible, so a
             * fade-out is never cut off mid-ramp. */
            if (v->gain == 0.0f && v->sm_gain < 0.0005f) {
                v->active = FALSE;
                v->sm_gain = 0.0f;
            }
        }

        /* Clock ticks: short decaying blips, centre panned so they sit apart
         * from the owner-panned gauges. A wooden click rather than a tone --
         * fast decay on a mid frequency. */
        for (int ti = 0; ti < TICK_SLOTS; ti++) {
            tick_voice *t = &g_tick[ti];
            /* Once the scene router owns output, legacy ticks cannot leak
             * across its boundary, including ticks armed before the claim. */
            if (g_scene_owned) { InterlockedExchange(&t->armed, 0); continue; }
            if (!t->armed) continue;
            t->phase += 1200.0f / (float)AUDIO_SR;
            if (t->phase >= 1.0f) t->phase -= 1.0f;
            /* ⚠ 0.9965, not 0.9994. I wrote 0.9994 with the comment "~40 ms"
             * without checking: it actually decays over 235 ms, which is a
             * sustained blip rather than a clock tick, and the offline suite
             * caught it. 0.9965 really is ~40 ms. Compute the constant, do
             * not assert it. */
            t->env *= 0.9965f;                 /* ~40 ms to inaudible */
            float ts = sinf(t->phase * 2.0f * (float)M_PI) * t->env * 0.5f
                     * t->vol * (on ? 1.0f : 0.0f) * g_master;
            l += ts; r += ts;
            if (t->env < 0.002f) { t->env = 0.0f; InterlockedExchange(&t->armed, 0); }
        }

        if (l >  1.0f) l =  1.0f;
        if (l < -1.0f) l = -1.0f;
        if (r >  1.0f) r =  1.0f;
        if (r < -1.0f) r = -1.0f;
        out[i * 2]     = (int16_t)(l * 32000.0f);
        out[i * 2 + 1] = (int16_t)(r * 32000.0f);
    }
}

void audio_render_block_for_test(int16_t *out) { render(out); }

/* ---- device thread ------------------------------------------------------ */
static DWORD WINAPI audio_thread(LPVOID param)
{
    (void)param;
    while (InterlockedCompareExchange(&g_run, 1, 1)) {
        BOOL did = FALSE;
        for (int i = 0; i < AUD_NBUF; i++) {
            if (g_hdr[i].dwFlags & WHDR_DONE) {
                render(g_buf[i]);
                g_hdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(g_dev, &g_hdr[i], sizeof(WAVEHDR));
                did = TRUE;
            }
        }
        if (!did) WaitForSingleObject(g_event, 50);
    }
    return 0;
}

static void read_ini(const wchar_t *game_dir)
{
    if (!game_dir) return;
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\cfc2_audio.ini", game_dir);
    int v = (int)GetPrivateProfileIntW(L"audio", L"volume", 60, path);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    g_master = (float)v / 100.0f;

    /* ⭐ Remember where to write the hotkey changes back. Captured here rather
     * than passed to each setter so a hotkey handler needs no game-dir. */
    wcsncpy(g_ini_path, path, MAX_PATH - 1);
    g_ini_path[MAX_PATH - 1] = 0;

    /* Per-cue levels, defaulting to 100 = the balance tuned by ear. An ini
     * with no vol_* keys therefore sounds exactly as it did before these
     * existed, which is what makes the default safe. */
    for (int c = 0; c < AUDIO_CUE_COUNT; c++) {
        const wchar_t *key = cue_ini_key((audio_cue)c);
        if (!key) continue;
        int cv = (int)GetPrivateProfileIntW(L"audio", key, 100, path);
        audio_cue_set_volume((audio_cue)c, cv);
    }
}

void audio_init(const wchar_t *game_dir)
{
    if (g_ready) return;
    lock_ensure();
    read_ini(game_dir);

    WAVEFORMATEX wf;
    memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = AUDIO_SR;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    g_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    MMRESULT r = waveOutOpen(&g_dev, WAVE_MAPPER, &wf, (DWORD_PTR)g_event, 0,
                             CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        /* Not fatal and not silent: the mod keeps running, speech is
         * unaffected, and the log says why there are no cues. */
        log_line("audio: waveOutOpen failed (%u) -- cues disabled, speech "
                 "unaffected", (unsigned)r);
        if (g_event) { CloseHandle(g_event); g_event = NULL; }
        return;
    }

    for (int i = 0; i < AUD_NBUF; i++) {
        memset(&g_hdr[i], 0, sizeof(WAVEHDR));
        g_hdr[i].lpData = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = sizeof(g_buf[i]);
        waveOutPrepareHeader(g_dev, &g_hdr[i], sizeof(WAVEHDR));
        render(g_buf[i]);
        waveOutWrite(g_dev, &g_hdr[i], sizeof(WAVEHDR));
    }

    g_ready = TRUE;
    InterlockedExchange(&g_run, 1);
    g_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
    /* ⭐ The OTHER half of the crackle. This thread refills the device queue;
     * at NORMAL priority it competes with the game's render thread and loses
     * often enough to underrun. TIME_CRITICAL is the standard priority for an
     * audio callback thread and is safe here because the thread does bounded
     * work per wake (render one block, write it) and then blocks on an event
     * -- it cannot spin and starve anything.
     *
     * ⚠ Not fatal if it fails: a refused priority leaves a working but
     * underrun-prone thread, which is the behaviour we already had. Logged
     * rather than assumed, so a future crackle report can rule it in or out. */
    if (g_thread) {
        if (!SetThreadPriority(g_thread, THREAD_PRIORITY_TIME_CRITICAL))
            log_line("audio: ⚠ could not raise device-thread priority (%lu) -- "
                     "cues may crackle under load", GetLastError());
    }
    log_line("audio: waveOut open, %d x %d frames at %d Hz, volume %d%%",
             AUD_NBUF, AUDIO_BLOCK, AUDIO_SR, (int)(g_master * 100.0f));
}

void audio_shutdown(void)
{
    if (!g_ready) return;
    InterlockedExchange(&g_run, 0);
    if (g_event) SetEvent(g_event);
    if (g_thread) { WaitForSingleObject(g_thread, 1000); CloseHandle(g_thread); }
    waveOutReset(g_dev);
    for (int i = 0; i < AUD_NBUF; i++)
        waveOutUnprepareHeader(g_dev, &g_hdr[i], sizeof(WAVEHDR));
    waveOutClose(g_dev);
    if (g_event) CloseHandle(g_event);
    g_ready = FALSE;
    /* The critical section deliberately outlives shutdown: a late
     * audio_gauge_set() must find a valid lock, not a destroyed one. */
}
