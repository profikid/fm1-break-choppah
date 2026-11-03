/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the KICK page (EDIT): the kick's layers, effects, macros and output. (Part of app.c's unity: included
 * there in order.) */

/* ---- the KICK page (PRD K-*): EDIT opens it, EDIT again the next sub-page. The white keys are the kick's 16
 * steps, ALGORITHM the layer, PRESETS the built-in kicks, SEL + EDIT through the FX chain or around it */
enum { KP_MIX, KP_LAYER, KP_FX, KP_MACRO, KP_OUT, KPAGES };
static const char *const KPAGE_NAME[KPAGES] = {"MIX", "LAYER", "LAYER FX", "MACRO", "OUT"};
/* the kick's parts in a row, as ALGORITHM turns through them: MIX, SUB, SUB FX, PUNCH, PUNCH FX, GRIT, GRIT FX,
 * CLICK, CLICK FX, MACRO, OUT (each a sub-page, and for a layer its layer) */
#define KPARTS (3u + 2u * KICK_LAYERS)
static uint32_t kick_part(void)
{
    return ui.kpage == KP_MIX ? 0u : ui.kpage == KP_MACRO ? KPARTS - 2u : ui.kpage == KP_OUT ? KPARTS - 1u
           : 1u + ui.klayer * 2u + (ui.kpage == KP_FX ? 1u : 0u);
}
static void kick_part_name(uint32_t p, char *t)
{
    if (p == 0u || p >= KPARTS - 2u) {
        text_cat(t, KPAGE_NAME[p == 0u ? KP_MIX : p == KPARTS - 2u ? KP_MACRO : KP_OUT]);
        return;
    }
    text_cat(t, KICK_LAYER_NAME[(p - 1u) / 2u]);
    if (!((p - 1u) & 1u))
        return;
    text_cat(t, " FX");
}
static void kick_part_set(uint32_t p)
{
    if (p == 0u)
        ui.kpage = KP_MIX;
    else if (p == KPARTS - 2u)
        ui.kpage = KP_MACRO;
    else if (p == KPARTS - 1u)
        ui.kpage = KP_OUT;
    else {
        ui.kpage = (p - 1u) & 1u ? KP_FX : KP_LAYER;
        ui.klayer = (uint8_t)((p - 1u) / 2u);
    }
    page_say();
}
static const char *const KN_KICK[KPAGES][4] = {{"SUB", "PUNCH", "GRIT", "CLICK"},
                                               {"LEVEL", "PITCH", "DECAY", "TONE"},
                                               {"EFFECT", "", "", "WET"},
                                               {"SUB", "PUNCH", "GRIT", "INTENSITY"},
                                               {"VOLUME", "", "", ""}};
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

/* EDIT's lower strip: the three drums side by side, each its own waveform (one hit, worked out again when its sound
 * changes), the one edited outlined, lit on its hits */
#define DW 66u                           /* a drum's waveform: its columns */
static uint8_t drum_px[3][DW];
static uint32_t drum_sig[3] = {1u, 1u, 1u};
static uint32_t sig_of(const void *p, uint32_t n)
{
    uint32_t i, h = 2166136261u;
    for (i = 0; i < n; i++)
        h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
    return h;
}
static void draw_drums(const brk_status_t *st)
{
    static const char *const NAME[3] = {"KICK", "SNARE", "HAT"};
    uint32_t i, c;
    for (i = 0; i < 3u; i++) {
        uint32_t sig = i ? sig_of(&set.perc.v[i - 1u], sizeof set.perc.v[0]) : sig_of(&set.kick, sizeof set.kick);
        if (sig != drum_sig[i]) {        /* its hit again, over a window that fits its decay, as tall as the box */
            uint32_t pk = 0u;
            drum_sig[i] = sig;
            if (i) {
                float d = perc_decay_ms(i - 1u, set.perc.v[i - 1u].decay) * 1.4f;
                perc_shape(i - 1u, &set.perc.v[i - 1u], drum_px[i], DW, (uint32_t)brk_clampf(d, 60.0f, 900.0f), 120u);
            } else
                kick_shape(&set.kick, drum_px[0], DW, 600u, 120u);
            for (c = 0; c < DW; c++)
                pk = drum_px[i][c] > pk ? drum_px[i][c] : pk;
            for (c = 0; c < DW && pk; c++)
                drum_px[i][c] = (uint8_t)(drum_px[i][c] * 30u / pk);
        }
    }
    cv_begin(240, 52, C_BLACK);
    for (i = 0; i < 3u; i++) {
        int32_t x = 2 + (int32_t)i * 80;
        int sel = i == ui.einst, hit = st->playing && st->bar16 >= 0 && (lane_now[i] >> st->bar16 & 1u);
        uint16_t ink = hit ? C_BLACK : sel ? C_HI : C_GRAY;
        cv_rect(x, 2, 76, 48, sel ? C_WHITE : C_LINE);
        cv_rect(x + 1, 3, 74, 46, hit ? C_BLOCK_HI : C_BLACK);
        cv_text(x + 5, 4, &FONT_T, NAME[i], hit ? C_BLACK : sel ? C_HI : C_GRAY);
        cv_rect(x + 5, 33, DW, 1, hit ? C_BLACK : C_LINE);   /* the centre line, the hit mirrored about it */
        for (c = 0; c < DW; c++)
            if (drum_px[i][c])
                cv_rect(x + 5 + (int32_t)c, 33 - drum_px[i][c] / 2, 1, drum_px[i][c] | 1, ink);
    }
    cv_blit(0, 140);
}

/* the kick's page (EDIT): as the snare's and the hat's: its part's four settings as bars (lit), and under them the
 * four layers' levels (the one the part edits lit, amber with an effect on); then the three drums */
static float kick_knob_f(int i)          /* the part's knob i, 0..1 (as its bar) */
{
    const kick_t *k = &set.kick;
    const kick_layer_t *l = &k->l[ui.klayer % KICK_LAYERS];
    switch (ui.kpage) {
    case KP_MIX:
        return k->l[i].level;
    case KP_LAYER:
        return i == 0 ? l->level : i == 1 ? l->pitch : i == 2 ? l->decay : l->tone;
    case KP_FX:
        return i == 0 ? (float)l->fx / (float)(KICK_FX - 1) : i == 1 ? l->fa : i == 2 ? l->fb : l->wet;
    case KP_MACRO:
        return i == 3 ? k->intensity : kick_macro_value(i);
    default:
        return i == 0 ? k->volume : 0.0f;
    }
}
static void draw_kick(const brk_status_t *st)
{
    const kick_t *k = &set.kick;
    uint32_t i;
    char t[24];
    cv_begin(240, 52, C_BLACK);
    t[0] = 0;
    text_cat(t, "KICK  ");
    kick_part_name(kick_part(), t);
    cv_text(6, 0, &FONT_XS, t, C_GRAY);
    for (i = 0; i < 4u; i++) {           /* the part's four settings */
        int32_t x = 6 + (int32_t)i * 58;
        const char *n = kick_knob_name((int)i);
        if (!n[0])
            continue;
        cv_text(x, 16, &FONT_XS, n, C_GRAY);
        cv_rect(x, 29, 52, 3, C_BLOCK);
        cv_rect(x, 29, (int32_t)(52.0f * brk_clampf(kick_knob_f((int)i), 0.0f, 1.0f) + 0.5f), 3, C_HI);
    }
    for (i = 0; i < KICK_LAYERS; i++) {  /* the layers' levels */
        int32_t x = 6 + (int32_t)i * 58;
        int lit = i == ui.klayer && (ui.kpage == KP_LAYER || ui.kpage == KP_FX);   /* (MIX: the row above is them) */
        cv_text(x, 34, &FONT_XS, KICK_LAYER_NAME[i], lit ? C_GRAY : C_DIM);
        cv_rect(x, 47, 52, 3, C_BLOCK);
        cv_rect(x, 47, (int32_t)(52.0f * k->l[i].level + 0.5f), 3, lit ? C_HI : k->l[i].fx ? C_AMB : C_DIM);
    }
    cv_blit(0, 88);
    draw_drums(st);
}
