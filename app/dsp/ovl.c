/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's overlay effects (ovl.h). */
#include "ovl.h"
#include "brk.h"
#include "brk_math.h"

const char *const OVL_NAME[OVL_N] = {"DIVE", "RISE", "RADIO", "WAH", "CRUSH", "LO-FI", "DIST", "RING",
                                     "CHOP", "GATE", "DUCK", "PAN", "BOOM", "WOBBLE", "JET", "CUT"};
const char *const OVL_RATE_NAME[4] = {"1/4", "1/8", "1/16", "1/32"};
const char *const OVL_SWEEP_NAME[4] = {"1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS"};

#define SR ((float)BRK_SR)
#define RAMP (1.0f / 220.0f)             /* on / off: 5 ms */

static volatile uint32_t p_on, p_rate, p_sweep;
static volatile uint8_t p_amt[OVL_N];
static float env[OVL_N];                 /* each one's fade, 0..1 */
static uint32_t held[OVL_N];             /* samples since it came on (the sweeps) */
static int16_t *line;
static uint32_t wpos;
typedef struct { float ic1, ic2; } svf_t;   /* a state-variable filter's state (TPT) */
static svf_t sv[4][2];                   /* DIVE, RISE, RADIO, WAH; left, right */
static float boom_lp[2], hold_v[2], chop_g = 1.0f, gate_g = 1.0f;
static uint32_t hold_n;
static float ring_ph, wob_ph, jet_ph;
static uint32_t sounding;

typedef struct { float a1, a2, a3, k; } svc_t;
static void svc(svc_t *c, float hz, float q)
{
    float g = brk_tan_pi(brk_clampf(hz, 20.0f, 18000.0f) / SR);
    c->k = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}
static inline void svf(svf_t *s, const svc_t *c, float x, float *lp, float *bp, float *hp)
{
    float v3 = x - s->ic2, v1 = c->a1 * s->ic1 + c->a2 * v3, v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    *lp = v2;
    *bp = v1;
    *hp = x - c->k * v1 - v2;
}
static inline float soft(float x) { return x / (1.0f + brk_absf(x)); }
static inline float frac(float x) { return x - (float)(int32_t)x; }

void ovl_init(int16_t *buf)
{
    uint32_t i;
    line = buf;
    for (i = 0; line && i < 2u * OVL_LINE; i++)
        line[i] = 0;
    for (i = 0; i < OVL_N; i++) {
        env[i] = 0.0f;
        held[i] = 0u;
        p_amt[i] = 100u;
    }
    p_on = 0u;
}

void ovl_set(uint32_t on, const uint8_t *amount, uint32_t rate, uint32_t sweep)
{
    uint32_t i;
    for (i = 0; i < OVL_N; i++)
        p_amt[i] = amount[i] > 100u ? 100u : amount[i];
    p_rate = rate & 3u;
    p_sweep = sweep & 3u;
    p_on = on;
}

uint32_t ovl_sounding(void) { return sounding; }

static float tap(uint32_t ch, float d)   /* the delay line, d samples back (linear between two) */
{
    uint32_t a = (uint32_t)d, at = (wpos + 2u * OVL_LINE - a) % OVL_LINE;
    float f = d - (float)a;
    float x0 = (float)line[ch * OVL_LINE + at], x1 = (float)line[ch * OVL_LINE + (at + OVL_LINE - 1u) % OVL_LINE];
    return (x0 + (x1 - x0) * f) * (1.0f / 16384.0f);
}

void ovl_process(float *l, float *r, uint32_t n, float beat, float spb)
{
    uint32_t on = p_on, i, k, act = 0u, rate = p_rate;
    float amt[OVL_N], div = (float)(1u << rate), sweep_n = (float)(1u << p_sweep) * spb, inv = spb > 1.0f ? 1.0f / spb : 0.0f;
    svc_t c_dive, c_rise, c_radio, c_wah;
    for (k = 0; k < OVL_N; k++) {
        amt[k] = (float)p_amt[k] * 0.01f;
        if ((on >> k) & 1u || env[k] > 0.0f)
            act |= 1u << k;
        if (!((on >> k) & 1u) && env[k] <= 0.0f)
            held[k] = 0u;
    }
    sounding = act;
    if (!act) {                          /* nothing on: the line still listens (a clean start for WOBBLE and JET) */
        for (i = 0; line && i < n; i++) {
            line[wpos] = (int16_t)(brk_clampf(l[i], -1.99f, 1.99f) * 16384.0f);
            line[OVL_LINE + wpos] = (int16_t)(brk_clampf(r[i], -1.99f, 1.99f) * 16384.0f);
            wpos = (wpos + 1u) % OVL_LINE;
        }
        return;
    }
    {                                    /* the filters' coefficients, for this block */
        float td = brk_clampf((float)held[OV_DIVE] / (sweep_n > 1.0f ? sweep_n : 1.0f), 0.0f, 1.0f);
        float tr = brk_clampf((float)held[OV_RISE] / (sweep_n > 1.0f ? sweep_n : 1.0f), 0.0f, 1.0f);
        float wah = 0.5f + 0.5f * brk_sin_turns(beat * div * 0.5f);
        svc(&c_dive, 18000.0f * brk_exp2f(-td * (2.0f + 5.5f * amt[OV_DIVE])), 0.8f + 0.9f * amt[OV_DIVE]);
        svc(&c_rise, 20.0f * brk_exp2f(tr * (3.0f + 5.5f * amt[OV_RISE])), 0.8f + 0.9f * amt[OV_RISE]);
        svc(&c_radio, 1300.0f, 1.2f + 3.0f * amt[OV_RADIO]);
        svc(&c_wah, 350.0f * brk_exp2f(wah * 2.8f), 2.0f + 4.0f * amt[OV_WAH]);
    }
    for (i = 0; i < n; i++) {
        float x[2], bp_ = beat + (float)i * inv, ph;
        x[0] = l[i];
        x[1] = r[i];
        if (line) {                      /* the line: what comes in (WOBBLE and JET read it) */
            line[wpos] = (int16_t)(brk_clampf(x[0], -1.99f, 1.99f) * 16384.0f);
            line[OVL_LINE + wpos] = (int16_t)(brk_clampf(x[1], -1.99f, 1.99f) * 16384.0f);
        }
        for (k = 0; k < OVL_N; k++) {
            float e, w;
            uint32_t c;
            if (!((act >> k) & 1u))
                continue;
            e = env[k] = brk_clampf(env[k] + ((on >> k) & 1u ? RAMP : -RAMP), 0.0f, 1.0f);
            held[k]++;
            w = e * amt[k];              /* how much of it */
            if (e <= 0.0f)
                continue;
            switch (k) {
            case OV_DIVE: case OV_RISE: case OV_RADIO: case OV_WAH:
                for (c = 0; c < 2u; c++) {
                    float lp, bp, hp, y;
                    const svc_t *cc = k == OV_DIVE ? &c_dive : k == OV_RISE ? &c_rise : k == OV_RADIO ? &c_radio : &c_wah;
                    svf(&sv[k][c], cc, x[c], &lp, &bp, &hp);
                    y = k == OV_DIVE ? lp : k == OV_RISE ? hp : k == OV_RADIO ? soft(bp * cc->k * 3.0f) : bp * cc->k * 1.4f;
                    x[c] += (y - x[c]) * (k <= OV_RISE ? e : w);   /* (the sweeps: AMOUNT is how far) */
                }
                break;
            case OV_CRUSH: {                 /* 16 bits down to 3 */
                float q = brk_exp2f(-(15.0f - 12.0f * amt[k]));
                for (c = 0; c < 2u; c++) {
                    float y = (float)(int32_t)(x[c] / q + (x[c] < 0.0f ? -0.5f : 0.5f)) * q;
                    x[c] += (y - x[c]) * e;
                }
                break;
            }
            case OV_LOFI:                    /* each sample held 2 .. 24 times */
                if (hold_n == 0u) {
                    hold_v[0] = x[0];
                    hold_v[1] = x[1];
                }
                hold_n = (hold_n + 1u) % (2u + (uint32_t)(22.0f * amt[k]));
                for (c = 0; c < 2u; c++)
                    x[c] += (hold_v[c] - x[c]) * e;
                break;
            case OV_DIST: {
                float g = 1.0f + 30.0f * amt[k], mk = 0.9f * (1.0f + g) / g;
                for (c = 0; c < 2u; c++)
                    x[c] += (soft(x[c] * g) * mk * 0.5f - x[c]) * e;
                break;
            }
            case OV_RING: {                  /* 90 .. 1200 Hz */
                float m = brk_sin_turns(ring_ph);
                ring_ph = frac(ring_ph + (90.0f * brk_exp2f(3.7f * amt[k])) / SR);
                for (c = 0; c < 2u; c++)
                    x[c] += (x[c] * m - x[c]) * e;
                break;
            }
            case OV_CHOP: case OV_GATE: {    /* the level cut on the beat's grid: on the first half of each */
                float d = k == OV_CHOP ? div : div * 2.0f, *g = k == OV_CHOP ? &chop_g : &gate_g;
                float want = frac(bp_ * d) < 0.5f ? 1.0f : 1.0f - amt[k];
                *g += (want - *g) * 0.05f;   /* (~0.5 ms: no clicks) */
                for (c = 0; c < 2u; c++)
                    x[c] += (x[c] * *g - x[c]) * e;
                break;
            }
            case OV_DUCK: {                  /* a pump: down on each beat, back up over it */
                float p = 1.0f - frac(bp_), g = 1.0f - amt[k] * p * p * p;
                for (c = 0; c < 2u; c++)
                    x[c] += (x[c] * g - x[c]) * e;
                break;
            }
            case OV_PAN: {                   /* left, right, a cycle every four of the rate */
                float p = w * brk_sin_turns(bp_ * div * 0.25f);
                x[0] *= 1.0f - p;
                x[1] *= 1.0f + p;
                break;
            }
            case OV_BOOM:                    /* the low end up: + its 110 Hz low-pass, soft-clipped */
                for (c = 0; c < 2u; c++) {
                    boom_lp[c] += (x[c] - boom_lp[c]) * 0.0156f;
                    x[c] += (soft((x[c] + boom_lp[c] * 3.0f * amt[k]) * 1.2f) - x[c]) * e;
                }
                break;
            case OV_WOBBLE:                  /* tape wow (0.8 Hz) and flutter (7 Hz) */
                ph = brk_sin_turns(wob_ph) + 0.25f * brk_sin_turns(wob_ph * 8.75f);
                wob_ph = frac(wob_ph + 0.8f / SR);
                if (line)
                    for (c = 0; c < 2u; c++)
                        x[c] += (tap(c, 340.0f + (40.0f + 220.0f * amt[k]) * ph) - x[c]) * e;
                break;
            case OV_JET:                     /* a flanger, sweeping once a bar */
                ph = 0.5f + 0.5f * brk_sin_turns(bp_ * 0.25f + jet_ph);
                if (line)
                    for (c = 0; c < 2u; c++)
                        x[c] += ((x[c] + tap(c, 8.0f + 200.0f * ph)) * 0.6f - x[c]) * w;
                break;
            default:                         /* CUT */
                for (c = 0; c < 2u; c++)
                    x[c] -= x[c] * w;
                break;
            }
        }
        if (line)
            wpos = (wpos + 1u) % OVL_LINE;
        l[i] = x[0];
        r[i] = x[1];
    }
}
