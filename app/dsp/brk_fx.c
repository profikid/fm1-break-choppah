/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's effects (brk_fx.h). */
#include "brk_fx.h"
#include "brk.h"
#include "brk_math.h"

#define FL_LEN 512u                      /* a flanger's line: 11.6 ms (it reaches 5.2 ms back) */

typedef struct { float ic1, ic2; } svf_t;  /* a TPT state-variable filter (Simper) */
typedef struct { float a1, a2, a3, k; } svf_c;

static volatile brk_fx_t p_fx;
static brk_fx_t fx;                      /* the render's copy */
static float lim_gain = 1.0f, dj_s;      /* the limiter; the DJ filter's smoothed position */
static svf_t dj_l, dj_r;
const char *const BRK_FXT_NAME[BRK_NFXT] = {"EMPTY", "FILTER", "CRUNCH", "DRIVE", "PUNCH", "FLANGER", "RING MOD",
                                            "TREMOLO"};
const char *const BRK_TREM_DIV_NAME[BRK_TREM_DIVS] = {"1/2", "1/4", "1/8", "1/16", "1/32", "1/8T"};
static const float TREM_STEPS[BRK_TREM_DIVS] = {8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 4.0f / 3.0f};   /* 16ths a cycle */

/* a slot's own state: each slot has its own, so two of an effect never share (cleared when its effect changes) */
typedef struct {
    uint8_t type;                        /* the effect this state is for */
    svf_t f[2];                          /* FILTER */
    float hz_s;                          /* FILTER: the cutoff, smoothed */
    float ph, ph2;                       /* RING, FLANGER, TREMOLO: phases */
    float hold[2], hph;                  /* CRUNCH: held samples, the rate's phase */
    float lp[2];                         /* DRIVE: its tone */
    float env, gr;                       /* PUNCH: the level followed, the gain */
    float fl[2][FL_LEN];                 /* FLANGER */
    uint32_t w;
} slot_st_t;
static slot_st_t st[BRK_FX_SLOTS];
/* the dub delay: a 16-bit line the app hands over, its tone filter, the send's level */
const char *const BRK_DUB_DIV_NAME[BRK_DUB_DIVS] = {"1/8", "3/16", "1/4", "3/8"};
static const float DUB_STEPS[BRK_DUB_DIVS] = {2.0f, 3.0f, 4.0f, 6.0f};
static int16_t *dub_buf;
static uint32_t dub_n, dub_w;
static float dub_lp, dub_send, dub_time_s;
static volatile float p_bpm = 170.0f;

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

static inline float fl_read(const float *buf, uint32_t w, float d)   /* d samples back from w, linear */
{
    float r = (float)w - d;
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

void brk_fx_slot_defaults(brk_fxslot_t *s, int type)
{
    /* each effect's starting point: p[0..2], mix */
    static const float D[BRK_NFXT][4] = {
        {0.0f, 0.0f, 0.0f, 1.0f},        /* EMPTY */
        {0.55f, 0.3f, 0.0f, 1.0f},       /* FILTER: low-pass 1 kHz, a little resonance */
        {0.57f, 0.4f, 0.2f, 1.0f},       /* CRUNCH: 8 bits, 11 kHz */
        {0.35f, 0.6f, 0.7f, 1.0f},       /* DRIVE */
        {0.5f, 0.3f, 0.3f, 1.0f},        /* PUNCH */
        {0.4f, 0.6f, 0.75f, 0.5f},       /* FLANGER: 0.25 Hz, feedback +0.45 */
        {0.35f, 0.0f, 0.0f, 0.5f},       /* RING MOD: 100 Hz */
        {0.6f, 0.7f, 0.0f, 1.0f},        /* TREMOLO: 1/16 */
    };
    uint32_t t = (uint32_t)type < BRK_NFXT ? (uint32_t)type : 0u;
    s->type = (uint8_t)t;
    s->on = 0u;
    s->rsv[0] = s->rsv[1] = 0u;
    s->p[0] = D[t][0];
    s->p[1] = D[t][1];
    s->p[2] = D[t][2];
    s->mix = D[t][3];
}

void brk_fx_defaults(brk_fx_t *p)
{
    static const uint8_t CHAIN[BRK_FX_SLOTS] = {FXT_CRUNCH, FXT_FILTER, FXT_FLANGER, FXT_RING};   /* all bypassed */
    uint32_t i;
    for (i = 0; i < BRK_FX_SLOTS; i++)
        brk_fx_slot_defaults(&p->slot[i], CHAIN[i]);
    p->wet = 1.0f;
    p->dj = 0.0f;
    p->flt_target = -0.92f;
    p->flt_res = 0.35f;
    p->flt_speed = 0.5f;
    p->dub_div = 1u;                     /* 3/16: the dub classic */
    p->rsv[0] = p->rsv[1] = p->rsv[2] = 0u;
    p->dub_fb = 0.6f;
    p->dub_tone = 0.5f;
    p->dub_mix = 0.6f;
}

void brk_fx_delay_buffer(int16_t *buf, uint32_t n)
{
    uint32_t i;
    dub_n = 0u;                          /* (the render skips the delay until the line is clear) */
    for (i = 0; buf && i < n; i++)
        buf[i] = 0;
    dub_buf = buf;
    dub_w = 0u;
    dub_n = buf ? n : 0u;
}
void brk_fx_tempo(float bpm) { p_bpm = brk_clampf(bpm, 40.0f, 300.0f); }
float brk_fx_dj_now(void) { return dj_s; }

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

/* a slot's settings for this block, mapped from 0..1 */
typedef struct {
    uint8_t on, type;
    float mix;
    svf_c c;                             /* FILTER */
    uint8_t mode;                        /* FILTER: 0 low-pass, 1 band-pass, 2 high-pass */
    float a, b, g;                       /* the effect's own: see slot_block */
} slot_c_t;
static slot_c_t sc[BRK_FX_SLOTS];

static void slot_block(uint32_t k)
{
    const brk_fxslot_t *f = &fx.slot[k];
    slot_st_t *s = &st[k];
    slot_c_t *c = &sc[k];
    float p0 = brk_clampf(f->p[0], 0.0f, 1.0f), p1 = brk_clampf(f->p[1], 0.0f, 1.0f), p2 = brk_clampf(f->p[2], 0.0f, 1.0f);
    uint32_t t = f->type < BRK_NFXT ? f->type : FXT_EMPTY;
    if (s->type != t) {                  /* a new effect here: its state from nothing */
        uint8_t *z = (uint8_t *)s;
        uint32_t i;
        for (i = 0; i < sizeof *s; i++)
            z[i] = 0u;
        s->type = (uint8_t)t;
        s->hz_s = 30.0f * brk_exp2f(p0 * 9.06f);
        s->gr = 1.0f;
    }
    c->type = (uint8_t)t;
    c->on = (uint8_t)(f->on && t != FXT_EMPTY);
    c->mix = brk_clampf(f->mix, 0.0f, 1.0f);
    switch (t) {
    case FXT_FILTER:                     /* the cutoff glides (no zipper noise, PRD A-41) */
        s->hz_s += (30.0f * brk_exp2f(p0 * 9.06f) - s->hz_s) * 0.3f;
        c->c = svf_coef(brk_clampf(s->hz_s, 20.0f, 18000.0f), p1 * 0.95f);
        c->mode = (uint8_t)(p2 < 0.33f ? 0u : p2 < 0.67f ? 1u : 2u);
        break;
    case FXT_CRUNCH:                     /* a: the levels (2^(bits-1)), b: the rate's step, g: the drive */
        c->a = brk_exp2f(16.0f - 14.0f * p0 - 1.0f);
        c->b = brk_exp2f(-p1 * 5.46f);
        c->g = 1.0f + 8.0f * p2;
        break;
    case FXT_DRIVE:                      /* g: the gain, b: the tone's coefficient, a: the level after */
        c->g = 1.0f + 29.0f * p0 * p0;
        c->b = brk_clampf(brk_tan_pi((800.0f * brk_exp2f(p1 * 4.3f)) / (float)BRK_SR) * 2.0f, 0.02f, 1.0f);
        c->a = (0.25f + 0.75f * p2) * (1.0f + 0.3f * p0);
        break;
    case FXT_PUNCH:                      /* a: the threshold, b: attack, g: release (per-sample coefficients) */
        c->a = brk_exp2f(-p0 * 5.0f);    /* 1 .. 1/32: the more, the lower */
        c->b = 1.0f / (1.0f + (0.1f + 29.9f * p1 * p1) * 44.1f);
        c->g = 1.0f / (1.0f + (20.0f + 480.0f * p2) * 44.1f);
        break;
    case FXT_FLANGER:                    /* a: the rate's step, b: the depth's span (samples), g: feedback */
        c->a = 0.02f * brk_exp2f(p0 * 9.0f) / (float)BRK_SR;
        c->b = (0.2f + 4.8f * p1) * 44.1f;
        c->g = (2.0f * p2 - 1.0f) * 0.9f;
        break;
    case FXT_RING:                       /* a: left's step, b: right's */
        c->a = 20.0f * brk_exp2f(p0 * 6.64f) / (float)BRK_SR;
        c->b = c->a * (1.0f + p1);
        break;
    case FXT_TREMOLO: {                  /* a: the cycle's step (on the tempo), b: depth, g: pan */
        uint32_t d = (uint32_t)(p0 * (float)(BRK_TREM_DIVS - 1u) + 0.5f);
        c->a = 1.0f / (TREM_STEPS[d < BRK_TREM_DIVS ? d : 0u] * (float)BRK_SR * 60.0f / (p_bpm * 4.0f));
        c->b = p1;
        c->g = p2;
        break;
    }
    default:
        break;
    }
}

static inline void slot_run(uint32_t k, float *xl, float *xr)
{
    slot_st_t *s = &st[k];
    const slot_c_t *c = &sc[k];
    float yl = *xl, yr = *xr, lp;
    switch (c->type) {
    case FXT_FILTER: {
        float hl = svf_run(&s->f[0], &c->c, yl, &lp), lpl = lp;
        float hr = svf_run(&s->f[1], &c->c, yr, &lp), lpr = lp;
        if (c->mode == 0u) {
            yl = lpl;
            yr = lpr;
        } else if (c->mode == 2u) {
            yl = hl;
            yr = hr;
        } else {                         /* band-pass: what is neither */
            yl = *xl - hl - lpl;
            yr = *xr - hr - lpr;
        }
        break;
    }
    case FXT_CRUNCH:                     /* sample and hold at the lower rate, then fewer bits */
        s->hph += c->b;
        if (s->hph >= 1.0f) {
            s->hph -= 1.0f;
            s->hold[0] = (float)(int32_t)(soft(yl * c->g) * c->a) / c->a;
            s->hold[1] = (float)(int32_t)(soft(yr * c->g) * c->a) / c->a;
        }
        yl = s->hold[0];
        yr = s->hold[1];
        break;
    case FXT_DRIVE:                      /* gain into a soft clip, a tone filter, the level */
        s->lp[0] += (soft(yl * c->g) - s->lp[0]) * c->b;
        s->lp[1] += (soft(yr * c->g) - s->lp[1]) * c->b;
        yl = s->lp[0] * c->a;
        yr = s->lp[1] * c->a;
        break;
    case FXT_PUNCH: {                    /* a peak compressor (4:1 above the threshold), made up to the threshold */
        float pk = brk_absf(yl) > brk_absf(yr) ? brk_absf(yl) : brk_absf(yr), want;
        s->env += (pk - s->env) * (pk > s->env ? c->b : c->g);
        want = s->env > c->a ? (c->a + (s->env - c->a) * 0.25f) / s->env : 1.0f;
        s->gr += (want - s->gr) * 0.02f;
        yl *= s->gr / (c->a + (1.0f - c->a) * 0.25f);
        yr *= s->gr / (c->a + (1.0f - c->a) * 0.25f);
        break;
    }
    case FXT_FLANGER: {                  /* 9 samples + the depth's sweep, the right a quarter turn behind */
        float dl = 9.0f + c->b * (0.5f + 0.5f * brk_sin_turns(s->ph));
        float dr = 9.0f + c->b * (0.5f + 0.5f * brk_sin_turns(s->ph + 0.25f));
        float rl = fl_read(s->fl[0], s->w, dl), rr = fl_read(s->fl[1], s->w, dr);
        s->fl[0][s->w] = yl + rl * c->g;
        s->fl[1][s->w] = yr + rr * c->g;
        s->w = (s->w + 1u) % FL_LEN;
        yl = 0.5f * (yl + rl);
        yr = 0.5f * (yr + rr);
        s->ph += c->a;
        if (s->ph >= 1.0f)
            s->ph -= 1.0f;
        break;
    }
    case FXT_RING:
        yl *= brk_sin_turns(s->ph);
        yr *= brk_sin_turns(s->ph2);
        s->ph += c->a;
        s->ph2 += c->b;
        if (s->ph >= 1.0f)
            s->ph -= 1.0f;
        if (s->ph2 >= 1.0f)
            s->ph2 -= 1.0f;
        break;
    case FXT_TREMOLO: {                  /* a sine on the tempo: both sides together, or across (pan) */
        float m = 0.5f + 0.5f * brk_sin_turns(s->ph), gl, gr;
        gl = 1.0f - c->b * m;
        gr = 1.0f - c->b * (c->g * (1.0f - m) + (1.0f - c->g) * m);
        yl *= gl;
        yr *= gr;
        s->ph += c->a;
        if (s->ph >= 1.0f)
            s->ph -= 1.0f;
        break;
    }
    default:
        return;
    }
    *xl += (yl - *xl) * c->mix;
    *xr += (yr - *xr) * c->mix;
}

void brk_fx_process(const float *in, const float *aux, float aux_send, float *l, float *r, uint32_t n,
                    uint32_t pads)
{
    uint32_t i, k;
    svf_c djc;
    float dj, dj_hz, dub_want = 0.0f, dub_lpk = 0.0f, dub_fb = 0.0f, wet;
    int dj_hp, throw = (pads >> PAD_FILTER) & 1u, dub_on = (pads >> PAD_DUB) & 1u;
    fx = *(const brk_fx_t *)&p_fx;
    wet = brk_clampf(fx.wet, 0.0f, 1.0f);
    /* the FILTER pad throws the DJ filter to its target at its speed, with its resonance */
    dj = throw ? brk_clampf(fx.flt_target, -1.0f, 1.0f) : brk_clampf(fx.dj, -1.0f, 1.0f);
    dj_s += (dj - dj_s) * (throw ? 0.01f + 0.3f * brk_clampf(fx.flt_speed, 0.0f, 1.0f) : 0.3f);
    dj_hp = dj_s > 0.0f;
    /* -1 .. 0: low-pass 18 kHz down to 80 Hz; 0 .. 1: high-pass 20 Hz up to 6 kHz (exponential sweeps) */
    dj_hz = dj_hp ? 20.0f * brk_exp2f(dj_s * 8.2f) : 18000.0f * brk_exp2f(dj_s * 7.8f);
    djc = svf_coef(dj_hz, throw ? brk_clampf(fx.flt_res, 0.0f, 0.95f) : 0.35f);
    if (dub_n) {                         /* the dub delay's time (glides, a tape-ish pitch bend) and tone */
        float want = DUB_STEPS[fx.dub_div % BRK_DUB_DIVS] * (float)BRK_SR * 60.0f / (p_bpm * 4.0f);
        dub_want = brk_clampf(want, 64.0f, (float)(dub_n - 4u));
        if (dub_time_s <= 0.0f)
            dub_time_s = dub_want;
        dub_lpk = 0.05f + 0.9f * brk_clampf(fx.dub_tone, 0.0f, 1.0f) * brk_clampf(fx.dub_tone, 0.0f, 1.0f);
        dub_fb = brk_clampf(fx.dub_fb, 0.0f, 0.95f);
    }
    for (k = 0; k < BRK_FX_SLOTS; k++)   /* the chain's settings, once a block */
        slot_block(k);
    for (i = 0; i < n; i++) {
        float x = in[i], xl = x, xr = x, lp;
        for (k = 0; k < BRK_FX_SLOTS; k++)   /* the chain, slot by slot */
            if (sc[k].on)
                slot_run(k, &xl, &xr);
        if (wet < 0.999f) {              /* PERFORM's FX: the chain against the dry break */
            xl = x + (xl - x) * wet;
            xr = x + (xr - x) * wet;
        }
        {
            float a = aux ? aux[i] : 0.0f;   /* the soundboard joins here: clean, then the delay and the filter */
            xl += a;
            xr += a;
        }
        if (dub_n) {                     /* the dub delay: held, the pad sends the break in; the echoes ring on */
            float d, rp, t, y;
            int32_t i0;
            dub_send += ((dub_on ? 1.0f : 0.0f) - dub_send) * 0.002f;
            dub_time_s += (dub_want - dub_time_s) * 0.00005f;
            d = dub_time_s;
            rp = (float)dub_w - d;
            while (rp < 0.0f)
                rp += (float)dub_n;
            i0 = (int32_t)rp;
            t = rp - (float)i0;
            y = ((float)dub_buf[(uint32_t)i0 % dub_n] * (1.0f - t) + (float)dub_buf[(uint32_t)(i0 + 1) % dub_n] * t)
                * (1.0f / 32768.0f);
            dub_lp += (y - dub_lp) * dub_lpk;    /* the tone: each echo darker */
            {
                float w = soft(0.5f * (xl + xr) * dub_send + (aux ? aux[i] * aux_send : 0.0f) + dub_lp * dub_fb);
                dub_buf[dub_w] = (int16_t)(w * 32000.0f);
            }
            dub_w = (dub_w + 1u) % dub_n;
            xl += dub_lp * fx.dub_mix;
            xr += dub_lp * fx.dub_mix * 0.92f;   /* (a touch of width) */
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
