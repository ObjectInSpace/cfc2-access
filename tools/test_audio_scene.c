/* Offline rendered-sample acceptance for the optional four-participant path. */
#include "../src/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures, checks;
static int16_t block[AUDIO_BLOCK * 2];
static ULONGLONG now = 1000;
ULONGLONG audio_scene_test_now(void) { return now; }
#define CHECK(c, m) do { checks++; if (!(c)) { failures++; printf("FAIL: %s\n", m); } } while (0)

static int peak(int channel, int count)
{
    int result = 0;
    while (count--) {
        audio_render_block_for_test(block);
        for (int i = channel; i < AUDIO_BLOCK * 2; i += 2) {
            int value = abs(block[i]);
            if (value > result) result = value;
        }
    }
    return result;
}

static int dominant(int channel)
{
    int peaks[2] = {0};
    audio_render_block_for_test(block);
    for (int i = 0; i < AUDIO_BLOCK * 2; i++) {
        int value = abs(block[i]);
        if (value > peaks[i % 2]) peaks[i % 2] = value;
    }
    return peaks[channel] > 3 * peaks[1 - channel];
}

int main(void)
{
    audio_scene s = {0};
    s.epoch = 1;
    s.sequence = 1;
    s.participants[2].present = TRUE;
    s.participants[2].incarnation = 1;
    s.participants[2].beacon_valid = TRUE;
    s.participants[2].screen_pan = -1;
    s.participants[2].stance = 1;
    CHECK(!audio_scene_begin(0), "zero epoch rejected");
    audio_tick(); /* Already queued legacy output must not cross ownership. */
    CHECK(audio_scene_begin(1), "begin");
    CHECK(peak(0, 2) == 0 && peak(1, 2) == 0, "ownership claim discards queued legacy tick");
    CHECK(audio_scene_publish(&s), "publish");
    CHECK(peak(0, 1) > 0, "first scene block sounds");
    CHECK(peak(0, 1) > 0, "scene persists without republishing every mixer block");
    CHECK(dominant(0), "P3 beacon follows supplied left pan");
    CHECK(!audio_scene_begin(1), "same epoch rejected");
    CHECK(!audio_scene_publish(&s), "duplicate sequence rejected");
    s.sequence = 0;
    CHECK(!audio_scene_publish(&s), "old sequence rejected");
    s.sequence = 2;
    s.epoch = 2;
    CHECK(!audio_scene_publish(&s), "wrong epoch rejected");
    s.epoch = 1;
    s.participants[2].screen_pan = NAN;
    CHECK(!audio_scene_publish(&s), "nonfinite position rejected");
    s.participants[2].screen_pan = -1;
    now += AUDIO_SCENE_TTL_MS - 1;
    CHECK(peak(0, 4) > 0, "scene remains valid before TTL");
    s.sequence = 1;
    CHECK(!audio_scene_publish(&s), "duplicate cannot renew TTL");
    now++;
    (void)peak(0, 32);
    CHECK(peak(0, 4) == 0 && peak(1, 4) == 0, "expired scene fades to silence at TTL");
    audio_beacon_set(0, -1, 1);
    audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 100, 100);
    audio_gauge_set(AUDIO_GAUGE_HEALTH, 0, 50, 100);
    audio_tick();
    CHECK(peak(0, 4) == 0 && peak(1, 4) == 0, "legacy producers cannot reclaim expired output");
    memset(s.participants, 0, sizeof s.participants);
    s.sequence = 2;
    audio_scene_participant *p = &s.participants[3];
    p->present = TRUE;
    p->incarnation = 10;
    audio_scene_gauge *g = &p->gauges[AUDIO_GAUGE_HEALTH];
    *g = (audio_scene_gauge){TRUE, 100, 100, 1, 1};
    CHECK(audio_scene_publish(&s), "P4 gauge accepted");
    CHECK(peak(1, 4) == 0, "new gauge seeds silently");
    g->value = 80;
    s.sequence++;
    CHECK(audio_scene_publish(&s), "gauge decrease accepted");
    (void)peak(1, 8);
    CHECK(dominant(1), "P4 gauge sounds at supplied HUD pan");
    audio_scene_stop(99);
    CHECK(peak(1, 1) > 0, "old stop cannot silence current owner");
    p->incarnation++;
    g->value = 50;
    s.sequence++;
    CHECK(audio_scene_publish(&s), "participant replacement accepted");
    (void)peak(1, 32);
    CHECK(peak(1, 4) == 0, "replacement seeds without false drain cue");
    g->generation++;
    g->value = 30;
    s.sequence++;
    CHECK(audio_scene_publish(&s), "resource replacement accepted");
    CHECK(peak(1, 4) == 0, "resource replacement seeds silently");
    g->maximum = 0;
    s.sequence++;
    CHECK(!audio_scene_publish(&s), "invalid gauge maximum rejected");
    g->maximum = 100;
    p->beacon_valid = TRUE;
    p->screen_pan = 1;
    p->stance = 1;
    CHECK(audio_scene_publish(&s), "rejected publication did not consume sequence");
    CHECK(peak(1, 4) > 0, "P4 beacon audible");
    p->present = FALSE;
    s.sequence++;
    CHECK(audio_scene_publish(&s), "absent participant accepted");
    (void)peak(1, 32);
    CHECK(peak(1, 4) == 0, "absent participant silenced");
    audio_scene_stop(1);
    s.sequence++;
    CHECK(!audio_scene_publish(&s), "stop rejects same-epoch publication");
    CHECK(!audio_scene_begin(1), "stop cannot restart the old epoch");
    CHECK(audio_scene_begin(2), "new epoch restarts");
    CHECK(!audio_scene_publish(&s), "old snapshot rejected after restart");
    CHECK(peak(0, 4) == 0 && peak(1, 4) == 0, "new epoch has no stale output");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
