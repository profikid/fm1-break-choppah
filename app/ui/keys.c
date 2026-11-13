/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the white keys: each key's loop and slice effect (K1-K4 while held), and the held key's view. (Part of
 * app.c's unity: included there in order.) */

/* ------------------------------------------------- the white keys' own loops --- */
static const char *const KEY_KNOB[4] = {"LENGTH", "PITCH", "START", "MODE"};
static const char *const SLX_KNOB[4] = {"EFFECT", "AMOUNT", "TONE", "LEVEL"};
static const char *const SLX_TAG[BRK_SLX] = {"", "LP", "HP", "CR", "DR", "RM", "EC", "VB"};

/* a key's other layer (a key held, FX): its slice's own effect, amount, tone (the filters' resonance, RING's mix)
 * and level */
static void slx_knob_value(int key, int k, char *t)
{
    const brk_slicefx_t *f = &set.slx[key & 15];
    t[0] = 0;
    if (k == 0)
        text_cat(t, BRK_SLX_NAME[f->type < BRK_SLX ? f->type : 0]);
    else if (k == 1)
        pct_text(f->type == SLX_OFF ? 0.0f : (float)f->amount * 0.01f, t);
    else if (k == 2) {
        if (f->type == SLX_LOPASS || f->type == SLX_HIPASS || f->type == SLX_RING)
            pct_text((float)f->tone * 0.01f, t);
        else
            text_cat(t, "-");
    } else
        pct_text((float)f->level * 0.01f, t);
}

static void slx_knob_turn(int key, int k, int32_t d)
{
    brk_slicefx_t *f = &set.slx[key & 15];
    char t[16];
    if (k == 0)
        f->type = (uint8_t)clamp((int32_t)f->type + (d > 0 ? 1 : -1), 0, BRK_SLX - 1);
    else if (k == 1)
        f->amount = (uint8_t)clamp((int32_t)f->amount + d * 2, 0, 100);
    else if (k == 2)
        f->tone = (uint8_t)clamp((int32_t)f->tone + d * 2, 0, 100);
    else if (k == 3)
        f->level = (uint8_t)clamp((int32_t)f->level + d * 2, 0, 100);
    else
        return;
    ui.key_turned = 1u;
    slx_knob_value(key, k, t);
    app_say(SLX_KNOB[k], t);
    changed();
}
#define KEY_SHOW_MS 160u                 /* a key held this long (or a knob turned) takes the strip: taps don't */

/* the key K1-K4 tune: the white key held, unless a pad was pressed after it */
static int key_tuned(void)
{
    if (ui.key_focus < 0 || !slices_page())
        return -1;
    return ui.pad_focus >= 0 && ui.pad_since > ui.key_since ? -1 : ui.key_focus;
}

static float key_sps(const brk_break_t *b)   /* 16ths in a slice of this break */
{
    return b && b->nslices ? (float)(b->bars * BRK_STEPS_PER_BAR) / (float)b->nslices : 1.0f;
}

/* a length in 16ths as music: 0.5 -> "1/32", 2 -> "1/8", 16 -> "1 BAR", 24 -> "1.5 BAR" */
static void sixteenths_text(float steps, char *t)
{
    static const uint32_t DEN[7] = {2u, 4u, 8u, 16u, 32u, 64u, 128u};
    uint32_t i;
    t[0] = 0;
    if (steps >= 16.0f) {
        tenths_text((int32_t)(steps / 16.0f * 10.0f + 0.5f), t);
        if (t[1] == '.' && t[2] == '0')
            t[1] = 0;                    /* "1.0" -> "1" */
        else if (t[2] == '.' && t[3] == '0')
            t[2] = 0;
        text_cat(t, " BAR");
        return;
    }
    for (i = 0; i < 7u; i++) {           /* the smallest denominator it is a whole number of */
        float n = steps * (float)DEN[i] / 16.0f;
        if (n - (float)(int32_t)(n + 0.001f) < 0.01f && n >= 0.99f) {
            char d[8];
            int_text((int32_t)(n + 0.5f), t);
            text_cat(t, "/");
            int_text((int32_t)DEN[i], d);
            text_cat(t, d);
            return;
        }
    }
    text_cat(t, "1/128");
}

static void key_knob_value(int key, int k, char *t)
{
    const brk_keyset_t *ks = &set.keys[key & 15];
    t[0] = 0;
    if (k == 0)
        sixteenths_text(BRK_KEY_SLICES[ks->len % BRK_KEY_LENS] * key_sps(brk_get_break()), t);
    else if (k == 1) {
        t[0] = ks->pitch > 0 ? '+' : 0;
        t[1] = 0;
        int_text(ks->pitch, t + (ks->pitch > 0));
        text_cat(t, " ST");
    } else if (k == 2)
        pct_text((float)ks->start / 16.0f, t);
    else
        text_cat(t, BRK_KEY_MODE_NAME[ks->mode % BRK_KEY_MODES]);
}

static void key_knob_turn(int key, int k, int32_t d)
{
    brk_keyset_t *ks = &set.keys[key & 15];
    char t[16], n[16];
    if (ui.key_layer) {                  /* the slice's effect layer */
        slx_knob_turn(key, k, d);
        return;
    }
    if (k == 0) {                        /* LENGTH: shortest to longest (3/16 after 1/8, 3/8 after 1/4) */
        int32_t o = 0;
        while (o < (int32_t)BRK_KEY_LENS - 1 && BRK_KEY_ORDER[o] != ks->len)
            o++;
        ks->len = BRK_KEY_ORDER[clamp(o + d, 0, BRK_KEY_LENS - 1)];
    }
    else if (k == 1)
        ks->pitch = (int8_t)clamp((int32_t)ks->pitch + d, -12, 12);
    else if (k == 2)
        ks->start = (uint8_t)clamp((int32_t)ks->start + d, 0, 15);
    else if (k == 3)
        ks->mode = (uint8_t)clamp((int32_t)ks->mode + d, 0, BRK_KEY_MODES - 1);
    else
        return;
    ui.key_turned = 1u;
    key_knob_value(key, k, t);
    n[0] = 0;
    text_cat(n, KEY_KNOB[k]);
    app_say(n, t);
    changed();
}

static int key_custom(int key)           /* the key has settings of its own (a dot on its block) */
{
    const brk_keyset_t *ks = &set.keys[key & 15];
    return ks->len != 3u || ks->pitch || ks->start || ks->mode != KEY_LOOP;
}

/* the held key's view: its loop window zoomed to the strip's width, the part this repeat has played lit, the
 * playhead, the slice lines inside it, and a counter of repeats along the bottom */
#define KW_X 4
#define KW_N 232                         /* columns of waveform */
static uint8_t key_px[KW_N];             /* the window's shape (cached: it changes with the key or its settings) */
static uint32_t key_px_sig;

static void key_window(const brk_break_t *b, int key, float *lo, float *w)   /* (as the engine's key_begin) */
{
    const brk_keyset_t *ks = &set.keys[key & 15];
    uint32_t s = (uint32_t)key < b->nslices ? (uint32_t)key : 0u;
    *lo = (float)b->slice[s] + (float)(ks->start & 15u) / 16.0f * (float)(b->slice[s + 1] - b->slice[s]);
    *w = BRK_KEY_SLICES[ks->len % BRK_KEY_LENS] * key_sps(b) * (float)b->len / (float)(b->bars * BRK_STEPS_PER_BAR);
    if (*w < 32.0f)
        *w = 32.0f;
}

static void draw_key_band(int key, const brk_status_t *st, uint32_t now, int32_t top)
{
    const brk_break_t *b = brk_get_break();
    const brk_keyset_t *ks = &set.keys[key & 15];
    char t[24], v[16];
    float lo, w, ph;
    uint32_t c, sig, len;
    int32_t px = -1, x, live;
    (void)now;
    cv_begin(240, FX_BAND, C_BLACK);
    cv_oy = -top;                        /* (this band's rows of the picture) */
    if (!b || !b->len || !b->pcm) {
        cv_oy = 0;
        cv_blit(0, FX_Y + (uint32_t)top);
        return;
    }
    len = b->len;
    key_window(b, key, &lo, &w);
    sig = (uint32_t)lo ^ (uint32_t)w << 9 ^ len << 3 ^ (uint32_t)key << 27;
    if (sig != key_px_sig) {             /* the window's shape: the peak of each column (wrapping round the end) */
        key_px_sig = sig;
        for (c = 0; c < KW_N; c++) {
            uint32_t i0 = (uint32_t)(lo + w * (float)c / (float)KW_N);
            uint32_t i1 = (uint32_t)(lo + w * (float)(c + 1u) / (float)KW_N);
            uint32_t i, stp = (i1 - i0) / 24u + 1u, pk = 0u;
            for (i = i0; i <= i1; i += stp) {
                int32_t a = b->pcm[i % len];
                uint32_t m = (uint32_t)(a < 0 ? -a : a);
                if (m > pk)
                    pk = m;
            }
            key_px[c] = (uint8_t)(pk * 28u / 32768u);
        }
    }
    live = st->key == key;
    ph = live ? st->key_phase : 0.0f;
    if (live) {                          /* the playhead, while it is inside the window */
        float rel = st->pos - lo;
        if (rel < 0.0f)
            rel += (float)len;
        if (rel >= 0.0f && rel < w)
            px = (int32_t)(rel / w * (float)KW_N);
    }
    /* the title: KEY 5, its length and mode; on the right its pitch */
    t[0] = 0;
    text_cat(t, "KEY ");
    int_text(key + 1, v);
    text_cat(t, v);
    text_cat(t, " ");
    if (ui.key_layer) {                  /* the effect layer: the slice's effect */
        const brk_slicefx_t *sf = &set.slx[key & 15];
        text_cat(t, "FX");
        x = cv_text(6, 0, &FONT_B, t, C_HI);
        t[0] = 0;
        text_cat(t, BRK_SLX_NAME[sf->type < BRK_SLX ? sf->type : 0]);
        if (sf->level < 100u) {
            text_cat(t, "  LEVEL ");
            pct_text((float)sf->level * 0.01f, v);
            text_cat(t, v);
        }
        cv_text(x + 8, 4, &FONT_XS, t, sf->type ? C_AMB : C_GRAY);
    } else {
        sixteenths_text(BRK_KEY_SLICES[ks->len % BRK_KEY_LENS] * key_sps(b), v);
        text_cat(t, v);
        x = cv_text(6, 0, &FONT_B, t, C_HI);
        cv_text(x + 8, 4, &FONT_XS, BRK_KEY_MODE_NAME[ks->mode % BRK_KEY_MODES], C_GRAY);
    }
    if (ks->pitch) {
        key_knob_value(key, 1, v);
        cv_text(234 - text_w(&FONT_T, v), 2, &FONT_T, v, C_AMB);
    }
    /* the window: played this repeat lit (from the right when backwards), the rest dim */
    for (c = 0; c < KW_N; c++) {
        int32_t h = key_px[c];
        int lit = live && (st->key_rev ? (int32_t)c >= px : (int32_t)c <= px) && px >= 0;
        cv_rect(KW_X + (int32_t)c, FX_MID - 4 - h, 1, 2 * h + 1, lit ? C_HI : C_DIM);
    }
    for (c = 1; c <= b->nslices; c++) {  /* the slice lines inside the window */
        float rel = (float)b->slice[c % b->nslices] - lo;
        if (rel <= 0.0f)
            rel += (float)len;
        if (rel > 0.0f && rel < w)
            cv_rect(KW_X + (int32_t)(rel / w * (float)KW_N), 22, 1, 5, C_GRAY);
    }
    if (px >= 0)
        cv_rect(KW_X + px, 20, 2, 70, C_WHITE);
    /* the bottom: this repeat's progress, then a dot for each repeat (sixteen, then round again) */
    cv_rect(KW_X, 88, (int32_t)(ph * 120.0f), 3, live ? C_HI : C_LINE);
    cv_rect(KW_X + (int32_t)(ph * 120.0f), 88, 120 - (int32_t)(ph * 120.0f), 3, C_BLOCK);
    for (c = 0; c < 16u; c++) {
        int on = live && c <= (uint32_t)(st->key_n % 16u);
        cv_rect(132 + (int32_t)c * 7, 86, 5, 6, on ? (c == st->key_n % 16u ? C_WHITE : C_AMB) : C_BLOCK);
    }
    cv_oy = 0;
    cv_blit(0, FX_Y + (uint32_t)top);
}

/* (drawn in two bands of FX_BAND rows: the canvas is 60 rows) */
static void draw_key(int key, const brk_status_t *st, uint32_t now)
{
    draw_key_band(key, st, now, 0);
    draw_key_band(key, st, now, FX_BAND);
}
