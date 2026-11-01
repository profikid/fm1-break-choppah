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
static volatile float p_tempo = 136.9f, p_pitch, p_swing = 0.5f;
static volatile int p_playing;
static volatile uint32_t p_pads;         /* bit PAD_*: held */
static volatile uint32_t p_reset;        /* a count: changed = reset at the next render */
static volatile uint32_t p_rewind;       /* a count: changed = a rewind starts */
static volatile uint32_t p_len;          /* pattern length, 0 = the break's own */
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
const float BRK_KEY_SLICES[BRK_KEY_LENS] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
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
static int32_t step = -1, last_slice = -1, pending = BRK_NO_SLICE;
static uint32_t pending_at;              /* the snap: the step (counted from the reset) the key pressed begins on */
static float pending_wait = -1.0f;       /* or, less than a step apart, the samples until it does (< 0: a step) */
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
static uint32_t rewind_seen, rewind_n, rewind_len;   /* the pull-up: samples into it, its length (0: none) */
static float rat_len, rat_left;
static uint32_t rat_count;
static int32_t rat_slice;
static int rat_rev;
static float rat_pitch, step_pitch;
static uint32_t step_in_len;             /* the length of the step playing, for the gate */
static float step_age;
static brk_status_t status;
/* the key loop (brk_keyset): the key held last loops its window, restarting every key_steps 16ths */
static int32_t key_slice = -1;           /* the key looping, -1 none */
static brk_keyset_t key_set;             /* its settings, as the repeat playing began */
static float key_steps;                  /* its length in 16ths of the tempo */
static float key_left;                   /* samples to the next repeat */
static float key_lo, key_w;              /* its window in the break's samples */
static float key_pitch;                  /* semitones, while a key's sound plays */
static uint32_t key_n;                   /* repeats since the key went down */
/* MIDI clock */
#define CLK_MAX 32u
static uint16_t clk_off[CLK_MAX];        /* the last render's ticks: their sample offsets */
static uint32_t clk_cnt;
static float clk_left;                   /* samples to the next tick */
static uint32_t clk_k = 12u;             /* ticks given in this pair of 16ths (12: done until the next pair) */
static uint32_t steps_total;             /* steps begun since the reset (the outside clock counts from its start) */
static int was_playing;                  /* the transport at the last render: a stop fades the break out */
static volatile uint32_t n_renders;

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
    uint32_t n = p_len ? p_len : (b ? b->bars : 1u) * BRK_STEPS_PER_BAR;
    return n < 1u ? 1u : n > BRK_MAX_STEPS ? BRK_MAX_STEPS : n;
}

/* a voice's slice effect, from the slice's settings as it starts */
static void voice_fx(voice_t *n, const brk_break_t *b, int32_t s)
{
    slxword_t u;
    float a, t;
    (void)b;
    u.w = (s >= 0 && (uint32_t)s < BRK_KEYS) ? p_slx[s] : 0x64000000u;   /* (off, level 100) */
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

static void voice_start(const brk_break_t *b, int32_t s, int rev, float at, float gain)
{
    voice_t *o = &v[cur], *n;
    if (o->active) {                     /* the old voice fades out */
        o->amp_to = 0.0f;
        o->amp_step = o->amp / FADE_OUT;
    }
    cur ^= 1;
    n = &v[cur];
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
    last_slice = s;
}

static void trigger(const brk_break_t *b, int32_t s, int rev)
{
    if (!b || s < 0 || (uint32_t)s >= b->nslices)
        return;
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
static void key_refresh(const brk_break_t *b, int32_t s)
{
    key_set = keyset_of(s);
    key_steps = BRK_KEY_SLICES[key_set.len] * (float)(b->bars * BRK_STEPS_PER_BAR) / (float)b->nslices;
}

/* a repeat of key s's loop: its window from the start, forwards or backwards (the mode, the REVERSE pad) */
static void key_begin(const brk_break_t *b, int32_t s)
{
    float ns = (float)b->len / (float)(b->bars * BRK_STEPS_PER_BAR);         /* a 16th in the break's samples */
    float hi;
    int rev;
    voice_t *n;
    if (s < 0 || (uint32_t)s >= b->nslices)
        return;
    key_refresh(b, s);
    key_lo = (float)b->slice[s] + (float)(key_set.start & 15u) * (1.0f / 16.0f) * (float)(b->slice[s + 1] - b->slice[s]);
    key_w = key_steps * ns;
    if (key_w < 32.0f)
        key_w = 32.0f;
    rev = key_set.mode == KEY_REVERSE || (key_set.mode == KEY_PINGPONG && (key_n & 1u));
    rev ^= (int)((pads_now >> PAD_REVERSE) & 1u);
    hi = key_lo + key_w > (float)b->len ? (float)b->len : key_lo + key_w;
    voice_start(b, s, rev, rev ? hi - 1.0f : key_lo, 1.0f);
    n = &v[cur];
    n->lo = key_lo;
    if (rev) {                           /* backwards: round the window until the next repeat */
        n->hi = hi;
        n->loop = (uint8_t)(key_set.mode != KEY_ONESHOT);
        n->wrap = 0u;
    } else if (key_set.mode == KEY_ONESHOT) {   /* once through the window, then quiet */
        n->hi = hi;
        n->wrap = 0u;
    } else {                             /* forwards it runs on (round the break's end) until the next repeat */
        n->hi = (float)b->len;
        n->wrap = 1u;
    }
    key_pitch = (float)key_set.pitch;
}

/* the key let go (or tapped): playing, the sound runs on into the loop; stopped, it ends with its window */
static void key_let_go(const brk_break_t *b)
{
    voice_t *c = &v[cur];
    key_slice = -1;
    if (!c->active)
        return;
    c->loop = 0u;
    if (c->dir > 0.0f) {
        c->hi = p_playing ? (float)b->len : (key_lo + key_w > (float)b->len ? (float)b->len : key_lo + key_w);
        c->wrap = (uint8_t)(p_playing != 0);
    }
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
    if (rewind_len)
        return 1;
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
static void key_snapped(const brk_break_t *b)
{
    int32_t s = pending;
    pending = BRK_NO_SLICE;
    pending_wait = -1.0f;
    key_n = 0u;
    key_begin(b, s);
    key_left = key_steps * step_len();
    if ((held_mask >> s) & 1u)
        key_slice = s;
    else
        key_let_go(b);                   /* a tap: one hit */
}

/* a key pressed while playing: where it begins (brk_snap). Whole steps wait for that step (the swing's); less than a
 * step counts samples, straight. now_len: the step playing's length */
static void key_snap(const brk_break_t *b, int32_t s)
{
    float sps = (float)(b->bars * BRK_STEPS_PER_BAR) / (float)b->nslices, snap = p_snap * sps, pos, target;
    pending = s;
    pending_wait = -1.0f;
    if (!steps_total || !step_in_len) {  /* not begun yet: the first step */
        pending_at = steps_total;
        return;
    }
    pos = (float)(steps_total - 1u) + ((float)step_in_len - to_next) / (float)step_in_len;   /* in 16ths */
    if (snap <= 0.0f) {                  /* OFF: now */
        pending_wait = 0.0f;
        return;
    }
    target = snap * (float)((uint32_t)(pos / snap) + 1u);    /* the next grid point after now */
    if (snap >= 0.999f && target - (float)(uint32_t)(target + 0.001f) < 0.002f)
        pending_at = (uint32_t)(target + 0.5f);   /* a whole step: on_step begins it */
    else
        pending_wait = (target - pos) * step_len();
}

static void on_step(const brk_break_t *b)
{
    uint32_t n = loop_steps(b);
    const brk_step_t *p;
    int32_t s = BRK_NO_SLICE;
    int rev = (pads_now >> PAD_REVERSE) & 1u;
    step = (int32_t)(((uint32_t)(step + 1)) % n);
    if (step == 0)
        bar++;
    step_age = 0.0f;
    rat_count = 0u;
    step_pitch = 0.0f;
    if (p_ps.gate_div != 0u || !(step & 1))
        gate_pos = 0.0f;                 /* the gate starts with the step (1/8: every other step) */
    if (suppressed())
        return;
    p = &pat[step];
    if (pending != BRK_NO_SLICE && pending_wait < 0.0f && steps_total >= pending_at) {   /* the snap point */
        key_snapped(b);
        if (roll_arm)
            roll_begin(b);
        return;
    }
    if (key_slice >= 0) {                /* a key looping owns the voice: the sequencer counts on, quiet */
        if (roll_arm)
            roll_begin(b);
        return;
    }
    if (((pads_now >> PAD_RANDOM) & 1u) && brk_randf(&rng) < p_ps.rnd_amount) {
        s = (int32_t)(brk_rand(&rng) % b->nslices);   /* a random slice, and a random pitch for it */
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
    key_pitch = 0.0f;                    /* the loop's own sound now, not a key's */
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

void brk_render(float *out, uint32_t n)
{
    const brk_break_t *b = brk_now;
    uint32_t i, pads = p_pads;
    float sl = step_len(), inc, pf, half, ratio, sw = brk_clampf(p_swing, 0.5f, 0.75f);
    int playing = p_playing, keep = p_keep;
    n_renders++;
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
    if (p_reset != reset_seen) {
        reset_seen = p_reset;
        step = -1;
        bar = 0u;
        to_next = 0.0f;
        steps_total = 0u;
    }
    clk_cnt = 0u;
    if (was_playing && !playing) {       /* stopped: the break fades out (2 ms), but a key held loops on */
        uint32_t k;
        pending = BRK_NO_SLICE;
        roll_slice = -1;
        roll_arm = 0u;
        rat_count = 0u;
        if (key_slice < 0)
            for (k = 0; k < 2u; k++)
                if (v[k].active) {
                    v[k].amp_to = 0.0f;
                    v[k].amp_step = v[k].amp / FADE_OUT + 1e-6f;
                    v[k].loop = v[k].wrap = 0u;
                }
    }
    was_playing = playing;
    while (q_r != q_w) {                 /* the keys */
        kev_t e = q[q_r % QLEN];
        q_r++;
        if (e.slice < 0 || (uint32_t)e.slice >= b->nslices || e.slice >= 32)
            continue;
        if (e.down) {
            held_mask |= 1u << e.slice;
            if (playing)
                key_snap(b, e.slice);    /* quantised: the next snap point */
            else {                       /* free play: now, looping while held */
                key_n = 0u;
                key_begin(b, e.slice);
                key_left = key_steps * sl;
                key_slice = e.slice;
            }
        } else {
            held_mask &= ~(1u << e.slice);
            if (e.slice == key_slice) {  /* the key looping let go: another still held takes over, or none */
                key_let_go(b);
                if (held_mask) {
                    int32_t k = 0;
                    while (!((held_mask >> k) & 1u))
                        k++;
                    if (playing)
                        key_snap(b, k);
                    else {
                        key_n = 0u;
                        key_begin(b, k);
                        key_left = key_steps * sl;
                        key_slice = k;
                    }
                }
            }
        }
    }
    if (pads != pads_now)
        pads_changed(b, pads, sl);
    if (p_rewind != rewind_seen) {       /* a rewind: the voice playing runs backwards round the whole break */
        voice_t *c = &v[cur];
        rewind_seen = p_rewind;
        rewind_n = 0u;
        rewind_len = (uint32_t)(0.7f * (float)BRK_SR);
        if (c->active) {
            c->lo = 0.0f;
            c->hi = (float)b->len;
            c->dir = -1.0f;
            c->loop = 1u;
            c->wrap = 0u;
        }
    }
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
                on_step(b);
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
        if (pending != BRK_NO_SLICE && pending_wait >= 0.0f && playing && !suppressed()) {
            pending_wait -= 1.0f;        /* a snap less than a step (or OFF): its moment */
            if (pending_wait <= 0.0f)
                key_snapped(b);
        }
        if (key_slice >= 0 && !suppressed()) {   /* the key loop: its window again, every key_steps 16ths */
            key_left -= 1.0f;
            if (key_left <= 0.0f) {      /* (the settings as they are now: a one shot turned back to a loop
                                          * loops again) */
                key_refresh(b, key_slice);
                if (key_set.mode != KEY_ONESHOT) {
                    key_n++;
                    key_begin(b, key_slice);
                }
                key_left += key_steps * sl;
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
                fxp += brk_clampf((float)roll_n * p_ps.roll_pitch, -24.0f, 24.0f);
            if ((pads_now >> PAD_FREEZE) & 1u)
                fxp += p_ps.frz_pitch;
            fxp += key_pitch;
            pf = brk_exp2f((p_pitch + step_pitch + fxp) * (1.0f / 12.0f));
            inc = ratio * half * tape;
            if (!keep) {                 /* SPEED: the pitch is the speed */
                inc *= pf;
                pf = 1.0f;
            }
        }
        if (rewind_len) {                /* the pull-up: up to ten times as fast, then the top of the bar */
            float r = (float)rewind_n / (float)rewind_len;
            inc *= 1.0f + 9.0f * r * r;
            if (++rewind_n >= rewind_len) {
                rewind_len = 0u;
                v[0].loop = v[1].loop = 0u;
                if (v[cur].active) {
                    v[cur].amp_to = 0.0f;
                    v[cur].amp_step = v[cur].amp / FADE_OUT;
                }
                step = -1;               /* the next sample starts step 0 */
                to_next = 0.0f;
            }
        }
        voice_tick(b, &v[0], inc, pf, &acc, &ea, &va);
        voice_tick(b, &v[1], inc, pf, &acc, &ea, &va);
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
        {
            float k = (1.0f / 32768.0f) * gate_amp * (tape < 0.02f ? tape * 50.0f : 1.0f);
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
    status.rewind = rewind_len ? (float)rewind_n / (float)rewind_len : 0.0f;
    status.key = key_slice;
    status.key_lo = key_lo;
    status.key_w = key_w;
    status.key_phase = key_slice >= 0 && key_steps > 0.0f
                       ? brk_clampf(1.0f - key_left / (key_steps * sl), 0.0f, 1.0f) : 0.0f;
    status.key_n = (uint16_t)key_n;
    status.key_rev = (uint8_t)(v[cur].dir < 0.0f);
}

/* -------------------------------------------------------------- main loop --- */
static void pattern_defaults(void)
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
void brk_init(void)
{
    pattern_defaults();
    v[0].active = v[1].active = 0u;
    v[0].loop = v[1].loop = 0u;
    step = -1;
    last_slice = -1;
    pending = BRK_NO_SLICE;
    pending_wait = -1.0f;
    bar = held_mask = pads_now = p_pads = 0u;
    p_playing = 0;
    p_len = 0u;
    to_next = 0.0f;
    tape = gate_amp = 1.0f;
    roll_slice = -1;
    roll_arm = 0u;
    roll_pad = -1;
    roll_n = 0u;
    roll_gain = 1.0f;
    gate_pos = 0.0f;
    rewind_len = rewind_n = 0u;
    rewind_seen = p_rewind;
    rat_count = 0u;
    step_pitch = 0.0f;
    key_slice = -1;
    key_pitch = 0.0f;
    key_n = 0u;
    steps_total = 0u;
    was_playing = 0;
    clk_cnt = 0u;
    clk_k = 12u;
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

void brk_padset_defaults(brk_padset_t *p)
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

void brk_padset(const brk_padset_t *p) { p_ps = *p; }

void brk_slicefx_defaults(brk_slicefx_t *f)
{
    f->type = SLX_OFF;
    f->amount = 50u;
    f->tone = 30u;
    f->level = 100u;
}

void brk_slicefx(uint32_t slice, const brk_slicefx_t *f)
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

void brk_keyset_defaults(brk_keyset_t *k)
{
    k->len = 3u;                         /* one slice */
    k->pitch = 0;
    k->start = 0u;
    k->mode = KEY_LOOP;
}

void brk_keyset(uint32_t key, const brk_keyset_t *k)
{
    keyword_t u;
    if (key >= BRK_KEYS)
        return;
    u.k = *k;
    p_keys[key] = u.w;                   /* one word: the render never sees half of it */
}

void brk_set_break(const brk_break_t *b) { brk_now = b; }
const brk_break_t *brk_get_break(void) { return brk_now; }
void brk_play(int on)
{
    if (on && !p_playing)
        p_reset++;                       /* play starts at the pattern's first step */
    p_playing = on ? 1 : 0;
}
void brk_reset(void) { p_reset++; }
void brk_rewind(void) { p_rewind++; }
void brk_tempo(float bpm) { p_tempo = brk_clampf(bpm, 40.0f, 300.0f); }
float brk_get_tempo(void) { return p_tempo; }
void brk_pitch(float st) { p_pitch = brk_clampf(st, -24.0f, 24.0f); }
void brk_pitch_mode(int keep_length, float grain_ms)
{
    p_grain = brk_clampf(grain_ms, 10.0f, 80.0f) * ((float)BRK_SR / 1000.0f);
    p_keep = keep_length != 0;
}
void brk_swing(float s) { p_swing = brk_clampf(s, 0.5f, 0.75f); }
void brk_gate(float g) { p_gate = brk_clampf(g, 0.05f, 1.0f); }
void brk_snap(float slices) { p_snap = brk_clampf(slices, 0.0f, 16.0f); }
void brk_gen(const brk_gen_t *g) { p_gen = *g; }
void brk_seed(uint32_t seed) { rng = seed ? seed : 1u; }
void brk_pattern_len(uint32_t len) { p_len = len > BRK_MAX_STEPS ? BRK_MAX_STEPS : len; }
void brk_step_set(uint32_t i, const brk_step_t *s)
{
    if (i < BRK_MAX_STEPS)
        pat[i] = *s;
}
void brk_step_get(uint32_t i, brk_step_t *s)
{
    if (i < BRK_MAX_STEPS)
        *s = pat[i];
}
void brk_pattern_clear(void) { pattern_defaults(); }

void brk_key(int slice, int down)
{
    if (q_w - q_r >= QLEN)
        return;                          /* full: the press is dropped rather than a queue overrun */
    q[q_w % QLEN].slice = (int8_t)slice;
    q[q_w % QLEN].down = (uint8_t)(down != 0);
    __asm__ volatile("" ::: "memory");   /* the event before its index */
    q_w++;
}

void brk_pad(int pad, int down)
{
    if (pad < 0 || pad >= BRK_NPADS)
        return;
    if (down)
        p_pads |= 1u << pad;
    else
        p_pads &= ~(1u << pad);
}

void brk_randomize(void)
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
uint32_t brk_renders(void) { return n_renders; }

uint32_t brk_clock_ticks(uint16_t *off, uint32_t max)
{
    uint32_t i, n = clk_cnt < max ? clk_cnt : max;
    for (i = 0; i < n; i++)
        off[i] = clk_off[i];
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
