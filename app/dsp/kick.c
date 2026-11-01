/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's kick (kick.h). */
#include "kick.h"
#include "brk.h"
#include "brk_math.h"

const char *const KICK_LAYER_NAME[KICK_LAYERS] = {"SUB", "PUNCH", "GRIT", "CLICK"};
const char *const KICK_FX_NAME[KICK_FX] = {"OFF", "DRIVE", "CRUNCH", "FILTER", "RING MOD"};
const char *const KICK_PRESET_NAME[KICK_PRESETS] = {"909", "TECHNO", "EARLY HC", "ROTTERDAM", "INDUSTRIAL", "DISTORTO"};

static inline float soft(float x) { return x / (1.0f + brk_absf(x)); }

/* one kick sounding, its layers' state (the screen's shape renders its own) */
typedef struct {
    float ph[KICK_LAYERS], ph2, env[KICK_LAYERS], pe, pe2;
    float lp;                            /* CLICK: its noise's low-pass (what it keeps is above it) */
    float s1[KICK_LAYERS], s2[KICK_LAYERS], fph[KICK_LAYERS], fhold[KICK_LAYERS];   /* the layers' effects */
    uint32_t rng;
    float last, declick;
} kv_t;

/* the block's settings, mapped */
typedef struct {
    float f[KICK_LAYERS];                /* each layer's frequency step (cycles a sample) */
    float dec[KICK_LAYERS];              /* each envelope's factor a sample */
    float lvl[KICK_LAYERS];
    float sub_sat, punch_sweep, grit_index, grit_noise, click_noise;
    float click_k;                       /* the click's high-pass coefficient */
    float pe_dec, pe2_dec;               /* the pitch sweeps' factors */
    /* the layers' effects */
    uint8_t fx[KICK_LAYERS];
    float fa[KICK_LAYERS], fb[KICK_LAYERS], wet[KICK_LAYERS];
    float ga[KICK_LAYERS], gb[KICK_LAYERS], gc[KICK_LAYERS];   /* the effect's own coefficients */
    float g, hard, make, vol;            /* the sum: drive, how hard the clip, make-up, volume */
} kc_t;

static volatile kick_t p_k;
static volatile uint32_t p_hits;         /* a count: kick_hit */
static uint32_t hits_seen;
static kv_t kv;

/* the factor a sample that falls 40 dB (to where a kick is heard as gone) in ms milliseconds */
static float decay_factor(float ms) { return brk_exp2f(-6.644f / (ms * 0.001f * (float)BRK_SR)); }

/* the knobs' 0..1 as each layer's frequency and its decay (to -40 dB) */
static const float P_LO[KICK_LAYERS] = {30.0f, 50.0f, 40.0f, 1000.0f};
static const float P_OCT[KICK_LAYERS] = {1.585f, 2.0f, 3.32f, 3.585f};
static const float D_LO[KICK_LAYERS] = {60.0f, 20.0f, 20.0f, 1.0f};
static const float D_HI[KICK_LAYERS] = {1500.0f, 400.0f, 800.0f, 40.0f};
float kick_pitch_hz(uint32_t layer, float p)
{
    layer %= KICK_LAYERS;
    return P_LO[layer] * brk_exp2f(brk_clampf(p, 0.0f, 1.0f) * P_OCT[layer]);
}
float kick_decay_ms(uint32_t layer, float d)
{
    layer %= KICK_LAYERS;
    d = brk_clampf(d, 0.0f, 1.0f);
    return D_LO[layer] + (D_HI[layer] - D_LO[layer]) * d * d;
}

float kick_fx_hz(uint32_t fx, float a)
{
    a = brk_clampf(a, 0.0f, 1.0f);
    return fx == KFX_FILTER ? 60.0f * brk_exp2f(a * 7.6f) : 30.0f * brk_exp2f(a * 6.0f);   /* 60..11.6k; 30..1.9k */
}
uint32_t kick_fx_bits(float a) { return 15u - (uint32_t)(brk_clampf(a, 0.0f, 1.0f) * 12.0f + 0.5f); }   /* 15..3 */

static void kc_make(const kick_t *k, kc_t *c)
{
    uint32_t i;
    for (i = 0; i < KICK_LAYERS; i++) {
        const kick_layer_t *l = &k->l[i];
        float fa = brk_clampf(l->fa, 0.0f, 1.0f), fb = brk_clampf(l->fb, 0.0f, 1.0f);
        c->f[i] = kick_pitch_hz(i, l->pitch) / (float)BRK_SR;
        c->dec[i] = decay_factor(kick_decay_ms(i, l->decay));
        c->lvl[i] = brk_clampf(l->level, 0.0f, 1.0f);
        c->fx[i] = l->fx < KICK_FX ? l->fx : KFX_OFF;
        c->fa[i] = fa;
        c->fb[i] = fb;
        c->wet[i] = brk_clampf(l->wet, 0.0f, 1.0f);
        switch (c->fx[i]) {
        case KFX_DRIVE:                  /* ga: gain, gb: its tone's coefficient */
            c->ga[i] = 1.0f + 24.0f * fa * fa;
            c->gb[i] = 0.05f + 0.95f * fb * fb;
            break;
        case KFX_CRUNCH:                 /* ga: levels, gb: the rate's step */
            c->ga[i] = brk_exp2f((float)kick_fx_bits(fa) - 1.0f);
            c->gb[i] = brk_exp2f(-fb * 4.5f);
            break;
        case KFX_FILTER: {               /* a 2-pole low-pass: ga g, gb k (resonance), gc a1 */
            float g = brk_tan_pi(kick_fx_hz(KFX_FILTER, fa) / (float)BRK_SR);
            c->gb[i] = 2.0f - 1.9f * fb;
            c->gc[i] = 1.0f / (1.0f + g * (g + c->gb[i]));
            c->ga[i] = g;
            break;
        }
        case KFX_RING:
            c->ga[i] = kick_fx_hz(KFX_RING, fa) / (float)BRK_SR;
            break;
        default:
            break;
        }
    }
    c->sub_sat = 1.0f + 4.0f * brk_clampf(k->l[KL_SUB].tone, 0.0f, 1.0f);
    c->punch_sweep = 3.0f + 14.0f * brk_clampf(k->l[KL_PUNCH].tone, 0.0f, 1.0f);
    c->grit_index = 0.5f + 7.0f * brk_clampf(k->l[KL_GRIT].tone, 0.0f, 1.0f);
    c->grit_noise = 0.5f * brk_clampf(k->l[KL_GRIT].tone, 0.0f, 1.0f);
    c->click_noise = brk_clampf(k->l[KL_CLICK].tone, 0.0f, 1.0f);
    c->click_k = brk_clampf(brk_tan_pi(c->f[KL_CLICK]) * 2.0f, 0.02f, 0.98f);
    c->pe_dec = decay_factor(120.0f);     /* SUB's drop and GRIT's */
    c->pe2_dec = decay_factor(40.0f);     /* PUNCH's fast sweep */
    {
        float in = brk_clampf(k->intensity, 0.0f, 1.0f), sg;
        c->g = 0.3f + 30.0f * in * in;   /* (0: near clean, the clip only rounding the peaks) */
        c->hard = in > 0.5f ? (in - 0.5f) * 1.6f : 0.0f;
        sg = c->g / (1.0f + c->g);       /* a full-scale sum through the clip: kept there */
        c->make = 0.95f / (sg * (1.0f - c->hard) + c->hard);
    }
    c->vol = brk_clampf(k->volume, 0.0f, 1.0f);
}

static void kv_hit(kv_t *v)
{
    uint32_t i;
    v->declick = v->last;                /* the kick still sounding fades out under the new one */
    for (i = 0; i < KICK_LAYERS; i++) {
        v->ph[i] = 0.0f;
        v->env[i] = 1.0f;
        v->s1[i] = v->s2[i] = v->fph[i] = v->fhold[i] = 0.0f;
    }
    v->ph2 = 0.0f;
    v->pe = v->pe2 = 1.0f;
    v->lp = 0.0f;
}

static inline float noise(kv_t *v)
{
    v->rng = v->rng * 1664525u + 1013904223u;
    return (float)(int32_t)v->rng * (1.0f / 2147483648.0f);
}

/* a layer's own effect (on x, -1..1) */
static inline float layer_fx(kv_t *v, const kc_t *c, uint32_t i, float x)
{
    float y = x;
    switch (c->fx[i]) {
    case KFX_DRIVE:
        v->s1[i] += (soft(x * c->ga[i]) - v->s1[i]) * c->gb[i];
        y = v->s1[i];
        break;
    case KFX_CRUNCH:
        v->fph[i] += c->gb[i];
        if (v->fph[i] >= 1.0f) {
            v->fph[i] -= 1.0f;
            v->fhold[i] = (float)(int32_t)(x * c->ga[i]) / c->ga[i];
        }
        y = v->fhold[i];
        break;
    case KFX_FILTER: {                   /* (Simper's state-variable low-pass) */
        float g = c->ga[i], a1 = c->gc[i], a2 = g * a1, a3 = g * a2;
        float v3 = x - v->s2[i], v1 = a1 * v->s1[i] + a2 * v3, v2 = v->s2[i] + a2 * v->s1[i] + a3 * v3;
        v->s1[i] = 2.0f * v1 - v->s1[i];
        v->s2[i] = 2.0f * v2 - v->s2[i];
        y = v2;
        break;
    }
    case KFX_RING:
        y = x * brk_sin_turns(v->fph[i]);
        v->fph[i] += c->ga[i];
        if (v->fph[i] >= 1.0f)
            v->fph[i] -= 1.0f;
        break;
    default:
        return x;
    }
    return x + (y - x) * c->wet[i];
}

static inline float kv_tick(kv_t *v, const kc_t *c)
{
    float x[KICK_LAYERS], y = 0.0f, d, out;
    uint32_t i;
    /* SUB: a sine dropping an octave and a half to its pitch, saturated by its tone */
    x[KL_SUB] = soft(brk_sin_turns(v->ph[KL_SUB]) * c->sub_sat) * (1.0f + c->sub_sat) / c->sub_sat;
    v->ph[KL_SUB] += c->f[KL_SUB] * (1.0f + 2.0f * v->pe);
    /* PUNCH: a sine swept down fast from far above */
    x[KL_PUNCH] = brk_sin_turns(v->ph[KL_PUNCH]);
    v->ph[KL_PUNCH] += c->f[KL_PUNCH] * (1.0f + c->punch_sweep * v->pe2);
    /* GRIT: FM (the modulator at 1.41x), driven, noise by its tone */
    x[KL_GRIT] = soft(4.0f * (brk_sin_turns(v->ph[KL_GRIT] + c->grit_index * 0.159f * brk_sin_turns(v->ph2)) +
                              c->grit_noise * noise(v)));
    v->ph[KL_GRIT] += c->f[KL_GRIT] * (1.0f + v->pe);
    v->ph2 += c->f[KL_GRIT] * 1.41f * (1.0f + v->pe);
    /* CLICK: noise high-passed at its pitch, and a click ringing there; tone between them */
    {
        float n = noise(v), ring = brk_sin_turns(v->ph[KL_CLICK]);
        v->lp += (n - v->lp) * c->click_k;
        x[KL_CLICK] = (n - v->lp) * c->click_noise + ring * (1.0f - c->click_noise);
        v->ph[KL_CLICK] += c->f[KL_CLICK];
    }
    for (i = 0; i < KICK_LAYERS; i++) {
        if (v->ph[i] >= 1.0f)
            v->ph[i] -= (float)(uint32_t)v->ph[i];
        y += layer_fx(v, c, i, x[i] * v->env[i]) * c->lvl[i];
        v->env[i] *= c->dec[i];
    }
    if (v->ph2 >= 1.0f)
        v->ph2 -= (float)(uint32_t)v->ph2;
    v->pe *= c->pe_dec;
    v->pe2 *= c->pe2_dec;
    /* the sum: a drive into a clip, soft and then (INTENSITY over half) hard: the distorted kick */
    d = y * c->g;
    out = (soft(d) * (1.0f - c->hard) + brk_clampf(d, -1.0f, 1.0f) * c->hard) * c->make * c->vol;
    v->last = out;
    out += v->declick;
    v->declick *= 0.985f;
    return out;
}

void kick_init(void)
{
    uint32_t i;
    for (i = 0; i < KICK_LAYERS; i++)
        kv.env[i] = 0.0f;
    kv.declick = kv.last = 0.0f;
    kv.rng = 0x5eed1u;
    hits_seen = p_hits;
}

void kick_set(const kick_t *k) { *(kick_t *)&p_k = *k; }
void kick_hit(void) { p_hits++; }

void kick_render(float *out, uint32_t n, const uint16_t *at, uint32_t nat)
{
    kick_t k = *(const kick_t *)&p_k;
    kc_t c;
    uint32_t i, j = 0;
    if (p_hits != hits_seen) {           /* a MIDI note: now */
        hits_seen = p_hits;
        kv_hit(&kv);
    } else if (!nat && kv.env[KL_SUB] < 3e-5f && kv.env[KL_PUNCH] < 3e-5f && kv.env[KL_GRIT] < 3e-5f &&
               kv.env[KL_CLICK] < 3e-5f && brk_absf(kv.declick) < 1e-5f) {
        for (i = 0; i < n; i++)          /* gone quiet, nothing coming: no work */
            out[i] = 0.0f;
        kv.last = 0.0f;
        return;
    }
    kc_make(&k, &c);
    for (i = 0; i < n; i++) {
        while (j < nat && at[j] <= i) {  /* the steps' kicks, on their samples */
            kv_hit(&kv);
            j++;
        }
        out[i] = kv_tick(&kv, &c);
    }
}

void kick_shape(const kick_t *k, uint8_t *px, uint32_t n, uint32_t ms, uint32_t height)
{
    kc_t c;
    kv_t v;
    uint32_t i, total = ms * (BRK_SR / 1000u), per = total / (n ? n : 1u);
    kc_make(k, &c);
    v.rng = 0x5eed1u;
    v.last = v.declick = 0.0f;
    kv_hit(&v);
    v.declick = 0.0f;
    for (i = 0; i < n; i++) {
        uint32_t s;
        float pk = 0.0f;
        for (s = 0; s < per; s++) {
            float a = brk_absf(kv_tick(&v, &c));
            if (a > pk)
                pk = a;
        }
        px[i] = (uint8_t)(brk_clampf(pk, 0.0f, 1.0f) * (float)height + 0.5f);
    }
}

/* the built-in kicks: per layer level, pitch, decay, tone; then intensity */
static const float PRESET[KICK_PRESETS][KICK_LAYERS * 4 + 1] = {
    {0.80f, 0.35f, 0.45f, 0.10f, 0.70f, 0.50f, 0.30f, 0.40f, 0.00f, 0.30f, 0.20f, 0.30f, 0.35f, 0.50f, 0.25f, 0.30f,
     0.00f},                             /* 909: clean, round */
    {0.90f, 0.25f, 0.55f, 0.20f, 0.60f, 0.40f, 0.25f, 0.30f, 0.10f, 0.25f, 0.20f, 0.20f, 0.30f, 0.60f, 0.20f, 0.50f,
     0.15f},                             /* TECHNO: deep, a touch of grit */
    {0.75f, 0.45f, 0.40f, 0.40f, 0.80f, 0.55f, 0.35f, 0.60f, 0.35f, 0.35f, 0.30f, 0.50f, 0.40f, 0.55f, 0.20f, 0.40f,
     0.45f},                             /* EARLY HC: punchy, driven */
    {0.90f, 0.30f, 0.60f, 0.60f, 0.70f, 0.45f, 0.45f, 0.70f, 0.55f, 0.30f, 0.55f, 0.70f, 0.35f, 0.60f, 0.20f, 0.50f,
     0.85f},                             /* ROTTERDAM: the long distorted tail */
    {0.70f, 0.30f, 0.50f, 0.50f, 0.60f, 0.60f, 0.30f, 0.50f, 0.80f, 0.55f, 0.45f, 0.90f, 0.50f, 0.70f, 0.30f, 0.80f,
     0.70f},                             /* INDUSTRIAL: metal and noise */
    {0.85f, 0.35f, 0.55f, 0.80f, 0.85f, 0.50f, 0.40f, 0.80f, 0.70f, 0.40f, 0.50f, 0.80f, 0.40f, 0.55f, 0.25f, 0.45f,
     1.00f},                             /* DISTORTO: everything into the clip */
};

void kick_preset(kick_t *k, uint32_t i)
{
    const float *p = PRESET[i % KICK_PRESETS];
    uint32_t l;
    for (l = 0; l < KICK_LAYERS; l++) {
        k->l[l].level = p[l * 4u];
        k->l[l].pitch = p[l * 4u + 1u];
        k->l[l].decay = p[l * 4u + 2u];
        k->l[l].tone = p[l * 4u + 3u];
        k->l[l].fx = KFX_OFF;
        k->l[l].rsv[0] = k->l[l].rsv[1] = k->l[l].rsv[2] = 0u;
        k->l[l].fa = 0.5f;
        k->l[l].fb = 0.5f;
        k->l[l].wet = 1.0f;
    }
    k->intensity = p[KICK_LAYERS * 4u];
}

void kick_defaults(kick_t *k)
{
    kick_preset(k, 3u);                  /* ROTTERDAM: CHOPPAH is for the hard stuff */
    k->volume = 0.8f;
    k->steps = 0x1111u;                  /* four to the floor */
    k->chain = 1u;
    k->rsv = 0u;
}
