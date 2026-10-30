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
    brk_set_break(&brk);
    brk_tempo(BPM);
    brk_pitch(0.0f);
    brk_swing(0.5f);
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

static void test_fx(void)
{
    brk_fx_t p;
    uint32_t n = brk_step_len_samples() * 32u, i, ok = 1, f;
    float peak = 0.0f;
    fresh(1);
    brk_play(1);
    render(buf, n, 0, 0, 0);
    for (f = 0; f <= BRK_NFX; f++) {     /* each effect alone, then all four */
        brk_fx_init();
        brk_fx_defaults(&p);
        for (i = 0; i < BRK_NFX; i++)
            p.on[i] = (uint8_t)(f == BRK_NFX || i == f);
        brk_fx_set(&p);
        for (i = 0; i < n; i += BLOCK)
            brk_fx_process(buf + i, bl + i, br + i, n - i < BLOCK ? n - i : BLOCK, 0u);
        for (i = 0; i < n; i++) {
            if (!(fabsf(bl[i]) <= 1.0f) || !(fabsf(br[i]) <= 1.0f))
                ok = 0;
            if (fabsf(bl[i]) > peak)
                peak = fabsf(bl[i]);
        }
        if (f == BRK_NFX)
            wav("fx_all.wav", bl, br, n);
    }
    CHECK(ok, "FX output stays finite and inside -1..1");
    CHECK(peak > 0.01f, "FX output is not silent (peak %.3f)", peak);
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

int main(int argc, char **argv)
{
    if (argc > 1)
        outdir = argv[1];
    test_straight();
    test_tempo_accuracy();
    test_free_play();
    test_quantised_key();
    test_hold_loops();
    test_generator();
    test_pads();
    test_fx();
    test_adpcm();
    printf("%d of %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
