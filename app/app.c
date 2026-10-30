/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH on fm1-base: the app's unity build (compiled into fm1-base's device/unity.c after the platform, so gfx.c's
 * cv_* drawing and the plat_* calls are in scope) and the glue between the contract (app.h) and the engine
 * (dsp/brk.c, dsp/brk_fx.c: their own -O2 units).
 *
 * M1 scope (PRD 13): the built-in break, 16 slices on the white keys, the 11 pads on the black keys, play / reset,
 * tempo and tap tempo, pitch on OCT-/OCT+, the generator, the four FX and the DJ filter, settings kept by SAVE.
 * Breaks: the synthesised one, then a bank built into the image when app/bank_data.h exists (tools/prep_breaks.py
 * --header; git-ignored: recorded breaks stay out of public builds), chosen on ALGORITHM. */
#include "adpcm.h"
#include "brk_bank.h"
#include "brk_math.h"
#include "brk_synth.h"
#include "sfx.h"
#if defined(__has_include)
#if __has_include("bank_data.h")
#include "bank_data.h"
#define CH_HAVE_BANK 1
#endif
#endif
#ifndef CH_HAVE_BANK
#define CH_HAVE_BANK 0
#endif

/* ------------------------------------------------------------------ settings --- */
#define SET_MAGIC 0x33504843u            /* "CHP3" (+ the pads' settings, the soundboard): another magic loads defaults */
typedef struct {
    uint32_t magic;
    uint16_t tempo_x10;                  /* 400..3000 */
    int8_t pitch;                        /* semitones */
    uint8_t swing;                       /* 50..75 % */
    brk_gen_t gen;
    brk_fx_t fx;
    uint8_t brk;                         /* the break: 0 the synthesised one, 1.. the bank's slots */
    uint8_t rsv[15];
    brk_padset_t pads;                   /* the pads' own settings (K1-K4 while a pad is held) */
    sfx_set_t sfx;                       /* the soundboard: level, pitch, echo, length */
} settings_t;
static settings_t set;
static uint8_t set_dirty;
static uint8_t foot_dirty;               /* a setting changed: the slicer's foot shows the new value */

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.tempo_x10 = 1700u;               /* jungle tempo; the built-in break is played at 136.9 */
    set.pitch = 0;
    set.swing = 50u;
    set.gen.complexity = 0.0f;
    set.gen.anchor = 0.7f;
    set.gen.roll = 0.3f;
    set.gen.fill = 0.5f;
    set.gen.phrase_bars = 4u;
    brk_fx_defaults(&set.fx);
    set.brk = 0u;                        /* the first break: the bank's first when there is one (app_init) */
    brk_padset_defaults(&set.pads);
    sfx_defaults(&set.sfx);
}

static void settings_apply(void)
{
    brk_tempo((float)set.tempo_x10 * 0.1f);
    brk_pitch((float)set.pitch);
    brk_swing((float)set.swing * 0.01f);
    brk_gen(&set.gen);
    brk_fx_set(&set.fx);
    brk_fx_tempo((float)set.tempo_x10 * 0.1f);
    brk_padset(&set.pads);
    sfx_set(&set.sfx);
}

static void changed(void)
{
    set_dirty = 1u;
    foot_dirty = 1u;
    settings_apply();
}

/* ------------------------------------------------------------------- engine --- */
static int16_t pcm_a[CH_PCM_MAX] PLAT_POOL;
#define CH_DUB_LEN 32768u                /* the dub delay's line: 0.74 s, 3/8 down to 91 BPM */
static int16_t dub_line[CH_DUB_LEN] PLAT_POOL;
static brk_break_t brk0;
static brk_break_t brk_cur;              /* the bank's break playing (pcm_a holds it) */
static uint32_t n_synth;                 /* the built-in styles come first (brk_synth.c) */
static uint32_t n_breaks;                /* then the bank's valid slots */
static uint8_t wave_px[240];             /* the waveform's height per column, for the screen */
#define BLK_N 16u                        /* the slicer view's blocks: one per white key */
#define BLK_W 26u                        /* columns of waveform in a block */
static uint8_t blk_px[BLK_N][BLK_W];     /* each slice's waveform, 0..19 */
static uint8_t blk_n;                    /* blocks with a slice behind them */
static volatile uint8_t muted;

static void wave_build(const brk_break_t *b)
{
    uint32_t x, i;
    for (x = 0; x < 240u; x++) {
        uint32_t a = b->len * x / 240u, z = b->len * (x + 1u) / 240u, pk = 0u;
        for (i = a; i < z; i++) {
            int32_t s = b->pcm[i] < 0 ? -b->pcm[i] : b->pcm[i];
            if ((uint32_t)s > pk)
                pk = (uint32_t)s;
        }
        wave_px[x] = (uint8_t)(pk * 24u / 32768u);
    }
    blk_n = (uint8_t)(b->nslices < BLK_N ? b->nslices : BLK_N);
    for (x = 0; x < BLK_N; x++) {
        uint32_t a = x < blk_n ? b->slice[x] : 0u, z = x < blk_n ? b->slice[x + 1] : 0u, c;
        for (c = 0; c < BLK_W; c++) {
            uint32_t i0 = a + (z - a) * c / BLK_W, i1 = a + (z - a) * (c + 1u) / BLK_W, pk = 0u;
            for (i = i0; i < i1; i++) {
                int32_t v = b->pcm[i] < 0 ? -b->pcm[i] : b->pcm[i];
                if ((uint32_t)v > pk)
                    pk = (uint32_t)v;
            }
            blk_px[x][c] = (uint8_t)(pk * 19u / 32768u);
        }
    }
}

/* the bank built into the image: its slot i (0..), or 0 when absent or not sane */
static const bank_slot_t *bank_slot(uint32_t i)
{
#if CH_HAVE_BANK
    const bank_header_t *h = (const bank_header_t *)CH_BANK;
    const bank_slot_t *s;
    if (h->magic != BANK_MAGIC || h->version != BANK_VERSION || i >= h->nslots || i >= BANK_SLOTS ||
        16u + 168u * (i + 1u) > CH_BANK_SIZE)
        return 0;
    s = (const bank_slot_t *)(CH_BANK + 16u + 168u * i);
    if (s->data_off + s->data_len > CH_BANK_SIZE || s->len > CH_PCM_MAX || s->data_len < (s->len + 1u) / 2u ||
        !s->nslices || s->nslices > BANK_SLICES || !s->bars || s->slice[s->nslices] != s->len || !s->bpm_x100)
        return 0;
    return s;
#else
    (void)i;
    return 0;
#endif
}

/* break i into pcm_a (nothing may be playing from it: the render was given no break, see break_ask) */
static void break_load(uint32_t i)
{
    const bank_slot_t *s = i >= n_synth ? bank_slot(i - n_synth) : 0;
    const brk_break_t *b = &brk0;
    uint32_t k;
    brk_set_break(0);
    if (s) {
        adpcm_state_t st = {0, 0, 0};
#if CH_HAVE_BANK
        adpcm_decode(&st, CH_BANK + s->data_off, pcm_a, s->len);
#endif
        brk_cur.pcm = pcm_a;
        brk_cur.len = s->len;
        brk_cur.rate = s->rate;
        brk_cur.bpm_x100 = s->bpm_x100;
        brk_cur.bars = s->bars;
        brk_cur.nslices = s->nslices;
        for (k = 0; k <= s->nslices; k++)
            brk_cur.slice[k] = s->slice[k];
        for (k = 0; k < 12u; k++)
            brk_cur.name[k] = s->name[k];
        brk_cur.name[12] = 0;
        b = &brk_cur;
    } else {                             /* a built-in style, played into RAM now */
        if (i >= n_synth)
            i = 0u;
        if (brk_synth_style_len(i, CH_RATE) <= CH_PCM_MAX)
            brk_synth_style(pcm_a, CH_RATE, i, &brk0);
    }
    set.brk = (uint8_t)i;
    wave_build(b);
    brk_set_break(b);
}

void app_init(void)
{
    brk_init();
    brk_fx_init();
    brk_fx_delay_buffer(dub_line, CH_DUB_LEN);
    sfx_init();
    n_synth = n_breaks = brk_synth_count();
    while (n_breaks - n_synth < BANK_SLOTS && bank_slot(n_breaks - n_synth))
        n_breaks++;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.tempo_x10 < 400u || set.tempo_x10 > 3000u || set.swing < 50u || set.swing > 75u) {
        settings_defaults();
        if (n_breaks > n_synth)
            set.brk = (uint8_t)n_synth;  /* the bank's first break */
    }
    if (set.brk >= n_breaks)
        set.brk = 0u;
    break_load(set.brk);                 /* boot: the audio isn't running yet */
    settings_apply();
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float mono[APP_BLOCK], fx[APP_BLOCK], l[APP_BLOCK], r[APP_BLOCK];
    float gain = muted ? 0.0f : (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f;
    brk_status_t st;
    while (n) {
        uint32_t m = n < APP_BLOCK ? n : APP_BLOCK, i;
        brk_render(mono, m);
        sfx_render(fx, m);               /* the soundboard, over the break, into the delay at its echo */
        brk_status(&st);
        brk_fx_process(mono, fx, sfx_echo(), l, r, m, st.pads);
        for (i = 0; i < m; i++) {
            lr[2u * i] = (int32_t)(l[i] * gain);
            lr[2u * i + 1u] = (int32_t)(r[i] * gain);
        }
        lr += 2u * m;
        n -= m;
    }
}

void app_panic(void)
{
    muted = 1u;
    brk_play(0);
}

/* ----------------------------------------------------------------------- UI --- */
enum { PG_HOME, PG_GEN, PG_FX, PG_SOUND, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"PERFORM", "GENERATOR", "FX", "SOUNDBOARD"};
static const char *const FX_NAME[BRK_NFX] = {"RING MOD", "HI-PASS", "FLANGER", "DEGRADER"};
static const char *const PAD_NAME[BRK_NPADS] = {"R8", "R16", "R32", "REV", "FRZ", "HLF", "STP", "GAT", "RND",
                                                "FLT", "DUB"};
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;

static struct {
    uint32_t keys, btn;
    uint8_t page, fx_focus;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t taps[4], ntaps;             /* tap tempo on SEL */
    uint32_t oct_t, oct_rep;             /* OCT- / OCT+ held: repeats that speed up */
    int32_t last_step, last_slice;
    uint32_t last_pads, last_keys, last_hdr;
    uint32_t intro_t0;                   /* the start screen: when it began (0: over) */
    uint8_t intro, intro_drawn;          /* showing it; blocks drawn so far */
    uint8_t touched;                     /* a key, button or encoder moved since app_frame last looked */
    int8_t sfx_hit;                      /* the soundboard key pressed last, and when */
    uint32_t sfx_hit_t, sfx_drawn, sfx_mask;
    int8_t pad_focus;                    /* the pad held last (its animation, K1-K4 tune it); -1 none */
    int8_t pad_shown;                    /* the pad the screen shows now */
    uint32_t pad_since, fx_drawn;        /* when it was pressed; the animation's last frame */
    float reel;                          /* the tape stop's reels, turns */
    uint8_t redraw;                      /* RG_* bits: the regions to draw again */
} ui;
enum { RG_HEAD = 1, RG_WAVE = 2, RG_BLOCKS = 4, RG_FOOT = 8, RG_ALL = 15, RG_FX = 16 };

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void tenths_text(int32_t v, char *b)   /* 1700 -> "170.0" */
{
    char t[12];
    int_text(v / 10, b);
    t[0] = '.';
    t[1] = (char)('0' + (v < 0 ? -v : v) % 10);
    t[2] = 0;
    text_cat(b, t);
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw |= 1u;               /* RG_HEAD: the message */
}

static void say_int(const char *what, int32_t v, const char *unit)
{
    char t[24];
    int_text(v, t);
    text_cat(t, unit);
    app_say(what, t);
}

static float knob_f(float v, int32_t d, float step, float lo, float hi)
{
    v += (float)d * step;
    return v < lo ? lo : v > hi ? hi : v;
}

static void tap_tempo(uint32_t now)
{
    uint32_t i;
    if (ui.ntaps && now - ui.taps[(ui.ntaps - 1u) % 4u] > 2000u)
        ui.ntaps = 0u;                   /* a pause starts a new count */
    ui.taps[ui.ntaps % 4u] = now;
    ui.ntaps++;
    if (ui.ntaps >= 4u) {                /* the last four taps: three intervals */
        uint32_t first = ui.taps[(ui.ntaps - 4u) % 4u], span = now - first;
        if (span) {
            i = 3u * 600000u / span;     /* BPM x 10 */
            set.tempo_x10 = (uint16_t)(i < 400u ? 400u : i > 3000u ? 3000u : i);
            changed();
            {
                char t[16];
                tenths_text(set.tempo_x10, t);
                app_say("TAP", t);
            }
        }
    }
}

static void knob_turn(int k, int32_t d)
{
    brk_fx_t *f = &set.fx;
    if (ui.page == PG_SOUND) {           /* the soundboard's level, pitch, echo and length */
        sfx_set_t *x = &set.sfx;
        char t[16];
        if (k == EN_K1) {
            x->level = knob_f(x->level, d, 0.02f, 0.0f, 1.0f);
            say_int("LEVEL", (int32_t)(x->level * 100.0f), "%");
        } else if (k == EN_K2) {
            x->pitch = knob_f(x->pitch, d, 1.0f, -12.0f, 12.0f);
            int_text((int32_t)x->pitch, t);
            text_cat(t, " ST");
            app_say("PITCH", t);
        } else if (k == EN_K3) {
            x->echo = knob_f(x->echo, d, 0.02f, 0.0f, 1.0f);
            say_int("ECHO", (int32_t)(x->echo * 100.0f), "%");
        } else {
            x->length = knob_f(x->length, d, 0.05f, 0.5f, 2.0f);
            say_int("LENGTH", (int32_t)(x->length * 100.0f), "%");
        }
        changed();
        return;
    }
    if (ui.page == PG_HOME) {
        if (k == EN_K1) {                /* the DJ filter: left low-pass, right high-pass */
            f->dj = knob_f(f->dj, d, 0.02f, -1.0f, 1.0f);
            say_int("DJ FILTER", (int32_t)(f->dj * 100.0f), "");
        } else if (k == EN_K2) {
            set.swing = (uint8_t)clamp((int32_t)set.swing + d, 50, 75);
            say_int("SWING", set.swing, "%");
        } else if (k == EN_K3) {
            set.gen.complexity = knob_f(set.gen.complexity, d, 0.02f, 0.0f, 1.0f);
            say_int("COMPLEXITY", (int32_t)(set.gen.complexity * 100.0f), "%");
        } else if (k == EN_K4) {         /* FX mix: the degrader's depth, off at 0 */
            f->deg_bits = knob_f(f->deg_bits, -d, 0.25f, 2.0f, 16.0f);
            f->on[FX_DEGRADE] = f->deg_bits < 15.9f;
            f->deg_rate = 2000.0f + (f->deg_bits - 2.0f) * (42100.0f / 14.0f);
            say_int("CRUNCH", (int32_t)((16.0f - f->deg_bits) * 100.0f / 14.0f), "%");
        }
    } else if (ui.page == PG_GEN) {
        float *p = k == EN_K1 ? &set.gen.complexity : k == EN_K2 ? &set.gen.anchor : k == EN_K3 ? &set.gen.roll
                 : &set.gen.fill;
        static const char *const N[4] = {"COMPLEXITY", "ANCHOR", "ROLL", "FILL"};
        *p = knob_f(*p, d, 0.02f, 0.0f, 1.0f);
        say_int(N[k - EN_K1], (int32_t)(*p * 100.0f), "%");
    } else {                             /* FX: the focused effect's three settings and its mix */
        int i = k - EN_K1;
        switch (ui.fx_focus * 4 + i) {
        case FX_RING * 4 + 0: f->ring_hz = knob_f(f->ring_hz, d, 10.0f, 20.0f, 2000.0f); say_int("RING", (int32_t)f->ring_hz, " HZ"); break;
        case FX_RING * 4 + 3: f->ring_mix = knob_f(f->ring_mix, d, 0.02f, 0.0f, 1.0f); say_int("RING MIX", (int32_t)(f->ring_mix * 100.0f), "%"); break;
        case FX_HPF * 4 + 0: f->hpf_hz = knob_f(f->hpf_hz, d, 20.0f, 20.0f, 8000.0f); say_int("CUTOFF", (int32_t)f->hpf_hz, " HZ"); break;
        case FX_HPF * 4 + 1: f->hpf_res = knob_f(f->hpf_res, d, 0.02f, 0.0f, 0.95f); say_int("RESONANCE", (int32_t)(f->hpf_res * 100.0f), "%"); break;
        case FX_HPF * 4 + 2: f->hpf_spread = knob_f(f->hpf_spread, d, 0.02f, 0.0f, 1.0f); say_int("SPREAD", (int32_t)(f->hpf_spread * 100.0f), "%"); break;
        case FX_FLANGER * 4 + 0: f->fl_rate = knob_f(f->fl_rate, d, 0.02f, 0.02f, 10.0f); say_int("RATE", (int32_t)(f->fl_rate * 100.0f), " CHZ"); break;
        case FX_FLANGER * 4 + 1: f->fl_depth = knob_f(f->fl_depth, d, 0.02f, 0.0f, 1.0f); say_int("DEPTH", (int32_t)(f->fl_depth * 100.0f), "%"); break;
        case FX_FLANGER * 4 + 2: f->fl_fb = knob_f(f->fl_fb, d, 0.02f, -0.9f, 0.9f); say_int("FEEDBACK", (int32_t)(f->fl_fb * 100.0f), "%"); break;
        case FX_FLANGER * 4 + 3: f->fl_mix = knob_f(f->fl_mix, d, 0.02f, 0.0f, 1.0f); say_int("FLANGE MIX", (int32_t)(f->fl_mix * 100.0f), "%"); break;
        case FX_DEGRADE * 4 + 0: f->deg_bits = knob_f(f->deg_bits, d, 0.25f, 2.0f, 16.0f); say_int("BITS", (int32_t)f->deg_bits, ""); break;
        case FX_DEGRADE * 4 + 1: f->deg_rate = knob_f(f->deg_rate, d, 250.0f, 1000.0f, 44100.0f); say_int("RATE", (int32_t)f->deg_rate, " HZ"); break;
        case FX_DEGRADE * 4 + 2: f->deg_drive = knob_f(f->deg_drive, d, 0.02f, 0.0f, 1.0f); say_int("DRIVE", (int32_t)(f->deg_drive * 100.0f), "%"); break;
        default: return;
        }
    }
    changed();
}

/* a break change while running, without blocking: the render gets no break (silence), and once two renders have
 * passed (it has let go of pcm_a) app_frame loads the new one */
static int32_t brk_next = -1;
static uint32_t brk_mark;
static void break_ask(uint32_t i)
{
    set.brk = (uint8_t)i;                /* turning on fast just moves the target */
    if (brk_next < 0) {
        brk_set_break(0);
        brk_mark = brk_renders();
    }
    brk_next = (int32_t)i;
    set_dirty = 1u;
}
static void break_poll(void)
{
    if (brk_next >= 0 && brk_renders() - brk_mark >= 2u) {
        break_load((uint32_t)brk_next);
        brk_next = -1;
        app_say("BREAK", brk_get_break()->name);
    }
}

/* -------------------------------------------------- the pads' own settings --- */
/* what K1-K4 tune while a pad is held, by pad (brk.h PAD_*); 0: that knob does nothing for it */
static const char *const PAD_KNOB[BRK_NPADS][4] = {
    {"RATE", "DECAY", "PITCH", 0}, {"RATE", "DECAY", "PITCH", 0}, {"RATE", "DECAY", "PITCH", 0},
    {"SPEED", 0, 0, 0}, {"LENGTH", "PITCH", 0, 0}, {"SPEED", 0, 0, 0}, {"STOP", "SPIN UP", 0, 0},
    {"RATE", "LENGTH", 0, 0}, {"AMOUNT", "PITCH", 0, 0}, {"TARGET", "RESO", "SPEED", 0},
    {"TIME", "FEEDBACK", "TONE", "MIX"}};
static const char *const PAD_TITLE[BRK_NPADS] = {"ROLL", "ROLL", "ROLL", "REVERSE", "FREEZE", "HALF SPEED",
                                                  "TAPE STOP", "GATE", "RANDOM", "FILTER", "DUB"};
static const char *const FRZ_LEN_NAME[4] = {"SLICE", "1/2", "1/4", "1/8"};

static void pct_text(float v, char *t)   /* 0.42 -> "42%" */
{
    int_text((int32_t)(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f)), t);
    text_cat(t, "%");
}
static void signed_tenths(float v, const char *unit, char *t)   /* -1.5 -> "-1.5ST" */
{
    int32_t x = (int32_t)(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f));
    t[0] = x > 0 ? '+' : 0;
    t[1] = 0;
    tenths_text(x, t + (x > 0));
    if (x < 0 && x > -10) {              /* tenths_text drops the sign of -0.x */
        t[0] = '-';
        tenths_text(-x, t + 1);
    }
    text_cat(t, unit);
}
static void times_text(float v, char *t) /* 0.5 -> "x0.50" */
{
    int32_t x = (int32_t)(v * 100.0f + 0.5f);
    char d[8];
    t[0] = 'x';
    int_text(x / 100, t + 1);
    text_cat(t, ".");
    d[0] = (char)('0' + x / 10 % 10);
    d[1] = (char)('0' + x % 10);
    d[2] = 0;
    text_cat(t, d);
}

/* -1..1 as the DJ filter's frequency: "LP 340HZ" / "HP 1.2K" */
static void dj_text(float dj, char *t)
{
    float hz = dj > 0.0f ? 20.0f * brk_exp2f(dj * 8.2f) : 18000.0f * brk_exp2f(dj * 7.8f);
    t[0] = 0;
    text_cat(t, dj > 0.0f ? "HP " : "LP ");
    if (hz >= 1000.0f) {
        tenths_text((int32_t)(hz / 100.0f), t + 3);
        text_cat(t, "K");
    } else {
        int_text((int32_t)hz, t + 3);
        text_cat(t, "HZ");
    }
}

static void pad_knob_value(int pad, int k, char *t)
{
    const brk_padset_t *p = &set.pads;
    const brk_fx_t *f = &set.fx;
    t[0] = 0;
    switch (pad <= PAD_ROLL32 ? 0 : pad) {
    case 0:
        if (k == 0) text_cat(t, BRK_ROLL_DIV_NAME[p->roll_div[pad] % BRK_ROLL_DIVS]);
        else if (k == 1) pct_text(p->roll_decay, t);
        else if (k == 2) signed_tenths(p->roll_pitch, "ST", t);
        break;
    case PAD_REVERSE: times_text(p->rev_speed, t); break;
    case PAD_FREEZE:
        if (k == 0) text_cat(t, FRZ_LEN_NAME[p->frz_len & 3u]);
        else signed_tenths(p->frz_pitch, "ST", t);
        break;
    case PAD_HALF: times_text(p->half_speed, t); break;
    case PAD_TAPESTOP:
        if (k == 0) { signed_tenths(p->stop_beats, " BT", t); if (t[0] == '+') t[0] = ' '; }
        else { signed_tenths(p->spin_steps, " ST", t); if (t[0] == '+') t[0] = ' '; }
        break;
    case PAD_GATE:
        if (k == 0) text_cat(t, BRK_GATE_DIV_NAME[p->gate_div % BRK_GATE_DIVS]);
        else pct_text(p->gate_len, t);
        break;
    case PAD_RANDOM:
        if (k == 0) pct_text(p->rnd_amount, t);
        else { int_text((int32_t)p->rnd_pitch, t); text_cat(t, "ST"); }
        break;
    case PAD_FILTER:
        if (k == 0) dj_text(f->flt_target, t);
        else if (k == 1) pct_text(f->flt_res, t);
        else pct_text(f->flt_speed, t);
        break;
    case PAD_DUB:
        if (k == 0) text_cat(t, BRK_DUB_DIV_NAME[f->dub_div % BRK_DUB_DIVS]);
        else pct_text(k == 1 ? f->dub_fb : k == 2 ? f->dub_tone : f->dub_mix, t);
        break;
    default: break;
    }
}

static void pad_knob_turn(int pad, int k, int32_t d)
{
    brk_padset_t *p = &set.pads;
    brk_fx_t *f = &set.fx;
    char t[16];
    if (pad < 0 || pad >= BRK_NPADS || k < 0 || k > 3 || !PAD_KNOB[pad][k])
        return;
    switch (pad <= PAD_ROLL32 ? 0 : pad) {
    case 0:
        if (k == 0) p->roll_div[pad] = (uint8_t)clamp((int32_t)p->roll_div[pad] + d, 0, BRK_ROLL_DIVS - 1);
        else if (k == 1) p->roll_decay = knob_f(p->roll_decay, d, 0.02f, 0.0f, 0.9f);
        else p->roll_pitch = knob_f(p->roll_pitch, d, 0.5f, -3.0f, 3.0f);
        break;
    case PAD_REVERSE: p->rev_speed = knob_f(p->rev_speed, d, 0.05f, 0.5f, 2.0f); break;
    case PAD_FREEZE:
        if (k == 0) p->frz_len = (uint8_t)clamp((int32_t)p->frz_len + d, 0, 3);
        else p->frz_pitch = knob_f(p->frz_pitch, d, 1.0f, -12.0f, 12.0f);
        break;
    case PAD_HALF: p->half_speed = knob_f(p->half_speed, d, 0.05f, 0.25f, 0.75f); break;
    case PAD_TAPESTOP:
        if (k == 0) p->stop_beats = knob_f(p->stop_beats, d, 0.25f, 0.25f, 4.0f);
        else p->spin_steps = knob_f(p->spin_steps, d, 0.25f, 0.25f, 8.0f);
        break;
    case PAD_GATE:
        if (k == 0) p->gate_div = (uint8_t)clamp((int32_t)p->gate_div + d, 0, BRK_GATE_DIVS - 1);
        else p->gate_len = knob_f(p->gate_len, d, 0.05f, 0.1f, 0.9f);
        break;
    case PAD_RANDOM:
        if (k == 0) p->rnd_amount = knob_f(p->rnd_amount, d, 0.05f, 0.0f, 1.0f);
        else p->rnd_pitch = knob_f(p->rnd_pitch, d, 1.0f, 0.0f, 12.0f);
        break;
    case PAD_FILTER:
        if (k == 0) f->flt_target = knob_f(f->flt_target, d, 0.04f, -1.0f, 1.0f);
        else if (k == 1) f->flt_res = knob_f(f->flt_res, d, 0.02f, 0.0f, 0.95f);
        else f->flt_speed = knob_f(f->flt_speed, d, 0.02f, 0.0f, 1.0f);
        break;
    case PAD_DUB:
        if (k == 0) f->dub_div = (uint8_t)clamp((int32_t)f->dub_div + d, 0, BRK_DUB_DIVS - 1);
        else if (k == 1) f->dub_fb = knob_f(f->dub_fb, d, 0.02f, 0.0f, 0.95f);
        else if (k == 2) f->dub_tone = knob_f(f->dub_tone, d, 0.02f, 0.0f, 1.0f);
        else f->dub_mix = knob_f(f->dub_mix, d, 0.02f, 0.0f, 1.0f);
        break;
    default: break;
    }
    pad_knob_value(pad, k, t);
    app_say(PAD_KNOB[pad][k], t);
    changed();
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms(), i;
    uint32_t kdn = keys & ~ui.keys, kup = ui.keys & ~keys, bdn = btn & ~ui.btn;
    int k;
    if (keys != ui.keys || btn != ui.btn)
        ui.touched = 1u;                 /* (read here too between frames: app_input_only) */
    for (i = 0; i < 16u; i++) {          /* white keys: slices, or on the soundboard its sounds */
        if (ui.page == PG_SOUND) {
            if (kdn >> WK[i] & 1u) {
                sfx_trigger((int)i);
                if (i == SFX_REWIND)
                    brk_rewind();        /* the pull-up: the break spins back and starts again */
                ui.sfx_hit = (int8_t)i;
                ui.sfx_hit_t = now;
                app_say(SFX_NAME[i], "");
            }
            if (kup >> WK[i] & 1u)
                sfx_release((int)i);
            continue;
        }
        if (kdn >> WK[i] & 1u)
            brk_key((int)i, 1);
        if (kup >> WK[i] & 1u)
            brk_key((int)i, 0);
    }
    for (i = 0; i < 11u; i++) {          /* black keys: the pads, held; the last one pressed has the screen */
        if (kdn >> BK[i] & 1u) {
            brk_pad((int)i, 1);
            ui.pad_focus = (int8_t)i;
            ui.pad_since = now;
        }
        if (kup >> BK[i] & 1u)
            brk_pad((int)i, 0);
    }
    if (ui.pad_focus >= 0 && !(keys >> BK[ui.pad_focus] & 1u)) {   /* let go: another pad still held, or none */
        ui.pad_focus = -1;
        for (i = 0; i < 11u; i++)
            if (keys >> BK[i] & 1u)
                ui.pad_focus = (int8_t)i;
    }
    if (bdn & 1u << B_PLAY) {
        brk_status_t st;
        brk_status(&st);
        if (btn & 1u << B_HOME) {        /* HOME + PLAY: back to bar 1 */
            brk_reset();
            app_say("RESET", "");
        } else
            brk_play(!st.playing);
        muted = 0u;
    }
    if (bdn & 1u << B_HOME) {
        ui.page = PG_HOME;
        ui.redraw |= 8u;               /* RG_FOOT: the page */
    }
    if (bdn & 1u << B_ENV) {             /* ENV: the soundboard */
        ui.page = PG_SOUND;
        ui.redraw |= 8u | 4u;            /* RG_FOOT | RG_BLOCKS */
        app_say("SOUNDBOARD", "");
    }
    if (bdn & 1u << B_ARP) {
        if (btn & 1u << B_SEL) {         /* SEL + ARP: a random pattern */
            brk_randomize();
            app_say("RANDOMIZE", "");
        }
        ui.page = PG_GEN;
        ui.redraw |= 8u;               /* RG_FOOT: the page */
    }
    if (bdn & 1u << B_FX) {              /* FX: the page; again: the focused effect on / off */
        if (ui.page == PG_FX) {
            set.fx.on[ui.fx_focus] ^= 1u;
            app_say(FX_NAME[ui.fx_focus], set.fx.on[ui.fx_focus] ? "ON" : "OFF");
            changed();
        }
        ui.page = PG_FX;
        ui.redraw |= 8u;               /* RG_FOOT: the page */
    }
    if (bdn & 1u << B_SEL && ui.page == PG_HOME && !(btn & 1u << B_ARP))
        tap_tempo(now);
    if (bdn & 1u << B_SAVE) {
        app_say("SAVE", plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0 ? "DONE" : "FAILED");
        set_dirty = 0u;
    }
    /* OCT- / OCT+: pitch in semitones, repeating faster while held; both: back to 0 */
    if ((btn & (1u << B_OCTDN | 1u << B_OCTUP)) == (1u << B_OCTDN | 1u << B_OCTUP)) {
        if (bdn & (1u << B_OCTDN | 1u << B_OCTUP) && set.pitch) {
            set.pitch = 0;
            changed();
            app_say("PITCH", "0");
        }
    } else if (btn & (1u << B_OCTDN | 1u << B_OCTUP)) {
        int32_t dir = btn & 1u << B_OCTUP ? 1 : -1;
        if (bdn & (1u << B_OCTDN | 1u << B_OCTUP)) {
            ui.oct_t = now + 400u;
            ui.oct_rep = 0u;
        } else if (now >= ui.oct_t) {
            ui.oct_rep++;
            ui.oct_t = now + (ui.oct_rep < 4u ? 150u : 50u);
        } else
            dir = 0;
        if (dir) {
            set.pitch = (int8_t)clamp(set.pitch + dir, -24, 24);
            changed();
            say_int("PITCH", set.pitch, " ST");
        }
    }
    for (k = 0; k < NE; k++) {
        int32_t d = plat_enc(k);
        if (!d)
            continue;
        ui.touched = 1u;
        if (k == EN_SELECT) {            /* tempo: 1 BPM a detent, 0.1 with SEL held */
            set.tempo_x10 = (uint16_t)clamp((int32_t)set.tempo_x10 + d * (btn & 1u << B_SEL ? 1 : 10), 400, 3000);
            changed();
            {
                char t[16];
                tenths_text(set.tempo_x10, t);
                app_say("BPM", t);
            }
        } else if (k == EN_ALGO) {       /* the break (one in M1); on the FX page the focused effect */
            if (ui.page == PG_FX) {
                ui.fx_focus = (uint8_t)((ui.fx_focus + (d > 0 ? 1 : BRK_NFX - 1)) % BRK_NFX);
                app_say(FX_NAME[ui.fx_focus], set.fx.on[ui.fx_focus] ? "ON" : "OFF");
            } else {                     /* the next or previous break */
                break_ask((set.brk + (d > 0 ? 1u : n_breaks - 1u)) % n_breaks);
            }
        } else if (k == EN_PRESET)
            app_say("PATTERN", "1");     /* patterns arrive in M2 */
        else if (ui.pad_focus >= 0)      /* a pad held: K1-K4 tune it */
            pad_knob_turn(ui.pad_focus, k - EN_K1, d);
        else
            knob_turn(k, d);
    }
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

static void leds(void)
{
    brk_status_t st;
    uint32_t b = 0u, k = 0u, i;
    brk_status(&st);
    if (st.playing)
        b |= 1u << B_PLAY;
    b |= 1u << (ui.page == PG_HOME ? B_HOME : ui.page == PG_GEN ? B_ARP : ui.page == PG_SOUND ? B_ENV : B_FX);
    if (set_dirty)
        b |= 1u << B_SAVE;
    if (ui.page == PG_SOUND) {           /* the soundboard: the sounds playing */
        uint32_t m = sfx_playing();
        for (i = 0; i < 16u; i++)
            if (m >> i & 1u)
                k |= 1u << WK[i];
    } else if (st.slice >= 0 && st.slice < 16)
        k |= 1u << WK[st.slice];
    for (i = 0; i < 11u; i++)
        if (st.pads >> i & 1u)
            k |= 1u << BK[i];
    plat_glow(1);
    plat_leds(b, k);
}

/* ------------------------------------------------------------- start screen --- */
#define C_BLOCK RGB(24, 24, 30)          /* a slice block's ground */
#define C_BLOCK_HI RGB(255, 184, 70)     /* the block sounding */
#define INTRO_STEP 55u                   /* ms between two blocks dropping in */
#define INTRO_HOLD 900u                  /* ms the finished screen stays before the slicer opens */

/* the mark: sixteen bars like a break's hits, two of them chopped out of line */
static const uint8_t MARK_H[16] = {30, 12, 26, 10, 34, 14, 11, 20, 12, 22, 28, 24, 34, 10, 13, 22};
static void mark(int32_t y0, uint32_t shown, const uint8_t (*px)[BLK_W])
{
    uint32_t k, c;
    for (k = 0; k < 16u && k < shown; k++) {
        int32_t x = 8 + (int32_t)k * 14, lift = (k == 5u || k == 11u) ? -10 : 0;
        uint16_t col = (k == 5u || k == 11u) ? C_WHITE : C_BLOCK_HI;
        if (px) {                        /* the loaded break: each slice's own shape */
            for (c = 0; c < 12u; c++) {
                int32_t h = px[k][c * BLK_W / 12u] * 2 + 1;
                cv_rect(x + (int32_t)c, y0 + lift + 20 - h / 2, 1, h, col);
            }
        } else                           /* the splash, before any break is in: the drawn mark */
            cv_rect(x, y0 + lift + 20 - MARK_H[k] / 2, 12, MARK_H[k], col);
    }
}

static void title(int32_t y)             /* the name and the tagline, centred, into the current strip */
{
    cv_text((240 - text_w(&FONT_L, APP_NAME)) / 2, y, &FONT_L, APP_NAME, C_WHITE);
    cv_text((240 - text_w(&FONT_XS, "AMEN CHOPPER  FOR THE FM-1")) / 2, y + 36, &FONT_XS,
            "AMEN CHOPPER  FOR THE FM-1", C_HI);
}

/* fm1-base's first screen at power-on, while the breaks are decoded (APP_SPLASH): the mark and the name */
void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 60, C_BLACK);
    mark(10, 16u, 0);
    cv_blit(0, 50);
    cv_begin(240, 56, C_BLACK);
    title(0);
    cv_blit(0, 118);
    cv_begin(240, 20, C_BLACK);
    cv_text((240 - text_w(&FONT_T, CH_VERSION)) / 2, 2, &FONT_T, CH_VERSION, C_GRAY);
    cv_blit(0, 210);
}

static void intro_draw(uint32_t now)
{
    uint32_t shown = (now - ui.intro_t0) / INTRO_STEP + 1u;
    char t[32];
    if (shown > 16u)
        shown = 16u;
    if (shown != ui.intro_drawn) {       /* the loaded break drops in, slice by slice */
        ui.intro_drawn = (uint8_t)shown;
        cv_begin(240, 60, C_BLACK);
        mark(10, shown, blk_n ? (const uint8_t (*)[BLK_W])blk_px : 0);
        cv_blit(0, 50);
    }
    if (ui.redraw & RG_HEAD) {
        const brk_break_t *b = brk_get_break();
        cv_begin(240, 56, C_BLACK);
        title(0);
        cv_blit(0, 118);
        cv_begin(240, 40, C_BLACK);
        t[0] = 0;
        text_cat(t, b ? b->name : "");
        cv_text((240 - text_w(&FONT_XS, t)) / 2, 2, &FONT_XS, t, C_GRAY);
        cv_text((240 - text_w(&FONT_T, "PRESS PLAY")) / 2, 22, &FONT_T, "PRESS PLAY", C_AMB);
        cv_blit(0, 190);
        ui.redraw = 0u;
    }
}

static void intro_end(void)
{
    ui.intro = 0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = RG_ALL;
}

/* ------------------------------------------------------------ slicer view --- */
static void knob_value(int i, char *t)  /* the page's knob i (0..3), as shown under its name */
{
    const brk_fx_t *f = &set.fx;
    t[0] = 0;
    if (ui.page == PG_SOUND) {
        const sfx_set_t *x = &set.sfx;
        if (i == 1) {
            int_text((int32_t)x->pitch, t);
            text_cat(t, " ST");
        } else {
            int_text((int32_t)((i == 0 ? x->level : i == 2 ? x->echo : x->length) * 100.0f), t);
            text_cat(t, "%");
        }
        return;
    }
    if (ui.page == PG_HOME) {
        if (i == 0) {
            int32_t v = (int32_t)(f->dj * 100.0f);
            if (v > -2 && v < 2) {
                text_cat(t, "OFF");
                return;
            }
            text_cat(t, v < 0 ? "LP " : "HP ");
            int_text(v < 0 ? -v : v, t + 3);
        } else if (i == 1) {
            int_text(set.swing, t);
            text_cat(t, "%");
        } else if (i == 2) {
            int_text((int32_t)(set.gen.complexity * 100.0f), t);
            text_cat(t, "%");
        } else {
            int_text(f->on[FX_DEGRADE] ? (int32_t)((16.0f - f->deg_bits) * 100.0f / 14.0f) : 0, t);
            text_cat(t, "%");
        }
    } else if (ui.page == PG_GEN) {
        float v = i == 0 ? set.gen.complexity : i == 1 ? set.gen.anchor : i == 2 ? set.gen.roll : set.gen.fill;
        int_text((int32_t)(v * 100.0f), t);
        text_cat(t, "%");
    } else {
        static const char *const NONE = "-";
        float v = 0.0f;
        const char *unit = "%";
        int pct = 1;
        switch (ui.fx_focus * 4 + i) {
        case FX_RING * 4 + 0: v = f->ring_hz; unit = "HZ"; pct = 0; break;
        case FX_RING * 4 + 3: v = f->ring_mix; break;
        case FX_HPF * 4 + 0: v = f->hpf_hz; unit = "HZ"; pct = 0; break;
        case FX_HPF * 4 + 1: v = f->hpf_res; break;
        case FX_HPF * 4 + 2: v = f->hpf_spread; break;
        case FX_FLANGER * 4 + 0: v = f->fl_rate * 100.0f; unit = "CHZ"; pct = 0; break;
        case FX_FLANGER * 4 + 1: v = f->fl_depth; break;
        case FX_FLANGER * 4 + 2: v = f->fl_fb; break;
        case FX_FLANGER * 4 + 3: v = f->fl_mix; break;
        case FX_DEGRADE * 4 + 0: v = f->deg_bits; unit = "BIT"; pct = 0; break;
        case FX_DEGRADE * 4 + 1: v = f->deg_rate; unit = "HZ"; pct = 0; break;
        case FX_DEGRADE * 4 + 2: v = f->deg_drive; break;
        default: text_cat(t, NONE); return;
        }
        int_text((int32_t)(pct ? v * 100.0f : v), t);
        text_cat(t, unit);
    }
}

/* the header: the break and the tempo, a play / stop mark; or a message for a moment */
static void draw_head(const brk_status_t *st, const brk_break_t *b, uint32_t now)
{
    char t[24];
    cv_begin(240, 28, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        if (st->playing) {               /* a play triangle, or a stop square */
            int32_t r;
            for (r = 0; r < 6; r++)
                cv_rect(8 + r, 8 + r, 1, 13 - 2 * r, C_HI);
        } else
            cv_rect(8, 9, 10, 10, C_DIM);
        cv_text(24, 6, &FONT_B, b ? b->name : "NO BREAK", C_WHITE);
        tenths_text(set.tempo_x10, t);
        cv_text(232 - text_w(&FONT_B, t), 6, &FONT_B, t, C_HI);
        cv_text(228 - text_w(&FONT_B, t) - text_w(&FONT_T, "BPM"), 10, &FONT_T, "BPM", C_GRAY);
    }
    cv_rect(0, 27, 240, 1, C_LINE);
    cv_blit(0, 0);
}

/* the whole break: its waveform, the slice markers, the slice sounding lit, the playhead */
static void draw_wave(const brk_status_t *st, const brk_break_t *b)
{
    uint32_t i;
    cv_begin(240, 54, C_BLACK);
    if (b && b->len) {
        int32_t lit_a = -1, lit_z = -1;
        if (st->slice >= 0) {
            lit_a = (int32_t)(b->slice[st->slice] * 240u / b->len);
            lit_z = (int32_t)(b->slice[st->slice + 1] * 240u / b->len);
            cv_rect(lit_a, 2, lit_z - lit_a, 50, C_BLOCK);
        }
        for (i = 0; i < 240u; i++) {
            int32_t h = wave_px[i];
            cv_rect((int32_t)i, 27 - h, 1, 2 * h + 1, (int32_t)i >= lit_a && (int32_t)i < lit_z ? C_HI : C_DIM);
        }
        for (i = 1; i < b->nslices; i++)
            cv_rect((int32_t)(b->slice[i] * 240u / b->len), 0, 1, 5, C_GRAY);
        if (st->slice >= 0)
            cv_rect((int32_t)((uint32_t)st->pos * 240u / b->len), 0, 1, 54, C_WHITE);
    }
    cv_blit(0, 30);
}

/* the slicer: sixteen blocks, one per white key, each with its slice's waveform and number; the one sounding is lit
 * and a key held is outlined */
/* the soundboard: sixteen tiles in key order, 4 x 4; a sound playing fills its tile, burning down like a fuse */
static void draw_sounds(uint32_t keys, uint32_t now)
{
    uint32_t row, k, m = sfx_playing();
    char t[4];
    for (row = 0; row < 2u; row++) {
        cv_begin(240, 52, C_BLACK);
        for (k = row * 8u; k < row * 8u + 8u; k++) {
            int32_t x = 1 + (int32_t)(k % 4u) * 60, y = (int32_t)((k / 4u) % 2u) * 26;
            int on = (m >> k) & 1u, held = (keys >> WK[k]) & 1u;
            int fresh = ui.sfx_hit == (int8_t)k && now - ui.sfx_hit_t < 120u;
            cv_rect(x, y, 58, 24, held || fresh ? C_WHITE : C_BLOCK);
            if (held || fresh)
                cv_rect(x + 2, y + 2, 54, 20, C_BLOCK);
            if (on) {                    /* the fuse: what is left of the sound */
                int32_t w = (int32_t)((1.0f - sfx_progress((int)k)) * 54.0f);
                cv_rect(x + 2, y + 2, w < 2 ? 2 : w, 20, C_HI);
            }
            int_text((int32_t)k + 1, t);
            cv_text(x + 4, y + 6, &FONT_T, t, on ? C_BLACK : C_AMB);
            cv_text(x + 18, y + 5, &FONT_XS, SFX_NAME[k], on ? C_BLACK : C_WHITE);
        }
        cv_blit(0, 88u + row * 52u);
    }
}

static void draw_blocks(const brk_status_t *st, uint32_t keys)
{
    uint32_t row, k, c;
    char t[4];
    for (row = 0; row < 2u; row++) {
        cv_begin(240, 52, C_BLACK);
        for (k = row * 8u; k < row * 8u + 8u; k++) {
            int32_t x = 1 + (int32_t)(k % 8u) * 30, held = (keys >> WK[k]) & 1u;
            int on = st->slice == (int32_t)k, have = k < blk_n;
            uint16_t ground = on ? C_BLOCK_HI : C_BLOCK, ink = on ? C_BLACK : have ? C_GRAY : C_LINE;
            cv_rect(x, 0, 28, 50, held ? C_WHITE : ground);
            if (held)
                cv_rect(x + 2, 2, 24, 46, ground);
            if (have)
                for (c = 0; c < BLK_W - 4u; c++) {   /* the slice's shape, 22 columns of its 26 */
                    int32_t h = blk_px[k][c * BLK_W / (BLK_W - 4u)];
                    cv_rect(x + 3 + (int32_t)c, 19 - h / 2, 1, h + 1, ink);
                }
            int_text((int32_t)k + 1, t);
            cv_text(x + 3, 35, &FONT_T, t, on ? C_BLACK : have ? C_AMB : C_LINE);
        }
        cv_blit(0, 88u + row * 52u);
    }
}

/* the foot: the pads, then the page and its knobs with their values */
static void draw_foot(const brk_status_t *st)
{
    static const char *const KN[NPAGES][4] = {{"FILTER", "SWING", "CMPLX", "CRUNCH"},
                                              {"CMPLX", "ANCHOR", "ROLL", "FILL"},
                                              {"P1", "P2", "P3", "MIX"},
                                              {"LEVEL", "PITCH", "ECHO", "LENGTH"}};
    uint32_t i;
    char t[16];
    cv_begin(240, 48, C_BLACK);
    for (i = 0; i < 11u; i++) {
        int on = (st->pads >> i) & 1u;
        int32_t x = 2 + (int32_t)i * 21;
        cv_rect(x, 0, 20, 14, on ? C_HI : C_BLOCK);
        cv_text(x + (20 - text_w(&FONT_T, PAD_NAME[i])) / 2, 1, &FONT_T, PAD_NAME[i], on ? C_BLACK : C_GRAY);
    }
    for (i = 0; i < 4u; i++) {
        const char *name = KN[ui.page][i];
        if (ui.pad_focus >= 0) {         /* a pad held: its own settings */
            name = PAD_KNOB[ui.pad_focus][i];
            if (!name)
                continue;
            cv_text(4 + (int32_t)i * 60, 18, &FONT_T, name, C_HI);
            pad_knob_value(ui.pad_focus, (int)i, t);
            cv_text(4 + (int32_t)i * 60, 30, &FONT_XS, t, C_WHITE);
            continue;
        }
        if (ui.page == PG_FX && i == 0u)
            name = FX_NAME[ui.fx_focus];
        cv_text(4 + (int32_t)i * 60, 18, &FONT_T, name, ui.page == PG_FX && i == 0u
                ? (set.fx.on[ui.fx_focus] ? C_HI : C_DIM) : C_GRAY);
        knob_value((int)i, t);
        cv_text(4 + (int32_t)i * 60, 30, &FONT_XS, t, C_WHITE);
    }
    cv_blit(0, 192);
}

/* ---------------------------------------------------------- pad animations --- */
#define C_ICE RGB(150, 220, 255)
#define C_DUB RGB(120, 230, 160)
#define FX_Y 90                          /* the animation's strip: 240 x 96 where the slice blocks are */
#define FX_MID 58                        /* its waveform's centre line, in the strip */

static uint32_t fx_hash(uint32_t x)      /* a stable pseudo-random number from x (the screen's dice) */
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}

static void fx_ring(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* a circle of dots */
{
    uint32_t a;
    for (a = 0; a < 64u; a++) {
        float t = (float)a / 64.0f;
        cv_rect(cx + (int32_t)((float)r * brk_sin_turns(t + 0.25f)), cy + (int32_t)((float)r * brk_sin_turns(t)),
                2, 2, c);
    }
}

static void fx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t n = (x1 > x0 ? x1 - x0 : x0 - x1) > (y1 > y0 ? y1 - y0 : y0 - y1) ? (x1 > x0 ? x1 - x0 : x0 - x1)
                                                                            : (y1 > y0 ? y1 - y0 : y0 - y1), i;
    for (i = 0; i <= n; i++)
        cv_rect(x0 + (x1 - x0) * i / (n ? n : 1), y0 + (y1 - y0) * i / (n ? n : 1), 2, 2, c);
}

/* a slice's shape (blk_px) stretched over w columns from x, h times as tall, in colour c */
static void fx_slice(int32_t s, int32_t x, int32_t w, float h, uint16_t c)
{
    int32_t i;
    if (s < 0 || s >= (int32_t)blk_n)
        return;
    for (i = 0; i < w; i++) {
        int32_t v = (int32_t)((float)blk_px[s][(uint32_t)i * BLK_W / (uint32_t)w] * 1.7f * h);
        cv_rect(x + i, FX_MID - v, 1, 2 * v + 1, c);
    }
}

/* the held pad's picture: what it does to the break, moving with it, and its settings */
static void draw_fx(int pad, const brk_status_t *st, uint32_t now)
{
    const brk_padset_t *p = &set.pads;
    const brk_fx_t *f = &set.fx;
    char t[24];
    int32_t i, s = st->slice >= 0 ? st->slice : 0;
    uint32_t held = now - ui.pad_since;
    cv_begin(240, 96, C_BLACK);
    t[0] = 0;
    text_cat(t, PAD_TITLE[pad]);
    if (pad <= PAD_ROLL32) {
        text_cat(t, " ");
        text_cat(t, BRK_ROLL_DIV_NAME[p->roll_div[pad] % BRK_ROLL_DIVS]);
    }
    cv_text(6, 0, &FONT_B, t, C_HI);
    switch (pad <= PAD_ROLL32 ? 0 : pad) {
    case 0: {                            /* ROLL: the caught slice again and again, each copy quieter, the hit lit */
        int32_t rs = st->roll_slice >= 0 ? st->roll_slice : s, cur = (int32_t)(st->roll_n % 8u);
        float g = 1.0f;
        for (i = 0; i < 8; i++) {
            fx_slice(rs, 4 + i * 29, 27, g, i == cur ? C_HI : C_DIM);
            g *= 1.0f - p->roll_decay;
        }
        int_text((int32_t)st->roll_n, t);
        text_cat(t, " HITS");
        cv_text(234 - text_w(&FONT_XS, t), 2, &FONT_XS, t, C_GRAY);
        break;
    }
    case PAD_REVERSE: {                  /* REVERSE: the break running backwards past a fixed playhead, chevrons */
        int32_t off = (int32_t)((float)held * 0.12f * p->rev_speed) % 240;
        for (i = 0; i < 240; i++) {
            int32_t h = wave_px[(uint32_t)(240 + off - i) % 240u] * 3 / 2;
            cv_rect(i, FX_MID - h, 1, 2 * h + 1, i < 120 ? C_DIM : C_HI);
        }
        cv_rect(120, 20, 2, 76, C_WHITE);
        for (i = 0; i < 2; i++) {        /* two chevrons pointing left, sliding */
            int32_t cx = 40 + i * 22 - (int32_t)(held / 40u % 22u), r;
            for (r = 0; r < 9; r++)
                cv_rect(cx + r, FX_MID - r - 1, 3, 2, C_WHITE), cv_rect(cx + r, FX_MID + r, 3, 2, C_WHITE);
        }
        break;
    }
    case PAD_FREEZE: {                   /* FREEZE: the slice iced over, its loop bracketed, a cursor going round */
        int32_t loop = 240 >> (p->frz_len & 3u), cx;
        fx_slice(s, 0, 240, 1.0f, C_LINE);
        fx_slice(s, 0, 240, 1.0f, C_ICE);
        cv_rect(0, 22, loop, 2, C_WHITE);
        cv_rect(0, 92, loop, 2, C_WHITE);
        cv_rect(loop - 2, 22, 2, 72, C_WHITE);
        cx = (int32_t)(held % 700u) * loop / 700;
        cv_rect(cx, 22, 2, 72, C_HI);
        for (i = 0; i < 24; i++) {       /* frost: dots that twinkle */
            uint32_t r = fx_hash((uint32_t)i * 977u + held / 180u);
            cv_rect((int32_t)(r % 236u), 24 + (int32_t)((r >> 8) % 66u), 2, 2, (r >> 20) & 1u ? C_WHITE : C_ICE);
        }
        break;
    }
    case PAD_HALF: {                     /* HALF SPEED: the break stretched, crawling */
        int32_t off = (int32_t)((float)held * 0.05f * p->half_speed);
        for (i = 0; i < 240; i++) {
            int32_t h = wave_px[(uint32_t)((i + off) / 2) % 240u] * 3 / 2;
            cv_rect(i, FX_MID - h, 1, 2 * h + 1, (i / 2) % 2 ? C_HI : C_AMB);
        }
        times_text(p->half_speed, t);
        cv_text(234 - text_w(&FONT_B, t), 0, &FONT_B, t, C_WHITE);
        break;
    }
    case PAD_TAPESTOP: {                 /* TAPE STOP: two reels slowing to a stop, the tape between them */
        int32_t k;
        for (k = 0; k < 2; k++) {
            int32_t cx = k ? 176 : 64, j;
            fx_ring(cx, FX_MID + 2, 30, C_GRAY);
            fx_ring(cx, FX_MID + 2, 6, C_HI);
            for (j = 0; j < 3; j++) {    /* the spokes, turning with the tape */
                float a = ui.reel + (float)j / 3.0f + (k ? 0.1f : 0.0f);
                fx_line(cx, FX_MID + 2, cx + (int32_t)(27.0f * brk_sin_turns(a + 0.25f)),
                        FX_MID + 2 + (int32_t)(27.0f * brk_sin_turns(a)), C_HI);
            }
        }
        cv_rect(64, FX_MID + 32, 112, 2, C_GRAY);
        pct_text(st->tape, t);
        cv_text(120 - text_w(&FONT_XS, t) / 2, 22, &FONT_XS, t, C_WHITE);
        break;
    }
    case PAD_GATE: {                     /* GATE: the bar's gates, open part lit, the one playing bright */
        int32_t cells = 8 << (p->gate_div % BRK_GATE_DIVS), w = 236 / cells, curc;
        float pos = (float)(st->step < 0 ? 0 : st->step % 16) + st->phase;
        curc = (int32_t)(pos * (float)cells / 16.0f) % cells;
        for (i = 0; i < cells; i++) {
            int32_t open = (int32_t)((float)(w - 1) * p->gate_len);
            cv_rect(2 + i * w, 34, w - 1, 48, C_BLOCK);
            cv_rect(2 + i * w, 34, open < 1 ? 1 : open, 48, i == curc ? (st->gate > 0.5f ? C_WHITE : C_HI) : C_AMB);
        }
        break;
    }
    case PAD_RANDOM: {                   /* RANDOM: dice over the slices, the one sounding big */
        uint32_t seed = (uint32_t)(st->step + 1) * 2654435761u;
        for (i = 0; i < 16; i++) {
            int32_t x = 120 + (i % 8) * 15, y = 30 + (i / 8) * 32;
            uint32_t r = fx_hash(seed + (uint32_t)i);
            cv_rect(x, y, 13, 28, i == s ? C_HI : (r & 3u) == 0u ? C_GRAY : C_BLOCK);
        }
        int_text(s + 1, t);
        cv_text(20, 30, &FONT_L, t, C_WHITE);
        if (p->rnd_pitch > 0.0f)
            cv_text(20, 72, &FONT_T, "+ PITCH", C_AMB);
        break;
    }
    case PAD_FILTER: {                   /* FILTER: the response curve, its cutoff sliding to the target */
        float dj = brk_fx_dj_now(), xc = dj > 0.0f ? dj * 220.0f + 10.0f : (1.0f + dj) * 220.0f + 10.0f;
        float res = f->flt_res;
        for (i = 0; i < 240; i++) {
            float x = (float)i, d = dj > 0.0f ? xc - x : x - xc, y;
            y = d < 0.0f ? 1.0f : 1.0f / (1.0f + d * d * 0.004f);   /* past the cutoff: falls away */
            if (d > -12.0f && d < 12.0f)
                y += res * 0.8f * (1.0f - brk_absf(d) / 12.0f);     /* the resonance's bump */
            {
                int32_t h = (int32_t)(y * 34.0f);
                cv_rect(i, 92 - h, 1, h, i % 2 ? C_AMB : C_HI);
            }
        }
        cv_rect((int32_t)xc, 22, 2, 72, C_WHITE);
        dj_text(dj, t);
        cv_text(234 - text_w(&FONT_XS, t), 2, &FONT_XS, t, C_WHITE);
        break;
    }
    case PAD_DUB: {                      /* DUB: echoes marching out, each one fainter and darker */
        static const uint8_t SP[BRK_DUB_DIVS] = {2, 3, 4, 6};
        float tempo = (float)set.tempo_x10 * 0.1f, step_ms = 60000.0f / (tempo * 4.0f);
        float per = step_ms * (float)SP[f->dub_div % BRK_DUB_DIVS], ph = (float)(held % (uint32_t)per) / per;
        int32_t sp = 26 + 8 * (int32_t)SP[f->dub_div % BRK_DUB_DIVS] / 2;
        float g = 1.0f;
        for (i = 0; i < 10; i++) {
            int32_t x = 6 + i * sp + (int32_t)(ph * (float)sp);
            if (x > 236)
                break;
            fx_slice(s, x, 18, g, g > 0.6f ? C_DUB : g > 0.3f ? C_GRAY : C_LINE);
            g *= f->dub_fb;
        }
        break;
    }
    default: break;
    }
    cv_blit(0, FX_Y);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.last_step = ui.last_slice = -2;
    ui.intro = 1u;                       /* the start screen first */
    ui.intro_t0 = plat_ms();
    ui.intro_drawn = 0u;
    ui.touched = 0u;
    ui.pad_focus = ui.pad_shown = -1;
    ui.sfx_hit = -1;
    ui.redraw = RG_HEAD;
}

void app_frame(void)
{
    brk_status_t st;
    const brk_break_t *b;
    uint32_t now = plat_ms(), hdr;
    input();
    break_poll();
    leds();
    if (ui.intro) {                      /* any key or button opens the slicer (PLAY also starts the break) */
        if (ui.touched || now - ui.intro_t0 > 16u * INTRO_STEP + INTRO_HOLD)
            intro_end();
        else {
            intro_draw(now);
            return;
        }
    }
    ui.touched = 0u;
    brk_status(&st);
    b = brk_get_break();
    if (st.step != ui.last_step || st.slice != ui.last_slice) {   /* the playhead moved */
        ui.last_step = st.step;
        ui.last_slice = st.slice;
        ui.redraw |= RG_WAVE | RG_BLOCKS;
    }
    if (ui.keys != ui.last_keys) {
        ui.last_keys = ui.keys;
        ui.redraw |= RG_BLOCKS;
    }
    if (ui.page == PG_SOUND) {           /* the soundboard's fuses burn at about 20 frames a second */
        uint32_t m = sfx_playing();
        if (m != ui.sfx_mask || (m && now - ui.sfx_drawn >= 50u) ||
            (ui.sfx_hit >= 0 && now - ui.sfx_hit_t < 200u && now - ui.sfx_drawn >= 50u))
            ui.redraw |= RG_BLOCKS;
        ui.sfx_mask = m;
    }
    if (foot_dirty) {
        foot_dirty = 0u;
        ui.redraw |= RG_FOOT;
    }
    if (ui.pad_focus != ui.pad_shown) {  /* a pad taken or let go: the animation, or the blocks back */
        if (ui.pad_focus >= 0 && ui.pad_shown < 0)
            lcd_fill(0, 86, 240, 106, C_BLACK);
        ui.pad_shown = ui.pad_focus;
        ui.redraw |= RG_FOOT | (ui.pad_focus >= 0 ? RG_FX : RG_BLOCKS);
        if (ui.pad_focus < 0)
            lcd_fill(0, 86, 240, 106, C_BLACK);
    }
    if (ui.pad_focus >= 0) {             /* the animation runs at about 30 frames a second */
        if (ui.pad_focus == PAD_TAPESTOP)
            ui.reel += 0.06f * st.tape;
        if (now - ui.fx_drawn >= 33u)
            ui.redraw |= RG_FX;
        ui.redraw &= ~(uint8_t)RG_BLOCKS;
    }
    if (st.pads != ui.last_pads) {
        ui.last_pads = st.pads;
        ui.redraw |= RG_FOOT;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0u;
        ui.redraw |= RG_HEAD;
    }
    /* the header's own state: playing, the tempo, the break */
    hdr = (uint32_t)st.playing | (uint32_t)set.tempo_x10 << 1 | (uint32_t)set.brk << 13 | (b ? 1u : 0u) << 20;
    if (hdr != ui.last_hdr) {
        ui.last_hdr = hdr;
        ui.redraw |= RG_HEAD;
    }
    if (!ui.redraw)
        return;
    if (ui.redraw & RG_HEAD)
        draw_head(&st, b, now);
    if (ui.redraw & RG_WAVE)
        draw_wave(&st, b);
    if (ui.redraw & RG_FX) {
        draw_fx(ui.pad_focus, &st, now);
        ui.fx_drawn = now;
    }
    if (ui.redraw & RG_BLOCKS) {
        if (ui.page == PG_SOUND) {
            draw_sounds(ui.keys, now);
            ui.sfx_drawn = now;
        } else
            draw_blocks(&st, ui.keys);
    }
    if (ui.redraw & RG_FOOT)
        draw_foot(&st);
    ui.redraw = 0u;
}

#ifdef PLAT_HOST
/* the host simulator's commands: "splash" draws the power-on screen (the device draws it before app_init; the
 * simulator never does) */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (!strcmp(cmd, "splash")) {
        app_splash();
        return 1;
    }
    return 0;
}
#endif
