/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the FILL page (ARP, PRD B-51). White keys 1-8 throw a fill to end on the next downbeat, 9-16 the same
 * fill at once; ALGORITHM picks the fill the auto fill plays (and the preview draws); K1 its length, K2 the pitch
 * it ramps to, K3 its density, K4 the auto fill (off, every 2 .. 16 bars). (Part of app.c's unity: included there
 * in order.) */

static const char *const FILL_LEN_NAME[4] = {"1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS"};
static const char *const FILL_AUTO_NAME[5] = {"OFF", "2 BARS", "4 BARS", "8 BARS", "16 BARS"};

static void fill_throw(uint32_t type, int now)   /* a white key on FILL */
{
    brk_fill(type, 4u << (set.fill_len & 3u), set.fill_pitch, set.fill_dens ? set.fill_dens : 1u, !now);
    app_say(BRK_FILL_NAME[type % BRK_FILLS], now ? "NOW" : "TO THE BAR");
    ui.redraw |= RG_BLOCKS;
}
static void fill_value(int i, char *t)
{
    t[0] = 0;
    if (i == 0)
        text_cat(t, FILL_LEN_NAME[set.fill_len & 3u]);
    else if (i == 1) {
        if (set.fill_pitch > 0)
            text_cat(t, "+");
        int_text(set.fill_pitch, t + (set.fill_pitch > 0));
        text_cat(t, " ST");
    } else if (i == 2) {
        text_cat(t, "X");
        int_text(set.fill_dens ? set.fill_dens : 1, t + 1);
    } else
        text_cat(t, FILL_AUTO_NAME[set.fill_auto % 5u]);
}
static void fill_turn(int i, int32_t d)
{
    static const char *const N[4] = {"LENGTH", "PITCH", "DENSITY", "AUTO FILL"};
    char t[16];
    if (i == 0)
        set.fill_len = (uint8_t)clamp((int32_t)set.fill_len + (d > 0 ? 1 : -1), 0, 3);
    else if (i == 1)
        set.fill_pitch = (int8_t)clamp((int32_t)set.fill_pitch + d, -12, 12);
    else if (i == 2)
        set.fill_dens = (uint8_t)clamp((int32_t)(set.fill_dens ? set.fill_dens : 1u) + (d > 0 ? 1 : -1), 1, 4);
    else
        set.fill_auto = (uint8_t)clamp((int32_t)set.fill_auto + (d > 0 ? 1 : -1), 0, 4);
    changed();
    fill_value(i, t);
    app_say(N[i], t);
    ui.redraw |= RG_BLOCKS;
}
static uint32_t fill_hits(uint32_t type, uint32_t i, uint32_t len)   /* the preview: hits on step i (as the engine) */
{
    uint32_t d = set.fill_dens ? set.fill_dens : 1u, h;
    if (type == FILL_ROLL || type == FILL_SNARE || type == FILL_KICK) {
        h = d + d * 2u * i / len;
        return h > 8u ? 8u : h;
    }
    if (type == FILL_RANDOM)
        return ((i * 7u + 3u) % 4u) < d - 1u ? 2u : 1u;
    return type == FILL_DROP ? 0u : 1u;
}

/* the FILL page's strip: the eight fills (the one picked outlined, the one playing lit, one waiting outlined amber,
 * each with its keys), then the picked one drawn step by step (its hits, its pitch rising or falling, the step
 * playing white) */
static void draw_fill(const brk_status_t *st)
{
    uint32_t i, len = 4u << (set.fill_len & 3u), type = set.fill_type % BRK_FILLS;
    int32_t playing = st->fill ? st->fill - 1 : -1, armed = st->fill_armed ? st->fill_armed - 1 : -1;
    char t[24];
    cv_begin(240, 52, C_BLACK);
    for (i = 0; i < BRK_FILLS; i++) {
        int32_t x = 1 + (int32_t)(i % 4u) * 60, y = (int32_t)(i / 4u) * 26;
        int on = (int32_t)i == playing, sel = i == type;
        cv_rect(x, y, 57, 24, on ? C_HI : sel ? C_WHITE : (int32_t)i == armed ? C_AMB : C_LINE);
        cv_rect(x + 1, y + 1, 55, 22, on ? C_HI : C_BLOCK);
        t[0] = 0;                        /* its keys, small, over its name */
        int_text((int32_t)i + 1, t);
        text_cat(t, " / ");
        int_text((int32_t)i + 9, t + 4);
        cv_text(x + 4, y + 1, &FONT_T, t, on ? C_BLACK : C_DIM);
        cv_text(x + 4, y + 9, &FONT_XS, BRK_FILL_NAME[i], on ? C_BLACK : sel ? C_HI : C_GRAY);
    }
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    t[0] = 0;
    text_cat(t, BRK_FILL_NAME[type]);
    text_cat(t, "  ");
    text_cat(t, FILL_LEN_NAME[set.fill_len & 3u]);
    cv_text(4, 0, &FONT_XS, t, C_GRAY);
    if (set.fill_auto) {
        t[0] = 0;
        text_cat(t, "AUTO ");
        text_cat(t, FILL_AUTO_NAME[set.fill_auto % 5u]);
        cv_text(236 - text_w(&FONT_XS, t), 0, &FONT_XS, t, C_AMB);
    }
    for (i = 0; i < len; i++) {          /* its steps */
        int32_t w = 232 / (int32_t)len, x = 4 + (int32_t)i * w, lift, k, h = (int32_t)fill_hits(type, i, len);
        int now = playing == (int32_t)type && st->fill_i == i;
        uint16_t c = now ? C_WHITE : C_HI;
        lift = set.fill_pitch * (int32_t)(i + 1u) / (int32_t)len;   /* the pitch: higher, lower */
        if (type == FILL_RANDOM)
            lift = (int32_t)(((i * 5u + 1u) % 7u)) * set.fill_pitch / 6 * ((i & 1u) ? 1 : -1);
        if (type == FILL_STOP)           /* the tape stop: slower and slower */
            h = 0, cv_rect(x, 50 - (int32_t)(len - i) * 30 / (int32_t)len, w > 1 ? w - 1 : 1,
                           (int32_t)(len - i) * 30 / (int32_t)len, c);
        else if (type == FILL_HALF)
            h = 0, cv_rect(x, 36, w > 1 ? w - 1 : 1, 14, (i & 1u) ? C_BLOCK_HI : c);
        else if (type == FILL_DROP)
            cv_rect(x, 49, w > 1 ? w - 1 : 1, 1, now ? C_WHITE : C_LINE);
        for (k = 0; k < h; k++)          /* its hits, stacked */
            cv_rect(x, 46 - k * 4 - lift, w > 1 ? w - 1 : 1, 3, type == FILL_REVERSE && k == 0 ? C_AMB : c);
    }
    cv_blit(0, 140);
}
