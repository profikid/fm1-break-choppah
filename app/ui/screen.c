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

/* TAP TEMPO (ENV): the whole screen between the header and the pads. The tempo in big digits (lit as a tap lands),
 * the bar's four beats (the one playing lit, the first brighter), the taps of this run, what to press */
static void draw_tap(const brk_status_t *st, uint32_t now)
{
    uint32_t band, i;
    char t[16];
    int flash = ui.tap_last && now - ui.tap_last < 120u, ext = ext_active();
    int32_t beat = st->playing && st->bar16 >= 0 ? st->bar16 / 4 : -1;
    tenths_text(set.tempo_x10, t);
    for (band = 0; band < 3u; band++) {
        int32_t top = 30 + (int32_t)band * 60, h = band < 2u ? 60 : 42, w = 3 * text_w(&FONT_B, t);
        cv_begin(240, (uint32_t)h, C_BLACK);
        cv_oy = -top;                    /* (screen rows: the canvas shows top..top + h - 1) */
        cv_text(8, 34, &FONT_XS, "TAP TEMPO", C_GRAY);
        cv_text(232 - text_w(&FONT_XS, ext ? "EXT CLOCK" : "BPM"), 34, &FONT_XS, ext ? "EXT CLOCK" : "BPM",
                ext ? C_AMB : C_GRAY);
        if (flash)
            cv_rect(0, 52, 240, 54, C_HI);
        cv_textn((240 - w) / 2, 56, &FONT_B, t, flash ? C_BLACK : ext ? C_GRAY : C_WHITE, 3u);
        for (i = 0; i < 4u; i++) {       /* the beats */
            int32_t x = 8 + (int32_t)i * 57, on = (int32_t)i == beat;
            cv_rect(x, 114, 53, 34, on ? (i ? C_HI : C_WHITE) : C_BLOCK);
            t[0] = (char)('1' + i);
            t[1] = 0;
            cv_text(x + 4, 116, &FONT_XS, t, on ? C_BLACK : C_GRAY);
        }
        for (i = 0; i < 8u; i++)         /* this run's taps (up to eight count) */
            cv_rect(8 + (int32_t)i * 12, 158, 8, 8, i < ui.ntaps ? C_HI : C_LINE);
        cv_text(112, 155, &FONT_XS, ui.ntaps ? "TAPS" : "NO TAPS YET", C_DIM);
        cv_text(8, 174, &FONT_XS, "ENV OR ANY WHITE KEY: TAP", C_GRAY);
        cv_oy = 0;
        cv_blit(0, (uint32_t)top);
    }
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

static void intro_end(void)
{
    ui.intro = 0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = RG_ALL;
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
    }
    cv_rect(0, 27, 240, 1, C_LINE);
    cv_blit(0, 0);
}

/* the whole break: its waveform, the slice markers, the slice sounding lit, the playhead (32 rows: the tab bar and
 * the page's help line go under it) */
static void draw_wave(const brk_status_t *st, const brk_break_t *b)
{
    uint32_t i;
    cv_begin(240, 32, C_BLACK);
    if (b && b->len) {
        int32_t lit_a = -1, lit_z = -1;
        if (st->slice >= 0) {
            lit_a = (int32_t)(b->slice[st->slice] * 240u / b->len);
            lit_z = (int32_t)(b->slice[st->slice + 1] * 240u / b->len);
            cv_rect(lit_a, 1, lit_z - lit_a, 30, C_BLOCK);
            if (set.gate && set.gate < 100u && st->key < 0)   /* gated: only the part that sounds lit */
                lit_z = lit_a + ((lit_z - lit_a) * (int32_t)set.gate + 99) / 100;
        }
        for (i = 0; i < 240u; i++) {
            int32_t h = wave_px[i] * 5 / 8;
            cv_rect((int32_t)i, 16 - h, 1, 2 * h + 1, (int32_t)i >= lit_a && (int32_t)i < lit_z ? C_HI : C_DIM);
        }
        for (i = 1; i < b->nslices; i++)
            cv_rect((int32_t)(b->slice[i] * 240u / b->len), 0, 1, 5, C_GRAY);
        if (st->slice >= 0)
            cv_rect((int32_t)((uint32_t)st->pos * 240u / b->len), 0, 1, 32, C_WHITE);
    }
    cv_blit(0, 30);
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
