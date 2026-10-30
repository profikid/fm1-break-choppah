/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's break engine (brk.h): the sequencer, the generator, the pads and two crossfading slice voices. */
#include "brk.h"
#include "brk_math.h"

#define FADE_IN 44.0f                    /* samples: 1 ms in, 2 ms out on every cut (PRD A-6) */
#define FADE_OUT 88.0f
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
static brk_gen_t p_gen;
static brk_padset_t p_ps;                /* the pads' settings (brk_padset) */
static brk_step_t pat[BRK_MAX_STEPS];
static volatile uint32_t p_keys[BRK_KEYS];   /* the white keys' settings, a brk_keyset_t a word */
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
} voice_t;

static voice_t v[2];
static int cur;                          /* the voice that plays; the other fades out */
static int32_t step = -1, last_slice = -1, pending = BRK_NO_SLICE;
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
    n->active = 1;
    last_slice = s;
}

static void trigger(const brk_break_t *b, int32_t s, int rev)
{
    if (!b || s < 0 || (uint32_t)s >= b->nslices)
        return;
    voice_start(b, s, rev, -1.0f, 1.0f);
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
    roll_gain = 1.0f;
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
    if (pending != BRK_NO_SLICE) {       /* a key pressed since the last step: its loop starts here */
        s = pending;
        pending = BRK_NO_SLICE;
        key_n = 0u;
        key_begin(b, s);
        key_left = key_steps * step_len();
        if ((held_mask >> s) & 1u)
            key_slice = s;
        else
            key_let_go(b);               /* a tap: one hit */
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
    }
    if (off & 1u << PAD_FREEZE)
        v[0].loop = v[1].loop = 0;
}

static void voice_tick(const brk_break_t *b, voice_t *o, float inc, float *acc)
{
    if (!o->active)
        return;
    *acc += hermite(b, o->pos) * o->amp;
    o->pos += inc * o->dir;
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
    float sl = step_len(), inc, half, ratio, sw = brk_clampf(p_swing, 0.5f, 0.75f);
    int playing = p_playing;
    n_renders++;
    if (!b || !b->len || !b->nslices) {
        for (i = 0; i < n; i++)
            out[i] = 0.0f;
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
    while (q_r != q_w) {                 /* the keys */
        kev_t e = q[q_r % QLEN];
        q_r++;
        if (e.slice < 0 || (uint32_t)e.slice >= b->nslices || e.slice >= 32)
            continue;
        if (e.down) {
            held_mask |= 1u << e.slice;
            if (playing)
                pending = e.slice;       /* quantised: the next step */
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
                        pending = k;
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
        float acc = 0.0f, g;
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
                roll_n++;                /* each repeat a little quieter (the decay) */
                roll_gain *= 1.0f - brk_clampf(p_ps.roll_decay, 0.0f, 0.9f);
                if (roll_gain < 0.02f)
                    roll_gain = 0.02f;
                voice_start(b, roll_slice, (pads_now >> PAD_REVERSE) & 1u, roll_pos, roll_gain);
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
            inc = ratio * brk_exp2f((p_pitch + step_pitch + fxp) * (1.0f / 12.0f)) * half * tape;
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
        voice_tick(b, &v[0], inc, &acc);
        voice_tick(b, &v[1], inc, &acc);
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
        out[i] = acc * (1.0f / 32768.0f) * gate_amp * (tape < 0.02f ? tape * 50.0f : 1.0f);
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
    clk_cnt = 0u;
    clk_k = 12u;
    {
        brk_keyset_t d;
        uint32_t k;
        brk_keyset_defaults(&d);
        for (k = 0; k < BRK_KEYS; k++)
            brk_keyset(k, &d);
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
    p->rsv[0] = p->rsv[1] = p->rsv[2] = 0u;
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
void brk_swing(float s) { p_swing = brk_clampf(s, 0.5f, 0.75f); }
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
