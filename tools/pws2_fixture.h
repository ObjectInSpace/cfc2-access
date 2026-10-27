/* Loader for the Power Stone 2 replay fixtures (tools/make_pws2_fixtures.py):
 *   tools/fixtures/pws2-frames-20261027.bin  "PW2F"  the Arcade fight
 *   tools/fixtures/pws2-stage-20261027.bin   "PW2S"  the Versus stage select
 * every sample of a live capture where a decoder input changes.
 * pws2_fixture_apply() writes one record into a full-size state block; the
 * pool records are cleared first, so objects that left are gone. Test-only;
 * include from exactly one translation unit. */
#ifndef CFC2_PWS2_FIXTURE_H
#define CFC2_PWS2_FIXTURE_H
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PWS2_FX_SIZE 0x8CD40u
#define PWS2_FX_A(p) (0x40818u + (p) * 0x42B8u)
#define PWS2_FX_POOL 0x4B10u
#define PWS2_FX_STRIDE 0x4C8u
/* Same lists as RANGES, STAGE_RANGES, POOL_FIELDS and STAGE_FIELDS in
 * tools/make_pws2_fixtures.py. */
static const unsigned pws2_fx_actor[][2] = {
    { 1, 2 }, { 0x3C, 12 }, { 0x1DC, 4 }, { 0x401C, 1 }, { 0x4028, 1 }, { 0x4055, 1 }, { 0x4080, 8 }, { 0x4258, 4 },
};
static const unsigned pws2_fx_root[][2] = {
    { 0x529D2, 1 }, { 0x529D2 + 0x98, 1 }, { 0x529D2 + 2 * 0x98, 1 }, { 0x529D2 + 3 * 0x98, 1 },
    { 0x52BD8, 8 }, { 0x52D2B, 1 }, { 0x52D2E, 1 }, { 0x52D34, 4 }, { 0x52E88, 1 },
    { 0x52EE0, 2 }, { 0x52EE5, 1 }, { 0x52FDC, 1 }, { 0x54B49, 1 }, { 0x51D90, 0x40 },
    /* PW2S only: */ { 0x52BE8, 2 }, { 0x52BEA, 1 }, { 0x53010, 1 },
};
#define PWS2_FX_ROOT_F 14u          /* the PW2F list is the first 14 */
static const unsigned pws2_fx_pool_f[][2] = { { 0, 1 }, { 0x3C, 12 }, { 0xB8, 8 }, { 0x4B0, 2 } };
static const unsigned pws2_fx_pool_s[][2] = { { 0, 1 }, { 2, 1 }, { 8, 8 }, { 0x3C, 12 }, { 0xB8, 8 }, { 0x4B0, 2 } };
#define PWS2_FX_N(a) (sizeof a / sizeof a[0])

typedef struct {
    unsigned char *data;
    unsigned count, nroot, npool;
    const unsigned (*pool)[2];
    size_t *at;                     /* record i starts here */
} pws2_fx;

static pws2_fx pws2_fx_main;
#define pws2_fx_count (pws2_fx_main.count)

static void pws2_fx_open(pws2_fx *x, const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t len, pos = 8;
    unsigned head = 16, pool = 1;
    assert(f);
    fseek(f, 0, SEEK_END);
    len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    x->data = malloc(len);
    assert(x->data && fread(x->data, 1, len, f) == len);
    fclose(f);
    if (!memcmp(x->data, "PW2F", 4)) {
        x->nroot = PWS2_FX_ROOT_F; x->pool = pws2_fx_pool_f; x->npool = PWS2_FX_N(pws2_fx_pool_f);
    } else {
        assert(!memcmp(x->data, "PW2S", 4));
        x->nroot = PWS2_FX_N(pws2_fx_root); x->pool = pws2_fx_pool_s; x->npool = PWS2_FX_N(pws2_fx_pool_s);
    }
    memcpy(&x->count, x->data + 4, 4);
    x->at = malloc(sizeof *x->at * x->count);
    assert(x->at);
    for (unsigned k = 0; k < PWS2_FX_N(pws2_fx_actor); k++) head += 4 * pws2_fx_actor[k][1];
    for (unsigned k = 0; k < x->nroot; k++) head += pws2_fx_root[k][1];
    for (unsigned k = 0; k < x->npool; k++) pool += x->pool[k][1];
    for (unsigned i = 0; i < x->count; i++) {
        x->at[i] = pos;
        assert(pos + head + 1 <= len);
        pos += head + 1 + (size_t)x->data[pos + head] * pool;
    }
    assert(pos == len);
}

static inline double pws2_fx_time(const pws2_fx *x, unsigned i)
{ double t; memcpy(&t, x->data + x->at[i], 8); return t; }
static inline unsigned long long pws2_fx_root_of(const pws2_fx *x, unsigned i)
{ unsigned long long r; memcpy(&r, x->data + x->at[i] + 8, 8); return r; }

static void pws2_fx_apply(const pws2_fx *x, unsigned i, unsigned char *s)
{
    const unsigned char *r = x->data + x->at[i] + 16;
    unsigned used;
    for (unsigned p = 0; p < 4; p++)
        for (unsigned k = 0; k < PWS2_FX_N(pws2_fx_actor); k++) {
            memcpy(s + PWS2_FX_A(p) + pws2_fx_actor[k][0], r, pws2_fx_actor[k][1]);
            r += pws2_fx_actor[k][1];
        }
    for (unsigned k = 0; k < x->nroot; k++) {
        memcpy(s + pws2_fx_root[k][0], r, pws2_fx_root[k][1]);
        r += pws2_fx_root[k][1];
    }
    for (unsigned o = 0; o < 200; o++) memset(s + PWS2_FX_POOL + o * PWS2_FX_STRIDE, 0, PWS2_FX_STRIDE);
    used = *r++;
    for (unsigned u = 0; u < used; u++) {
        unsigned idx = *r++;
        assert(idx < 200);
        for (unsigned k = 0; k < x->npool; k++) {
            memcpy(s + PWS2_FX_POOL + idx * PWS2_FX_STRIDE + x->pool[k][0], r, x->pool[k][1]);
            r += x->pool[k][1];
        }
    }
}

/* The Arcade fight, as the original single-file interface. */
static void pws2_fixture_load(void) { pws2_fx_open(&pws2_fx_main, "tools/fixtures/pws2-frames-20261027.bin"); }
static inline double pws2_fixture_time(unsigned i) { return pws2_fx_time(&pws2_fx_main, i); }
static inline unsigned long long pws2_fixture_root(unsigned i) { return pws2_fx_root_of(&pws2_fx_main, i); }
static inline void pws2_fixture_apply(unsigned i, unsigned char *s) { pws2_fx_apply(&pws2_fx_main, i, s); }
#endif
