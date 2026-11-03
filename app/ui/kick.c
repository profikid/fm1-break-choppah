/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the KICK page (EDIT): the kick's layers, effects, macros and output. (Part of app.c's unity: included
 * there in order.) */

/* ---- the KICK page (PRD K-*): EDIT opens it, EDIT again the next sub-page. The white keys are the kick's 16
 * steps, ALGORITHM the layer, PRESETS the built-in kicks, SEL + EDIT through the FX chain or around it */
enum { KP_MIX, KP_LAYER, KP_FX, KP_MACRO, KP_OUT, KPAGES };
static const char *const KPAGE_NAME[KPAGES] = {"MIX", "LAYER", "LAYER FX", "MACRO", "OUT"};
static const char *const KN_KICK[KPAGES][4] = {{"SUB", "PUNCH", "GRIT", "CLICK"},
                                               {"LEVEL", "PITCH", "DECAY", "TONE"},
                                               {"EFFECT", "", "", "WET"},
                                               {"SUB", "PUNCH", "GRIT", "INTENSITY"},
                                               {"VOLUME", "ROUTE", "", ""}};
static const char *const KFX_PARAM[KICK_FX][2] = {{"", ""}, {"GAIN", "TONE"}, {"BITS", "RATE"},
                                                  {"CUTOFF", "RESO"}, {"FREQ", ""}};
/* the macros: each moves a few of the sound's settings together */
#define KM_N 3u
static float *kick_macro(int m, uint32_t j)
{
    kick_t *k = &set.kick;
    switch (m) {
    case 0:                              /* SUB: its level, its tail, its weight */
        return j == 0u ? &k->l[KL_SUB].level : j == 1u ? &k->l[KL_SUB].decay : &k->l[KL_SUB].tone;
    case 1:                              /* PUNCH: its level, its sweep, the click */
        return j == 0u ? &k->l[KL_PUNCH].level : j == 1u ? &k->l[KL_PUNCH].tone : &k->l[KL_CLICK].level;
    default:                             /* GRIT: its level, its dirt, its tail */
        return j == 0u ? &k->l[KL_GRIT].level : j == 1u ? &k->l[KL_GRIT].tone : &k->l[KL_GRIT].decay;
    }
}
static float kick_macro_value(int m)
{
    uint32_t j;
    float v = 0.0f;
    for (j = 0; j < KM_N; j++)
        v += *kick_macro(m, j);
    return v * (1.0f / (float)KM_N);
}
static const char *kick_knob_name(int i)
{
    if (ui.kpage == KP_FX && (i == 1 || i == 2))
        return KFX_PARAM[set.kick.l[ui.klayer % KICK_LAYERS].fx % KICK_FX][i - 1];
    return KN_KICK[ui.kpage % KPAGES][i];
}
static void kick_value(int i, char *t)
{
    const kick_t *k = &set.kick;
    const kick_layer_t *l = &k->l[ui.klayer % KICK_LAYERS];
    t[0] = 0;
    switch (ui.kpage) {
    case KP_MIX:
        pct_text(k->l[i].level, t);
        break;
    case KP_LAYER:
        if (i == 1)
            hz_text(kick_pitch_hz(ui.klayer, l->pitch), t);
        else if (i == 2) {
            int_text((int32_t)(kick_decay_ms(ui.klayer, l->decay) + 0.5f), t);
            text_cat(t, " MS");
        } else
            pct_text(i == 0 ? l->level : l->tone, t);
        break;
    case KP_FX:
        if (i == 0)
            text_cat(t, KICK_FX_NAME[l->fx % KICK_FX]);
        else if (i == 3)
            pct_text(l->wet, t);
        else if (!kick_knob_name(i)[0])
            ;
        else if (l->fx == KFX_CRUNCH && i == 1) {
            int_text((int32_t)kick_fx_bits(l->fa), t);
            text_cat(t, " BIT");
        } else if ((l->fx == KFX_FILTER || l->fx == KFX_RING) && i == 1)
            hz_text(kick_fx_hz(l->fx, l->fa), t);
        else
            pct_text(i == 1 ? l->fa : l->fb, t);
        break;
    case KP_MACRO:
        pct_text(i == 3 ? k->intensity : kick_macro_value(i), t);
        break;
    default:
        if (i == 0)
            pct_text(k->volume, t);
        else if (i == 1)
            text_cat(t, set.drums_chain ? "THRU FX" : "AROUND FX");
        break;
    }
}
static void kick_knob_turn(int i, int32_t d)
{
    kick_t *k = &set.kick;
    kick_layer_t *l = &k->l[ui.klayer % KICK_LAYERS];
    char t[16];
    switch (ui.kpage) {
    case KP_MIX:
        k->l[i].level = knob_f(k->l[i].level, d, 0.02f, 0.0f, 1.0f);
        break;
    case KP_LAYER: {
        float *p = i == 0 ? &l->level : i == 1 ? &l->pitch : i == 2 ? &l->decay : &l->tone;
        *p = knob_f(*p, d, i == 1 ? 0.01f : 0.02f, 0.0f, 1.0f);
        break;
    }
    case KP_FX:
        if (i == 0)
            l->fx = (uint8_t)clamp((int32_t)l->fx + (d > 0 ? 1 : -1), 0, KICK_FX - 1);
        else if (i == 3)
            l->wet = knob_f(l->wet, d, 0.02f, 0.0f, 1.0f);
        else if (kick_knob_name(i)[0]) {
            float *p = i == 1 ? &l->fa : &l->fb;
            *p = knob_f(*p, d, 0.02f, 0.0f, 1.0f);
        }
        break;
    case KP_MACRO:
        if (i == 3)
            k->intensity = knob_f(k->intensity, d, 0.02f, 0.0f, 1.0f);
        else {
            uint32_t j;
            for (j = 0; j < KM_N; j++)
                *kick_macro(i, j) = knob_f(*kick_macro(i, j), d, 0.02f, 0.0f, 1.0f);
        }
        break;
    default:
        if (i == 0)
            k->volume = knob_f(k->volume, d, 0.02f, 0.0f, 1.0f);
        else if (i == 1)
            set.drums_chain = d > 0 ? 0u : 1u;  /* right: around the chain, left: through it */
        else
            return;
        break;
    }
    kick_value(i, t);
    if (ui.kpage == KP_LAYER || (ui.kpage == KP_FX && i)) {   /* "GRIT  DECAY 240 MS" */
        char w[24];
        w[0] = 0;
        text_cat(w, kick_knob_name(i));
        text_cat(w, " ");
        text_cat(w, t);
        app_say(KICK_LAYER_NAME[ui.klayer % KICK_LAYERS], w);
    } else
        app_say(kick_knob_name(i), t);
    ui.redraw |= RG_BLOCKS;
    changed();
}

/* the KICK page's strip: the kick's shape and its layers' levels; then its 16 steps (the one playing outlined),
 * what the page edits and where the kick goes */
#define KS_W 160u                        /* the shape's columns: its first 600 ms */
static uint8_t kick_px[KS_W];
static uint32_t kick_px_sig = 1u;

static void draw_kick(const brk_status_t *st)
{
    const kick_t *k = &set.kick;
    uint32_t i, sig = 2166136261u;
    char t[40];
    for (i = 0; i < sizeof *k; i++)      /* the sound's bytes: changed? */
        sig = (sig ^ ((const uint8_t *)k)[i]) * 16777619u;
    if (sig != kick_px_sig) {
        kick_px_sig = sig;
        kick_shape(k, kick_px, KS_W, 600u, 36u);
    }
    cv_begin(240, 52, C_BLACK);
    for (i = 0; i < KS_W; i++)           /* the shape, mirrored about its centre line */
        if (kick_px[i])
            cv_rect(4 + (int32_t)i, 26 - kick_px[i] / 2, 1, kick_px[i] | 1, k->volume > 0.0f ? C_HI : C_DIM);
    cv_rect(4, 26, KS_W, 1, C_LINE);
    cv_text(6, 0, &FONT_XS, KPAGE_NAME[ui.kpage % KPAGES], C_GRAY);
    for (i = 0; i < KICK_LAYERS; i++) {  /* the layers' levels, the one edited outlined */
        int32_t x = 172 + (int32_t)i * 17, h = (int32_t)(k->l[i].level * 36.0f + 0.5f);
        int sel = i == ui.klayer && (ui.kpage == KP_LAYER || ui.kpage == KP_FX);
        cv_rect(x, 2, 14, 38, sel ? C_WHITE : C_BLOCK);
        cv_rect(x + 1, 3, 12, 36, C_BLOCK);
        if (h)
            cv_rect(x + 1, 39 - h, 12, h, sel ? C_HI : k->l[i].fx ? C_AMB : C_GRAY);
        t[0] = KICK_LAYER_NAME[i][0];
        t[1] = 0;
        cv_text(x + 4, 41, &FONT_XS, t, sel ? C_HI : C_GRAY);
    }
    cv_blit(0, 88);
    draw_lanes(st, LANE_KICK, "KEYS: KICK STEPS  ALGO: LAYER  PRESETS: KICK");
}
