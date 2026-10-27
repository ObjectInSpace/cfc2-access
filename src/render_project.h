/*
 * The shared NAOMI renderer's projection, for any game drawn through it.
 *
 * The renderer composes ctx+0x1F812C (view) and ctx+0x1F816C (projection),
 * both row-vector 4x4, with the viewport into ctx+0x1F8200 (0x140F33830);
 * ctx = *0x142C79B40. A point's horizontal screen position is clip.x /
 * clip.w, -1 left edge .. +1 right edge (the viewport at 0x141C00210 maps
 * that to 0..640 without a flip). Live: MJUS (mjus-match-fields.md) and
 * Plasma Sword (stg2-match-fields.md).
 *
 * Pure arithmetic on copied floats; usable on any thread.
 */
#ifndef CFC2_RENDER_PROJECT_H
#define CFC2_RENDER_PROJECT_H

#include <math.h>
#include <stdbool.h>

/* Horizontal screen position of `pt` through `cam` (16 floats view, then 16
 * projection), or false when the projection is not a perspective camera or
 * the point is behind it. */
static inline bool render_project_x(const float cam[32], const float pt[3], float *ndc_x)
{
    const float *v = cam, *pr = cam + 16;
    float e[4], c0, c3;
    /* Perspective: w comes from -z ([2][3] = -1), nothing else feeds w. */
    if (pr[11] != -1.0f || pr[3] != 0.0f || pr[7] != 0.0f || pr[15] != 0.0f || !(pr[0] > 0.0f))
        return false;
    for (int c = 0; c < 4; c++)
        e[c] = pt[0] * v[c] + pt[1] * v[4 + c] + pt[2] * v[8 + c] + v[12 + c];
    c0 = e[0] * pr[0] + e[1] * pr[4] + e[2] * pr[8] + e[3] * pr[12];
    c3 = e[0] * pr[3] + e[1] * pr[7] + e[2] * pr[11] + e[3] * pr[15];
    if (!isfinite(c0) || !isfinite(c3) || c3 <= 1e-3f) return false;
    *ndc_x = c0 / c3;
    return isfinite(*ndc_x);
}

/* Whether `pt` is drawn on screen. 1: on screen, `ndc_x` its horizontal
 * position (-1 left edge .. +1 right edge); 0: off screen -- behind the
 * camera or past any edge; -1: not a perspective camera or non-finite input,
 * so it cannot be told. The Power Stone games sound nothing off screen
 * (user, 2026-10-27). */
static inline int render_project_screen(const float cam[32], const float pt[3], float *ndc_x)
{
    const float *v = cam, *pr = cam + 16;
    float e[4], c0, c1, c3;
    if (pr[11] != -1.0f || pr[3] != 0.0f || pr[7] != 0.0f || pr[15] != 0.0f || !(pr[0] > 0.0f))
        return -1;
    if (!isfinite(pt[0]) || !isfinite(pt[1]) || !isfinite(pt[2])) return -1;
    for (int c = 0; c < 4; c++)
        e[c] = pt[0] * v[c] + pt[1] * v[4 + c] + pt[2] * v[8 + c] + v[12 + c];
    c0 = e[0] * pr[0] + e[1] * pr[4] + e[2] * pr[8] + e[3] * pr[12];
    c1 = e[0] * pr[1] + e[1] * pr[5] + e[2] * pr[9] + e[3] * pr[13];
    c3 = e[0] * pr[3] + e[1] * pr[7] + e[2] * pr[11] + e[3] * pr[15];
    if (!isfinite(c0) || !isfinite(c1) || !isfinite(c3)) return -1;
    if (c3 <= 1e-3f || fabsf(c0) > c3 || fabsf(c1) > c3) return 0;
    *ndc_x = c0 / c3;
    return 1;
}

/* For a point off screen when off-screen sounds are switched on: past an edge
 * in front of the camera, that edge; beside or behind it, the side the view's
 * own right axis puts it on. false only when the camera or point is unusable. */
static inline bool render_project_side(const float cam[32], const float pt[3], float *ndc_x)
{
    const float *v = cam, *pr = cam + 16;
    float e0, e1, e2, e3, c0, c3;
    if (pr[11] != -1.0f || pr[3] != 0.0f || pr[7] != 0.0f || pr[15] != 0.0f || !(pr[0] > 0.0f))
        return false;
    if (!isfinite(pt[0]) || !isfinite(pt[1]) || !isfinite(pt[2])) return false;
    e0 = pt[0] * v[0] + pt[1] * v[4] + pt[2] * v[8] + v[12];
    e1 = pt[0] * v[1] + pt[1] * v[5] + pt[2] * v[9] + v[13];
    e2 = pt[0] * v[2] + pt[1] * v[6] + pt[2] * v[10] + v[14];
    e3 = pt[0] * v[3] + pt[1] * v[7] + pt[2] * v[11] + v[15];
    c0 = e0 * pr[0] + e1 * pr[4] + e2 * pr[8] + e3 * pr[12];
    c3 = e0 * pr[3] + e1 * pr[7] + e2 * pr[11] + e3 * pr[15];
    if (!isfinite(c0) || !isfinite(c3) || !isfinite(e0)) return false;
    if (c3 > 1e-3f) *ndc_x = c0 / c3 < -1.0f ? -1.0f : c0 / c3 > 1.0f ? 1.0f : c0 / c3;
    else *ndc_x = e0 >= 0.0f ? 1.0f : -1.0f;
    return true;
}

#endif
