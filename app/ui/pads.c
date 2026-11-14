/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the black-key pads: their own settings (K1-K4 while held) and their animations. (Part of app.c's unity:
 * included there in order.) */

/* -------------------------------------------------- the pads' own settings --- */
/* what K1-K4 tune while a pad is held, by pad (brk.h PAD_*); 0: that knob does nothing for it */
static const char *const PAD_KNOB[BRK_NPADS][4] = {
    {"RATE", "DECAY", "PITCH", "LEVEL"}, {"RATE", "DECAY", "PITCH", "LEVEL"}, {"RATE", "DECAY", "PITCH", "LEVEL"},
    {"SPEED", 0, 0, 0}, {"LENGTH", "PITCH", 0, 0}, {"SPEED", 0, 0, 0}, {"STOP", "SPIN UP", 0, 0},
    {"RATE", "LENGTH", 0, 0}, {"AMOUNT", "PITCH", 0, 0}, {"TARGET", "RESO", "SPEED", 0},
    {"TIME", "FEEDBACK", "TONE", "MIX"}};
static const char *const PAD_TITLE[BRK_NPADS] = {"ROLL", "ROLL", "ROLL", "REVERSE", "FREEZE", "HALF SPEED",
                                                  "TAPE STOP", "GATE", "RANDOM", "FILTER", "DUB"};
static const char *const FRZ_LEN_NAME[4] = {"SLICE", "1/2", "1/4", "1/8"};

/* -1..1 as the DJ filter's frequency: "LP 340HZ" / "HP 1.2K" */
static void dj_text(float dj, char *t)
{
    float hz = dj > 0.0f ? 20.0f * brk_exp2f(dj * 8.2f) : 18000.0f * brk_exp2f(dj * 7.8f);
    t[0] = 0;
    text_cat(t, dj > 0.0f ? "HP " : "LP ");
    if (hz >= 1000.0f) {
        tenths_text((int32_t)(hz / 100.0f), t + 3);
        text_cat(t, "K");
    } else {
        int_text((int32_t)hz, t + 3);
        text_cat(t, "HZ");
    }
}

/* the roll's decay: "DOWN 20%" each repeat quieter, "UP 20%" a swell, "OFF" */
static void roll_decay_text(float dc, char *t)
{
    char v[8];
    t[0] = 0;
    if (dc > -0.01f && dc < 0.01f) {
        text_cat(t, "OFF");
        return;
    }
    text_cat(t, dc > 0.0f ? "DOWN " : "UP ");
    pct_text(dc > 0.0f ? dc : -dc, v);
    text_cat(t, v);
}

static void pad_knob_value(int pad, int k, char *t)
{
    const brk_padset_t *p = &set.pads;
    const brk_fx_t *f = &set.fx;
    t[0] = 0;
    switch (pad <= PAD_ROLL32 ? 0 : pad) {
    case 0:
        if (k == 0) text_cat(t, BRK_ROLL_DIV_NAME[p->roll_div[pad] % BRK_ROLL_DIVS]);
        else if (k == 1) roll_decay_text(p->roll_decay, t);
        else if (k == 2) signed_tenths(p->roll_pitch, "ST", t);
        else pct_text((float)(p->roll_level ? p->roll_level : 100u) * 0.01f, t);
        break;
    case PAD_REVERSE: times_text(p->rev_speed, t); break;
    case PAD_FREEZE:
        if (k == 0) text_cat(t, FRZ_LEN_NAME[p->frz_len & 3u]);
        else signed_tenths(p->frz_pitch, "ST", t);
        break;
    case PAD_HALF: times_text(p->half_speed, t); break;
    case PAD_TAPESTOP:
        if (k == 0)
            signed_tenths(p->stop_beats, " BT", t);
        else
            signed_tenths(p->spin_steps, " ST", t);
        if (t[0] == '+') {               /* (no sign: always positive) */
            uint32_t j = 0;
            do
                t[j] = t[j + 1];
            while (t[j++]);
        }
        break;
    case PAD_GATE:
        if (k == 0) text_cat(t, BRK_GATE_DIV_NAME[p->gate_div % BRK_GATE_DIVS]);
        else pct_text(p->gate_len, t);
        break;
    case PAD_RANDOM:
        if (k == 0) pct_text(p->rnd_amount, t);
        else { int_text((int32_t)p->rnd_pitch, t); text_cat(t, "ST"); }
        break;
    case PAD_FILTER:
        if (k == 0) dj_text(f->flt_target, t);
        else if (k == 1) pct_text(f->flt_res, t);
        else pct_text(f->flt_speed, t);
        break;
    case PAD_DUB:                        /* with a DELAY in the chain, the pad throws into it: its settings */
        if (delay_slot() >= 0) fx_value((uint32_t)delay_slot(), k, t);
        else if (k == 0) text_cat(t, BRK_DUB_DIV_NAME[f->dub_div % BRK_DUB_DIVS]);
        else pct_text(k == 1 ? f->dub_fb : k == 2 ? f->dub_tone : f->dub_mix, t);
        break;
    default: break;
    }
}

static void pad_knob_turn(int pad, int k, int32_t d)
{
    brk_padset_t *p = &set.pads;
    brk_fx_t *f = &set.fx;
    char t[16];
    if (pad < 0 || pad >= BRK_NPADS || k < 0 || k > 3 || !PAD_KNOB[pad][k])
        return;
    switch (pad <= PAD_ROLL32 ? 0 : pad) {
    case 0:
        if (k == 0) p->roll_div[pad] = (uint8_t)clamp((int32_t)p->roll_div[pad] + d, 0, BRK_ROLL_DIVS - 1);
        else if (k == 1) p->roll_decay = knob_f(p->roll_decay, d, 0.02f, -0.9f, 0.9f);
        else if (k == 2) {               /* 0.1 a detent, 0.5 turned fast (a run of detents < 40 ms apart) */
            static uint32_t last, run;
            uint32_t now = plat_ms();
            float v;
            run = now - last < 40u ? run + 1u : 0u;
            last = now;
            v = knob_f(p->roll_pitch, d, run >= 3u || d > 2 || d < -2 ? 0.5f : 0.1f, -12.0f, 12.0f);
            p->roll_pitch = (float)(int32_t)(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f)) * 0.1f;   /* (on the tenth) */
        }
        else p->roll_level = (uint8_t)clamp((int32_t)(p->roll_level ? p->roll_level : 100u) + d * 5, 5, 100);
        break;
    case PAD_REVERSE: p->rev_speed = knob_f(p->rev_speed, d, 0.05f, 0.5f, 2.0f); break;
    case PAD_FREEZE:
        if (k == 0) p->frz_len = (uint8_t)clamp((int32_t)p->frz_len + d, 0, 3);
        else p->frz_pitch = knob_f(p->frz_pitch, d, 1.0f, -12.0f, 12.0f);
        break;
    case PAD_HALF: p->half_speed = knob_f(p->half_speed, d, 0.05f, 0.25f, 0.75f); break;
    case PAD_TAPESTOP:
        if (k == 0) p->stop_beats = knob_f(p->stop_beats, d, 0.25f, 0.25f, 4.0f);
        else p->spin_steps = knob_f(p->spin_steps, d, 0.25f, 0.25f, 8.0f);
        break;
    case PAD_GATE:
        if (k == 0) p->gate_div = (uint8_t)clamp((int32_t)p->gate_div + d, 0, BRK_GATE_DIVS - 1);
        else p->gate_len = knob_f(p->gate_len, d, 0.05f, 0.1f, 0.9f);
        break;
    case PAD_RANDOM:
        if (k == 0) p->rnd_amount = knob_f(p->rnd_amount, d, 0.05f, 0.0f, 1.0f);
        else p->rnd_pitch = knob_f(p->rnd_pitch, d, 1.0f, 0.0f, 12.0f);
        break;
    case PAD_FILTER:
        if (k == 0) f->flt_target = knob_f(f->flt_target, d, 0.04f, -1.0f, 1.0f);
        else if (k == 1) f->flt_res = knob_f(f->flt_res, d, 0.02f, 0.0f, 0.95f);
        else f->flt_speed = knob_f(f->flt_speed, d, 0.02f, 0.0f, 1.0f);
        break;
    case PAD_DUB:
        if (delay_slot() >= 0) {         /* the DELAY slot's own settings */
            fx_knob_turn((uint32_t)delay_slot(), k, d);
            return;
        }
        if (k == 0) f->dub_div = (uint8_t)clamp((int32_t)f->dub_div + d, 0, BRK_DUB_DIVS - 1);
        else if (k == 1) f->dub_fb = knob_f(f->dub_fb, d, 0.02f, 0.0f, 0.95f);
        else if (k == 2) f->dub_tone = knob_f(f->dub_tone, d, 0.02f, 0.0f, 1.0f);
        else f->dub_mix = knob_f(f->dub_mix, d, 0.02f, 0.0f, 1.0f);
        break;
    default: break;
    }
    pad_knob_value(pad, k, t);
    app_say(PAD_KNOB[pad][k], t);
    changed();
}

/* ---------------------------------------------------------- pad animations --- */
#define C_ICE RGB(150, 220, 255)
#define C_DUB RGB(120, 230, 160)
#define FX_Y 90                          /* the animation's strip: 240 x 96 where the slice blocks are */
#define FX_MID 58                        /* its waveform's centre line, in the strip */
#define FX_BAND 48                       /* drawn in two bands of 48 rows (the canvas holds 60) */

static uint32_t fx_hash(uint32_t x)      /* a stable pseudo-random number from x (the screen's dice) */
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}

static void fx_ring(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* a circle of dots */
{
    uint32_t a;
    for (a = 0; a < 64u; a++) {
        float t = (float)a / 64.0f;
        cv_rect(cx + (int32_t)((float)r * brk_sin_turns(t + 0.25f)), cy + (int32_t)((float)r * brk_sin_turns(t)),
                2, 2, c);
    }
}

static void fx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t n = (x1 > x0 ? x1 - x0 : x0 - x1) > (y1 > y0 ? y1 - y0 : y0 - y1) ? (x1 > x0 ? x1 - x0 : x0 - x1)
                                                                            : (y1 > y0 ? y1 - y0 : y0 - y1), i;
    for (i = 0; i <= n; i++)
        cv_rect(x0 + (x1 - x0) * i / (n ? n : 1), y0 + (y1 - y0) * i / (n ? n : 1), 2, 2, c);
}

/* a slice's shape (blk_px) stretched over w columns from x, h times as tall, in colour c */
static void fx_slice(int32_t s, int32_t x, int32_t w, float h, uint16_t c)
{
    int32_t i;
    if (s < 0 || s >= (int32_t)blk_n)
        return;
    for (i = 0; i < w; i++) {
        int32_t v = (int32_t)((float)blk_px[s][(uint32_t)i * BLK_W / (uint32_t)w] * 1.7f * h);
        cv_rect(x + i, FX_MID - v, 1, 2 * v + 1, c);
    }
}

/* the held pad's picture: what it does to the break, moving with it, and its settings */
static void draw_fx_band(int pad, const brk_status_t *st, uint32_t now, int32_t top)
{
    const brk_padset_t *p = &set.pads;
    const brk_fx_t *f = &set.fx;
    char t[24];
    int32_t i, s = st->slice >= 0 ? st->slice : 0;
    uint32_t held = now - ui.pad_since;
    cv_begin(240, FX_BAND, C_BLACK);
    cv_oy = -top;                        /* (this band's rows of the picture) */
    t[0] = 0;
    text_cat(t, PAD_TITLE[pad]);
    if (pad <= PAD_ROLL32) {
        text_cat(t, " ");
        text_cat(t, BRK_ROLL_DIV_NAME[p->roll_div[pad] % BRK_ROLL_DIVS]);
    }
    cv_text(6, 0, &FONT_B, t, C_HI);
    switch (pad <= PAD_ROLL32 ? 0 : pad) {
    case 0: {                            /* ROLL: the caught slice again and again at its level, fading or
                                          * swelling, the hit lit */
        int32_t rs = st->roll_slice >= 0 ? st->roll_slice : s, cur = (int32_t)(st->roll_n % 8u);
        float g = (float)(p->roll_level ? p->roll_level : 100u) * 0.01f;
        for (i = 0; i < 8; i++) {
            fx_slice(rs, 4 + i * 29, 27, g, i == cur ? C_HI : C_DIM);
            g = p->roll_decay >= 0.0f ? g * (1.0f - p->roll_decay) : g + (1.0f - g) * -p->roll_decay;
            if (g < 0.02f)
                g = 0.02f;
        }
        int_text((int32_t)st->roll_n, t);
        text_cat(t, " HITS");
        cv_text(234 - text_w(&FONT_XS, t), 2, &FONT_XS, t, C_GRAY);
        break;
    }
    case PAD_REVERSE: {                  /* REVERSE: the break running backwards past a fixed playhead, chevrons */
        int32_t off = (int32_t)((float)held * 0.12f * p->rev_speed) % 240;
        for (i = 0; i < 240; i++) {
            int32_t h = wave_px[(uint32_t)(240 + off - i) % 240u] * 3 / 2;
            cv_rect(i, FX_MID - h, 1, 2 * h + 1, i < 120 ? C_DIM : C_HI);
        }
        cv_rect(120, 20, 2, 76, C_WHITE);
        for (i = 0; i < 2; i++) {        /* two chevrons pointing left, sliding */
            int32_t cx = 40 + i * 22 - (int32_t)(held / 40u % 22u), r;
            for (r = 0; r < 9; r++)
                cv_rect(cx + r, FX_MID - r - 1, 3, 2, C_WHITE), cv_rect(cx + r, FX_MID + r, 3, 2, C_WHITE);
        }
        break;
    }
    case PAD_FREEZE: {                   /* FREEZE: the slice iced over, its loop bracketed, a cursor going round */
        int32_t loop = 240 >> (p->frz_len & 3u), cx;
        fx_slice(s, 0, 240, 1.0f, C_LINE);
        fx_slice(s, 0, 240, 1.0f, C_ICE);
        cv_rect(0, 22, loop, 2, C_WHITE);
        cv_rect(0, 92, loop, 2, C_WHITE);
        cv_rect(loop - 2, 22, 2, 72, C_WHITE);
        cx = (int32_t)(held % 700u) * loop / 700;
        cv_rect(cx, 22, 2, 72, C_HI);
        for (i = 0; i < 24; i++) {       /* frost: dots that twinkle */
            uint32_t r = fx_hash((uint32_t)i * 977u + held / 180u);
            cv_rect((int32_t)(r % 236u), 24 + (int32_t)((r >> 8) % 66u), 2, 2, (r >> 20) & 1u ? C_WHITE : C_ICE);
        }
        break;
    }
    case PAD_HALF: {                     /* HALF SPEED: the break stretched, crawling */
        int32_t off = (int32_t)((float)held * 0.05f * p->half_speed);
        for (i = 0; i < 240; i++) {
            int32_t h = wave_px[(uint32_t)((i + off) / 2) % 240u] * 3 / 2;
            cv_rect(i, FX_MID - h, 1, 2 * h + 1, (i / 2) % 2 ? C_HI : C_AMB);
        }
        times_text(p->half_speed, t);
        cv_text(234 - text_w(&FONT_B, t), 0, &FONT_B, t, C_WHITE);
        break;
    }
    case PAD_TAPESTOP: {                 /* TAPE STOP: two reels slowing to a stop, the tape between them */
        int32_t k;
        for (k = 0; k < 2; k++) {
            int32_t cx = k ? 176 : 64, j;
            fx_ring(cx, FX_MID + 2, 30, C_GRAY);
            fx_ring(cx, FX_MID + 2, 6, C_HI);
            for (j = 0; j < 3; j++) {    /* the spokes, turning with the tape */
                float a = ui.reel + (float)j / 3.0f + (k ? 0.1f : 0.0f);
                fx_line(cx, FX_MID + 2, cx + (int32_t)(27.0f * brk_sin_turns(a + 0.25f)),
                        FX_MID + 2 + (int32_t)(27.0f * brk_sin_turns(a)), C_HI);
            }
        }
        cv_rect(64, FX_MID + 32, 112, 2, C_GRAY);
        pct_text(st->tape, t);
        cv_text(120 - text_w(&FONT_XS, t) / 2, 22, &FONT_XS, t, C_WHITE);
        break;
    }
    case PAD_GATE: {                     /* GATE: the bar's gates, open part lit, the one playing bright */
        int32_t cells = 8 << (p->gate_div % BRK_GATE_DIVS), w = 236 / cells, curc;
        float pos = (float)(st->step < 0 ? 0 : st->step % 16) + st->phase;
        curc = (int32_t)(pos * (float)cells / 16.0f) % cells;
        for (i = 0; i < cells; i++) {
            int32_t open = (int32_t)((float)(w - 1) * p->gate_len);
            cv_rect(2 + i * w, 34, w - 1, 48, C_BLOCK);
            cv_rect(2 + i * w, 34, open < 1 ? 1 : open, 48, i == curc ? (st->gate > 0.5f ? C_WHITE : C_HI) : C_AMB);
        }
        break;
    }
    case PAD_RANDOM: {                   /* RANDOM: dice over the slices, the one sounding big */
        uint32_t seed = (uint32_t)(st->step + 1) * 2654435761u;
        for (i = 0; i < 16; i++) {
            int32_t x = 120 + (i % 8) * 15, y = 30 + (i / 8) * 32;
            uint32_t r = fx_hash(seed + (uint32_t)i);
            cv_rect(x, y, 13, 28, i == s ? C_HI : (r & 3u) == 0u ? C_GRAY : C_BLOCK);
        }
        int_text(s + 1, t);
        cv_text2(20, 30, &FONT_B, t, C_WHITE);
        if (p->rnd_pitch > 0.0f)
            cv_text(20, 72, &FONT_T, "+ PITCH", C_AMB);
        break;
    }
    case PAD_FILTER: {                   /* FILTER: the response curve, its cutoff sliding to the target */
        float dj = brk_fx_dj_now(), xc = dj > 0.0f ? dj * 220.0f + 10.0f : (1.0f + dj) * 220.0f + 10.0f;
        float res = f->flt_res;
        for (i = 0; i < 240; i++) {
            float x = (float)i, d = dj > 0.0f ? xc - x : x - xc, y;
            y = d < 0.0f ? 1.0f : 1.0f / (1.0f + d * d * 0.004f);   /* past the cutoff: falls away */
            if (d > -12.0f && d < 12.0f)
                y += res * 0.8f * (1.0f - brk_absf(d) / 12.0f);     /* the resonance's bump */
            {
                int32_t h = (int32_t)(y * 34.0f);
                cv_rect(i, 92 - h, 1, h, i % 2 ? C_AMB : C_HI);
            }
        }
        cv_rect((int32_t)xc, 22, 2, 72, C_WHITE);
        dj_text(dj, t);
        cv_text(234 - text_w(&FONT_XS, t), 2, &FONT_XS, t, C_WHITE);
        break;
    }
    case PAD_DUB: {                      /* DUB: echoes marching out, each one fainter and darker */
        static const uint8_t SP[BRK_DUB_DIVS] = {2, 3, 4, 6};
        float tempo = (float)set.tempo_x10 * 0.1f, step_ms = 60000.0f / (tempo * 4.0f);
        float per = step_ms * (float)SP[f->dub_div % BRK_DUB_DIVS], ph = (float)(held % (uint32_t)per) / per;
        int32_t sp = 26 + 8 * (int32_t)SP[f->dub_div % BRK_DUB_DIVS] / 2;
        float g = 1.0f;
        for (i = 0; i < 10; i++) {
            int32_t x = 6 + i * sp + (int32_t)(ph * (float)sp);
            if (x > 236)
                break;
            fx_slice(s, x, 18, g, g > 0.6f ? C_DUB : g > 0.3f ? C_GRAY : C_LINE);
            g *= f->dub_fb;
        }
        break;
    }
    default: break;
    }
    cv_oy = 0;
    cv_blit(0, FX_Y + (uint32_t)top);
}

/* (drawn in two bands of FX_BAND rows: the canvas is 60 rows) */
static void draw_fx(int pad, const brk_status_t *st, uint32_t now)
{
    draw_fx_band(pad, st, now, 0);
    draw_fx_band(pad, st, now, FX_BAND);
}
