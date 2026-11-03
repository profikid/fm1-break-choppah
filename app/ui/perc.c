/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the SNARE / HAT page (ENV). (Part of app.c's unity: included there in order.) */

/* ---- the SNARE / HAT page (ENV): ALGORITHM the voice, ENV again SOUND or SHAPE, PRESETS its presets */
static const char *const PERC_KNOB[2][2][4] = {{{"LEVEL", "TUNE", "DECAY", "TONE"}, {"SNAP", "DRIVE", "", ""}},
                                               {{"LEVEL", "TUNE", "DECAY", "METAL/NZ"}, {"CUT", "DRIVE", "", ""}}};
static float *perc_param(int i)
{
    perc_voice_t *v = &set.perc.v[ui.pvoice % PERC_VOICES];
    float *P[6] = {&v->level, &v->tune, &v->decay, &v->tone, &v->snap, &v->drive};
    int k = ui.ppage ? 4 + i : i;
    return k < 6 ? P[k] : 0;
}
static void perc_value(int i, char *t)
{
    float *p = perc_param(i);
    t[0] = 0;
    if (!p)
        return;
    if (!ui.ppage && i == 1)
        hz_text(perc_tune_hz(ui.pvoice, *p), t);
    else if (!ui.ppage && i == 2) {
        int_text((int32_t)(perc_decay_ms(ui.pvoice, *p) + 0.5f), t);
        text_cat(t, " MS");
    } else
        pct_text(*p, t);
}
static void perc_turn(int i, int32_t d)
{
    float *p = perc_param(i);
    char t[12];
    if (!p)
        return;
    *p = knob_f(*p, d, 0.02f, 0.0f, 1.0f);
    perc_value(i, t);
    app_say(PERC_KNOB[ui.pvoice % PERC_VOICES][ui.ppage][i], t);
    ui.redraw |= RG_BLOCKS;
    changed();
}

/* the SNARE / HAT page: the voice's six settings as bars (the sub-page's lit); then the lanes */
static void draw_perc(const brk_status_t *st)
{
    const perc_voice_t *v = &set.perc.v[ui.pvoice % PERC_VOICES];
    const float P[6] = {v->level, v->tune, v->decay, v->tone, v->snap, v->drive};
    uint32_t i;
    char t[24];
    cv_begin(240, 52, C_BLACK);
    t[0] = 0;
    text_cat(t, PERC_VOICE_NAME[ui.pvoice % PERC_VOICES]);
    text_cat(t, ui.ppage ? "  SHAPE" : "  SOUND");
    cv_text(6, 0, &FONT_XS, t, C_GRAY);
    for (i = 0; i < 6u; i++) {
        int32_t x = 6 + (int32_t)(i % 4u) * 58, y = 16 + (int32_t)(i / 4u) * 18;
        int lit = (i >= 4u) == (ui.ppage != 0u);
        cv_text(x, y, &FONT_XS, PERC_KNOB[ui.pvoice % PERC_VOICES][i / 4u][i % 4u], lit ? C_GRAY : C_DIM);
        cv_rect(x, y + 13, 52, 3, C_BLOCK);
        cv_rect(x, y + 13, (int32_t)(52.0f * P[i] + 0.5f), 3, lit ? C_HI : C_DIM);
    }
    cv_blit(0, 88);
    draw_lanes(st, LANE_SNARE + ui.pvoice);
}
