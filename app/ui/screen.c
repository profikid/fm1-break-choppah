/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the start screen, the header, the break's waveform and the slice blocks. (Part of app.c's unity:
 * included there in order.) */

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

/* the platform's own screens' faces (APP_SYS_TEXT, device/sys_text.c): the body FONT_XS, a heading FONT_B, the big
 * one FONT_B doubled (the device's: the simulator draws none of them) */
#ifndef PLAT_HOST
static int32_t app_sys_h(int face)
{
    return face == SYS_BIG ? 2 * (int32_t)FONT_B.h : face == SYS_B ? FONT_B.h : FONT_XS.h;
}
static int32_t app_sys_w(int face, const char *s)
{
    return face == SYS_BIG ? text2_w(&FONT_B, s) : text_w(face == SYS_B ? &FONT_B : &FONT_XS, s);
}
static void app_sys_text(int32_t x, int32_t y, int face, const char *s, uint16_t c)
{
    if (face == SYS_BIG)
        cv_text2(x, y, &FONT_B, s, c);
    else
        cv_text(x, y, face == SYS_B ? &FONT_B : &FONT_XS, s, c);
}
#endif

static void title(int32_t y)             /* the name and the tagline, centred, into the current strip */
{
    cv_text2((240 - text2_w(&FONT_B, APP_NAME)) / 2, y, &FONT_B, APP_NAME, C_WHITE);
    cv_text((240 - text_w(&FONT_XS, "AMEN CHOPPER  FOR THE FM-1")) / 2, y + 36, &FONT_XS,
            "AMEN CHOPPER  FOR THE FM-1", C_HI);
}

/* the version, as the screens show it: "V0.7" (a release), "DEV" (any other build) */
static void version_text(char *t)
{
    t[0] = 0;
    if (CH_VERSION[0] >= '0' && CH_VERSION[0] <= '9')
        text_cat(t, "V");
    text_cat(t, CH_VERSION);
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
    {
        char t[16];
        version_text(t);
        cv_begin(240, 20, C_BLACK);
        cv_text((240 - text_w(&FONT_T, t)) / 2, 2, &FONT_T, t, C_GRAY);
        cv_blit(0, 210);
    }
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
        cv_begin(240, 20, C_BLACK);      /* the version, at the top */
        version_text(t);
        cv_text((240 - text_w(&FONT_T, t)) / 2, 2, &FONT_T, t, C_GRAY);
        cv_blit(0, 16);
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

/* TAP TEMPO (ENV): the whole screen between the tab bar and the pads. The tempo on an LED display (seven-segment
 * digits drawn as bevelled bars, the unlit ones a ghost), a metronome swinging in time (its bob lit on each beat,
 * white on the bar's first), a ripple from its pivot on each tap. The display redraws when the tempo or the taps
 * change; the metronome about 25 times a second */
#define TP_OFF RGB(44, 30, 10)            /* an unlit segment */
static void tp_seg(int32_t x, int32_t y, int32_t len, int horiz, uint16_t c)   /* a bevelled bar, 6 thick */
{
    int32_t i;
    for (i = 0; i < 6; i++) {
        int32_t in = i < 3 ? 2 - i : i - 3;       /* 2 1 0 0 1 2: the bevel */
        if (horiz)
            cv_rect(x + in, y + i, len - 2 * in, 1, c);
        else
            cv_rect(x + i, y + in, 1, len - 2 * in, c);
    }
}
static void tp_digit(int32_t x, int32_t y, int d, uint16_t on)   /* 30 wide, 50 tall; d < 0: all unlit */
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = d >= 0 && d <= 9 ? SEG[d] : 0u;
    tp_seg(x + 3, y, 24, 1, m & 1u ? on : TP_OFF);                /* a */
    tp_seg(x + 24, y + 3, 21, 0, m & 2u ? on : TP_OFF);           /* b */
    tp_seg(x + 24, y + 26, 21, 0, m & 4u ? on : TP_OFF);          /* c */
    tp_seg(x + 3, y + 44, 24, 1, m & 8u ? on : TP_OFF);           /* d */
    tp_seg(x, y + 26, 21, 0, m & 16u ? on : TP_OFF);              /* e */
    tp_seg(x, y + 3, 21, 0, m & 32u ? on : TP_OFF);               /* f */
    tp_seg(x + 3, y + 22, 24, 1, m & 64u ? on : TP_OFF);          /* g */
}
static void tp_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    uint32_t i, n = (uint32_t)r * 6u + 24u;
    for (i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        cv_rect(cx + (int32_t)((float)r * brk_sin_turns(t + 0.25f)), cy + (int32_t)((float)r * brk_sin_turns(t)), 2, 2, c);
    }
}
static void draw_tap(const brk_status_t *st, uint32_t now, int full)
{
    static uint32_t a_sig = 0xFFFFFFFFu;
    int ext = ext_active(), flash = ui.tap_last && now - ui.tap_last < 110u;
    uint32_t v = set.tempo_x10, sig = v | (uint32_t)flash << 16 | (uint32_t)ext << 17 | (ui.ntaps < 15u ? ui.ntaps : 15u) << 18 |
                                      (uint32_t)(set.click != 0u) << 22;
    uint32_t band;
    float beats, sw, fr;
    int32_t beat, px, py, bx, by, i;
    if (st->playing && st->bar16 >= 0)   /* where in the bar: the engine's, or (stopped) the clock's at the tempo */
        beats = ((float)st->bar16 + st->phase) * 0.25f;
    else
        beats = (float)(now % 600000u) * (float)v * (1.0f / 600000.0f);
    beat = (int32_t)beats;
    fr = beats - (float)beat;
    sw = 0.42f * brk_sin_turns(beats * 0.5f + 0.25f);   /* the swing: at an end on each beat (radians) */
    px = 120;
    py = 186;
    bx = px + (int32_t)(68.0f * brk_sin_turns(sw * 0.159155f));
    by = py - (int32_t)(68.0f * brk_sin_turns(sw * 0.159155f + 0.25f));
    for (band = full || sig != a_sig ? 0u : 1u; band < 3u; band++) {
        int32_t top = 44 + (int32_t)band * 60, h = band < 2u ? 60 : 28;
        cv_begin(240, (uint32_t)h, C_BLACK);
        cv_oy = -top;                    /* (screen rows: the canvas shows top..top + h - 1) */
        if (band == 0u) {                /* the display: TAP TEMPO, the taps, the tempo, BPM */
            uint16_t on = flash ? C_WHITE : ext ? C_GRAY : C_HI;
            int32_t x = 14;
            tp_digit(x, 50, v >= 1000u ? (int)(v / 1000u) : -1, on);
            tp_digit(x + 38, 50, v >= 100u ? (int)(v / 100u % 10u) : -1, on);
            tp_digit(x + 76, 50, (int)(v / 10u % 10u), on);
            cv_rect(x + 112, 94, 6, 6, on);                     /* the point */
            tp_digit(x + 124, 50, (int)(v % 10u), on);
            for (i = 0; i < 8; i++)      /* this run's taps, two rows of four */
                cv_rect(186 + (i & 3) * 11, 54 + (i >> 2) * 10, 8, 7, (uint32_t)i < ui.ntaps ? C_HI : C_LINE);
            cv_text(186, 84, &FONT_XS, ext ? "EXT" : "BPM", ext ? C_AMB : C_GRAY);
        }
        if (band >= 1u) {                /* the metronome */
            for (i = 106; i < 190; i++) { /* its body: a tall triangle, the apex up */
                int32_t w = (i - 106) * 52 / 84;
                cv_rect(px - w, i, 2 * w + 1, 1, RGB(30, 26, 20));
                cv_rect(px - w - 1, i, 2, 1, C_LINE);
                cv_rect(px + w, i, 2, 1, C_LINE);
            }
            for (i = 0; i < 5; i++)      /* its scale, down the middle */
                cv_rect(px - 6 + (i & 1) * 2, 126 + i * 10, 13 - (i & 1) * 4, 2, C_LINE);
            if (ui.tap_last && now - ui.tap_last < 320u) {   /* a tap: a ripple from the pivot */
                uint32_t age = now - ui.tap_last;
                tp_circle(px, py, 12 + (int32_t)age / 5, age < 160u ? C_HI : C_AMB);
            }
            for (i = 0; i <= 40; i++)    /* the arm */
                cv_rect(px + (bx - px) * i / 40 - 1, py + (by - py) * i / 40 - 1, 3, 3, C_GRAY);
            {
                uint16_t bob = fr < 0.18f ? ((beat & 3) == 0 && st->playing ? C_WHITE : C_HI) : RGB(150, 100, 30);
                int32_t r;
                for (r = -9; r <= 9; r++) {   /* the bob: a disc */
                    int32_t w = 0;
                    while ((w + 1) * (w + 1) + r * r <= 81)
                        w++;
                    cv_rect(bx - w, by + r, 2 * w + 1, 1, bob);
                }
            }
            cv_rect(px - 14, py, 29, 4, C_LINE);                /* the pivot */
            cv_rect(px - 3, py - 3, 7, 7, C_GRAY);
            {                            /* the click's level (K4): a bar, OFF at 0 */
                char cl[8];
                cl[0] = 0;
                if (set.click) {
                    int_text(set.click, cl);
                    text_cat(cl, "%");
                } else
                    text_cat(cl, "OFF");
                cv_text(8, 120, &FONT_XS, "CLICK", set.click ? C_HI : C_DIM);
                cv_text(8, 134, &FONT_XS, cl, set.click ? C_WHITE : C_DIM);
                cv_rect(48, 140, 30, 3, C_BLOCK);
                cv_rect(48, 140, (int32_t)set.click * 30 / 100, 3, C_HI);
            }
            cv_text(8, 162, &FONT_XS, ext ? "EXT CLOCK" : "TAP: ENV", C_DIM);
            cv_text(8, 176, &FONT_XS, ext ? "" : "OR ANY KEY", C_DIM);
            if (st->playing && !ext) {   /* the beat at the right: 1 (white) .. 4 */
                char t[2] = {(char)('1' + (beat & 3)), 0};
                cv_text(200, 140, &FONT_XS, "BEAT", C_GRAY);
                cv_text2(206, 156, &FONT_XS, t, (beat & 3) == 0 ? C_WHITE : C_HI);
            }
        }
        cv_oy = 0;
        cv_blit(0, (uint32_t)top);
    }
    a_sig = sig;
}

/* RESET ALL's screen: a brain seen from the side, an ice pick in through the eye socket, the frontal lobe gone grey,
 * a flatline under it, LOBOTOMIZED. Drawn whole, in four strips of 60 rows, from shapes (no picture in flash) */
#define LB_PINK RGB(255, 140, 160)
#define LB_FOLD RGB(190, 70, 100)
#define LB_EDGE RGB(120, 30, 60)
#define LB_DEAD RGB(96, 96, 104)
static int lobo_in(int32_t x, int32_t y)  /* inside the brain: the cerebrum (flat-bottomed), the cerebellum, the stem */
{
    float dx = (float)(x - 126) * (1.0f / 92.0f), dy = (float)(y - 88) * (1.0f / 60.0f);
    float cx = (float)(x - 176) * (1.0f / 36.0f), cy = (float)(y - 132) * (1.0f / 19.0f);
    if (dx * dx + dy * dy < 1.0f && dy < 0.66f)
        return 1;
    if (cx * cx + cy * cy < 1.0f)
        return 1;
    return x >= 146 && x < 162 && y >= 120 && y < 168;
}
static void draw_lobotomy(void)
{
    uint32_t band;
    for (band = 0; band < 4u; band++) {
        int32_t top = (int32_t)band * 60, y, x, i;
        cv_begin(240, 60, C_BLACK);
        cv_oy = -top;                    /* (draw in screen rows: the canvas shows top..top + 59) */
        for (y = top; y < top + 60 && y < 172; y++)
            for (x = 30; x < 222; x++) {
                float d, ex, ey;
                uint16_t c;
                if (!lobo_in(x, y))
                    continue;
                ex = (float)(x - 58);    /* the pick's tip, in the frontal lobe */
                ey = (float)(y - 96);
                if (!lobo_in(x - 1, y) || !lobo_in(x + 1, y) || !lobo_in(x, y - 1) || !lobo_in(x, y + 1))
                    c = LB_EDGE;
                else {                   /* the folds: two wavy fields crossing */
                    d = brk_sin_turns((float)x * 0.045f + 0.35f * brk_sin_turns((float)y * 0.03f + (float)x * 0.01f)) +
                        brk_sin_turns((float)y * 0.06f + 0.3f * brk_sin_turns((float)x * 0.025f));
                    c = brk_absf(d) < 0.14f ? LB_FOLD : LB_PINK;
                    if (x >= 140 && y >= 118)            /* the cerebellum: finer, level folds */
                        c = brk_sin_turns((float)y * 0.28f) > 0.6f ? LB_FOLD : LB_PINK;
                }
                if (ex * ex + ey * ey < 576.0f)          /* what the pick took: gone grey */
                    c = c == LB_PINK ? LB_DEAD : c == LB_FOLD ? RGB(52, 52, 58) : c;
                cv_rect(x, y, 1, 1, c);
            }
        for (i = 0; i <= 120; i++) {     /* the pick: a handle, then the shaft up to the tip */
            int32_t px = 4 + (58 - 4) * i / 120, py = 196 - (196 - 96) * i / 120;
            if (i < 40)
                cv_rect(px - 2, py - 2, 5, 5, C_AMB);
            else
                cv_rect(px, py, 2, 2, i > 112 ? C_WHITE : C_GRAY);
        }
        for (x = 96; x < 236; x++) {     /* the flatline, one last blip */
            int32_t b = x >= 112 && x < 124 ? (x < 116 ? -(x - 112) * 4 : x < 120 ? -16 + (x - 116) * 6 : 8 - (x - 120) * 2) : 0;
            cv_rect(x, 182 + b, 1, 2, x < 124 ? C_HI : C_GRAY);
        }
        cv_text2((240 - text2_w(&FONT_B, "LOBOTOMIZED")) / 2, 194, &FONT_B, "LOBOTOMIZED", C_HI);
        cv_text((240 - text_w(&FONT_XS, "ALL SETTINGS CLEARED")) / 2, 226, &FONT_XS, "ALL SETTINGS CLEARED", C_GRAY);
        cv_oy = 0;
        cv_blit(0, (uint32_t)top);
    }
}

/* the start screen's lights: a comet runs through the buttons as the break drops in (OCT- and OCT+, the top row left
 * to right, the bottom row back), a tail of mid-lit buttons behind it and the ones it passed glowing; then all flash
 * as the mark completes, and PLAY blinks green with PRESS PLAY. Returns 0 once the start screen is over */
/* the buttons' comet's way round: OCT- and OCT+, the top row left to right, the bottom row back (the demo's too) */
static const uint8_t PATH[NB] = {B_OCTDN, B_OCTUP, B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO,
                                 B_REC, B_PLAY, B_SEQ, B_ARP, B_SAVE, B_HOME};
static int intro_leds(uint32_t now)
{
    uint32_t t = now - ui.intro_t0, run = 16u * INTRO_STEP, b = 0u, mid = 0u, dim = 0u, i;
    if (!ui.intro)
        return 0;
    if (t < run) {                       /* the comet */
        uint32_t head = t * (NB + 2u) / run;   /* (+2: its tail runs out too) */
        for (i = 0; i < NB; i++)
            if (i == head)
                b |= 1u << PATH[i];
            else if (i + 2u >= head && i < head)
                mid |= 1u << PATH[i];
            else if (i < head)
                dim |= 1u << PATH[i];
    } else if (t < run + 160u)           /* the mark complete: all flash */
        b = ((1u << NB) - 1u) | LED_PLAY_GREEN;
    else {                               /* PRESS PLAY: PLAY blinks green, the rest glow */
        dim = ((1u << NB) - 1u) & ~(1u << B_PLAY);
        if ((t - run - 160u) % 500u < 250u)
            b = LED_PLAY_GREEN;
    }
    plat_lights(b, 0u, dim, 0u, 1);
    plat_lights_mid(mid, 0u);
    return 1;
}

static void intro_end(void)
{
    ui.intro = 0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = RG_ALL;
}

/* the battery, next to the header's level meter (plat_battery): three bars from the foot, red at one or none, a USB
 * host powering the FM-1 shows a bolt instead. 9 wide, rows 4..20 (the header's canvas); -1 (not read yet): nothing */
#define BATT_W 9                         /* the body's width (the nub 3 wide on top of it) */
#define BATT_LOW RGB(255, 104, 72)       /* one bar or none: red */
static void draw_batt(int32_t x, int b)
{
    uint16_t c = b >= 0 && b <= 1 ? BATT_LOW : C_DIM;
    int32_t i;
    if (b < 0)
        return;                          /* not read yet (the keys own the converter: APP_KEY_SENSE) */
    cv_rect(x + 3, 4, 3, 2, c);           /* the nub */
    cv_rect(x, 6, BATT_W, 14, c);         /* the body's frame */
    cv_rect(x + 1, 7, BATT_W - 2, 12, C_BLACK);
    if (b == 4) {                        /* a USB host powers the FM-1: a bolt */
        cv_line(x + 5, 7, x + 2, 12, C_HI);
        cv_line(x + 2, 12, x + 6, 12, C_HI);
        cv_line(x + 6, 12, x + 3, 18, C_HI);
        return;
    }
    for (i = 0; i < b && i < 3; i++)      /* the bars from the foot up */
        cv_rect(x + 2, 15 - i * 4, BATT_W - 4, 3, b <= 1 ? BATT_LOW : C_HI);
}

/* the header's level meter: what leaves, left and right, -48..0 dBFS in 18 steps of 2.7 dB; amber from -8 dB, the
 * top white near full scale */
static void draw_meter(void)
{
    float lv[2];
    uint32_t c;
    int32_t h;
    lv[0] = meter_l;
    lv[1] = meter_r;
    cv_begin(8, 20, C_BLACK);
    for (c = 0; c < 2u; c++) {
        for (h = 0; h < 18; h++)         /* the steps it is above: -48 dB + 2.67 dB each (2^(dB / 6.02)) */
            if (lv[c] < brk_exp2f(((float)h * (48.0f / 18.0f) - 48.0f) * (1.0f / 6.0206f)))
                break;
        cv_rect((int32_t)c * 4, 1, 3, 18, C_BLOCK);
        if (h)
            cv_rect((int32_t)c * 4, 19 - h, 3, h, h >= 18 ? C_WHITE : h >= 15 ? C_AMB : C_HI);
    }
    cv_blit(ui.meter_x, 4);
}

/* the header: the break and the tempo, a play / stop mark; or a message for a moment */
static void draw_head(const brk_status_t *st, const brk_break_t *b, uint32_t now)
{
    char t[24];
    cv_begin(240, 28, C_BLACK);
    ui.meter_x = 0;
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
        {                                /* the break, the pattern (> one queued), REC */
            int32_t x = cv_text(24, 6, &FONT_B, b ? break_name(set.brk) : "NO BREAK",   /* (amber: still to come) */
                                brk_next >= 0 ? C_AMB : C_WHITE);
            t[0] = 'P';
            int_text((int32_t)set.pattern + 1, t + 1);
            if (pat_next >= 0)
                text_cat(t, ">");
            x = cv_text(x + 6, 10, &FONT_T, t, pat_next >= 0 ? C_AMB : C_GRAY);
            if (ui.rec)
                cv_text(x + 5, 10, &FONT_T, "REC", C_AMB);
        }
        tenths_text(set.tempo_x10, t);
        cv_text(232 - text_w(&FONT_B, t), 6, &FONT_B, t, C_HI);
        ui.meter_x = (int16_t)(228 - text_w(&FONT_B, t) - text_w(&FONT_T, "BPM") - 12);   /* left of BPM */
        cv_text(228 - text_w(&FONT_B, t) - text_w(&FONT_T, ext_active() ? "EXT" : "BPM"), 10, &FONT_T,
                ext_active() ? "EXT" : "BPM", ext_active() ? C_AMB : C_GRAY);
        draw_batt(ui.meter_x - 12, plat_battery());        /* the battery, left of the level meter */
    }
    cv_rect(0, 27, 240, 1, C_LINE);
    cv_blit(0, 0);
}

/* the whole break: its waveform, the slice markers, the slice sounding lit, the playhead (44 rows, under the tab
 * bar) */
static void draw_wave(const brk_status_t *st, const brk_break_t *b)
{
    uint32_t i;
    cv_begin(240, 44, C_BLACK);
    if (b && b->len) {
        int32_t lit_a = -1, lit_z = -1;
        if (st->slice >= 0) {
            lit_a = (int32_t)(b->slice[st->slice] * 240u / b->len);
            lit_z = (int32_t)(b->slice[st->slice + 1] * 240u / b->len);
            cv_rect(lit_a, 1, lit_z - lit_a, 42, C_BLOCK);
            if (set.gate && set.gate < 100u && st->key < 0)   /* gated: only the part that sounds lit */
                lit_z = lit_a + ((lit_z - lit_a) * (int32_t)set.gate + 99) / 100;
        }
        for (i = 0; i < 240u; i++) {
            int32_t h = wave_px[i] * 5 / 6;
            cv_rect((int32_t)i, 22 - h, 1, 2 * h + 1, (int32_t)i >= lit_a && (int32_t)i < lit_z ? C_HI : C_DIM);
        }
        for (i = 1; i < b->nslices; i++)
            cv_rect((int32_t)(b->slice[i] * 240u / b->len), 0, 1, 5, C_GRAY);
        if (st->slice >= 0)
            cv_rect((int32_t)((uint32_t)st->pos * 240u / b->len), 0, 1, 44, C_WHITE);
    }
    cv_blit(0, 44);
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
            if (have && key_custom((int)k))  /* a loop of its own: a dot */
                cv_rect(x + 21, 39, 4, 4, on ? C_BLACK : C_HI);
            if (have && (set.slx[k].type || set.slx[k].level < 100u))   /* an effect of its own: its tag */
                cv_text(x + 13, 35, &FONT_XS, set.slx[k].type ? SLX_TAG[set.slx[k].type] : "LV",
                        on ? C_BLACK : C_HI);
        }
        cv_blit(0, 88u + row * 52u);
    }
}
