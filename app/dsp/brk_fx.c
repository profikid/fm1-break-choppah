/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's effects (brk_fx.h). */
#define BRK_ENGINE 1                     /* (slot_run runs from RAM on the FM-1: what it uses inlined, brk_math.h) */
#include "brk_fx.h"
#include "brk.h"
#include "brk_math.h"

/* the block-rate code's maths: the shared copies (brk_math.c); the per-sample code has them inlined (BRK_INL) */
#define c_exp2 brk_exp2f_c
#define c_log2 brk_log2f_c
#define c_tan_pi brk_tan_pi_c
#define FL_LEN 256u
/* the block-rate code (settings mapped once a block, defaults): small, not fast; the per-sample code stays -O2 */
#if defined(__clang__)
#define BRK_COLD __attribute__((noinline, minsize))
#else
#define BRK_COLD __attribute__((noinline, cold))
#endif                      /* a flanger's line: 5.8 ms (it reaches 230 samples back) */

BRK_INL float soft(float x) { return x / (1.0f + brk_absf(x)); }
static volatile float p_bpm = 170.0f;
typedef struct { float ic1, ic2; } svf_t;  /* a TPT state-variable filter (Simper) */
typedef struct { float a1, a2, a3, k; } svf_c;

static volatile brk_fx_t p_fx;
static brk_fx_t fx;                      /* the render's copy */
static float lim_gain = 1.0f, dj_s;      /* the limiter; the DJ filter's smoothed position */
static svf_t dj_b, dj_e, dj_v;           /* the DJ filter: on the break, its echo throw, its reverb throw */
const char *const BRK_FXT_NAME[BRK_NFXT] = {"EMPTY", "FILTER", "CRUNCH", "DRIVE", "PUNCH", "FLANGER", "RING MOD",
                                            "TREMOLO", "REVERB", "DELAY", "EQ", "TAPE", "MB COMP", "CLIP"};
const char *const BRK_DLY_DIV_NAME[BRK_DLY_DIVS] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "1/8T", "1/4T"};
static const float DLY_STEPS[BRK_DLY_DIVS] = {1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 4.0f / 3.0f, 8.0f / 3.0f};
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
    float fl[2][FL_LEN];                 /* FLANGER; TAPE's wow */
    uint32_t w;
    svf_t eq[3][2];                      /* EQ: low, mid, high, left and right; TAPE: eq[0] its head bump;
                                          * MULTIBAND: eq[0] the high band's split (f: the low's) */
    float mb_env[3], mb_g[3];            /* MULTIBAND: each band's level followed, its gain */
    uint32_t mb_n;                       /* (its gains are worked out every fourth sample) */
    float hp[2];                         /* the extras' own: DRIVE's low cut (lp-subtracted), PUNCH's side-chain */
    uint32_t rng;                        /* CRUNCH's dither, TAPE's hiss */
} slot_st_t;
static slot_st_t st_chain[BRK_FX_SLOTS];
/* each slot's state: the chain's here, the pads' voices in the caller's RAM (brk_fx_pad_buffer; 0: no pads) */
static slot_st_t *st[BRK_FX_ALL] = {&st_chain[0], &st_chain[1], &st_chain[2], &st_chain[3]};
typedef char pad_st_fits[sizeof(slot_st_t) <= BRK_PAD_ST_BYTES ? 1 : -1];
/* the pads' voices (PRD B-55): their slots as set, which are on; the render's copies, each one's gate (eased), its
 * wet peak this block, which still sound; REVERB and DELAY gate their send (the tail rings on), the rest their wet */
static volatile brk_fxslot_t p_pad[BRK_PAD_V];
static volatile uint32_t p_pad_on;
static brk_fxslot_t pad_sl[BRK_PAD_V];
static float pad_g[BRK_PAD_V], pad_pk[BRK_PAD_V];
static uint32_t pad_busy;
/* each slot's extras (brk_fxx_t) as set, its modulator and its meters (kept across effect changes) */
static volatile brk_fxx_t p_fxx[BRK_FX_ALL];
#define SHED_BLOCKS 344u                 /* 2 s of blocks: a shed slot comes back after that, clear */
static volatile uint32_t shed_hit;       /* the platform: a render ran over 90 % */
static uint32_t shed_lv, shed_n;         /* slots shed now (from the chain's end), blocks till one comes back */
void brk_fx_overload(void) { shed_hit = 1u; }
uint32_t brk_fx_shed(void) { return shed_lv; }
static volatile uint32_t watch = BRK_FX_ALL;     /* the slot the screen meters (BRK_FX_ALL: none) */
void brk_fx_watch(uint32_t slot) { watch = slot; }
static struct {
    float ph, sh, env, mod;              /* the modulator: its phase, S&H's held value, the input followed, now */
    float in, out, pk_in, pk_out;        /* the meters (peaks falling back) and this block's peaks */
    float p[4];                          /* the settings as modulated this block */
    uint32_t rng;
    uint32_t track;                      /* its peaks followed this block: the screen's slot, or its ENV needs them */
} md[BRK_FX_ALL];
static const float MOD_STEPS[BRK_MOD_DIVS] = {64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 8.0f / 3.0f, 4.0f / 3.0f};
const char *const BRK_MOD_DIV_NAME[BRK_MOD_DIVS] = {"4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16",
                                                    "1/4T", "1/8T"};
const char *const BRK_MOD_SHAPE_NAME[BRK_MOD_SHAPES] = {"SINE", "TRIANGLE", "SAW", "RAMP", "SQUARE", "S&H", "ENV"};
static const float FLSYNC_STEPS[BRK_FLSYNCS] = {0.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f};
const char *const BRK_FLSYNC_NAME[BRK_FLSYNCS] = {"OFF", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4"};
const float BRK_PUNCH_RATIO[BRK_PUNCH_RATIOS] = {1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 20.0f};


BRK_COLD void brk_fxx_defaults(brk_fxx_t *x, int type)
{
    /* each effect's MORE page as it always sounded (see brk_fx.h), then the MOD page: 1 bar, none, sine, K1 */
    static const float D[BRK_NFXT][4] = {
        {0.0f, 0.0f, 0.0f, 0.0f},        /* EMPTY */
        {0.0f, 0.0f, 0.0f, 0.0f},        /* FILTER: no drive, 12 dB, no spread */
        {1.0f, 0.0f, 0.0f, 0.0f},        /* CRUNCH: no tone filter, no dither */
        {0.0f, 0.0f, 0.0f, 0.0f},        /* DRIVE: soft, no bias, no low cut */
        {3.0f / 7.0f, 0.0f, 0.0f, 0.0f}, /* PUNCH: 4:1, no make-up, no side-chain filter */
        {0.0f, 0.5f, 0.0f, 0.0f},        /* FLANGER: 0.2 ms, 90 degrees, free */
        {0.0f, 0.0f, 0.0f, 0.0f},        /* RING MOD: sine, ring */
        {0.0f, 0.0f, 0.0f, 0.0f},        /* TREMOLO: sine, sharp */
        {1.0f, 0.0f, 1.0f, 1.0f},        /* REVERB: wide, no low cut, the full wobble, full diffusion */
        {1.0f, 0.0f, 0.0f, 0.0f},        /* DELAY: full ping-pong, no low cut, no ducking */
        {0.47712f, 0.42413f, 2.0f / 3.0f, 0.5f},   /* EQ: 120 Hz, Q 0.9, 8 kHz, 0 dB */
        {0.1f, 0.50117f, 0.0f, 0.0f},    /* TAPE: a little flutter, wow at 0.55 Hz, no hiss */
        {0.0f, 0.0f, 0.0f, 0.0f},        /* MB COMP (its detail is brk_mbc_t) */
        {0.0f, 1.0f, 0.0f, 0.0f},        /* CLIP: no bias, 0 dB out */
    };
    uint32_t t = (uint32_t)type < BRK_NFXT ? (uint32_t)type : 0u, i;
    for (i = 0; i < 4u; i++)
        x->x[i] = D[t][i];
    x->x[FXX_RATE] = 2.0f / (float)(BRK_MOD_DIVS - 1u);
    x->x[FXX_DEPTH] = 0.5f;
    x->x[FXX_SHAPE] = 0.0f;
    x->x[FXX_TARGET] = 0.0f;
    x->type = (uint8_t)t;
    x->rsv[0] = x->rsv[1] = x->rsv[2] = 0u;
}

/* slot k's extras for its effect t this block: as set, or t's defaults when they are another effect's */
BRK_COLD static void fxx_get(uint32_t k, uint32_t t, brk_fxx_t *out)
{
    *out = *(const brk_fxx_t *)&p_fxx[k];
    if (out->type != t)
        brk_fxx_defaults(out, (int)t);
}

void brk_fx_set_fxx(uint32_t slot, const brk_fxx_t *x)
{
    if (slot < BRK_FX_ALL)
        *(brk_fxx_t *)&p_fxx[slot] = *x;
}

BRK_COLD void brk_fx_meter(uint32_t slot, brk_fx_meter_t *m)
{
    uint32_t i, live = slot < BRK_FX_ALL;
    m->in = live ? md[slot].in : 0.0f;
    m->out = live ? md[slot].out : 0.0f;
    m->mod = live ? md[slot].mod : 0.0f;
    m->ph = live ? md[slot].ph : 0.0f;
    for (i = 0; i < 4u; i++)
        m->p[i] = live ? md[slot].p[i] : 0.0f;
}

/* ------------------------------------------------------------------- reverb --- */
/* a plate after Jon Dattorro, "Effect Design, Part 1" (JAES 45/9, 1997): a pre-delay, four input diffusers, then a
 * tank of two cross-fed halves (a modulated allpass, a delay, damping, an allpass, a delay each) read at fourteen taps
 * for left and right. It runs at 22.05 kHz (every other sample; the tail needs no more) on 16-bit lines, ~35 KB, its
 * lengths Dattorro's at 29.76 kHz scaled. One in the chain: the first REVERB slot has it */
#define RV(n) ((uint32_t)(((uint32_t)(n) * 22050u + 14880u) / 29761u))
#define RV_EXC 12u                       /* the tank's modulation: up to 12 samples */
#define RV_PRE 1324u                     /* 60 ms */
#define RV_Q 8192.0f                     /* a line's 1.0 (headroom to 4.0) */
enum { RV_LPRE, RV_LAP1, RV_LAP2, RV_LAP3, RV_LAP4, RV_LMAP1, RV_LDL1, RV_LAP5, RV_LDL2, RV_LMAP2, RV_LDL3, RV_LAP6,
       RV_LDL4, RV_NL };
static const uint16_t RV_LEN[RV_NL] = {RV_PRE, RV(142), RV(107), RV(379), RV(277), RV(672) + RV_EXC + 2u, RV(4453),
                                       RV(1800), RV(3720), RV(908) + RV_EXC + 2u, RV(4217), RV(2656), RV(3163)};
#define RV_MEM (RV_PRE + RV(142) + RV(107) + RV(379) + RV(277) + RV(672) + RV(4453) + RV(1800) + RV(3720) + \
                RV(908) + RV(4217) + RV(2656) + RV(3163) + 2u * (RV_EXC + 2u))
static int16_t rv_mem[RV_MEM];
static struct {
    uint16_t base[RV_NL], w[RV_NL];
    float damp_l, damp_r, last_l, last_r, ph, in_prev, y_l, y_r, yo_l, yo_r;
    uint8_t odd;
} rv;

static void rv_clear(void)
{
    uint32_t i, b = 0u;
    for (i = 0; i < RV_MEM; i++)
        rv_mem[i] = 0;
    for (i = 0; i < RV_NL; i++) {
        rv.base[i] = (uint16_t)b;
        rv.w[i] = 0u;
        b += RV_LEN[i];
    }
    rv.damp_l = rv.damp_r = rv.last_l = rv.last_r = rv.in_prev = 0.0f;
    rv.y_l = rv.y_r = rv.yo_l = rv.yo_r = 0.0f;
    rv.odd = 0u;
}

BRK_INL void rv_put(uint32_t l, float x)
{
    float v = x * RV_Q;
    rv_mem[rv.base[l] + rv.w[l]] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
    if (++rv.w[l] >= RV_LEN[l])
        rv.w[l] = 0u;
}

BRK_INL float rv_tap(uint32_t l, uint32_t d)   /* d samples back (1 .. its length) */
{
    int32_t i = (int32_t)rv.w[l] - (int32_t)d;
    if (i < 0)
        i += RV_LEN[l];
    return (float)rv_mem[rv.base[l] + (uint32_t)i] * (1.0f / RV_Q);
}

BRK_INL float rv_tapf(uint32_t l, float d)  /* (linear between two: the modulated allpasses) */
{
    uint32_t i = (uint32_t)d;
    float t = d - (float)i;
    return rv_tap(l, i) * (1.0f - t) + rv_tap(l, i + 1u) * t;
}

/* an allpass on line l, d samples long, coefficient g */
BRK_INL float rv_ap(uint32_t l, float x, float d, float g)
{
    float z = d == (float)(uint32_t)d ? rv_tap(l, (uint32_t)d) : rv_tapf(l, d), v = x - g * z;
    rv_put(l, v);
    return z + g * v;
}

/* one 22.05 kHz sample: in mono, out the plate's left and right */
static struct { float g1, g2, exc, lc, lcz, width; } rvp = {0.75f, 0.625f, (float)RV_EXC, 0.0f, 0.0f, 1.0f};
BRK_INL void rv_tick(float x, float decay, float damp, uint32_t pre, float *yl, float *yr)
{
    float a, m1, m2;
    const float g1 = rvp.g1, g2 = rvp.g2, exc = rvp.exc;   /* (in registers: the lines' stores don't reload them) */
    if (rvp.lc > 0.0f) {                 /* the low cut into it: what a one-pole low-pass keeps, taken away */
        rvp.lcz += (x - rvp.lcz) * rvp.lc;
        x -= rvp.lcz;
    }
    rv_put(RV_LPRE, x);
    a = pre ? rv_tap(RV_LPRE, pre) : x;
    a = rv_ap(RV_LAP1, a, (float)RV(142), g1);
    a = rv_ap(RV_LAP2, a, (float)RV(107), g1);
    a = rv_ap(RV_LAP3, a, (float)RV(379), g2);
    a = rv_ap(RV_LAP4, a, (float)RV(277), g2);
    rv.ph += 0.9f / 22050.0f;            /* the tank's slow wobble: no metallic ring */
    if (rv.ph >= 1.0f)
        rv.ph -= 1.0f;
    m1 = (float)RV(672) + 1.0f + exc * (0.5f + 0.5f * brk_sin_turns(rv.ph));
    m2 = (float)RV(908) + 1.0f + exc * (0.5f + 0.5f * brk_sin_turns(rv.ph + 0.5f));
    {                                    /* the left half, fed by the right's end */
        float t = rv_ap(RV_LMAP1, a + decay * rv.last_r, m1, -0.7f), u;
        rv_put(RV_LDL1, t);
        u = rv_tap(RV_LDL1, RV(4453));
        rv.damp_l += (u - rv.damp_l) * (1.0f - damp);
        u = rv_ap(RV_LAP5, rv.damp_l * decay, (float)RV(1800), 0.5f);
        rv_put(RV_LDL2, u);
        rv.last_l = rv_tap(RV_LDL2, RV(3720));
    }
    {                                    /* the right half, fed by the left's end */
        float t = rv_ap(RV_LMAP2, a + decay * rv.last_l, m2, -0.7f), u;
        rv_put(RV_LDL3, t);
        u = rv_tap(RV_LDL3, RV(4217));
        rv.damp_r += (u - rv.damp_r) * (1.0f - damp);
        u = rv_ap(RV_LAP6, rv.damp_r * decay, (float)RV(2656), 0.5f);
        rv_put(RV_LDL4, u);
        rv.last_r = rv_tap(RV_LDL4, RV(3163));
    }
    *yl = 0.6f * (rv_tap(RV_LDL3, RV(266)) + rv_tap(RV_LDL3, RV(2974)) - rv_tap(RV_LAP6, RV(1913)) +
                  rv_tap(RV_LDL4, RV(1996)) - rv_tap(RV_LDL1, RV(1990)) - rv_tap(RV_LAP5, RV(187)) -
                  rv_tap(RV_LDL2, RV(1066)));
    *yr = 0.6f * (rv_tap(RV_LDL1, RV(353)) + rv_tap(RV_LDL1, RV(3627)) - rv_tap(RV_LAP5, RV(1228)) +
                  rv_tap(RV_LDL2, RV(2673)) - rv_tap(RV_LDL3, RV(2111)) - rv_tap(RV_LAP6, RV(335)) -
                  rv_tap(RV_LDL4, RV(121)));
}
/* the delay: a stereo ping-pong at 22.05 kHz on the 16-bit line the app hands over (left and right interleaved), at
 * a DELAY slot or after the chain (the DUB pad's settings) */
const char *const BRK_DUB_DIV_NAME[BRK_DUB_DIVS] = {"1/8", "3/16", "1/4", "3/8"};
static const uint8_t DUB_DIV_DLY[BRK_DUB_DIVS] = {1u, 2u, 3u, 4u};   /* the DUB pad's times among the delay's */
static int16_t *dub_buf;
static uint32_t dub_n, dub_w;            /* (dub_n: frames of two) */
static float dub_send;                   /* the DUB pad's throw, eased */
static struct {
    float lp_l, lp_r, y_l, y_r, yo_l, yo_r, in_prev, time_s, lcz, duck;
    uint8_t odd;
} dl;
/* a block's: frames, feedback, the tone's coefficient, return; the DELAY slot's extras: the loop's low cut (a one-pole
 * coefficient, 0 none), width, ducking */
typedef struct { float time, fb, tone, ret, lc, width, duck; } dl_c_t;

BRK_INL float dl_read(uint32_t ch, float d)   /* d frames back, linear */
{
    float r = (float)dub_w - d, t;
    uint32_t i;
    while (r < 0.0f)
        r += (float)dub_n;
    i = (uint32_t)r;
    t = r - (float)i;
    return ((float)dub_buf[2u * (i % dub_n) + ch] * (1.0f - t) + (float)dub_buf[2u * ((i + 1u) % dub_n) + ch] * t)
           * (1.0f / 32000.0f);
}

/* one 22.05 kHz frame: x in (mono), the echoes out. Left takes the input and right's echo fed back, right takes
 * left's echo: the repeats bounce between the sides, darker each time */
BRK_INL void dl_tick(const dl_c_t *c, float x, float *yl, float *yr)
{
    float ol, or_;
    dl.time_s += (c->time - dl.time_s) * 0.0005f;   /* a new time glides there (a tape's bend) */
    ol = dl_read(0u, dl.time_s);
    or_ = dl_read(1u, dl.time_s);
    dl.lp_l += (ol - dl.lp_l) * c->tone;
    dl.lp_r += (or_ - dl.lp_r) * c->tone;
    or_ = dl.lp_r;
    if (c->lc > 0.0f) {                  /* the loop's low cut: each repeat thinner */
        dl.lcz += (or_ - dl.lcz) * c->lc;
        or_ -= dl.lcz;
    }
    dub_buf[2u * dub_w] = (int16_t)(soft(x + or_ * c->fb) * 32000.0f);
    dub_buf[2u * dub_w + 1u] = (int16_t)(soft(dl.lp_l) * 32000.0f);
    dub_w = (dub_w + 1u) % dub_n;
    *yl = dl.lp_l;
    *yr = dl.lp_r;
}

/* the delay at the full rate: a frame every other sample, linear between */
BRK_INL void dl_run(const dl_c_t *c, float x, float *yl, float *yr)
{
    if (dl.odd) {
        dl.yo_l = dl.y_l;
        dl.yo_r = dl.y_r;
        dl_tick(c, 0.5f * (x + dl.in_prev), &dl.y_l, &dl.y_r);
        *yl = 0.5f * (dl.yo_l + dl.y_l);
        *yr = 0.5f * (dl.yo_r + dl.y_r);
    } else {
        *yl = dl.y_l;
        *yr = dl.y_r;
    }
    dl.in_prev = x;
    dl.odd ^= 1u;
}

BRK_COLD static void dl_settings(dl_c_t *c, uint32_t div, float fb, float tone, float ret)
{
    float want = DLY_STEPS[div % BRK_DLY_DIVS] * 22050.0f * 60.0f / (p_bpm * 4.0f);
    c->time = brk_clampf(want, 32.0f, (float)(dub_n - 4u));
    if (dl.time_s <= 0.0f)
        dl.time_s = c->time;
    c->fb = brk_clampf(fb, 0.0f, 0.95f);
    tone = brk_clampf(tone, 0.0f, 1.0f);
    c->tone = 0.08f + 0.9f * tone * tone;
    c->ret = ret;
    c->lc = 0.0f;
    c->width = 1.0f;
    c->duck = 0.0f;
}

BRK_COLD static svf_c svf_coef(float hz, float res)
{
    svf_c c;
    float g = c_tan_pi(hz / (float)BRK_SR);
    c.k = 2.0f - 2.0f * brk_clampf(res, 0.0f, 0.95f);
    c.a1 = 1.0f / (1.0f + g * (g + c.k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

/* one sample; *lp gets the low-pass, the return is the high-pass */
BRK_INL float svf_run(svf_t *s, const svf_c *c, float x, float *lp)
{
    float v3 = x - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    *lp = v2;
    return x - c->k * v1 - v2;
}

BRK_INL float fl_read(const float *buf, uint32_t w, float d)   /* d samples back from w, linear */
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

/* clean below 0.8, then a soft knee that never passes 1 */
BRK_INL float knee(float x)
{
    float a = brk_absf(x);
    if (a <= 0.8f)
        return x;
    a = 0.8f + 0.2f * soft((a - 0.8f) * 5.0f);
    return x < 0.0f ? -a : a;
}

BRK_COLD void brk_fx_slot_defaults(brk_fxslot_t *s, int type)
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
        {0.6f, 0.4f, 0.25f, 0.3f},       /* REVERB: a medium plate, 15 ms ahead */
        {2.0f / 7.0f, 0.5f, 0.5f, 0.35f},   /* DELAY: 3/16, feedback 0.47 */
        {0.5f, 0.5f, 0.5f, 0.5f},        /* EQ: flat, the mid at 1 kHz */
        {0.4f, 0.5f, 0.15f, 1.0f},       /* TAPE: 10 dB of drive, a little wow */
        {0.5f, 0.35f, 0.45f, 1.0f},      /* MULTIBAND: the low held down, the rest lighter */
        {0.4f, 0.5f, 0.06f, 1.0f},       /* CLIP: +10 dB in, a middling knee, -0.7 dB */
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
    dl.lp_l = dl.lp_r = dl.y_l = dl.y_r = dl.yo_l = dl.yo_r = dl.in_prev = dl.time_s = 0.0f;
    dub_n = buf ? n / 2u : 0u;           /* frames of left and right */
}
void brk_fx_tempo(float bpm) { p_bpm = brk_clampf(bpm, 40.0f, 300.0f); }
float brk_fx_dj_now(void) { return dj_s; }

BRK_COLD void brk_fx_init(void)
{
    rv_clear();
    brk_fx_t d;
    brk_mbc_t m;
    uint32_t k;
    brk_fx_defaults(&d);
    brk_fx_set(&d);
    brk_mbc_defaults(&m);
    for (k = 0; k < BRK_FX_SLOTS; k++) {
        brk_fxx_t x;
        brk_fxx_defaults(&x, d.slot[k].type);
        brk_fx_set_mbc(k, &m);
        brk_fx_set_fxx(k, &x);
    }
}

void brk_fx_set(const brk_fx_t *p)
{
    *(brk_fx_t *)&p_fx = *p;             /* written whole from the main loop; the render copies it per block */
}

void brk_fx_pad_buffer(void *mem)
{
    uint32_t v;
    p_pad_on = 0u;
    for (v = 0; v < BRK_PAD_V; v++)
        st[BRK_FX_SLOTS + v] = mem ? (slot_st_t *)((uint8_t *)mem + v * BRK_PAD_ST_BYTES) : 0;
    for (v = 0; mem && v < BRK_PAD_V; v++)
        st[BRK_FX_SLOTS + v]->type = 0xFFu;
}
void brk_fx_set_pad(uint32_t v, const brk_fxslot_t *s, uint32_t on)
{
    brk_fxslot_t c = *s;
    if (v >= BRK_PAD_V)
        return;
    if (c.type >= BRK_NFXT || c.type == FXT_MBCOMP)
        c.type = FXT_EMPTY;              /* (no MB COMP on a pad: its detail is the chain's) */
    *(brk_fxslot_t *)&p_pad[v] = c;
    p_pad_on = on ? p_pad_on | 1u << v : p_pad_on & ~(1u << v);
}
uint32_t brk_fx_pad_busy(void) { return pad_busy | p_pad_on; }

/* Andrew Simper's state-variable EQ bands (SvfLinearTrapOptimised2): g from the frequency, k the damping, and the
 * mix of the input, band and low outputs. A = 10^(dB/40) = 2^(dB * 0.0830), sqrt(A) = 2^(dB * 0.0415) */
enum { BAND_LOWSHELF, BAND_BELL, BAND_HIGHSHELF };
BRK_COLD static void eq_band(svf_c *c, float *m, int kind, float hz, float q, float db)
{
    float A = c_exp2(db * 0.08304f), sA = c_exp2(db * 0.04152f), g = c_tan_pi(hz / (float)BRK_SR);
    if (kind == BAND_LOWSHELF)
        g /= sA;
    else if (kind == BAND_HIGHSHELF)
        g *= sA;
    c->k = kind == BAND_BELL ? 1.0f / (q * A) : 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
    if (kind == BAND_LOWSHELF) {
        m[0] = 1.0f;
        m[1] = c->k * (A - 1.0f);
        m[2] = A * A - 1.0f;
    } else if (kind == BAND_BELL) {
        m[0] = 1.0f;
        m[1] = c->k * (A * A - 1.0f);
        m[2] = 0.0f;
    } else {
        m[0] = A * A;
        m[1] = c->k * (1.0f - A) * A;
        m[2] = 1.0f - A * A;
    }
}

BRK_INL float eq_run(svf_t *s, const svf_c *c, const float *m, float x)
{
    float v3 = x - s->ic2, v1 = c->a1 * s->ic1 + c->a2 * v3, v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    return m[0] * x + m[1] * v1 + m[2] * v2;
}

/* a slot's settings for this block, mapped from 0..1 */
typedef struct {
    uint8_t on, type;
    float mix;
    svf_c c;                             /* FILTER */
    uint8_t mode;                        /* FILTER: 0 low-pass, 1 band-pass, 2 high-pass */
    float a, b, g;                       /* the effect's own: see slot_block */
    svf_c e[3];                          /* EQ's bands (TAPE: e[0] the bump) */
    float m[3][3];                       /* and their mixes: out = v0 * m0 + v1 * m1 + v2 * m2 */
    float mb_att[3], mb_rel[3], mb_w;    /* MB COMP: each band's attack and release (per sample), the knee (octaves) */
    uint8_t mb_solo;                     /* and its solo (0 none, 1 + the band) */
    uint8_t qi;                          /* the extras (MORE page) mapped: a choice, four values (see slot_block) */
    float q[4];
} slot_c_t;
static slot_c_t sc[BRK_FX_ALL];

/* MB COMP's detail: per slot, set from the main loop */
const float BRK_MB_RATIO[BRK_MB_RATIOS] = {1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f, 10.0f, 12.0f,
                                           16.0f, 20.0f, 1000.0f};
static volatile brk_mbc_t p_mbc[BRK_FX_SLOTS];   /* (read once a block by slot_block, as fx is copied) */

BRK_COLD void brk_mbc_defaults(brk_mbc_t *m)
{
    /* what MB COMP always was: -30 dB and 8:1 at full amount, the low slow (10 ms in, 150 out), the mid 5 / 100, the
     * high fast (2 / 60), split at 200 Hz and 2.5 kHz, a hard knee, nothing added after */
    static const float ATT[3] = {0.6667f, 0.5661f, 0.4337f}, REL[3] = {0.5878f, 0.5f, 0.3890f};
    uint32_t b;
    for (b = 0; b < MB_BANDS; b++) {
        m->thr[b] = 0.375f;
        m->ratio[b] = 9.0f / (float)(BRK_MB_RATIOS - 1u);
        m->att[b] = ATT[b];
        m->rel[b] = REL[b];
        m->gain[b] = 0.5f;
    }
    m->xlo = 0.5372f;
    m->xhi = 0.3686f;
    m->knee = 0.0f;
    m->out = 0.5f;
    m->solo = 0u;
    m->rsv[0] = m->rsv[1] = m->rsv[2] = 0u;
}

void brk_fx_set_mbc(uint32_t slot, const brk_mbc_t *m)
{
    if (slot < BRK_FX_SLOTS)
        *(brk_mbc_t *)&p_mbc[slot] = *m;   /* (whole, as brk_fx_set: the render copies it per block) */
}
static int rv_taken;                     /* a slot has the reverb this block (the first REVERB in the chain) */
static int dl_taken;                     /* and the delay (the first DELAY, when the line is there) */
static dl_c_t dl_slot_c;                 /* the DELAY slot's settings this block */
static float cur_throw, cur_verb;        /* this sample's throws into the delay and the reverb */


/* a slot's modulator, once a block: its phase on the tempo, S&H's new value each cycle, ENV the slot's input followed
 * (-36 .. 0 dB as 0..1); the setting it targets moved by it (the render's copy of the slot: fresh each block); the
 * meters */
static brk_fxslot_t *slot_of(uint32_t k) { return k < BRK_FX_SLOTS ? &fx.slot[k] : &pad_sl[k - BRK_FX_SLOTS]; }
BRK_COLD static void mod_block(uint32_t k, uint32_t n, const float *x)
{
    brk_fxslot_t *f = slot_of(k);
    float depth = (brk_clampf(x[FXX_DEPTH], 0.0f, 1.0f) - 0.5f) * 2.0f, m, *t;
    uint32_t shape = brk_idx(x[FXX_SHAPE], BRK_MOD_SHAPES), i;
    float steps = MOD_STEPS[brk_idx(x[FXX_RATE], BRK_MOD_DIVS)];
    md[k].ph += (float)n / (steps * (float)BRK_SR * 60.0f / (p_bpm * 4.0f));
    if (md[k].ph >= 1.0f) {
        md[k].ph -= (float)(int32_t)md[k].ph;
        md[k].sh = 2.0f * brk_randf(&md[k].rng) - 1.0f;
    }
    md[k].env += (md[k].pk_in - md[k].env) * (md[k].pk_in > md[k].env ? 0.6f : 0.08f);
    if (shape == MOD_ENV)
        m = md[k].env > 0.0001f ? brk_clampf((6.0206f * c_log2(md[k].env) + 36.0f) * (1.0f / 36.0f), 0.0f, 1.0f) : 0.0f;
    else
        m = shape == MOD_SH ? md[k].sh : brk_mod_wave(shape, md[k].ph);
    md[k].mod = m;
    md[k].in = md[k].pk_in > md[k].in * 0.94f ? md[k].pk_in : md[k].in * 0.94f;
    md[k].out = md[k].pk_out > md[k].out * 0.94f ? md[k].pk_out : md[k].out * 0.94f;
    md[k].pk_in = md[k].pk_out = 0.0f;
    md[k].track = k == watch || (shape == MOD_ENV && (depth < -0.01f || depth > 0.01f));
    if (depth < -0.01f || depth > 0.01f) {
        i = brk_idx(x[FXX_TARGET], 4u);
        t = i < 3u ? &f->p[i] : &f->mix;
        *t = brk_clampf(*t + 0.5f * depth * m, 0.0f, 1.0f);
    }
    for (i = 0; i < 3u; i++)
        md[k].p[i] = f->p[i];
    md[k].p[3] = f->mix;
}

BRK_COLD static void slot_block(uint32_t k, const float *x)
{
    const brk_fxslot_t *f = slot_of(k);
    slot_st_t *s = st[k];
    slot_c_t *c = &sc[k];
    float p0 = brk_clampf(f->p[0], 0.0f, 1.0f), p1 = brk_clampf(f->p[1], 0.0f, 1.0f), p2 = brk_clampf(f->p[2], 0.0f, 1.0f);
    float x0 = brk_clampf(x[0], 0.0f, 1.0f), x1 = brk_clampf(x[1], 0.0f, 1.0f), x2 = brk_clampf(x[2], 0.0f, 1.0f);
    float x3 = brk_clampf(x[3], 0.0f, 1.0f);
    uint32_t t = f->type < BRK_NFXT ? f->type : FXT_EMPTY;
    if (s->type != t) {                  /* a new effect here: its state from nothing */
        uint8_t *z = (uint8_t *)s;
        uint32_t i;
        for (i = 0; i < sizeof *s; i++)
            z[i] = 0u;
        s->type = (uint8_t)t;
        s->hz_s = 30.0f * c_exp2(p0 * 9.06f);
        s->gr = 1.0f;
        if (t == FXT_REVERB && !rv_taken)
            rv_clear();                  /* a reverb put in: its tail from silence (not one that is in use) */
    }
    c->type = (uint8_t)t;
    c->on = (uint8_t)(f->on && t != FXT_EMPTY);
    c->mix = brk_clampf(f->mix, 0.0f, 1.0f);
    switch (t) {
    case FXT_FILTER:                     /* the cutoff glides (no zipper noise, PRD A-41) */
        s->hz_s += (30.0f * c_exp2(p0 * 9.06f) - s->hz_s) * 0.3f;
        c->c = svf_coef(brk_clampf(s->hz_s, 20.0f, 18000.0f), p1 * 0.95f);
        c->mode = (uint8_t)(p2 < 0.33f ? 0u : p2 < 0.67f ? 1u : 2u);
        /* extras: q[0] the drive (1: none), q[1] its make-up (a 0.7 peak stays 0.7), qi 24 dB; e[0] the right's */
        c->q[0] = 1.0f + 7.0f * x0 * x0;
        c->q[1] = 0.7f / soft(0.7f * c->q[0]);
        c->qi = (uint8_t)(x1 >= 0.5f);
        c->e[0] = x2 > 0.005f ? svf_coef(brk_clampf(s->hz_s * c_exp2(2.0f * x2), 20.0f, 18000.0f), p1 * 0.95f) : c->c;
        break;
    case FXT_CRUNCH:                     /* a: the levels (2^(bits-1)), b: the rate's step, g: the drive */
        c->a = c_exp2(16.0f - 14.0f * p0 - 1.0f);
        c->b = c_exp2(-p1 * 5.46f);
        c->g = 1.0f + 8.0f * p2;
        /* extras: q[0] the tone's coefficient (1: off), q[1] the dither (in steps) */
        c->q[0] = x0 > 0.99f ? 1.0f
                  : brk_clampf(c_tan_pi((1000.0f * c_exp2(x0 * 4.32f)) / (float)BRK_SR) * 2.0f, 0.02f, 1.0f);
        c->q[1] = x1;
        break;
    case FXT_DRIVE:                      /* g: the gain, b: the tone's coefficient, a: the level after */
        c->g = 1.0f + 29.0f * p0 * p0;
        c->b = brk_clampf(c_tan_pi((800.0f * c_exp2(p1 * 4.3f)) / (float)BRK_SR) * 2.0f, 0.02f, 1.0f);
        c->a = (0.25f + 0.75f * p2) * (1.0f + 0.3f * p0);
        /* extras: qi the shape, q[0] the bias, q[1] the low cut's coefficient (0: off) */
        c->qi = (uint8_t)brk_idx(x0, 4u);
        c->q[0] = 0.6f * x1;
        c->q[1] = x2 > 0.005f ? brk_clampf(6.2832f * 20.0f * c_exp2(x2 * 5.32f) / (float)BRK_SR, 0.0f, 0.5f) : 0.0f;
        break;
    case FXT_PUNCH:                      /* a: the threshold, b: attack, g: release (per-sample coefficients) */
        c->a = c_exp2(-p0 * 5.0f);    /* 1 .. 1/32: the more, the lower */
        c->b = 1.0f / (1.0f + (0.1f + 29.9f * p1 * p1) * 44.1f);
        c->g = 1.0f / (1.0f + (20.0f + 480.0f * p2) * 44.1f);
        /* extras: q[0] 1/ratio, q[1] the make-up (to full scale, and more), q[2] the side-chain's high-pass */
        c->q[0] = 1.0f / BRK_PUNCH_RATIO[brk_idx(x0, BRK_PUNCH_RATIOS)];
        c->q[1] = c_exp2(x1 * 12.0f * 0.16610f) / (c->a + (1.0f - c->a) * c->q[0]);
        c->q[2] = x2 > 0.005f ? 6.2832f * 20.0f * c_exp2(x2 * 3.907f) / (float)BRK_SR : 0.0f;
        break;
    case FXT_FLANGER:                    /* a: the rate's step, b: the depth's span (samples), g: feedback */
        c->a = 0.02f * c_exp2(p0 * 9.0f) / (float)BRK_SR;
        c->b = (0.2f + 4.8f * p1) * 44.1f;
        c->g = (2.0f * p2 - 1.0f) * 0.9f;
        /* extras: q[0] where the sweep starts (samples), q[1] the right's phase behind, the rate on the tempo */
        c->q[0] = 9.0f + 111.0f * x0;
        if (c->q[0] + c->b > 250.0f)
            c->b = 250.0f - c->q[0];
        c->q[1] = 0.5f * x1;
        if (brk_idx(x2, BRK_FLSYNCS))
            c->a = 1.0f / (FLSYNC_STEPS[brk_idx(x2, BRK_FLSYNCS)] * (float)BRK_SR * 60.0f / (p_bpm * 4.0f));
        break;
    case FXT_RING:                       /* a: left's step, b: right's */
        c->a = 20.0f * c_exp2(p0 * 6.64f) / (float)BRK_SR;
        c->b = c->a * (1.0f + p1);
        c->qi = (uint8_t)(brk_idx(x0, 3u) | (x1 >= 0.5f ? 4u : 0u));   /* the wave (SINE TRI SQUARE), 4: AM */
        break;
    case FXT_REVERB:                     /* a: decay, b: damping, g: pre-delay (22.05 kHz samples) */
        if (rv_taken || !c->on) {        /* (one reverb: a second passes the sound on) */
            c->on = 0u;
            break;
        }
        rv_taken = 1;
        c->a = 0.25f + 0.72f * p0;
        c->b = 0.0005f + 0.7f * p1;
        c->g = (float)(uint32_t)(p2 * (float)(RV_PRE - 2u));
        rvp.width = x0;                  /* the extras straight into the plate (there is one) */
        rvp.lc = x1 > 0.005f ? brk_clampf(6.2832f * 20.0f * c_exp2(x1 * 4.907f) / 22050.0f, 0.0f, 0.5f) : 0.0f;
        rvp.exc = (float)RV_EXC * x2;
        rvp.g1 = 0.75f * (0.4f + 0.6f * x3);
        rvp.g2 = 0.625f * (0.4f + 0.6f * x3);
        break;
    case FXT_DELAY:                      /* the delay's settings; mix is the send */
        if (dl_taken || !c->on || !dub_n) {
            c->on = 0u;
            break;
        }
        dl_taken = 1;
        dl_settings(&dl_slot_c, (uint32_t)(p0 * (float)(BRK_DLY_DIVS - 1u) + 0.5f), p1 * 0.95f, p2, 1.0f);
        dl_slot_c.width = x0;
        dl_slot_c.lc = x1 > 0.005f ? brk_clampf(6.2832f * 20.0f * c_exp2(x1 * 5.644f) / 22050.0f, 0.0f, 0.5f) : 0.0f;
        dl_slot_c.duck = x2;
        break;
    case FXT_EQ:                         /* three bands; mix is the mid's frequency */
        eq_band(&c->e[0], c->m[0], BAND_LOWSHELF, 40.0f * c_exp2(x0 * 3.3219f), 0.707f, (p0 - 0.5f) * 24.0f);
        eq_band(&c->e[1], c->m[1], BAND_BELL, 200.0f * c_exp2(c->mix * 4.64f), 0.3f * c_exp2(x1 * 3.737f),
                (p1 - 0.5f) * 24.0f);
        eq_band(&c->e[2], c->m[2], BAND_HIGHSHELF, 2000.0f * c_exp2(x2 * 3.0f), 0.707f, (p2 - 0.5f) * 24.0f);
        c->q[0] = c_exp2((x3 - 0.5f) * 24.0f * 0.16610f);   /* the level */
        break;
    case FXT_TAPE: {                     /* g: the drive, a: its make-up, b: the top's roll-off, e[0]: the bump;
                                          * the wow's depth in samples in m[1][0] */
        float d = p0 * 24.0f;            /* 0 dB: barely bent (0.3 into the curve); 24 dB: deep into it. The make-up
                                          * keeps a peak of 0.9 at 0.9, so the drive packs the quiet parts in */
        c->g = 0.3f * c_exp2(d * 0.1661f);
        c->a = 0.9f / (soft(c->g * 0.9f + 0.12f) - soft(0.12f));
        c->b = brk_clampf(c_tan_pi((18000.0f * c_exp2(-p1 * 1.85f)) / (float)BRK_SR) * 2.0f, 0.05f, 1.0f);
        eq_band(&c->e[0], c->m[0], BAND_BELL, 90.0f, 1.2f, p1 * 4.0f);
        c->m[1][0] = p2 * 66.0f * 0.8f;  /* the wow's depth; the flutter's (both in samples), the wow's step, hiss */
        c->m[1][1] = x0 * 66.0f * 0.3f;
        c->m[1][2] = 0.1f * c_exp2(x1 * 4.907f) / (float)BRK_SR;
        c->q[0] = x2 * x2 * 0.02f;
        break;
    }
    case FXT_MBCOMP: {                   /* c: the low's split, e[0] the high's; per band m[b]: log2 of the threshold,
                                          * the slope (1 - 1/ratio), the level after (the make-up, its gain, the
                                          * output); each the band's amount of brk_mbc_t's threshold and ratio */
        const float amt[3] = {p0, p1, p2};
        const brk_mbc_t *m = (const brk_mbc_t *)&p_mbc[k];
        float out = c_exp2(brk_mb_db24(m->out) * 0.16610f);
        uint32_t b;
        c->c = svf_coef(brk_mb_xlo_hz(m->xlo), 0.0f);
        c->e[0] = svf_coef(brk_mb_xhi_hz(m->xhi), 0.0f);
        c->mb_w = brk_mb_knee_db(m->knee) * 0.16610f;   /* (dB to octaves of level: / 6.02) */
        c->mb_solo = (uint8_t)(m->solo <= MB_BANDS ? m->solo : 0u);
        for (b = 0; b < MB_BANDS; b++) {
            float th = brk_mb_thr_db(m->thr[b]) * 0.16610f * amt[b];
            float ratio = 1.0f + (brk_mb_ratio(m->ratio[b]) - 1.0f) * amt[b], slope = 1.0f - 1.0f / ratio;
            c->m[b][0] = th;
            c->m[b][1] = slope;
            /* half the reduction a full-scale band gets, the band's gain, the output */
            c->m[b][2] = c_exp2(0.5f * brk_mb_reduce(-th, slope, c->mb_w) +
                                   brk_mb_db24(m->gain[b]) * 0.16610f) * out;
            c->mb_att[b] = 1.0f / (1.0f + brk_mb_att_ms(m->att[b]) * 44.1f);
            c->mb_rel[b] = 1.0f / (1.0f + brk_mb_rel_ms(m->rel[b]) * 44.1f);
        }
        break;
    }
    case FXT_CLIP:                       /* g: the drive (over the ceiling), a: the ceiling, b: how hard */
        c->a = c_exp2(-p2 * 12.0f * 0.1661f);
        c->g = c_exp2(p0 * 24.0f * 0.1661f) / c->a;
        c->b = p1;
        c->q[0] = 0.5f * x0;             /* the bias; the output; the bias's own place on the curve (taken off) */
        c->q[1] = c_exp2((x1 - 1.0f) * 12.0f * 0.16610f);
        c->q[2] = brk_clip_curve(c->q[0], c->b);
        break;
    case FXT_TREMOLO: {                  /* a: the cycle's step (on the tempo), b: depth, g: pan */
        uint32_t d = (uint32_t)(p0 * (float)(BRK_TREM_DIVS - 1u) + 0.5f);
        c->a = 1.0f / (TREM_STEPS[d < BRK_TREM_DIVS ? d : 0u] * (float)BRK_SR * 60.0f / (p_bpm * 4.0f));
        c->b = p1;
        c->g = p2;
        c->qi = (uint8_t)brk_idx(x0, 4u);   /* the shape (SINE TRI SQUARE SAW), q[0] the smoothing's coefficient */
        c->q[0] = 1.0f / (1.0f + x1 * x1 * 400.0f);
        break;
    }
    default:
        break;
    }
}

BRK_INL void slot_run(uint32_t k, float *xl, float *xr)
{
    slot_st_t *s = st[k];
    const slot_c_t *c = &sc[k];
    float yl = *xl, yr = *xr, lp;
    uint32_t i;
    switch (c->type) {
    case FXT_FILTER: {                   /* (the drive into it; 24 dB: the same again) */
        uint32_t j, n2 = c->qi ? 2u : 1u;
        if (c->q[0] > 1.001f) {
            yl = soft(yl * c->q[0]) * c->q[1];
            yr = soft(yr * c->q[0]) * c->q[1];
        }
        for (j = 0; j < n2; j++) {
            svf_t *fs = j ? s->eq[0] : s->f;
            float il = yl, ir = yr;
            float hl = svf_run(&fs[0], &c->c, il, &lp), lpl = lp;
            float hr = svf_run(&fs[1], &c->e[0], ir, &lp), lpr = lp;
            if (c->mode == 0u) {
                yl = lpl;
                yr = lpr;
            } else if (c->mode == 2u) {
                yl = hl;
                yr = hr;
            } else {                     /* band-pass: what is neither */
                yl = il - hl - lpl;
                yr = ir - hr - lpr;
            }
        }
        break;
    }
    case FXT_CRUNCH:                     /* sample and hold at the lower rate, then fewer bits */
        s->hph += c->b;
        if (s->hph >= 1.0f) {
            float dz = c->q[1] > 0.0f ? c->q[1] * (brk_randf(&s->rng) - 0.5f) : 0.0f;   /* (the dither, in steps) */
            s->hph -= 1.0f;
            s->hold[0] = (float)(int32_t)(soft(yl * c->g) * c->a + dz) / c->a;
            s->hold[1] = (float)(int32_t)(soft(yr * c->g) * c->a + dz) / c->a;
        }
        yl = s->hold[0];
        yr = s->hold[1];
        if (c->q[0] < 1.0f) {            /* the tone: the steps' edges rounded */
            s->lp[0] += (yl - s->lp[0]) * c->q[0];
            s->lp[1] += (yr - s->lp[1]) * c->q[0];
            yl = s->lp[0];
            yr = s->lp[1];
        }
        break;
    case FXT_DRIVE:                      /* (the low cut,) gain into its curve, a tone filter, the level */
        if (c->q[1] > 0.0f) {
            s->hp[0] += (yl - s->hp[0]) * c->q[1];
            s->hp[1] += (yr - s->hp[1]) * c->q[1];
            yl -= s->hp[0];
            yr -= s->hp[1];
        }
        s->lp[0] += (brk_drive_curve(c->qi, yl * c->g, c->q[0]) - s->lp[0]) * c->b;
        s->lp[1] += (brk_drive_curve(c->qi, yr * c->g, c->q[0]) - s->lp[1]) * c->b;
        yl = s->lp[0] * c->a;
        yr = s->lp[1] * c->a;
        break;
    case FXT_PUNCH: {                    /* a peak compressor (its ratio above the threshold), made up to full scale;
                                          * the level it follows through the side-chain's high-pass */
        float pk, want, dl_ = yl, dr = yr;
        if (c->q[2] > 0.0f) {
            s->hp[0] += (0.5f * (yl + yr) - s->hp[0]) * c->q[2];
            dl_ = dr = 0.5f * (yl + yr) - s->hp[0];
        }
        pk = brk_absf(dl_) > brk_absf(dr) ? brk_absf(dl_) : brk_absf(dr);
        s->env += (pk - s->env) * (pk > s->env ? c->b : c->g);
        want = s->env > c->a ? (c->a + (s->env - c->a) * c->q[0]) / s->env : 1.0f;
        s->gr += (want - s->gr) * 0.02f;
        yl *= s->gr * c->q[1];
        yr *= s->gr * c->q[1];
        break;
    }
    case FXT_FLANGER: {                  /* 9 samples + the depth's sweep, the right a quarter turn behind */
        float dl = c->q[0] + c->b * (0.5f + 0.5f * brk_sin_turns(s->ph));
        float dr = c->q[0] + c->b * (0.5f + 0.5f * brk_sin_turns(s->ph + c->q[1]));
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
    case FXT_RING: {                     /* the carrier's wave; AM: lifted to 0..1 */
        float cl = c->qi & 3u ? brk_mod_wave((c->qi & 3u) == 1u ? MOD_TRI : MOD_SQUARE, s->ph) : brk_sin_turns(s->ph);
        float cr = c->qi & 3u ? brk_mod_wave((c->qi & 3u) == 1u ? MOD_TRI : MOD_SQUARE, s->ph2) : brk_sin_turns(s->ph2);
        if (c->qi & 4u) {
            cl = 0.5f + 0.5f * cl;
            cr = 0.5f + 0.5f * cr;
        }
        yl *= cl;
        yr *= cr;
        s->ph += c->a;
        s->ph2 += c->b;
        if (s->ph >= 1.0f)
            s->ph -= 1.0f;
        if (s->ph2 >= 1.0f)
            s->ph2 -= 1.0f;
        break;
    }
    case FXT_EQ:                         /* (no mix: the bands are the sound) */
        for (i = 0; i < 3u; i++) {
            yl = eq_run(&s->eq[i][0], &c->e[i], c->m[i], yl);
            yr = eq_run(&s->eq[i][1], &c->e[i], c->m[i], yr);
        }
        *xl = yl * c->q[0];
        *xr = yr * c->q[0];
        return;
    case FXT_TAPE: {                     /* the wow first (a short line read wandering), then the drive into a curve
                                          * leaning one way (even harmonics), the bump, the top rolled off */
        float d, rl, rr, bias = 0.12f;
        s->ph += c->m[1][2];             /* wow */
        if (s->ph >= 1.0f)
            s->ph -= 1.0f;
        s->ph2 += 6.3f / (float)BRK_SR;  /* flutter */
        if (s->ph2 >= 1.0f)
            s->ph2 -= 1.0f;
        d = 80.0f + c->m[1][0] * brk_sin_turns(s->ph) + c->m[1][1] * brk_sin_turns(s->ph2);
        s->fl[0][s->w] = yl;
        s->fl[1][s->w] = yr;
        rl = c->m[1][0] + c->m[1][1] > 0.5f ? fl_read(s->fl[0], s->w, d) : yl;
        rr = c->m[1][0] + c->m[1][1] > 0.5f ? fl_read(s->fl[1], s->w, d) : yr;
        s->w = (s->w + 1u) % FL_LEN;
        rl = (soft(rl * c->g + bias) - soft(bias)) * c->a;
        rr = (soft(rr * c->g + bias) - soft(bias)) * c->a;
        if (c->q[0] > 0.0f) {            /* hiss */
            rl += c->q[0] * (brk_randf(&s->rng) - 0.5f);
            rr += c->q[0] * (brk_randf(&s->rng) - 0.5f);
        }
        rl = eq_run(&s->eq[0][0], &c->e[0], c->m[0], rl);
        rr = eq_run(&s->eq[0][1], &c->e[0], c->m[0], rr);
        s->lp[0] += (rl - s->lp[0]) * c->b;
        s->lp[1] += (rr - s->lp[1]) * c->b;
        yl = s->lp[0];
        yr = s->lp[1];
        break;
    }
    case FXT_MBCOMP: {                   /* split (low and high by their filters, mid what is left), each band's level
                                          * followed (left and right together), pressed, made up, summed */
        float bl[3], br[3], hp, lpl, lpr;
        uint32_t b;
        svf_run(&s->f[0], &c->c, yl, &lpl);
        svf_run(&s->f[1], &c->c, yr, &lpr);
        bl[0] = lpl;
        br[0] = lpr;
        bl[2] = svf_run(&s->eq[0][0], &c->e[0], yl, &hp);
        br[2] = svf_run(&s->eq[0][1], &c->e[0], yr, &hp);
        bl[1] = yl - bl[0] - bl[2];
        br[1] = yr - br[0] - br[2];
        yl = yr = 0.0f;
        for (b = 0; b < 3u; b++) {
            float pk = brk_absf(bl[b]) > brk_absf(br[b]) ? brk_absf(bl[b]) : brk_absf(br[b]);
            s->mb_env[b] += (pk - s->mb_env[b]) * (pk > s->mb_env[b] ? c->mb_att[b] : c->mb_rel[b]);
            if (!(s->mb_n & 3u)) {       /* the gain: the reduction over the threshold through the knee, in octaves */
                float over = s->mb_env[b] > 1e-6f ? brk_log2f(s->mb_env[b]) - c->m[b][0] : -20.0f;
                float red = c->m[b][1] > 0.0f ? brk_mb_reduce(over, c->m[b][1], c->mb_w) : 0.0f;
                s->mb_g[b] = red > 0.0f ? brk_exp2f(-red) : 1.0f;
            }
            if (c->mb_solo && c->mb_solo != b + 1u)
                continue;                /* (soloed: that band alone) */
            yl += bl[b] * s->mb_g[b] * c->m[b][2];
            yr += br[b] * s->mb_g[b] * c->m[b][2];
        }
        s->mb_n++;
        if (c->mb_solo) {                /* a band alone: heard as it is, no dry under it */
            *xl = yl;
            *xr = yr;
            return;
        }
        break;
    }
    case FXT_CLIP: {                     /* a cubic's soft curve (flat at 1.5) and a hard edge at 1, blended by the
                                          * knee; both start at a slope of 1: below the edge the drive is the gain */
        float u[2];
        uint32_t j;
        u[0] = yl * c->g + c->q[0];
        u[1] = yr * c->g + c->q[0];
        for (j = 0; j < 2u; j++)         /* (the bias taken back off: no offset left) */
            u[j] = (brk_clip_curve(u[j], c->b) - c->q[2]) * c->a * c->q[1];
        yl = u[0];
        yr = u[1];
        break;
    }
    case FXT_DELAY: {                    /* in: what reaches it at the send, and the throws; out: the dry and the
                                          * echoes at full */
        float el, er, in = 0.5f * (yl + yr), m;
        dl_run(&dl_slot_c, in * c->mix + cur_throw, &el, &er);
        m = 0.5f * (el + er);            /* width: the ping-pong narrowed to the middle */
        el = m + (el - m) * dl_slot_c.width;
        er = m + (er - m) * dl_slot_c.width;
        if (dl_slot_c.duck > 0.0f) {     /* the echoes duck under what plays */
            float a = brk_absf(in), g;
            dl.duck += (a - dl.duck) * (a > dl.duck ? 0.01f : 0.0003f);
            g = 1.0f - dl_slot_c.duck * brk_clampf(dl.duck * 4.0f, 0.0f, 1.0f);
            el *= g;
            er *= g;
        }
        *xl = yl + el;
        *xr = yr + er;
        return;
    }
    case FXT_REVERB: {                   /* the plate at half the rate: a new pair of outputs every other sample */
        float in = 0.5f * (yl + yr) + cur_verb, ol, or_;
        if (rv.odd) {
            rv.yo_l = rv.y_l;
            rv.yo_r = rv.y_r;
            rv_tick(0.5f * (in + rv.in_prev), c->a, c->b, (uint32_t)c->g, &rv.y_l, &rv.y_r);
            ol = 0.5f * (rv.yo_l + rv.y_l);
            or_ = 0.5f * (rv.yo_r + rv.y_r);
        } else {
            ol = rv.y_l;
            or_ = rv.y_r;
        }
        rv.in_prev = in;
        rv.odd ^= 1u;
        if (rvp.width < 0.995f) {        /* width: the plate's sides toward the middle */
            float m = 0.5f * (ol + or_);
            ol = m + (ol - m) * rvp.width;
            or_ = m + (or_ - m) * rvp.width;
        }
        yl += ol;                        /* the dry sound and the plate: MIX is the plate's level */
        yr += or_;
        break;
    }
    case FXT_TREMOLO: {                  /* its shape on the tempo: both sides together, or across (pan), the edges
                                          * smoothed */
        static const uint8_t SH[4] = {MOD_SINE, MOD_TRI, MOD_SQUARE, MOD_SAW};
        float m = 0.5f + 0.5f * brk_mod_wave(SH[c->qi & 3u], s->ph), gl, gr;
        gl = 1.0f - c->b * m;
        gr = 1.0f - c->b * (c->g * (1.0f - m) + (1.0f - c->g) * m);
        s->lp[0] += (gl - s->lp[0]) * c->q[0];
        s->lp[1] += (gr - s->lp[1]) * c->q[0];
        yl *= s->lp[0];
        yr *= s->lp[1];
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


BRK_COLD void brk_fx_mb_meter(uint32_t slot, float *gr, float *lvl)
{
    uint32_t b, live = slot < BRK_FX_SLOTS && sc[slot].on && sc[slot].type == FXT_MBCOMP && st[slot]->type == FXT_MBCOMP;
    for (b = 0; b < MB_BANDS; b++) {     /* (single words, read from the main loop) */
        gr[b] = live ? st[slot]->mb_g[b] : 1.0f;
        lvl[b] = live ? st[slot]->mb_env[b] : 0.0f;
    }
}

/* the block's samples, all four slots: from RAM on the FM-1 (BRK_RAM, one call a block: nothing in it calls out) */
typedef struct { svf_c djc; dl_c_t dub_c; float wet; int dj_hp, dub_on; float pad_on[BRK_PAD_V]; uint8_t pad_send[BRK_PAD_V]; } fx_q_t;
BRK_RAM static void fx_samples(const fx_q_t *q, const float *in, const float *thru, const float *echo, const float *verb,
                               const float *post, float *l, float *r, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        float x = in[i], xl, xr, lp, ec = echo ? echo[i] : 0.0f, vb = verb ? verb[i] : 0.0f;
        if (dj_s < -0.01f || dj_s > 0.01f) {   /* the DJ filter: the break's alone (and its throws) */
            float h = svf_run(&dj_b, &q->djc, x, &lp);
            x = q->dj_hp ? h : lp;
            h = svf_run(&dj_e, &q->djc, ec, &lp);
            ec = q->dj_hp ? h : lp;
            h = svf_run(&dj_v, &q->djc, vb, &lp);
            vb = q->dj_hp ? h : lp;
        }
        if (thru)
            x += thru[i];
        xl = xr = x;
        dub_send += ((q->dub_on ? 1.0f : 0.0f) - dub_send) * 0.002f;
        cur_throw = x * dub_send + ec;
        cur_verb = vb;
        for (k = 0; k < BRK_FX_ALL; k++) {   /* the chain, slot by slot; then the pads' voices on the whole mix */
            float dl0 = 0.0f, dr0 = 0.0f, ul = 0.0f, ur = 0.0f, gw = 1.0f;
            uint32_t tr;
            if (k == BRK_FX_SLOTS) {
                if (q->wet < 0.999f) {      /* the MIXER's FX: the chain against the dry sound */
                    xl = x + (xl - x) * q->wet;
                    xr = x + (xr - x) * q->wet;
                }
                if (post) {                  /* the drums around the chain join here, clean */
                    xl += post[i];
                    xr += post[i];
                }
                if (dub_n && !dl_taken) {    /* the delay after the chain: the throws in, the echoes at the DUB pad's
                                              * mix (the black keys' effects after it: on the whole mix) */
                    float el, er;
                    dl_run(&q->dub_c, cur_throw, &el, &er);
                    xl += el * q->dub_c.ret;
                    xr += er * q->dub_c.ret;
                }
            }
            if (!sc[k].on)
                continue;
            if (k >= BRK_FX_SLOTS) {     /* a pad: its gate eased (~5 ms); REVERB and DELAY gate what goes in */
                uint32_t v = k - BRK_FX_SLOTS;
                float g = pad_g[v] += (q->pad_on[v] - pad_g[v]) * 0.005f;
                dl0 = xl;
                dr0 = xr;
                if (q->pad_send[v]) {
                    xl *= g;
                    xr *= g;
                } else
                    gw = g;
                ul = xl;
                ur = xr;
            }
            tr = md[k].track && !(i & 3u);   /* (its peaks: the screen's slot, every 4th sample) */
            if (tr) {
                float a = brk_absf(xl) > brk_absf(xr) ? brk_absf(xl) : brk_absf(xr);
                if (a > md[k].pk_in)
                    md[k].pk_in = a;
            }
            slot_run(k, &xl, &xr);
            if (k >= BRK_FX_SLOTS) {     /* the dry sound and what the pad adds (its wet, followed for its tail) */
                float wl = (xl - ul) * gw, wr = (xr - ur) * gw, a = brk_absf(wl) + brk_absf(wr);
                uint32_t v = k - BRK_FX_SLOTS;
                if (a > pad_pk[v])
                    pad_pk[v] = a;
                xl = dl0 + wl;
                xr = dr0 + wr;
            }
            if (tr) {
                float a = brk_absf(xl) > brk_absf(xr) ? brk_absf(xl) : brk_absf(xr);
                if (a > md[k].pk_out)
                    md[k].pk_out = a;
            }
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
/* (called through a pointer: the flash's code is out of a direct call's reach of the RAM's) */
static void (*volatile fx_samples_p)(const fx_q_t *, const float *, const float *, const float *, const float *,
                                     const float *, float *, float *, uint32_t) = fx_samples;


void brk_fx_process(const float *in, const float *thru, const float *echo, const float *verb, const float *post,
                    float *l, float *r, uint32_t n, uint32_t pads)
{
    uint32_t k, i;
    svf_c djc;
    float dj, dj_hz, wet;
    dl_c_t dub_c;
    int dj_hp, throw = (pads >> PAD_FILTER) & 1u, dub_on = (pads >> PAD_DUB) & 1u;
    uint32_t pads_on;
    fx = *(const brk_fx_t *)&p_fx;
    wet = brk_clampf(fx.wet, 0.0f, 1.0f);
    /* the FILTER pad throws the DJ filter to its target at its speed, with its resonance */
    dj = throw ? brk_clampf(fx.flt_target, -1.0f, 1.0f) : brk_clampf(fx.dj, -1.0f, 1.0f);
    dj_s += (dj - dj_s) * (throw ? 0.01f + 0.3f * brk_clampf(fx.flt_speed, 0.0f, 1.0f) : 0.3f);
    dj_hp = dj_s > 0.0f;
    /* -1 .. 0: low-pass 18 kHz down to 80 Hz; 0 .. 1: high-pass 20 Hz up to 6 kHz (exponential sweeps) */
    dj_hz = dj_hp ? 20.0f * brk_exp2f(dj_s * 8.2f) : 18000.0f * brk_exp2f(dj_s * 7.8f);
    djc = svf_coef(dj_hz, throw ? brk_clampf(fx.flt_res, 0.0f, 0.95f) : 0.35f);
    if (dub_n)                           /* the delay after the chain (no DELAY slot): the DUB pad's settings */
        dl_settings(&dub_c, DUB_DIV_DLY[fx.dub_div % BRK_DUB_DIVS], fx.dub_fb, fx.dub_tone,
                    brk_clampf(fx.dub_mix, 0.0f, 1.0f));
    rv_taken = dl_taken = 0;
    pads_on = p_pad_on;
    for (k = 0; k < BRK_FX_ALL; k++) {   /* the chain's settings, then the pads', once a block (each modulated first) */
        brk_fxx_t xx;
        brk_fxslot_t *f;
        if (k >= BRK_FX_SLOTS) {         /* a pad's voice: off and silent, nothing to do */
            uint32_t v = k - BRK_FX_SLOTS, on = pads_on >> v & 1u;
            pad_sl[v] = *(const brk_fxslot_t *)&p_pad[v];
            if (!st[k] || (!on && !(pad_busy >> v & 1u))) {
                sc[k].on = 0u;
                pad_g[v] = 0.0f;
                pad_busy &= ~(1u << v);
                continue;
            }
            if (!(pad_busy >> v & 1u))
                st[k]->type = 0xFFu;     /* (sounding again from silence: its state from nothing) */
            pad_busy |= 1u << v;
            pad_sl[v].on = 1u;
        }
        f = slot_of(k);
        fxx_get(k, f->type < BRK_NFXT ? f->type : FXT_EMPTY, &xx);
        mod_block(k, n, xx.x);
        slot_block(k, xx.x);
    }
    if (shed_hit && (!shed_lv || shed_n < SHED_BLOCKS - 32u)) {   /* over: one more slot off (not twice in 0.2 s) */
        if (shed_lv < BRK_FX_ALL)
            shed_lv++;
        shed_n = SHED_BLOCKS;
    } else if (shed_lv && !--shed_n) {   /* 2 s clear: one back */
        shed_lv--;
        shed_n = SHED_BLOCKS;
    }
    shed_hit = 0u;
    for (k = BRK_FX_ALL, i = shed_lv; k-- > 0u && i;)   /* the last ones on, bypassed (the pads first) */
        if (sc[k].on) {
            sc[k].on = 0u;
            i--;
        }
    {
        fx_q_t q;
        q.djc = djc;
        q.dub_c = dub_c;
        q.wet = wet;
        q.dj_hp = dj_hp;
        q.dub_on = dub_on;
        for (k = 0; k < BRK_PAD_V; k++) {
            q.pad_on[k] = (float)(pads_on >> k & 1u);
            q.pad_send[k] = (uint8_t)(sc[BRK_FX_SLOTS + k].type == FXT_REVERB || sc[BRK_FX_SLOTS + k].type == FXT_DELAY);
            pad_pk[k] = 0.0f;
        }
        fx_samples_p(&q, in, thru, echo, verb, post, l, r, n);
    }
    for (k = 0; k < BRK_PAD_V; k++)      /* a pad let go still sounds while it fades, or its tail rings */
        if (!(pads_on >> k & 1u) && pad_g[k] < 0.0003f && pad_pk[k] < 0.0003f)
            pad_busy &= ~(1u << k);
}
