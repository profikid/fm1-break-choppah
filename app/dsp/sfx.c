/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's soundboard (sfx.h): each sound a few oscillators, noise and filters, shaped in time. */
#include "sfx.h"
#include "brk.h"
#include "brk_math.h"

#define NV 4u                            /* voices */
#define SR ((float)BRK_SR)
#define QLEN 16u

const char *const SFX_NAME[SFX_N] = {"HORN", "SIREN", "WHOOP", "LASER", "ZAP", "BOOM", "BRAP", "FYAH",
                                     "HEY", "RISER", "REVCYM", "SUBDRP", "STAB", "SKANK", "SCRTCH", "REWIND"};

typedef struct { float ic1, ic2, a1, a2, a3, k; } bp_t;   /* a TPT band-pass */

typedef struct {
    int32_t id;                          /* -1 free */
    uint32_t n;                          /* samples since the start */
    uint32_t dur;                        /* samples, 0 = while held */
    uint32_t seed;
    float ph[4];
    float lp, lp2, env, rel;             /* filters, the envelope, the release after a key up */
    uint8_t held;
    bp_t bp[3];
} voice_t;

static voice_t vo[NV];
static volatile sfx_set_t p_set = {0.8f, 0.0f, 0.3f, 1.0f};
static sfx_set_t cur;
typedef struct { int8_t id; uint8_t down; } ev_t;
static ev_t q[QLEN];
static volatile uint32_t q_w, q_r;
static volatile uint32_t playing_mask;
static volatile float prog[SFX_N];

static void bp_set(bp_t *b, float hz, float q)
{
    float g = brk_tan_pi(brk_clampf(hz, 30.0f, 16000.0f) / SR);
    b->k = 1.0f / q;
    b->a1 = 1.0f / (1.0f + g * (g + b->k));
    b->a2 = g * b->a1;
    b->a3 = g * b->a2;
}
static inline float bp_run(bp_t *b, float x)
{
    float v3 = x - b->ic2, v1 = b->a1 * b->ic1 + b->a2 * v3, v2 = b->ic2 + b->a2 * b->ic1 + b->a3 * v3;
    b->ic1 = 2.0f * v1 - b->ic1;
    b->ic2 = 2.0f * v2 - b->ic2;
    return v1;
}
static inline float saw(float *ph, float hz)
{
    float y = *ph * 2.0f - 1.0f;
    *ph += hz / SR;
    if (*ph >= 1.0f)
        *ph -= 1.0f;
    return y;
}
static inline float sine(float *ph, float hz)
{
    float y = brk_sin_turns(*ph);
    *ph += hz / SR;
    if (*ph >= 1.0f)
        *ph -= 1.0f;
    return y;
}
static inline float noise(uint32_t *s) { return brk_randf(s) * 2.0f - 1.0f; }
static inline float soft(float x) { return x / (1.0f + brk_absf(x)); }
static inline float knee(float x)        /* clean below 0.7, then a soft knee that never passes 1 */
{
    float a = brk_absf(x);
    if (a <= 0.7f)
        return x;
    a = 0.7f + 0.3f * soft((a - 0.7f) * 3.3f);
    return x < 0.0f ? -a : a;
}
static inline float exp_decay(float t, float tau) { return brk_exp2f(-1.4427f * t / tau); }   /* e^(-t/tau) */
static inline float tri(float x) { x -= (float)(int32_t)x; return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x; }

/* how long each one plays at length 1, in seconds; 0: while held (with a ceiling) */
static const float DUR[SFX_N] = {0.95f, 0.0f, 0.7f, 0.35f, 0.26f, 1.6f, 0.4f, 0.95f,
                                 0.55f, 0.0f, 1.6f, 1.5f, 0.4f, 0.16f, 0.45f, 0.7f};
#define HELD_MAX 4.0f                    /* a held sound stops by itself after this many seconds */

static void start(int id)
{
    uint32_t i, best = 0u;
    voice_t *v;
    for (i = 0; i < NV; i++)             /* the same sound restarts; else a free voice; else the oldest */
        if (vo[i].id == id) {
            best = i;
            goto found;
        }
    for (i = 0; i < NV; i++)
        if (vo[i].id < 0) {
            best = i;
            goto found;
        }
    for (i = 1; i < NV; i++)
        if (vo[i].n > vo[best].n)
            best = i;
found:
    v = &vo[best];
    v->id = id;
    v->n = 0u;
    v->dur = DUR[id] > 0.0f ? (uint32_t)(DUR[id] * cur.length * SR) : 0u;
    v->seed = 0x51EDu + (uint32_t)id * 977u;
    v->ph[0] = v->ph[1] = v->ph[2] = v->ph[3] = 0.0f;
    v->lp = v->lp2 = v->env = 0.0f;
    v->rel = 1.0f;
    v->held = 1u;
    for (i = 0; i < 3u; i++)
        v->bp[i].ic1 = v->bp[i].ic2 = 0.0f;
    if (id == SFX_FYAH) {                /* the vowels' formants: "ah", then "eh" for HEY */
        bp_set(&v->bp[0], 730.0f, 6.0f);
        bp_set(&v->bp[1], 1090.0f, 8.0f);
        bp_set(&v->bp[2], 2440.0f, 10.0f);
    } else if (id == SFX_HEY) {
        bp_set(&v->bp[0], 530.0f, 6.0f);
        bp_set(&v->bp[1], 1840.0f, 9.0f);
        bp_set(&v->bp[2], 2480.0f, 10.0f);
    }
}

/* one sample of voice v; t its time in seconds, x how far through (0..1, held sounds by HELD_MAX) */
static float tick(voice_t *v, float P)
{
    float t = (float)v->n / SR, L = cur.length, y = 0.0f;
    float x = v->dur ? (float)v->n / (float)v->dur : t / HELD_MAX;
    switch (v->id) {
    case SFX_HORN: {                     /* air horn: three detuned saws, BAAP BAAP BAAAAAP */
        float u = t / L, g = (u < 0.11f) || (u > 0.16f && u < 0.27f) || (u > 0.32f && u < 0.93f) ? 1.0f : 0.0f;
        float f = 415.0f * P;
        v->env += (g - v->env) * 0.006f;
        y = saw(&v->ph[0], f) + saw(&v->ph[1], f * 1.006f) + saw(&v->ph[2], f * 0.994f) +
            0.5f * saw(&v->ph[3], f * 0.5f);
        v->lp += (y - v->lp) * 0.35f;
        y = soft(v->lp * 0.9f) * v->env;
        break;
    }
    case SFX_SIREN: {                    /* dub siren: a square wailing upwards, again and again, while held */
        float r = t * 3.5f, f;
        r -= (float)(int32_t)r;
        f = 280.0f * P * brk_exp2f(1.3f * r * r);
        y = v->ph[0] < 0.5f ? 0.6f : -0.6f;
        saw(&v->ph[0], f);
        v->lp += (y - v->lp) * 0.2f;
        v->env += (1.0f - v->env) * 0.003f;
        y = v->lp * v->env;
        break;
    }
    case SFX_WHOOP: {                    /* whoop: up three octaves, then back down */
        float a = t / (0.45f * L), f = 180.0f * P * (a < 1.0f ? brk_exp2f(3.0f * a) : brk_exp2f(3.0f - 6.0f * (a - 1.0f)));
        y = 0.6f * sine(&v->ph[0], f) + 0.25f * saw(&v->ph[1], f);
        v->env += (1.0f - v->env) * 0.004f;
        y *= v->env * (1.0f - x);
        break;
    }
    case SFX_LASER:                      /* laser: a sine diving five octaves */
        y = 0.8f * sine(&v->ph[0], 3200.0f * P * brk_exp2f(-5.0f * x)) * (1.0f - x);
        break;
    case SFX_ZAP: {                      /* zap: two quick square dives */
        float u = t / L, s = u < 0.13f ? u : u - 0.13f;
        y = (v->ph[0] < 0.5f ? 0.5f : -0.5f);
        saw(&v->ph[0], 1400.0f * P * brk_exp2f(-4.5f * s / 0.11f));
        v->lp += (y - v->lp) * 0.4f;
        y = s < 0.11f ? v->lp * (1.0f - s / 0.11f) : 0.0f;
        break;
    }
    case SFX_BOOM: {                     /* gunshot: a crack, a thump, a long dark tail */
        float n = noise(&v->seed), crack = t < 0.025f ? n : 0.0f;
        v->lp += (n - v->lp) * 0.08f;
        y = crack * 0.9f + 0.9f * sine(&v->ph[0], 55.0f * P) * exp_decay(t, 0.25f * L) +
            1.6f * v->lp * exp_decay(t, 0.5f * L);
        break;
    }
    case SFX_BRAP: {                     /* brap: five shots, fast */
        float u = t / L, k = (float)(int32_t)(u / 0.07f), s = u - k * 0.07f;
        float n = noise(&v->seed);
        y = k < 5.0f ? (n * 0.8f + 0.6f * sine(&v->ph[0], 70.0f * P)) * exp_decay(s, 0.012f) : 0.0f;
        break;
    }
    case SFX_FYAH:                       /* "FYAH!": an F of hiss, then "ah" falling, formants on a saw */
    case SFX_HEY: {                      /* "HEY!": a breath, then "eh" */
        float cons = v->id == SFX_FYAH ? 0.12f * L : 0.06f * L, n = noise(&v->seed), src, f0;
        if (t < cons) {
            v->lp += (n - v->lp) * (v->id == SFX_FYAH ? 0.7f : 0.25f);
            y = (v->id == SFX_FYAH ? (n - v->lp) : v->lp) * 0.4f * (t / cons);
        } else {
            float vt = (t - cons) / (v->dur ? (float)v->dur / SR - cons : 1.0f);
            f0 = v->id == SFX_FYAH ? (210.0f - 60.0f * vt) : (230.0f + 60.0f * vt * (1.0f - vt) * 4.0f - 30.0f * vt);
            f0 *= P * (1.0f + 0.02f * brk_sin_turns(t * 5.5f));
            src = saw(&v->ph[0], f0) + 0.15f * n;
            y = 1.4f * bp_run(&v->bp[0], src) + 0.9f * bp_run(&v->bp[1], src) + 0.45f * bp_run(&v->bp[2], src);
            v->env += (1.0f - v->env) * 0.004f;
            y = soft(y * 2.0f) * v->env * (vt > 0.8f ? (1.0f - vt) * 5.0f : 1.0f);
        }
        break;
    }
    case SFX_RISER: {                    /* riser: noise through a band sweeping up, a tone under it, while held */
        float n = noise(&v->seed), a = brk_clampf(t / (2.2f * L), 0.0f, 1.0f);
        if ((v->n & 63u) == 0u)
            bp_set(&v->bp[0], 200.0f * brk_exp2f(5.3f * a), 3.0f);
        y = (1.2f * bp_run(&v->bp[0], n) + 0.2f * sine(&v->ph[0], 200.0f * P * brk_exp2f(3.0f * a))) * (0.2f + 0.8f * a);
        break;
    }
    case SFX_REVCYM: {                   /* reverse cymbal: hiss swelling to a sudden stop */
        float n = noise(&v->seed);
        v->lp += (n - v->lp) * 0.3f;
        y = (n - v->lp) * 0.9f * x * x * x * (x > 0.99f ? (1.0f - x) * 100.0f : 1.0f);
        break;
    }
    case SFX_SUBDROP:                    /* sub drop: a sine falling from 95 Hz to the floor */
        y = (0.95f * sine(&v->ph[0], 95.0f * P * brk_exp2f(-1.6f * x)) + (t < 0.004f ? 0.4f : 0.0f)) *
            exp_decay(t, 0.9f * L);
        break;
    case SFX_STAB: {                     /* horn stab: a major chord of saws, the filter closing */
        float f = 233.0f * P;
        y = saw(&v->ph[0], f) + saw(&v->ph[1], f * 1.2599f) + saw(&v->ph[2], f * 1.4983f);
        v->lp += (y - v->lp) * (0.05f + 0.5f * (1.0f - x) * (1.0f - x));
        y = soft(v->lp * 0.8f) * exp_decay(t, 0.18f * L);
        break;
    }
    case SFX_SKANK: {                    /* skank: the offbeat guitar chop, a minor chord, short and bright */
        float f = 392.0f * P, n = noise(&v->seed);
        y = saw(&v->ph[0], f) + saw(&v->ph[1], f * 1.1892f) + saw(&v->ph[2], f * 1.4983f);
        v->lp += (y - v->lp) * 0.1f;
        y = ((y - v->lp) * 0.5f + (t < 0.005f ? n * 0.5f : 0.0f)) * exp_decay(t, 0.05f * L);
        break;
    }
    case SFX_SCRATCH: {                  /* scratch: wiki-wiki, a band of noise and a saw pushed back and forth */
        float r = 1.0f + 0.9f * tri(t * 7.0f / L), n = noise(&v->seed);
        if ((v->n & 31u) == 0u)
            bp_set(&v->bp[0], 900.0f * r * P, 4.0f);
        y = (1.4f * bp_run(&v->bp[0], n) + 0.3f * saw(&v->ph[0], 250.0f * r * P)) * brk_absf(tri(t * 7.0f / L));
        break;
    }
    case SFX_REWIND: {                   /* rewind: the whoosh under the pull-up (the break itself rewinds) */
        float n = noise(&v->seed);
        if ((v->n & 63u) == 0u)
            bp_set(&v->bp[0], 6000.0f * brk_exp2f(-4.3f * x), 2.0f);
        y = 1.2f * bp_run(&v->bp[0], n) * (1.0f - x) * brk_clampf(t * 30.0f, 0.0f, 1.0f);
        break;
    }
    default: break;
    }
    return y;
}

void sfx_render(float *out, uint32_t n)
{
    uint32_t i, k, mask = 0u;
    float P, lvl;
    cur = *(const sfx_set_t *)&p_set;
    P = brk_exp2f(brk_clampf(cur.pitch, -12.0f, 12.0f) * (1.0f / 12.0f));
    lvl = brk_clampf(cur.level, 0.0f, 1.0f) * 0.6f;
    while (q_r != q_w) {
        ev_t e = q[q_r % QLEN];
        q_r++;
        if (e.id < 0 || e.id >= SFX_N)
            continue;
        if (e.down)
            start(e.id);
        else
            for (k = 0; k < NV; k++)
                if (vo[k].id == e.id)
                    vo[k].held = 0u;
    }
    for (i = 0; i < n; i++)
        out[i] = 0.0f;
    for (k = 0; k < NV; k++) {
        voice_t *v = &vo[k];
        if (v->id < 0)
            continue;
        for (i = 0; i < n; i++) {
            if (!v->dur && !v->held)
                v->rel *= 0.9996f;       /* a held sound let go: 0.25 s away */
            out[i] += tick(v, P) * v->rel * lvl;
            v->n++;
        }
        if ((v->dur && v->n >= v->dur) || (!v->dur && (v->rel < 0.001f || (float)v->n > HELD_MAX * SR))) {
            prog[v->id] = 0.0f;
            v->id = -1;
            continue;
        }
        mask |= 1u << v->id;
        prog[v->id] = v->dur ? (float)v->n / (float)v->dur : brk_clampf((float)v->n / (HELD_MAX * SR), 0.0f, 1.0f);
    }
    for (i = 0; i < n; i++)
        out[i] = knee(out[i]);
    playing_mask = mask;
}

void sfx_init(void)
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        vo[i].id = -1;
    q_r = q_w;
    playing_mask = 0u;
}
void sfx_defaults(sfx_set_t *s)
{
    s->level = 0.8f;
    s->pitch = 0.0f;
    s->echo = 0.3f;
    s->length = 1.0f;
}
void sfx_set(const sfx_set_t *s) { *(sfx_set_t *)&p_set = *s; }
static void push(int id, int down)
{
    if (q_w - q_r >= QLEN)
        return;
    q[q_w % QLEN].id = (int8_t)id;
    q[q_w % QLEN].down = (uint8_t)down;
    __asm__ volatile("" ::: "memory");
    q_w++;
}
void sfx_trigger(int id) { push(id, 1); }
void sfx_release(int id) { push(id, 0); }
uint32_t sfx_playing(void) { return playing_mask; }
float sfx_progress(int id) { return id >= 0 && id < SFX_N ? prog[id] : 0.0f; }
float sfx_echo(void) { return p_set.echo; }
