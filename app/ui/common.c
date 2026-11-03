/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the UI's state, its text helpers, messages, tap tempo, the screen's colours and big text. (Part of
 * app.c's unity: included there in order.) */

enum { PG_HOME, PG_GEN, PG_FX, PG_GLOBAL, PG_KICK, PG_MIX, PG_SEQ, PG_PERC, PG_TAP, PG_FILL, NPAGES };
static const char *const PAD_NAME[BRK_NPADS] = {"R8", "R16", "R32", "REV", "FRZ", "HLF", "STP", "GAT", "RND",
                                                "FLT", "DUB"};
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;

#define TAPS 8u                          /* tap tempo: the last eight taps (seven intervals) average */
static struct {
    uint32_t keys, btn;
    uint8_t page, fx_focus;              /* fx_focus: the FX chain's slot the FX page tunes (0..3) */
    char msg[2][24];
    uint32_t msg_until;
    uint32_t taps[TAPS], ntaps;          /* tap tempo on SEL: the taps' times, how many in this run */
    uint32_t tap_t;                      /* SEL went down then: a tap if it comes up alone */
    uint8_t tap_pend, tap_combo;         /* SEL down as a tap; something else touched while it was held */
    uint32_t oct_t, oct_rep;             /* OCT- / OCT+ held: repeats that speed up */
    int32_t last_step, last_slice;
    uint32_t last_pads, last_keys, last_hdr;
    uint32_t intro_t0;                   /* the start screen: when it began (0: over) */
    uint8_t intro, intro_drawn;          /* showing it; blocks drawn so far */
    uint8_t touched;                     /* a key, button or encoder moved since app_frame last looked */
    uint32_t panel_drawn;                /* the GLOBAL panel's, the MB COMP meter's last frame */
    int8_t pad_focus;                    /* the pad held last (its animation, K1-K4 tune it); -1 none */
    int8_t key_focus;                    /* the white key held that loops (its view, K1-K4 tune it); -1 none */
    uint8_t key_layer;                   /* K1-K4 on a key held: 0 its loop, 1 its slice's effect (FX flips) */
    uint8_t key_turned;                  /* a knob turned while it is held: its view at once */
    uint32_t key_since;                  /* when it was pressed */
    int16_t shown;                       /* the strip shows: -1 the blocks, 0..10 a pad, 100 + k white key k */
    uint8_t gpage;                       /* the GLOBAL page's part: 0 MIDI and Bluetooth, 1 pitch, 2 BT audio */
    uint8_t bta_sel, bta_booted;         /* BT AUDIO: the device chosen in its list; the boot's reconnect done */
    uint8_t kpage, klayer, kpreset;      /* the KICK page: its sub-page (KP_*), the layer it edits, the preset */
    uint8_t mpage;                       /* the MIXER's half: 0 LEVELS (break, kick, perc, FX), 1 GROOVE */
    uint8_t seq_lane, seq_step;          /* the SEQ page: the lane (SL_*) and the break step (0..31) it edits */
    uint8_t pvoice, ppage, ppreset[PERC_VOICES];   /* the SNARE / HAT page: its voice, sub-page, presets */
    uint8_t einst;                       /* EDIT, the drums' design: 0 the kick (PG_KICK), 1 the snare, 2 the hat (PG_PERC) */
    uint8_t rec;                         /* REC: white keys and MIDI notes write into the pattern as it plays */
    uint8_t help;                        /* SEL pressed alone: the SEL combinations on the strip, until the next press */
    uint32_t help_t, tap_last;           /* when the card opened; the last tap (TAP's flash) */
    uint8_t drawn_page;                  /* the page last drawn (TAP takes the whole screen: leaving it redraws all) */
    uint32_t lobo_t;                     /* RESET ALL done: the lobotomy screen, since then (0: not showing) */
    uint8_t home_hold, home_combo;       /* HOME held on its page (up alone: its other part); PLAY pressed meanwhile */
    uint8_t fx_hold, fx_turned;          /* FX held on the FX page (up alone: the next slot); a knob turned meanwhile */
    uint32_t reset_t;                    /* GLOBAL's RESET ALL: when it was first turned (it asks twice) */
    uint32_t mix_drawn;                  /* its faders' meters: when last drawn */
    int16_t meter_x;                     /* the header's level meter: where (0: not shown now, a message is) */
    uint32_t meter_drawn;
    uint32_t pad_since, fx_drawn;        /* when it was pressed; the animation's last frame */
    float reel;                          /* the tape stop's reels, turns */
    uint8_t redraw;                      /* RG_* bits: the regions to draw again */
} ui;
/* the white keys play slices (or, on the SEQ, KICK and SNARE / HAT pages, steps) */
static void page_say(void);              /* the header: the page's button and part, as its tabs (main.c) */
static int slices_page(void) { return ui.page != PG_SEQ && ui.page != PG_TAP && ui.page != PG_FILL; }   /* (FILL: fills) */
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

/* tap tempo (PRD A-38): SEL tapped in time, on any page. The tempo follows from the second tap on, the mean of the
 * last eight taps; a pause of 2 s starts a new run, and a tap far off the run's pace (under 0.7 or over 1.4 of it)
 * starts one from the tap before. A SEL that is part of a combination (SEL + ALGORITHM, + EDIT, + a knob ...) is no
 * tap: the tap counts when SEL comes up alone, at the time it went down */
static void tap_tempo(uint32_t t)
{
    char a[12], v[16];
    uint32_t n, span;
    if (ui.ntaps && t - ui.taps[(ui.ntaps - 1u) % TAPS] > 2000u)
        ui.ntaps = 0u;                   /* a pause starts a new run */
    if (ui.ntaps >= 3u) {                /* far off the run's pace: a new run from the last tap */
        uint32_t last = ui.taps[(ui.ntaps - 1u) % TAPS], m = ui.ntaps < TAPS ? ui.ntaps : TAPS;
        uint32_t avg = (last - ui.taps[(ui.ntaps - m) % TAPS]) / (m - 1u), iv = t - last;
        if (iv * 10u < avg * 7u || iv * 10u > avg * 14u) {
            ui.taps[0] = last;
            ui.ntaps = 1u;
        }
    }
    ui.taps[ui.ntaps % TAPS] = t;
    ui.ntaps++;
    ui.tap_last = t | 1u;
    ui.redraw |= 4u;                     /* (RG_BLOCKS: the TAP page) */
    a[0] = 0;
    text_cat(a, "TAP ");
    int_text((int32_t)(ui.ntaps < 99u ? ui.ntaps : 99u), a + 4);
    if (ui.ntaps < 2u) {
        app_say(a, "AGAIN IN TIME");
        return;
    }
    n = ui.ntaps < TAPS ? ui.ntaps : TAPS;
    span = t - ui.taps[(ui.ntaps - n) % TAPS];
    if (!span)
        return;
    n = (n - 1u) * 600000u / span;       /* BPM x 10 */
    set.tempo_x10 = (uint16_t)(n < 400u ? 400u : n > 3000u ? 3000u : n);
    changed();
    tenths_text(set.tempo_x10, v);
    text_cat(v, " BPM");
    app_say(a, v);
}

/* ------------------------------------------------------------- the FX chain --- */

static void hz_text(float hz, char *t)   /* 340 -> "340HZ", 1234 -> "1.2K" */
{
    t[0] = 0;
    if (hz >= 1000.0f) {
        tenths_text((int32_t)(hz / 100.0f + 0.5f), t);
        text_cat(t, "K");
    } else {
        int_text((int32_t)(hz + 0.5f), t);
        text_cat(t, "HZ");
    }
}

static void hundredths_text(float v, char *t)   /* 0.25 -> "0.25" (0..99.99) */
{
    int32_t c = (int32_t)(v * 100.0f + 0.5f);
    char d[4];
    int_text(c / 100, t);
    text_cat(t, ".");
    d[0] = (char)('0' + c / 10 % 10);
    d[1] = (char)('0' + c % 10);
    d[2] = 0;
    text_cat(t, d);
}

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

/* ------------------------------------------------------------- start screen --- */
#define C_BLOCK RGB(24, 24, 30)          /* a slice block's ground */
#define C_BLOCK_HI RGB(255, 184, 70)     /* the block sounding */

/* big text: a font at twice its size, each of its pixels a 2 x 2 block (blended as cv_text blends). It stands in for
 * a 31-pixel font: that one costs 20 KB of flash, which Bluetooth audio needed */
static int32_t text2_w(const felucca_font_t *f, const char *s) { return 2 * text_w(f, s); }
static int32_t cv_textn(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint32_t n);
static int32_t cv_text2(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    return cv_textn(x, y, f, s, c, 2u);
}
/* a font at n times its size (TAP's tempo: 3) */
static int32_t cv_textn(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint32_t n)
{
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u;
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t a = gd[gy * bpr + gx / 2u], k;
                a = (gx & 1u) ? (a & 15u) : (a >> 4);
                if (!a)
                    continue;
                for (k = 0; k < n * n; k++) {
                    int32_t px = x + (int32_t)n * ((int32_t)gx - f->pad) + (int32_t)(k % n),
                            py = y + (int32_t)n * (int32_t)gy + (int32_t)(k / n) + cv_oy;
                    uint32_t at, bg;
                    if ((uint32_t)px >= cv_w || py < cv_y0 || py >= cv_y1)
                        continue;
                    at = (uint32_t)py * cv_w + (uint32_t)px;
                    bg = swap16(cv_px[at]);
                    cv_px[at] = swap16((((r * a + (bg >> 11) * (15u - a)) / 15u) << 11) |
                                       (((g * a + ((bg >> 5) & 63u) * (15u - a)) / 15u) << 5) |
                                       ((b * a + (bg & 31u) * (15u - a)) / 15u));
                }
            }
        x += (int32_t)n * f->adv[gi];
    }
    return x;
}
