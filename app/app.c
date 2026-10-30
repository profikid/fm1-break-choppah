/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH on fm1-base: the app's unity build (compiled into fm1-base's device/unity.c after the platform, so gfx.c's
 * cv_* drawing and the plat_* calls are in scope) and the glue between the contract (app.h) and the engine
 * (dsp/brk.c, dsp/brk_fx.c: their own -O2 units).
 *
 * M1 scope (PRD 13): the built-in break, 16 slices on the white keys, the 11 pads on the black keys, play / reset,
 * tempo and tap tempo, pitch on OCT-/OCT+, the generator, the four FX and the DJ filter, settings kept by SAVE. */
#include "brk_synth.h"

/* ------------------------------------------------------------------ settings --- */
#define SET_MAGIC 0x31504843u            /* "CHP1": another magic loads the defaults */
typedef struct {
    uint32_t magic;
    uint16_t tempo_x10;                  /* 400..3000 */
    int8_t pitch;                        /* semitones */
    uint8_t swing;                       /* 50..75 % */
    brk_gen_t gen;
    brk_fx_t fx;
    uint8_t rsv[16];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

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
    settings_apply();
}

/* ------------------------------------------------------------------- engine --- */
static int16_t pcm_a[CH_PCM_MAX] PLAT_POOL;
static brk_break_t brk0;
static uint8_t wave_px[240];             /* the waveform's height per column, for the screen */
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
        wave_px[x] = (uint8_t)(pk * 30u / 32768u);
    }
}

void app_init(void)
{
    uint32_t n = brk_synth_len(CH_RATE, 136.9f, 1u);
    brk_init();
    brk_fx_init();
    if (n <= CH_PCM_MAX) {
        brk_synth_break(pcm_a, CH_RATE, 136.9f, 1u, &brk0);
        brk_set_break(&brk0);
        wave_build(&brk0);
    }
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.tempo_x10 < 400u || set.tempo_x10 > 3000u || set.swing < 50u || set.swing > 75u)
        settings_defaults();
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
    int32_t last_step;
    uint8_t redraw;
} ui;

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
    ui.redraw = 1u;
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

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms(), i;
    uint32_t kdn = keys & ~ui.keys, kup = ui.keys & ~keys, bdn = btn & ~ui.btn;
    int k;
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
        ui.redraw = 1u;
    }
    if (bdn & 1u << B_ARP) {
        if (btn & 1u << B_SEL) {         /* SEL + ARP: a random pattern */
            brk_randomize();
            app_say("RANDOMIZE", "");
        }
        ui.page = PG_GEN;
        ui.redraw = 1u;
    }
    if (bdn & 1u << B_FX) {              /* FX: the page; again: the focused effect on / off */
        if (ui.page == PG_FX) {
            set.fx.on[ui.fx_focus] ^= 1u;
            app_say(FX_NAME[ui.fx_focus], set.fx.on[ui.fx_focus] ? "ON" : "OFF");
            changed();
        }
        ui.page = PG_FX;
        ui.redraw = 1u;
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
            } else
                app_say("BREAK", brk0.name);
        } else if (k == EN_PRESET)
            app_say("PATTERN", "1");     /* patterns arrive in M2 */
        else
            knob_turn(k, d);
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1u;
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

static void draw(void)
{
    brk_status_t st;
    const brk_break_t *b = brk_get_break();
    char t[24];
    uint32_t i, now = plat_ms();
    brk_status(&st);
    /* the header: name and tempo, or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(8, 6, &FONT_B, APP_NAME, C_WHITE);
        tenths_text(set.tempo_x10, t);
        cv_text(232 - text_w(&FONT_B, t), 6, &FONT_B, t, C_HI);
        cv_text(200 - text_w(&FONT_B, t), 10, &FONT_XS, "BPM", C_GRAY);
    }
    cv_blit(0, 0);
    /* the break: its waveform, the slices, the one sounding lit, the playhead */
    cv_begin(240, 80, C_BLACK);
    if (b && b->len) {
        int32_t lit_a = -1, lit_z = -1;
        if (st.slice >= 0) {
            lit_a = (int32_t)(b->slice[st.slice] * 240u / b->len);
            lit_z = (int32_t)(b->slice[st.slice + 1] * 240u / b->len);
        }
        for (i = 0; i < 240u; i++) {
            int32_t h = wave_px[i];
            cv_rect((int32_t)i, 40 - h, 1, 2 * h + 1, (int32_t)i >= lit_a && (int32_t)i < lit_z ? C_HI : C_DIM);
        }
        for (i = 1; i < b->nslices; i++)
            cv_rect((int32_t)(b->slice[i] * 240u / b->len), 0, 1, 6, C_LINE);
        if (st.slice >= 0)
            cv_rect((int32_t)((uint32_t)st.pos * 240u / b->len), 0, 1, 80, C_WHITE);
    }
    cv_blit(0, 40);
    /* the pads, then the page and its four knobs */
    cv_begin(240, 96, C_BLACK);
    for (i = 0; i < 11u; i++) {
        int on = (st.pads >> i) & 1u;
        int32_t x = 2 + (int32_t)i * 21;
        cv_rect(x, 2, 20, 16, on ? C_HI : C_LINE);
        cv_text(x + (20 - text_w(&FONT_T, PAD_NAME[i])) / 2, 5, &FONT_T, PAD_NAME[i], on ? C_BLACK : C_GRAY);
    }
    cv_text(8, 28, &FONT_XS, ui.page == PG_FX ? FX_NAME[ui.fx_focus] : PAGE_NAME[ui.page], C_AMB);
    if (ui.page == PG_FX)
        cv_text(120, 28, &FONT_XS, set.fx.on[ui.fx_focus] ? "ON" : "OFF", set.fx.on[ui.fx_focus] ? C_HI : C_DIM);
    {
        static const char *const KN[NPAGES][4] = {{"FILTER", "SWING", "CMPLX", "CRUNCH"},
                                                  {"CMPLX", "ANCHOR", "ROLL", "FILL"},
                                                  {"P1", "P2", "P3", "MIX"}};
        for (i = 0; i < 4u; i++)
            cv_text(8 + (int32_t)i * 58, 50, &FONT_XS, KN[ui.page][i], C_GRAY);
    }
    int_text(st.playing ? (int32_t)st.bar : 0, t);
    text_cat(t, ".");
    {
        char s[8];
        int_text(st.step < 0 ? 1 : st.step / 4 + 1, s);
        text_cat(t, s);
    }
    cv_text(8, 72, &FONT_B, t, st.playing ? C_WHITE : C_DIM);
    cv_text(120, 76, &FONT_XS, b ? b->name : "NO BREAK", C_GRAY);
    cv_blit(0, 136);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.last_step = -2;
    ui.redraw = 1u;
}

void app_frame(void)
{
    brk_status_t st;
    uint32_t now = plat_ms();
    input();
    brk_status(&st);
    if (st.step != ui.last_step) {       /* the playhead moves: draw again */
        ui.last_step = st.step;
        ui.redraw = 1u;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0u;
        ui.redraw = 1u;
    }
    leds();
    if (ui.redraw) {
        ui.redraw = 0u;
        draw();
    }
}
