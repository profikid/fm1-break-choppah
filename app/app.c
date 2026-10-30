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
#include "brk_synth.h"
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
#define SET_MAGIC 0x31504843u            /* "CHP1": another magic loads the defaults */
typedef struct {
    uint32_t magic;
    uint16_t tempo_x10;                  /* 400..3000 */
    int8_t pitch;                        /* semitones */
    uint8_t swing;                       /* 50..75 % */
    brk_gen_t gen;
    brk_fx_t fx;
    uint8_t brk;                         /* the break: 0 the synthesised one, 1.. the bank's slots */
    uint8_t rsv[15];
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
    set.brk = CH_HAVE_BANK ? 1u : 0u;
}

static void settings_apply(void)
{
    brk_tempo((float)set.tempo_x10 * 0.1f);
    brk_pitch((float)set.pitch);
    brk_swing((float)set.swing * 0.01f);
    brk_gen(&set.gen);
    brk_fx_set(&set.fx);
}

static void changed(void)
{
    set_dirty = 1u;
    foot_dirty = 1u;
    settings_apply();
}

/* ------------------------------------------------------------------- engine --- */
static int16_t pcm_a[CH_PCM_MAX] PLAT_POOL;
static brk_break_t brk0;
static brk_break_t brk_cur;              /* the bank's break playing (pcm_a holds it) */
static uint32_t n_breaks = 1u;           /* the synthesised one, then the bank's valid slots */
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
    const bank_slot_t *s = i ? bank_slot(i - 1u) : 0;
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
    } else {
        brk_synth_break(pcm_a, CH_RATE, 136.9f, 1u, &brk0);
        i = 0u;
    }
    set.brk = (uint8_t)i;
    wave_build(b);
    brk_set_break(b);
}

void app_init(void)
{
    brk_init();
    brk_fx_init();
    while (n_breaks <= BANK_SLOTS && bank_slot(n_breaks - 1u))
        n_breaks++;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.tempo_x10 < 400u || set.tempo_x10 > 3000u || set.swing < 50u || set.swing > 75u)
        settings_defaults();
    if (set.brk >= n_breaks)
        set.brk = n_breaks > 1u ? 1u : 0u;
    break_load(set.brk);                 /* boot: the audio isn't running yet */
    settings_apply();
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float mono[APP_BLOCK], l[APP_BLOCK], r[APP_BLOCK];
    float gain = muted ? 0.0f : (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f;
    brk_status_t st;
    while (n) {
        uint32_t m = n < APP_BLOCK ? n : APP_BLOCK, i;
        brk_render(mono, m);
        brk_status(&st);
        brk_fx_process(mono, l, r, m, st.pads);
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
enum { PG_HOME, PG_GEN, PG_FX, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"PERFORM", "GENERATOR", "FX"};
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
    uint8_t redraw;                      /* RG_* bits: the regions to draw again */
} ui;
enum { RG_HEAD = 1, RG_WAVE = 2, RG_BLOCKS = 4, RG_FOOT = 8, RG_ALL = 15 };

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

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms(), i;
    uint32_t kdn = keys & ~ui.keys, kup = ui.keys & ~keys, bdn = btn & ~ui.btn;
    int k;
    if (keys != ui.keys || btn != ui.btn)
        ui.touched = 1u;                 /* (read here too between frames: app_input_only) */
    for (i = 0; i < 16u; i++) {          /* white keys: slices */
        if (kdn >> WK[i] & 1u)
            brk_key((int)i, 1);
        if (kup >> WK[i] & 1u)
            brk_key((int)i, 0);
    }
    for (i = 0; i < 11u; i++) {          /* black keys: the pads, held */
        if (kdn >> BK[i] & 1u)
            brk_pad((int)i, 1);
        if (kup >> BK[i] & 1u)
            brk_pad((int)i, 0);
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
    b |= 1u << (ui.page == PG_HOME ? B_HOME : ui.page == PG_GEN ? B_ARP : B_FX);
    if (set_dirty)
        b |= 1u << B_SAVE;
    if (st.slice >= 0 && st.slice < 16)
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
                                              {"P1", "P2", "P3", "MIX"}};
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
        if (ui.page == PG_FX && i == 0u)
            name = FX_NAME[ui.fx_focus];
        cv_text(4 + (int32_t)i * 60, 18, &FONT_T, name, ui.page == PG_FX && i == 0u
                ? (set.fx.on[ui.fx_focus] ? C_HI : C_DIM) : C_GRAY);
        knob_value((int)i, t);
        cv_text(4 + (int32_t)i * 60, 30, &FONT_XS, t, C_WHITE);
    }
    cv_blit(0, 192);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.last_step = ui.last_slice = -2;
    ui.intro = 1u;                       /* the start screen first */
    ui.intro_t0 = plat_ms();
    ui.intro_drawn = 0u;
    ui.touched = 0u;
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
    if (foot_dirty) {
        foot_dirty = 0u;
        ui.redraw |= RG_FOOT;
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
    if (ui.redraw & RG_BLOCKS)
        draw_blocks(&st, ui.keys);
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
