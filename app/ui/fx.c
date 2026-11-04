/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the FX page: the master chain's slots, their settings and the chain drawn. (Part of app.c's unity:
 * included there in order.) */

/* each effect's settings, as the foot names them (K1-K3; K4 is MIX) */
static const char *const FXP_NAME[BRK_NFXT][3] = {
    {"", "", ""}, {"CUTOFF", "RESO", "MODE"}, {"BITS", "RATE", "DRIVE"}, {"GAIN", "TONE", "LEVEL"},
    {"AMOUNT", "ATTACK", "RELEASE"}, {"RATE", "DEPTH", "FEEDBK"}, {"FREQ", "SPREAD", ""}, {"RATE", "DEPTH", "PAN"},
    {"SIZE", "TONE", "PRE"}, {"TIME", "FEEDBK", "TONE"}, {"LOW", "MID", "HIGH"}, {"DRIVE", "WARMTH", "WOW"},
    {"LOW", "MID", "HIGH"}, {"DRIVE", "KNEE", "CEILING"}};

/* ------------------------------------------------------- MB COMP's detail --- */
/* (PRD A-49a) an MB COMP focused has six parts: AMOUNT (the slot's own knobs: each band's amount, MIX), each band's
 * THRESH RATIO ATTACK RELEASE, GAIN (each band's level after it, SOLO) and X-OVER (the splits, KNEE, OUTPUT). FX
 * again steps them, then on to the next slot; ALGORITHM still picks the slot (and starts at AMOUNT) */
enum { MBP_AMOUNT, MBP_LOW, MBP_MID, MBP_HIGH, MBP_GAIN, MBP_XOVER, NMBP };
static const char *const MBP_NAME[NMBP] = {"AMOUNT", "LOW", "MID", "HIGH", "GAIN", "X-OVER"};
static const char *const MBP_TAB[NMBP] = {"AMT", "LOW", "MID", "HIGH", "GAIN", "XOVER"};
static const char *const MB_BAND_NAME[MB_BANDS] = {"LOW", "MID", "HIGH"};
static const char *const MBK_BAND[4] = {"THRESH", "RATIO", "ATTACK", "RELEASE"};
static const char *const MBK_GAIN[4] = {"LOW", "MID", "HIGH", "SOLO"};
static const char *const MBK_XOVER[4] = {"LOW X", "HIGH X", "KNEE", "OUTPUT"};
/* each knob's step a detent: 1 dB of threshold, a ratio, 0.5 dB of gain, knee and output */
static const float MB_STEP[NMBP][4] = {{0.0f, 0.0f, 0.0f, 0.0f},
                                       {1.0f / 48.0f, 1.0f / 14.0f, 0.02f, 0.02f},
                                       {1.0f / 48.0f, 1.0f / 14.0f, 0.02f, 0.02f},
                                       {1.0f / 48.0f, 1.0f / 14.0f, 0.02f, 0.02f},
                                       {1.0f / 48.0f, 1.0f / 48.0f, 1.0f / 48.0f, 0.0f},
                                       {0.02f, 0.02f, 1.0f / 24.0f, 1.0f / 48.0f}};
/* the bands' colours: the low warm, the mid amber, the high ice; dim for fills, a darker tint for the bands not
 * focused */
static const uint16_t MB_COL[MB_BANDS] = {RGB(255, 104, 72), RGB(255, 184, 70), RGB(130, 210, 255)};
static const uint16_t MB_DIM[MB_BANDS] = {RGB(120, 44, 30), RGB(116, 80, 26), RGB(44, 84, 112)};
static const uint16_t MB_TINT_HI[MB_BANDS] = {RGB(58, 30, 26), RGB(52, 42, 22), RGB(26, 40, 56)};

static int mb_detail(uint32_t slot)      /* the focused MB COMP past AMOUNT: K1-K4 edit its detail */
{
    return (slot & 3u) == (ui.fx_focus & 3u) && set.fx.slot[slot & 3u].type == FXT_MBCOMP &&
           ui.mb_page != MBP_AMOUNT && ui.mb_page < NMBP;
}
static const char *mb_knob_name(int i)
{
    return ui.mb_page == MBP_GAIN ? MBK_GAIN[i & 3] : ui.mb_page == MBP_XOVER ? MBK_XOVER[i & 3] : MBK_BAND[i & 3];
}
static float *mb_knob_val(brk_mbc_t *m, int i)   /* the setting knob i turns (0: SOLO, not a float) */
{
    uint32_t b = (uint32_t)(ui.mb_page - MBP_LOW) % MB_BANDS;
    i &= 3;
    if (ui.mb_page == MBP_GAIN)
        return i < 3 ? &m->gain[i] : 0;
    if (ui.mb_page == MBP_XOVER)
        return i == 0 ? &m->xlo : i == 1 ? &m->xhi : i == 2 ? &m->knee : &m->out;
    return i == 0 ? &m->thr[b] : i == 1 ? &m->ratio[b] : i == 2 ? &m->att[b] : &m->rel[b];
}
static void db_text(float db, char *t)   /* "+3.5DB", "-30.0DB", "0 DB" */
{
    t[0] = 0;
    if (db > -0.05f && db < 0.05f)
        text_cat(t, "0 DB");
    else
        signed_tenths(db, "DB", t);
}
static void ms_text(float ms, char *t)   /* "0.4MS", "150MS" */
{
    t[0] = 0;
    if (ms < 9.95f)
        tenths_text((int32_t)(ms * 10.0f + 0.5f), t);
    else
        int_text((int32_t)(ms + 0.5f), t);
    text_cat(t, "MS");
}
static void ratio_text(float r, char *t) /* "OFF", "2.5:1", "8:1", "LIMIT" */
{
    int32_t r10 = (int32_t)(r * 10.0f + 0.5f);
    t[0] = 0;
    if (r >= 999.0f)
        text_cat(t, "LIMIT");
    else if (r10 <= 10)
        text_cat(t, "OFF");
    else {
        if (r10 % 10)
            tenths_text(r10, t);
        else
            int_text(r10 / 10, t);
        text_cat(t, ":1");
    }
}
static void mb_value(int i, char *t)
{
    brk_mbc_t *m = &set.mbc[ui.fx_focus & 3u];
    float *v;
    i &= 3;
    t[0] = 0;
    if (ui.mb_page == MBP_GAIN && i == 3) {
        text_cat(t, m->solo && m->solo <= MB_BANDS ? MB_BAND_NAME[m->solo - 1u] : "OFF");
        return;
    }
    v = mb_knob_val(m, i);
    if (ui.mb_page == MBP_GAIN)
        db_text(brk_mb_db24(*v), t);
    else if (ui.mb_page == MBP_XOVER) {
        if (i == 0)
            hz_text(brk_mb_xlo_hz(*v), t);
        else if (i == 1)
            hz_text(brk_mb_xhi_hz(*v), t);
        else if (i == 2) {               /* (a width: no sign) */
            if (*v < 0.01f)
                text_cat(t, "HARD");
            else {
                tenths_text((int32_t)(brk_mb_knee_db(*v) * 10.0f + 0.5f), t);
                text_cat(t, "DB");
            }
        } else
            db_text(brk_mb_db24(*v), t);
    } else if (i == 0)
        db_text(brk_mb_thr_db(*v), t);
    else if (i == 1)
        ratio_text(brk_mb_ratio(*v), t);
    else
        ms_text(i == 2 ? brk_mb_att_ms(*v) : brk_mb_rel_ms(*v), t);
}
static void mb_knob_turn(int i, int32_t d)
{
    brk_mbc_t *m = &set.mbc[ui.fx_focus & 3u];
    char t[16], n[24];
    i &= 3;
    if (ui.mb_page == MBP_GAIN && i == 3)
        m->solo = (uint8_t)clamp((int32_t)m->solo + (d > 0 ? 1 : -1), 0, MB_BANDS);
    else {
        float *v = mb_knob_val(m, i);
        *v = knob_f(*v, d, MB_STEP[ui.mb_page % NMBP][i], 0.0f, 1.0f);
    }
    mb_value(i, t);
    n[0] = 0;
    if (ui.mb_page >= MBP_LOW && ui.mb_page <= MBP_HIGH) {   /* "LOW RATIO" */
        text_cat(n, MB_BAND_NAME[ui.mb_page - MBP_LOW]);
        text_cat(n, " ");
    } else if (ui.mb_page == MBP_GAIN && i < 3)
        text_cat(n, "GAIN ");
    text_cat(n, mb_knob_name(i));
    app_say(n, t);
    ui.redraw |= RG_BLOCKS;
    changed();
}
/* FX again on the FX page: an MB COMP's next part, past its last the next slot */
static void fx_algo(int32_t d, uint32_t sel);
static void fx_next_part(void)
{
    if (set.fx.slot[ui.fx_focus & 3u].type == FXT_MBCOMP && ui.mb_page + 1u < NMBP) {
        ui.mb_page++;
        page_say();
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    fx_algo(1, 0u);
}

static const char *fx_param_name(uint32_t slot, int i)
{
    const brk_fxslot_t *sl = &set.fx.slot[slot & 3u];
    if (mb_detail(slot))
        return mb_knob_name(i);
    if (i == 3)
        return sl->type == FXT_EMPTY ? "" : sl->type == FXT_DELAY ? "SEND" : sl->type == FXT_EQ ? "FREQ" : "MIX";
    return FXP_NAME[sl->type < BRK_NFXT ? sl->type : 0][i & 3];
}

static void fx_value(uint32_t slot, int i, char *t)
{
    const brk_fxslot_t *sl = &set.fx.slot[slot & 3u];
    float v = i == 3 ? sl->mix : sl->p[i & 3];
    t[0] = 0;
    if (mb_detail(slot)) {
        mb_value(i, t);
        return;
    }
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
    if (i >= 0 && i <= 3 && mb_detail(slot)) {
        mb_knob_turn(i, d);
        return;
    }
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
    if (t == FXT_MBCOMP)                 /* an MB COMP put in: its detail from the start, at its AMOUNT */
        brk_mbc_defaults(&set.mbc[ui.fx_focus & 3u]);
    ui.mb_page = MBP_AMOUNT;
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
        brk_mbc_t tm = set.mbc[a];
        if ((d > 0 && a == 3u) || (d < 0 && a == 0u))
            return;                      /* (the ends stay the ends) */
        set.fx.slot[a] = set.fx.slot[b];
        set.fx.slot[b] = tmp;
        set.mbc[a] = set.mbc[b];         /* (an MB COMP's detail goes with it) */
        set.mbc[b] = tm;
        changed();
    } else
        ui.mb_page = MBP_AMOUNT;         /* another slot: an MB COMP there starts at its AMOUNT */
    ui.fx_focus = (uint8_t)b;
    int_text((int32_t)b + 1, n);
    if (sel)
        app_say("MOVED TO", n);
    else
        page_say();
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

/* an MB COMP band as it acts now (its amount of the detail's threshold and ratio): the threshold (dB), the slope
 * (1 - 1/ratio), the ratio, the knee (dB) */
typedef struct { float thr, slope, ratio, knee; } mb_eff_t;
static mb_eff_t mb_eff(uint32_t slot, uint32_t b)
{
    const brk_fxslot_t *sl = &set.fx.slot[slot & 3u];
    const brk_mbc_t *m = &set.mbc[slot & 3u];
    float amt = brk_clampf(sl->p[b % MB_BANDS], 0.0f, 1.0f);
    mb_eff_t e;
    e.thr = brk_mb_thr_db(m->thr[b]) * amt;
    e.ratio = 1.0f + (brk_mb_ratio(m->ratio[b]) - 1.0f) * amt;
    e.slope = 1.0f - 1.0f / e.ratio;
    e.knee = brk_mb_knee_db(m->knee);
    return e;
}

/* the bands' response at f Hz, each at its gain g[] now: the low's 2-pole low-pass and the high's 2-pole high-pass
 * (as the engine's SVFs at Q 0.5, analog), the mid what is left; the dry under it by the mix; in dB. *band: the band
 * giving most of it there */
static float mb_resp_db(float f, float flo, float fhi, const float *g, uint32_t solo, float mix, uint32_t *band)
{
    float w = f / flo, a = 1.0f - w * w, b = 2.0f * w, n = a * a + b * b;
    float v = f / fhi, c = 1.0f - v * v, e = 2.0f * v, m = c * c + e * e;
    float hr[3], hi[3], tr = 0.0f, ti = 0.0f, best = -1.0f;
    uint32_t k;
    hr[0] = a / n;                       /* 1 / (1 + jw)^2 */
    hi[0] = -b / n;
    hr[2] = -v * v * c / m;              /* (jv)^2 / (1 + jv)^2 */
    hi[2] = v * v * e / m;
    hr[1] = 1.0f - hr[0] - hr[2];        /* the mid: what the two leave */
    hi[1] = -hi[0] - hi[2];
    for (k = 0; k < MB_BANDS; k++) {
        float p = (hr[k] * hr[k] + hi[k] * hi[k]) * g[k] * g[k];
        if (solo && solo != k + 1u)
            continue;
        tr += g[k] * hr[k];
        ti += g[k] * hi[k];
        if (p > best) {
            best = p;
            *band = k;
        }
    }
    if (!solo) {                         /* the parallel mix */
        tr = 1.0f + (tr - 1.0f) * mix;
        ti *= mix;
    }
    return 3.0103f * brk_log2f(tr * tr + ti * ti + 1e-9f);
}

static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

/* the FX page with an MB COMP focused (PRD A-49a). Above: its parts as tabs, then the spectrum 20 Hz .. 20 kHz split
 * in its three bands, the curve the sound goes through right now (each band at its reduction: it dips as the band is
 * pressed), the splits marked. Below: each band's in / out curve (threshold, ratio, knee) with its level riding it as
 * a dot, its level in, its reduction (a peak held a second), its ratio, threshold and gain */
#define MB_X(f) ((int32_t)(brk_log2f((f) * 0.05f) * (239.0f / 9.9658f) + 0.5f))   /* 20 Hz .. 20 kHz on 0..239 */
static void draw_mbcomp(uint32_t now)
{
    uint32_t k = ui.fx_focus & 3u, b, solo;
    const brk_fxslot_t *sl = &set.fx.slot[k];
    const brk_mbc_t *m = &set.mbc[k];
    float gr[MB_BANDS], lvl[MB_BANDS], g[MB_BANDS], g_rest[MB_BANDS], grdb[MB_BANDS], flo, fhi;
    mb_eff_t e[MB_BANDS];
    int32_t x, xlo, xhi, py = 0, focus = ui.mb_page >= MBP_LOW && ui.mb_page <= MBP_HIGH ? ui.mb_page - MBP_LOW : -1;
    char t[16];
    solo = m->solo <= MB_BANDS ? m->solo : 0u;
    brk_fx_mb_meter(k, gr, lvl);
    flo = brk_mb_xlo_hz(m->xlo);
    fhi = brk_mb_xhi_hz(m->xhi);
    xlo = MB_X(flo);
    xhi = MB_X(fhi);
    for (b = 0; b < MB_BANDS; b++) {
        e[b] = mb_eff(k, b);
        g_rest[b] = brk_exp2f((brk_mb_db24(m->gain[b]) + brk_mb_db24(m->out)) * 0.16610f);
        g[b] = gr[b] * g_rest[b];
        grdb[b] = gr[b] < 0.9995f ? -6.0206f * brk_log2f(gr[b] > 0.001f ? gr[b] : 0.001f) : 0.0f;
        if (grdb[b] >= ui.mb_hold[b] || now - ui.mb_hold_t[b] > 1000u) {   /* the peak, held a second */
            ui.mb_hold[b] = grdb[b];
            ui.mb_hold_t[b] = now;
        }
    }

    /* ---- above: the parts, the spectrum */
    cv_begin(240, 52, C_BLACK);
    for (b = 0; b < NMBP; b++) {         /* the parts: FX again steps them */
        int32_t cx = 2 + (int32_t)b * 32, on = b == ui.mb_page;
        cv_rect(cx, 0, 30, 11, on ? C_HI : C_BLOCK);
        cv_text(cx + (30 - text_w(&FONT_T, MBP_TAB[b])) / 2, 0, &FONT_T, MBP_TAB[b], on ? C_BLACK : C_GRAY);
    }
    {
        const char *s = !sl->on ? "BYPASS" : solo ? "SOLO" : "ON";
        cv_text(237 - text_w(&FONT_T, s), 0, &FONT_T, s, !sl->on ? C_DIM : solo ? MB_COL[solo - 1u] : C_HI);
    }
    for (x = 0; x < 3; x++) {            /* 100 Hz, 1 kHz, 10 kHz: faint marks */
        int32_t gx = MB_X(x == 0 ? 100.0f : x == 1 ? 1000.0f : 10000.0f), y;
        for (y = 13; y < 41; y += 3)
            cv_rect(gx, y, 1, 1, C_LINE);
    }
    for (x = 0; x < 240; x++) {          /* 1 px a dB, +4 .. -23 (0 dB on row 17): where it rests (dotted: each band's
                                          * GAIN and the OUTPUT), and now, filled under in its band's colour (its
                                          * reduction a dip; the make-up left out, it would hide the dip) */
        uint32_t zb = MB_MID, zr = MB_MID;
        float f = 20.0f * brk_exp2f((float)x * (9.9658f / 239.0f)), mix = brk_clampf(sl->mix, 0.0f, 1.0f);
        float db = mb_resp_db(f, flo, fhi, g, solo, sl->on ? mix : 0.0f, &zb);
        float rdb = mb_resp_db(f, flo, fhi, g_rest, solo, sl->on ? mix : 0.0f, &zr);
        int32_t y = 17 - (int32_t)(db + (db < 0.0f ? -0.5f : 0.5f)), ry = 17 - (int32_t)(rdb + (rdb < 0.0f ? -0.5f : 0.5f));
        int lit = focus < 0 ? !solo : (int32_t)zb == focus;
        y = y < 13 ? 13 : y > 40 ? 40 : y;
        ry = ry < 13 ? 13 : ry > 39 ? 39 : ry;
        if (y < 40)
            cv_rect(x, y, 1, 40 - y, !sl->on ? C_BLOCK : lit || (solo && solo == zb + 1u) ? MB_DIM[zb] : MB_TINT_HI[zb]);
        if (!(x & 1) && ry < y)          /* where it would be: what the reduction takes away shows between */
            cv_rect(x, ry, 1, 1, C_GRAY);
        if (x && y < 40)
            cv_line(x - 1, py, x, y, sl->on ? MB_COL[zb] : C_GRAY);
        py = y < 40 ? y : 39;
    }
    for (x = 0; x < 240; x += 4)         /* 0 dB */
        cv_rect(x, 17, 2, 1, C_DIM);
    for (b = 0; b < 2u; b++) {           /* the splits, their frequencies under them (lit on X-OVER) */
        int32_t sx = b ? xhi : xlo, y, w;
        uint16_t c = ui.mb_page == MBP_XOVER ? C_WHITE : C_GRAY;
        for (y = 13; y < 41; y += 2)
            cv_rect(sx, y, 1, 1, c);
        hz_text(b ? fhi : flo, t);
        w = text_w(&FONT_T, t);
        x = sx - w / 2;
        x = x < 1 ? 1 : x + w > 239 ? 239 - w : x;
        cv_text(x, 40, &FONT_T, t, c);
    }
    cv_blit(0, 88);

    /* ---- below: each band */
    cv_begin(240, 52, C_BLACK);
    for (b = 0; b < MB_BANDS; b++) {
        int32_t x0 = (int32_t)b * 80, bx = x0 + 3, by = 14, i, cy = 0;
        int dim = !sl->on || (solo && solo != b + 1u);
        uint16_t col = dim ? C_GRAY : MB_COL[b];
        float in_db = lvl[b] > 0.0039f ? 6.0206f * brk_log2f(lvl[b]) : -48.0f;   /* (-48 dB and under: the floor) */
        if ((int32_t)b == focus)
            cv_frame(x0 + 1, 0, 78, 52, C_WHITE);
        cv_text(x0 + 4, 1, &FONT_T, MB_BAND_NAME[b], col);
        if (grdb[b] >= 0.1f) {           /* its reduction now */
            signed_tenths(-grdb[b], "DB", t);
            cv_text(x0 + 76 - text_w(&FONT_T, t), 1, &FONT_T, t, C_WHITE);
        }
        /* its curve: in -48 .. 0 dB across 34 px, out the same up */
        cv_rect(bx, by, 34, 34, C_BLOCK);
        for (i = 0; i < 34; i += 2)      /* in = out */
            cv_rect(bx + i, by + 33 - i, 1, 1, C_LINE);
        if (e[b].ratio > 1.001f) {       /* the threshold */
            int32_t tx = bx + (int32_t)((e[b].thr + 48.0f) * (33.0f / 48.0f) + 0.5f);
            for (i = 0; i < 34; i += 3)
                cv_rect(tx, by + i, 1, 1, C_DIM);
        }
        for (i = 0; i < 34; i++) {
            float idb = -48.0f + (float)i * (48.0f / 33.0f), odb = idb - brk_mb_reduce(idb - e[b].thr, e[b].slope, e[b].knee);
            int32_t y = by + 33 - (int32_t)((odb + 48.0f) * (33.0f / 48.0f) + 0.5f);
            y = y < by ? by : y > by + 33 ? by + 33 : y;
            if (i)
                cv_line(bx + i - 1, cy, bx + i, y, col);
            cy = y;
        }
        if (lvl[b] > 0.0039f && sl->on) {   /* its level, riding the curve */
            float odb = in_db - brk_mb_reduce(in_db - e[b].thr, e[b].slope, e[b].knee);
            int32_t dx = bx + (int32_t)((in_db + 48.0f) * (33.0f / 48.0f) + 0.5f);
            int32_t dy = by + 33 - (int32_t)((odb + 48.0f) * (33.0f / 48.0f) + 0.5f);
            dx = dx > bx + 33 ? bx + 33 : dx;
            dy = dy < by ? by : dy;
            cv_rect(dx - 1, dy - 1, 3, 3, C_WHITE);
        }
        {                                /* its level in (up from the floor), the threshold marked */
            int32_t h = (int32_t)((in_db + 48.0f) * (34.0f / 48.0f) + 0.5f), ty;
            cv_rect(x0 + 40, by, 4, 34, C_BLOCK);
            if (h > 0)
                cv_rect(x0 + 40, by + 34 - h, 4, h, dim ? C_DIM : MB_DIM[b]);
            ty = by + 33 - (int32_t)((e[b].thr + 48.0f) * (33.0f / 48.0f) + 0.5f);
            if (e[b].ratio > 1.001f)
                cv_rect(x0 + 39, ty, 6, 1, C_WHITE);
        }
        {                                /* its reduction (down from the top, 0 .. 24 dB), the peak held */
            int32_t h = (int32_t)(brk_clampf(grdb[b], 0.0f, 24.0f) * (34.0f / 24.0f) + 0.5f);
            int32_t hh = (int32_t)(brk_clampf(ui.mb_hold[b], 0.0f, 24.0f) * (34.0f / 24.0f) + 0.5f);
            cv_rect(x0 + 46, by, 5, 34, C_BLOCK);
            if (h > 0)
                cv_rect(x0 + 46, by, 5, h, C_HI);
            if (hh > 1)
                cv_rect(x0 + 46, by + hh - 1, 5, 1, C_WHITE);
        }
        ratio_text(e[b].ratio, t);       /* its ratio, threshold and gain as they act */
        cv_text(x0 + 54, by - 1, &FONT_T, t, e[b].ratio > 1.001f ? col : C_DIM);
        if (e[b].ratio > 1.001f) {
            int_text((int32_t)(e[b].thr - 0.5f), t);
            cv_text(x0 + 54, by + 11, &FONT_T, t, C_GRAY);
        }
        {
            float gdb = brk_mb_db24(m->gain[b]);
            if (gdb > 0.05f || gdb < -0.05f) {
                signed_tenths(gdb, "", t);
                cv_text(x0 + 54, by + 23, &FONT_T, t, C_GRAY);
            }
        }
    }
    cv_blit(0, 140);
}

/* the FX page: the chain as four tiles in order, IN to OUT (the focused one outlined), then the focused slot (an MB
 * COMP has its own view: draw_mbcomp) */
static void draw_chain(void)
{
    const brk_fx_t *f = &set.fx;
    uint32_t k, i;
    char t[24], v[16];
    if (f->slot[ui.fx_focus & 3u].type == FXT_MBCOMP) {
        draw_mbcomp(plat_ms());
        return;
    }
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
