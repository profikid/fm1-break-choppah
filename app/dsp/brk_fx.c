/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's effects (brk_fx.h). */
#include "brk_fx.h"
#include "brk.h"
#include "brk_math.h"

#define FL_LEN 1024u                     /* the flanger's line: 23 ms */

typedef struct { float ic1, ic2; } svf_t;  /* a TPT state-variable filter (Simper) */
typedef struct { float a1, a2, a3, k; } svf_c;

static volatile brk_fx_t p_fx;
static brk_fx_t fx;                      /* the render's copy */
static float ring_ph, fl_ph, deg_ph, deg_hold, lim_gain = 1.0f;
static float hpf_hz_s = 20.0f, dj_s;     /* smoothed controls */
static svf_t hpf_l, hpf_r, dj_l, dj_r;
static float fl_buf[2][FL_LEN];
static uint32_t fl_w;

static svf_c svf_coef(float hz, float res)
{
    svf_c c;
    float g = brk_tan_pi(hz / (float)BRK_SR);
    c.k = 2.0f - 2.0f * brk_clampf(res, 0.0f, 0.95f);
    c.a1 = 1.0f / (1.0f + g * (g + c.k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

/* one sample; *lp gets the low-pass, the return is the high-pass */
static inline float svf_run(svf_t *s, const svf_c *c, float x, float *lp)
{
    float v3 = x - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    *lp = v2;
    return x - c->k * v1 - v2;
}

static inline float fl_read(const float *buf, float d)   /* d samples back, linear */
{
    float r = (float)fl_w - d;
    int32_t i;
    float t;
    while (r < 0.0f)
        r += (float)FL_LEN;
    i = (int32_t)r;
    t = r - (float)i;
    return buf[(uint32_t)i % FL_LEN] * (1.0f - t) + buf[(uint32_t)(i + 1) % FL_LEN] * t;
}

static inline float soft(float x) { return x / (1.0f + brk_absf(x)); }
/* clean below 0.8, then a soft knee that never passes 1 */
static inline float knee(float x)
{
    float a = brk_absf(x);
    if (a <= 0.8f)
        return x;
    a = 0.8f + 0.2f * soft((a - 0.8f) * 5.0f);
    return x < 0.0f ? -a : a;
}

void brk_fx_defaults(brk_fx_t *p)
{
    uint32_t i;
    for (i = 0; i < BRK_NFX; i++)
        p->on[i] = 0u;
    p->ring_hz = 220.0f;
    p->ring_mix = 0.5f;
    p->hpf_hz = 200.0f;
    p->hpf_res = 0.3f;
    p->hpf_spread = 0.2f;
    p->fl_rate = 0.25f;
    p->fl_depth = 0.6f;
    p->fl_fb = 0.5f;
    p->fl_mix = 0.5f;
    p->deg_bits = 8.0f;
    p->deg_rate = 11025.0f;
    p->deg_drive = 0.2f;
    p->dj = 0.0f;
}

void brk_fx_init(void)
{
    brk_fx_t d;
    brk_fx_defaults(&d);
    brk_fx_set(&d);
}

void brk_fx_set(const brk_fx_t *p)
{
    *(brk_fx_t *)&p_fx = *p;             /* written whole from the main loop; the render copies it per block */
}

void brk_fx_process(const float *in, float *l, float *r, uint32_t n, uint32_t pads)
{
    uint32_t i;
    svf_c hc_l, hc_r, djc;
    float dj, dj_hz, ring_inc, fl_inc, deg_inc, q;
    int dj_hp;
    fx = *(const brk_fx_t *)&p_fx;
    /* the controls glide once a block: no zipper noise (PRD A-41) */
    hpf_hz_s += (brk_clampf(fx.hpf_hz, 20.0f, 8000.0f) - hpf_hz_s) * 0.3f;
    hc_l = svf_coef(hpf_hz_s, fx.hpf_res);
    hc_r = svf_coef(brk_clampf(hpf_hz_s * (1.0f + 2.0f * fx.hpf_spread), 20.0f, 16000.0f), fx.hpf_res);
    dj = (pads >> PAD_FILTER) & 1u ? -0.92f : brk_clampf(fx.dj, -1.0f, 1.0f);
    dj_s += (dj - dj_s) * ((pads >> PAD_FILTER) & 1u ? 0.08f : 0.3f);
    dj_hp = dj_s > 0.0f;
    /* -1 .. 0: low-pass 18 kHz down to 80 Hz; 0 .. 1: high-pass 20 Hz up to 6 kHz (exponential sweeps) */
    dj_hz = dj_hp ? 20.0f * brk_exp2f(dj_s * 8.2f) : 18000.0f * brk_exp2f(dj_s * 7.8f);
    djc = svf_coef(dj_hz, 0.35f);
    ring_inc = brk_clampf(fx.ring_hz, 20.0f, 2000.0f) / (float)BRK_SR;
    fl_inc = brk_clampf(fx.fl_rate, 0.02f, 10.0f) / (float)BRK_SR;
    deg_inc = brk_clampf(fx.deg_rate, 500.0f, (float)BRK_SR) / (float)BRK_SR;
    q = brk_exp2f(brk_clampf(fx.deg_bits, 2.0f, 16.0f) - 1.0f);
    for (i = 0; i < n; i++) {
        float x = in[i], xl, xr, lp;
        if (fx.on[FX_RING]) {
            x = x * (1.0f - fx.ring_mix) + x * brk_sin_turns(ring_ph) * fx.ring_mix;
            ring_ph += ring_inc;
            if (ring_ph >= 1.0f)
                ring_ph -= 1.0f;
        }
        xl = xr = x;
        if (fx.on[FX_HPF]) {
            xl = svf_run(&hpf_l, &hc_l, xl, &lp);
            xr = svf_run(&hpf_r, &hc_r, xr, &lp);
        }
        if (fx.on[FX_FLANGER]) {
            /* 0.2 .. 5 ms, the right side a quarter turn behind: the flanger widens the mono break */
            float span = (0.2f + 4.8f * brk_clampf(fx.fl_depth, 0.0f, 1.0f)) * 44.1f;
            float dl = 9.0f + span * (0.5f + 0.5f * brk_sin_turns(fl_ph));
            float dr = 9.0f + span * (0.5f + 0.5f * brk_sin_turns(fl_ph + 0.25f));
            float yl = fl_read(fl_buf[0], dl), yr = fl_read(fl_buf[1], dr);
            float fb = brk_clampf(fx.fl_fb, -0.9f, 0.9f);
            fl_buf[0][fl_w] = xl + yl * fb;
            fl_buf[1][fl_w] = xr + yr * fb;
            fl_w = (fl_w + 1u) % FL_LEN;
            xl = xl * (1.0f - 0.5f * fx.fl_mix) + yl * 0.5f * fx.fl_mix;
            xr = xr * (1.0f - 0.5f * fx.fl_mix) + yr * 0.5f * fx.fl_mix;
            fl_ph += fl_inc;
            if (fl_ph >= 1.0f)
                fl_ph -= 1.0f;
        }
        if (fx.on[FX_DEGRADE]) {
            float m = 0.5f * (xl + xr), d = 1.0f + 8.0f * fx.deg_drive;
            deg_ph += deg_inc;
            if (deg_ph >= 1.0f) {        /* sample and hold at the reduced rate, then fewer bits */
                deg_ph -= 1.0f;
                deg_hold = (float)(int32_t)(soft(m * d) * q) / q;
            }
            xl = xr = deg_hold;
        }
        if (dj_s < -0.01f || dj_s > 0.01f) {   /* the master DJ filter */
            float hl = svf_run(&dj_l, &djc, xl, &lp);
            xl = dj_hp ? hl : lp;
            hl = svf_run(&dj_r, &djc, xr, &lp);
            xr = dj_hp ? hl : lp;
        }
        {   /* the limiter: a fast attack, a slow release, then a soft clip for what gets past */
            float pk = brk_absf(xl) > brk_absf(xr) ? brk_absf(xl) : brk_absf(xr);
            float want = pk * lim_gain > 0.95f ? 0.95f / pk : 1.0f;
            lim_gain += (want - lim_gain) * (want < lim_gain ? 0.5f : 0.0002f);
            l[i] = knee(xl * lim_gain);
            r[i] = knee(xr * lim_gain);
        }
    }
}
