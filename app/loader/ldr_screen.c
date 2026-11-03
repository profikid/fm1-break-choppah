/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (felucca-bsp loader/ldr_screen.c); CHOPPAH's look, 2026 */
/* CHOPPAH's update screen: the update loader's own (felucca-bsp loader/ldr_screen.c, which build.sh swaps this in for
 * while the firmware builds), in CHOPPAH's colours: black, the slice mark (sixteen bars, two chopped out of line)
 * filling in amber as the app area is written, "UPDATING" in cream, "DO NOT UNPLUG" in amber, a bar of amber
 * segments, CHOPPAH small under it. Only the drawing differs from the original: the panel's start (reset, sleep
 * out, RGB565, IPS inversion), ldr_screen_init and ldr_screen_progress are the same, and every buffer is in RAM (the
 * SPI DMA reads RAM only). Part of loader.c's single unit. */
#include "../boards/fm1/fm1_lcd_hw.h"

#define SCR_BG 0x0000u                              /* black */
#define SCR_FG 0xEF3Au                              /* #ede6d4 cream */
#define SCR_AMB 0xFD83u                             /* #ffb21a amber */
#define SCR_HOT 0xFFFFu                             /* white: the mark's two chopped bars */
#define SCR_DIM 0x3962u                             /* #3c2c10: the mark, not yet written */
#define SCR_EDGE 0x2944u                            /* #2f2a20: the bar's frame */
#define SCR_NAME 0x6B2Au                            /* #6e6454 */
#define SCR_BAR_X 30u
#define SCR_BAR_Y 170u
#define SCR_BAR_W 180u
#define SCR_BAR_H 12u
#define SCR_SEG 12u                                 /* a segment of the bar: 10 lit, 2 gap */
#define SCR_ROWS 21u                                /* the tallest line: 5x7 glyphs at 3x */

static uint16_t scr_strip[240u * SCR_ROWS];         /* one line of text, byte-swapped (the panel takes MSB first) */
static uint8_t scr_small[8];
static uint32_t scr_shown;                          /* bar pixels drawn */
static uint32_t scr_marks;                          /* the mark's bars lit */
static uint8_t scr_on;

/* 5x7 glyphs, a row a byte (bit 4 = left) */
static const struct { char c; uint8_t rows[7]; } SCR_FONT[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
};

/* the mark: sixteen bars like a break's hits (as the app's start screen draws it) */
static const uint8_t SCR_MARK_H[16] = {30, 12, 26, 10, 34, 14, 11, 20, 12, 22, 28, 24, 34, 10, 13, 22};

static void scr_cmd(uint8_t c)
{
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void scr_data(const void *p, uint32_t n)     /* n > 0 */
{
    fm1_lcd_send_data(p, n);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void scr_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    scr_small[0] = (uint8_t)(x0 >> 8); scr_small[1] = (uint8_t)x0;
    scr_small[2] = (uint8_t)(x1 >> 8); scr_small[3] = (uint8_t)x1;
    scr_cmd(0x2A);
    scr_data(scr_small, 4);
    scr_small[4] = (uint8_t)(y0 >> 8); scr_small[5] = (uint8_t)y0;
    scr_small[6] = (uint8_t)(y1 >> 8); scr_small[7] = (uint8_t)y1;
    scr_cmd(0x2B);
    scr_data(scr_small + 4, 4);
    scr_cmd(0x2C);
}

static uint16_t scr_swap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static void scr_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, n = w * h, k = 240u * SCR_ROWS;
    if (!n)
        return;
    for (i = 0; i < k && i < n; i++)
        scr_strip[i] = scr_swap(c);
    scr_window(x, y, x + w - 1u, y + h - 1u);
    for (; n; n -= k) {
        if (k > n)
            k = n;
        scr_data(scr_strip, k * 2u);
    }
}

static void scr_text(uint32_t y, const char *s, uint32_t scale, uint16_t fg)   /* one line, centred; scale <= 3 */
{
    uint32_t len = 0, i, r, b, sx, sy, x0, w;
    while (s[len])
        len++;
    w = len * 6u * scale - scale;
    x0 = (240u - w) / 2u;
    for (i = 0; i < w * 7u * scale; i++)
        scr_strip[i] = scr_swap(SCR_BG);
    for (i = 0; i < len; i++) {
        const uint8_t *g = 0;
        uint32_t f;
        for (f = 0; f < sizeof SCR_FONT / sizeof SCR_FONT[0]; f++)
            if (SCR_FONT[f].c == s[i])
                g = SCR_FONT[f].rows;
        if (!g)
            continue;                               /* a space */
        for (r = 0; r < 7u; r++)
            for (b = 0; b < 5u; b++)
                if (g[r] & (0x10u >> b))
                    for (sy = 0; sy < scale; sy++)
                        for (sx = 0; sx < scale; sx++)
                            scr_strip[(r * scale + sy) * w + (i * 6u + b) * scale + sx] = scr_swap(fg);
    }
    scr_window(x0, y, x0 + w - 1u, y + 7u * scale - 1u);
    scr_data(scr_strip, w * 7u * scale * 2u);
}

static void scr_mark(uint32_t k, int lit)          /* bar k of the mark, written (lit) or still to come */
{
    uint32_t h = SCR_MARK_H[k], chop = k == 5u || k == 11u, y = (chop ? 58u : 68u) - h / 2u;
    scr_fill(8u + k * 14u, y, 12u, h, lit ? (chop ? SCR_HOT : SCR_AMB) : SCR_DIM);
}

static void ldr_screen_init(void)
{
    uint32_t k;
    fm1_lcd_hw_init();                              /* pins, backlight on, SPI1 */
    fm1_delay_ms(20);
    scr_cmd(0x01);                                  /* SWRESET */
    fm1_delay_ms(150);
    scr_cmd(0x11);                                  /* SLPOUT */
    fm1_delay_ms(120);
    scr_cmd(0x3A);                                  /* COLMOD: RGB565 */
    scr_small[0] = 0x55;
    scr_data(scr_small, 1);
    scr_cmd(0x36);                                  /* MADCTL: top-left origin, RGB order */
    scr_small[0] = 0x00;
    scr_data(scr_small, 1);
    scr_cmd(0x21);                                  /* INVON: IPS panel */
    scr_cmd(0x13);                                  /* NORON */
    scr_fill(0, 0, 240, 240, SCR_BG);
    for (k = 0; k < 16u; k++)
        scr_mark(k, 0);
    scr_text(100, "UPDATING", 3, SCR_FG);
    scr_text(134, "DO NOT UNPLUG", 2, SCR_AMB);
    scr_fill(SCR_BAR_X - 2u, SCR_BAR_Y - 2u, SCR_BAR_W + 4u, 1u, SCR_EDGE);             /* the bar's frame */
    scr_fill(SCR_BAR_X - 2u, SCR_BAR_Y + SCR_BAR_H + 1u, SCR_BAR_W + 4u, 1u, SCR_EDGE);
    scr_fill(SCR_BAR_X - 2u, SCR_BAR_Y - 1u, 1u, SCR_BAR_H + 2u, SCR_EDGE);
    scr_fill(SCR_BAR_X + SCR_BAR_W + 1u, SCR_BAR_Y - 1u, 1u, SCR_BAR_H + 2u, SCR_EDGE);
    scr_text(202, "CHOPPAH", 1, SCR_NAME);
    scr_cmd(0x29);                                  /* DISPON */
    scr_on = 1;
}

static void ldr_screen_progress(uint32_t done, uint32_t total)
{
    uint32_t px, marks;
    if (!scr_on || !total)
        return;
    px = done >= total ? SCR_BAR_W : done * SCR_BAR_W / total;     /* < 2^32: total <= 2 MiB */
    marks = done >= total ? 16u : done * 16u / total;
    while (scr_marks < marks)                       /* the mark fills in, a bar a sixteenth */
        scr_mark(scr_marks++, 1);
    if (px <= scr_shown)
        return;
    while (scr_shown < px) {                        /* the bar: its segments' lit part, the gaps left dark */
        uint32_t in = scr_shown % SCR_SEG, end = scr_shown - in + SCR_SEG - 2u, to = end < px ? end : px;
        if (in < SCR_SEG - 2u && to > scr_shown)
            scr_fill(SCR_BAR_X + scr_shown, SCR_BAR_Y, to - scr_shown, SCR_BAR_H, SCR_AMB);
        scr_shown = to > scr_shown ? to : scr_shown - in + SCR_SEG;
        if (scr_shown > px)
            scr_shown = px;
    }
}
