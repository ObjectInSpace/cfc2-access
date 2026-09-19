/* See audio.h for the design this implements. */

#include "audio.h"
#include "log.h"

#include <mmsystem.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define AUD_NBUF 4                     /* ~23 ms each: 92 ms worst case */
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

/* ---- voices -------------------------------------------------------------
 * One voice per (gauge, player). Everything the mixer needs lives here, so the
 * mix loop is uniform -- no named globals, no per-cue special cases. That is
 * the whole reason this was written fresh rather than adapted: the reference
 * implementation hardcoded one beacon, one hiss and one earcon slot, and
 * panned only the beacon. */
typedef struct {
    float phase;        /* oscillator phase, 0..1                          */
    float freq;         /* target frequency, Hz                            */
    float sm_freq;      /* smoothed frequency (pitch glides, never jumps)  */
    float gain;         /* target gain 0..1                                */
    float sm_gain;      /* smoothed gain -- silence is gain 0, not a flag  */
    float pan;          /* -1 left .. +1 right                             */
    float sm_gl, sm_gr; /* smoothed per-voice channel gains                */
    int   timbre;       /* which waveform generator                        */
    BOOL  active;
} voice;

#define VOICE_COUNT (AUDIO_GAUGE_COUNT * 2)
static voice g_voice[VOICE_COUNT];

/* Per-gauge tone character. Timbre identifies the gauge, so these must stay
 * audibly distinct; the base frequencies are deliberately spread apart. */
typedef struct { float f_lo, f_hi; int timbre; } gauge_tone;
static const gauge_tone g_tone[AUDIO_GAUGE_COUNT] = {
    /* SUPER  */ { 330.0f, 990.0f,  0 },  /* sine: clean, mid       */
    /* HEALTH */ { 180.0f, 540.0f,  1 },  /* triangle: softer, low  */
    /* STUN   */ { 520.0f, 1560.0f, 2 },  /* square-ish: edgy, high */
};

/* ---- gauge tracking -----------------------------------------------------
 * The change rule: a tone sounds only while the value is MOVING. We hold the
 * last value and a decay timer. Any change re-arms the timer; when it expires
 * the voice fades to silence.
 *
 * The timer counts BLOCKS, not samples: audio_gauge_set() is called from the
 * worker (20 ms) while the mixer runs per block (23 ms), and tying the lifetime
 * to the render side keeps it independent of how often the caller polls. */
#define HOLD_BLOCKS 6                  /* ~140 ms of tone after a change */
typedef struct {
    int   last;
    BOOL  seeded;
    int   hold;
    float level;                       /* 0..1, the gauge fill fraction */
} gauge_state;
static gauge_state g_gauge[AUDIO_GAUGE_COUNT][2];

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
static INIT_ONCE g_lock_once = INIT_ONCE_STATIC_INIT;

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

/* ---- generators --------------------------------------------------------- */
static float osc(int timbre, float phase)
{
    switch (timbre) {
    case 0: return sinf(phase * 2.0f * (float)M_PI);
    case 1: {   /* triangle */
        float t = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
        return t * 2.0f - 1.0f;
    }
    default: {  /* softened square: odd harmonics without the full buzz */
        float s  = sinf(phase * 2.0f * (float)M_PI);
        float s3 = sinf(phase * 6.0f * (float)M_PI) / 3.0f;
        return (s + s3) * 0.75f;
    }
    }
}

/* Equal-power pan: -1..+1 into per-channel gains of constant total power. */
static void pan_gains(float pan, float *gl, float *gr)
{
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    float ang = (pan + 1.0f) * ((float)M_PI / 4.0f);
    *gl = cosf(ang);
    *gr = sinf(ang);
}

/* ---- public API --------------------------------------------------------- */
void audio_gauge_set(audio_gauge g, unsigned player, int value, int maxv)
{
    if ((int)g < 0 || g >= AUDIO_GAUGE_COUNT || player > 1 || maxv <= 0)
        return;
    lock_ensure();

    EnterCriticalSection(&g_lock);
    gauge_state *st = &g_gauge[g][player];
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
    g_gauge[g][player].seeded = FALSE;
    g_gauge[g][player].hold = 0;
    LeaveCriticalSection(&g_lock);
}

void audio_gauge_reset_all(void)
{
    for (int g = 0; g < AUDIO_GAUGE_COUNT; g++)
        for (unsigned p = 0; p < 2; p++)
            audio_gauge_reset((audio_gauge)g, p);
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
    for (int g = 0; g < AUDIO_GAUGE_COUNT; g++) {
        for (unsigned p = 0; p < 2; p++) {
            gauge_state *st = &g_gauge[g][p];
            voice *v = &g_voice[g * 2 + p];
            const gauge_tone *t = &g_tone[g];

            v->timbre = t->timbre;
            /* Pan is fixed by OWNER, matching the HUD: P1 left, P2 right. */
            v->pan = (p == 0) ? -0.85f : 0.85f;
            /* Pitch rises with fill. */
            v->freq = t->f_lo + (t->f_hi - t->f_lo) * st->level;

            if (st->hold > 0) {
                st->hold--;
                v->gain = 0.35f;
                v->active = TRUE;
            } else {
                v->gain = 0.0f;   /* fades out; retired once truly silent */
            }
        }
    }
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
            v->sm_gain += 0.004f * (gt - v->sm_gain);
            if (v->sm_freq <= 0.0f) v->sm_freq = v->freq;
            v->sm_freq += 0.010f * (v->freq - v->sm_freq);

            float gl, gr;
            pan_gains(v->pan, &gl, &gr);
            v->sm_gl += 0.010f * (gl - v->sm_gl);
            v->sm_gr += 0.010f * (gr - v->sm_gr);

            v->phase += v->sm_freq / (float)AUDIO_SR;
            if (v->phase >= 1.0f) v->phase -= 1.0f;

            float s = osc(v->timbre, v->phase) * v->sm_gain * g_master;
            l += s * v->sm_gl;
            r += s * v->sm_gr;

            /* Retire a voice only once it is genuinely inaudible, so a
             * fade-out is never cut off mid-ramp. */
            if (v->gain == 0.0f && v->sm_gain < 0.0005f) {
                v->active = FALSE;
                v->sm_gain = 0.0f;
            }
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
