/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the MIXER (LFO): the break, the kick, the snare and hat, FX; swing and the drums' route. (Part of
 * app.c's unity: included there in order.) */

/* the MIXER's knob i on half h, as the foot and the strip show it */
static void mix_value(uint32_t h, int i, char *t)
{
    t[0] = 0;
    if (h) {
        if (i == 0) {
            int_text(set.swing, t);
            text_cat(t, "%");
        } else if (i == 1)
            text_cat(t, set.drums_chain ? "THRU FX" : "AROUND FX");
        return;
    }
    if (i == 0) {
        int_text(set.brk_level, t);
        text_cat(t, "%");
    } else
        pct_text(i == 1 ? set.kick.volume : i == 2 ? set.perc.volume : set.fx.wet, t);
}

/* the MIXER's strip: four channels side by side (a fader, its value; BREAK and KICK with their level meters, FX with
 * the chain's slots; their values are in the foot), drawn as one 240 x 104 picture in two 52-row strips. GROOVE:
 * swing, the drums' route */
#define MX_Y 18                          /* the faders and meters: from row 18, 80 rows (the values are in the foot) */
#define MX_H 80
static int32_t meter_px(float lv, int32_t h)   /* a level's height on an h-pixel meter: -48..0 dBFS */
{
    float db = lv > 0.0039f ? 6.0206f * brk_log2f(lv) : -48.0f;
    return (int32_t)(brk_clampf((db + 48.0f) * (1.0f / 48.0f), 0.0f, 1.0f) * (float)h + 0.5f);
}
static void mix_meter(int32_t x, float lv)
{
    int32_t h = meter_px(lv, MX_H);
    cv_rect(x, MX_Y, 5, MX_H, C_BLOCK);
    if (h)
        cv_rect(x, MX_Y + MX_H - h, 5, h, h >= MX_H - 2 ? C_WHITE : h >= MX_H * 5 / 6 ? C_AMB : C_HI);
}
static void mix_fader(int32_t x, float v, int on)   /* 0..1 from the bottom */
{
    int32_t h = (int32_t)(brk_clampf(v, 0.0f, 1.0f) * (float)MX_H + 0.5f);
    cv_rect(x, MX_Y, 14, MX_H, C_BLOCK);
    if (h)
        cv_rect(x + 2, MX_Y + MX_H - h, 10, h, on ? C_HI : C_DIM);
    cv_rect(x - 1, MX_Y + MX_H - h - 1, 16, 2, C_WHITE);   /* the cap */
}
static void mix_picture(int32_t top)
{
    static const char *const NAME[2][4] = {{"BREAK", "KICK", "PERC", "FX"}, {"SWING", "DRUMS", "", ""}};
    uint32_t c;
    cv_oy = -top;                        /* (this strip's rows of the picture) */
    for (c = 0; c < 4u; c++) {
        int32_t x = (int32_t)c * 60;
        if (c)
            cv_rect(x, 4, 1, 96, C_LINE);
        if (NAME[ui.mpage][c][0])
            cv_text(x + 6, 1, &FONT_XS, NAME[ui.mpage][c], C_GRAY);
    }
    if (ui.mpage) {
        mix_fader(10, (float)(set.swing - 50u) * (1.0f / 25.0f), set.swing > 50u);
        {                                /* the drums' route: two boxes, the one taken lit */
            int thru = set.drums_chain;
            cv_rect(66, 26, 48, 26, thru ? C_BLOCK_HI : C_BLOCK);
            cv_text(70, 31, &FONT_XS, "THRU FX", thru ? C_BLACK : C_GRAY);
            cv_rect(66, 62, 48, 26, thru ? C_BLOCK : C_BLOCK_HI);
            cv_text(70, 67, &FONT_XS, "AROUND", thru ? C_GRAY : C_BLACK);
        }
        cv_text(126, 40, &FONT_XS, "LFO: LEVELS", C_DIM);
        return;
    }
    mix_fader(10, (float)set.brk_level * 0.01f, set.brk_level > 0u);
    mix_meter(30, meter_brk);
    mix_fader(70, set.kick.volume, set.kick.volume > 0.0f);
    mix_meter(90, meter_kick);
    mix_fader(130, set.perc.volume, set.perc.volume > 0.0f);
    mix_meter(150, meter_perc);
    mix_fader(190, set.fx.wet, set.fx.wet > 0.005f);
    for (c = 0; c < BRK_FX_SLOTS; c++) { /* the chain's four slots, lit while on */
        const brk_fxslot_t *sl = &set.fx.slot[c];
        int on = sl->on && sl->type != FXT_EMPTY;
        cv_rect(210, MX_Y + (int32_t)c * 21, 6, 17, on ? C_BLOCK_HI : sl->type == FXT_EMPTY ? C_BLACK : C_BLOCK);
        if (sl->type == FXT_EMPTY)
            cv_rect(210, MX_Y + (int32_t)c * 21 + 8, 6, 1, C_LINE);
    }
}
static void draw_mixer(void)
{
    cv_begin(240, 52, C_BLACK);
    mix_picture(0);
    cv_oy = 0;
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    mix_picture(52);
    cv_oy = 0;
    cv_blit(0, 140);
}
