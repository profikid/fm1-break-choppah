/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH engine tests: the break engine, the effects and the ADPCM decoder on a computer, with plain gcc.
 * Writes WAV files of a few renders to OUTDIR to listen to.   tests/engine/run.sh */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adpcm.h"
#include "brk.h"
#include "brk_fx.h"
#include "brk_synth.h"
#include "brk_math.h"
#define brk_randf_test(s) (brk_randf(s) * 2.0f - 1.0f)

#define RATE 22050u
#define BPM 136.9f
#define BLOCK 256u

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static int16_t pcm[200000];
static brk_break_t brk;
static const char *outdir = ".";

static void fresh(uint32_t bars)
{
    brk_init();
    brk_synth_break(pcm, RATE, BPM, bars, &brk);
    brk.split = 0u;
    brk_set_break(&brk);
    brk_tempo(BPM);
    brk_pitch(0.0f);
    brk_swing(0.5f);
    brk_snap(1.0f);                      /* (keys at the next step: a slice is a step on these 1-bar breaks) */
    brk_seed(1u);
    {
        uint32_t p;
        for (p = 0; p < BRK_NPADS; p++)
            brk_pad((int)p, 0);
    }
}

/* renders n frames in blocks; slices[]: the slice sounding at the start of each step seen (may be 0) */
static float render(float *out, uint32_t n, int32_t *steps_seen, int32_t *slices, uint32_t max)
{
    uint32_t i = 0, k = 0;
    int32_t last_step = -2;
    float peak = 0.0f;
    while (i < n) {
        uint32_t m = n - i < BLOCK ? n - i : BLOCK, j;
        brk_status_t st;
        brk_render(out + i, m);
        brk_status(&st);
        if (slices && st.step != last_step && k < max) {
            slices[k++] = st.slice;
            last_step = st.step;
        }
        for (j = 0; j < m; j++)
            if (fabsf(out[i + j]) > peak)
                peak = fabsf(out[i + j]);
        i += m;
    }
    if (steps_seen)
        *steps_seen = (int32_t)k;
    return peak;
}

static void wav(const char *name, const float *l, const float *r, uint32_t n)
{
    char path[512];
    FILE *f;
    uint32_t i, bytes = n * 4u, v;
    snprintf(path, sizeof path, "%s/%s", outdir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fwrite("RIFF", 1, 4, f);
    v = 36u + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16u; fwrite(&v, 4, 1, f);
    { uint16_t a = 1, c = 2; fwrite(&a, 2, 1, f); fwrite(&c, 2, 1, f); }
    v = BRK_SR; fwrite(&v, 4, 1, f);
    v = BRK_SR * 4u; fwrite(&v, 4, 1, f);
    { uint16_t a = 4, b = 16; fwrite(&a, 2, 1, f); fwrite(&b, 2, 1, f); }
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    for (i = 0; i < n; i++) {
        int16_t s[2] = {(int16_t)(l[i] * 32000.0f), (int16_t)((r ? r[i] : l[i]) * 32000.0f)};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

static float buf[BRK_SR * 30u], bl[BRK_SR * 30u], br[BRK_SR * 30u];

static void test_straight(void)
{
    int32_t seen, sl[64], i, ok = 1;
    uint32_t bar = (uint32_t)(BRK_SR * 60.0f / BPM * 4.0f);
    fresh(1);
    brk_play(1);
    render(buf, bar, &seen, sl, 64);
    /* each step is about 19 blocks long, so every step is seen and the loop plays its slices in order */
    CHECK(seen >= 16, "steps seen %d", seen);
    for (i = 0; i < 16 && i < seen; i++)
        if (sl[i] != i)
            ok = 0;
    CHECK(ok, "straight loop plays slices 0..15 in order");
    wav("straight.wav", buf, 0, bar);
}

/* slices in order are the record as it is: markers moved onto the hits (off the grid, as --transients makes them)
 * play exactly what markers on the grid play, one voice running on, no jump, no hit twice */
static float ref_buf[BRK_SR * 4u];
static void test_in_order(void)
{
    uint32_t bar = (uint32_t)(BRK_SR * 60.0f / BPM * 4.0f), i, len, sixteenth;
    float worst = 0.0f;
    int32_t seen, sl[64], ok = 1;
    fresh(1);
    brk_play(1);
    render(ref_buf, 2u * bar, 0, 0, 0);
    fresh(1);
    len = brk.len;
    sixteenth = len / 16u;
    brk.slice[3] += sixteenth * 2u / 5u;   /* a hit late */
    brk.slice[6] -= sixteenth * 3u / 5u;   /* a hit early */
    brk.slice[11] += sixteenth / 4u;
    brk_set_break(&brk);
    brk_play(1);
    render(buf, 2u * bar, &seen, sl, 64);
    for (i = 0; i < 2u * bar; i++)
        if (fabsf(buf[i] - ref_buf[i]) > worst)
            worst = fabsf(buf[i] - ref_buf[i]);
    CHECK(worst < 1e-4f, "slices in order with markers off the grid play as on it (worst %g)", (double)worst);
    for (i = 0; i < 16 && (int32_t)i < seen; i++)
        if (sl[i] != (int32_t)i)
            ok = 0;
    CHECK(ok, "the slices still count 0..15 in order");
    brk_play(0);
}

static void test_tempo_accuracy(void)
{
    /* 64 bars at 174 BPM: the 1024th step starts within 1 sample of where it should */
    uint32_t n = 0, steps = 0, target = 64u * 16u;
    double expect = (double)BRK_SR * 60.0 / (174.0 * 4.0) * target;
    int32_t last = -1;
    brk_status_t st;
    fresh(1);
    brk_tempo(174.0f);
    brk_play(1);
    while (steps < target + 1u) {        /* one frame at a time: the exact sample a step starts */
        brk_render(buf, 1);
        brk_status(&st);
        if (st.step != last) {
            last = st.step;
            if (steps == target)
                break;
            steps++;
        }
        n++;
    }
    CHECK(fabs((double)n - expect) <= 1.0, "step %u at sample %u, expected %.1f", target, n, expect);
}

static void test_free_play(void)
{
    float peak;
    fresh(1);
    brk_play(0);
    render(buf, BLOCK, 0, 0, 0);
    CHECK(render(buf, BLOCK, 0, 0, 0) == 0.0f, "stopped and untouched: silence");
    brk_key(4, 1);
    peak = render(buf, BLOCK, 0, 0, 0);
    CHECK(peak > 0.05f, "free play sounds in the first block (peak %.3f)", peak);
    CHECK(fabsf(buf[0]) < 0.05f, "and fades in (first sample %.3f)", buf[0]);
    brk_key(4, 0);
}

static void test_quantised_key(void)
{
    int32_t seen, sl[8];
    uint32_t half_step = brk_step_len_samples() / 2u;
    fresh(1);
    brk_play(1);
    render(buf, half_step, 0, 0, 0); /* in the middle of step 0 */
    brk_key(9, 1);
    brk_key(9, 0);                       /* a tap */
    render(buf, brk_step_len_samples() * 2u, &seen, sl, 8);
    CHECK(seen >= 2 && sl[1] == 9, "a tap plays at the next step (got %d)", seen >= 2 ? sl[1] : -9);
    CHECK(seen >= 3 && sl[2] == 2, "then the loop goes on (got %d)", seen >= 3 ? sl[2] : -9);
}

static void test_hold_loops(void)
{
    int32_t seen, sl[8], i, ok = 1;
    fresh(1);
    brk_play(1);
    brk_key(3, 1);
    render(buf, brk_step_len_samples() * 5u, &seen, sl, 8);
    for (i = 1; i < seen; i++)
        if (sl[i] != 3)
            ok = 0;
    CHECK(ok && seen >= 4, "a held key plays its slice at every step");
    brk_key(3, 0);
}

static void test_generator(void)
{
    int32_t seen, a[64], b[64], i, same = 1, differ = 0;
    brk_gen_t g = {0.0f, 0.0f, 0.0f, 0.0f, 0};
    uint32_t bars4 = brk_step_len_samples() * 64u;
    fresh(1);
    brk_gen(&g);
    brk_play(1);
    render(buf, bars4, &seen, a, 64);
    for (i = 0; i < seen; i++)
        if (a[i] != i % 16)
            same = 0;
    CHECK(same, "complexity 0 is the straight loop");
    g.complexity = 0.8f;
    g.anchor = 0.0f;
    fresh(1);
    brk_gen(&g);
    brk_seed(42u);
    brk_play(1);
    render(buf, bars4, &seen, a, 64);
    fresh(1);
    brk_gen(&g);
    brk_seed(42u);
    brk_play(1);
    render(buf, bars4, &seen, b, 64);
    for (i = 0; i < seen; i++) {
        if (a[i] != b[i])
            same = 0;
        if (a[i] != i % 16)
            differ++;
    }
    CHECK(same, "the same seed gives the same variation");
    CHECK(differ > 16, "complexity 0.8 rearranges the loop (%d of %d steps moved)", differ, seen);
    wav("generator.wav", buf, 0, bars4);
}

static void test_pads(void)
{
    int32_t seen, sl[16], i, ok = 1;
    float peak;
    /* a roll keeps the slice it caught while the sequencer counts on */
    fresh(1);
    brk_play(1);
    render(buf, brk_step_len_samples() * 3u + 10u, 0, 0, 0);
    brk_pad(PAD_ROLL16, 1);
    render(buf, brk_step_len_samples() * 6u, &seen, sl, 16);
    /* pressed during step 3, the roll is quantised: it catches step 4's slice and repeats it */
    for (i = 2; i < seen; i++)
        if (sl[i] != sl[1])
            ok = 0;
    CHECK(ok && sl[1] == 4, "ROLL catches the next step's slice and holds it (%d)", sl[1]);
    brk_pad(PAD_ROLL16, 0);
    render(buf, brk_step_len_samples() * 2u, &seen, sl, 16);
    CHECK(seen >= 2 && sl[1] == (int32_t)((3u + 6u + 1u) % 16u), "and rejoins in phase (got %d)", sl[1]);
    /* a tape stop is silent after a beat */
    fresh(1);
    brk_play(1);
    render(buf, brk_step_len_samples() * 2u, 0, 0, 0);
    brk_pad(PAD_TAPESTOP, 1);
    render(buf, brk_step_len_samples() * 4u + 64u, 0, 0, 0);
    peak = render(buf, BLOCK, 0, 0, 0);
    CHECK(peak < 0.01f, "TAPE STOP: silent after a beat (peak %.3f)", peak);
    brk_pad(PAD_TAPESTOP, 0);
    peak = render(buf, brk_step_len_samples() * 3u, 0, 0, 0);
    CHECK(peak > 0.05f, "and back on release (peak %.3f)", peak);
    /* the mangled bounce, for the ears */
    {
        uint32_t s = brk_step_len_samples(), at = 0;
        fresh(1);
        brk_play(1);
        at += 16u * s; render(buf, 16u * s, 0, 0, 0);
        brk_pad(PAD_ROLL32, 1); render(buf + at, 4u * s, 0, 0, 0); at += 4u * s; brk_pad(PAD_ROLL32, 0);
        render(buf + at, 4u * s, 0, 0, 0); at += 4u * s;
        brk_pad(PAD_REVERSE, 1); render(buf + at, 4u * s, 0, 0, 0); at += 4u * s; brk_pad(PAD_REVERSE, 0);
        render(buf + at, 4u * s, 0, 0, 0); at += 4u * s;
        brk_pad(PAD_TAPESTOP, 1); render(buf + at, 8u * s, 0, 0, 0); at += 8u * s; brk_pad(PAD_TAPESTOP, 0);
        render(buf + at, 8u * s, 0, 0, 0); at += 8u * s;
        brk_pad(PAD_HALF, 1); render(buf + at, 8u * s, 0, 0, 0); at += 8u * s; brk_pad(PAD_HALF, 0);
        brk_pad(PAD_GATE, 1); render(buf + at, 8u * s, 0, 0, 0); at += 8u * s; brk_pad(PAD_GATE, 0);
        wav("pads.wav", buf, 0, at);
    }
}

static void fx_run(const brk_fx_t *p, uint32_t n)
{
    uint32_t i;
    brk_fx_init();
    brk_fx_set(p);
    for (i = 0; i < n; i += BLOCK)
        brk_fx_process(buf + i, 0, 0, 0, 0, bl + i, br + i, n - i < BLOCK ? n - i : BLOCK, 0u);
}

static void test_fx(void)
{
    brk_fx_t p;
    uint32_t n = brk_step_len_samples() * 32u, i, ok = 1, t, silent = 0, same = 1;
    float peak, diff = 0.0f;
    static float ref[BRK_SR * 2u];
    fresh(1);
    brk_play(1);
    render(buf, n, 0, 0, 0);
    for (t = 1; t <= BRK_NFXT; t++) {    /* each effect alone in slot 1, then the default chain all on */
        brk_fx_defaults(&p);
        if (t < BRK_NFXT) {
            brk_fx_slot_defaults(&p.slot[0], (int)t);
            p.slot[0].on = 1u;
        } else
            for (i = 0; i < BRK_FX_SLOTS; i++)
                p.slot[i].on = 1u;
        fx_run(&p, n);
        for (i = 0, peak = 0.0f; i < n; i++) {
            if (!(fabsf(bl[i]) <= 1.0f) || !(fabsf(br[i]) <= 1.0f))
                ok = 0;
            if (fabsf(bl[i]) > peak)
                peak = fabsf(bl[i]);
        }
        if (peak < 0.01f)
            silent |= 1u << t;
        if (t == BRK_NFXT)
            wav("fx_chain.wav", bl, br, n);
    }
    CHECK(ok, "FX output stays finite and inside -1..1, every effect");
    CHECK(!silent, "no effect silences the break (mask %x)", silent);
    /* the order matters: DRIVE then a low-pass is not a low-pass then DRIVE */
    brk_fx_defaults(&p);
    brk_fx_slot_defaults(&p.slot[0], FXT_DRIVE);
    brk_fx_slot_defaults(&p.slot[1], FXT_FILTER);
    p.slot[0].p[0] = 1.0f;
    p.slot[0].on = p.slot[1].on = 1u;
    fx_run(&p, BRK_SR);
    for (i = 0; i < BRK_SR; i++)
        ref[i] = bl[i];
    p.slot[0] = p.slot[1];
    brk_fx_slot_defaults(&p.slot[1], FXT_DRIVE);
    p.slot[1].p[0] = 1.0f;
    p.slot[1].on = 1u;
    fx_run(&p, BRK_SR);
    for (i = 0; i < BRK_SR; i++)
        diff += fabsf(bl[i] - ref[i]);
    CHECK(diff / (float)BRK_SR > 0.003f, "the chain's order matters (mean difference %.4f)", diff / (float)BRK_SR);
    /* FX 0: the whole chain against the dry break, all dry */
    brk_fx_defaults(&p);
    fx_run(&p, BRK_SR);
    for (i = 0; i < BRK_SR; i++)
        ref[i] = bl[i];
    for (i = 0; i < BRK_FX_SLOTS; i++)
        p.slot[i].on = 1u;
    p.wet = 0.0f;
    fx_run(&p, BRK_SR);
    for (i = 0; i < BRK_SR; i++)
        if (fabsf(bl[i] - ref[i]) > 1e-4f)
            same = 0;
    CHECK(same, "FX 0: the chain on but all dry");
}

/* an IMA ADPCM encoder for the test only (tools/prep_breaks.py has the real one) */
static const int16_t STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
    55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t ADJ[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static void enc(const int16_t *in, uint8_t *out, uint32_t n)
{
    int32_t pred = 0, idx = 0;
    uint32_t i;
    memset(out, 0, (n + 1u) / 2u);
    for (i = 0; i < n; i++) {
        int32_t d = in[i] - pred, step = STEP[idx], code = 0, diff = step >> 3;
        if (d < 0) { code = 8; d = -d; }
        if (d >= step) { code |= 4; d -= step; diff += step; }
        if (d >= step >> 1) { code |= 2; d -= step >> 1; diff += step >> 1; }
        if (d >= step >> 2) { code |= 1; diff += step >> 2; }
        pred += (code & 8) ? -diff : diff;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += ADJ[code & 7];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        out[i >> 1] |= (uint8_t)(code << ((i & 1u) * 4u));
    }
}

static void test_adpcm(void)
{
    static int16_t dec[40000];
    static uint8_t a[20000];
    adpcm_state_t st = {0, 0, 0};
    uint32_t n = brk_synth_len(RATE, BPM, 1), i;
    double sig = 0.0, err = 0.0, snr;
    fresh(1);
    if (n > 40000u)
        n = 40000u;
    enc(pcm, a, n);
    adpcm_decode(&st, a, dec, n);
    for (i = 0; i < n; i++) {
        sig += (double)pcm[i] * pcm[i];
        err += (double)(pcm[i] - dec[i]) * (pcm[i] - dec[i]);
    }
    snr = 10.0 * log10(sig / (err + 1.0));
    CHECK(snr > 20.0, "ADPCM round trip of the break: %.1f dB SNR", snr);
    CHECK(n / 2u < 22000u, "a bar at 22.05 kHz is %u bytes of ADPCM", n / 2u);
}

/* the pads' settings, tuned while held (brk_padset) */
static void test_pad_settings(void)
{
    brk_padset_t ps;
    brk_status_t st;
    uint32_t s = brk_step_len_samples(), i;
    float pk[4], peak;
    int32_t seen, sl[16];
    /* roll rate: 1/4 repeats far less often than 1/32 in the same time */
    uint32_t n4, n32;
    fresh(1);
    brk_padset_defaults(&ps);
    ps.roll_div[0] = 0u;                 /* R8 pad set to 1/4 */
    brk_padset(&ps);
    brk_play(1);
    render(buf, s * 2u + 10u, 0, 0, 0);
    brk_pad(PAD_ROLL8, 1);
    render(buf, s * 16u, 0, 0, 0);
    brk_status(&st);
    n4 = st.roll_n;
    brk_pad(PAD_ROLL8, 0);
    ps.roll_div[0] = 3u;                 /* now 1/32 */
    brk_padset(&ps);
    render(buf, s * 2u, 0, 0, 0);
    brk_pad(PAD_ROLL8, 1);
    render(buf, s * 16u, 0, 0, 0);
    brk_status(&st);
    n32 = st.roll_n;
    brk_pad(PAD_ROLL8, 0);
    CHECK(n4 >= 3u && n4 <= 4u && n32 >= 30u, "roll rate follows K1: 1/4 gave %u repeats, 1/32 gave %u", n4, n32);
    /* roll decay: each repeat quieter */
    fresh(1);
    brk_padset_defaults(&ps);
    ps.roll_div[1] = 1u;                 /* R16 pad at 1/8: one repeat every two steps */
    ps.roll_decay = 0.5f;
    brk_padset(&ps);
    brk_play(1);
    render(buf, s * 2u + 10u, 0, 0, 0);
    brk_pad(PAD_ROLL16, 1);
    render(buf, s, 0, 0, 0);             /* to the roll's first hit */
    for (i = 0; i < 4u; i++)
        pk[i] = render(buf, s * 2u, 0, 0, 0);
    brk_pad(PAD_ROLL16, 0);
    CHECK(pk[1] > pk[0] * 0.9f && pk[2] < pk[1] * 0.8f && pk[3] < pk[2] * 0.8f,
          "roll decay: the first repeat at the level, then quieter: %.3f %.3f %.3f %.3f", pk[0], pk[1], pk[2], pk[3]);
    /* roll level: ghost notes, a quarter as loud; and a swell (decay < 0) from there up */
    {
        float full = pk[1], g[4];
        fresh(1);
        brk_padset_defaults(&ps);
        ps.roll_div[1] = 1u;
        ps.roll_level = 25u;
        brk_padset(&ps);
        brk_play(1);
        render(buf, s * 2u + 10u, 0, 0, 0);
        brk_pad(PAD_ROLL16, 1);
        render(buf, s, 0, 0, 0);
        for (i = 0; i < 2u; i++)
            g[i] = render(buf, s * 2u, 0, 0, 0);
        brk_pad(PAD_ROLL16, 0);
        CHECK(g[1] > full * 0.18f && g[1] < full * 0.32f, "roll level 25%%: the repeats a quarter as loud (%.3f of %.3f)",
              g[1], full);
        fresh(1);
        ps.roll_decay = -0.5f;
        brk_padset(&ps);
        brk_play(1);
        render(buf, s * 2u + 10u, 0, 0, 0);
        brk_pad(PAD_ROLL16, 1);
        render(buf, s, 0, 0, 0);
        for (i = 0; i < 4u; i++)
            g[i] = render(buf, s * 2u, 0, 0, 0);
        brk_pad(PAD_ROLL16, 0);
        CHECK(g[1] < full * 0.32f && g[2] > g[1] * 1.3f && g[3] > g[2] * 1.1f,
              "roll swell: from the level up: %.3f %.3f %.3f", g[1], g[2], g[3]);
        brk_padset_defaults(&ps);
        brk_padset(&ps);
    }
    /* tape stop time: a quarter beat stops within two steps */
    fresh(1);
    brk_padset_defaults(&ps);
    ps.stop_beats = 0.25f;
    brk_padset(&ps);
    brk_play(1);
    render(buf, s * 2u, 0, 0, 0);
    brk_pad(PAD_TAPESTOP, 1);
    render(buf, s + 64u, 0, 0, 0);
    peak = render(buf, BLOCK, 0, 0, 0);
    brk_pad(PAD_TAPESTOP, 0);
    CHECK(peak < 0.01f, "tape stop at 1/4 beat: silent after a step (peak %.3f)", peak);
    /* gate length: 10% open is mostly silence */
    {
        uint32_t loud = 0u, n = s * 8u;
        fresh(1);
        brk_padset_defaults(&ps);
        ps.gate_len = 0.1f;
        brk_padset(&ps);
        brk_play(1);
        render(buf, s, 0, 0, 0);
        brk_pad(PAD_GATE, 1);
        render(buf, n, 0, 0, 0);
        for (i = 0; i < n; i++)
            if (fabsf(buf[i]) > 0.02f)
                loud++;
        brk_pad(PAD_GATE, 0);
        CHECK(loud < n / 4u, "gate 10%%: %u of %u samples sound", loud, n);
    }
    /* random amount 0: the loop as it is */
    {
        int ok = 1;
        fresh(1);
        brk_padset_defaults(&ps);
        ps.rnd_amount = 0.0f;
        brk_padset(&ps);
        brk_play(1);
        brk_pad(PAD_RANDOM, 1);
        render(buf, s * 8u, &seen, sl, 16);
        for (i = 0; i < (uint32_t)seen && i < 8u; i++)
            if (sl[i] != (int32_t)i)
                ok = 0;
        brk_pad(PAD_RANDOM, 0);
        CHECK(ok, "random at 0%% plays the loop");
    }
}

/* the dub delay: echoes ring on after the pad and the break stop */
static void test_dub(void)
{
    static int16_t line[32768];
    brk_fx_t p;
    uint32_t s = brk_step_len_samples(), n = s * 4u, i;
    float tail = 0.0f;
    fresh(1);
    brk_fx_init();
    brk_fx_defaults(&p);
    brk_fx_set(&p);
    brk_fx_delay_buffer(line, 32768u);
    brk_fx_tempo(BPM);
    brk_play(1);
    render(buf, n, 0, 0, 0);
    for (i = 0; i < n; i += BLOCK)
        brk_fx_process(buf + i, 0, 0, 0, 0, bl + i, br + i, n - i < BLOCK ? n - i : BLOCK, 1u << PAD_DUB);
    brk_play(0);
    brk_pad(PAD_DUB, 0);
    for (i = 0; i < BLOCK; i++)
        buf[i] = 0.0f;
    for (i = 0; i < s * 8u; i += BLOCK) {    /* silence in, two bars of it */
        uint32_t j;
        brk_fx_process(buf, 0, 0, 0, 0, bl, br, BLOCK, 0u);
        for (j = 0; j < BLOCK; j++)
            if (fabsf(bl[j]) > tail)
                tail = fabsf(bl[j]);
    }
    CHECK(tail > 0.01f, "dub echoes ring on after the pad (peak %.3f)", tail);
    brk_fx_delay_buffer(0, 0u);
}

/* the built-in styles: each one loads, has 16 slices and plays them in order straight */
static void test_styles(void)
{
    uint32_t k, bad = 0u;
    for (k = 0; k < brk_synth_count(); k++) {
        int32_t seen, sl[20], i, ok = 1;
        brk_init();
        brk_synth_style(pcm, RATE, k, &brk);
        brk_set_break(&brk);
        brk_tempo((float)brk.bpm_x100 / 100.0f);
        brk_play(1);
        render(buf, brk_step_len_samples() * 16u, &seen, sl, 20);
        for (i = 0; i < 16 && i < seen; i++)
            if (sl[i] != i)
                ok = 0;
        if (!ok || seen < 16 || brk.nslices != 16u || brk.len > 81920u)
            bad++;
    }
    CHECK(!bad && brk_synth_count() >= 5u, "%u styles, %u bad", brk_synth_count(), bad);
}

/* the soundboard: every sound sounds, stays inside -1..1 and ends; the siren holds until let go */

/* the pull-up: the break spins back, then starts again from step 0 */

/* the soundboard through the dub delay: echoes after the sound has stopped */

/* the white keys' own loops (brk_keyset): length, ping-pong, one shot, pitch, and the loop back after */
/* the split (PRD S-030 to S-032): a 2-bar break of 16 slices, the first 8 bar 1 (break A), the rest bar 2 (break B) */
static void split_fresh(void)
{
    fresh(1);
    brk.bars = 2u;                       /* (the synth's bar of 16 slices played as 2 bars: a slice an 8th) */
    brk.split = 8u;
    brk_set_break(&brk);
}
static void steps_over(uint32_t n, int32_t *lo, int32_t *hi)   /* the steps seen in n frames: their range */
{
    uint32_t i = 0;
    *lo = 99;
    *hi = -1;
    while (i < n) {
        brk_status_t st;
        brk_render(buf, BLOCK);
        brk_status(&st);
        if (st.step >= 0 && st.step < *lo)
            *lo = st.step;
        if (st.step > *hi)
            *hi = st.step;
        i += BLOCK;
    }
}
static float rms_of(const float *x, uint32_t n)
{
    double a = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++)
        a += (double)x[i] * (double)x[i];
    return (float)sqrt(a / (double)n);
}
static void test_split(void)
{
    uint32_t sl = brk_step_len_samples(), i;
    int32_t lo, hi, seen, sls[64], ok;
    brk_status_t st;
    float both, one;
    /* one half alone: from the next bar line the loop rounds bar 1 only, its slices those of the first half */
    split_fresh();
    brk_play(1);
    brk_half(0);
    render(buf, sl * 16u, 0, 0, 0);      /* (to the first bar line) */
    steps_over(sl * 48u, &lo, &hi);
    CHECK(lo == 0 && hi == 15, "HALF 1: three bars on steps %d..%d (0..15)", lo, hi);
    render(buf, sl * 32u, &seen, sls, 64);
    for (ok = 1, i = 1; i < (uint32_t)seen; i++)
        if (sls[i] < 0 || sls[i] >= 8)
            ok = 0;
    CHECK(ok && seen > 20, "and only the first half's slices sound");
    /* the second half asked for mid-bar: it waits for the bar line */
    render(buf, sl * 4u + sl / 2u, 0, 0, 0);
    brk_status(&st);
    brk_half(1);
    {
        int32_t at = st.step;
        steps_over(sl * (uint32_t)(15 - at), &lo, &hi);
        CHECK(hi <= 15, "HALF 2 asked mid-bar (step %d): bar 1 plays out (up to %d)", at, hi);
    }
    render(buf, sl, 0, 0, 0);            /* (over the bar line) */
    steps_over(sl * 33u, &lo, &hi);
    CHECK(lo == 16 && hi == 31, "then bar 2 alone: steps %d..%d (16..31)", lo, hi);
    brk_status(&st);
    CHECK(st.half == 1, "the status says half 2 (%d)", st.half);
    /* the generator's slices fold into the half */
    {
        brk_gen_t g = {0.9f, 0.0f, 0.0f, 0.0f, 0u};
        brk_gen(&g);
        render(buf, sl * 64u, &seen, sls, 64);
        for (ok = 1, i = 1; i < (uint32_t)seen; i++)
            if (sls[i] >= 0 && sls[i] < 8)
                ok = 0;
        CHECK(ok && seen > 20, "COMPLEXITY's jumps stay in half 2");
        g.complexity = 0.0f;
        brk_gen(&g);
    }
    /* both again */
    brk_half(-1);
    steps_over(sl * 80u, &lo, &hi);
    CHECK(lo == 0 && hi == 31, "both halves again: steps %d..%d", lo, hi);
    /* no split: the half asked for does nothing */
    fresh(1);
    brk.bars = 2u;
    brk_set_break(&brk);
    brk_play(1);
    brk_half(0);
    steps_over(sl * 80u, &lo, &hi);
    CHECK(lo == 0 && hi == 31, "no split: HALF ignored (steps %d..%d)", lo, hi);

    /* layers, stopped: a key in each half sounds together */
    split_fresh();
    brk_key(2, 1);
    render(buf, sl * 2u, 0, 0, 0);
    one = rms_of(buf, sl * 2u);
    brk_key(11, 1);
    render(buf, sl * 2u, 0, 0, 0);
    both = rms_of(buf, sl * 2u);
    brk_status(&st);
    CHECK(st.key == 2 && st.layer == 11, "a key in each half: key %d loops, %d layers", st.key, st.layer);
    CHECK(both > one * 1.15f, "and both sound (rms %.4f, one alone %.4f)", both, one);
    CHECK(st.layer_lo == (float)brk.slice[11] && st.layer_pos >= 0.0f, "the layer loops its own window (from %.0f)",
          st.layer_lo);
    brk_key(5, 1);                       /* the first half again: it takes over that half, the layer stays */
    render(buf, sl, 0, 0, 0);
    brk_status(&st);
    CHECK(st.key == 5 && st.layer == 11, "a second key in half 1 takes it over (key %d, layer %d)", st.key, st.layer);
    brk_key(13, 1);
    render(buf, sl, 0, 0, 0);
    brk_status(&st);
    CHECK(st.key == 5 && st.layer == 13, "and one in half 2 the layer (key %d, layer %d)", st.key, st.layer);
    brk_key(13, 0);
    render(buf, sl, 0, 0, 0);
    brk_status(&st);
    CHECK(st.layer == 11, "the layer let go: 11, still held, takes it again (%d)", st.layer);
    brk_key(11, 0);
    brk_key(5, 0);
    brk_key(2, 0);
    render(buf, sl * 4u, 0, 0, 0);
    brk_status(&st);
    CHECK(st.key == -1 && st.layer == -1 && render(buf, BLOCK, 0, 0, 0) == 0.0f, "all let go: quiet");
    /* playing: the layer lands on the snap too, over the key looping */
    split_fresh();
    brk_play(1);
    render(buf, sl / 2u, 0, 0, 0);
    brk_key(1, 1);
    render(buf, sl * 2u, 0, 0, 0);
    brk_key(12, 1);
    brk_status(&st);
    CHECK(st.layer == -1, "playing: the layer waits for its snap");
    render(buf, sl * 3u, 0, 0, 0);
    brk_status(&st);
    CHECK(st.key == 1 && st.layer == 12, "then layers (key %d, layer %d)", st.key, st.layer);
    brk_key(1, 0);
    render(buf, sl * 3u, 0, 0, 0);
    brk_status(&st);
    CHECK(st.key == -1 && st.layer == 12, "the key let go, the layer loops on over the loop (%d)", st.layer);
    brk_key(12, 0);
    /* no split: the last key wins, as ever */
    fresh(1);
    brk_key(2, 1);
    brk_key(11, 1);
    render(buf, sl, 0, 0, 0);
    brk_status(&st);
    CHECK(st.key == 11 && st.layer == -1, "no split: the last key wins (key %d, layer %d)", st.key, st.layer);
    brk_key(2, 0);
    brk_key(11, 0);
}

static void test_key_loops(void)
{
    brk_keyset_t k;
    brk_status_t st;
    uint32_t sl = brk_step_len_samples(), i;
    float peak, p0;
    int rev_seen[2] = {0, 0};
    /* 4 slices (= 4 16ths of a 1-bar break), stopped: it restarts every 4 steps on the tempo */
    fresh(1);
    brk_keyset_defaults(&k);
    k.len = 5u;
    brk_keyset(2u, &k);
    brk_key(2, 1);
    render(buf, sl * 9u / 2u, 0, 0, 0);
    brk_status(&st);
    CHECK(st.key == 2 && st.key_n == 1u, "a held key loops (key %d, %u repeats after 4.5 steps)", st.key, st.key_n);
    CHECK(st.key_phase > 0.05f && st.key_phase < 0.25f, "and is an eighth into its second repeat (%.2f)", st.key_phase);
    CHECK(st.slice == 2 && st.pos >= (float)brk.slice[2] && st.pos < (float)brk.slice[6], "inside its 4-slice window");
    brk_key(2, 0);
    render(buf, sl * 5u, 0, 0, 0);
    brk_status(&st);
    CHECK(st.key == -1 && render(buf, BLOCK, 0, 0, 0) == 0.0f, "let go (stopped): it ends with its window");
    /* the jungle chops on a 2-bar break of 16 slices (an 8th each, as the bank's): 1.5 slices restart every 3 16ths
     * (3/16), 3 every 6 (3/8) */
    {
        static const uint8_t LEN[2] = {8u, 9u};
        static const uint32_t N[2] = {2u, 1u};
        static const float PH[2] = {1.0f / 3.0f, 1.0f / 6.0f};
        for (i = 0; i < 2u; i++) {
            fresh(1);
            brk.bars = 2u;               /* (the synth's bar of 16 slices played as 2 bars: a slice an 8th) */
            brk_set_break(&brk);
            brk_keyset_defaults(&k);
            k.len = LEN[i];
            brk_keyset(3u, &k);
            brk_key(3, 1);
            render(buf, sl * 7u, 0, 0, 0);
            brk_status(&st);
            CHECK(st.key == 3 && st.key_n == N[i] && fabsf(st.key_phase - PH[i]) < 0.06f,
                  "length %s: %u repeats in 7 16ths, %.2f into the next", i ? "3/8" : "3/16", st.key_n, st.key_phase);
            brk_key(3, 0);
        }
    }
    /* ping-pong: every other repeat backwards */
    fresh(1);
    brk_keyset_defaults(&k);
    k.mode = KEY_PINGPONG;
    brk_keyset(5u, &k);
    brk_key(5, 1);
    for (i = 0; i < 6u; i++) {
        render(buf, sl / 2u, 0, 0, 0);
        brk_status(&st);
        rev_seen[st.key_rev ? 1 : 0] |= 1 << (st.key_n & 1u);
        render(buf, sl / 2u, 0, 0, 0);
    }
    CHECK(rev_seen[0] == 1 && rev_seen[1] == 2, "ping-pong: even repeats forwards, odd ones backwards (%d %d)",
          rev_seen[0], rev_seen[1]);
    brk_key(5, 0);
    /* one shot: once through, then quiet while still held */
    fresh(1);
    brk_keyset_defaults(&k);
    k.mode = KEY_ONESHOT;
    brk_keyset(4u, &k);
    brk_key(4, 1);
    peak = render(buf, sl, 0, 0, 0);
    render(buf, sl / 4u, 0, 0, 0);
    CHECK(peak > 0.05f && render(buf, sl * 2u, 0, 0, 0) < 0.001f, "one shot: plays (%.2f), then quiet while held", peak);
    k.mode = KEY_LOOP;                   /* turned back to a loop while held: it loops again */
    brk_keyset(4u, &k);
    render(buf, sl, 0, 0, 0);
    CHECK(render(buf, sl, 0, 0, 0) > 0.05f, "a one shot turned back to a loop while held loops again");
    brk_key(4, 0);
    /* pitch: +12 runs through the break twice as fast */
    fresh(1);
    brk_key(0, 1);
    render(buf, BLOCK, 0, 0, 0);
    brk_status(&st);
    p0 = st.pos;
    render(buf, BLOCK * 4u, 0, 0, 0);
    brk_status(&st);
    p0 = st.pos - p0;
    brk_key(0, 0);
    fresh(1);
    brk_keyset_defaults(&k);
    k.pitch = 12;
    brk_keyset(0u, &k);
    brk_key(0, 1);
    render(buf, BLOCK, 0, 0, 0);
    brk_status(&st);
    peak = st.pos;
    render(buf, BLOCK * 4u, 0, 0, 0);
    brk_status(&st);
    CHECK(fabsf((st.pos - peak) / p0 - 2.0f) < 0.05f, "a key's +12 plays at twice the speed (%.2f)", (st.pos - peak) / p0);
    brk_key(0, 0);
    /* playing: a whole-bar loop from slice 8 owns the voice; let go, the loop takes over at the next slice */
    {
        int32_t seen, sls[40], ok = 1;
        fresh(1);
        brk_keyset_defaults(&k);
        k.len = 7u;                      /* 16 slices: the whole bar */
        brk_keyset(8u, &k);
        brk_play(1);
        render(buf, sl / 2u, 0, 0, 0);
        brk_key(8, 1);
        render(buf, sl * 20u, &seen, sls, 40);
        brk_status(&st);
        for (i = 1; i < (uint32_t)seen; i++)
            if (sls[i] != 8)
                ok = 0;
        CHECK(ok && st.key == 8 && st.key_n == 1u, "playing, a 1-bar key loop holds for 20 steps (repeats %u)", st.key_n);
        brk_key(8, 0);
        render(buf, sl * 2u, &seen, sls, 40);
        brk_status(&st);
        CHECK(st.key == -1 && st.slice != 8, "let go, the loop plays on (slice %d)", st.slice);
    }
}

/* MIDI clock: 24 straight ticks a beat out (swing or not); in, the loop eases onto an outside clock's 16ths */
static void test_clock(void)
{
    uint16_t off[64];
    uint32_t sl = brk_step_len_samples(), t, n, total, k, last = 0, bad = 0;
    int pass;
    for (pass = 0; pass < 2; pass++) {
        fresh(1);
        brk_swing(pass ? 0.66f : 0.5f);
        brk_play(1);
        total = 0;
        for (t = 0; t < sl * 16u; t += 64u) {   /* one bar */
            brk_render(buf, 64);
            n = brk_clock_ticks(off, 64);
            for (k = 0; k < n; k++) {
                uint32_t at = t + off[k];
                if (at >= sl * 16u)
                    break;               /* (the next bar's first) */
                if (total && (at - last > sl / 6u + 2u || at - last < sl / 6u - 2u))
                    bad++;
                last = at;
                total++;
            }
        }
        CHECK(total == 96u && !bad, "clock out: %u ticks a bar (96), %u uneven%s", total, bad, pass ? ", with swing" : "");
    }
    /* in: the clock's 16ths begin 0.4 of a step after the loop's; synced every beat, the loop moves onto them */
    {
        uint32_t ext0 = sl * 2u / 5u, beat = sl * 4u, nb = 0;
        int32_t last_step = -9, d = 99999;
        brk_status_t st;
        fresh(1);
        brk_play(1);
        for (t = 0; t < beat * 12u; t += 16u) {
            if (t >= ext0 && (t - ext0) / beat >= nb) {   /* a beat of the outside clock began in the last block */
                uint32_t at = ext0 + nb * beat;
                brk_sync(nb * 4u, (float)(t - at));
                nb++;
            }
            brk_render(buf, 16);
            brk_status(&st);
            if (st.step != last_step) {
                last_step = st.step;
                if (t > beat * 10u) {    /* the loop's step boundary against the clock's nearest */
                    int32_t rel = (int32_t)((t + 16u - ext0) % sl);
                    if (rel > (int32_t)sl / 2)
                        rel -= (int32_t)sl;
                    if (rel < 0 ? -rel < (d < 0 ? -d : d) : rel < (d < 0 ? -d : d))
                        d = rel;
                }
            }
        }
        CHECK(d > -48 && d < 48, "clock in: the loop's steps land on the clock's (%d samples off, was %u)", d, ext0);
    }
    /* far off: it jumps to where the clock is */
    {
        brk_status_t st;
        fresh(1);
        brk_play(1);
        render(buf, sl * 3u + sl / 2u, 0, 0, 0);
        brk_sync(40u, 0.0f);
        render(buf, sl / 2u, 0, 0, 0);
        brk_status(&st);
        CHECK(st.step == 41 % 16, "clock in, far off: the loop jumps to the clock's 16th (step %d, want %d)", st.step, 41 % 16);
    }
}

/* pitch, KEEP LENGTH: a 1 kHz tone pitched +12 plays at 2 kHz, but the voice runs at its own speed */
static void test_keep_length(void)
{
    static brk_break_t tone;
    uint32_t i, n = 22050u, zc[2], mode;
    float adv[2];
    for (i = 0; i < n; i++)
        pcm[i] = (int16_t)(12000.0f * sinf(6.2831853f * (float)i * (1000.0f / 22050.0f)));
    for (mode = 0; mode < 2u; mode++) {
        brk_status_t st;
        float p0;
        uint32_t k;
        brk_init();
        tone.pcm = pcm;
        tone.len = n;
        tone.rate = 22050u;
        tone.bpm_x100 = 12000u;
        tone.bars = 1u;
        tone.nslices = 1u;
        tone.slice[0] = 0u;
        tone.slice[1] = n;
        brk_set_break(&tone);
        brk_tempo(120.0f);
        brk_pitch(12.0f);
        brk_pitch_mode((int)mode, 30.0f);
        brk_key(0, 1);
        render(buf, 4096u, 0, 0, 0);
        brk_status(&st);
        p0 = st.pos;
        render(buf, 8192u, 0, 0, 0);
        brk_status(&st);
        adv[mode] = (st.pos - p0) / (8192.0f * 0.5f);   /* 1: the voice at its own speed (22.05 kHz into 44.1) */
        for (k = 1, zc[mode] = 0; k < 8192u; k++)
            if ((buf[k - 1] < 0.0f) != (buf[k] < 0.0f))
                zc[mode]++;
        brk_key(0, 0);
    }
    brk_pitch(0.0f);
    brk_pitch_mode(0, 30.0f);
    CHECK(adv[0] > 1.9f && adv[0] < 2.1f && zc[0] > 700u && zc[0] < 790u,
          "pitch SPEED: +12 runs twice as fast (%.2f) at 2 kHz (%u crossings)", adv[0], zc[0]);
    CHECK(adv[1] > 0.95f && adv[1] < 1.05f && zc[1] > 690u && zc[1] < 800u,
          "pitch KEEP LENGTH: +12 runs at its own speed (%.2f) but sounds at 2 kHz (%u crossings)", adv[1], zc[1]);
}

/* the gate: at 25 % each slice of the loop sounds for a quarter of its length, then it is quiet until the next */
static void test_gate(void)
{
    uint32_t sl, i;
    float early[2], late[2];
    int g;
    for (g = 0; g < 2; g++) {
        fresh(1);                        /* 1 bar, 16 slices: a slice a step */
        sl = brk_step_len_samples();     /* (at fresh's tempo) */
        brk_gate(g ? 0.25f : 1.0f);
        brk_play(1);
        render(buf, sl * 4u, 0, 0, 0);   /* into step 4 */
        early[g] = late[g] = 0.0f;
        for (i = 0; i < 4u; i++) {       /* in each of the next steps: its first fifth, and its second half */
            render(buf, sl, 0, 0, 0);
            {
                uint32_t k;
                for (k = 0; k < sl / 5u; k++)
                    if (fabsf(buf[k]) > early[g])
                        early[g] = fabsf(buf[k]);
                for (k = sl / 2u; k < sl; k++)
                    if (fabsf(buf[k]) > late[g])
                        late[g] = fabsf(buf[k]);
            }
        }
    }
    brk_gate(1.0f);
    CHECK(early[1] > early[0] * 0.8f && late[1] < 0.002f && late[0] > 0.01f,
          "gate 25%%: each slice's start sounds (%.3f), its second half is quiet (%.4f; ungated %.3f)",
          early[1], late[1], late[0]);
}

/* a break change (ALGORITHM): muted, the break runs on silent; the next one swapped in waits for the next step,
 * then sounds; stopped, it is in at once */
static void test_swap(void)
{
    static int16_t pcm2[200000];
    static brk_break_t b2;
    uint32_t sl;
    float pk;
    fresh(1);
    brk_synth_break(pcm2, RATE, BPM, 1u, &b2);
    sl = brk_step_len_samples();
    brk_play(1);
    render(buf, sl * 3u + sl / 4u, 0, 0, 0);     /* a quarter into step 3 */
    brk_mute(1);
    render(buf, 512u, 0, 0, 0);                   /* (the fade: two renders) */
    pk = render(buf, sl / 4u, 0, 0, 0);
    CHECK(pk < 1e-4f, "muted: silent (%.5f)", pk);
    brk_swap_break(&b2);
    render(buf, sl / 8u, 0, 0, 0);
    CHECK(brk_get_break() == &brk, "the new break waits for the next step");
    pk = render(buf, sl, 0, 0, 0);                /* past step 4's start */
    CHECK(brk_get_break() == &b2 && pk > 0.01f, "in on the next step, and sounding (%.3f)", pk);
    brk_play(0);
    render(buf, 1024u, 0, 0, 0);
    brk_swap_break(&brk);
    render(buf, 256u, 0, 0, 0);
    CHECK(brk_get_break() == &brk, "stopped: in at once");
}

/* the snap: where a key pressed while playing begins. A 1-bar break of 16 slices: a slice is a 16th */
static void test_snap(void)
{
    uint32_t sl, t;
    int32_t at;
    brk_status_t st;
    /* 2 slices: pressed in step 0, it waits for step 2 (not 1) */
    fresh(1);
    sl = brk_step_len_samples();
    brk_snap(2.0f);
    brk_play(1);
    render(buf, sl / 2u, 0, 0, 0);
    brk_key(9, 1);
    brk_key(9, 0);
    render(buf, sl, 0, 0, 0);            /* well into step 1 */
    brk_status(&st);
    CHECK(st.step == 1 && st.slice == 1, "snap 2 slices: not yet at step 1 (slice %d)", st.slice);
    render(buf, sl, 0, 0, 0);            /* into step 2 */
    brk_status(&st);
    CHECK(st.step == 2 && st.slice == 9, "snap 2 slices: the key begins at step 2 (slice %d)", st.slice);
    /* OFF: at once, mid-step; and a quarter slice: a quarter step on */
    {
        float q[2] = {0.0f, 0.25f};
        int k;
        for (k = 0; k < 2; k++) {
            fresh(1);
            sl = brk_step_len_samples();
            brk_snap(q[k]);
            brk_play(1);
            render(buf, sl + sl / 10u, 0, 0, 0);   /* a tenth into step 1 */
            brk_key(12, 1);
            for (t = 0, at = -1; t < sl; t += 16u) {
                brk_render(buf, 16);
                brk_status(&st);
                if (st.slice == 12) {
                    at = (int32_t)t;
                    break;
                }
            }
            brk_key(12, 0);
            if (k == 0)
                CHECK(at >= 0 && at < 64, "snap OFF: the key at once (%d samples)", at);
            else
                CHECK(at > (int32_t)(sl * 3u / 20u) - 64 && at < (int32_t)(sl * 3u / 20u) + 64,
                      "snap 1/4 slice: the key a quarter step in (%d samples, want %u)", at, sl * 3u / 20u);
        }
    }
    brk_snap(1.0f);
}

/* the reverb: an impulse rings on, decays, comes out different left and right; a bigger size rings longer; a second
 * REVERB slot passes the sound on */
static float rv_energy(float size, uint32_t from, uint32_t to, int two, float *corr)
{
    brk_fx_t p;
    uint32_t i, n = BRK_SR * 4u;
    float e = 0.0f, lr = 0.0f, ll = 0.0f, rr = 0.0f;
    for (i = 0; i < n; i++)
        buf[i] = i == 100u ? 1.0f : 0.0f;
    brk_fx_defaults(&p);
    brk_fx_slot_defaults(&p.slot[0], FXT_REVERB);
    p.slot[0].on = 1u;
    p.slot[0].mix = 1.0f;
    p.slot[0].p[0] = size;
    p.slot[0].p[2] = 0.0f;
    if (two) {
        p.slot[2] = p.slot[0];
    }
    fx_run(&p, n);
    for (i = from; i < to; i++) {
        e += bl[i] * bl[i] + br[i] * br[i];
        lr += bl[i] * br[i];
        ll += bl[i] * bl[i];
        rr += br[i] * br[i];
    }
    if (corr)
        *corr = lr / sqrtf(ll * rr + 1e-12f);
    return e;
}

static void test_reverb(void)
{
    float early, late, big_late, corr, one, two;
    early = rv_energy(0.6f, BRK_SR / 10u, BRK_SR / 2u, 0, &corr);
    late = rv_energy(0.6f, BRK_SR * 2u, BRK_SR * 3u, 0, 0);
    big_late = rv_energy(1.0f, BRK_SR * 2u, BRK_SR * 3u, 0, 0);
    CHECK(early > 1e-3f && late < early * 0.05f, "reverb: an impulse rings (%.4f) and dies away (%.6f 2 s on)", early, late);
    CHECK(corr < 0.5f && corr > -0.5f, "reverb: left and right differ (correlation %.2f)", corr);
    CHECK(big_late > late * 4.0f, "reverb: a bigger size rings longer (%.6f against %.6f 2 s on)", big_late, late);
    one = rv_energy(0.6f, 0u, BRK_SR, 0, 0);
    two = rv_energy(0.6f, 0u, BRK_SR, 1, 0);
    CHECK(fabsf(two - one) < one * 0.01f, "reverb: a second REVERB slot passes the sound on (%.4f, %.4f)", one, two);
    {   /* a listen: the break through the plate */
        brk_fx_t p;
        uint32_t n = BRK_SR * 4u;
        fresh(1);
        brk_play(1);
        render(buf, n, 0, 0, 0);
        brk_fx_defaults(&p);
        brk_fx_slot_defaults(&p.slot[0], FXT_REVERB);
        p.slot[0].on = 1u;
        p.slot[0].mix = 0.5f;
        fx_run(&p, n);
        wav("reverb.wav", bl, br, n);
    }
}

/* STOP: the break fades out at once (a key held loops on) */
static void test_stop(void)
{
    fresh(1);
    brk_play(1);
    render(buf, brk_step_len_samples() * 4u + 100u, 0, 0, 0);
    brk_play(0);
    render(buf, 441u, 0, 0, 0);          /* 10 ms */
    CHECK(render(buf, BLOCK * 4u, 0, 0, 0) == 0.0f, "stop: the break is quiet 10 ms on");
    fresh(1);
    brk_play(1);
    render(buf, brk_step_len_samples() * 2u, 0, 0, 0);
    brk_play(0);
    render(buf, BLOCK, 0, 0, 0);
    brk_key(5, 1);                       /* stopped, a key still plays and loops */
    render(buf, brk_step_len_samples() * 3u, 0, 0, 0);
    CHECK(render(buf, brk_step_len_samples(), 0, 0, 0) > 0.05f, "stop: a key pressed after still loops");
    brk_key(5, 0);
}

/* the DELAY slot: a click comes back on the left at the time, then on the right, ping-pong; and the slices' own
 * effects: a level of 0 silences that slice alone, a LO-PASS darkens it, an ECHO throws it alone into the delay */
static int16_t dly_line[32768];
static void test_delay_slices(void)
{
    brk_fx_t p;
    uint32_t i, n = BRK_SR * 2u, T = (uint32_t)(3.0f * 22050.0f * 60.0f / (170.0f * 4.0f)) * 2u, pkl = 0, pkr = 0;
    float ml = 0.0f, mr = 0.0f;
    brk_fx_init();
    brk_fx_delay_buffer(dly_line, 32768u);
    brk_fx_tempo(170.0f);
    for (i = 0; i < n; i++)
        buf[i] = i == 1000u ? 1.0f : 0.0f;
    brk_fx_defaults(&p);
    brk_fx_slot_defaults(&p.slot[0], FXT_DELAY);   /* 3/16 */
    p.slot[0].on = 1u;
    p.slot[0].mix = 1.0f;
    brk_fx_set(&p);
    for (i = 0; i < n; i += BLOCK)
        brk_fx_process(buf + i, 0, 0, 0, 0, bl + i, br + i, BLOCK, 0u);
    for (i = 1100u; i < 1000u + T + T / 2u; i++) {   /* the first echo: left only */
        if (fabsf(bl[i]) > ml) { ml = fabsf(bl[i]); pkl = i; }
        if (fabsf(br[i]) > mr) mr = fabsf(br[i]);
    }
    CHECK(ml > 0.02f && mr < ml * 0.1f && pkl > 1000u + T - 8u && pkl < 1000u + T + 8u,
          "delay: the first echo on the left at 3/16 (%u samples on, want %u; right %.3f)", pkl - 1000u, T, mr);
    ml = mr = 0.0f;
    for (i = 1000u + T + T / 2u; i < 1000u + 2u * T + T / 2u; i++) {   /* the second: right */
        if (fabsf(br[i]) > mr) { mr = fabsf(br[i]); pkr = i; }
        if (fabsf(bl[i]) > ml) ml = fabsf(bl[i]);
    }
    CHECK(mr > 0.02f && ml < mr * 0.1f && pkr > 1000u + 2u * T - 12u && pkr < 1000u + 2u * T + 12u,
          "delay: the second on the right, ping-pong (%u on)", pkr - 1000u);
    brk_fx_delay_buffer(0, 0u);
    /* the slices' own effects, through the engine */
    {
        brk_slicefx_t f;
        uint32_t sl, k, zc[2];
        float lv[2], e_on, e_off;
        static float ebuf[BRK_SR * 2u];
        for (k = 0; k < 2u; k++) {       /* level: slice 4 at 0 */
            fresh(1);
            sl = brk_step_len_samples();
            brk_slicefx_defaults(&f);
            f.level = k ? 0u : 100u;
            brk_slicefx(4u, &f);
            brk_play(1);
            render(buf, sl * 4u + sl / 10u, 0, 0, 0);
            lv[k] = render(buf, sl * 8u / 10u, 0, 0, 0);    /* inside step 4 */
        }
        CHECK(lv[0] > 0.05f && lv[1] < 0.001f, "slice level 0: that slice silent (%.3f, was %.3f)", lv[1], lv[0]);
        for (k = 0; k < 2u; k++) {       /* LO-PASS on slice 2 (a snare's hiss): fewer zero crossings */
            uint32_t j;
            fresh(1);
            sl = brk_step_len_samples();
            brk_slicefx_defaults(&f);
            f.type = k ? SLX_LOPASS : SLX_OFF;
            f.amount = 85u;
            brk_slicefx(2u, &f);
            brk_play(1);
            render(buf, sl * 2u + 64u, 0, 0, 0);
            render(buf, sl / 2u, 0, 0, 0);
            for (j = 1, zc[k] = 0; j < sl / 2u; j++)
                if ((buf[j - 1] < 0.0f) != (buf[j] < 0.0f))
                    zc[k]++;
        }
        CHECK(zc[1] < zc[0] / 2u, "slice LO-PASS: darker (%u crossings, was %u)", zc[1], zc[0]);
        /* ECHO on slice 0 only: its throw is there while slice 0 plays, and nothing while slice 1 does */
        fresh(1);
        sl = brk_step_len_samples();
        brk_slicefx_defaults(&f);
        f.type = SLX_ECHO;
        f.amount = 100u;
        brk_slicefx(0u, &f);
        brk_sends(ebuf, 0);
        brk_play(1);
        render(buf, sl / 2u, 0, 0, 0);
        for (i = 0, e_on = 0.0f; i < sl / 2u; i++)
            e_on += fabsf(ebuf[i]);
        render(buf, sl / 2u + 64u, 0, 0, 0);
        render(buf, sl / 2u, 0, 0, 0);
        for (i = 0, e_off = 0.0f; i < sl / 2u; i++)
            e_off += fabsf(ebuf[i]);
        brk_sends(0, 0);
        CHECK(e_on > 1.0f && e_off < 0.01f, "slice ECHO: slice 0 thrown (%.2f), slice 1 not (%.4f)", e_on, e_off);
    }
}

/* EQ: flat at 0.5; LOW +12 dB lifts 60 Hz four times, HIGH -12 dB drops 12 kHz to a quarter. TAPE: drive puts
 * harmonics on a pure tone; wow makes its pitch wander */
static float tone_peak_a(float hz, float amp, int type, float p0, float p1, float p2, float mix, uint32_t from)
{
    brk_fx_t p;
    uint32_t i, n = BRK_SR;
    float pk = 0.0f;
    for (i = 0; i < n; i++)
        buf[i] = amp * sinf(6.2831853f * hz * (float)i / (float)BRK_SR);
    brk_fx_defaults(&p);
    brk_fx_slot_defaults(&p.slot[0], type);
    p.slot[0].on = 1u;
    p.slot[0].p[0] = p0;
    p.slot[0].p[1] = p1;
    p.slot[0].p[2] = p2;
    p.slot[0].mix = mix;
    fx_run(&p, n);
    for (i = from; i < n; i++)
        if (fabsf(bl[i]) > pk)
            pk = fabsf(bl[i]);
    return pk;
}

static float tone_peak(float hz, int type, float p0, float p1, float p2, float mix, uint32_t from)
{
    return tone_peak_a(hz, 0.25f, type, p0, p1, p2, mix, from);
}

static void test_eq_tape(void)
{
    float flat = tone_peak(1000.0f, FXT_EQ, 0.5f, 0.5f, 0.5f, 0.5f, 4000u);
    float low = tone_peak(60.0f, FXT_EQ, 1.0f, 0.5f, 0.5f, 0.5f, 8000u);
    float high = tone_peak(12000.0f, FXT_EQ, 0.5f, 0.5f, 0.0f, 0.5f, 4000u);
    float mid = tone_peak(1000.0f, FXT_EQ, 0.5f, 1.0f, 0.5f, 0.5f, 4000u);
    CHECK(fabsf(flat - 0.25f) < 0.01f, "EQ flat: a 1 kHz tone through unchanged (%.3f of 0.25)", flat);
    CHECK(low > 0.85f && low < 1.15f, "EQ LOW +12 dB: 60 Hz four times (%.3f from 0.25)", low);
    CHECK(high > 0.05f && high < 0.08f, "EQ HIGH -12 dB: 12 kHz to a quarter (%.3f from 0.25)", high);
    CHECK(mid > 0.85f && mid < 1.15f, "EQ MID +12 dB at 1 kHz: four times (%.3f)", mid);
    {   /* TAPE: the drive's harmonics (what is left after a notch at the tone), and the wow's wandering */
        uint32_t i, last = 0, nz = 0;
        float lo = 1e9f, hi = 0.0f, res = 0.0f, x1 = 0.0f, x2 = 0.0f;
        float w0 = 6.2831853f * 1000.0f / (float)BRK_SR, cw = 2.0f * cosf(w0);
        tone_peak(1000.0f, FXT_TAPE, 1.0f, 0.0f, 0.0f, 1.0f, 0u);
        for (i = 4000u; i < BRK_SR; i++) {   /* a notch at 1 kHz: x[n] - 2cos(w) x[n-1] + x[n-2] */
            float y = bl[i] - cw * x1 + x2;
            x2 = x1;
            x1 = bl[i];
            if (i > 4010u && fabsf(y) > res)
                res = fabsf(y);
        }
        CHECK(res > 0.02f, "TAPE drive: harmonics on a pure tone (%.3f left past a notch at it)", res);
        tone_peak(1000.0f, FXT_TAPE, 0.0f, 0.0f, 1.0f, 1.0f, 0u);
        for (i = 4001u; i < BRK_SR; i++)  /* the periods between upward zero crossings */
            if (bl[i - 1] < 0.0f && bl[i] >= 0.0f) {
                float at = (float)(i - 1u) + bl[i - 1] / (bl[i - 1] - bl[i]);
                if (nz++) {
                    float per = at - (float)last * 0.001f;
                    if (per < lo) lo = per;
                    if (per > hi) hi = per;
                }
                last = (uint32_t)(at * 1000.0f);
            }
        CHECK(hi - lo > 0.15f, "TAPE wow: the pitch wanders (periods %.2f .. %.2f samples)", lo, hi);
    }
}

/* the DJ filter is the break's: a low-passed break goes dark while the kick joining through the chain and the
 * drums around it stay bright */
static void test_dj_break(void)
{
    static float tone[BLOCK], z[BLOCK];
    brk_fx_t p;
    uint32_t i, b;
    float pk[3];
    int which;
    for (i = 0; i < BLOCK; i++) {
        tone[i] = 0.25f * sinf(6.2831853f * 4000.0f * (float)i / (float)BRK_SR);
        z[i] = 0.0f;
    }
    for (which = 0; which < 3; which++) {   /* the tone as the break, as thru (drums through the chain), as post
                                             * (drums around it) */
        brk_fx_init();
        brk_fx_defaults(&p);
        p.dj = -1.0f;
        brk_fx_set(&p);
        pk[which] = 0.0f;
        for (b = 0; b < 60u; b++) {
            brk_fx_process(which == 0 ? tone : z, which == 1 ? tone : 0, 0, 0, which == 2 ? tone : 0, bl, br, BLOCK, 0u);
            if (b > 40u)
                for (i = 0; i < BLOCK; i++)
                    if (fabsf(bl[i]) > pk[which])
                        pk[which] = fabsf(bl[i]);
        }
    }
    CHECK(pk[0] < 0.01f && pk[1] > 0.2f && pk[2] > 0.2f,
          "DJ filter low-passed: the break %.3f, drums through the chain %.3f, drums around it %.3f", pk[0], pk[1],
          pk[2]);
}

/* MULTIBAND: the bands sum back to the input untouched; a band pressed holds its level, the others don't move; CLIP:
 * clean below its edge, held at its ceiling above */
static void test_mb_clip(void)
{
    float gr[3], lv[3];
    float f60 = tone_peak(60.0f, FXT_MBCOMP, 0.0f, 0.0f, 0.0f, 1.0f, 8000u);
    float f1k = tone_peak(1000.0f, FXT_MBCOMP, 0.0f, 0.0f, 0.0f, 1.0f, 4000u);
    float f8k = tone_peak(8000.0f, FXT_MBCOMP, 0.0f, 0.0f, 0.0f, 1.0f, 4000u);
    float lo_q, lo_l, k1;
    CHECK(fabsf(f60 - 0.25f) < 0.01f && fabsf(f1k - 0.25f) < 0.01f && fabsf(f8k - 0.25f) < 0.01f,
          "MULTIBAND at 0: the bands sum back (60 Hz %.3f, 1 kHz %.3f, 8 kHz %.3f of 0.25)", f60, f1k, f8k);
    lo_q = tone_peak_a(60.0f, 0.05f, FXT_MBCOMP, 1.0f, 0.0f, 0.0f, 1.0f, 30000u);
    lo_l = tone_peak_a(60.0f, 0.25f, FXT_MBCOMP, 1.0f, 0.0f, 0.0f, 1.0f, 30000u);
    brk_fx_mb_meter(0u, gr, lv);
    CHECK(lo_l / lo_q < 1.5f && lo_l / lo_q > 1.0f, "MULTIBAND LOW: 14 dB more in, %.1f dB more out", 
          20.0f * log10f(lo_l / lo_q));
    CHECK(gr[0] < 0.4f && gr[1] > 0.95f && gr[2] > 0.95f, "MULTIBAND LOW: its reduction shown (%.2f %.2f %.2f)",
          gr[0], gr[1], gr[2]);
    k1 = tone_peak(1000.0f, FXT_MBCOMP, 1.0f, 0.0f, 0.0f, 1.0f, 4000u);
    CHECK(fabsf(k1 - 0.25f) < 0.025f, "MULTIBAND LOW pressed: 1 kHz untouched (%.3f)", k1);
    k1 = tone_peak(1000.0f, FXT_CLIP, 0.0f, 0.5f, 0.0f, 1.0f, 4000u);
    CHECK(fabsf(k1 - 0.25f) < 0.01f, "CLIP at 0 dB in: clean (%.3f)", k1);
    k1 = tone_peak(1000.0f, FXT_CLIP, 1.0f, 0.0f, 0.5f, 1.0f, 4000u);
    CHECK(k1 > 0.48f && k1 < 0.51f, "CLIP +24 dB into -6 dB: held at the ceiling (%.3f)", k1);
    k1 = tone_peak(1000.0f, FXT_CLIP, 1.0f, 1.0f, 0.5f, 1.0f, 4000u);
    CHECK(k1 > 0.48f && k1 < 0.51f, "CLIP hard: held at the ceiling too (%.3f)", k1);
}

/* slot 0 emptied for a block: its state (the levels followed) from nothing for the next run */
static void mb_fresh(void)
{
    static float z[BLOCK], zl[BLOCK], zr[BLOCK];
    brk_fx_t e;
    brk_fx_defaults(&e);
    brk_fx_slot_defaults(&e.slot[0], FXT_EMPTY);
    brk_fx_init();
    brk_fx_set(&e);
    brk_fx_process(z, 0, 0, 0, 0, zl, zr, BLOCK, 0u);
}

/* MB COMP's detail (PRD A-49a): a tone through slot 0's MB COMP with the band amounts a[] and the detail m; the peak
 * from `from` on (from < to), its meter afterwards */
static float mb_peak(float hz, float amp, const float *a, const brk_mbc_t *m, uint32_t from, uint32_t to,
                     float *gr, float *lvl)
{
    brk_fx_t p;
    uint32_t i, n = BRK_SR;
    float pk = 0.0f;
    for (i = 0; i < n; i++)
        buf[i] = amp * sinf(6.2831853f * hz * (float)i / (float)BRK_SR);
    brk_fx_defaults(&p);
    brk_fx_slot_defaults(&p.slot[0], FXT_MBCOMP);
    p.slot[0].on = 1u;
    p.slot[0].p[0] = a[0];
    p.slot[0].p[1] = a[1];
    p.slot[0].p[2] = a[2];
    p.slot[0].mix = 1.0f;
    mb_fresh();
    brk_fx_set(&p);
    brk_fx_set_mbc(0u, m);
    for (i = 0; i < n; i += BLOCK)
        brk_fx_process(buf + i, 0, 0, 0, 0, bl + i, br + i, n - i < BLOCK ? n - i : BLOCK, 0u);
    for (i = from; i < to && i < n; i++)
        if (fabsf(bl[i]) > pk)
            pk = fabsf(bl[i]);
    if (gr && lvl)
        brk_fx_mb_meter(0u, gr, lvl);
    return pk;
}

static void test_mb_detail(void)
{
    static const float none[3] = {0.0f, 0.0f, 0.0f}, low[3] = {1.0f, 0.0f, 0.0f}, all[3] = {1.0f, 1.0f, 1.0f};
    brk_mbc_t m, d;
    float gr[3], lvl[3], a, b, q, l;
    brk_mbc_defaults(&d);
    /* the defaults are what MB COMP was: the old test's numbers through the new path */
    q = mb_peak(60.0f, 0.05f, low, &d, 30000u, BRK_SR, 0, 0);
    l = mb_peak(60.0f, 0.25f, low, &d, 30000u, BRK_SR, gr, lvl);
    CHECK(l / q < 1.5f && l / q > 1.0f && gr[0] < 0.4f && lvl[0] > 0.2f && lvl[2] < 0.01f,
          "MB detail defaults: 14 dB more in, %.1f dB out; low reduced to %.2f, its level %.2f, the high's %.3f",
          20.0f * log10f(l / q), gr[0], lvl[0], lvl[2]);
    m = d;                               /* the ratio at 1:1: nothing pressed whatever the amount */
    m.ratio[MB_LOW] = 0.0f;
    l = mb_peak(60.0f, 0.25f, low, &m, 30000u, BRK_SR, gr, lvl);
    CHECK(fabsf(l - 0.25f) < 0.015f && gr[0] > 0.999f, "MB 1:1: the low untouched at full amount (%.3f, gain %.3f)",
          l, gr[0]);
    mb_peak(60.0f, 0.25f, low, &d, 30000u, BRK_SR, gr, lvl);
    q = gr[0];                           /* LIMIT: pressed harder than 8:1 */
    m = d;
    m.ratio[MB_LOW] = 1.0f;
    mb_peak(60.0f, 0.25f, low, &m, 30000u, BRK_SR, gr, lvl);
    CHECK(gr[0] < q * 0.8f, "MB LIMIT: the low pressed harder than at 8:1 (gain %.3f, %.3f)", gr[0], q);
    m = d;                               /* a band's gain: +12 dB on the low, nothing pressed */
    m.gain[MB_LOW] = 1.0f;
    a = mb_peak(60.0f, 0.05f, none, &m, 8000u, BRK_SR, 0, 0);
    CHECK(a > 0.18f && a < 0.22f, "MB LOW GAIN +12 dB: 60 Hz four times (%.3f from 0.05)", a);
    m = d;                               /* the output: -12 dB */
    m.out = 0.0f;
    a = mb_peak(1000.0f, 0.2f, none, &m, 4000u, BRK_SR, 0, 0);
    CHECK(a > 0.045f && a < 0.055f, "MB OUTPUT -12 dB: a quarter (%.3f from 0.2)", a);
    m = d;                               /* the solo: the low alone takes 60 Hz, leaves 8 kHz out */
    m.solo = 1u + MB_LOW;
    a = mb_peak(60.0f, 0.25f, none, &m, 8000u, BRK_SR, 0, 0);
    b = mb_peak(8000.0f, 0.25f, none, &m, 4000u, BRK_SR, 0, 0);
    CHECK(a > 0.2f && b < 0.01f, "MB SOLO LOW: 60 Hz through (%.3f), 8 kHz gone (%.4f)", a, b);
    m.solo = 1u + MB_HIGH;
    a = mb_peak(60.0f, 0.25f, none, &m, 8000u, BRK_SR, 0, 0);
    b = mb_peak(8000.0f, 0.25f, none, &m, 4000u, BRK_SR, 0, 0);
    CHECK(a < 0.01f && b > 0.2f, "MB SOLO HIGH: 60 Hz gone (%.4f), 8 kHz through (%.3f)", a, b);
    m = d;                               /* the low split moved up to 800 Hz: the low band takes 500 Hz */
    m.solo = 1u + MB_LOW;
    a = mb_peak(500.0f, 0.25f, none, &m, 4000u, BRK_SR, 0, 0);
    m.xlo = 1.0f;
    b = mb_peak(500.0f, 0.25f, none, &m, 4000u, BRK_SR, 0, 0);
    CHECK(a < 0.08f && b > 0.15f, "MB LOW X 200 Hz -> 800 Hz: 500 Hz in the low band %.3f -> %.3f", a, b);
    m = d;                               /* the attack: slow lets the onset through, fast holds it */
    m.att[MB_LOW] = 1.0f;
    a = mb_peak(60.0f, 0.25f, low, &m, 0u, 1500u, 0, 0);
    m.att[MB_LOW] = 0.0f;
    b = mb_peak(60.0f, 0.25f, low, &m, 0u, 1500u, 0, 0);
    CHECK(a > b * 1.5f, "MB ATTACK 100 ms lets the first 34 ms through (%.3f), 0.1 ms holds it (%.3f)", a, b);
    m = d;                               /* the release: after the tone stops, a fast one lets go sooner */
    {
        brk_fx_t p;
        uint32_t i, r;
        float g_fast = 0.0f, g_slow = 0.0f;
        for (r = 0; r < 2u; r++) {
            m.rel[MB_LOW] = r ? 1.0f : 0.0f;
            for (i = 0; i < BRK_SR; i++)
                buf[i] = i < BRK_SR / 2u ? 0.25f * sinf(6.2831853f * 60.0f * (float)i / (float)BRK_SR) : 0.0f;
            brk_fx_defaults(&p);
            brk_fx_slot_defaults(&p.slot[0], FXT_MBCOMP);
            p.slot[0].on = 1u;
            p.slot[0].p[0] = 1.0f;
            mb_fresh();
            brk_fx_set(&p);
            brk_fx_set_mbc(0u, &m);
            for (i = 0; i < BRK_SR / 2u + 4410u; i += BLOCK)   /* the tone, then 100 ms of nothing */
                brk_fx_process(buf + i, 0, 0, 0, 0, bl + i, br + i, BLOCK, 0u);
            brk_fx_mb_meter(0u, gr, lvl);
            if (r)
                g_slow = gr[0];
            else
                g_fast = gr[0];
        }
        CHECK(g_fast > 0.9f && g_slow < 0.6f, "MB RELEASE: 100 ms after, 10 ms has let go (%.2f), 1 s not (%.2f)",
              g_fast, g_slow);
    }
    m = d;                               /* the knee: 4 dB under the threshold, hard does nothing, soft starts */
    a = 0.0316f * 0.63f / 0.85f;         /* (-30 dB, -4 dB, the mid band's share of 1 kHz) */
    mb_peak(1000.0f, a, all, &m, 4000u, BRK_SR, gr, lvl);
    q = gr[1];
    m.knee = 1.0f;
    mb_peak(1000.0f, a, all, &m, 4000u, BRK_SR, gr, lvl);
    CHECK(q > 0.999f && gr[1] < 0.995f, "MB KNEE: under the threshold, hard %.3f, soft 12 dB %.3f", q, gr[1]);
    CHECK(brk_mb_reduce(-1.0f, 0.5f, 0.0f) == 0.0f && fabsf(brk_mb_reduce(2.0f, 0.5f, 0.0f) - 1.0f) < 1e-6f &&
          fabsf(brk_mb_reduce(3.0f, 0.5f, 4.0f) - 1.5f) < 1e-6f && brk_mb_reduce(0.0f, 0.5f, 4.0f) > 0.0f,
          "MB curve: none under, slope above, the knee between");
}


/* the extras (PRD A-49b): slot 0 holds `type` at p[], mix, with the extras x (0: the effect's defaults); buf (n samples)
 * through it into bl, br */
static void fx1(int type, float p0, float p1, float p2, float mix, const brk_fxx_t *x, uint32_t n)
{
    brk_fx_t p;
    brk_fxx_t d;
    uint32_t i;
    brk_fx_defaults(&p);
    brk_fx_slot_defaults(&p.slot[0], type);
    p.slot[0].on = 1u;
    p.slot[0].p[0] = p0;
    p.slot[0].p[1] = p1;
    p.slot[0].p[2] = p2;
    p.slot[0].mix = mix;
    mb_fresh();
    brk_fx_set(&p);
    brk_fxx_defaults(&d, type);
    brk_fx_set_fxx(0u, x ? x : &d);
    brk_fx_watch(0u);                    /* (slot 0's meters followed) */
    for (i = 0; i < n; i += BLOCK)
        brk_fx_process(buf + i, 0, 0, 0, 0, bl + i, br + i, n - i < BLOCK ? n - i : BLOCK, 0u);
}
static void tone_in(float hz, float amp, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        buf[i] = amp * sinf(6.2831853f * hz * (float)i / (float)BRK_SR);
}
static float peak_of(const float *b, uint32_t from, uint32_t to)
{
    float pk = 0.0f;
    uint32_t i;
    for (i = from; i < to; i++)
        if (fabsf(b[i]) > pk)
            pk = fabsf(b[i]);
    return pk;
}

static void test_fx_extras(void)
{
    uint32_t n = BRK_SR / 2u, t, j, v, i, ok = 1, bad_t = 0, bad_j = 0;
    brk_fxx_t x;
    brk_fx_meter_t m;
    float a, b;
    /* every effect, every extra at both ends, and every modulator shape at full depth: bounded, no NaN */
    for (i = 0; i < n; i++)              /* a loud, busy input: two tones and a click each beat */
        buf[i] = 0.5f * sinf(6.2831853f * 70.0f * (float)i / (float)BRK_SR) +
                 0.4f * sinf(6.2831853f * 3100.0f * (float)i / (float)BRK_SR) + (i % 11025u < 40u ? 0.6f : 0.0f);
    {
        static int16_t line[32768];      /* (the DELAY needs its line) */
        brk_fx_delay_buffer(line, 32768u);
    }
    for (t = 1; t < BRK_NFXT && ok; t++)
        for (j = 0; j < 4u + BRK_MOD_SHAPES && ok; j++)
            for (v = 0; v < 2u && ok; v++) {
                brk_fxx_defaults(&x, (int)t);
                if (j < 4u)
                    x.x[j] = (float)v;
                else {
                    x.x[FXX_SHAPE] = (float)(j - 4u) / (float)(BRK_MOD_SHAPES - 1u);
                    x.x[FXX_DEPTH] = (float)v;
                    x.x[FXX_RATE] = 1.0f;
                    x.x[FXX_TARGET] = (float)(j % 4u) / 3.0f;
                }
                fx1((int)t, 0.8f, 0.8f, 0.8f, 1.0f, &x, n);
                for (i = 0; i < n; i++)
                    if (!(fabsf(bl[i]) <= 1.0f) || !(fabsf(br[i]) <= 1.0f)) {
                        ok = 0;
                        bad_t = t;
                        bad_j = j;
                        break;
                    }
            }
    brk_fx_delay_buffer(0, 0u);
    CHECK(ok, "FX extras: every effect, every extra at both ends, every modulator: bounded (first bad: %s, %u)",
          ok ? "-" : BRK_FXT_NAME[bad_t], bad_j);
    /* FILTER 24 dB: a low-pass at 1 kHz takes more off 8 kHz than at 12 dB; SPREAD: the right brighter */
    tone_in(8000.0f, 0.25f, n);
    fx1(FXT_FILTER, 0.55f, 0.0f, 0.0f, 1.0f, 0, n);
    a = peak_of(bl, 8000u, n);
    brk_fxx_defaults(&x, FXT_FILTER);
    x.x[1] = 1.0f;
    fx1(FXT_FILTER, 0.55f, 0.0f, 0.0f, 1.0f, &x, n);
    b = peak_of(bl, 8000u, n);
    CHECK(b < a * 0.3f, "FILTER 24 dB: 8 kHz through a 1 kHz low-pass %.4f (12 dB %.4f)", b, a);
    brk_fxx_defaults(&x, FXT_FILTER);
    x.x[2] = 1.0f;
    tone_in(3000.0f, 0.25f, n);
    fx1(FXT_FILTER, 0.55f, 0.0f, 0.0f, 1.0f, &x, n);
    CHECK(peak_of(br, 8000u, n) > 2.0f * peak_of(bl, 8000u, n), "FILTER SPREAD: the right brighter (%.3f, left %.3f)",
          peak_of(br, 8000u, n), peak_of(bl, 8000u, n));
    /* EQ: the low shelf moved up to 400 Hz takes 300 Hz with it; LEVEL -12 dB a quarter */
    tone_in(300.0f, 0.05f, n);
    fx1(FXT_EQ, 1.0f, 0.5f, 0.5f, 0.5f, 0, n);
    a = peak_of(bl, 8000u, n);
    brk_fxx_defaults(&x, FXT_EQ);
    x.x[0] = 1.0f;
    fx1(FXT_EQ, 1.0f, 0.5f, 0.5f, 0.5f, &x, n);
    b = peak_of(bl, 8000u, n);
    CHECK(b > 1.6f * a, "EQ LOW FREQ 400 Hz: 300 Hz lifted %.3f (at 120 Hz %.3f)", b, a);
    brk_fxx_defaults(&x, FXT_EQ);
    x.x[3] = 0.0f;
    tone_in(1000.0f, 0.2f, n);
    fx1(FXT_EQ, 0.5f, 0.5f, 0.5f, 0.5f, &x, n);
    a = peak_of(bl, 4000u, n);
    CHECK(a > 0.045f && a < 0.055f, "EQ LEVEL -12 dB: a quarter (%.3f from 0.2)", a);
    /* CLIP OUTPUT -12 dB: the ceiling a quarter down */
    brk_fxx_defaults(&x, FXT_CLIP);
    x.x[1] = 0.0f;
    fx1(FXT_CLIP, 1.0f, 1.0f, 0.0f, 1.0f, &x, n);
    a = peak_of(bl, 4000u, n);
    CHECK(a > 0.24f && a < 0.26f, "CLIP OUTPUT -12 dB: held at a quarter (%.3f)", a);
    /* REVERB and DELAY WIDTH 0: the same both sides */
    for (i = 0; i < n; i++)
        buf[i] = i < 2000u ? 0.5f * sinf(6.2831853f * 440.0f * (float)i / (float)BRK_SR) : 0.0f;
    brk_fxx_defaults(&x, FXT_REVERB);
    x.x[0] = 0.0f;
    fx1(FXT_REVERB, 0.6f, 0.4f, 0.0f, 1.0f, &x, n);
    for (a = 0.0f, i = 3000u; i < n; i++)
        a += fabsf(bl[i] - br[i]);
    b = peak_of(bl, 3000u, n);
    CHECK(a < 1e-3f && b > 0.001f, "REVERB WIDTH 0: the tail rings (%.3f) the same left and right (%.5f apart)", b, a);
    brk_fxx_defaults(&x, FXT_DELAY);
    x.x[0] = 0.0f;
    {
        static int16_t line[32768];
        brk_fx_delay_buffer(line, 32768u);
        fx1(FXT_DELAY, 2.0f / 7.0f, 0.6f, 0.5f, 1.0f, &x, n);
        brk_fx_delay_buffer(0, 0u);
    }
    for (a = 0.0f, i = 3000u; i < n; i++)
        a += fabsf(bl[i] - br[i]);
    b = peak_of(bl, 3000u, n);
    CHECK(a < 1e-3f && b > 0.01f, "DELAY WIDTH 0: echoes (%.3f) the same left and right (%.5f apart)", b, a);
    /* PUNCH: 20:1 holds a loud tone nearer a quiet one than 1.5:1 */
    {
        float q[2], l[2];
        for (j = 0; j < 2u; j++) {
            brk_fxx_defaults(&x, FXT_PUNCH);
            x.x[0] = (float)j;
            tone_in(200.0f, 0.03f, n);
            fx1(FXT_PUNCH, 1.0f, 0.1f, 0.3f, 1.0f, &x, n);
            q[j] = peak_of(bl, 8000u, n);
            tone_in(200.0f, 0.3f, n);
            fx1(FXT_PUNCH, 1.0f, 0.1f, 0.3f, 1.0f, &x, n);
            l[j] = peak_of(bl, 8000u, n);
        }
        CHECK(l[1] / q[1] < 0.6f * l[0] / q[0], "PUNCH RATIO: 20 dB more in comes out %.1f dB louder at 20:1, %.1f at 1.5:1",
              20.0f * log10f(l[1] / q[1]), 20.0f * log10f(l[0] / q[0]));
    }
    /* MOD: a low-pass's cutoff swept by a sine at 1/16 makes a 2 kHz tone come and go; ENV follows the input */
    tone_in(2000.0f, 0.25f, BRK_SR);
    brk_fxx_defaults(&x, FXT_FILTER);
    x.x[FXX_DEPTH] = 1.0f;
    x.x[FXX_RATE] = 6.0f / 8.0f;
    fx1(FXT_FILTER, 0.5f, 0.0f, 0.0f, 1.0f, &x, BRK_SR);
    for (a = 1.0f, b = 0.0f, i = 0; i + 441u < BRK_SR; i += 441u) {   /* 10 ms windows: the quietest, the loudest */
        float w = i > 8000u ? peak_of(bl, i, i + 441u) : 0.5f;
        if (w < a) a = w;
        if (w > b) b = w;
    }
    CHECK(b > 0.15f && a < 0.03f, "MOD on FILTER's cutoff: 2 kHz comes and goes (%.3f .. %.3f)", a, b);
    brk_fxx_defaults(&x, FXT_DRIVE);
    x.x[FXX_SHAPE] = 1.0f;
    x.x[FXX_DEPTH] = 1.0f;
    tone_in(200.0f, 0.5f, n);
    fx1(FXT_DRIVE, 0.2f, 0.5f, 0.5f, 1.0f, &x, n);
    brk_fx_meter(0u, &m);
    a = m.mod;
    b = m.in;
    for (i = 0; i < n; i++)
        buf[i] = 0.0f;
    fx1(FXT_DRIVE, 0.2f, 0.5f, 0.5f, 1.0f, &x, n);
    brk_fx_meter(0u, &m);
    CHECK(a > 0.8f && b > 0.4f && m.mod < 0.05f && m.p[0] < 0.21f,
          "MOD ENV: loud %.2f (level in %.2f), silent %.2f; the drive back at %.2f", a, b, m.mod, m.p[0]);
}

/* the overload guard (PRD E-7): a render over 90 % bypasses the chain's last slot on; 2 s clear, it comes back */
static void test_overload(void)
{
    brk_fx_t p;
    uint32_t i, b;
    float during, after, before;
    tone_in(8000.0f, 0.25f, BLOCK);
    brk_fx_defaults(&p);
    brk_fx_slot_defaults(&p.slot[0], FXT_FILTER);   /* a low-pass at 100 Hz: 8 kHz gone while it is on */
    p.slot[0].on = 1u;
    p.slot[0].p[0] = 0.2f;
    mb_fresh();
    brk_fx_set(&p);
    for (b = 0; b < 40u; b++)
        brk_fx_process(buf, 0, 0, 0, 0, bl, br, BLOCK, 0u);
    before = peak_of(bl, 0u, BLOCK);
    brk_fx_overload();
    for (b = 0; b < 10u; b++)
        brk_fx_process(buf, 0, 0, 0, 0, bl, br, BLOCK, 0u);
    during = peak_of(bl, 0u, BLOCK);
    i = brk_fx_shed();
    for (b = 0; b < 400u; b++)           /* 2.3 s */
        brk_fx_process(buf, 0, 0, 0, 0, bl, br, BLOCK, 0u);
    after = peak_of(bl, 0u, BLOCK);
    CHECK(before < 0.01f && during > 0.2f && i == 1u && brk_fx_shed() == 0u && after < 0.01f,
          "OVERLOAD: the slot shed (8 kHz %.3f -> %.3f, %u shed), back after 2 s (%.3f, %u)", before, during, i, after,
          brk_fx_shed());
}

/* ------------------------------------------------------------------ MATHCORE (docs/PRD_MATHCORE.md) --- */
/* a test break whose hits are known: K a low thump, S a mid body with noise, H quiet high hiss, P a high-mid knock */
static void mc_test_break(const char *pat)
{
    uint32_t n = 16u, sl = 1376u, i, s, seed = 7u;
    float lp = 0.0f;
    brk_init();
    for (i = 0; i < n * sl; i++)
        pcm[i] = 0;
    for (s = 0; s < n; s++)
        for (i = 0; i < sl; i++) {
            float t = (float)i / (float)RATE, x = 0.0f, nz = brk_randf_test(&seed);
            switch (pat[s]) {
            case 'K': x = 0.9f * sinf(6.2831853f * 55.0f * t) * expf(-t * 18.0f); break;
            case 'S': lp += (nz - lp) * 0.3f;
                      x = (0.35f * sinf(6.2831853f * 330.0f * t) + 0.6f * lp) * expf(-t * 25.0f); break;
            case 'H': x = 0.25f * (nz - lp) * expf(-t * 35.0f); lp = nz; break;
            case 'P': x = 0.3f * sinf(6.2831853f * 900.0f * t) * expf(-t * 40.0f); break;
            default: break;
            }
            pcm[s * sl + i] = (int16_t)(x * 30000.0f);
        }
    brk.pcm = pcm;
    brk.len = n * sl;
    brk.rate = RATE;
    brk.bars = 1u;
    brk.nslices = n;
    brk.split = 0u;
    for (i = 0; i <= n; i++)
        brk.slice[i] = i * sl;
    brk.bpm_x100 = (uint32_t)(60.0f * (float)RATE / (4.0f * (float)sl) * 100.0f + 0.5f);
    snprintf(brk.name, sizeof brk.name, "MC TEST");
    brk_set_break(&brk);
    brk_tempo((float)brk.bpm_x100 * 0.01f);
    brk_pitch(0.0f);
    brk_swing(0.5f);
    brk_seed(1u);
}

/* the Slice Map, heard: the kicks, the snares, the hats where they are; each role found */
static void test_mc_map(void)
{
    static const char P[] = "KHSHKHSHKHSHKHSP";
    uint8_t r[BRK_MAX_SLICES];
    uint32_t i, kick_ok = 1, snare_ok = 1, hat_ok = 1;
    mc_test_break(P);
    mc_auto_roles(&brk, r);
    for (i = 0; i < 16u; i++) {
        int k = r[i] >> ROLE_KICK & 1u, s = r[i] >> ROLE_SNARE & 1u, h = r[i] >> ROLE_HAT & 1u;
        if (k != (P[i] == 'K'))
            kick_ok = 0;
        if (P[i] == 'S' ? !s : s && P[i] != 'P')
            snare_ok = 0;
        if (P[i] == 'H' && !h)
            hat_ok = 0;
        if (P[i] != 'H' && h)
            hat_ok = 0;
    }
    CHECK(kick_ok, "Slice Map: the kicks are the K slices");
    CHECK(snare_ok, "Slice Map: the snares are the S slices");
    CHECK(hat_ok, "Slice Map: the hats are the H slices");
    for (i = 0; i < 16u; i++)
        printf("%s%s%s%s%s", i ? " " : "  map: ", r[i] & 1u ? "K" : "", r[i] & 2u ? "S" : "", r[i] & 4u ? "H" : "",
               r[i] & 8u ? "P" : r[i] ? "" : "-");
    printf("\n");
    {
        uint8_t r2[BRK_MAX_SLICES];
        mc_auto_roles(&brk, r2);
        CHECK(!memcmp(r, r2, sizeof r), "Slice Map: the same break gives the same map");
    }
    {                                    /* silence: no roles at all */
        for (i = 0; i < brk.len; i++)
            pcm[i] = 0;
        mc_auto_roles(&brk, r);
        for (i = 0; i < 16u && !r[i]; i++)
            ;
        CHECK(i == 16u, "Slice Map: a silent break has no roles");
    }
}

/* role lookup; multiple roles on a slice; an empty role says so; the pattern's drum lanes put their slices in the
 * loop's place; everything on the break's one voice */
static float mc_run(uint32_t n, float *peak_out, float *peak_lanes)
{
    static float o[BLOCK];
    uint32_t i = 0, j;
    float po = 0.0f, pl;
    while (i < n) {
        uint32_t m = n - i < BLOCK ? n - i : BLOCK;
        brk_render(o, m);
        for (j = 0; j < m; j++)
            if (fabsf(o[j]) > po)
                po = fabsf(o[j]);
        i += m;
    }
    pl = po;
    if (peak_out)
        *peak_out = po;
    if (peak_lanes)
        *peak_lanes = pl;
    return pl;
}
static void test_mc_roles(void)
{
    uint8_t r[BRK_MAX_SLICES] = {0}, list[BRK_MAX_SLICES];
    brk_status_t st;
    float pl;
    r[0] = 1u << ROLE_KICK;
    r[4] = 1u << ROLE_SNARE | 1u << ROLE_PERC;
    r[8] = 1u << ROLE_KICK;
    CHECK(mc_role_slices(r, 16u, ROLE_KICK, list) == 2u && list[0] == 0u && list[1] == 8u,
          "roles: KICK is slices 1 and 9");
    CHECK(mc_role_slices(r, 16u, ROLE_PERC, list) == 1u && list[0] == 4u &&
          mc_role_slices(r, 16u, ROLE_SNARE, list) == 1u && list[0] == 4u, "roles: one slice in two roles");
    CHECK(mc_role_slices(r, 16u, ROLE_HAT, list) == 0u, "roles: HAT has none");
    mc_test_break("KHSHKHSHKHSHKHSP");
    brk_roles(r, BRK_MAX_SLICES);
    brk_drum_lanes(0x0040u, 0u, 0u);     /* the pattern's kick on step 7: its KICK slice (1) in the loop's place */
    brk_play(1);
    {
        uint32_t sl = brk_step_len_samples();
        mc_run(6u * sl + sl / 2u, 0, &pl);
        brk_status(&st);
        CHECK(st.step == 6 && st.slice == 0, "drum lanes: step 7 plays the KICK slice, not its own (%d, slice %d)",
              st.step, st.slice + 1);
        mc_run(sl, 0, &pl);
        brk_status(&st);
        CHECK(st.slice == 7, "drum lanes: the loop its own again after it (slice %d)", st.slice + 1);
    }
    brk_drum_lanes(0u, 0u, 0x0001u);     /* the hat lane: no HAT slice: the loop's own slice, and said */
    brk_play(0);
    mc_run(BLOCK * 4u, 0, 0);
    brk_play(1);
    mc_run(brk.len, 0, &pl);
    brk_status(&st);
    CHECK(pl > 0.1f && (st.mc_missing >> ROLE_HAT & 1u), "drum lanes: an empty role leaves the loop (%.3f), missing (%x)",
          pl, st.mc_missing);
    brk_play(0);
    brk_drum_lanes(0u, 0u, 0u);
    mc_run(BLOCK * 4u, 0, 0);
    brk_role_hit(ROLE_SNARE);            /* MIDI's drum note: now, stopped too */
    mc_run(BLOCK * 8u, 0, &pl);
    brk_status(&st);
    CHECK(pl > 0.1f && st.slice == 4, "role hit: the SNARE slice at once (%.3f, slice %d)", pl, st.slice + 1);
}

/* the scenes' lanes: four at once, of 5, 7, 11 and 13 events, on one clock for 64 bars (their places always where the
 * step count says); mute keeps the phase; the seed repeats; the break's loop quiet under a scene without it */
static mc_scene_t mc_sc[MC_SCENES];
static void mc_scene_init(mc_scene_t *s, uint32_t bars)
{
    uint32_t l;
    memset(s, 0, sizeof *s);
    s->bars = (uint8_t)bars;
    for (l = 0; l < MC_LANES; l++) {
        mc_lane_t *L = &s->lane[l];
        L->len = 16u;
        L->rate = MCR_16;
        L->prob = 100u;
        L->ratchet = 1u;
        L->gate = 100u;
        L->level = 100u;
        L->role = (uint8_t)l;
    }
}
static uint32_t mc_hits_total[MC_LANES];
static uint8_t mc_hits_was[MC_LANES];
static void mc_count(void)
{
    brk_status_t st;
    uint32_t l;
    brk_status(&st);
    for (l = 0; l < MC_LANES; l++) {
        mc_hits_total[l] += (uint8_t)(st.mc_hits[l] - mc_hits_was[l]);
        mc_hits_was[l] = st.mc_hits[l];
    }
}
static void mc_run_counting(uint32_t n, float *buf)
{
    static float o[BLOCK];
    uint32_t i = 0;
    while (i < n) {
        uint32_t m = n - i < BLOCK ? n - i : BLOCK;
        brk_render(buf ? buf + i : o, m);
        mc_count();
        i += m;
    }
}
static void mc_zero_counts(void)
{
    brk_status_t st;
    uint32_t l;
    brk_status(&st);
    for (l = 0; l < MC_LANES; l++) {
        mc_hits_total[l] = 0u;
        mc_hits_was[l] = st.mc_hits[l];
    }
}
static void test_mc_lanes(void)
{
    static const uint8_t LEN[4] = {5u, 7u, 11u, 13u};
    uint8_t r[BRK_MAX_SLICES];
    uint32_t l, steps, sl;
    brk_status_t st;
    int ok = 1;
    float po, pl;
    mc_test_break("KHSHKHSHKHSHKHSP");
    mc_auto_roles(&brk, r);
    brk_roles(r, BRK_MAX_SLICES);
    mc_scene_init(&mc_sc[0], 4u);
    for (l = 0; l < MC_LANES; l++) {
        mc_sc[0].lane[l].len = LEN[l];
        mc_sc[0].lane[l].hits = 1u;      /* one hit a cycle: on its first event */
        mc_sc[0].lane[l].flags = MCL_ON | MCL_FREE;   /* (counting from the start: no reset at each scene) */
    }
    brk_mc_scenes(mc_sc);
    brk_mc_start(0);
    brk_mc_next(0);                      /* A, round and round */
    mc_zero_counts();
    brk_play(1);
    sl = brk_step_len_samples();
    mc_run_counting(64u * 16u * sl + sl / 2u, 0);   /* 64 bars and half a step */
    brk_status(&st);
    steps = 64u * 16u + 1u;              /* (the 16ths begun: 1025) */
    for (l = 0; l < MC_LANES; l++) {
        uint32_t want = (steps + LEN[l] - 1u) / LEN[l];
        if (mc_hits_total[l] != want || st.mc_pos[l] != (steps - 1u) % LEN[l])
            ok = 0;
        printf("  lane %u: %u hits (%u), at %u of %u (%u)\n", l, mc_hits_total[l], want, st.mc_pos[l], LEN[l],
               (steps - 1u) % LEN[l]);
    }
    CHECK(ok, "lanes: 5, 7, 11 and 13 together for 64 bars, each hit and place where the master clock says");
    CHECK(st.mc_adv == 16u, "lanes: a 4-bar scene, 16 boundaries in 64 bars (%u)", st.mc_adv);
    {                                    /* the reset at a scene's start: a lane not FREE starts its cycle again */
        uint32_t f = mc_sc[0].lane[0].flags;
        mc_sc[0].lane[0].flags = MCL_ON;
        mc_zero_counts();
        mc_run_counting(67u * sl, 0);    /* 16th 1092 playing: the scene began on 1089 (17 x 64 + 1) */
        mc_sc[0].lane[0].flags = (uint8_t)f;
        brk_status(&st);
        CHECK(st.mc_pos[0] == 3u, "a lane not FREE starts again with its scene (at %u, 3; FREE: %u)", st.mc_pos[0],
              (1092u - 1u) % 5u);
        steps += 67u;
    }
    /* mute: the lane quiet, its place running on */
    brk_mc_mute(1u, 0u);
    mc_zero_counts();
    mc_run_counting(10u * sl, 0);
    brk_status(&st);
    CHECK(mc_hits_total[0] == 0u && mc_hits_total[1] >= 1u, "mute: the kick lane quiet (%u), the others on (%u)",
          mc_hits_total[0], mc_hits_total[1]);
    CHECK(st.mc_pos[0] == (steps + 10u - 1u) % 5u, "mute: its place runs on (%u, %u)", st.mc_pos[0],
          (steps + 9u) % 5u);
    brk_mc_mute(0u, 1u << 2);            /* solo the hat lane */
    mc_zero_counts();
    mc_run_counting(26u * sl, 0);
    CHECK(mc_hits_total[2] >= 2u && !mc_hits_total[0] && !mc_hits_total[1] && !mc_hits_total[3],
          "solo: only the hat lane (%u %u %u %u)", mc_hits_total[0], mc_hits_total[1], mc_hits_total[2],
          mc_hits_total[3]);
    brk_mc_mute(0u, 0u);
    /* the scene plays the break itself: only its roles' slices sound (the loop's own don't), dry or running on */
    {
        int only_roles = 1, seen = 0;
        uint32_t k2;
        float pmax = 0.0f;
        for (k2 = 0; k2 < 64u; k2++) {
            mc_run(sl / 2u, &po, &pl);
            pmax = po > pmax ? po : pmax;
            brk_status(&st);
            if (st.slice >= 0) {
                seen++;
                if (!r[st.slice]) {
                    only_roles = 0;
                    printf("  not a role's: slice %d (step %d)\n", st.slice + 1, st.step);
                }
            }
        }
        CHECK(seen && only_roles && pmax > 0.05f, "a scene: the break's voice on its roles' slices alone (%d seen, %.3f)",
              seen, pmax);
        mc_sc[0].flags = MCS_BREAK;
        mc_run(8u * sl, &po, &pl);
        CHECK(po > 0.1f, "a scene running on: it plays (%.3f)", po);
        mc_sc[0].flags = 0u;
    }
    brk_play(0);
    mc_run(BLOCK * 4u, 0, 0);
    /* the seed: RANDOM with a chance, twice from the start: the same sound */
    {
        static float a[44100], b2[44100];
        mc_scene_init(&mc_sc[1], 2u);
        mc_sc[1].lane[0].flags = MCL_ON;
        mc_sc[1].lane[0].role = ROLE_HAT;
        mc_sc[1].lane[0].hits = 0xFFFFu;
        mc_sc[1].lane[0].mode = MCM_RANDOM;
        mc_sc[1].lane[0].prob = 60u;
        mc_sc[1].lane[0].seed = 42u;
        brk_mc_start(1);
        brk_mc_next(1);
        brk_play(1);
        mc_run_counting(44100u, a);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(1);
        brk_play(1);
        mc_run_counting(44100u, b2);
        CHECK(!memcmp(a, b2, sizeof a), "seed: RANDOM with a chance plays the same twice");
        mc_sc[1].lane[0].seed = 43u;
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(1);
        brk_play(1);
        mc_run_counting(44100u, b2);
        CHECK(memcmp(a, b2, sizeof a) != 0, "seed: another seed, other choices");
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* bounded: every lane every 1/32T with ratchets 8: the hits counted stay what they should */
    {
        mc_scene_init(&mc_sc[2], 1u);
        for (l = 0; l < MC_LANES; l++) {
            mc_sc[2].lane[l].flags = MCL_ON;
            mc_sc[2].lane[l].rate = MCR_32T;
            mc_sc[2].lane[l].hits = 0xFFFFFFFFu;
            mc_sc[2].lane[l].len = 32u;
            mc_sc[2].lane[l].ratchet = 4u;
            mc_sc[2].lane[l].role = ROLE_KICK;
        }
        brk_mc_start(2);
        brk_mc_next(2);
        mc_zero_counts();
        brk_play(1);
        mc_run(16u * sl, &po, &pl);
        mc_count();
        CHECK(pl < 4.0f && pl > 0.1f, "dense: four lanes of 1/32T x4 ratchets stay bounded (%.3f)", pl);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    brk_mc_scenes(0);
}

/* the queue (the main loop's, here the test's): A -> B -> C -> A; A alone round and round; a cleared queue ends at
 * the boundary; keys played meanwhile don't move it */
static void test_mc_queue(void)
{
    uint8_t r[BRK_MAX_SLICES], q[8];
    uint32_t n, pos = 0u, k, sl, adv_seen, seen[12], ns = 0u;
    brk_status_t st;
    mc_test_break("KHSHKHSHKHSHKHSP");
    mc_auto_roles(&brk, r);
    brk_roles(r, BRK_MAX_SLICES);
    for (k = 0; k < 3u; k++) {
        mc_scene_init(&mc_sc[k], 1u);
        mc_sc[k].lane[0].flags = MCL_ON;
        mc_sc[k].lane[0].hits = 1u;
    }
    brk_mc_scenes(mc_sc);
    q[0] = 0u; q[1] = 1u; q[2] = 2u;
    n = 3u;
    brk_mc_start(q[0]);
    brk_mc_next(q[1]);
    brk_play(1);
    sl = brk_step_len_samples();
    brk_status(&st);
    adv_seen = st.mc_adv;
    seen[ns++] = 0u;
    for (k = 0; k < 6u * 16u; k++) {     /* six bars, a step at a time; the keys played in bar 3 */
        if (k == 40u)
            brk_key(5, 1);
        if (k == 44u)
            brk_key(5, 0);
        mc_run(sl, 0, 0);
        brk_status(&st);
        if (st.mc_adv != adv_seen) {     /* a boundary: the entry after it to the engine */
            adv_seen = st.mc_adv;
            pos = (pos + 1u) % n;
            if (ns < 12u)
                seen[ns++] = (uint32_t)st.mc_scene;
            brk_mc_next(q[(pos + 1u) % n]);
        }
    }
    CHECK(ns >= 6u && seen[0] == 0u && seen[1] == 1u && seen[2] == 2u && seen[3] == 0u && seen[4] == 1u &&
          seen[5] == 2u, "queue: A B C A B C (%u %u %u %u %u %u), the keys played meanwhile left it alone",
          seen[0], seen[1], seen[2], seen[3], seen[4], seen[5]);
    brk_play(0);
    mc_run(BLOCK * 8u, 0, 0);
    /* one entry: A, A, A */
    brk_mc_start(0);
    brk_mc_next(0);
    brk_play(1);
    mc_run(3u * 16u * sl + sl / 2u, 0, 0);
    brk_status(&st);
    CHECK(st.mc_scene == 0 && st.mc_adv - adv_seen == 3u, "queue: A alone repeats (%d, %u boundaries)", st.mc_scene,
          st.mc_adv - adv_seen);
    /* cleared: the scene playing finishes, then the lanes stop (and say so) */
    {
        uint32_t ended = st.mc_ended;
        brk_mc_next(-1);
        mc_run(8u * sl, 0, 0);
        brk_status(&st);
        CHECK(st.mc_scene == 0, "queue cleared: the scene playing isn't cut (%d)", st.mc_scene);
        mc_run(9u * sl, 0, 0);
        brk_status(&st);
        CHECK(st.mc_scene == -1 && st.mc_ended == ended + 1u, "queue cleared: it ends at the boundary (%d, %u)",
              st.mc_scene, st.mc_ended - ended);
    }
    brk_play(0);
    mc_run(BLOCK * 8u, 0, 0);
    brk_mc_scenes(0);
}

/* playing it: a launch lands on its beat or bar (counted from the bar: a fill on beat 3 ends on the bar line), MATH
 * turns the lanes' lengths to 5 / 7 / 11 / 13 with nothing programmed, SPEED doubles, DENSITY adds hits (the same each
 * cycle), CHAOS stays bounded, a riser speeds up over its phrase */
static void test_mc_play(void)
{
    uint8_t r[BRK_MAX_SLICES];
    uint32_t l, sl, k;
    brk_status_t st;
    float pl;
    mc_test_break("KHSHKHSHKHSHKHSP");
    mc_auto_roles(&brk, r);
    brk_roles(r, BRK_MAX_SLICES);
    for (k = 0; k < 3u; k++) {
        mc_scene_init(&mc_sc[k], k == 1u ? 1u : 2u);
        for (l = 0; l < MC_LANES; l++) {
            mc_sc[k].lane[l].flags = MCL_ON;
            mc_sc[k].lane[l].hits = 0x1111u;
        }
    }
    brk_mc_scenes(mc_sc);
    brk_mc_macro(50u, 0u, 50u, 0u);
    brk_mc_start(0);
    brk_mc_next(0);
    brk_play(1);
    sl = brk_step_len_samples();
    mc_run(16u * sl + 5u * sl + sl / 2u, 0, 0);   /* bar 2, its 6th 16th */
    brk_mc_jump(1, 4u);                  /* a fill on the next beat (the 9th 16th) */
    brk_mc_next(0);
    mc_run(2u * sl, 0, 0);
    brk_status(&st);
    CHECK(st.mc_scene == 0, "launch: not before its beat (%d)", st.mc_scene);
    mc_run(2u * sl, 0, 0);
    brk_status(&st);
    CHECK(st.mc_scene == 1 && st.mc_step == 10u, "launch: on the beat, counted from the bar (scene %d, 16th %u)",
          st.mc_scene, st.mc_step);
    mc_run(7u * sl, 0, 0);               /* its bar line: the groove again */
    brk_status(&st);
    CHECK(st.mc_scene == 0 && st.mc_step == 1u, "launch: the fill ends on the bar line, the groove back (%d, %u)",
          st.mc_scene, st.mc_step);
    brk_mc_jump(2, 16u);                 /* a groove on the next bar */
    brk_mc_next(2);
    mc_run(10u * sl, 0, 0);
    brk_status(&st);
    CHECK(st.mc_scene == 0, "launch: a groove waits for the bar (%d)", st.mc_scene);
    mc_run(6u * sl, 0, 0);
    brk_status(&st);
    CHECK(st.mc_scene == 2 && st.mc_step == 1u, "launch: the groove on the bar (%d, %u)", st.mc_scene, st.mc_step);
    /* MATH at full: the lanes' cycles are 5, 7, 11 and 13 */
    brk_mc_macro(50u, 100u, 50u, 0u);
    mc_zero_counts();
    mc_run_counting(31u * 16u * sl, 0);  /* (31 bars of scene C, round: its phrase restarts each 2 bars) */
    brk_status(&st);
    {
        int ok = 1;
        for (l = 0; l < MC_LANES; l++)
            if (st.mc_pos[l] >= MC_MATH_LEN[MC_MATH_STEPS - 1u][l])
                ok = 0;
        CHECK(ok && MC_MATH_LEN[MC_MATH_STEPS - 1u][0] == 5u && MC_MATH_LEN[MC_MATH_STEPS - 1u][3] == 13u,
              "MATH: the lanes cycle in 5, 7, 11 and 13 (%u %u %u %u)", st.mc_pos[0], st.mc_pos[1], st.mc_pos[2],
              st.mc_pos[3]);
    }
    /* SPEED double: twice the hits; DENSITY up: more, the same twice */
    {
        uint32_t base[MC_LANES], dbl[MC_LANES], dense[2];
        brk_mc_macro(50u, 0u, 50u, 0u);
        mc_zero_counts();
        mc_run_counting(32u * sl, 0);
        memcpy(base, mc_hits_total, sizeof base);
        brk_mc_macro(50u, 0u, 100u, 0u);
        mc_zero_counts();
        mc_run_counting(32u * sl, 0);
        memcpy(dbl, mc_hits_total, sizeof dbl);
        CHECK(dbl[1] >= 2u * base[1] - 1u && dbl[1] <= 2u * base[1] + 1u, "SPEED double: twice the hits (%u, %u)",
              dbl[1], base[1]);
        brk_mc_macro(100u, 0u, 50u, 0u);
        mc_zero_counts();
        mc_run_counting(32u * sl, 0);
        dense[0] = mc_hits_total[2];
        mc_zero_counts();
        mc_run_counting(32u * sl, 0);
        dense[1] = mc_hits_total[2];
        CHECK(dense[0] > base[2] && dense[0] == dense[1], "DENSITY up: more hits (%u, %u), the same each time (%u)",
              dense[0], base[2], dense[1]);
        brk_mc_macro(0u, 0u, 50u, 0u);
        mc_zero_counts();
        mc_run_counting(32u * sl, 0);
        CHECK(mc_hits_total[2] < base[2] && mc_hits_total[2] > 0u, "DENSITY down: fewer hits, the downbeats stay (%u)",
              mc_hits_total[2]);
        brk_mc_macro(100u, 100u, 100u, 100u);   /* everything up at once: bounded */
        mc_run(64u * sl, 0, &pl);
        CHECK(pl > 0.05f && pl < 4.0f, "macros all up: bounded (%.3f)", pl);
        brk_mc_macro(50u, 0u, 50u, 0u);
    }
    brk_play(0);
    mc_run(BLOCK * 8u, 0, 0);
    /* a riser: one hit an 1/8, then faster each quarter of a 4-bar phrase */
    {
        uint32_t q[4];
        mc_scene_init(&mc_sc[3], 4u);
        mc_sc[3].lane[1].flags = MCL_ON;
        mc_sc[3].lane[1].hits = 1u;
        mc_sc[3].lane[1].len = 1u;
        mc_sc[3].lane[1].rate = MCR_8;
        mc_sc[3].lane[1].rise = 12;
        brk_mc_start(3);
        brk_mc_next(3);
        brk_play(1);
        for (k = 0; k < 4u; k++) {
            mc_zero_counts();
            mc_run_counting(16u * sl, 0);
            q[k] = mc_hits_total[1];
        }
        CHECK(q[0] == 8u && q[1] == 16u && q[2] == 32u && q[3] >= 60u, "riser: 8, 16, 32, 64 hits a bar (%u %u %u %u)",
              q[0], q[1], q[2], q[3]);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* the lanes sound together (MG-001): each lane has its own voice, so a hit does not cut another lane's */
    {
        mc_scene_init(&mc_sc[6], 1u);
        mc_sc[6].lane[0].flags = MCL_ON;
        mc_sc[6].lane[0].role = ROLE_KICK;
        mc_sc[6].lane[0].hits = 0xFFu;
        mc_sc[6].lane[0].len = 8u;
        mc_sc[6].lane[0].rate = MCR_8;
        mc_sc[6].lane[1].flags = MCL_ON;
        mc_sc[6].lane[1].role = ROLE_SNARE;
        mc_sc[6].lane[1].hits = 0xFFu;
        mc_sc[6].lane[1].len = 8u;
        mc_sc[6].lane[1].rate = MCR_8;
        brk_mc_start(6);
        brk_mc_next(6);
        brk_play(1);
        mc_run(BLOCK * 8u, 0, 0);
        brk_status(&st);
        CHECK(st.mc_slice[0] >= 0 && st.mc_slice[1] >= 0, "the lanes sound together: each has its own slice (%d, %d)",
              st.mc_slice[0], st.mc_slice[1]);
        CHECK(st.mc_slice[0] != st.mc_slice[1], "and they are different slices (%d, %d)", st.mc_slice[0], st.mc_slice[1]);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* the groove-event layer (MG-004, MG-005): the event has its own velocity and its own time */
    {
        static const uint8_t gruv[MC_GRUV_BYTES + 4u] = { 1u, 12u, 50u, 0u, 0u, 0u, 1u, 0u, 7u, 1u, 0u, 0u };
        static const uint8_t evnt[MC_EVNT_BYTES] = { 0u, 20u, 0x88u, 0x13u, 0u, 3u, 128u, 100u, 0u, 0u };
        float p1 = 0.0f, p2 = 0.0f, po = 0.0f, pl = 0.0f;
        mc_scene_init(&mc_sc[7], 1u);
        mc_sc[7].lane[0].flags = MCL_ON;
        mc_sc[7].lane[0].role = ROLE_KICK;
        mc_sc[7].lane[0].hits = 1u;
        mc_sc[7].lane[0].len = 8u;
        mc_sc[7].lane[0].rate = MCR_8;
        mc_sc[7].lane[0].level = 100u;
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_mc_start(7);
        brk_mc_next(7);
        brk_play(1);
        mc_run(BLOCK * 4u, &po, &pl);
        p1 = po > pl ? po : pl;
        /* the same lane, without the event: its pattern plays at the lane's level */
        brk_mc_groove(0, 0, 0, 0);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(7);
        brk_play(1);
        mc_run(BLOCK * 4u, &po, &pl);
        p2 = po > pl ? po : pl;
        CHECK(p1 > 0.0f, "the event is heard (%.3f)", p1);
        CHECK(p1 < p2 * 0.8f, "at its own velocity: quieter than the pattern's (%.3f < %.3f)", p1, p2);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* the early and the late events (MG-005): an event whose micro-timing is off its grid tick is heard
       there (the countdown is in samples, so the hit lands on the sample it belongs to) */
    {
        static const uint8_t gruv[MC_GRUV_BYTES + 4u] = { 1u, 12u, 50u, 0u, 0u, 0u, 1u, 0u, 8u, 1u, 0u, 0u };
        static uint8_t evnt[MC_EVNT_BYTES] = { 0u, 110u, 0u, 0u, 0u, 3u, 128u, 100u, 1u, 0u };
        static float buf[65536];
        int32_t on0 = -1, on1 = -1, on2 = -1;
        uint32_t n = 65536u, i, m;
        mc_scene_init(&mc_sc[8], 1u);
        mc_sc[8].lane[0].flags = MCL_ON;
        mc_sc[8].lane[0].role = ROLE_KICK;
        mc_sc[8].lane[0].hits = 2u;                /* the event is at position 1: there is a tick before it */
        mc_sc[8].lane[0].len = 8u;
        mc_sc[8].lane[0].rate = MCR_8;
        mc_sc[8].lane[0].level = 100u;
        /* on the grid */
        evnt[2] = 0u;
        evnt[3] = 0u;
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_mc_start(8);
        brk_mc_next(8);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(8);
        brk_play(1);
        for (i = 0; i < n; i += BLOCK) {
            m = n - i > BLOCK ? BLOCK : n - i;
            brk_render(buf + i, m);
        }
        for (i = 0; i < n; i++)
            if (fabsf(buf[i]) > 0.05f) {
                on0 = (int32_t)i;
                break;
            }
        /* five milliseconds early: the micro-timing is a signed 16-bit, little endian */
        evnt[2] = 0x78u;
        evnt[3] = 0xECu;                     /* -5000 µs */
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(8);
        brk_play(1);
        for (i = 0; i < n; i += BLOCK) {
            m = n - i > BLOCK ? BLOCK : n - i;
            brk_render(buf + i, m);
        }
        for (i = 0; i < n; i++)
            if (fabsf(buf[i]) > 0.05f) {
                on1 = (int32_t)i;
                break;
            }
        /* five milliseconds late */
        evnt[2] = 0x88u;
        evnt[3] = 0x13u;                     /* +5000 µs */
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(8);
        brk_play(1);
        for (i = 0; i < n; i += BLOCK) {
            m = n - i > BLOCK ? BLOCK : n - i;
            brk_render(buf + i, m);
        }
        for (i = 0; i < n; i++)
            if (fabsf(buf[i]) > 0.05f) {
                on2 = (int32_t)i;
                break;
            }
        CHECK(on0 > 0 && on1 > 0 && on2 > 0, "the event is heard (%d, %d, %d)", on0, on1, on2);
        CHECK(on0 - on1 >= 215 && on0 - on1 <= 226, "five milliseconds early (220 samples): %d vs %d", on0, on1);
        CHECK(on2 - on0 >= 215 && on2 - on0 <= 226, "five milliseconds late (220 samples): %d vs %d", on2, on0);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* same-position groove events (MG-004/005): every record must survive, including multiple early events */
    {
        static const uint8_t gruv[MC_GRUV_BYTES + 4u] = {
            1u, 12u, 50u, 0u, 0u, 0u, 1u, 0u, 10u, 2u, 0u, 0u
        };
        static uint8_t evnt[2u * MC_EVNT_BYTES] = {
            0u, 127u, 0u, 0u, 0u, 0u, 0u, 100u, 0u, 0u,
            0u, 80u,  0u, 0u, 0u, 4u, 0u, 100u, 0u, 0u
        };
        brk_status_t before, after;
        uint32_t first_count;
        mc_scene_init(&mc_sc[10], 1u);
        mc_sc[10].lane[0].flags = MCL_ON;
        mc_sc[10].lane[0].role = ROLE_KICK;
        mc_sc[10].lane[0].hits = 0u;   /* the EVNT records are the entire lane for this scene */
        mc_sc[10].lane[0].len = 8u;
        mc_sc[10].lane[0].rate = MCR_8;
        mc_sc[10].lane[0].level = 100u;
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_mc_start(10);
        brk_mc_next(10);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_status(&before);
        brk_mc_start(10);
        brk_play(1);
        mc_run_counting(2u * sl, 0);
        brk_status(&after);
        first_count = (uint8_t)(after.mc_hits[0] - before.mc_hits[0]);
        CHECK(first_count == 2u, "same lane and position: both on-grid event records are played (%u)", first_count);

        /* Both early events share one grid position. Tracking only a position bit used to collapse them to one. */
        evnt[2] = 0x78u; evnt[3] = 0xECu; evnt[8] = 1u;   /* -5000 us, position 1 */
        evnt[12] = 0x48u; evnt[13] = 0xE3u; evnt[18] = 1u; /* -7344 us, same position */
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_status(&before);
        brk_mc_start(10);
        brk_play(1);
        mc_run_counting(4u * sl, 0);
        brk_status(&after);
        first_count = (uint8_t)(after.mc_hits[0] - before.mc_hits[0]);
        CHECK(first_count == 2u, "same lane and position: both early event records are scheduled (%u)", first_count);
        CHECK((brk_mc_caps() & MC_CAPS_ARTICULATION) == 0u,
              "device does not advertise articulation before the renderer implements it");
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_groove(0, 0, 0, 0);
    }
    /* bounded late-event queue (MG-005): a fifth late hit falls back to immediate playback, not silence */
    {
        static const uint8_t gruv[MC_GRUV_BYTES + 4u] = {
            1u, 12u, 50u, 0u, 0u, 0u, 1u, 0u, 11u, 5u, 0u, 0u
        };
        static const uint8_t evnt[5u * MC_EVNT_BYTES] = {
            0u, 127u, 0x88u, 0x13u, 0u, 0u, 0u, 100u, 0u, 0u,
            0u, 127u, 0x88u, 0x13u, 0u, 0u, 0u, 100u, 0u, 0u,
            0u, 127u, 0x88u, 0x13u, 0u, 0u, 0u, 100u, 0u, 0u,
            0u, 127u, 0x88u, 0x13u, 0u, 0u, 0u, 100u, 0u, 0u,
            0u, 127u, 0x88u, 0x13u, 0u, 0u, 0u, 100u, 0u, 0u
        };
        float peak = 0.0f;
        mc_scene_init(&mc_sc[11], 1u);
        mc_sc[11].lane[0].flags = MCL_ON;
        mc_sc[11].lane[0].role = ROLE_KICK;
        mc_sc[11].lane[0].hits = 0u;
        mc_sc[11].lane[0].len = 8u;
        mc_sc[11].lane[0].rate = MCR_8;
        mc_sc[11].lane[0].level = 100u;
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(11);
        brk_mc_next(11);
        brk_play(1);
        mc_run(128u, &peak, 0);   /* 128 samples is before the requested +5 ms offset (~220 samples) */
        CHECK(peak > 0.05f, "late-event queue full: the fifth event falls back to immediate sound (%.3f)", peak);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_groove(0, 0, 0, 0);
    }
    /* the per-event timbre (MG-006): the event's own brightness, a lowpass on its voice */
    {
        static const uint8_t gruv[MC_GRUV_BYTES + 4u] = { 1u, 12u, 50u, 0u, 0u, 0u, 1u, 0u, 9u, 1u, 0u, 0u };
        static uint8_t evnt[MC_EVNT_BYTES] = { 0u, 110u, 0u, 0u, 0u, 3u, 128u, 100u, 1u, 0u };
        static float buf[65536];
        float bright = 0.0f, dark = 0.0f;
        uint32_t n = 65536u, i, m;
        mc_scene_init(&mc_sc[9], 1u);
        mc_sc[9].lane[0].flags = MCL_ON;
        mc_sc[9].lane[0].role = ROLE_KICK;
        mc_sc[9].lane[0].hits = 2u;
        mc_sc[9].lane[0].len = 8u;
        mc_sc[9].lane[0].rate = MCR_8;
        mc_sc[9].lane[0].level = 100u;
        evnt[6] = 0u;                         /* the timbre: none */
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_mc_start(9);
        brk_mc_next(9);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(9);
        brk_play(1);
        for (i = 0; i < n; i += BLOCK) {
            m = n - i > BLOCK ? BLOCK : n - i;
            brk_render(buf + i, m);
        }
        for (i = 1; i < n; i++)              /* the brightness: the energy of the differences */
            bright += fabsf(buf[i] - buf[i - 1]);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        evnt[6] = 200u;                      /* the timbre: dark */
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_mc_start(9);
        brk_play(1);
        for (i = 0; i < n; i += BLOCK) {
            m = n - i > BLOCK ? BLOCK : n - i;
            brk_render(buf + i, m);
        }
        for (i = 1; i < n; i++)
            dark += fabsf(buf[i] - buf[i - 1]);
        CHECK(bright > 0.0f && dark > 0.0f, "the event is heard (%.3f, %.3f)", bright, dark);
        CHECK(dark < bright * 0.8f, "the timbre darkens it (%.3f < %.3f)", dark, bright);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* the pitch (MG-007): the lane's own, and the event's on top of it */
    {
        static const uint8_t gruv[MC_GRUV_BYTES + 4u] = { 1u, 12u, 50u, 0u, 0u, 0u, 1u, 0u, 9u, 1u, 0u, 0u };
        static uint8_t evnt[MC_EVNT_BYTES] = { 0u, 110u, 0u, 0u, 0u, 3u, 128u, 100u, 1u, 0u };
        static float buf[65536];
        uint32_t n = 65536u, i, m, z0 = 0u, z1 = 0u, z2 = 0u;
        mc_scene_init(&mc_sc[9], 1u);
        mc_sc[9].lane[0].flags = MCL_ON;
        mc_sc[9].lane[0].role = ROLE_KICK;
        mc_sc[9].lane[0].hits = 2u;
        mc_sc[9].lane[0].len = 8u;
        mc_sc[9].lane[0].rate = MCR_8;
        mc_sc[9].lane[0].level = 100u;
        evnt[4] = 0u;                         /* the event's pitch: none */
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_mc_start(9);
        brk_mc_next(9);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(9);
        brk_play(1);
        for (i = 0; i < n; i += BLOCK) {
            m = n - i > BLOCK ? BLOCK : n - i;
            brk_render(buf + i, m);
        }
        for (i = 1; i < n; i++)
            if ((buf[i] > 0.0f) != (buf[i - 1] > 0.0f) && fabsf(buf[i]) > 0.01f)
                z0++;
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        mc_sc[9].lane[0].pitch = 12;          /* the lane's own pitch: an octave up */
        brk_mc_start(9);
        brk_play(1);
        for (i = 0; i < n; i += BLOCK) {
            m = n - i > BLOCK ? BLOCK : n - i;
            brk_render(buf + i, m);
        }
        for (i = 1; i < n; i++)
            if ((buf[i] > 0.0f) != (buf[i - 1] > 0.0f) && fabsf(buf[i]) > 0.01f)
                z1++;
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        mc_sc[9].lane[0].pitch = 0;
        evnt[4] = 12;                         /* the event's own pitch, on top of the lane's */
        brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
        brk_mc_start(9);
        brk_play(1);
        for (i = 0; i < n; i += BLOCK) {
            m = n - i > BLOCK ? BLOCK : n - i;
            brk_render(buf + i, m);
        }
        for (i = 1; i < n; i++)
            if ((buf[i] > 0.0f) != (buf[i - 1] > 0.0f) && fabsf(buf[i]) > 0.01f)
                z2++;
        CHECK(z0 > 0u && z1 > z0 && z2 > z0, "the pitch is heard (%u, %u, %u)", z0, z1, z2);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* a triplet roll: a snare lane at 1/8 with ratchet x3 under hats at 1/16T: 24 snare hits a bar, the hats
     * between never cutting it short; SWELL climbs without speeding up */
    {
        uint32_t snares = 0u, last = 0u, t2, rolls;
        mc_scene_init(&mc_sc[5], 1u);
        mc_sc[5].lane[1].flags = MCL_ON | MCL_SWELL;
        mc_sc[5].lane[1].hits = 0xFFu;
        mc_sc[5].lane[1].len = 8u;
        mc_sc[5].lane[1].rate = MCR_8;
        mc_sc[5].lane[1].ratchet = 3u;
        mc_sc[5].lane[1].rise = 5;
        mc_sc[5].lane[2].flags = MCL_ON;
        mc_sc[5].lane[2].hits = 0xFFFFFFu;
        mc_sc[5].lane[2].len = 24u;
        mc_sc[5].lane[2].rate = MCR_16T;
        brk_mc_start(5);
        brk_mc_next(5);
        mc_zero_counts();
        brk_play(1);
        for (t2 = 0; t2 < 16u * sl; t2 += 64u) {   /* the voice's slice every 64 samples: a snare's */
            mc_run(64u, 0, 0);
            brk_status(&st);
            snares += st.mc_slice[1] >= 0 && (r[st.mc_slice[1]] >> ROLE_SNARE & 1u);
            last++;
        }
        CHECK(snares * 100u >= last * 97u, "triplet roll: the hats never cut it (%u of %u looks a snare)", snares, last);
        mc_count();
        rolls = mc_hits_total[1];
        CHECK(rolls == 8u, "triplet roll: 8 events a bar (x3: 24 triplet snares), SWELL not speeding up (%u)", rolls);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* HOME's GATE on a scene's hits: at 25 % each hit's slice is silent after its first quarter */
    {
        uint32_t k3, quiet = 0u, loud = 0u;
        float p1;
        mc_scene_init(&mc_sc[6], 1u);
        mc_sc[6].flags = MCS_BREAK;
        mc_sc[6].lane[0].flags = MCL_ON;
        mc_sc[6].lane[0].hits = 0x1111u;
        brk_gate(0.25f);
        brk_mc_start(6);
        brk_mc_next(6);
        brk_play(1);
        mc_run(16u * sl, 0, 0);
        for (k3 = 0; k3 < 16u; k3++) {   /* each 16th: its first quarter, then the rest */
            mc_run(sl / 2u, &p1, 0);
            if (k3 % 4u == 0u)
                loud += p1 > 0.05f;
            mc_run(sl - sl / 2u, &p1, 0);
            quiet += p1 < 0.01f;
        }
        CHECK(loud >= 3u && quiet >= 14u, "GATE: a scene's hits gated as the loop's (%u loud starts, %u quiet tails)",
              loud, quiet);
        brk_gate(1.0f);
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    /* VARY: a snare lane on 5 and 13 grows ghost notes round them, never the same bar twice, the same run twice */
    {
        static float a1[4u * 44100u], a2[4u * 44100u];
        uint32_t plain, varied;
        mc_scene_init(&mc_sc[4], 4u);
        mc_sc[4].lane[1].flags = MCL_ON;
        mc_sc[4].lane[1].hits = 0x1010u;
        brk_mc_start(4);
        brk_mc_next(4);
        mc_zero_counts();
        brk_play(1);
        mc_run_counting(64u * sl, 0);
        plain = mc_hits_total[1];
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        mc_sc[4].lane[1].vary = 80u;
        brk_mc_start(4);
        mc_zero_counts();
        brk_play(1);
        mc_run_counting(64u * sl, a1);
        varied = mc_hits_total[1];
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
        brk_mc_start(4);
        brk_play(1);
        mc_run_counting(64u * sl, a2);
        CHECK(plain == 8u && varied > plain + 2u, "VARY: ghost notes round the backbeat (%u hits, %u plain)", varied,
              plain);
        CHECK(!memcmp(a1, a2, 64u * sl * sizeof(float)), "VARY: the same seed, the same variations");
        brk_play(0);
        mc_run(BLOCK * 8u, 0, 0);
    }
    brk_mc_scenes(0);
}

/* the overlay effects (ARP): nothing on passes the sound through as it is; each one changes it and stays bounded;
 * CUT silences, DIVE darkens over its sweep, RISE thins; let go, it fades back to the dry sound */
/* the ARP page's black-key effects (PRD B-55): pad voices after the chain, on the whole mix */
static uint32_t pad_mem[BRK_PAD_V * BRK_PAD_ST_BYTES / 4u];
static float pad_zero[BLOCK];
static void pad_run(uint32_t from, uint32_t n, int as_post)   /* buf[from..] through, into bl / br [from..] */
{
    uint32_t i, m;
    for (i = from; i < from + n; i += m) {
        m = from + n - i < BLOCK ? from + n - i : BLOCK;
        if (as_post)                     /* (the drums around the chain: joined after it) */
            brk_fx_process(pad_zero, 0, 0, 0, buf + i, bl + i, br + i, m, 0u);
        else
            brk_fx_process(buf + i, 0, 0, 0, 0, bl + i, br + i, m, 0u);
    }
}
static void pad_put(uint32_t v, int type, float p0, float mix, uint32_t on)
{
    brk_fxslot_t s;
    brk_fxx_t d;
    brk_fx_slot_defaults(&s, type);
    s.p[0] = p0;
    s.mix = mix;
    brk_fxx_defaults(&d, type);
    brk_fx_set_fxx(BRK_FX_SLOTS + v, &d);
    brk_fx_set_pad(v, &s, on);
}
static void test_pad_fx(void)
{
    brk_fx_t p;
    uint32_t i, v, t, ok;
    float dry, held, after, post, mb, tail, late;
    brk_fx_defaults(&p);                 /* the chain: all bypassed */
    mb_fresh();
    brk_fx_set(&p);
    brk_fx_pad_buffer(pad_mem);
    tone_in(8000.0f, 0.25f, BRK_SR);
    pad_run(0u, BRK_SR / 4u, 0);
    dry = peak_of(bl, BRK_SR / 8u, BRK_SR / 4u);
    pad_put(0u, FXT_FILTER, 0.2f, 1.0f, 1u);   /* a low-pass at ~100 Hz, held */
    pad_run(0u, BRK_SR / 4u, 0);
    held = peak_of(bl, BRK_SR / 8u, BRK_SR / 4u);
    CHECK(dry > 0.2f && held < 0.01f && (brk_fx_pad_busy() & 1u),
          "PAD FX: a low-pass pad held takes the 8 kHz away (%.3f -> %.4f)", dry, held);
    pad_put(0u, FXT_FILTER, 0.2f, 1.0f, 0u);   /* let go: back to the dry sound in a few ms */
    pad_run(0u, BRK_SR / 4u, 0);
    after = peak_of(bl, BRK_SR / 20u, BRK_SR / 4u);
    CHECK(after > dry * 0.97f && !(brk_fx_pad_busy() & 1u),
          "PAD FX: let go, the dry sound back (%.3f) and the voice free", after);
    tone_in(8000.0f, 0.25f, BRK_SR);
    pad_put(1u, FXT_FILTER, 0.2f, 1.0f, 1u);   /* on the whole mix: the drums around the chain too */
    pad_run(0u, BRK_SR / 4u, 1);
    post = peak_of(bl, BRK_SR / 8u, BRK_SR / 4u);
    pad_put(1u, FXT_FILTER, 0.2f, 1.0f, 0u);
    pad_put(2u, FXT_MBCOMP, 1.0f, 1.0f, 1u);   /* MB COMP is the chain's: on a pad it passes */
    pad_run(0u, BRK_SR / 4u, 0);
    mb = peak_of(bl, BRK_SR / 8u, BRK_SR / 4u);
    pad_put(2u, FXT_MBCOMP, 1.0f, 1.0f, 0u);
    CHECK(post < 0.01f && fabsf(mb - dry) < 0.01f, "PAD FX: on the drums after the chain too (%.4f); MB COMP passes (%.3f)",
          post, mb);
    for (i = 0; i < BRK_SR; i++)         /* a burst of tone, then silence: a REVERB pad's tail rings on after it */
        buf[i] = i < BRK_SR / 10u ? 0.4f * sinf(6.2831853f * 440.0f * (float)i / (float)BRK_SR) : 0.0f;
    pad_put(3u, FXT_REVERB, 0.3f, 0.6f, 1u);
    pad_run(0u, BRK_SR / 8u, 0);
    pad_put(3u, FXT_REVERB, 0.3f, 0.6f, 0u);
    pad_run(BRK_SR / 8u, BRK_SR / 2u - BRK_SR / 8u, 0);
    tail = peak_of(bl, BRK_SR / 4u, BRK_SR / 2u);
    t = brk_fx_pad_busy() >> 3 & 1u;
    for (i = 0; i < 40u; i++)            /* 10 s more */
        pad_run(BRK_SR / 2u, BRK_SR / 4u, 0);
    late = peak_of(bl, BRK_SR / 2u, BRK_SR * 3u / 4u);
    CHECK(tail > 0.002f && t && late < 0.001f && !(brk_fx_pad_busy() >> 3 & 1u),
          "PAD FX: a REVERB pad let go rings on (%.4f, busy %u), then quiet and free (%.5f)", tail, t, late);
    for (ok = 1u, t = FXT_FILTER; t < BRK_NFXT; t++) {   /* every effect, four voices at once, bounded */
        tone_in(220.0f, 0.5f, BRK_SR);
        for (v = 0; v < BRK_PAD_V; v++)
            pad_put(v, (int)((t + v - 1u) % (BRK_NFXT - 1u) + 1u), 0.8f, 1.0f, 1u);
        pad_run(0u, BRK_SR / 4u, 0);
        held = peak_of(bl, 0u, BRK_SR / 4u);
        if (!(held < 1.01f) || held != held)
            ok = 0u;
    }
    for (v = 0; v < BRK_PAD_V; v++)
        pad_put(v, FXT_EMPTY, 0.0f, 0.0f, 0u);
    CHECK(ok, "PAD FX: every effect on four pads at once stays in range");
    brk_fx_pad_buffer(0);
}

/* the overlay (PRD_PACKS PO-001): a 4-bar loop of ADPCM played from its bytes, on the transport's bars: at each bar
 * line its sample is that bar's first; silent while stopped and while it's off; HALF slows it; a new one mid-play
 * starts on the bar */
static uint8_t ov_enc(int16_t x, int16_t *pred, uint8_t *idx)   /* IMA ADPCM, as the page's adpcmEncode */
{
    static const int16_t ST[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
        55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
        494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
        3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
    static const int8_t AD[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    int32_t d = x - *pred, step = ST[*idx], diff = step >> 3, p;
    uint8_t code = 0;
    if (d < 0) { code = 8; d = -d; }
    if (d >= step) { code |= 4; d -= step; diff += step; }
    if (d >= step >> 1) { code |= 2; d -= step >> 1; diff += step >> 1; }
    if (d >= step >> 2) { code |= 1; diff += step >> 2; }
    p = *pred + ((code & 8) ? -diff : diff);
    *pred = (int16_t)(p > 32767 ? 32767 : p < -32768 ? -32768 : p);
    p = *idx + AD[code & 7];
    *idx = (uint8_t)(p < 0 ? 0 : p > 88 ? 88 : p);
    return code;
}
static void ov_render(float *out, float *ovo, uint32_t n)   /* blocks of BLOCK, the overlay's samples beside them */
{
    uint32_t i;
    for (i = 0; i < n; i += BLOCK) {
        brk_overlay_out(ovo + i);
        brk_render(out + i, n - i < BLOCK ? n - i : BLOCK);
    }
}

/* the layer (docs/PRD_LAYER.md): two loops, each on its own bar; a tap waits for the bar, an unquantised one comes in
 * at once in phase; one loop voice: the newer loop takes it; a shot plays from its start, at +12 in half the time; a
 * choke cuts the other; a CUT shot silences the break and lets it back; a GATE shot stops on release */
static uint32_t ly_tone(uint8_t *d, uint32_t len, float cyc, float lvl)   /* a sweep of ADPCM */
{
    int16_t pred = 0;
    uint8_t idx = 0;
    uint32_t i;
    memset(d, 0, (len + 1u) / 2u);
    for (i = 0; i < len; i++) {
        int16_t x = (int16_t)(lvl * sinf(6.2831853f * cyc * (float)i / (float)len) + 0.3f * lvl * (float)i / (float)len);
        d[i >> 1] |= (uint8_t)(ov_enc(x, &pred, &idx) << (4u * (i & 1u)));
    }
    return len;
}
static float ly_peak(float *out, float *ovo, uint32_t n, uint32_t from)
{
    uint32_t i;
    float pk = 0.0f;
    ov_render(out, ovo, n);
    for (i = from; i < n; i++)
        pk = fmaxf(pk, fabsf(ovo[i]));
    return pk;
}
static void test_layer(void)
{
    static uint8_t la[60000], lb[60000], sa[20000], sb[20000];
    static int16_t dec[120000];
    static float out[200000], ovo[200000];
    uint32_t bar = (uint32_t)(BRK_SR * 240.0f / BPM + 0.5f), la_len = 4u * (uint32_t)(RATE * 240.0f / BPM);
    uint32_t sl = (uint32_t)(RATE * 0.25f), i, t;
    adpcm_state_t st = {0, 0, 0};
    brk_ly_status_t ls;
    float pk, e;
    if (la_len > 2u * sizeof la)
        la_len = 2u * (uint32_t)sizeof la;
    ly_tone(la, la_len, 3.0f, 12000.0f);
    ly_tone(lb, la_len / 2u, 5.0f, 9000.0f);
    ly_tone(sa, sl, 40.0f, 14000.0f);
    ly_tone(sb, sl, 70.0f, 14000.0f);
    adpcm_decode(&st, la, dec, la_len);
    fresh(2u);
    brk_overlay_on(1);
    brk_ly_voices(2u, 4u);
    brk_ly_loop(0u, la, la_len, 4u);
    brk_ly_loop_cfg(0u, BRK_LY_LCFG(100u, 0, 0u, 16u, LYQ_BAR, 0u));
    brk_ly_loop(1u, lb, la_len / 2u, 2u);
    brk_ly_loop_cfg(1u, BRK_LY_LCFG(100u, 0, 0u, 16u, LYQ_NOW, 0u));
    brk_reset();
    brk_play(1);
    pk = ly_peak(out, ovo, 2048u, 0u);
    CHECK(pk < 1e-6f, "no loop on: the layer silent (%g)", (double)pk);
    ov_render(out, ovo, bar / 2u - 2048u);   /* mid bar 1: a tap on loop 0 (to the bar) */
    brk_ly_want(0u, 1, LYQ_BAR);
    pk = ly_peak(out, ovo, 4096u, 0u);
    brk_ly_status(&ls);
    CHECK(pk < 1e-6f && (ls.pend & 1u) && !(ls.on & 1u), "a tap waits for the bar (peak %g, pend %u)", (double)pk,
          (unsigned)ls.pend);
    {                                    /* to bar 2's first sample: loop 0 comes in on its bar 2 */
        uint32_t done = bar / 2u + 2048u;
        while (done + BLOCK <= bar + 300u) {
            ov_render(out, ovo, BLOCK);
            done += BLOCK;
        }
        brk_ly_status(&ls);
        CHECK(ls.on & 1u, "on at the bar line (on %u)", (unsigned)ls.on);
        ov_render(out, ovo, 2048u);
        e = fabsf(ovo[2047] - (float)dec[la_len / 4u + (uint32_t)((float)(done + 2047u - bar) * RATE / BRK_SR)] / 32768.0f);
        CHECK(e < 0.05f, "it plays its bar 2 (off by %g)", (double)e);
    }
    brk_ly_want(1u, 1, LYQ_NOW);         /* loop 1 at once (a hold): both sound, in phase */
    pk = ly_peak(out, ovo, 4096u, 512u);
    brk_ly_status(&ls);
    CHECK((ls.on & 3u) == 3u && pk > 0.3f, "two loops at once (on %u, peak %g)", (unsigned)ls.on, (double)pk);
    brk_ly_voices(1u, 4u);               /* one loop voice: one of them gives way */
    ov_render(out, ovo, 4096u);
    brk_ly_status(&ls);
    CHECK((ls.on & 3u) == 1u || (ls.on & 3u) == 2u, "one voice: one loop (on %u)", (unsigned)ls.on);
    brk_ly_want(0u, 1, LYQ_NOW);         /* a loop turned on takes the voice from the older */
    ov_render(out, ovo, 4096u);
    brk_ly_status(&ls);
    CHECK((ls.on & 3u) == 1u, "the newer loop takes the voice (on %u)", (unsigned)ls.on);
    brk_ly_want(0u, 0, LYQ_NOW);
    brk_ly_want(1u, 0, LYQ_NOW);
    pk = ly_peak(out, ovo, 2048u, 1024u);
    CHECK(pk < 1e-4f, "off at once: silent in 5 ms (%g)", (double)pk);
    /* the shots */
    brk_ly_shot(0u, sa, sl, (uint32_t)RATE, 0u);
    brk_ly_shot(1u, sb, sl, (uint32_t)RATE, 0u);
    brk_ly_shot_cfg(0u, BRK_LY_SCFG(100u, 0, 0u, SHM_ONE, 1u, SHQ_NOW, 0u));
    brk_ly_shot_cfg(1u, BRK_LY_SCFG(100u, 12, 0u, SHM_ONE, 1u, SHQ_NOW, 0u));
    brk_ly_hit(0u, 1);
    brk_ly_hit(0u, 0);
    ov_render(out, ovo, BLOCK);
    brk_ly_status(&ls);
    CHECK(ls.shots & 1u, "a shot sounds (%04x)", (unsigned)ls.shots);
    for (t = 0, i = 0; i < 40u && (ls.shots & 1u); i++) {
        ov_render(out, ovo, BLOCK);
        brk_ly_status(&ls);
        t += BLOCK;
    }
    e = (float)t / ((float)sl * BRK_SR / RATE);
    CHECK(e > 0.85f && e < 1.15f, "a one shot plays its length (%g of it)", (double)e);
    brk_ly_hit(1u, 1);                   /* +12: half as long */
    for (t = 0, i = 0; i < 40u; i++) {
        ov_render(out, ovo, BLOCK);
        brk_ly_status(&ls);
        if (!(ls.shots & 2u))
            break;
        t += BLOCK;
    }
    e = (float)t / ((float)sl * BRK_SR / RATE);
    CHECK(e > 0.35f && e < 0.65f, "at +12 it lasts half as long (%g)", (double)e);
    brk_ly_hit(0u, 1);                   /* choke group 1: shot 1 cuts shot 0 */
    ov_render(out, ovo, BLOCK);
    brk_ly_hit(1u, 1);
    ov_render(out, ovo, 2u * BLOCK);
    brk_ly_status(&ls);
    CHECK((ls.shots & 3u) == 2u, "a choke cuts the other within 5 ms (%04x)", (unsigned)ls.shots);
    for (i = 0; i < 20u; i++)
        ov_render(out, ovo, BLOCK);
    /* a CUT shot: the break silent under it, back after */
    brk_ly_shot_cfg(0u, BRK_LY_SCFG(100u, 0, 0u, SHM_ONE, 0u, SHQ_NOW, 1u));
    {
        float before = 0.0f, during = 0.0f, after = 0.0f;
        ov_render(out, ovo, 4096u);
        for (i = 0; i < 4096u; i++)
            before = fmaxf(before, fabsf(out[i]));
        brk_ly_hit(0u, 1);
        ov_render(out, ovo, 4096u);
        for (i = 1024u; i < 4096u; i++)
            during = fmaxf(during, fabsf(out[i]));
        for (i = 0; i < 40u; i++)
            ov_render(out, ovo, BLOCK);
        ov_render(out, ovo, 8192u);
        for (i = 0; i < 8192u; i++)
            after = fmaxf(after, fabsf(out[i]));
        CHECK(before > 0.05f && during < 1e-4f && after > 0.05f, "a CUT shot silences the break, then lets it back "
              "(%g, %g, %g)", (double)before, (double)during, (double)after);
    }
    brk_ly_shot_cfg(1u, BRK_LY_SCFG(100u, -12, 0u, SHM_GATE, 0u, SHQ_NOW, 0u));   /* a GATE shot let go: gone */
    brk_ly_hit(1u, 1);
    ov_render(out, ovo, 2u * BLOCK);
    brk_ly_hit(1u, 0);
    ov_render(out, ovo, 2u * BLOCK);
    brk_ly_status(&ls);
    CHECK(!(ls.shots & 2u), "a GATE shot stops on release (%04x)", (unsigned)ls.shots);
    brk_ly_shot_cfg(1u, BRK_LY_SCFG(100u, 0, 0u, SHM_ONE, 0u, SHQ_BEAT, 0u));   /* to the beat */
    for (i = 0; i < 4u * 8u; i++) {
        brk_status_t bs;
        ov_render(out, ovo, 64u);
        brk_status(&bs);
        if ((bs.bar16 & 3) == 1)
            break;
    }
    brk_ly_hit(1u, 1);
    ov_render(out, ovo, BLOCK);
    brk_ly_status(&ls);
    CHECK(!(ls.shots & 2u), "a shot on the beat waits for it (%04x)", (unsigned)ls.shots);
    for (i = 0; i < 4u * 120u && !(ls.shots & 2u); i++) {
        ov_render(out, ovo, 64u);
        brk_ly_status(&ls);
    }
    CHECK(ls.shots & 2u, "then sounds on it");
    brk_play(0);                         /* stopped: the loops off */
    ov_render(out, ovo, BLOCK);
    brk_ly_loop_cfg(0u, BRK_LY_LCFG(100u, 0, 0u, 16u, LYQ_BAR, 1u));   /* AUTO: on when PLAY starts */
    brk_play(1);
    pk = ly_peak(out, ovo, 8192u, 4096u);
    brk_ly_status(&ls);
    CHECK((ls.on & 1u) && pk > 0.1f, "an AUTO loop comes on with PLAY (on %u, peak %g)", (unsigned)ls.on, (double)pk);
    brk_play(0);
    ov_render(out, ovo, BLOCK);
    brk_ly_status(&ls);
    CHECK(!ls.on, "stop: every loop off (%u)", (unsigned)ls.on);
    brk_ly_clear();
    brk_overlay_out(0);
    brk_ly_voices(1u, 3u);
}

static void test_overlay(void)
{
    static uint8_t ov[60000];
    static int16_t dec[120000];
    static float out[330000], ovo[330000];
    uint32_t len = 4u * (uint32_t)(RATE * 240.0f / BPM), i, bar = (uint32_t)(BRK_SR * 240.0f / BPM + 0.5f);
    int16_t pred = 0;
    uint8_t idx = 0;
    adpcm_state_t st = {0, 0, 0};
    float peak = 0.0f, e;
    if (len > 2u * sizeof ov)
        len = 2u * (uint32_t)sizeof ov;
    memset(ov, 0, sizeof ov);
    for (i = 0; i < len; i++) {          /* a slow sweep: every place in it a different value */
        int16_t x = (int16_t)(12000.0f * sinf(6.2831853f * 3.0f * (float)i / (float)len) + 6000.0f * (float)i / (float)len);
        ov[i >> 1] |= (uint8_t)(ov_enc(x, &pred, &idx) << (4u * (i & 1u)));
    }
    adpcm_decode(&st, ov, dec, len);
    fresh(2u);
    brk_overlay(ov, len, 4u);
    brk_overlay_on(1);
    brk_overlay_out(ovo);
    ov_render(out, ovo, 2048u);         /* stopped: nothing */
    for (i = 0; i < 2048u; i++)
        peak = fmaxf(peak, fabsf(ovo[i]));
    CHECK(peak < 1e-6f, "the overlay is silent while stopped (%g)", (double)peak);
    brk_reset();
    brk_play(1);
    {                                    /* through bar 1 to bar 2's first sample (blocks of 256) */
        uint32_t done = 0u;
        while (done + BLOCK <= bar) {
            ov_render(out, ovo, BLOCK);
            done += BLOCK;
        }
        ov_render(out, ovo, bar - done + 1u);
        e = fabsf(ovo[bar - done] - (float)dec[len / 4u] / 32768.0f);
        CHECK(e < 0.02f, "bar 2 begins on the overlay's bar 2 (off by %g of %g)", (double)e, (double)dec[len / 4u] / 32768.0);
    }
    peak = 0.0f;
    ov_render(out, ovo, 4096u);
    for (i = 0; i < 4096u; i++)
        peak = fmaxf(peak, fabsf(ovo[i]));
    CHECK(peak > 0.1f && peak < 1.0f, "the overlay sounds while playing (peak %g)", (double)peak);
    brk_overlay_on(0);                   /* a built-in break: it fades out */
    ov_render(out, ovo, 8192u);
    CHECK(fabsf(ovo[8191]) < 1e-3f, "off: it fades to nothing (%g)", (double)ovo[8191]);
    brk_overlay_on(1);
    brk_overlay(0, 0u, 0u);              /* taken away mid-play: silence, no crash */
    ov_render(out, ovo, 1024u);
    CHECK(fabsf(ovo[1023]) < 1e-3f, "no overlay: silence");
    brk_overlay(ov, len, 4u);            /* back, mid-bar: on its bar again from the mark before it */
    ov_render(out, ovo, 4u * bar);
    peak = 0.0f;
    for (i = 0; i < 4u * bar; i++)
        peak = fmaxf(peak, fabsf(ovo[i]));
    CHECK(peak > 0.1f && peak < 1.0f, "a new overlay mid-play sounds (peak %g)", (double)peak);
    brk_overlay_out(0);
    brk_overlay(0, 0u, 0u);
    brk_play(0);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        outdir = argv[1];
    test_straight();
    test_in_order();
    test_tempo_accuracy();
    test_free_play();
    test_quantised_key();
    test_hold_loops();
    test_key_loops();
    test_split();
    test_clock();
    test_keep_length();
    test_gate();
    test_swap();
    test_snap();
    test_reverb();
    test_stop();
    test_delay_slices();
    test_eq_tape();
    test_mb_clip();
    test_mb_detail();
    test_fx_extras();
    test_overload();
    test_dj_break();
    test_mc_map();
    test_mc_roles();
    test_mc_lanes();
    test_mc_queue();
    test_mc_play();
    test_generator();
    test_pads();
    test_fx();
    test_adpcm();
    test_pad_settings();
    test_dub();
    test_styles();
    test_pad_fx();
    test_overlay();
    test_layer();
    printf("%d of %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
