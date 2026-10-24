/* See pickup_cues.h. */
#include "pickup_cues.h"
#include <string.h>

#define FPS 60.0f

int pickup_interval_frames(audio_ping_family family, float distance)
{
    float d = distance < 0.0f ? 0.0f : distance > 2000.0f ? 2000.0f : distance;
    float s = 0.20f + d * 0.0005f;              /* 0.2 s .. 1.2 s */
    if (family == AUDIO_PING_PROP) s *= 1.5f;
    return (int)(s * FPS + 0.5f);
}

float pickup_gain(float distance)
{
    float d = distance < 0.0f ? 0.0f : distance;
    float g = 1.0f / (1.0f + d / 500.0f);       /* 1 near, 0.2 at 2000 */
    return g < 0.2f ? 0.2f : g;
}

void pickup_cues_reset(pickup_cues *c)
{
    memset(c->countdown, 0, sizeof c->countdown);
    memset(c->was_present, 0, sizeof c->was_present);
}

void pickup_cues_frame(pickup_cues *c, uint64_t epoch, const pickup_sample s[PICKUP_SLOTS])
{
    if (!c || !epoch) return;
    if (epoch != c->epoch) {
        pickup_cues_reset(c);
        c->epoch = epoch;
    }
    for (unsigned i = 0; i < PICKUP_SLOTS; i++) {
        const pickup_sample *p = &s[i];
        if (!p->present) { c->was_present[i] = false; continue; }
        /* A slot reused for a different kind of thing is a new pickup. */
        if (!c->was_present[i] || c->family[i] != (int)p->family || c->variant[i] != p->variant) {
            /* Staggered by slot, within 0.35 s: a round start brings a dozen
             * at once, and a dozen simultaneous pings are one blur. */
            c->countdown[i] = (int)(i % 8u) * 3;
            c->family[i] = (int)p->family;
            c->variant[i] = p->variant;
        }
        c->was_present[i] = true;
        if (c->countdown[i] > 0) { c->countdown[i]--; continue; }
        /* Due but not placeable this frame (off the camera): try next frame. */
        if (!p->placed) continue;
        audio_ping_owned(epoch, p->family, p->variant, p->pan, pickup_gain(p->distance));
        c->pings++;
        c->countdown[i] = pickup_interval_frames(p->family, p->distance);
    }
}
