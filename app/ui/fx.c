/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the FX page: the master chain's slots, their settings and the chain drawn. (Part of app.c's unity:
 * included there in order.) */

/* -------------------------------------------------- the slots the views edit --- */
/* The FX views edit a slot of the chain (the FX page: 0..3) or a black key's effect on the ARP page (PRD B-55:
 * BRK_FX_SLOTS + the key; its settings in set.pfx, set.pfxx). The ARP page's state lives here, for the views (arp.c
 * plays it) */
static uint16_t pfx_held, pfx_latched;   /* bit i: black key i held (pressed on ARP); latched (SEL + the key) */
static uint8_t pfx_sel, pfx_part;        /* the key the ARP page shows (K1-K4 edit it), its part (MAIN MORE MOD) */
static uint8_t pfx_vox[NPFX];            /* each key's voice in the engine + 1 (0: none) */
static uint8_t pfx_owner[BRK_PAD_V];     /* each voice's key + 1 (0: free) */
static uint32_t pfx_age[BRK_PAD_V];      /* when it was given (a fifth key takes the oldest) */
static uint32_t pfx_on(void) { return (uint32_t)(pfx_held | pfx_latched); }
static uint32_t fxk(void) { return ui.page == PG_ARP ? BRK_FX_SLOTS + pfx_sel % NPFX : ui.fx_focus & 3u; }
static brk_fxslot_t *usl(uint32_t k) { return k < BRK_FX_SLOTS ? &set.fx.slot[k] : &set.pfx[(k - BRK_FX_SLOTS) % NPFX]; }
static brk_fxx_t *uxx(uint32_t k) { return k < BRK_FX_SLOTS ? &set.fxx[k] : &set.pfxx[(k - BRK_FX_SLOTS) % NPFX]; }
static uint32_t eng_slot(uint32_t k)     /* the engine's slot for k: the chain's own, a key's voice (BRK_FX_ALL: none) */
{
    if (k < BRK_FX_SLOTS)
        return k;
    k = pfx_vox[(k - BRK_FX_SLOTS) % NPFX];
    return k ? BRK_FX_SLOTS + k - 1u : BRK_FX_ALL;
}
static uint8_t *fx_pg(void) { return ui.page == PG_ARP ? &pfx_part : &ui.fx_page; }
#define FXPG (*fx_pg())                  /* the shown slot's part: the FX page's, or the ARP page's own */

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
    return slot == fxk() && usl(slot)->type == FXT_MBCOMP &&
           FXPG != MBP_AMOUNT && FXPG < NMBP;
}
static const char *mb_knob_name(int i)
{
    return FXPG == MBP_GAIN ? MBK_GAIN[i & 3] : FXPG == MBP_XOVER ? MBK_XOVER[i & 3] : MBK_BAND[i & 3];
}
static float *mb_knob_val(brk_mbc_t *m, int i)   /* the setting knob i turns (0: SOLO, not a float) */
{
    uint32_t b = (uint32_t)(FXPG - MBP_LOW) % MB_BANDS;
    i &= 3;
    if (FXPG == MBP_GAIN)
        return i < 3 ? &m->gain[i] : 0;
    if (FXPG == MBP_XOVER)
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
    if (FXPG == MBP_GAIN && i == 3) {
        text_cat(t, m->solo && m->solo <= MB_BANDS ? MB_BAND_NAME[m->solo - 1u] : "OFF");
        return;
    }
    v = mb_knob_val(m, i);
    if (FXPG == MBP_GAIN)
        db_text(brk_mb_db24(*v), t);
    else if (FXPG == MBP_XOVER) {
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
    if (FXPG == MBP_GAIN && i == 3)
        m->solo = (uint8_t)clamp((int32_t)m->solo + (d > 0 ? 1 : -1), 0, MB_BANDS);
    else {
        float *v = mb_knob_val(m, i);
        *v = knob_f(*v, d, MB_STEP[FXPG % NMBP][i], 0.0f, 1.0f);
    }
    mb_value(i, t);
    n[0] = 0;
    if (FXPG >= MBP_LOW && FXPG <= MBP_HIGH) {   /* "LOW RATIO" */
        text_cat(n, MB_BAND_NAME[FXPG - MBP_LOW]);
        text_cat(n, " ");
    } else if (FXPG == MBP_GAIN && i < 3)
        text_cat(n, "GAIN ");
    text_cat(n, mb_knob_name(i));
    app_say(n, t);
    ui.redraw |= RG_BLOCKS;
    changed();
}
/* ------------------------------------------- the other effects' MORE and MOD --- */
/* (PRD A-49b) every other effect has three parts: MAIN (its settings, as always), MORE (brk_fxx_t x[0..3]: XK) and
 * MOD (x[4..7]: a modulator on one of its settings). FX again steps them, then on to the next slot */
enum { XP_MAIN, XP_MORE, XP_MOD, NXP };
static const char *const XP_NAME[NXP] = {"MAIN", "MORE", "MOD"};
enum { XU_PCT, XU_HZ, XU_DB, XU_CH, XU_MS, XU_DEG, XU_Q, XU_HZF };
/* each effect's MORE knobs, one after another (XK_AT: where each effect's start; the next one's start ends it): a
 * name, its unit, the choices' count, OFF at an end (1: at 0, 2: at 1), then its range (lo, hi) or its choices */
typedef struct {
    const char *name;
    uint8_t unit, n, off;
    union { float r[2]; const char *const *ch; } u;
} xk_t;
static const char *const CH_SLOPE[2] = {"12 DB", "24 DB"}, *const CH_DSHAPE[4] = {"SOFT", "TUBE", "HARD", "FOLD"};
static const char *const CH_RATIO[8] = {"1.5:1", "2:1", "3:1", "4:1", "6:1", "8:1", "12:1", "20:1"};
static const char *const CH_WAVE[3] = {"SINE", "TRI", "SQUARE"}, *const CH_RMODE[2] = {"RING", "AM"};
static const char *const CH_TSHAPE[4] = {"SINE", "TRI", "SQUARE", "SAW"};
static const xk_t XK[] = {
    {"DRIVE", XU_PCT, 0, 0, {{0}}},
    {"SLOPE", XU_CH, 2, 0, {.ch = CH_SLOPE}},
    {"SPREAD", XU_PCT, 0, 0, {{0}}},
    {"TONE", XU_HZ, 0, 2, {.r = {1000.0f, 20000.0f}}},
    {"DITHER", XU_PCT, 0, 0, {{0}}},
    {"SHAPE", XU_CH, 4, 0, {.ch = CH_DSHAPE}},
    {"BIAS", XU_PCT, 0, 0, {{0}}},
    {"LOW CUT", XU_HZ, 0, 1, {.r = {20.0f, 800.0f}}},
    {"RATIO", XU_CH, 8, 0, {.ch = CH_RATIO}},
    {"MAKE-UP", XU_DB, 0, 0, {.r = {0.0f, 12.0f}}},
    {"SC HPF", XU_HZ, 0, 1, {.r = {20.0f, 300.0f}}},
    {"DELAY", XU_MS, 0, 0, {.r = {0.2f, 2.7f}}},
    {"SPREAD", XU_DEG, 0, 0, {.r = {0.0f, 180.0f}}},
    {"SYNC", XU_CH, BRK_FLSYNCS, 0, {.ch = BRK_FLSYNC_NAME}},
    {"WAVE", XU_CH, 3, 0, {.ch = CH_WAVE}},
    {"MODE", XU_CH, 2, 0, {.ch = CH_RMODE}},
    {"SHAPE", XU_CH, 4, 0, {.ch = CH_TSHAPE}},
    {"SMOOTH", XU_PCT, 0, 0, {{0}}},
    {"WIDTH", XU_PCT, 0, 0, {{0}}},
    {"LOW CUT", XU_HZ, 0, 1, {.r = {20.0f, 600.0f}}},
    {"MOD", XU_PCT, 0, 0, {{0}}},
    {"DIFFUSE", XU_PCT, 0, 0, {{0}}},
    {"WIDTH", XU_PCT, 0, 0, {{0}}},
    {"LOW CUT", XU_HZ, 0, 1, {.r = {20.0f, 1000.0f}}},
    {"DUCK", XU_PCT, 0, 0, {{0}}},
    {"LOW F", XU_HZ, 0, 0, {.r = {40.0f, 400.0f}}},
    {"MID Q", XU_Q, 0, 0, {.r = {0.3f, 4.0f}}},
    {"HIGH F", XU_HZ, 0, 0, {.r = {2000.0f, 16000.0f}}},
    {"LEVEL", XU_DB, 0, 0, {.r = {-12.0f, 12.0f}}},
    {"FLUTTER", XU_PCT, 0, 0, {{0}}},
    {"WOW RATE", XU_HZF, 0, 0, {.r = {0.1f, 3.0f}}},
    {"HISS", XU_PCT, 0, 0, {{0}}},
    {"BIAS", XU_PCT, 0, 0, {{0}}},
    {"OUTPUT", XU_DB, 0, 0, {.r = {-12.0f, 0.0f}}},
};
static const uint8_t XK_AT[BRK_NFXT + 1] = {0, 0, 3, 5, 8, 11, 14, 16, 18, 22, 25, 29, 32, 32, 34};
static const xk_t *xk(uint32_t ty, int i)   /* effect ty's MORE knob i, 0 for none */
{
    uint32_t a = XK_AT[ty % BRK_NFXT] + (uint32_t)(i & 3);
    return a < XK_AT[ty % BRK_NFXT + 1u] ? &XK[a] : 0;
}
static const char *const MODK[4] = {"RATE", "DEPTH", "SHAPE", "TARGET"};

static uint32_t fx_npages(uint32_t t) { return t == FXT_EMPTY ? 1u : t == FXT_MBCOMP ? NMBP : NXP; }
static brk_fxx_t *fxx_of(uint32_t k)     /* slot k's extras, its effect's defaults if they were another's */
{
    brk_fxx_t *x = uxx(k);
    if (x->type != usl(k)->type)
        brk_fxx_defaults(x, usl(k)->type);
    return x;
}
static int x_detail(uint32_t slot)       /* the focused slot (not an MB COMP) on MORE or MOD */
{
    uint32_t t = usl(slot)->type;
    return slot == fxk() && t != FXT_MBCOMP && t != FXT_EMPTY && FXPG && FXPG < NXP;
}
static const char *fx_part_name(void)    /* the focused slot's part past its first, for the tabs ("" on the first) */
{
    uint32_t t = usl(fxk())->type;
    return !FXPG ? "" : t == FXT_MBCOMP ? MBP_NAME[FXPG % NMBP] : XP_NAME[FXPG % NXP];
}
static const char *base_param_name(uint32_t slot, int i);
static const char *x_name(int i)
{
    const xk_t *k = xk(usl(fxk())->type, i);
    return FXPG == XP_MOD ? MODK[i & 3] : k ? k->name : "";
}
static void x_value(int i, char *t)
{
    uint32_t ty = usl(fxk())->type % BRK_NFXT;
    const brk_fxx_t *x = fxx_of(fxk());
    const xk_t *k = xk(ty, i);
    float v = x->x[(i & 3) + (FXPG == XP_MOD ? 4 : 0)], f, lo, hi;
    t[0] = 0;
    if (FXPG == XP_MOD) {
        i &= 3;
        if (i == 0)
            text_cat(t, BRK_MOD_DIV_NAME[brk_idx(v, BRK_MOD_DIVS)]);
        else if (i == 2)
            text_cat(t, BRK_MOD_SHAPE_NAME[brk_idx(v, BRK_MOD_SHAPES)]);
        else if (i == 3)
            text_cat(t, base_param_name(fxk(), (int)brk_idx(v, 4u)));
        else if (v > 0.49f && v < 0.51f)
            text_cat(t, "OFF");
        else {
            if (v > 0.5f)
                text_cat(t, "+");
            pct_text((v - 0.5f) * 2.0f, t + (v > 0.5f));
        }
        return;
    }
    if (!k)
        return;
    if ((k->off == 1u && v < 0.005f) || (k->off == 2u && v > 0.99f)) {
        text_cat(t, "OFF");
        return;
    }
    lo = k->u.r[0];
    hi = k->u.r[1];
    f = lo + (hi - lo) * v;              /* (linear; the log ones below) */
    switch (k->unit) {
    case XU_HZ: hz_text(lo * brk_exp2f(v * brk_log2f(hi / lo)), t); break;
    case XU_HZF: hundredths_text(lo * brk_exp2f(v * brk_log2f(hi / lo)), t); text_cat(t, "HZ"); break;
    case XU_Q: text_cat(t, "Q "); hundredths_text(lo * brk_exp2f(v * brk_log2f(hi / lo)), t + 2); break;
    case XU_DB: db_text(f, t); break;
    case XU_CH: text_cat(t, k->u.ch[brk_idx(v, k->n)]); break;
    case XU_MS: ms_text(f, t); break;
    case XU_DEG: int_text((int32_t)(f + 0.5f), t); text_cat(t, " DEG"); break;
    default: pct_text(v, t); break;
    }
}
static void x_knob_turn(int i, int32_t d)
{
    uint32_t ty = usl(fxk())->type % BRK_NFXT, j = (uint32_t)(i & 3) + (FXPG == XP_MOD ? 4u : 0u);
    brk_fxx_t *x = fxx_of(fxk());
    const xk_t *k = xk(ty, i);
    static const uint8_t MODN[4] = {BRK_MOD_DIVS, 0, BRK_MOD_SHAPES, 4};
    uint32_t n = FXPG == XP_MOD ? MODN[i & 3] : k && k->unit == XU_CH ? k->n : 0u;
    char t[16];
    if (FXPG == XP_MORE && !k)
        return;
    x->x[j] = knob_f(x->x[j], d, n ? 1.0f / (float)(n - 1u) : 0.02f, 0.0f, 1.0f);
    x_value(i, t);
    app_say(x_name(i), t);
    ui.redraw |= RG_BLOCKS;
    changed();
}

/* FX again on the FX page: the focused slot's next part, past its last the next slot */
static void fx_algo(int32_t d, uint32_t sel);
static void fx_next_part(void)
{
    if (FXPG + 1u < fx_npages(usl(fxk())->type)) {
        FXPG++;
        page_say();
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    fx_algo(1, 0u);
}

static const char *fx_param_name(uint32_t slot, int i)
{
    if (mb_detail(slot))
        return mb_knob_name(i);
    if (x_detail(slot))
        return x_name(i);
    return base_param_name(slot, i);
}
static const char *base_param_name(uint32_t slot, int i)   /* its MAIN part's */
{
    const brk_fxslot_t *sl = usl(slot);
    if (i == 3)
        return sl->type == FXT_EMPTY ? "" : sl->type == FXT_DELAY ? "SEND" : sl->type == FXT_EQ ? "FREQ" : "MIX";
    return FXP_NAME[sl->type < BRK_NFXT ? sl->type : 0][i & 3];
}

static void fx_value(uint32_t slot, int i, char *t)
{
    const brk_fxslot_t *sl = usl(slot);
    float v = i == 3 ? sl->mix : sl->p[i & 3];
    t[0] = 0;
    if (mb_detail(slot)) {
        mb_value(i, t);
        return;
    }
    if (x_detail(slot)) {
        x_value(i, t);
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

static uint8_t cc_sent;                  /* this turn went out as a chain slot's CC */
static void fx_knob_turn(uint32_t slot, int i, int32_t d)
{
    brk_fxslot_t *sl = usl(slot);
    char t[16];
    float step = 0.02f;
    if (i >= 0 && i <= 3 && mb_detail(slot)) {
        mb_knob_turn(i, d);
        return;
    }
    if (i >= 0 && i <= 3 && x_detail(slot)) {
        x_knob_turn(i, d);
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
    if (slot < BRK_FX_SLOTS) {           /* a chain slot's: its CC out (24 + 4 x slot + knob), not K's */
        midi_send3(0xB0u, CC_FX0 + slot * 4u + (uint32_t)i,
                   (uint32_t)((i == 3 ? sl->mix : sl->p[i]) * 127.0f + 0.5f), MO_CC);
        cc_sent = 1u;
    }
    fx_value(slot, i, t);
    app_say(fx_param_name(slot, i), t);
    ui.redraw |= RG_BLOCKS;
    changed();
}

/* CC 24..39 in: chain slot i / 4's setting i % 4 (SET 1..3, MIX) to v / 127; applied and shown at the next frame */
static uint8_t fx_cc_at;                 /* 1 + the last one come in (0: none since the frame) */
static void midi_fx_cc(uint32_t i, uint32_t v)
{
    brk_fxslot_t *sl = &set.fx.slot[i / 4u];
    float f = (float)v * (1.0f / 127.0f);
    if (sl->type == FXT_EMPTY)
        return;
    if (i % 4u == 3u)
        sl->mix = f;
    else
        sl->p[i % 4u] = f;
    fx_cc_at = (uint8_t)(i + 1u);
}
static void fx_cc_frame(void)            /* (input, each frame) */
{
    char t[16];
    uint32_t i = fx_cc_at - 1u;
    if (!fx_cc_at)
        return;
    fx_cc_at = 0u;
    pct_text(i % 4u == 3u ? set.fx.slot[i / 4u].mix : set.fx.slot[i / 4u].p[i % 4u], t);
    app_say(base_param_name(i / 4u, (int)(i % 4u)), t);
    ui.redraw |= RG_BLOCKS;
    changed();
}

/* PRESETS on the FX page: the focused slot's effect, through the library (EMPTY first), with its own starting
 * settings, on at once */
static void fx_type_turn(int32_t d)
{
    uint32_t k = fxk(), pad = k >= BRK_FX_SLOTS;
    brk_fxslot_t *sl = usl(k);
    int32_t t = clamp((int32_t)sl->type + (d > 0 ? 1 : -1), 0, BRK_NFXT - 1);
    char n[16];
    if (pad && t == FXT_MBCOMP)          /* (MB COMP is the chain's alone) */
        t = clamp(t + (d > 0 ? 1 : -1), 0, BRK_NFXT - 1);
    if ((uint32_t)t == sl->type)
        return;
    brk_fx_slot_defaults(sl, (int)t);
    sl->on = (uint8_t)(t != FXT_EMPTY);
    if (t == FXT_MBCOMP)                 /* an MB COMP put in: its detail from the start, at its AMOUNT */
        brk_mbc_defaults(&set.mbc[k & 3u]);
    brk_fxx_defaults(uxx(k), (int)t);    /* its extras: the new effect's */
    FXPG = 0u;
    n[0] = 0;
    text_cat(n, pad ? "PAD " : "SLOT ");
    int_text((int32_t)(pad ? k - BRK_FX_SLOTS : k) + 1, n + (pad ? 4 : 5));
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
        set.mbc[a] = set.mbc[b];         /* (an MB COMP's detail goes with it, and the extras) */
        set.mbc[b] = tm;
        {
            brk_fxx_t tx = set.fxx[a];
            set.fxx[a] = set.fxx[b];
            set.fxx[b] = tx;
        }
        changed();
    } else
        FXPG = 0u;                 /* another slot: from its first part */
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
    const brk_fxslot_t *sl = usl(slot);
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

/* a row of chips along the top of a strip (the one chosen lit), and a word at the right */
static void fx_chips(const char *const *nm, uint32_t n, uint32_t sel, int32_t w, const char *st, uint16_t stc)
{
    uint32_t b;
    for (b = 0; b < n; b++) {
        int32_t cx = 2 + (int32_t)b * (w + 2), on = b == sel;
        cv_rect(cx, 0, w, 11, on ? C_HI : C_BLOCK);
        cv_text(cx + (w - text_w(&FONT_T, nm[b])) / 2, 0, &FONT_T, nm[b], on ? C_BLACK : C_GRAY);
    }
    if (st)
        cv_text(237 - text_w(&FONT_T, st), 0, &FONT_T, st, stc);
}

static const char *const SLOT_NUM[4] = {"1", "2", "3", "4"};   /* (the FX page's chain, the tabs: main.c) */

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
    int32_t x, xlo, xhi, py = 0, focus = FXPG >= MBP_LOW && FXPG <= MBP_HIGH ? FXPG - MBP_LOW : -1;
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
    fx_chips(MBP_TAB, NMBP, FXPG, 30, !sl->on ? "BYPASS" : solo ? "SOLO" : "ON",
             !sl->on ? C_DIM : solo ? MB_COL[solo - 1u] : C_HI);
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
        uint16_t c = FXPG == MBP_XOVER ? C_WHITE : C_GRAY;
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

/* the focused effect's picture (PRD A-49b), column x of 240: what comes out (-1..1, up), *in what went in (a test tone,
 * dim; below -1.5: none), *base where the fill starts. p: its settings as modulated now, xx its extras. The shapers
 * (DRIVE CLIP TAPE CRUNCH PUNCH) and RING / TREMOLO show a tone through them; FILTER FLANGER their response, 20 Hz ..
 * 20 kHz, +12 .. -36 dB (EQ +-12 dB about the middle); REVERB its decay over 3 s; DELAY its echoes (left up, right
 * down, each its feedback quieter) */
static float x_plot(uint32_t t, int32_t x, const float *p, const float *xx, uint32_t now, float *in, float *base)
{
    float u = (float)x * (1.0f / 240.0f), w = brk_sin_turns(2.0f * u), f = 20.0f * brk_exp2f(u * 9.9658f), g, r, db;
    *in = 0.7f * w;
    *base = 0.0f;
    switch (t) {
    case FXT_FILTER: {                   /* the SVF's |H|^2 (analog): LP 1, BP (k w)^2, HP w^4 over the denominator */
        float wf = f / (30.0f * brk_exp2f(p[0] * 9.06f)), k = 2.0f - 1.9f * p[1], w2 = wf * wf;
        uint32_t mode = p[2] < 0.33f ? 0u : p[2] < 0.67f ? 1u : 2u;
        r = (mode == 0u ? 1.0f : mode == 1u ? k * k * w2 : w2 * w2) / ((1.0f - w2) * (1.0f - w2) + k * k * w2);
        if (xx[1] >= 0.5f)
            r *= r;
        db = 3.0103f * brk_log2f(r + 1e-9f);
        break;
    }
    case FXT_EQ: {                       /* (drawn as smooth bands: shelves and a bell in dB) */
        float fl = 40.0f * brk_exp2f(xx[0] * 3.3219f), fh = 2000.0f * brk_exp2f(xx[2] * 3.0f);
        float fm = 200.0f * brk_exp2f(p[3] * 4.64f), q = 0.3f * brk_exp2f(xx[1] * 3.737f), b = f / fm - fm / f;
        db = (p[0] - 0.5f) * 24.0f / (1.0f + f * f / (fl * fl)) + (p[2] - 0.5f) * 24.0f / (1.0f + fh * fh / (f * f)) +
             (p[1] - 0.5f) * 24.0f / (1.0f + q * q * b * b) + (xx[3] - 0.5f) * 24.0f;
        *in = -2.0f;
        return db * (1.0f / 12.0f);      /* (+-12 dB about the middle) */
    }
    case FXT_FLANGER: {                  /* 0.5 (1 + its comb) at the delay now (the sweep: its rate, on the clock) */
        float d = 9.0f + 111.0f * xx[0] + (0.2f + 4.8f * p[1]) * 44.1f * (0.5f + 0.5f *
                  brk_sin_turns((float)now * 0.001f * 0.02f * brk_exp2f(p[0] * 9.0f)));
        float ph = f * d / (float)BRK_SR, c = brk_sin_turns(ph + 0.25f), sn = brk_sin_turns(ph);
        float fb = (2.0f * p[2] - 1.0f) * 0.9f, dd = 1.0f - 2.0f * fb * c + fb * fb;
        float re = 1.0f + 0.5f * ((1.0f + (c - fb) / dd) - 2.0f) * p[3], im = -0.5f * sn / dd * p[3];
        db = 3.0103f * brk_log2f(re * re + im * im + 1e-9f);
        break;
    }
    case FXT_REVERB: {                   /* silence for the pre-delay, then the tank's loop (~0.52 s) by its decay:
                                          * 0 .. -60 dB, top to bottom */
        float tt = u * 3.0f - p[2] * 0.06f;
        *in = -2.0f;
        *base = -1.0f;
        return tt < 0.0f ? -1.0f : 1.0f + 6.0206f * brk_log2f(0.25f + 0.72f * p[0]) * tt * 1.923f * (1.0f / 30.0f);
    }
    case FXT_DELAY: {                    /* eight echoes, each its feedback quieter: left, right, left ... */
        int32_t n = x / 26;
        *in = -2.0f;
        if (x % 26 > 3 || n < 1)
            return 0.0f;
        r = 0.95f * brk_exp2f(brk_log2f(p[1] * 0.95f + 0.001f) * (float)(n - 1));
        return n & 1 ? r : -r * xx[0];
    }
    case FXT_RING:                       /* the tone times the carrier (its wave; AM lifted) */
        g = brk_mod_wave(brk_idx(xx[0], 3u) == 1u ? MOD_TRI : brk_idx(xx[0], 3u) ? MOD_SQUARE : MOD_SINE,
                         u * (2.0f + 30.0f * p[0]));
        return *in * (xx[1] >= 0.5f ? 0.5f + 0.5f * g : g);
    case FXT_TREMOLO: {                  /* a tone under two of its cycles */
        static const uint8_t SH[4] = {MOD_SINE, MOD_TRI, MOD_SQUARE, MOD_SAW};
        *in = 0.9f * brk_sin_turns(24.0f * u);
        return *in * (1.0f - p[1] * (0.5f + 0.5f * brk_mod_wave(SH[brk_idx(xx[0], 4u)], 2.0f * u)));
    }
    case FXT_DRIVE:
        return brk_drive_curve(brk_idx(xx[0], 4u), *in * (1.0f + 29.0f * p[0] * p[0]), 0.6f * xx[1]) *
               (0.25f + 0.75f * p[2]) * (1.0f + 0.3f * p[0]);
    case FXT_CLIP:
        g = brk_exp2f(-p[2] * 1.993f);   /* the ceiling */
        return (brk_clip_curve(*in * brk_exp2f(p[0] * 3.986f) / g + 0.5f * xx[0], p[1]) -
                brk_clip_curve(0.5f * xx[0], p[1])) * g * brk_exp2f((xx[1] - 1.0f) * 1.993f);
    case FXT_TAPE:
        g = 0.3f * brk_exp2f(p[0] * 3.986f);
        return (brk_soft(*in * g + 0.12f) - brk_soft(0.12f)) * 0.9f / (brk_soft(g * 0.9f + 0.12f) - brk_soft(0.12f));
    case FXT_CRUNCH: {                   /* held at its rate (the steps drawn 8x wider), fewer bits */
        float lv = brk_exp2f(15.0f - 14.0f * p[0]), st = 8.0f * brk_exp2f(p[1] * 5.46f) / 240.0f;
        *in = 0.7f * brk_sin_turns(2.0f * (float)(int32_t)(u / st) * st);
        return (float)(int32_t)(brk_soft(*in * (1.0f + 8.0f * p[2])) * lv) / lv;
    }
    case FXT_PUNCH: {                    /* two hits: the peaks over its threshold held down */
        float e = 1.0f - (float)(x % 120) * (1.0f / 120.0f), th = brk_exp2f(-p[0] * 5.0f);
        e = e * e * e * 0.95f;
        *in = e * brk_sin_turns(6.0f * u);
        g = BRK_PUNCH_RATIO[brk_idx(xx[0], BRK_PUNCH_RATIOS)];
        return *in * (e > th ? (th + (e - th) / g) / e : 1.0f) / (th + (1.0f - th) / g) * 0.7f;
    }
    default:
        return 0.0f;
    }
    *in = -2.0f;                         /* (the responses: dB from the floor) */
    *base = -1.0f;
    return (db + 12.0f) * (1.0f / 24.0f);
}

/* a slot focused (not an MB COMP): above, its parts and its picture; below, the chain (the slot lit), its modulator's
 * cycle (where it is now, what it moves) and its level in and out */
static void draw_xview(uint32_t now)
{
    uint32_t k = fxk(), t = usl(k)->type, i, shape, pad = k >= BRK_FX_SLOTS, live;
    const brk_fxslot_t *sl = usl(k);
    const float *xx = fxx_of(k)->x;
    brk_fx_meter_t m;
    float p[4], depth = (xx[FXX_DEPTH] - 0.5f) * 2.0f;
    int32_t x, py = 0, y, yb;
    uint16_t col = sl->on ? MB_COL[1] : C_GRAY;
    char nm[16];
    brk_fx_meter(eng_slot(k), &m);
    live = pad ? eng_slot(k) < BRK_FX_ALL : sl->on;   /* (a key: while its voice sounds) */
    for (i = 0; i < 4u; i++)             /* the settings as the modulator leaves them (as set while it is off) */
        p[i] = live ? m.p[i] : i < 3u ? sl->p[i] : sl->mix;
    cv_begin(240, 52, C_BLACK);
    if (pad) {                           /* a key: held, latched or waiting */
        uint32_t b = pfx_on() >> (k - BRK_FX_SLOTS) & 1u, h = pfx_latched >> (k - BRK_FX_SLOTS) & 1u;
        fx_chips(XP_NAME, NXP, FXPG, 40, h ? "HOLD" : b ? "ON" : "OFF", h ? C_AMB : b ? C_HI : C_DIM);
    } else
        fx_chips(XP_NAME, NXP, FXPG, 40, sl->on ? "ON" : "BYPASS", sl->on ? C_HI : C_DIM);
    for (x = 0; x < 240; x += 4)         /* the middle (the tones), 0 dB (the responses) */
        cv_rect(x, 32, 2, 1, C_LINE);
    if (t == FXT_EMPTY)
        cv_text(6, 26, &FONT_T, "EMPTY: PRESETS PUTS AN EFFECT HERE", C_GRAY);
    for (x = 0; t != FXT_EMPTY && x < 240; x++) {
        float in, base, v = x_plot(t, x, p, xx, now, &in, &base);
        y = 32 - (int32_t)(brk_clampf(v, -1.0f, 1.0f) * 19.0f);
        yb = 32 - (int32_t)(base * 19.0f);
        cv_rect(x, y < yb ? y : yb, 1, y < yb ? yb - y : y - yb, sl->on ? MB_DIM[1] : C_BLOCK);
        if (in > -1.5f)
            cv_rect(x, 32 - (int32_t)(brk_clampf(in, -1.0f, 1.0f) * 19.0f), 1, 1, C_GRAY);
        if (x)
            cv_line(x - 1, py, x, y, col);
        py = y;
    }
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    if (pad) {                           /* a key: which one, and how it holds */
        nm[0] = 0;
        text_cat(nm, "PAD ");
        int_text((int32_t)(k - BRK_FX_SLOTS) + 1, nm + 4);
        cv_rect(2, 0, 56, 11, C_HI);
        cv_text(5, 0, &FONT_T, nm, C_BLACK);
        cv_text(66, 0, &FONT_T, "SEL + KEY: HOLD", C_GRAY);
    }
    for (i = 0; !pad && i < 4u; i++) {   /* the chain: each slot's effect, this one lit */
        const brk_fxslot_t *o = &set.fx.slot[i];
        nm[0] = 0;
        text_cat(nm, SLOT_NUM[i]);
        text_cat(nm, " ");
        text_cat(nm, o->type ? BRK_FXT_NAME[o->type % BRK_NFXT] : "-");
        cv_rect(2 + (int32_t)i * 60, 0, 56, 11, i == k ? C_HI : C_BLOCK);
        cv_text(5 + (int32_t)i * 60, 0, &FONT_T, nm, i == k ? C_BLACK : o->on ? C_GRAY : C_DIM);
    }
    cv_rect(0, 14, 176, 38, C_BLOCK);    /* the modulator: its cycle, where it is, what it moves */
    shape = brk_idx(xx[FXX_SHAPE], BRK_MOD_SHAPES);
    if (t == FXT_EMPTY || (depth > -0.01f && depth < 0.01f))
        cv_text(6, 26, &FONT_T, pad ? "MOD OFF  (ARP AGAIN: MOD)" : "MOD OFF  (FX AGAIN: MOD)", C_DIM);
    else {
        for (x = 0; shape < MOD_SH && x < 176; x++) {
            y = 33 - (int32_t)(brk_mod_wave(shape, (float)x * (1.0f / 176.0f)) * 14.0f);
            if (x)
                cv_line(x - 1, py, x, y, MB_DIM[1]);
            py = y;
        }
        x = shape < MOD_SH ? (int32_t)(m.ph * 176.0f) : 88;
        y = 33 - (int32_t)((shape == MOD_ENV ? 2.0f * m.mod - 1.0f : m.mod) * 14.0f);
        cv_rect(x, 14, 1, 38, C_GRAY);
        cv_rect(x - 2, y - 2, 5, 5, C_WHITE);
        nm[0] = 0;
        text_cat(nm, "> ");
        text_cat(nm, base_param_name(k, (int)brk_idx(xx[FXX_TARGET], 4u)));
        cv_text(4, 14, &FONT_T, nm, MB_COL[1]);
    }
    for (i = 0; i < 2u; i++) {           /* its level in and out (-48 .. 0 dB) */
        float lv = i ? m.out : m.in;
        int32_t h = lv > 0.0039f ? (int32_t)((6.0206f * brk_log2f(lv) + 48.0f) * (26.0f / 48.0f)) : 0;
        h = h > 26 ? 26 : h;             /* (over 0 dB: full) */
        cv_rect(186 + (int32_t)i * 28, 14, 12, 26, C_BLOCK);
        cv_rect(186 + (int32_t)i * 28, 40 - h, 12, h, i ? col : C_GRAY);
        cv_text(184 + (int32_t)i * 26, 40, &FONT_T, i ? "OUT" : "IN", C_GRAY);
    }
    cv_blit(0, 140);
}

/* the FX page: the focused slot's view (an MB COMP its own) */
static void draw_chain(void)
{
    if (usl(fxk())->type == FXT_MBCOMP)
        draw_mbcomp(plat_ms());
    else
        draw_xview(plat_ms());
}
