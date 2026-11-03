/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the demo (PRD B-54). Left alone for a minute while stopped (three while playing), the FM-1 shows four
 * scenes in turn, eight seconds each, on the tempo (the break's beat while it plays, a clock of its own stopped), the
 * lights animating with them: BARS (the mark's sixteen bars bouncing to an Amen beat), WAVE (the break's waveform
 * scrolling under a playhead, a slice chopped out of line on each beat), TUNNEL (squares rushing out from the
 * name, a white one on each beat) and STARS (slice blocks flying out of the screen). A comet runs through the
 * buttons all the while; the keys light with each scene. Any key, button or knob ends it, and does nothing else.
 * (Part of app.c's unity: included there in order, after screen.c.) */

#define DEMO_IDLE_MS 60000u              /* stopped: a minute untouched */
#define DEMO_IDLE_PLAY_MS 180000u        /* playing: three */
#define DEMO_SCENE_MS 8000u
#define DEMO_SCENES 4u
#define DEMO_FRAME_MS 40u                /* 25 frames a second */
#define DEMO_STARS 64u

static struct {
    uint8_t on;
    uint32_t t0, drawn, rng;
    float beat;                          /* where it is, in beats */
    int32_t step;                        /* the 16th playing (0..15), for the beat's hits */
    float env[16];                       /* BARS: each bar's bounce, 0..1 */
    uint8_t chop;                        /* WAVE: the slice chopped out of line this beat */
    uint16_t twinkle[NKEYS];             /* STARS: each key's light, ms left */
} demo;
static struct { float sx[DEMO_STARS], sy[DEMO_STARS], sz[DEMO_STARS]; } demo_sky PLAT_POOL;   /* STARS: where each
                                          * is (z: 1 far .. 0 here), set when the demo starts (the pool: RAM is short) */

/* an Amen bar's hits, a 16th each: bit 0 the kick, 1 the snare, 2 the hat */
static const uint8_t DEMO_AMEN[16] = {5, 4, 5, 4, 6, 4, 4, 6, 4, 6, 5, 5, 6, 4, 4, 6};

static void demo_star(uint32_t i)        /* a star from near the middle, far away */
{
    demo_sky.sx[i] = brk_randf(&demo.rng) * 2.0f - 1.0f;
    demo_sky.sy[i] = brk_randf(&demo.rng) * 2.0f - 1.0f;
    demo_sky.sz[i] = 0.6f + 0.4f * brk_randf(&demo.rng);
}

static void demo_start(uint32_t now)
{
    uint32_t i;
    demo.on = 1u;
    demo.t0 = now;
    demo.drawn = 0u;
    demo.rng = now | 1u;
    demo.step = -1;
    for (i = 0; i < 16u; i++)
        demo.env[i] = 0.0f;
    for (i = 0; i < DEMO_STARS; i++) {
        demo_star(i);
        demo_sky.sz[i] = 0.05f + 0.95f * brk_randf(&demo.rng);   /* (all depths at once: no empty start) */
    }
    for (i = 0; i < NKEYS; i++)
        demo.twinkle[i] = 0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
}

static void demo_end(void)
{
    demo.on = 0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = RG_ALL;
}

static uint32_t demo_scene(uint32_t now) { return (now - demo.t0) / DEMO_SCENE_MS % DEMO_SCENES; }

/* the beat: the break's while it plays, else the tempo's own clock; a new 16th sets off its hits */
static void demo_clock(uint32_t now)
{
    brk_status_t st;
    int32_t step;
    uint32_t i;
    brk_status(&st);
    if (st.playing && st.bar16 >= 0)
        demo.beat = (float)(st.bar % 64u) * 4.0f + ((float)st.bar16 + st.phase) * 0.25f;
    else
        demo.beat = (float)(now - demo.t0) * brk_get_tempo() * (1.0f / 60000.0f);
    for (i = 0; i < 16u; i++)            /* the bars fall back */
        demo.env[i] *= 0.82f;
    step = (int32_t)(demo.beat * 4.0f) & 15;
    if (step != demo.step) {
        uint32_t h = DEMO_AMEN[step];
        demo.step = step;
        demo.env[step] = 1.0f;           /* the 16th itself, then its hits: the kick low, the snare mid, the hat high */
        if (h & 1u)
            for (i = 0; i < 4u; i++)
                demo.env[i] = demo.env[i] > 0.9f - 0.15f * (float)i ? demo.env[i] : 0.9f - 0.15f * (float)i;
        if (h & 2u)
            for (i = 5u; i < 11u; i++)
                demo.env[i] = demo.env[i] > 0.85f ? demo.env[i] : 0.85f;
        if (h & 4u)
            demo.env[12u + (uint32_t)step % 4u] = 0.6f;
        if (!(step & 3))
            demo.chop = (uint8_t)(brk_rand(&demo.rng) % 16u);
    }
}

static void demo_press(int32_t top)      /* the scenes' foot: PRESS ANY KEY, blinking on the beat */
{
    float f = demo.beat - (float)(int32_t)demo.beat;
    if (f < 0.6f)
        cv_text((240 - text_w(&FONT_T, "PRESS ANY KEY")) / 2, 222 - top, &FONT_T, "PRESS ANY KEY", C_AMB);
}

static void demo_bars(int32_t top)      /* BARS: the mark's bars, bouncing, mirrored, the name over them */
{
    uint32_t k;
    title(26 - top);
    for (k = 0; k < 16u; k++) {
        int32_t x = 8 + (int32_t)k * 14, chop = k == 5u || k == 11u, h = 6 + (int32_t)(demo.env[k] * 52.0f);
        int32_t mid = 156 - top + (chop ? -10 : 0);
        uint16_t c = chop ? C_WHITE : demo.env[k] > 0.7f ? C_BLOCK_HI : C_HI;
        cv_rect(x, mid - h, 12, h, c);
        cv_rect(x, mid + 2, 12, h / 2, C_BLOCK);   /* its reflection */
        cv_rect(x, mid - h - 4, 12, 2, C_WHITE);   /* its cap */
    }
    demo_press(top);
}

static void demo_wave(int32_t top)      /* WAVE: the break scrolling under a playhead, a slice chopped each beat */
{
    const brk_break_t *b = brk_get_break();
    int32_t x, shift = (int32_t)(demo.beat * 30.0f) % 240;   /* (two bars across the screen) */
    cv_text((240 - text_w(&FONT_XS, b ? b->name : "")) / 2, 20 - top, &FONT_XS, b ? b->name : "", C_GRAY);
    cv_text2((240 - text2_w(&FONT_B, "CHOP")) / 2, 38 - top, &FONT_B, "CHOP", C_WHITE);
    for (x = 0; x < 240; x++) {
        int32_t src = (x + shift) % 240, sl = src / 15, h = (int32_t)wave_px[src] * 5 / 2 + 1, y = 150 - top;
        int hot = sl == demo.chop, near = x >= 112 && x < 128;
        uint16_t c = hot ? C_WHITE : near ? C_BLOCK_HI : (sl & 1) ? C_HI : C_AMB;
        if (hot)
            y -= 16;
        cv_rect(x, y - h, 1, 2 * h, c);
    }
    cv_rect(120, 84 - top, 1, 132, C_WHITE);   /* the playhead */
    demo_press(top);
}

static void demo_tunnel(uint32_t now, int32_t top)   /* TUNNEL: squares rushing out from the name */
{
    uint32_t i;
    float run = demo.beat * 0.5f;        /* a new square every half beat */
    for (i = 0; i < 8u; i++) {
        float z = 1.0f - ((float)i + (run - (float)(int32_t)run)) * 0.125f;   /* 1 far .. 0 near */
        int32_t r = 12 + (int32_t)(150.0f * (1.0f - z) * (1.0f - z)), cy = 120 - top;
        uint32_t idx = (uint32_t)(int32_t)run - i;
        uint16_t c = idx % 4u == 0u ? C_WHITE : z < 0.4f ? C_BLOCK_HI : z < 0.7f ? C_HI : C_DIM;
        if (r > 170)
            continue;
        cv_rect(120 - r, cy - r, 2 * r, 2, c);
        cv_rect(120 - r, cy + r - 2, 2 * r, 2, c);
        cv_rect(120 - r, cy - r, 2, 2 * r, c);
        cv_rect(120 + r - 2, cy - r, 2, 2 * r, c);
    }
    (void)now;
    cv_rect(40, 98 - top, 160, 46, C_BLACK);
    cv_text2((240 - text2_w(&FONT_B, APP_NAME)) / 2, 102 - top, &FONT_B, APP_NAME, C_WHITE);
}

static void demo_stars(int32_t top)     /* STARS: slice blocks flying out of the screen */
{
    uint32_t i;
    for (i = 0; i < DEMO_STARS; i++) {
        float z = demo_sky.sz[i] + 0.02f;
        int32_t x = 120 + (int32_t)(demo_sky.sx[i] * 40.0f / z), y = 120 + (int32_t)(demo_sky.sy[i] * 40.0f / z) - top;
        int32_t s = (int32_t)(2.0f / z);
        uint16_t c = z < 0.25f ? C_WHITE : z < 0.5f ? C_BLOCK_HI : z < 0.75f ? C_HI : C_DIM;
        if (s > 10)
            s = 10;
        cv_rect(x - s / 2, y - s, s, 2 * s, c);
    }
    if (top >= 180)
        demo_press(top);
}

static void demo_frame(uint32_t now)
{
    uint32_t sc, i;
    int32_t top;
    if (now - demo.drawn < DEMO_FRAME_MS)
        return;
    demo.drawn = now;
    demo_clock(now);
    sc = demo_scene(now);
    if (sc == 3u)                        /* the stars come closer, faster on the beat */
        for (i = 0; i < DEMO_STARS; i++) {
            float f = demo.beat - (float)(int32_t)demo.beat;
            demo_sky.sz[i] -= 0.012f + (f < 0.25f ? 0.02f : 0.0f);
            if (demo_sky.sz[i] < 0.02f || demo_sky.sx[i] * 40.0f / demo_sky.sz[i] > 130.0f ||
                demo_sky.sx[i] * 40.0f / demo_sky.sz[i] < -130.0f || demo_sky.sy[i] * 40.0f / demo_sky.sz[i] > 130.0f ||
                demo_sky.sy[i] * 40.0f / demo_sky.sz[i] < -130.0f)
                demo_star(i);
        }
    for (top = 0; top < 240; top += 60) {   /* the whole screen, in four strips */
        cv_begin(240, 60, C_BLACK);
        if (sc == 0u)
            demo_bars(top);
        else if (sc == 1u)
            demo_wave(top);
        else if (sc == 2u)
            demo_tunnel(now, top);
        else
            demo_stars(top);
        cv_blit(0, (uint32_t)top);
    }
}

/* the demo's lights: the buttons' comet (as the start screen's, round and round), and the keys with each scene */
static void demo_leds(uint32_t now)
{
    static const uint8_t PATH[NB] = {B_OCTDN, B_OCTUP, B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO,
                                     B_REC, B_PLAY, B_SEQ, B_ARP, B_SAVE, B_HOME};
    uint32_t t = now - demo.t0, head = t / 70u % NB, b = 0u, mid = 0u, dim, k = 0u, km = 0u, kd = 0u, i;
    uint32_t sc = demo_scene(now);
    float f = demo.beat - (float)(int32_t)demo.beat;
    dim = (((1u << NB) - 1u) & ~(1u << B_PLAY)) | LED_PLAY_GREEN;   /* (PLAY: its green LED) */
    b = 1u << PATH[head];
    mid = (1u << PATH[(head + NB - 1u) % NB] | 1u << PATH[(head + NB - 2u) % NB]) & ~(1u << B_PLAY);
    if (b & 1u << B_PLAY)
        b = LED_PLAY_GREEN;
    if (sc == 0u) {                      /* BARS: the white keys with their bars, the black ones on the snare */
        for (i = 0; i < 16u; i++)
            if (demo.env[i] > 0.6f)
                k |= 1u << WK[i];
            else if (demo.env[i] > 0.2f)
                km |= 1u << WK[i];
        if (demo.step >= 0 && DEMO_AMEN[demo.step] & 2u && f * 4.0f - (float)(int32_t)(f * 4.0f) < 0.5f)
            for (i = 0; i < 11u; i++)
                k |= 1u << BK[i];
    } else if (sc == 1u) {               /* WAVE: the slice under the playhead and the one chopped; black: ping-pong */
        int32_t shift = (int32_t)(demo.beat * 30.0f) % 240, p = (int32_t)(t / 60u % 20u);
        k |= 1u << WK[((120 + shift) % 240) / 15];
        km |= 1u << WK[demo.chop % 16u];
        p = p < 11 ? p : 20 - p;
        k |= 1u << BK[p % 11];
        if (p > 0)
            km |= 1u << BK[(p - 1) % 11];
    } else if (sc == 2u) {               /* TUNNEL: out from the middle keys, each half beat */
        float h = demo.beat * 2.0f;
        int32_t r = (int32_t)((h - (float)(int32_t)h) * 8.0f);
        k |= 1u << WK[7 - r] | 1u << WK[8 + r];
        if (r > 0)
            km |= 1u << WK[8 - r] | 1u << WK[7 + r];
        for (i = 0; i < 16u; i++)
            kd |= 1u << WK[i];
        if (f < 0.15f)
            for (i = 0; i < 11u; i++)
                k |= 1u << BK[i];
    } else {                             /* STARS: the keys twinkling */
        static uint32_t last;
        uint32_t dt = now - last;
        last = now;
        for (i = 0; i < NKEYS; i++) {
            demo.twinkle[i] = (uint16_t)(demo.twinkle[i] > dt ? demo.twinkle[i] - dt : 0u);
            if (!demo.twinkle[i] && brk_rand(&demo.rng) % 40u == 0u)
                demo.twinkle[i] = (uint16_t)(120u + brk_rand(&demo.rng) % 240u);
            if (demo.twinkle[i] > 120u)
                k |= 1u << i;
            else if (demo.twinkle[i])
                km |= 1u << i;
        }
    }
    plat_lights(b, k, dim, kd & ~k, 1);
    plat_lights_mid(mid, km);
}
