/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's snare and hi-hat (perc.h). */
#include "perc.h"
#include "brk.h"
#include "brk_math.h"

const char *const PERC_VOICE_NAME[PERC_VOICES] = {"SNARE", "HAT"};
const char *const PERC_PRESET_NAME[PERC_VOICES][PERC_PRESETS] = {{"909", "TIGHT", "FAT", "GABBER"},
                                                                 {"CLOSED", "808", "OPEN", "TRASH"}};

static inline float soft(float x) { return x / (1.0f + brk_absf(x)); }
static float decay_factor(float ms) { return brk_exp2f(-6.644f / (ms * 0.001f * (float)BRK_SR)); }   /* -40 dB */

static float onepole(float hz) { return 1.0f - brk_exp2f(-9.0647f * hz / (float)BRK_SR); }   /* 1 - e^(-2 pi f / fs) */

static const float HAT_RATIO[6] = {1.0f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f};   /* 808: 205.3 .. 800 Hz */

float perc_tune_hz(uint32_t voice, float t)
{
    t = brk_clampf(t, 0.0f, 1.0f);
    return voice == PV_SNARE ? 120.0f * brk_exp2f(t * 1.737f) : 100.0f * brk_exp2f(t * 3.0f);
}
float perc_decay_ms(uint32_t voice, float d)
{
    d = brk_clampf(d, 0.0f, 1.0f);
    return voice == PV_SNARE ? 60.0f + 640.0f * d * d : 15.0f + 885.0f * d * d;
}

typedef struct {                         /* a voice sounding */
    float env, env2, pe, ph[6], hp1, hp2, lp, last, declick;
} pv_t;
typedef struct {                         /* a voice's settings, mapped for the block */
    float f, dec, dec2, lvl, hpk, lpk, body, noise, crack, g, make;
} pc_t;

static volatile perc_t p_p;
static volatile uint32_t p_hits[PERC_VOICES];
static uint32_t hits_seen[PERC_VOICES];
static pv_t pv[PERC_VOICES];
static uint32_t rng = 0x2545F491u;

static inline float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

static void pc_make(uint32_t voice, const perc_voice_t *s, float volume, pc_t *c)
{
    float snap = brk_clampf(s->snap, 0.0f, 1.0f), tone = brk_clampf(s->tone, 0.0f, 1.0f);
    float dr = brk_clampf(s->drive, 0.0f, 1.0f);
    float ms = perc_decay_ms(voice, s->decay);
    c->f = perc_tune_hz(voice, s->tune) / (float)BRK_SR;
    c->lvl = brk_clampf(s->level, 0.0f, 1.0f) * brk_clampf(volume, 0.0f, 1.0f);
    c->g = 1.0f + 15.0f * dr * dr;       /* the drive, made back up to a full-scale peak */
    c->make = (1.0f + c->g) / c->g;
    if (voice == PV_SNARE) {
        c->dec = decay_factor(ms * 0.55f);           /* the body: shorter than the noise */
        c->dec2 = decay_factor(ms);
        c->hpk = onepole(700.0f * brk_exp2f(tone * 2.2f));
        c->lpk = onepole(3500.0f * brk_exp2f(tone * 1.5f));
        c->body = 1.0f - 0.6f * snap;
        c->noise = 0.35f + 0.85f * snap;
        c->crack = snap;
    } else {
        c->dec = c->dec2 = decay_factor(ms);
        c->hpk = onepole(3000.0f * brk_exp2f(snap * 1.74f));
        c->lpk = 0.0f;
        c->body = 1.0f - tone;           /* metal against noise */
        c->noise = tone;
        c->crack = 0.0f;
    }
}

static void pv_hit(pv_t *v)
{
    uint32_t k;
    v->declick = v->last;                /* what still sounds fades out under the new hit */
    v->env = v->env2 = v->pe = 1.0f;
    for (k = 0; k < 6u; k++)
        v->ph[k] = 0.0f;
}

static inline float snare_tick(pv_t *v, const pc_t *c)
{
    float b, n = noise(), x;
    b = 0.6f * brk_sin_turns(v->ph[0]) + 0.4f * brk_sin_turns(v->ph[1]);
    v->ph[0] += c->f * (1.0f + 0.7f * v->pe);
    v->ph[1] += c->f * 1.62f * (1.0f + 0.7f * v->pe);
    if (v->ph[0] >= 1.0f)
        v->ph[0] -= 1.0f;
    if (v->ph[1] >= 1.0f)
        v->ph[1] -= 1.0f;
    v->pe *= 0.9985f;                    /* (the body's drop: ~15 ms) */
    v->hp1 += (n - v->hp1) * c->hpk;     /* noise: a high-pass then a low-pass, a band */
    v->lp += ((n - v->hp1) - v->lp) * c->lpk;
    x = b * v->env * c->body + v->lp * v->env2 * (c->noise + c->crack * 2.0f * v->pe * v->pe);
    v->env *= c->dec;
    v->env2 *= c->dec2;
    return x;
}

static inline float hat_tick(pv_t *v, const pc_t *c)
{
    float m = 0.0f, x, h;
    uint32_t k;
    for (k = 0; k < 6u; k++) {           /* six squares */
        v->ph[k] += c->f * HAT_RATIO[k] * 2.0f;
        if (v->ph[k] >= 1.0f)
            v->ph[k] -= 1.0f;
        m += v->ph[k] < 0.5f ? 1.0f : -1.0f;
    }
    x = m * (1.0f / 6.0f) * c->body + noise() * c->noise;
    v->hp1 += (x - v->hp1) * c->hpk;     /* two high-passes at CUT */
    h = x - v->hp1;
    v->hp2 += (h - v->hp2) * c->hpk;
    x = (h - v->hp2) * 2.5f * v->env;
    v->env *= c->dec;
    return x;
}

void perc_init(void)
{
    uint32_t k;
    for (k = 0; k < PERC_VOICES; k++) {
        pv[k].env = pv[k].env2 = 0.0f;
        pv[k].last = pv[k].declick = 0.0f;
        hits_seen[k] = p_hits[k];
    }
}
void perc_set(const perc_t *p) { *(perc_t *)&p_p = *p; }
void perc_hit(uint32_t voice) { p_hits[voice % PERC_VOICES]++; }

void perc_render(float *out, uint32_t n, const uint16_t *at, const uint8_t *who, uint32_t nat)
{
    perc_t s = *(const perc_t *)&p_p;
    pc_t c[PERC_VOICES];
    uint32_t i, j, k, hit = nat ? 1u : 0u;
    for (k = 0; k < PERC_VOICES; k++)
        if (p_hits[k] != hits_seen[k]) {     /* a MIDI note: now */
            hits_seen[k] = p_hits[k];
            pv_hit(&pv[k]);
            hit = 1u;
        }
    if (!hit && pv[0].env < 3e-5f && pv[0].env2 < 3e-5f && pv[1].env < 3e-5f &&
        brk_absf(pv[0].declick) + brk_absf(pv[1].declick) < 1e-5f) {
        for (i = 0; i < n; i++)          /* quiet, nothing coming: no work */
            out[i] = 0.0f;
        pv[0].last = pv[1].last = 0.0f;
        return;
    }
    for (k = 0; k < PERC_VOICES; k++)
        pc_make(k, &s.v[k], s.volume, &c[k]);
    for (i = 0, j = 0; i < n; i++) {
        float y = 0.0f;
        while (j < nat && at[j] <= i) {
            for (k = 0; k < PERC_VOICES; k++)
                if ((who[j] >> k) & 1u)
                    pv_hit(&pv[k]);
            j++;
        }
        for (k = 0; k < PERC_VOICES; k++) {
            pv_t *v = &pv[k];
            float x = k == PV_SNARE ? snare_tick(v, &c[k]) : hat_tick(v, &c[k]);
            x = soft(x * c[k].g) * c[k].make * c[k].lvl;
            v->last = x;
            y += x + v->declick;
            v->declick *= 0.985f;
        }
        out[i] = y;
    }
}

void perc_shape(uint32_t voice, const perc_voice_t *s, uint8_t *px, uint32_t n, uint32_t ms, uint32_t height)
{
    pc_t c;
    pv_t v;
    uint32_t i, k, per = ms * (BRK_SR / 1000u) / (n ? n : 1u);
    voice %= PERC_VOICES;
    pc_make(voice, s, 1.0f, &c);
    for (k = 0; k < 6u; k++)
        v.ph[k] = 0.0f;
    v.hp1 = v.hp2 = v.lp = v.last = v.declick = 0.0f;
    pv_hit(&v);
    v.declick = 0.0f;
    for (i = 0; i < n; i++) {
        uint32_t j;
        float pk = 0.0f;
        for (j = 0; j < per; j++) {
            float x = voice == PV_SNARE ? snare_tick(&v, &c) : hat_tick(&v, &c);
            x = brk_absf(soft(x * c.g) * c.make * c.lvl);
            if (x > pk)
                pk = x;
        }
        px[i] = (uint8_t)(brk_clampf(pk, 0.0f, 1.0f) * (float)height + 0.5f);
    }
}

/* the built-in sounds: level, tune, decay, tone, snap (the hat's cut), drive */
static const float PRESET[PERC_VOICES][PERC_PRESETS][6] = {
    {{0.80f, 0.45f, 0.40f, 0.50f, 0.55f, 0.10f},    /* 909 */
     {0.75f, 0.65f, 0.20f, 0.70f, 0.70f, 0.20f},    /* TIGHT */
     {0.85f, 0.25f, 0.65f, 0.35f, 0.45f, 0.30f},    /* FAT */
     {0.85f, 0.55f, 0.45f, 0.60f, 0.75f, 0.85f}},   /* GABBER */
    {{0.60f, 0.55f, 0.10f, 0.55f, 0.55f, 0.00f},    /* CLOSED */
     {0.60f, 0.35f, 0.18f, 0.20f, 0.45f, 0.10f},    /* 808 */
     {0.55f, 0.50f, 0.75f, 0.50f, 0.50f, 0.05f},    /* OPEN */
     {0.65f, 0.80f, 0.15f, 0.85f, 0.70f, 0.80f}},   /* TRASH */
};

void perc_preset(perc_voice_t *v, uint32_t voice, uint32_t i)
{
    const float *p = PRESET[voice % PERC_VOICES][i % PERC_PRESETS];
    v->level = p[0];
    v->tune = p[1];
    v->decay = p[2];
    v->tone = p[3];
    v->snap = p[4];
    v->drive = p[5];
}

void perc_defaults(perc_t *p)
{
    uint32_t k;
    for (k = 0; k < PERC_VOICES; k++)
        perc_preset(&p->v[k], k, 0u);
    p->volume = 0.8f;
}
