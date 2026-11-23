/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's break engine (brk.h): the sequencer, the generator, the pads and two crossfading slice voices. */
#include "brk.h"
#include "brk_math.h"

#define FADE_IN 44.0f                    /* samples: 1 ms in, 2 ms out on every cut (PRD A-6) */
#define FADE_OUT 88.0f
#define GATE_FADE 132.0f                 /* samples: a gated slice's 3 ms fade */
#define QLEN 32u                         /* key events between two renders */

/* ---------------------------------------------------------------- shared --- */
/* written by the main loop, read by the render (single words) */
static const brk_break_t *volatile brk_now;
static const brk_break_t *volatile p_swap;   /* a break waiting for the next step (brk_swap_break) */
static volatile int p_mute;              /* the break silent (brk_mute), still running */
static float mute_amp = 1.0f;
static volatile float p_tempo = 136.9f, p_pitch, p_swing = 0.5f;
static volatile int p_playing;
static volatile uint32_t p_pads;         /* bit PAD_*: held */
static volatile uint32_t p_reset;        /* a count: changed = reset at the next render */
static volatile int p_keep;              /* pitch keeps the length (granular), not the speed */
static volatile float p_gate = 1.0f;     /* how much of each of the loop's slices sounds */
static volatile float p_snap = 0.5f;     /* where a key pressed while playing begins: slices (0 at once) */
static volatile float p_grain = 1323.0f; /* its grains, samples (30 ms) */
static brk_gen_t p_gen;
static brk_padset_t p_ps;                /* the pads' settings (brk_padset) */
static brk_step_t pat[BRK_MAX_STEPS];
static volatile uint32_t p_keys[BRK_KEYS];   /* the white keys' settings, a brk_keyset_t a word */
static volatile uint32_t p_slx[BRK_KEYS];    /* each slice's effect, a brk_slicefx_t a word */
typedef union { uint32_t w; brk_slicefx_t f; } slxword_t;
const char *const BRK_SLX_NAME[BRK_SLX] = {"OFF", "LO-PASS", "HI-PASS", "CRUNCH", "DRIVE", "RING MOD", "ECHO", "VERB"};
static float *send_echo, *send_verb;     /* the render's throws (brk_sends) */
typedef union { uint32_t w; brk_keyset_t k; } keyword_t;

const char *const BRK_ROLL_DIV_NAME[BRK_ROLL_DIVS] = {"1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T", "1/32T"};
const char *const BRK_GATE_DIV_NAME[BRK_GATE_DIVS] = {"1/8", "1/16", "1/32"};
static const float ROLL_STEPS[BRK_ROLL_DIVS] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 4.0f / 3.0f, 2.0f / 3.0f, 1.0f / 3.0f};
static const float GATE_STEPS[BRK_GATE_DIVS] = {2.0f, 1.0f, 0.5f};
const float BRK_KEY_SLICES[BRK_KEY_LENS] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 1.5f, 3.0f};
const uint8_t BRK_KEY_ORDER[BRK_KEY_LENS] = {0u, 1u, 2u, 3u, 8u, 4u, 9u, 5u, 6u, 7u};
const char *const BRK_KEY_MODE_NAME[BRK_KEY_MODES] = {"LOOP", "PING-PONG", "REVERSE", "ONE SHOT"};

typedef struct { int8_t slice; uint8_t down; } kev_t;
static kev_t q[QLEN];
static volatile uint32_t q_w, q_r;       /* the main loop writes q_w, the render q_r */

/* ----------------------------------------------------------------- render --- */
typedef struct {
    float pos, dir;                      /* dir +1 / -1 */
    float lo, hi;                        /* bounds: it stops (or loops) there */
    float amp, amp_to, amp_step;
    int32_t slice;
    uint8_t active, loop;
    uint8_t wrap;                        /* runs on round the whole loop (playing, forwards) */
    float g_pos[2], g_age;               /* KEEP LENGTH: the two grains' read positions, samples into the cycle */
    float gate_left;                     /* the gate: the break's samples left before it fades (< 0: no gate) */
    /* the slice's own effect (brk_slicefx), set as it starts */
    uint8_t fxt;
    float lvl, echo, verb;               /* its level; its throws */
    float f1, f2, fa1, fa2, fa3, fk;     /* LO-PASS / HI-PASS: a TPT state-variable filter */
    float fq, fstep, fph, fhold;         /* CRUNCH: levels, the rate's step, phase, held; RING: fstep, fph, fq = mix */
    float fg;                            /* DRIVE: gain */
} voice_t;

static voice_t v[2];
static int cur;                          /* the voice that plays; the other fades out */
static voice_t lv[2];                    /* the layer's two voices (a key held in the split's other half, PRD S-032) */
static int lcur;
static int32_t step = -1, last_slice = -1;
static uint32_t bar, held_mask, reset_seen, pads_now, rng = 0x1234567u;
static float to_next;                    /* samples to the next step */
static float tape = 1.0f;                /* tape stop: 1 running .. 0 stopped */
static float gate_amp = 1.0f;
/* rolls (beat repeat), ratchets */
static float roll_len, roll_left, roll_pos;
static int32_t roll_slice = -1;
static uint8_t roll_arm;                 /* a roll pressed while playing starts at the next step (quantised) */
static int32_t roll_pad = -1;            /* the roll pad whose rate rolls (the last pressed) */
static uint32_t roll_n;                  /* repeats since the roll began */
static float roll_gain = 1.0f;           /* this repeat's level (the decay) */
static float gate_pos;                   /* samples into the gate's cycle */
static float rat_len, rat_left;
static uint32_t rat_count;
static int32_t rat_slice;
static int rat_rev;
static float rat_pitch, step_pitch;
static uint32_t step_in_len;             /* the length of the step playing, for the gate */
static float step_age;
static brk_status_t status;
/* the key loops (brk_keyset), two lanes. Lane 0: the key held last loops its window on the loop's own voices
 * (the sequencer quiet meanwhile), restarting every steps 16ths. Lane 1, the layer (PRD S-032): with a split
 * (brk_split), a key held in the half lane 0's key isn't in loops on voices of its own, on top; one a half, the last
 * key pressed in a half winning */
typedef struct {
    voice_t *pv;                         /* its two voices */
    int *pc;                             /* which of them plays */
    int32_t slice;                       /* the key looping, -1 none */
    int32_t pending;                     /* a key pressed while playing, waiting for its snap point (BRK_NO_SLICE) */
    uint32_t pending_at;                 /* the snap: the step (counted from the reset) it begins on */
    float pending_wait;                  /* or, less than a step apart, the samples until it does (< 0: a step) */
    brk_keyset_t set;                    /* its settings, as the repeat playing began */
    float steps;                         /* its length in 16ths of the tempo */
    float left;                          /* samples to the next repeat */
    float lo, w;                         /* its window in the break's samples */
    float pitch;                         /* semitones, while its sound plays */
    uint32_t n;                          /* repeats since the key went down */
} lane_t;
static lane_t kl[2] = {{v, &cur, -1, BRK_NO_SLICE, 0u, -1.0f, {3u, 0, 0u, 0u}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0u},
                       {lv, &lcur, -1, BRK_NO_SLICE, 0u, -1.0f, {3u, 0, 0u, 0u}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0u}};
#define K0 (&kl[0])
#define K1 (&kl[1])
/* the split (PRD S-030): brk_break_t.split */
static volatile int p_half = -1;         /* the half asked for: -1 both, 0 the first, 1 the second */
static int half_on = -1;                 /* the half in force: it changes at a bar line */
/* MIDI clock */
#define CLK_MAX 32u
static uint16_t clk_off[CLK_MAX];        /* the last render's ticks: their sample offsets */
static uint32_t clk_cnt;
static float clk_left;                   /* samples to the next tick */
static uint32_t clk_k = 12u;             /* ticks given in this pair of 16ths (12: done until the next pair) */
#define EV_MAX 8u
static uint16_t ev_off[EV_MAX];          /* the last render's steps: offsets and places in the bar */
static uint8_t ev_idx[EV_MAX];
static uint32_t ev_cnt;
static uint32_t steps_total;             /* steps begun since the reset (the outside clock counts from its start) */
static int was_playing;                  /* the transport at the last render: a stop fades the break out */
static volatile uint32_t n_renders;
/* MATHCORE: the roles, the lanes' voices, the scene playing */
const uint8_t MC_RATE_TICKS[MC_RATES] = {6u, 12u, 24u, 48u, 4u, 8u, 16u};
const char *const MC_RATE_NAME[MC_RATES] = {"1/32", "1/16", "1/8", "1/4", "1/32T", "1/16T", "1/8T"};
const char *const MC_MODE_NAME[MC_MODES] = {"FIXED", "ROUND", "RANDOM", "ALT", "ACCENT"};
static uint8_t p_roles[BRK_MAX_SLICES];      /* slice i's role bits */
static volatile uint16_t p_dl[3];        /* the pattern's drum lanes (kick, snare, hat) */
static volatile uint32_t p_rhit[MC_ROLES];   /* role hits asked (counts) */
static uint32_t rhit_seen[MC_ROLES];
static const mc_scene_t *volatile p_scenes;
static volatile int32_t p_mc_start = -1; /* the scene a start asks for (-1 none): brk_mc_start */
static volatile int32_t p_mc_next = -1;
static volatile uint32_t p_mc_mute, p_mc_solo;
static volatile uint32_t p_mc_macro = 50u | 0u << 8 | 50u << 16 | 0u << 24;   /* density, math, speed, chaos */
static volatile int32_t p_mc_jump;      /* a launch: the scene, every p_mc_jq 16ths, its count p_mc_jn */
static volatile uint32_t p_mc_jq = 16u, p_mc_jn;
static uint32_t mc_jn_seen;
const uint8_t MC_MATH_LEN[MC_MATH_STEPS][4] = {{0u, 0u, 0u, 0u}, {0u, 0u, 0u, 12u}, {16u, 0u, 12u, 10u},
                                               {15u, 0u, 12u, 7u}, {14u, 16u, 11u, 7u}, {10u, 14u, 11u, 13u},
                                               {7u, 12u, 9u, 5u}, {5u, 7u, 11u, 13u}};
static int32_t mc_cur = -1;              /* the scene playing */
static uint32_t mc_base;                 /* the step (steps_total) it began on */
static uint32_t mc_steps;                /* 16ths of it played */
static uint32_t mc_adv, mc_ended;
static uint32_t mc_last_t = 0xFFFFFFFFu; /* the master tick last seen */
static float tick_k;                     /* ticks a sample in the step playing */
static uint32_t mc_n[MC_LANES];          /* hits since the scene began (ROUND, ALT) */
static uint8_t mc_pos[MC_LANES], mc_hitc[MC_LANES], mc_miss;
static uint32_t mr_count[MC_LANES];      /* a lane's ratchet: hits still to come, every mr_len samples */
static float mr_left[MC_LANES], mr_len[MC_LANES], mr_gain[MC_LANES];
static int32_t mr_slice[MC_LANES];
static int loop_sounds(void);

static inline float pcm_at(const brk_break_t *b, int32_t i)
{
    if (i < 0)
        i = 0;
    else if ((uint32_t)i >= b->len)
        i = (int32_t)b->len - 1;
    return (float)b->pcm[i];
}

/* 4-point Hermite (PRD E-1) */
static inline float hermite(const brk_break_t *b, float pos)
{
    int32_t i = (int32_t)pos;
    float t = pos - (float)i;
    float xm1 = pcm_at(b, i - 1), x0 = pcm_at(b, i), x1 = pcm_at(b, i + 1), x2 = pcm_at(b, i + 2);
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

static float step_len(void)              /* a 16th at the tempo, in samples */
{
    float t = brk_clampf(p_tempo, 40.0f, 300.0f);
    return (float)BRK_SR * 60.0f / (t * 4.0f);
}

static uint32_t loop_steps(const brk_break_t *b)
{
    uint32_t n = (b ? b->bars : 1u) * BRK_STEPS_PER_BAR;
    return n < 1u ? 1u : n > BRK_MAX_STEPS ? BRK_MAX_STEPS : n;
}

/* slice s's effect word (off, level 100 past the keys) */
static uint32_t slx_of(int32_t s)
{
    return (s >= 0 && (uint32_t)s < BRK_KEYS) ? p_slx[s] : 0x64000000u;
}

/* a voice's slice effect, from the slice's settings as it starts */
static void voice_fx(voice_t *n, const brk_break_t *b, int32_t s)
{
    slxword_t u;
    float a, t;
    (void)b;
    u.w = slx_of(s);
    a = (float)(u.f.amount > 100u ? 100u : u.f.amount) * 0.01f;
    t = (float)(u.f.tone > 100u ? 100u : u.f.tone) * 0.01f;
    n->fxt = u.f.type < BRK_SLX ? u.f.type : SLX_OFF;
    n->lvl = (float)(u.f.level > 100u ? 100u : u.f.level) * 0.01f;
    n->echo = n->verb = 0.0f;
    n->f1 = n->f2 = n->fph = n->fhold = 0.0f;
    switch (n->fxt) {
    case SLX_LOPASS:
    case SLX_HIPASS: {                   /* LO-PASS 16 kHz down to 150 Hz, HI-PASS 30 Hz up to 5 kHz */
        float hz = n->fxt == SLX_LOPASS ? 16000.0f * brk_exp2f(-a * 6.74f) : 30.0f * brk_exp2f(a * 7.38f);
        float g = brk_tan_pi(hz / (float)BRK_SR);
        n->fk = 2.0f - 1.9f * t;
        n->fa1 = 1.0f / (1.0f + g * (g + n->fk));
        n->fa2 = g * n->fa1;
        n->fa3 = g * n->fa2;
        break;
    }
    case SLX_CRUNCH:                     /* 16 bits at 44.1 kHz down to 3 bits at 2.8 kHz */
        n->fq = brk_exp2f(15.0f - 12.0f * a);
        n->fstep = brk_exp2f(-a * 4.0f);
        break;
    case SLX_DRIVE:
        n->fg = 1.0f + 24.0f * a * a;
        break;
    case SLX_RING:                       /* 40 Hz .. 2 kHz, mixed by tone (0: half) */
        n->fstep = 40.0f * brk_exp2f(a * 5.64f) / (float)BRK_SR;
        n->fq = 0.5f + 0.5f * t;
        break;
    case SLX_ECHO:
        n->echo = a;
        break;
    case SLX_VERB:
        n->verb = a;
        break;
    default:
        break;
    }
}

/* the voice's sample through its slice effect (the ECHO and VERB throws are taken from it after) */
static inline float voice_fx_run(voice_t *o, float x)
{
    switch (o->fxt) {
    case SLX_LOPASS:
    case SLX_HIPASS: {
        float v3 = x - o->f2, v1 = o->fa1 * o->f1 + o->fa2 * v3, v2 = o->f2 + o->fa2 * o->f1 + o->fa3 * v3;
        o->f1 = 2.0f * v1 - o->f1;
        o->f2 = 2.0f * v2 - o->f2;
        x = o->fxt == SLX_LOPASS ? v2 : x - o->fk * v1 - v2;
        break;
    }
    case SLX_CRUNCH:                     /* (x in the PCM's units, +-32768) */
        o->fph += o->fstep;
        if (o->fph >= 1.0f) {
            o->fph -= 1.0f;
            o->fhold = (float)(int32_t)(x * (1.0f / 32768.0f) * o->fq) / o->fq * 32768.0f;
        }
        x = o->fhold;
        break;
    case SLX_DRIVE: {
        float y = x * (1.0f / 32768.0f) * o->fg;
        x = y / (1.0f + brk_absf(y)) * 32768.0f * (1.0f + 0.15f * o->fg / 25.0f);
        break;
    }
    case SLX_RING:
        x = x * (1.0f - o->fq) + x * brk_sin_turns(o->fph) * o->fq;
        o->fph += o->fstep;
        if (o->fph >= 1.0f)
            o->fph -= 1.0f;
        break;
    default:
        break;
    }
    return x * o->lvl;
}

/* a voice of the pair pv (pc: the one playing) starts slice s; the other fades out */
static void voice_start_in(voice_t *pv, int *pc, const brk_break_t *b, int32_t s, int rev, float at, float gain)
{
    voice_t *o = &pv[*pc], *n;
    if (o->active) {                     /* the old voice fades out */
        o->amp_to = 0.0f;
        o->amp_step = o->amp / FADE_OUT;
    }
    *pc ^= 1;
    n = &pv[*pc];
    n->slice = s;
    n->lo = (float)b->slice[s];
    /* playing, a slice runs on into the next until the next trigger; stopped (free play) it stops at its end */
    n->hi = (float)(p_playing ? b->len : b->slice[s + 1]);
    n->dir = rev ? -1.0f : 1.0f;
    if (rev) {                           /* backwards: from the slice's end to its start */
        n->hi = (float)b->slice[s + 1];
        n->pos = at >= 0.0f ? at : n->hi - 1.0f;
    } else
        n->pos = at >= 0.0f ? at : n->lo;
    n->amp = 0.0f;
    n->amp_to = gain;
    n->amp_step = gain / FADE_IN;
    n->loop = 0;
    n->wrap = (uint8_t)(p_playing && !rev);
    n->g_pos[0] = n->g_pos[1] = n->pos;  /* the grains start on the hit (the second at full: the attack whole) */
    n->g_age = 0.0f;
    n->gate_left = -1.0f;
    voice_fx(n, b, s);
    n->active = 1;
    if (pv == v)
        last_slice = s;
}
static void voice_start(const brk_break_t *b, int32_t s, int rev, float at, float gain)
{
    voice_start_in(v, &cur, b, s, rev, at, gain);
}

static void trigger(const brk_break_t *b, int32_t s, int rev)
{
    voice_t *c = &v[cur];
    if (!b || s < 0 || (uint32_t)s >= b->nslices)
        return;
    /* the sound playing is already where slice s begins (the slices in order): it runs on, so 1..16 is the record as
     * it is, its timing and all (PRD A-10). The markers sit on the hits, up to 2/3 of a 16th off the grid: a jump
     * there would skip a bit or play a hit twice */
    if (c->active && !rev && c->dir > 0.0f && c->wrap && !c->loop && c->amp_to > 0.0f && c->slice >= 0 &&
        c->slice != s && slx_of(s) == slx_of(c->slice)) {
        float d = c->pos - (float)b->slice[s], n = (float)b->len;
        if (d > 0.5f * n)
            d -= n;
        else if (d < -0.5f * n)
            d += n;
        if (d * d < 0.49f * (n * n) / (float)(b->bars * b->bars * BRK_STEPS_PER_BAR * BRK_STEPS_PER_BAR)) {
            c->slice = s;
            last_slice = s;
            c->gate_left = -1.0f;
            c = 0;
        }
    }
    if (c)
        voice_start(b, s, rev, -1.0f, 1.0f);
    if (p_gate < 0.999f)                 /* the gate: this much of the slice, then a short fade */
        v[cur].gate_left = p_gate * (float)(b->slice[s + 1] - b->slice[s]);
}

static brk_keyset_t keyset_of(int32_t s)
{
    keyword_t u;
    u.w = (s >= 0 && (uint32_t)s < BRK_KEYS) ? p_keys[s] : 0x00000003u;   /* (the defaults: one slice, LOOP) */
    if (u.k.len >= BRK_KEY_LENS)
        u.k.len = 3u;
    if (u.k.mode >= BRK_KEY_MODES)
        u.k.mode = KEY_LOOP;
    return u.k;
}

/* key s's settings as they are now: its length in 16ths */
static void key_refresh(lane_t *k, const brk_break_t *b, int32_t s)
{
    k->set = keyset_of(s);
    k->steps = BRK_KEY_SLICES[k->set.len] * (float)(b->bars * BRK_STEPS_PER_BAR) / (float)b->nslices;
}

/* a repeat of key s's loop: its window from the start, forwards or backwards (the mode, the REVERSE pad) */
static void key_begin(lane_t *k, const brk_break_t *b, int32_t s)
{
    float ns = (float)b->len / (float)(b->bars * BRK_STEPS_PER_BAR);         /* a 16th in the break's samples */
    float hi;
    int rev;
    voice_t *n;
    if (s < 0 || (uint32_t)s >= b->nslices)
        return;
    key_refresh(k, b, s);
    k->lo = (float)b->slice[s] + (float)(k->set.start & 15u) * (1.0f / 16.0f) * (float)(b->slice[s + 1] - b->slice[s]);
    k->w = k->steps * ns;
    if (k->w < 32.0f)
        k->w = 32.0f;
    rev = k->set.mode == KEY_REVERSE || (k->set.mode == KEY_PINGPONG && (k->n & 1u));
    rev ^= (int)((pads_now >> PAD_REVERSE) & 1u);
    hi = k->lo + k->w > (float)b->len ? (float)b->len : k->lo + k->w;
    voice_start_in(k->pv, k->pc, b, s, rev, rev ? hi - 1.0f : k->lo, 1.0f);
    n = &k->pv[*k->pc];
    n->lo = k->lo;
    if (rev) {                           /* backwards: round the window until the next repeat */
        n->hi = hi;
        n->loop = (uint8_t)(k->set.mode != KEY_ONESHOT);
        n->wrap = 0u;
    } else if (k->set.mode == KEY_ONESHOT) {   /* once through the window, then quiet */
        n->hi = hi;
        n->wrap = 0u;
    } else {                             /* forwards it runs on (round the break's end) until the next repeat */
        n->hi = (float)b->len;
        n->wrap = 1u;
    }
    k->pitch = (float)k->set.pitch;
}

/* the key let go (or tapped): playing, lane 0's sound runs on into the loop; stopped (and the layer always), it ends
 * with its window */
static void key_let_go(lane_t *k, const brk_break_t *b)
{
    voice_t *c = &k->pv[*k->pc];
    int run = p_playing && k == K0 && loop_sounds();
    k->slice = -1;
    if (!c->active)
        return;
    c->loop = 0u;
    if (c->dir > 0.0f) {
        c->hi = run ? (float)b->len : (k->lo + k->w > (float)b->len ? (float)b->len : k->lo + k->w);
        c->wrap = (uint8_t)run;
    }
}

static int half_of(const brk_break_t *b, int32_t s) { return b->split && (uint32_t)s >= b->split; }

/* the lane a key pressed goes to: the layer when lane 0 has a key of the other half (or the layer one of this
 * half), else lane 0 */
static lane_t *lane_for(const brk_break_t *b, int32_t s)
{
    int32_t a = K0->slice >= 0 ? K0->slice : K0->pending, l = K1->slice >= 0 ? K1->slice : K1->pending;
    if (!b->split)
        return K0;
    if (l >= 0 && half_of(b, l) == half_of(b, s))
        return K1;
    return a >= 0 && half_of(b, a) != half_of(b, s) ? K1 : K0;
}

/* the half in force keeps the loop's own slices in it: one of the other half folds over */
static int32_t fold(const brk_break_t *b, int32_t s)
{
    uint32_t lo = half_on ? b->split : 0u, hi = half_on ? b->nslices : b->split;
    if (half_on < 0 || s < 0 || hi <= lo || b->split >= b->nslices)
        return s;
    return (uint32_t)s >= lo && (uint32_t)s < hi ? s : (int32_t)(lo + (uint32_t)s % (hi - lo));
}

/* the slice the loop itself plays at step i, or -1 (no slice starts there) */
static int32_t follow_slice(const brk_break_t *b, uint32_t i)
{
    uint32_t steps = b->bars * BRK_STEPS_PER_BAR, p = i % steps;
    int32_t f = (int32_t)(p * b->nslices / steps);
    int32_t prev = p ? (int32_t)((p - 1u) * b->nslices / steps) : -1;
    return f != prev ? f : -1;
}

/* the generator (PRD A-10): f is the loop's own slice at this step */
static int32_t generate(const brk_break_t *b, int32_t f)
{
    brk_gen_t g = p_gen;
    float cx = g.complexity;
    uint32_t in_bar = (uint32_t)step % BRK_STEPS_PER_BAR;
    if (g.phrase_bars && bar % g.phrase_bars == g.phrase_bars - 1u)
        cx += g.fill * (1.0f - cx);      /* the phrase's last bar breaks the rules */
    if (cx <= 0.0f)
        return f;                        /* straight: the loop as it is */
    if ((in_bar == 0u || in_bar == 8u || in_bar == 4u || in_bar == 12u) && brk_randf(&rng) < g.anchor)
        return f;                        /* kick on 1 and 3, snare on 2 and 4 */
    if (brk_randf(&rng) >= cx)
        return f;
    if (last_slice >= 0 && brk_randf(&rng) < g.roll) {   /* a repeat, or the slice next door */
        float r = brk_randf(&rng);
        int32_t s = r < 0.5f ? last_slice : last_slice + (r < 0.75f ? -1 : 1);
        return s < 0 ? 0 : (uint32_t)s >= b->nslices ? (int32_t)b->nslices - 1 : s;
    }
    return (int32_t)(brk_rand(&rng) % b->nslices);
}

static int suppressed(void)              /* pads that own the voice: the sequencer counts on but stays quiet */
{
    if (roll_arm)
        return (pads_now & (1u << PAD_FREEZE | 1u << PAD_TAPESTOP)) != 0u;
    return (pads_now & (1u << PAD_ROLL8 | 1u << PAD_ROLL16 | 1u << PAD_ROLL32 | 1u << PAD_FREEZE |
                        1u << PAD_TAPESTOP)) != 0u;
}

static void roll_begin(const brk_break_t *b)   /* the roll repeats the slice sounding, from its start */
{
    const voice_t *c = &v[cur];
    roll_arm = 0u;
    if (!c->active || c->slice < 0)
        return;
    roll_slice = c->slice;
    roll_pos = c->dir > 0.0f ? (float)b->slice[c->slice] : (float)b->slice[c->slice + 1] - 1.0f;
    roll_left = roll_len;                /* this hit has just started: the next one a roll's length on */
    roll_n = 0u;
    roll_gain = (float)(p_ps.roll_level ? p_ps.roll_level : 100u) * 0.01f;   /* the repeats begin at the level */
    if (roll_gain > 1.0f)
        roll_gain = 1.0f;
}

/* a key pressed while playing, at its snap point: its loop starts here (released already: one hit) */
static void key_snapped(lane_t *k, const brk_break_t *b)
{
    int32_t s = k->pending;
    k->pending = BRK_NO_SLICE;
    k->pending_wait = -1.0f;
    k->n = 0u;
    key_begin(k, b, s);
    k->left = k->steps * step_len();
    if ((held_mask >> s) & 1u)
        k->slice = s;
    else
        key_let_go(k, b);                /* a tap: one hit */
}

/* a key pressed while playing: where it begins (brk_snap). Whole steps wait for that step (the swing's); less than a
 * step counts samples, straight. now_len: the step playing's length */
static void key_snap(lane_t *k, const brk_break_t *b, int32_t s)
{
    float sps = (float)(b->bars * BRK_STEPS_PER_BAR) / (float)b->nslices, snap = p_snap * sps, pos, target;
    k->pending = s;
    k->pending_wait = -1.0f;
    if (!steps_total || !step_in_len) {  /* not begun yet: the first step */
        k->pending_at = steps_total;
        return;
    }
    pos = (float)(steps_total - 1u) + ((float)step_in_len - to_next) / (float)step_in_len;   /* in 16ths */
    if (snap <= 0.0f) {                  /* OFF: now */
        k->pending_wait = 0.0f;
        return;
    }
    target = snap * (float)((uint32_t)(pos / snap) + 1u);    /* the next grid point after now */
    if (snap >= 0.999f && target - (float)(uint32_t)(target + 0.001f) < 0.002f)
        k->pending_at = (uint32_t)(target + 0.5f);   /* a whole step: on_step begins it */
    else
        k->pending_wait = (target - pos) * step_len();
}

/* a key down on lane k: playing, at its snap point; stopped (free play), now, looping while held */
static void key_press(lane_t *k, const brk_break_t *b, int32_t s, int playing, float sl)
{
    if (playing)
        key_snap(k, b, s);
    else {
        k->n = 0u;
        key_begin(k, b, s);
        k->left = k->steps * sl;
        k->slice = s;
    }
}

/* ---- MATHCORE: the sample lanes (docs/PRD_MATHCORE.md) */
uint32_t mc_role_slices(const uint8_t *roles, uint32_t n, uint32_t role, uint8_t *list)
{
    uint32_t i, c = 0u;
    if (n > BRK_MAX_SLICES)
        n = BRK_MAX_SLICES;
    for (i = 0; i < n; i++)
        if (roles[i] >> role & 1u)
            list[c++] = (uint8_t)i;
    return c;
}

static uint32_t mc_hash(uint32_t seed, uint32_t lane, uint32_t e)   /* the same three, the same number */
{
    uint32_t x = (seed + 1u) * 0x9E3779B1u ^ (lane + 1u) * 0x85EBCA6Bu ^ (e + 1u) * 0xC2B2AE35u;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    return x ^ x >> 15;
}

/* a scene plays the break itself: its hits are the break's voice, each cutting the last (nothing laid over the
 * loop). MCS_BREAK: a hit runs on into the break after it until the next one (a re-cut break); without it each hit
 * stops at its slice's end (dry chops). Without a scene the loop runs on as ever */
static int loop_sounds(void)
{
    const mc_scene_t *sc = p_scenes;
    return mc_cur < 0 || !sc || (sc[mc_cur].flags & MCS_BREAK);
}

static uint8_t mr_flags[MC_LANES], mr_gate[MC_LANES], mr_score[MC_LANES];
static uint32_t mr_hold[MC_LANES];       /* a roll (a ratchet) owns the voice until this master tick */
static float mr_pitch[MC_LANES];
/* slice s on the break's voice (the one sound playing, its fade the cut): at gain, backwards, gate % of it (and of
 * HOME's GATE, as the loop's slices), at pitch semitones; it runs on into the break when the scene asks (loop_sounds), else it ends with its slice. A key looping
 * or a pad holding the voice (a roll, a freeze, the tape stop) keeps it: the hit is counted, not heard */
static void chop_hit(const brk_break_t *b, int32_t s, float gain, int rev, uint32_t gate, float pitch)
{
    voice_t *n;
    if (s < 0 || (uint32_t)s >= b->nslices || K0->slice >= 0 || suppressed())
        return;
    voice_start(b, s, rev, -1.0f, gain);
    n = &v[cur];
    if (!loop_sounds() || !p_playing) {  /* (dry: to the slice's end) */
        n->hi = (float)b->slice[s + 1];
        n->wrap = 0u;
    }
    {                                    /* the lane's gate times HOME's GATE (the loop's): this much, then a fade */
        float gl = (gate < 100u ? (float)gate * 0.01f : 1.0f) * p_gate;
        if (gl < 0.999f)
            n->gate_left = gl * (float)(b->slice[s + 1] - b->slice[s]);
    }
    K0->pitch = 0.0f;
    step_pitch = pitch;
}

/* a role's first slice (MIDI's drum notes): on the break's voice, at once */
static int32_t role_first(const brk_break_t *b, uint32_t role)
{
    uint8_t list[BRK_MAX_SLICES];
    uint32_t n = mc_role_slices(p_roles, b->nslices, role, list);
    if (!n) {
        mc_miss |= (uint8_t)(1u << role);
        return -1;
    }
    mc_miss &= (uint8_t)~(1u << role);
    return list[0];
}
static void role_hit(const brk_break_t *b, uint32_t role)
{
    chop_hit(b, role_first(b, role), 1.0f, 0, 100u, 0.0f);
}
/* the pattern's drum lanes at this step (bit at, the bar's 16th): the slice they put in the loop's place, -1 none.
 * One voice, so one wins: the snare, then the kick, then the hat */
static int32_t drum_slice(const brk_break_t *b, uint32_t at)
{
    static const uint8_t ORDER[3] = {1u, 0u, 2u};
    uint32_t k;
    for (k = 0; k < 3u; k++)
        if (p_dl[ORDER[k]] >> at & 1u) {
            int32_t s = role_first(b, ORDER[k]);
            if (s >= 0)
                return s;
        }
    return -1;
}

static void mc_begin(int32_t s)          /* scene s from this step */
{
    uint32_t l;
    mc_cur = s >= 0 && s < (int32_t)MC_SCENES ? s : -1;
    mc_base = steps_total;
    if (!loop_sounds()) {                /* a dry scene: the sound running on into the break ends with its slice */
        voice_t *c = &v[cur];
        const brk_break_t *b = brk_now;
        if (b && c->active && c->wrap && c->slice >= 0 && K0->slice < 0) {
            c->hi = (float)b->slice[c->slice + 1];
            c->wrap = 0u;
            if (c->pos >= c->hi)
                c->amp_to = 0.0f;
        }
    }
    mc_steps = 0u;
    mc_last_t = 0xFFFFFFFFu;
    for (l = 0; l < MC_LANES; l++) {
        const mc_scene_t *sc = p_scenes;
        if (!sc || mc_cur < 0 || !(sc[mc_cur].lane[l].flags & MCL_FREE))
            mc_n[l] = 0u;
        mr_count[l] = 0u;
        mr_hold[l] = 0u;
    }
}

/* a step begins: a start asked, or the scene's phrase done (the next scene, or none: the queue has run out) */
static volatile uint32_t p_mc_go;        /* a count: brk_mc_start */
static uint32_t mc_go_seen;
static void mc_step(void)
{
    const mc_scene_t *sc = p_scenes;
    if (p_mc_go != mc_go_seen) {
        mc_go_seen = p_mc_go;
        mc_jn_seen = p_mc_jn;            /* (a start overrules a launch asked before it) */
        mc_begin(p_mc_start);
    } else if (p_mc_jn != mc_jn_seen && !(steps_total % (p_mc_jq ? p_mc_jq : 16u))) {
        mc_jn_seen = p_mc_jn;            /* a launch on its beat or bar: counted from the bar it lands in */
        mc_begin(p_mc_jump);
        mc_base = steps_total - steps_total % 16u;
        mc_steps = steps_total % 16u;
        mc_adv++;
    } else if (mc_cur >= 0 && sc) {
        uint32_t bars = sc[mc_cur].bars;
        bars = bars < 1u ? 1u : bars > 16u ? 16u : bars;
        if (mc_steps >= bars * BRK_STEPS_PER_BAR) {
            int32_t nx = p_mc_next;
            mc_adv++;
            if (nx < 0 || nx >= (int32_t)MC_SCENES) {
                mc_cur = -1;
                mc_ended++;
            } else
                mc_begin(nx);
        }
    }
    if (mc_cur >= 0)
        mc_steps++;
}

/* master tick t (MC_TICKS a 16th since the start): every lane of the scene whose event falls on it */
/* event pos of lane l under DENSITY dens: as written, an accent, added (DENSITY up: fewer on the kick lane), dropped
 * (DENSITY down: off-beat, unaccented hits go first), or none. The same each cycle: the render and the screen agree */
uint32_t mc_event_kind(const mc_lane_t *L, uint32_t l, uint32_t pos, uint32_t dens)
{
    uint32_t ll = L->len ? L->len : 1u, hit = L->hits >> (pos % ll) & 1u, acc = L->accent >> (pos % ll) & 1u;
    if (hit && dens < 50u && pos && !acc && mc_hash(L->seed + 7u, l, pos) % 50u >= dens)
        return MCE_DROPPED;
    if (!hit && dens > 50u && mc_hash(L->seed + 3u, l, pos) % 100u < (dens - 50u) * (l ? 14u : 6u) / 10u)
        return MCE_ADDED;
    return !hit ? MCE_NONE : acc ? MCE_ACCENT : MCE_HIT;
}

static uint32_t bars_of(const mc_scene_t *sc)
{
    return sc->bars < 1u ? 1u : sc->bars > 16u ? 16u : sc->bars;
}
static void mc_tick(const brk_break_t *b, uint32_t t, float sl)
{
    const mc_scene_t *sc = p_scenes;
    static const uint8_t PRIO[MC_ROLES] = {3u, 4u, 1u, 2u};   /* one voice: the snare, the kick, the perc, the hat */
    uint32_t l, k, solo = p_mc_solo, mute = p_mc_mute, mac = p_mc_macro, win_score = 0u, win_r = 12u;
    int32_t win = -1, win_s = 0;
    float win_g = 1.0f, win_pt = 0.0f, win_late = 0.0f;
    uint8_t win_fl = 0u, win_rat = 1u;
    uint32_t dens = mac & 0xFFu, math = mac >> 8 & 0xFFu, spd = mac >> 16 & 0xFFu, chaos = mac >> 24 & 0xFFu;
    const uint8_t *mlen = MC_MATH_LEN[(math > 100u ? 100u : math) * (MC_MATH_STEPS - 1u) / 100u];
    if (!sc || mc_cur < 0)
        return;
    sc += mc_cur;
    for (l = 0; l < MC_LANES; l++) {
        const mc_lane_t *L = &sc->lane[l];
        uint8_t list[BRK_MAX_SLICES];
        uint32_t r, rel, e, pos, len = L->len < 1u ? 1u : L->len > MC_EVENTS ? MC_EVENTS : L->len, n, acc, hit;
        uint32_t role = L->role % MC_ROLES, ll = L->len ? L->len : 1u, vary = L->vary > 100u ? 100u : L->vary;
        uint32_t ghost = 0u;
        float late = 0.0f;
        uint8_t rat, fl;
        int32_t s;
        float g, pt;
        if (!(L->flags & MCL_ON))
            continue;
        r = MC_RATE_TICKS[L->rate < MC_RATES ? L->rate : MCR_16];
        if (spd < 34u)                   /* SPEED: half time, double time */
            r *= 2u;
        else if (spd > 66u && r > 4u)
            r /= 2u;
        if (L->rise && !(L->flags & MCL_SWELL)) {   /* a riser: faster each quarter of the phrase */
            uint32_t q = (mc_steps ? mc_steps - 1u : 0u) * 4u / (bars_of(sc) * BRK_STEPS_PER_BAR);
            r >>= q > 3u ? 3u : q;
            if (r < 3u)
                r = 3u;
        }
        rel = L->flags & MCL_FREE ? t : t - mc_base * MC_TICKS;
        if (rel % r)
            continue;
        e = rel / r;
        if (mlen[l])                     /* MATH: another length (the pattern read round its own) */
            len = mlen[l];
        pos = (e + L->phase) % len;
        mc_pos[l] = (uint8_t)pos;
        acc = L->accent >> (pos % (L->len ? L->len : 1u)) & 1u;
        {
            uint32_t kind = mc_event_kind(L, l, pos, dens);
            hit = kind == MCE_HIT || kind == MCE_ACCENT || kind == MCE_ADDED;
        }
        if (vary && mc_steps > (bars_of(sc) - 1u) * BRK_STEPS_PER_BAR)   /* the phrase's last bar: more */
            vary = vary * 3u / 2u + 20u > 100u ? 100u : vary * 3u / 2u + 20u;
        if (vary) {                      /* VARY: a drummer's own variations (seeded: the same each time) */
            uint32_t h = mc_hash(L->seed + 21u, l, e) % 100u;
            uint32_t nb = (L->hits >> ((pos + 1u) % ll) | L->hits >> ((pos + ll - 1u) % ll)) & 1u;
            if (!hit && (role == ROLE_SNARE || role == ROLE_PERC) && (nb || pos & 1u) &&
                h < vary * (nb ? 40u : 12u) / 100u)
                hit = ghost = 1u;        /* a ghost note: a drag before a hit, a bounce after it, an off-beat */
            else if (!hit && role == ROLE_KICK && (L->hits >> ((pos + 1u) % ll) & 1u) && h < vary * 15u / 100u)
                hit = ghost = 1u;        /* a pickup kick before a kick */
            else if (hit && role == ROLE_HAT && !acc && (pos & 1u) && h < vary * 20u / 100u)
                hit = 0u;                /* an off-beat hat left out */
        }
        if (!hit || (mute >> l & 1u) || (solo && !(solo >> l & 1u)))
            continue;                    /* (muted: the cycle counts on) */
        if (!ghost && L->prob < 100u && mc_hash(L->seed, l, e) % 100u >= L->prob)
            continue;
        n = mc_role_slices(p_roles, b->nslices, L->role % MC_ROLES, list);
        if (!n) {
            mc_miss |= (uint8_t)(1u << l);
            continue;
        }
        mc_miss &= (uint8_t)~(1u << l);
        switch (L->mode) {
        case MCM_ROUND: k = L->pick + mc_n[l]; break;
        case MCM_RANDOM: k = mc_hash(L->seed + 101u, l, e); break;
        case MCM_ALT: k = L->pick + (mc_n[l] & 1u); break;
        case MCM_ACCENT: k = L->pick + acc; break;
        default: k = L->pick; break;
        }
        rat = L->ratchet;
        fl = L->flags;
        if (vary) {                      /* another of the role's slices; a hat's little roll; a snare's flam */
            uint32_t h = mc_hash(L->seed + 31u, l, e) % 100u;
            if (n > 1u && h < vary / 2u)
                k += 1u + h % (n - 1u);
            if (role == ROLE_HAT && !acc && rat < 2u && r >= 12u && h % 5u == 2u && h < vary * 30u / 100u)
                rat = 2u;
            else if (role == ROLE_SNARE && acc && rat < 2u && h % 4u == 1u && h < vary * 25u / 100u)
                rat = 2u;
            if (ghost || (role == ROLE_HAT && (pos & 1u)))   /* laid back a touch: up to a tenth of a 16th */
                late = (float)(mc_hash(L->seed + 51u, l, e) % 100u) * (float)vary * 1e-5f * sl;
        }
        if (chaos) {                     /* CHAOS: a random slice, a ratchet, a reverse now and then */
            uint32_t h = mc_hash(L->seed + 5u, l, e) % 100u, c2 = mc_hash(L->seed + 9u, l, e) % 100u;
            if (h < chaos * 6u / 10u)
                k = mc_hash(L->seed + 11u, l, e);
            if (c2 < chaos * 3u / 10u)
                rat = (uint8_t)(2u + c2 % 2u);
            if (h % 7u == 3u && c2 < chaos * 15u / 100u)
                fl ^= MCL_REV;
        }
        s = list[k % n];
        mc_n[l]++;
        g = (float)(L->level > 100u ? 100u : L->level) * 0.01f * (L->accent && !acc ? 0.55f : 1.0f);
        if (ghost)
            g *= role == ROLE_KICK ? 0.6f : 0.3f;
        if (vary) {                      /* velocity: never twice the same, the off-beat hats softer */
            float rnd = (float)(mc_hash(L->seed + 41u, l, e) & 1023u) * (1.0f / 1023.0f);
            g *= 1.0f - (float)vary * 0.0035f * rnd;
            if (role == ROLE_HAT && (pos & 1u))
                g *= 1.0f - (float)vary * 0.003f;
        }
        pt = (float)L->pitch;
        if (L->rise) {                   /* the riser climbs and swells over its phrase */
            float pr = (float)(mc_steps ? mc_steps - 1u : 0u) / (float)(bars_of(sc) * BRK_STEPS_PER_BAR);
            pt += (float)L->rise * pr;
            g *= 0.35f + 0.65f * pr;
        }
        mc_hitc[l]++;                    /* (each lane's event counts; one of them is heard) */
        {
            uint32_t sc_ = ghost ? 0u : (acc ? 16u : 0u) + PRIO[role] * 2u + (rat > 1u);   /* (a ghost never cuts
                                          * a real hit; a roll wins over a single hit of the same weight) */
            if (sc_ > win_score || win < 0) {
                win = (int32_t)l;
                win_score = sc_;
                win_s = s;
                win_g = g;
                win_pt = pt;
                win_fl = fl;
                win_rat = rat;
                win_r = r;
                win_late = late;
            }
        }
    }
    if (win < 0)
        return;
    for (k = 0; k < MC_LANES; k++)       /* a roll (a ratchet) under way outranks a lighter hit: it rolls on */
        if (mr_hold[k] > t && mr_score[k] > win_score)
            return;
    l = (uint32_t)win;                   /* the one heard: the break's voice cut to it */
    mr_score[l] = (uint8_t)win_score;
    mr_flags[l] = win_fl;
    mr_gate[l] = (uint8_t)(sc->lane[l].gate ? sc->lane[l].gate : 100u);
    mr_pitch[l] = win_pt;
    for (k = 0; k < MC_LANES; k++)       /* (a new hit ends the ratchets before it) */
        mr_count[k] = 0u;
    if (win_late >= 1.0f && win_rat < 2u) {   /* laid back: the hit a little later (the ratchet's timer) */
        mr_count[l] = 1u;
        mr_hold[l] = 0u;
        mr_left[l] = mr_len[l] = win_late;
        mr_slice[l] = win_s;
        mr_gain[l] = win_g;
        return;
    }
    chop_hit(b, win_s, win_g, (win_fl & MCL_REV) != 0u, mr_gate[l], win_pt);
    mr_count[l] = win_rat > 1u ? (win_rat > 4u ? 3u : win_rat - 1u) : 0u;
    mr_hold[l] = mr_count[l] ? t + win_r : 0u;
    if (mr_count[l]) {                   /* the event's other hits, evenly inside it */
        mr_len[l] = (float)win_r * (1.0f / (float)MC_TICKS) * sl / (float)(mr_count[l] + 1u);
        mr_left[l] = mr_len[l];
        mr_slice[l] = win_s;
        mr_gain[l] = win_g;
    }
}

static void on_step(const brk_break_t *b)
{
    uint32_t n = loop_steps(b), hl = n / 2u;
    const brk_step_t *p;
    int32_t s = BRK_NO_SLICE;
    int rev = (pads_now >> PAD_REVERSE) & 1u;
    step = (int32_t)(((uint32_t)(step + 1)) % n);
    if (hl && !((uint32_t)step % hl)) {  /* a bar line (a split's halves are its bars): the half asked for */
        half_on = b->split ? p_half : -1;
        if (half_on >= 0 && (uint32_t)step / hl != (uint32_t)half_on)
            step = (int32_t)((uint32_t)half_on * hl);   /* one half alone: round that bar again */
    }
    if (step == (half_on > 0 ? (int32_t)hl : 0))
        bar++;
    step_age = 0.0f;
    rat_count = 0u;
    if (mc_cur < 0)
        step_pitch = 0.0f;               /* (a scene's hit keeps its pitch until the next) */
    if (p_ps.gate_div != 0u || !(step & 1))
        gate_pos = 0.0f;                 /* the gate starts with the step (1/8: every other step) */
    if (K1->pending != BRK_NO_SLICE && K1->pending_wait < 0.0f && steps_total >= K1->pending_at)
        key_snapped(K1, b);              /* the layer's snap point (the pads leave it alone) */
    if (suppressed())
        return;
    p = &pat[step];
    if (K0->pending != BRK_NO_SLICE && K0->pending_wait < 0.0f && steps_total >= K0->pending_at) {   /* the snap */
        key_snapped(K0, b);
        if (roll_arm)
            roll_begin(b);
        return;
    }
    if (K0->slice >= 0) {                /* a key looping owns the voice: the sequencer counts on, quiet */
        if (roll_arm)
            roll_begin(b);
        return;
    }
    if (mc_cur >= 0) {                   /* a scene plays the break: its hits cut the voice (mc_tick), not the loop */
        if (roll_arm)
            roll_begin(b);
        return;
    }
    if ((s = drum_slice(b, steps_total & 15u)) >= 0) {   /* the pattern's drum lanes: their slice in the loop's place */
        step_pitch = 0.0f;
    } else if (((pads_now >> PAD_RANDOM) & 1u) && brk_randf(&rng) < p_ps.rnd_amount) {
        s = fold(b, (int32_t)(brk_rand(&rng) % b->nslices));   /* a random slice, and a random pitch for it */
        step_pitch = (float)(int32_t)((brk_randf(&rng) * 2.0f - 1.0f) * p_ps.rnd_pitch);
    } else {
        int32_t f;
        if (!p->on)
            return;
        if (p->prob < 100u && brk_randf(&rng) * 100.0f >= (float)p->prob)
            return;
        f = follow_slice(b, (uint32_t)step);
        if (p->slice >= 0)
            s = p->slice;
        else if (f >= 0)
            s = generate(b, f);
        else
            return;
        s = fold(b, s);
        if (p->flags & STEP_REVERSE)
            rev ^= 1;
        step_pitch = (float)p->pitch;
        if (p->ratchet > 1u) {           /* the step's ratchet: the rest of the hits inside the step */
            rat_count = p->ratchet - 1u;
            rat_len = (float)step_in_len / (float)p->ratchet;
            rat_left = rat_len;
            rat_slice = s;
            rat_rev = rev;
            rat_pitch = step_pitch;
        }
    }
    K0->pitch = 0.0f;                    /* the loop's own sound now, not a key's */
    {
        float keep = step_pitch;         /* (trigger leaves it; the random step set its own) */
        trigger(b, s, rev);
        step_pitch = keep;
    }
    if (roll_arm)
        roll_begin(b);
}

/* pads pressed and let go since the last render */
static void pads_changed(const brk_break_t *b, uint32_t now, float sl)
{
    uint32_t on = now & ~pads_now, off = pads_now & ~now;
    voice_t *c = &v[cur];
    uint32_t rolls = now & (1u << PAD_ROLL8 | 1u << PAD_ROLL16 | 1u << PAD_ROLL32), k;
    pads_now = now;
    for (k = 0; k < 3u; k++)             /* the roll pad pressed last sets the rate */
        if ((on >> k) & 1u)
            roll_pad = (int32_t)k;
    if (roll_pad < 0 || !((rolls >> roll_pad) & 1u))
        for (roll_pad = -1, k = 0; k < 3u; k++)
            if ((rolls >> k) & 1u)
                roll_pad = (int32_t)k;
    if (rolls) {
        roll_len = ROLL_STEPS[p_ps.roll_div[roll_pad] % BRK_ROLL_DIVS] * sl;
        if (roll_slice < 0 && !roll_arm) {
            if (p_playing)
                roll_arm = 1u;           /* on the grid: from the next step */
            else if (c->active) {        /* free play: from the slice's start, now */
                roll_begin(b);
                roll_left = 0.0f;
            }
        }
    } else {
        roll_slice = -1;
        roll_arm = 0u;
    }
    if (on & 1u << PAD_REVERSE && c->active) {   /* reverse flips the voice playing now */
        c->dir = -1.0f;
        c->lo = (float)b->slice[c->slice];
    }
    if (off & 1u << PAD_REVERSE && c->active) {
        c->dir = 1.0f;
        c->hi = (float)b->len;
    }
    if (on & 1u << PAD_FREEZE && c->active) {   /* freeze: (part of) the slice playing loops until let go */
        c->lo = (float)b->slice[c->slice];
        c->hi = c->lo + (float)((b->slice[c->slice + 1] - b->slice[c->slice]) >> (p_ps.frz_len & 3u));
        c->loop = 1;
        c->gate_left = -1.0f;            /* (held: no gate) */
    }
    if (off & 1u << PAD_FREEZE)
        v[0].loop = v[1].loop = 0;
}

static inline float grain_at(const brk_break_t *b, float p)   /* (round the break: a loop's grains run on) */
{
    float n = (float)b->len;
    if (p >= n)
        p -= n;
    else if (p < 0.0f)
        p += n;
    return hermite(b, p);
}

/* one sample of a voice: inc its speed, pf the pitch to read it at (1: as it runs). KEEP LENGTH with a pitch: two
 * grains half a cycle apart, each a triangle (the two sum to one), each reading pf times as fast from where the voice
 * was when it began; a grain begins again at the foot of its triangle, so nothing clicks */
static void voice_tick(const brk_break_t *b, voice_t *o, float inc, float pf, float *acc, float *ea, float *va)
{
    float x;
    if (!o->active)
        return;
    if (pf > 0.9999f && pf < 1.0001f)
        x = hermite(b, o->pos);
    else {
        float g = p_grain, half = 0.5f * g, ph0 = o->g_age / g, ph1 = ph0 + 0.5f, step = inc * pf * o->dir;
        float w0, w1, age;
        if (ph1 >= 1.0f)
            ph1 -= 1.0f;
        w0 = 1.0f - (ph0 > 0.5f ? 2.0f * ph0 - 1.0f : 1.0f - 2.0f * ph0);
        w1 = 1.0f - (ph1 > 0.5f ? 2.0f * ph1 - 1.0f : 1.0f - 2.0f * ph1);
        x = grain_at(b, o->g_pos[0]) * w0 + grain_at(b, o->g_pos[1]) * w1;
        o->g_pos[0] += step;
        o->g_pos[1] += step;
        age = o->g_age + 1.0f;
        if (o->g_age < half && age >= half)
            o->g_pos[1] = o->pos;        /* grain 1 at its foot: again from the voice */
        if (age >= g) {
            age -= g;
            o->g_pos[0] = o->pos;        /* grain 0 at its foot */
        }
        o->g_age = age;
    }
    x = voice_fx_run(o, x) * o->amp;     /* the slice's own effect and level, then the voice's fade */
    *acc += x;
    *ea += x * o->echo;
    *va += x * o->verb;
    o->pos += inc * o->dir;
    if (o->gate_left >= 0.0f) {          /* gated: once it has played its share, it fades */
        o->gate_left -= inc;
        if (o->gate_left < 0.0f && o->amp_to > 0.0f) {
            o->amp_to = 0.0f;
            o->amp_step = o->amp / GATE_FADE;
            if (o->amp_step <= 0.0f)
                o->amp_step = 1.0f / GATE_FADE;
        }
    }
    if (o->dir > 0.0f ? o->pos >= o->hi : o->pos < o->lo) {
        if (o->wrap && !o->loop && o->dir > 0.0f && o->hi >= (float)b->len)
            o->pos -= (float)b->len;     /* a loop player: round again */
        else if (o->loop && o->hi > o->lo + 1.0f)
            o->pos += (o->dir > 0.0f ? -1.0f : 1.0f) * (o->hi - o->lo);
        else {
            o->pos = o->dir > 0.0f ? o->hi - 1.0f : o->lo;
            if (o->amp_to > 0.0f) {      /* the end: a short fade instead of a click */
                o->amp_to = 0.0f;
                o->amp_step = o->amp / FADE_IN;
            }
        }
    }
    if (o->amp < o->amp_to) {
        o->amp += o->amp_step;
        if (o->amp > o->amp_to)
            o->amp = o->amp_to;
    } else if (o->amp > o->amp_to) {
        o->amp -= o->amp_step;
        if (o->amp <= 0.0f) {
            o->amp = 0.0f;
            o->active = 0;
        }
    }
}

static const brk_break_t *swap_now(void);
void brk_render(float *out, uint32_t n)
{
    const brk_break_t *b = brk_now;
    uint32_t i, pads = p_pads;
    float sl = step_len(), inc, pf, half, ratio, sw = brk_clampf(p_swing, 0.5f, 0.75f);
    int playing = p_playing, keep = p_keep;
    n_renders++;
    if (p_swap && (!playing || !b))      /* stopped: a break waiting takes over at once */
        b = swap_now();
    if (!b || !b->len || !b->nslices) {
        for (i = 0; i < n; i++) {
            out[i] = 0.0f;
            if (send_echo)
                send_echo[i] = 0.0f;
            if (send_verb)
                send_verb[i] = 0.0f;
        }
        return;
    }
    {                                    /* role hits asked (MIDI's drum notes): now */
        uint32_t r;
        for (r = 0; r < MC_ROLES; r++)
            if (p_rhit[r] != rhit_seen[r]) {
                rhit_seen[r] = p_rhit[r];
                role_hit(b, r);
            }
    }
    if (p_reset != reset_seen) {
        reset_seen = p_reset;
        step = -1;
        bar = 0u;
        to_next = 0.0f;
        steps_total = 0u;
    }
    clk_cnt = 0u;
    ev_cnt = 0u;
    if (was_playing && !playing) {       /* stopped: the break fades out (2 ms), but a key held loops on */
        uint32_t k, j;
        roll_slice = -1;
        roll_arm = 0u;
        rat_count = 0u;
        mc_cur = -1;                     /* the scenes stop with the transport */
        for (j = 0; j < MC_LANES; j++)
            mr_count[j] = 0u;
        for (j = 0; j < 2u; j++) {
            kl[j].pending = BRK_NO_SLICE;
            if (kl[j].slice < 0)
                for (k = 0; k < 2u; k++) {
                    voice_t *o = &kl[j].pv[k];
                    if (o->active) {
                        o->amp_to = 0.0f;
                        o->amp_step = o->amp / FADE_OUT + 1e-6f;
                        o->loop = o->wrap = 0u;
                    }
                }
        }
    }
    was_playing = playing;
    while (q_r != q_w) {                 /* the keys */
        kev_t e = q[q_r % QLEN];
        q_r++;
        if (e.slice < 0 || (uint32_t)e.slice >= b->nslices || e.slice >= 32)
            continue;
        if (e.down) {                    /* playing: quantised, the next snap point; stopped: now */
            lane_t *k = lane_for(b, e.slice);
            held_mask |= 1u << e.slice;
            key_press(k, b, e.slice, playing, sl);
        } else {
            uint32_t j;
            held_mask &= ~(1u << e.slice);
            for (j = 0; j < 2u; j++) {   /* the key looping let go: another still held takes over, or none */
                lane_t *k = &kl[j], *o = &kl[j ^ 1u];
                uint32_t m = held_mask;
                if (e.slice != k->slice)
                    continue;
                key_let_go(k, b);
                if (o->slice >= 0)       /* (not the other lane's key) */
                    m &= ~(1u << o->slice);
                if (o->pending >= 0)
                    m &= ~(1u << o->pending);
                if (j) {                 /* the layer: a key of its own half */
                    int32_t t;
                    for (t = 0; t < 32; t++)
                        if (half_of(b, t) != half_of(b, e.slice))
                            m &= ~(1u << t);
                }
                if (m) {
                    int32_t t = 0;
                    while (!((m >> t) & 1u))
                        t++;
                    key_press(k, b, t, playing, sl);
                }
            }
        }
    }
    if (pads != pads_now)
        pads_changed(b, pads, sl);
    if (roll_pad >= 0 && roll_slice >= 0)    /* the settings, followed while the pads are held */
        roll_len = ROLL_STEPS[p_ps.roll_div[roll_pad] % BRK_ROLL_DIVS] * sl;
    if (((pads_now >> PAD_FREEZE) & 1u) && v[cur].loop && v[cur].slice >= 0 && v[cur].dir > 0.0f) {
        voice_t *c = &v[cur];
        c->hi = c->lo + (float)((b->slice[c->slice + 1] - b->slice[c->slice]) >> (p_ps.frz_len & 3u));
        if (c->pos >= c->hi)
            c->pos = c->lo;
    }
    half = (pads >> PAD_HALF) & 1u ? brk_clampf(p_ps.half_speed, 0.25f, 0.75f) : 1.0f;
    if ((pads >> PAD_REVERSE) & 1u)
        half *= brk_clampf(p_ps.rev_speed, 0.5f, 2.0f);
    ratio = (float)b->rate / (float)BRK_SR * (p_tempo * 100.0f / (float)b->bpm_x100);
    for (i = 0; i < n; i++) {
        float acc = 0.0f, ea = 0.0f, va = 0.0f, g;
        if (playing) {
            to_next -= 1.0f;
            if (to_next <= 0.0f) {
                /* swing: the even 16th takes sw of the pair, the odd one the rest */
                float len = 2.0f * sl * ((((uint32_t)(step + 1)) & 1u) ? 1.0f - sw : sw);
                step_in_len = (uint32_t)len;
                if (p_swap) {            /* a break waiting: in on this step (the main loop asks at a bar's end) */
                    b = swap_now();
                    ratio = (float)b->rate / (float)BRK_SR * (p_tempo * 100.0f / (float)b->bpm_x100);
                }
                tick_k = (float)MC_TICKS / (len > 1.0f ? len : 1.0f);
                mc_step();               /* (the scene first: it says whether the loop sounds) */
                on_step(b);
                if (ev_cnt < EV_MAX) {
                    ev_off[ev_cnt] = (uint16_t)i;
                    ev_idx[ev_cnt++] = (uint8_t)(steps_total & 15u);
                }
                steps_total++;
                to_next += len;
                if (!(step & 1)) {       /* MIDI clock: twelve straight ticks over each pair of 16ths */
                    clk_left = 0.0f;
                    clk_k = 0u;
                }
            }
            if (clk_k < 12u) {
                if (clk_left <= 0.0f) {
                    if (clk_cnt < CLK_MAX)
                        clk_off[clk_cnt++] = (uint16_t)i;
                    clk_left += sl / 6.0f;
                    clk_k++;
                }
                clk_left -= 1.0f;
            }
            if (mc_cur >= 0 && steps_total) {   /* the scene's lanes: on the master tick */
                uint32_t sub = (uint32_t)(step_age * tick_k), t, l;
                t = (steps_total - 1u) * MC_TICKS + (sub < MC_TICKS ? sub : MC_TICKS - 1u);
                if (t != mc_last_t) {
                    mc_last_t = t;
                    mc_tick(b, t, sl);
                }
                for (l = 0; l < MC_LANES; l++)   /* their ratchets */
                    if (mr_count[l]) {
                        mr_left[l] -= 1.0f;
                        if (mr_left[l] <= 0.0f) {
                            mr_left[l] += mr_len[l];
                            mr_count[l]--;
                            chop_hit(b, mr_slice[l], mr_gain[l], (mr_flags[l] & MCL_REV) != 0u, mr_gate[l],
                                     mr_pitch[l]);
                        }
                    }
            }
            step_age += 1.0f;
            if (rat_count) {             /* the step's ratchet hits */
                rat_left -= 1.0f;
                if (rat_left <= 0.0f) {
                    rat_left += rat_len;
                    rat_count--;
                    trigger(b, rat_slice, rat_rev);
                    step_pitch = rat_pitch;
                }
            }
        }
        {
            uint32_t j;
            for (j = 0; j < 2u; j++) {   /* (the pads that own the loop's voice hold lane 0, not the layer) */
                lane_t *k = &kl[j];
                int go = j || !suppressed();
                if (k->pending != BRK_NO_SLICE && k->pending_wait >= 0.0f && playing && go) {
                    k->pending_wait -= 1.0f;   /* a snap less than a step (or OFF): its moment */
                    if (k->pending_wait <= 0.0f)
                        key_snapped(k, b);
                }
                if (k->slice >= 0 && go) {   /* the key loop: its window again, every steps 16ths */
                    k->left -= 1.0f;
                    if (k->left <= 0.0f) {   /* (the settings as they are now: a one shot turned back to a loop
                                              * loops again) */
                        key_refresh(k, b, k->slice);
                        if (k->set.mode != KEY_ONESHOT) {
                            k->n++;
                            key_begin(k, b, k->slice);
                        }
                        k->left += k->steps * sl;
                    }
                }
            }
        }
        if (roll_slice >= 0) {           /* the roll: the captured spot again, every roll_len */
            roll_left -= 1.0f;
            if (roll_left <= 0.0f) {
                roll_left += roll_len;
                float dc = brk_clampf(p_ps.roll_decay, -0.9f, 0.9f);
                roll_n++;                /* each repeat at the level, then quieter (the decay) or louder (a swell) */
                voice_start(b, roll_slice, (pads_now >> PAD_REVERSE) & 1u, roll_pos, roll_gain);
                if (dc >= 0.0f)
                    roll_gain *= 1.0f - dc;
                else
                    roll_gain += (1.0f - roll_gain) * -dc;
                if (roll_gain < 0.02f)
                    roll_gain = 0.02f;
            }
        }
        /* tape stop: stop_beats down to nothing, spin_steps back up */
        if ((pads_now >> PAD_TAPESTOP) & 1u) {
            tape -= 1.0f / (brk_clampf(p_ps.stop_beats, 0.25f, 4.0f) * 4.0f * sl);
            if (tape < 0.0f)
                tape = 0.0f;
        } else if (tape < 1.0f) {
            tape += 1.0f / (brk_clampf(p_ps.spin_steps, 0.25f, 8.0f) * sl);
            if (tape > 1.0f)
                tape = 1.0f;
        }
        {
            float fxp = 0.0f;            /* the pads' own pitch: the roll's ramp, the freeze's offset */
            if (roll_slice >= 0)
                fxp += brk_clampf((float)roll_n * p_ps.roll_pitch, -36.0f, 36.0f);
            if ((pads_now >> PAD_FREEZE) & 1u)
                fxp += p_ps.frz_pitch;
            fxp += K0->pitch;
            pf = brk_exp2f((p_pitch + step_pitch + fxp) * (1.0f / 12.0f));
            inc = ratio * half * tape;
            if (!keep) {                 /* SPEED: the pitch is the speed */
                inc *= pf;
                pf = 1.0f;
            }
        }
        voice_tick(b, &v[0], inc, pf, &acc, &ea, &va);
        voice_tick(b, &v[1], inc, pf, &acc, &ea, &va);
        if (lv[0].active | lv[1].active) {   /* the layer: the tempo, HALF, REVERSE's speed, the tape, its own pitch */
            float lpf = brk_exp2f((p_pitch + K1->pitch) * (1.0f / 12.0f)), linc = ratio * half * tape;
            if (!keep) {
                linc *= lpf;
                lpf = 1.0f;
            }
            voice_tick(b, &lv[0], linc, lpf, &acc, &ea, &va);
            voice_tick(b, &lv[1], linc, lpf, &acc, &ea, &va);
        }
        /* the gate pad: open for gate_len of each 1/8, 1/16 or 1/32 */
        g = 1.0f;
        gate_pos += 1.0f;
        if (((pads_now >> PAD_GATE) & 1u) && playing) {
            float per = GATE_STEPS[p_ps.gate_div % BRK_GATE_DIVS] * sl, ph = gate_pos;
            while (ph >= per)
                ph -= per;
            g = ph < brk_clampf(p_ps.gate_len, 0.1f, 0.9f) * per ? 1.0f : 0.0f;
        }
        gate_amp += (g - gate_amp) * 0.02f;
        mute_amp += ((p_mute ? 0.0f : 1.0f) - mute_amp) * 0.02f;   /* (-60 dB in 8 ms) */
        {
            float k = (1.0f / 32768.0f) * gate_amp * mute_amp * (tape < 0.02f ? tape * 50.0f : 1.0f);
            out[i] = acc * k;
            if (send_echo)               /* the slices' throws, for the effects' delay and reverb */
                send_echo[i] = ea * k;
            if (send_verb)
                send_verb[i] = va * k;
        }
    }
    status.step = step;
    status.slice = v[cur].active ? v[cur].slice : -1;
    status.bar = bar;
    status.pos = v[cur].pos;
    status.pads = pads_now;
    status.playing = (uint8_t)playing;
    status.roll_div = (uint8_t)(roll_pad >= 0 ? p_ps.roll_div[roll_pad] % BRK_ROLL_DIVS : 0u);
    status.roll_n = (uint16_t)roll_n;
    status.roll_slice = roll_slice;
    status.tape = tape;
    status.gate = gate_amp;
    status.phase = step_in_len ? brk_clampf(step_age / (float)step_in_len, 0.0f, 1.0f) : 0.0f;
    status.key = K0->slice;
    status.key_lo = K0->lo;
    status.key_w = K0->w;
    status.key_phase = K0->slice >= 0 && K0->steps > 0.0f
                       ? brk_clampf(1.0f - K0->left / (K0->steps * sl), 0.0f, 1.0f) : 0.0f;
    status.key_n = (uint16_t)K0->n;
    status.layer = K1->slice;
    status.layer_lo = K1->lo;
    status.layer_w = K1->w;
    status.layer_phase = K1->slice >= 0 && K1->steps > 0.0f
                         ? brk_clampf(1.0f - K1->left / (K1->steps * sl), 0.0f, 1.0f) : 0.0f;
    status.layer_pos = lv[lcur].active ? lv[lcur].pos : -1.0f;
    status.half = (int8_t)half_on;
    status.key_rev = (uint8_t)(v[cur].dir < 0.0f);
    status.bar16 = steps_total ? (int8_t)((steps_total - 1u) & 15u) : (int8_t)-1;
    status.mc_scene = (int8_t)mc_cur;
    status.mc_next = (int8_t)(p_mc_jn != mc_jn_seen ? p_mc_jump : p_mc_next);   /* (a launch waiting: that) */
    status.mc_step = (uint16_t)mc_steps;
    status.mc_adv = mc_adv;
    status.mc_ended = mc_ended;
    status.mc_missing = mc_miss;
    for (i = 0; i < MC_LANES; i++) {
        status.mc_pos[i] = mc_pos[i];
        status.mc_hits[i] = mc_hitc[i];
    }
}

/* -------------------------------------------------------------- main loop --- */
BRK_COLD static void pattern_defaults(void)
{
    uint32_t i;
    for (i = 0; i < BRK_MAX_STEPS; i++) {
        pat[i].slice = BRK_NO_SLICE;
        pat[i].on = 1u;
        pat[i].pitch = 0;
        pat[i].flags = 0u;
        pat[i].ratchet = 1u;
        pat[i].prob = 100u;
    }
}

/* boot (and the tests): everything back to the start, the render's state too (call it with no render running) */
BRK_COLD void brk_init(void)
{
    pattern_defaults();
    v[0].active = v[1].active = lv[0].active = lv[1].active = 0u;
    v[0].loop = v[1].loop = lv[0].loop = lv[1].loop = 0u;
    {
        uint32_t l;
        for (l = 0; l < MC_LANES; l++) {
            mr_count[l] = 0u;
            mr_hold[l] = 0u;
            mc_n[l] = 0u;
            mc_pos[l] = 0u;
        }
        for (l = 0; l < BRK_MAX_SLICES; l++)
            p_roles[l] = 0u;
        p_dl[0] = p_dl[1] = p_dl[2] = 0u;
        mc_cur = -1;
        mc_go_seen = p_mc_go;
        mc_jn_seen = p_mc_jn;
        p_mc_next = -1;
        p_mc_mute = p_mc_solo = 0u;
        mc_miss = 0u;
        for (l = 0; l < MC_ROLES; l++)
            rhit_seen[l] = p_rhit[l];
    }
    step = -1;
    last_slice = -1;
    {
        uint32_t j;
        for (j = 0; j < 2u; j++) {
            kl[j].slice = -1;
            kl[j].pending = BRK_NO_SLICE;
            kl[j].pending_wait = -1.0f;
            kl[j].pitch = 0.0f;
            kl[j].n = 0u;
        }
    }
    p_half = half_on = -1;
    bar = held_mask = pads_now = p_pads = 0u;
    p_playing = 0;
    to_next = 0.0f;
    tape = gate_amp = 1.0f;
    roll_slice = -1;
    roll_arm = 0u;
    roll_pad = -1;
    roll_n = 0u;
    roll_gain = 1.0f;
    gate_pos = 0.0f;
    rat_count = 0u;
    step_pitch = 0.0f;
    steps_total = 0u;
    was_playing = 0;
    clk_cnt = 0u;
    clk_k = 12u;
    ev_cnt = 0u;
    {
        brk_keyset_t d;
        uint32_t k;
        brk_keyset_defaults(&d);
        brk_slicefx_t f;
        for (k = 0; k < BRK_KEYS; k++)
            brk_keyset(k, &d);
        brk_slicefx_defaults(&f);
        for (k = 0; k < BRK_KEYS; k++)
            brk_slicefx(k, &f);
    }
    q_r = q_w;
    p_gen.complexity = 0.0f;
    p_gen.anchor = 0.7f;
    p_gen.roll = 0.3f;
    p_gen.fill = 0.5f;
    p_gen.phrase_bars = 4u;
    brk_padset_defaults(&p_ps);
}

BRK_COLD void brk_padset_defaults(brk_padset_t *p)
{
    p->roll_div[0] = 1u;                 /* the three roll pads: 1/8, 1/16, 1/32 */
    p->roll_div[1] = 2u;
    p->roll_div[2] = 3u;
    p->frz_len = 0u;
    p->gate_div = 1u;
    p->roll_level = 100u;
    p->rsv[0] = p->rsv[1] = 0u;
    p->roll_decay = 0.0f;
    p->roll_pitch = 0.0f;
    p->rev_speed = 1.0f;
    p->frz_pitch = 0.0f;
    p->half_speed = 0.5f;
    p->stop_beats = 1.0f;
    p->spin_steps = 1.0f;
    p->gate_len = 0.5f;
    p->rnd_amount = 1.0f;
    p->rnd_pitch = 0.0f;
}

BRK_COLD void brk_padset(const brk_padset_t *p) { p_ps = *p; }

BRK_COLD void brk_slicefx_defaults(brk_slicefx_t *f)
{
    f->type = SLX_OFF;
    f->amount = 50u;
    f->tone = 30u;
    f->level = 100u;
}

BRK_COLD void brk_slicefx(uint32_t slice, const brk_slicefx_t *f)
{
    slxword_t u;
    if (slice >= BRK_KEYS)
        return;
    u.f = *f;
    p_slx[slice] = u.w;                  /* one word */
}

void brk_sends(float *echo, float *verb)
{
    send_echo = echo;
    send_verb = verb;
}

BRK_COLD void brk_keyset_defaults(brk_keyset_t *k)
{
    k->len = 3u;                         /* one slice */
    k->pitch = 0;
    k->start = 0u;
    k->mode = KEY_LOOP;
}

BRK_COLD void brk_keyset(uint32_t key, const brk_keyset_t *k)
{
    keyword_t u;
    if (key >= BRK_KEYS)
        return;
    u.k = *k;
    p_keys[key] = u.w;                   /* one word: the render never sees half of it */
}

BRK_COLD void brk_set_break(const brk_break_t *b) { brk_now = b; }
BRK_COLD void brk_mute(int on) { p_mute = on; }
BRK_COLD void brk_swap_break(const brk_break_t *b) { p_swap = b; }
/* the render: the break waiting takes over, its voices start clean and it sounds again */
static const brk_break_t *swap_now(void)
{
    brk_now = p_swap;
    p_swap = 0;
    v[0].active = v[1].active = lv[0].active = lv[1].active = 0u;
    p_mute = 0;
    mute_amp = 1.0f;
    return brk_now;
}
const brk_break_t *brk_get_break(void) { return brk_now; }
BRK_COLD void brk_play(int on)
{
    if (on && !p_playing)
        p_reset++;                       /* play starts at the pattern's first step */
    p_playing = on ? 1 : 0;
}
BRK_COLD void brk_reset(void) { p_reset++; }
BRK_COLD void brk_tempo(float bpm) { p_tempo = brk_clampf(bpm, 40.0f, 300.0f); }
float brk_get_tempo(void) { return p_tempo; }
BRK_COLD void brk_pitch(float st) { p_pitch = brk_clampf(st, -24.0f, 24.0f); }
BRK_COLD void brk_pitch_mode(int keep_length, float grain_ms)
{
    p_grain = brk_clampf(grain_ms, 10.0f, 80.0f) * ((float)BRK_SR / 1000.0f);
    p_keep = keep_length != 0;
}
BRK_COLD void brk_half(int h) { p_half = h < 0 || h > 1 ? -1 : h; }
int brk_get_half(void) { return p_half; }
BRK_COLD void brk_swing(float s) { p_swing = brk_clampf(s, 0.5f, 0.75f); }
BRK_COLD void brk_gate(float g) { p_gate = brk_clampf(g, 0.05f, 1.0f); }
BRK_COLD void brk_snap(float slices) { p_snap = brk_clampf(slices, 0.0f, 16.0f); }
BRK_COLD void brk_gen(const brk_gen_t *g) { p_gen = *g; }
BRK_COLD void brk_seed(uint32_t seed) { rng = seed ? seed : 1u; }
BRK_COLD void brk_step_set(uint32_t i, const brk_step_t *s)
{
    if (i < BRK_MAX_STEPS)
        pat[i] = *s;
}
BRK_COLD void brk_step_get(uint32_t i, brk_step_t *s)
{
    if (i < BRK_MAX_STEPS)
        *s = pat[i];
}

BRK_COLD void brk_key(int slice, int down)
{
    if (q_w - q_r >= QLEN)
        return;                          /* full: the press is dropped rather than a queue overrun */
    q[q_w % QLEN].slice = (int8_t)slice;
    q[q_w % QLEN].down = (uint8_t)(down != 0);
    __asm__ volatile("" ::: "memory");   /* the event before its index */
    q_w++;
}

BRK_COLD void brk_pad(int pad, int down)
{
    if (pad < 0 || pad >= BRK_NPADS)
        return;
    if (down)
        p_pads |= 1u << pad;
    else
        p_pads &= ~(1u << pad);
}

BRK_COLD void brk_randomize(void)
{
    const brk_break_t *b = brk_now;
    uint32_t i, n;
    if (!b)
        return;
    n = loop_steps(b);
    for (i = 0; i < n; i++) {
        uint32_t in_bar = i % BRK_STEPS_PER_BAR;
        int32_t f = follow_slice(b, i);
        if (in_bar % 4u == 0u)           /* the beats keep the loop's own slices */
            pat[i].slice = (int8_t)(f >= 0 ? f : BRK_NO_SLICE);
        else
            pat[i].slice = (int8_t)(brk_rand(&rng) % b->nslices);
        pat[i].on = 1u;
    }
}

void brk_status(brk_status_t *st) { *st = status; }

/* the Slice Map, heard (MM-SM): the first 60 ms of each slice's hit through three bands, a low one (under ~120 Hz:
 * a kick's body), a middle one (~250 Hz .. 4 kHz: a snare's body and crack) and a high one (over ~7 kHz: hats and
 * cymbals). A slice loud in the low band is a KICK, loud in the middle (and less so low) a SNARE, a quiet one mostly
 * high a HAT; other hits with something in them PERC. Each of KICK, SNARE and HAT gets its likeliest slice at the
 * least, so a break with hits in it always has all three. The same audio gives the same map (every load, every
 * boot): nothing to set by hand */
static float onepole(float hz, uint32_t rate) { return 1.0f - brk_exp2f(-9.0647f * hz / (float)rate); }
BRK_COLD void mc_auto_roles(const brk_break_t *b, uint8_t *roles)
{
    float lo[BRK_MAX_SLICES], mid[BRK_MAX_SLICES], hr[BRK_MAX_SLICES], en[BRK_MAX_SLICES];
    float mxl = 0.0f, mxm = 0.0f, mxh = 0.0f, mxe = 0.0f, a_l, a_m1, a_m2, a_h;
    uint32_t n, s, i, best[3] = {0u, 0u, 0u};
    float bs[3] = {-1.0f, -1.0f, -1.0f};
    for (s = 0; s < BRK_MAX_SLICES; s++)
        roles[s] = 0u;
    if (!b || !b->pcm || !b->nslices || !b->rate)
        return;
    n = b->nslices < BRK_MAX_SLICES ? b->nslices : BRK_MAX_SLICES;
    a_l = onepole(120.0f, b->rate);
    a_m1 = onepole(250.0f, b->rate);
    a_m2 = onepole(4000.0f, b->rate);
    a_h = onepole(7000.0f, b->rate);
    for (s = 0; s < n; s++) {
        uint32_t z = b->slice[s + 1], w = b->rate * 3u / 50u;
        float l1 = 0.0f, m1 = 0.0f, m2 = 0.0f, h1 = 0.0f, el = 0.0f, em = 0.0f, eh = 0.0f, et = 0.0f;
        if (z > b->slice[s] + w)
            z = b->slice[s] + w;
        if (z > b->len)
            z = b->len;
        for (i = b->slice[s]; i < z; i++) {
            float x = (float)b->pcm[i] * (1.0f / 32768.0f), m, h;
            l1 += (x - l1) * a_l;
            m1 += (x - m1) * a_m1;
            m2 += (x - m2) * a_m2;
            h1 += (x - h1) * a_h;
            m = m2 - m1;
            h = x - h1;
            el += l1 * l1;
            em += m * m;
            eh += h * h;
            et += x * x;
        }
        lo[s] = el;
        mid[s] = em;
        en[s] = et;
        hr[s] = et > 1e-9f ? eh / et : 0.0f;
        if (et > mxe)
            mxe = et;
    }
    for (s = 0; s < n; s++) {            /* (the band maxima over the slices with a hit in them) */
        if (en[s] < 0.015f * mxe)
            continue;
        if (lo[s] > mxl)
            mxl = lo[s];
        if (mid[s] > mxm)
            mxm = mid[s];
        if (hr[s] > mxh)
            mxh = hr[s];
    }
    if (mxe <= 1e-9f || mxl <= 0.0f || mxm <= 0.0f || mxh <= 0.0f)
        return;                          /* (silence: no roles) */
    for (s = 0; s < n; s++) {
        float nk = lo[s] / mxl, ns = mid[s] / mxm, nh = hr[s] / mxh, sc[3];
        uint32_t r;
        if (en[s] < 0.015f * mxe)
            continue;
        sc[0] = nk - 0.5f * ns;          /* the likeliness of each, for the best of each */
        sc[1] = ns - 0.7f * nk;
        sc[2] = nh - 0.6f * (nk > ns ? nk : ns);
        for (r = 0; r < 3u; r++)
            if (sc[r] > bs[r]) {
                bs[r] = sc[r];
                best[r] = s;
            }
        if (nk >= 0.45f && nk >= 0.8f * ns)
            roles[s] |= 1u << ROLE_KICK;
        if (ns >= 0.5f && ns > nk)
            roles[s] |= 1u << ROLE_SNARE;
        if (!roles[s] && nh >= 0.6f && nk < 0.3f && ns < 0.5f)
            roles[s] |= 1u << ROLE_HAT;
        if (!roles[s] && en[s] >= 0.08f * mxe)
            roles[s] |= 1u << ROLE_PERC;   /* (a ghost note, a tom, a rim: something to play) */
    }
    for (s = 0; s < 3u; s++)             /* each of the three at least once */
        if (bs[s] > -1.0f)
            roles[best[s]] |= (uint8_t)(1u << s);
    for (s = 0; s < n; s++)              /* PERC empty: the snares' quieter hits do */
        if (roles[s] & 1u << ROLE_PERC)
            return;
    for (s = 0; s < n; s++)
        if (roles[s] & 1u << ROLE_SNARE && s != best[1])
            roles[s] |= 1u << ROLE_PERC;
}

BRK_COLD void brk_roles(const uint8_t *roles, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < BRK_MAX_SLICES; i++)
        p_roles[i] = i < n ? roles[i] : 0u;
}
BRK_COLD void brk_drum_lanes(uint16_t kick, uint16_t snare, uint16_t hat)
{
    p_dl[0] = kick;
    p_dl[1] = snare;
    p_dl[2] = hat;
}
BRK_COLD void brk_role_hit(uint32_t role)
{
    if (role < MC_ROLES)
        p_rhit[role]++;
}
BRK_COLD void brk_mc_scenes(const mc_scene_t *s) { p_scenes = s; }
BRK_COLD void brk_mc_start(int scene)
{
    p_mc_start = scene;
    __asm__ volatile("" ::: "memory");   /* (the scene before the count) */
    p_mc_go++;
}
BRK_COLD void brk_mc_next(int scene) { p_mc_next = scene; }
BRK_COLD void brk_mc_jump(int scene, uint32_t quant)
{
    p_mc_jump = scene;
    p_mc_jq = quant ? quant : 16u;
    __asm__ volatile("" ::: "memory");   /* (the scene before the count) */
    p_mc_jn++;
}
BRK_COLD void brk_mc_macro(uint32_t density, uint32_t math, uint32_t speed, uint32_t chaos)
{
    p_mc_macro = (density > 100u ? 100u : density) | (math > 100u ? 100u : math) << 8 |
                 (speed > 100u ? 100u : speed) << 16 | (chaos > 100u ? 100u : chaos) << 24;
}
BRK_COLD void brk_mc_mute(uint32_t mute, uint32_t solo)
{
    p_mc_mute = mute;
    p_mc_solo = solo;
}
uint32_t brk_renders(void) { return n_renders; }

uint32_t brk_clock_ticks(uint16_t *off, uint32_t max)
{
    uint32_t i, n = clk_cnt < max ? clk_cnt : max;
    for (i = 0; i < n; i++)
        off[i] = clk_off[i];
    return n;
}

uint32_t brk_step_events(uint16_t *off, uint8_t *idx, uint32_t max)
{
    uint32_t i, n = ev_cnt < max ? ev_cnt : max;
    for (i = 0; i < n; i++) {
        off[i] = ev_off[i];
        idx[i] = ev_idx[i];
    }
    return n;
}

void brk_sync(uint32_t n16, float age)
{
    float sl = step_len(), len, pos, err;
    if (!p_playing || !steps_total || !step_in_len)
        return;
    len = (float)step_in_len;
    pos = (float)(steps_total - 1u) + (len - to_next) / len;   /* where the loop is, in 16ths since its start */
    err = pos - ((float)n16 + age / sl);                        /* > 0: the loop is ahead */
    if (err > 1.5f || err < -1.5f) {     /* far off (a start missed, a tempo jump): the next step is the clock's */
        uint32_t n = loop_steps(brk_now);
        uint32_t want = n16 + (uint32_t)(age / sl) + 1u;   /* the 16th that begins next */
        step = (int32_t)((want + n - 1u) % n);   /* on_step's +1 makes it want's place in the pattern */
        if (half_on >= 0)                /* one half alone: its place in that bar */
            step = (int32_t)((uint32_t)half_on * (n / 2u) + ((uint32_t)step + 1u) % (n / 2u) + n - 1u) % (int32_t)n;
        steps_total = want;              /* and the ++ after it makes the count want + 1 (number want playing) */
        to_next = 1.0f;
        clk_k = 12u;
        return;
    }
    to_next += err * sl * 0.3f;          /* near: eased, a third of the way a beat */
    if (to_next < 1.0f)
        to_next = 1.0f;
}
uint32_t brk_step_len_samples(void) { return (uint32_t)step_len(); }
