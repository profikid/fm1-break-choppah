/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the FX page: the master chain's slots, their settings and the chain drawn. (Part of app.c's unity:
 * included there in order.) */

/* each effect's settings, as the foot names them (K1-K3; K4 is MIX) */
static const char *const FXP_NAME[BRK_NFXT][3] = {
    {"", "", ""}, {"CUTOFF", "RESO", "MODE"}, {"BITS", "RATE", "DRIVE"}, {"GAIN", "TONE", "LEVEL"},
    {"AMOUNT", "ATTACK", "RELEASE"}, {"RATE", "DEPTH", "FEEDBK"}, {"FREQ", "SPREAD", ""}, {"RATE", "DEPTH", "PAN"},
    {"SIZE", "TONE", "PRE"}, {"TIME", "FEEDBK", "TONE"}, {"LOW", "MID", "HIGH"}, {"DRIVE", "WARMTH", "WOW"},
    {"LOW", "MID", "HIGH"}, {"DRIVE", "KNEE", "CEILING"}};

static const char *fx_param_name(uint32_t slot, int i)
{
    const brk_fxslot_t *sl = &set.fx.slot[slot & 3u];
    if (i == 3)
        return sl->type == FXT_EMPTY ? "" : sl->type == FXT_DELAY ? "SEND" : sl->type == FXT_EQ ? "FREQ" : "MIX";
    return FXP_NAME[sl->type < BRK_NFXT ? sl->type : 0][i & 3];
}

static void fx_value(uint32_t slot, int i, char *t)
{
    const brk_fxslot_t *sl = &set.fx.slot[slot & 3u];
    float v = i == 3 ? sl->mix : sl->p[i & 3];
    t[0] = 0;
    if (sl->type == FXT_EMPTY || !fx_param_name(slot, i)[0])
        return;
    if (i == 3) {
        if (sl->type == FXT_EQ)          /* the EQ's K4: the mid's frequency */
            hz_text(200.0f * brk_exp2f(v * 4.64f), t);
        else
            pct_text(v, t);
        return;
    }
    switch (sl->type * 4 + i) {
    case FXT_EQ * 4 + 0:
    case FXT_EQ * 4 + 1:
    case FXT_EQ * 4 + 2: {               /* -12 .. +12 dB, flat in the middle */
        float db = (v - 0.5f) * 24.0f;
        if (db > -0.25f && db < 0.25f)
            text_cat(t, "0 DB");
        else
            signed_tenths(db, "DB", t);
        return;
    }
    case FXT_TAPE * 4 + 0: int_text((int32_t)(v * 24.0f + 0.5f), t); text_cat(t, " DB"); return;
    case FXT_CLIP * 4 + 0: text_cat(t, "+"); int_text((int32_t)(v * 24.0f + 0.5f), t + 1); text_cat(t, " DB"); return;
    case FXT_CLIP * 4 + 1: text_cat(t, v < 0.34f ? "SOFT " : v < 0.67f ? "MID " : "HARD ");
        {
            char d[8];
            pct_text(v, d);
            text_cat(t, d);
        }
        return;
    case FXT_CLIP * 4 + 2:
        if (v < 0.005f)
            text_cat(t, "0 DB");
        else
            signed_tenths(-v * 12.0f, "DB", t);
        return;
    case FXT_MBCOMP * 4 + 0:
    case FXT_MBCOMP * 4 + 1:
    case FXT_MBCOMP * 4 + 2: {           /* its amount as the band's ratio */
        int32_t r10 = (int32_t)((1.0f + 7.0f * v) * 10.0f + 0.5f);
        if (r10 <= 10)
            text_cat(t, "OFF");
        else {
            tenths_text(r10, t);
            text_cat(t, ":1");
        }
        return;
    }
    case FXT_FILTER * 4 + 0: hz_text(30.0f * brk_exp2f(v * 9.06f), t); return;
    case FXT_FILTER * 4 + 2: text_cat(t, v < 0.33f ? "LP" : v < 0.67f ? "BP" : "HP"); return;
    case FXT_CRUNCH * 4 + 0: int_text((int32_t)(16.0f - 14.0f * v + 0.5f), t); text_cat(t, " BIT"); return;
    case FXT_CRUNCH * 4 + 1: hz_text(44100.0f * brk_exp2f(-v * 5.46f), t); return;
    case FXT_DRIVE * 4 + 0: times_text(1.0f + 29.0f * v * v, t); return;
    case FXT_PUNCH * 4 + 1: tenths_text((int32_t)((0.1f + 29.9f * v * v) * 10.0f + 0.5f), t); text_cat(t, "MS"); return;
    case FXT_PUNCH * 4 + 2: int_text((int32_t)(20.0f + 480.0f * v + 0.5f), t); text_cat(t, "MS"); return;
    case FXT_FLANGER * 4 + 0: hundredths_text(0.02f * brk_exp2f(v * 9.0f), t); text_cat(t, "HZ"); return;
    case FXT_FLANGER * 4 + 2: {
        int32_t x = (int32_t)((2.0f * v - 1.0f) * 90.0f + (v < 0.5f ? -0.5f : 0.5f));
        t[0] = x > 0 ? '+' : x < 0 ? '-' : 0;
        t[1] = 0;
        int_text(x < 0 ? -x : x, t + (x != 0));
        text_cat(t, "%");
        return;
    }
    case FXT_RING * 4 + 0: hz_text(20.0f * brk_exp2f(v * 6.64f), t); return;
    case FXT_REVERB * 4 + 1: text_cat(t, v < 0.34f ? "BRIGHT " : v < 0.67f ? "WARM " : "DARK ");
        {
            char d[8];
            pct_text(v, d);
            text_cat(t, d);
        }
        return;
    case FXT_REVERB * 4 + 2: int_text((int32_t)(v * 60.0f + 0.5f), t); text_cat(t, "MS"); return;
    case FXT_DELAY * 4 + 0: {
        uint32_t d = (uint32_t)(v * (float)(BRK_DLY_DIVS - 1u) + 0.5f);
        text_cat(t, BRK_DLY_DIV_NAME[d < BRK_DLY_DIVS ? d : 0u]);
        return;
    }
    case FXT_DELAY * 4 + 1: pct_text(v * 0.95f, t); return;
    case FXT_TREMOLO * 4 + 0: {
        uint32_t d = (uint32_t)(v * (float)(BRK_TREM_DIVS - 1u) + 0.5f);
        text_cat(t, BRK_TREM_DIV_NAME[d < BRK_TREM_DIVS ? d : 0u]);
        return;
    }
    default:
        pct_text(v, t);
        return;
    }
}

static void fx_knob_turn(uint32_t slot, int i, int32_t d)
{
    brk_fxslot_t *sl = &set.fx.slot[slot & 3u];
    char t[16];
    float step = 0.02f;
    if (sl->type == FXT_EMPTY || i < 0 || i > 3 || !fx_param_name(slot, i)[0])
        return;
    if (i == 3)
        sl->mix = knob_f(sl->mix, d, 0.02f, 0.0f, 1.0f);
    else {
        if (sl->type == FXT_FILTER && i == 2)
            step = 0.5f;                 /* LP, BP, HP */
        else if (sl->type == FXT_TREMOLO && i == 0)
            step = 1.0f / (float)(BRK_TREM_DIVS - 1u);
        else if (sl->type == FXT_DELAY && i == 0)
            step = 1.0f / (float)(BRK_DLY_DIVS - 1u);
        sl->p[i] = knob_f(sl->p[i], d, step, 0.0f, 1.0f);
    }
    fx_value(slot, i, t);
    app_say(fx_param_name(slot, i), t);
    ui.redraw |= RG_BLOCKS;
    changed();
}

/* PRESETS on the FX page: the focused slot's effect, through the library (EMPTY first), with its own starting
 * settings, on at once */
static void fx_type_turn(int32_t d)
{
    brk_fxslot_t *sl = &set.fx.slot[ui.fx_focus & 3u];
    int32_t t = clamp((int32_t)sl->type + (d > 0 ? 1 : -1), 0, BRK_NFXT - 1);
    char n[16];
    if ((uint32_t)t == sl->type)
        return;
    brk_fx_slot_defaults(sl, (int)t);
    sl->on = (uint8_t)(t != FXT_EMPTY);
    n[0] = 0;
    text_cat(n, "SLOT ");
    int_text((int32_t)(ui.fx_focus & 3u) + 1, n + 5);
    app_say(n, BRK_FXT_NAME[t]);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
    changed();
}

/* ALGORITHM on the FX page: the next or previous slot; with SEL held, the focused slot moves along the chain */
static void fx_algo(int32_t d, uint32_t sel)
{
    uint32_t a = ui.fx_focus & 3u, b = (a + (d > 0 ? 1u : 3u)) & 3u;
    char n[8];
    if (sel) {
        brk_fxslot_t tmp = set.fx.slot[a];
        if ((d > 0 && a == 3u) || (d < 0 && a == 0u))
            return;                      /* (the ends stay the ends) */
        set.fx.slot[a] = set.fx.slot[b];
        set.fx.slot[b] = tmp;
        changed();
    }
    ui.fx_focus = (uint8_t)b;
    int_text((int32_t)b + 1, n);
    app_say(sel ? "MOVED TO" : "SLOT", n);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static int delay_slot(void)              /* the chain's DELAY in use (the first one on), -1 none */
{
    uint32_t k;
    for (k = 0; k < BRK_FX_SLOTS; k++)
        if (set.fx.slot[k].type == FXT_DELAY && set.fx.slot[k].on)
            return (int)k;
    return -1;
}

/* the FX page: the chain as four tiles in order, IN to OUT (the focused one outlined), then the focused slot */
static void draw_chain(void)
{
    const brk_fx_t *f = &set.fx;
    uint32_t k, i;
    char t[24], v[16];
    cv_begin(240, 52, C_BLACK);
    for (k = 0; k < BRK_FX_SLOTS; k++) {
        const brk_fxslot_t *sl = &f->slot[k];
        int32_t x = 2 + (int32_t)k * 60, foc = k == (ui.fx_focus & 3u), on = sl->on && sl->type != FXT_EMPTY;
        cv_rect(x, 4, 54, 44, foc ? C_WHITE : C_BLOCK);
        cv_rect(x + 2, 6, 50, 40, on ? C_BLOCK_HI : C_BLOCK);
        int_text((int32_t)k + 1, t);
        cv_text(x + 5, 8, &FONT_T, t, on ? C_BLACK : C_AMB);
        cv_text(x + 5, 22, &FONT_XS, sl->type == FXT_EMPTY ? "-" : BRK_FXT_NAME[sl->type],
                on ? C_BLACK : sl->type == FXT_EMPTY ? C_LINE : C_GRAY);
        if (sl->type != FXT_EMPTY) {     /* its mix, a bar along the bottom */
            cv_rect(x + 5, 38, 44, 3, on ? C_AMB : C_LINE);
            cv_rect(x + 5, 38, (int32_t)(44.0f * sl->mix), 3, on ? C_BLACK : C_GRAY);
        }
        if (k + 1u < BRK_FX_SLOTS)       /* the signal on to the next */
            for (i = 0; i < 3u; i++)
                cv_rect(x + 55 + (int32_t)i, 22 + (int32_t)i, 1, 5 - 2 * (int32_t)i, C_GRAY);   /* > */
    }
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);          /* the focused slot: its effect, its settings, what the controls do */
    {
        const brk_fxslot_t *sl = &f->slot[ui.fx_focus & 3u];
        const char *state = sl->type == FXT_EMPTY ? "" : sl->on ? "ON" : "BYPASS";
        int32_t x;
        t[0] = 0;
        text_cat(t, "SLOT ");
        int_text((int32_t)(ui.fx_focus & 3u) + 1, v);
        text_cat(t, v);
        x = cv_text(6, 0, &FONT_T, t, C_GRAY);
        cv_text(x + 8, 0, &FONT_T, BRK_FXT_NAME[sl->type < BRK_NFXT ? sl->type : 0],
                sl->on && sl->type != FXT_EMPTY ? C_HI : C_DIM);
        cv_text(234 - text_w(&FONT_XS, state), 2, &FONT_XS, state, sl->on ? C_HI : C_DIM);
        for (i = 0; i < 4u && sl->type != FXT_EMPTY; i++) {   /* the settings as bars, over K1-K4 */
            float val = i == 3u ? sl->mix : sl->p[i];
            if (!fx_param_name(ui.fx_focus, (int)i)[0])
                continue;
            cv_rect(6 + (int32_t)i * 58, 16, 52, 4, C_BLOCK);
            cv_rect(6 + (int32_t)i * 58, 16, (int32_t)(52.0f * val), 4, sl->on ? C_HI : C_GRAY);
        }
        if (sl->type == FXT_MBCOMP && sl->on) {   /* each band's reduction now, under its knob (0..-24 dB) */
            float gr[3];
            brk_fx_gr(gr);
            for (i = 0; i < 3u; i++) {
                float db = gr[i] < 0.999f ? -6.0206f * brk_log2f(gr[i] > 0.01f ? gr[i] : 0.01f) : 0.0f;
                cv_rect(6 + (int32_t)i * 58, 21, (int32_t)(52.0f * brk_clampf(db / 24.0f, 0.0f, 1.0f)), 2, C_AMB);
            }
        }
        cv_text(6, 26, &FONT_XS, "SEL+FX: BYPASS", C_DIM);
        cv_text(6, 38, &FONT_XS, "FX+ALGO: MOVE", C_DIM);
        {                                /* the whole chain's mix (MIXER K4), to see, not to set */
            char w[16];
            pct_text(f->wet, v);
            w[0] = 0;
            text_cat(w, "FX ");
            text_cat(w, v);
            cv_text(234 - text_w(&FONT_T, w), 25, &FONT_T, w, f->wet > 0.005f ? C_HI : C_DIM);
            cv_rect(194, 41, 40, 3, C_BLOCK);
            cv_rect(194, 41, (int32_t)(40.0f * f->wet + 0.5f), 3, f->wet > 0.005f ? C_HI : C_DIM);
        }
    }
    cv_blit(0, 140);
}
