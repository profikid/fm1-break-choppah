/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the demo (PRD B-54). Left alone for a minute while stopped (three while playing), the FM-1 shows five
 * scenes in turn, eight seconds each, on the tempo (the break's beat while it plays, a clock of its own stopped), the
 * lights animating with them: BARS (the mark's sixteen bars bouncing to an Amen beat), WAVE (the break's waveform
 * scrolling under a playhead, a slice chopped out of line on each beat), TUNNEL (squares rushing out from the
 * name, a white one on each beat), WORMHOLE (rings of dots twisting down a bending tunnel to a glowing core) and STARS
 * (slice blocks flying out of the screen). A comet runs through the buttons all the while; the keys light with each
 * scene. Any key, button or knob ends it, and does nothing else. (Part of app.c's unity: included there in order,
 * after screen.c.) Written small: the flash is full */

#define DEMO_IDLE_MS 60000u              /* stopped: a minute untouched */
#define DEMO_IDLE_PLAY_MS 180000u        /* playing: three */
#define DEMO_SCENE_MS 8000u
#define DEMO_SCENES 5u
#define DEMO_FRAME_MS 40u                /* 25 frames a second */
#define DEMO_STARS 64u

static struct {
    uint8_t on;
    uint8_t chop;                        /* WAVE: the slice chopped out of line this beat */
    int32_t step;                        /* the 16th playing (0..15), for the beat's hits */
    uint32_t t0, drawn, rng;
    float beat, frac;                    /* where it is, in beats; and within the beat (0..1) */
    float env[16];                       /* BARS: each bar's bounce, 0..1 */
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
    for (i = 0; i < sizeof demo; i++)    /* (all of it: the bars still, the keys dark) */
        ((uint8_t *)&demo)[i] = 0u;
    demo.on = 1u;
    demo.t0 = now;
    demo.rng = now | 1u;
    demo.step = -1;
    for (i = 0; i < DEMO_STARS; i++) {
        demo_star(i);
        demo_sky.sz[i] = 0.05f + 0.95f * brk_randf(&demo.rng);   /* (all depths at once: no empty start) */
    }
    lcd_fill(0, 0, 240, 240, C_BLACK);
}

static void demo_end(void)
{
    demo.on = 0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = RG_ALL;
}

static uint32_t demo_scene(uint32_t now) { return (now - demo.t0) / DEMO_SCENE_MS % DEMO_SCENES; }
static float demo_max(float a, float b) { return a > b ? a : b; }

/* the beat: the break's while it plays, else the tempo's own clock; a new 16th sets off its hits */
static void demo_clock(uint32_t now)
{
    brk_status_t st;
    int32_t step;
    uint32_t i, h;
    brk_status(&st);
    demo.beat = st.playing && st.bar16 >= 0 ? (float)(st.bar % 64u) * 4.0f + ((float)st.bar16 + st.phase) * 0.25f
                                           : (float)(now - demo.t0) * brk_get_tempo() * (1.0f / 60000.0f);
    demo.frac = demo.beat - (float)(int32_t)demo.beat;
    for (i = 0; i < 16u; i++)            /* the bars fall back */
        demo.env[i] *= 0.82f;
    step = (int32_t)(demo.beat * 4.0f) & 15;
    if (step == demo.step)
        return;
    h = DEMO_AMEN[step];
    demo.step = step;
    demo.env[step] = 1.0f;               /* the 16th itself, then its hits: the kick low, the snare mid, the hat high */
    if (h & 1u)
        for (i = 0; i < 4u; i++)
            demo.env[i] = demo_max(demo.env[i], 0.9f - 0.15f * (float)i);
    if (h & 2u)
        for (i = 5u; i < 11u; i++)
            demo.env[i] = demo_max(demo.env[i], 0.85f);
    if (h & 4u)
        demo.env[12u + (uint32_t)step % 4u] = 0.6f;
    if (!(step & 3))
        demo.chop = (uint8_t)(brk_rand(&demo.rng) % 16u);
}

/* near to far: near (z under a), C_HI (under b), C_DIM */
static uint16_t demo_depth(float z, float a, float b, uint16_t near) { return z < a ? near : z < b ? C_HI : C_DIM; }

static void demo_bars(uint32_t now, int32_t top)   /* BARS: the mark's bars, bouncing, mirrored, the name over them */
{
    uint32_t k;
    title(26 - top);
    for (k = 0; k < 16u; k++) {
        int32_t x = 8 + (int32_t)k * 14, chop = k == 5u || k == 11u, h = 6 + (int32_t)(demo.env[k] * 52.0f);
        int32_t mid = 156 - top - (chop ? 10 : 0);
        cv_rect(x, mid - h, 12, h, chop ? C_WHITE : demo.env[k] > 0.7f ? C_BLOCK_HI : C_HI);
        cv_rect(x, mid + 2, 12, h / 2, C_BLOCK);   /* its reflection */
        cv_rect(x, mid - h - 4, 12, 2, C_WHITE);   /* its cap */
    }
    (void)now;
}

static void demo_wave(uint32_t now, int32_t top)   /* WAVE: the break scrolling under a playhead, a slice chopped each beat */
{
    const brk_break_t *b = brk_get_break();
    int32_t x, shift = (int32_t)(demo.beat * 30.0f) % 240;   /* (two bars across the screen) */
    cv_text((240 - text_w(&FONT_XS, b ? b->name : "")) / 2, 20 - top, &FONT_XS, b ? b->name : "", C_GRAY);
    cv_text2((240 - text2_w(&FONT_B, "CHOP")) / 2, 38 - top, &FONT_B, "CHOP", C_WHITE);
    for (x = 0; x < 240; x++) {
        int32_t src = (x + shift) % 240, sl = src / 15, h = (int32_t)wave_px[src] * 5 / 2 + 1, hot = sl == demo.chop;
        cv_rect(x, 150 - top - (hot ? 16 : 0) - h, 1, 2 * h,
                hot ? C_WHITE : x >= 112 && x < 128 ? C_BLOCK_HI : (sl & 1) ? C_HI : C_AMB);
    }
    cv_rect(120, 84 - top, 1, 132, C_WHITE);   /* the playhead */
    (void)now;
}

static void demo_tunnel(uint32_t now, int32_t top)   /* TUNNEL: squares rushing out from the name */
{
    uint32_t i;
    float run = demo.beat * 0.5f;        /* a new square every half beat */
    for (i = 0; i < 8u; i++) {
        float z = 1.0f - ((float)i + (run - (float)(int32_t)run)) * 0.125f;   /* 1 far .. 0 near */
        int32_t r = 12 + (int32_t)(150.0f * (1.0f - z) * (1.0f - z)), x = 120 - r, y = 120 - top - r;
        uint16_t c = ((uint32_t)(int32_t)run - i) % 4u == 0u ? C_WHITE : demo_depth(z, 0.4f, 0.7f, C_BLOCK_HI);
        if (r > 170)
            continue;
        cv_frame(x, y, 2 * r, 2 * r, c); /* (two of them, one inside the other: 2 pixels wide) */
        cv_frame(x + 1, y + 1, 2 * r - 2, 2 * r - 2, c);
    }
    (void)now;
    cv_rect(40, 98 - top, 160, 46, C_BLACK);
    cv_text2((240 - text2_w(&FONT_B, APP_NAME)) / 2, 102 - top, &FONT_B, APP_NAME, C_WHITE);
}

/* WORMHOLE: fourteen rings of twenty-four dots rushing at you, two a beat (a jolt on each beat), each turned a little
 * from the last, and each dot joined to its next ring's by a line: a wireframe tube whose seams run in spiral arms.
 * The far end bends round (the rings' centres follow a slow curve, the farther the more) to a glowing core. Near
 * rings bigger, brighter, their dots larger; every fourth ring white */
#define WH_RINGS 14u
#define WH_DOTS 24u
static void demo_wormhole(uint32_t now, int32_t top)
{
    float t = (float)(now - demo.t0) * 0.001f, f = demo.frac;
    float run = demo.beat * 2.0f + (f < 0.2f ? f * 2.5f : 0.5f), fr = run - (float)(int32_t)run;
    float bx = brk_sin_turns(t * 0.07f), by = brk_sin_turns(t * 0.05f + 0.3f), spin = t * 0.08f;
    int16_t px[WH_DOTS], py[WH_DOTS];    /* the farther ring's dots, for the seams */
    uint32_t i, j, have = 0u;
    int32_t g, cx = 120 + (int32_t)(70.0f * bx), cy = 120 + (int32_t)(60.0f * by) - top;
    for (i = WH_RINGS; i-- > 0u;) {      /* far to near: the near ones over */
        float w = 1.0f - ((float)i + fr) / (float)WH_RINGS;   /* 1 far .. 0 near */
        float r = 9.0f / (w + 0.055f), cxf = 120.0f + 70.0f * bx * w * w, cyf = 120.0f + 60.0f * by * w * w;
        uint32_t n = (uint32_t)(int32_t)run - i;   /* the ring's own number, as it comes closer */
        int32_t sz = 1 + (int32_t)(7.0f * (1.0f - w) * (1.0f - w)), ry = (int32_t)cyf - top;
        uint16_t seam = w < 0.35f ? C_HI : w < 0.7f ? C_AMB : C_BLOCK;
        if (w <= 0.0f || r > 260.0f) {
            have = 0u;
            continue;                    /* (behind the eye) */
        }
        for (j = 0; j < WH_DOTS; j++) {
            float a = (float)j * (1.0f / (float)WH_DOTS) + (float)n * 0.031f + spin;
            int32_t x = (int32_t)(cxf + r * brk_sin_turns(a + 0.25f)), y = ry + (int32_t)(r * brk_sin_turns(a));
            if (have && (y > -40 || py[j] > -40) && (y < 100 || py[j] < 100))
                cv_line(px[j], py[j], x, y, seam);   /* the seam from the farther ring */
            if (y + sz >= 0 && y - sz < 60)
                cv_rect(x - sz / 2, y - sz / 2, sz, sz,
                        n % 4u == 0u || (j + n) % 6u == 0u ? C_WHITE : demo_depth(w, 0.35f, 0.7f, C_BLOCK_HI));
            px[j] = (int16_t)x;
            py[j] = (int16_t)y;
        }
        have = 1u;
    }
    for (g = 4; g >= 1; g--)             /* the core: where the far end bends to, glowing, over the far seams */
        cv_rect(cx - g * 3, cy - g * 3, g * 6, g * 6, g == 1 ? C_WHITE : g == 2 ? C_BLOCK_HI : g == 3 ? C_HI : C_BLOCK);
}

static void demo_stars(uint32_t now, int32_t top)   /* STARS: slice blocks flying out of the screen */
{
    uint32_t i;
    for (i = 0; i < DEMO_STARS; i++) {
        float z = demo_sky.sz[i] + 0.02f;
        int32_t x = 120 + (int32_t)(demo_sky.sx[i] * 40.0f / z), y = 120 + (int32_t)(demo_sky.sy[i] * 40.0f / z) - top;
        int32_t s = (int32_t)(2.0f / z);
        s = s > 10 ? 10 : s;
        cv_rect(x - s / 2, y - s, s, 2 * s, z < 0.25f ? C_WHITE : demo_depth(z, 0.5f, 0.75f, C_BLOCK_HI));
    }
    (void)now;
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
    for (i = 0; sc == 4u && i < DEMO_STARS; i++) {   /* the stars come closer, faster on the beat */
        float *z = &demo_sky.sz[i];
        *z -= 0.012f + (demo.frac < 0.25f ? 0.02f : 0.0f);
        if (*z < 0.02f || brk_absf(demo_sky.sx[i] * 40.0f / *z) > 130.0f || brk_absf(demo_sky.sy[i] * 40.0f / *z) > 130.0f)
            demo_star(i);
    }
    for (top = 0; top < 240; top += 60) {   /* the whole screen, in four strips */
        cv_begin(240, 60, C_BLACK);
        if (sc == 0u)
            demo_bars(now, top);
        else if (sc == 1u)
            demo_wave(now, top);
        else if (sc == 2u)
            demo_tunnel(now, top);
        else if (sc == 3u)
            demo_wormhole(now, top);
        else
            demo_stars(now, top);
        if (sc != 2u && top == 180 && demo.frac < 0.6f)   /* the foot: PRESS ANY KEY, blinking on the beat */
            cv_text((240 - text_w(&FONT_T, "PRESS ANY KEY")) / 2, 42, &FONT_T, "PRESS ANY KEY", C_AMB);
        cv_blit(0, (uint32_t)top);
    }
}

static uint32_t demo_keys(const uint8_t *key, uint32_t i, uint32_t n, uint32_t step)   /* their lights' bits */
{
    uint32_t m = 0u;
    for (; i < n; i += step)
        m |= 1u << key[i];
    return m;
}

/* the demo's lights: the buttons' comet (the start screen's PATH, round and round), and the keys with each scene */
static void demo_leds(uint32_t now)
{
    uint32_t t = now - demo.t0, head = t / 70u % NB, b, mid, k = 0u, km = 0u, kd = 0u, i, sc = demo_scene(now);
    float f = demo.frac;
    b = 1u << PATH[head];
    mid = (1u << PATH[(head + NB - 1u) % NB] | 1u << PATH[(head + NB - 2u) % NB]) & ~(1u << B_PLAY);
    if (b & 1u << B_PLAY)
        b = LED_PLAY_GREEN;              /* (PLAY: its green LED) */
    if (sc == 0u) {                      /* BARS: the white keys with their bars, the black ones on the snare */
        for (i = 0; i < 16u; i++)
            if (demo.env[i] > 0.6f)
                k |= 1u << WK[i];
            else if (demo.env[i] > 0.2f)
                km |= 1u << WK[i];
        if (demo.step >= 0 && DEMO_AMEN[demo.step] & 2u && f * 4.0f - (float)(int32_t)(f * 4.0f) < 0.5f)
            k |= demo_keys(BK, 0u, 11u, 1u);
    } else if (sc == 1u) {               /* WAVE: the slice under the playhead and the one chopped; black: ping-pong */
        int32_t p = (int32_t)(t / 60u % 20u);
        k |= 1u << WK[((120 + (int32_t)(demo.beat * 30.0f)) % 240) / 15];   /* (the beat is never negative) */
        km |= 1u << WK[demo.chop % 16u];
        p = p < 11 ? p : 20 - p;
        k |= 1u << BK[p % 11];
        if (p > 0)
            km |= 1u << BK[(p - 1) % 11];
    } else if (sc < 4u) {                /* TUNNEL: out from the middle keys, each half beat; WORMHOLE: two lights
                                          * swirling round the white keys, the black on the beat */
        kd = demo_keys(WK, 0u, 16u, 1u);
        if (sc == 2u) {
            float h = demo.beat * 2.0f;
            int32_t r = (int32_t)((h - (float)(int32_t)h) * 8.0f);
            k |= 1u << WK[7 - r] | 1u << WK[8 + r];
            if (r > 0)
                km |= 1u << WK[8 - r] | 1u << WK[7 + r];
            if (f < 0.15f)
                k |= demo_keys(BK, 0u, 11u, 1u);
        } else {
            uint32_t p = t / 45u % 16u;
            k |= 1u << WK[p] | 1u << WK[(p + 8u) % 16u];
            km |= 1u << WK[(p + 15u) % 16u] | 1u << WK[(p + 7u) % 16u];
            if (f < 0.2f)
                k |= demo_keys(BK, (uint32_t)(int32_t)demo.beat & 1u, 11u, 2u);
        }
    } else {                             /* STARS: the keys twinkling */
        static uint32_t last;
        uint32_t dt = now - last;
        last = now;
        for (i = 0; i < NKEYS; i++) {
            uint16_t *w = &demo.twinkle[i];
            *w = (uint16_t)(*w > dt ? *w - dt : 0u);
            if (!*w && brk_rand(&demo.rng) % 40u == 0u)
                *w = (uint16_t)(120u + brk_rand(&demo.rng) % 240u);
            if (*w > 120u)
                k |= 1u << i;
            else if (*w)
                km |= 1u << i;
        }
    }
    plat_lights(b, k, (((1u << NB) - 1u) & ~(1u << B_PLAY)) | LED_PLAY_GREEN, kd & ~k, 1);
    plat_lights_mid(mid, km);
}
