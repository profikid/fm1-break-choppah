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
#include "sfx.h"

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
            brk_fx_process(buf + i, 0, 0.0f, bl + i, br + i, n - i < BLOCK ? n - i : BLOCK, 0u);
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
    CHECK(pk[1] < pk[0] * 0.8f && pk[2] < pk[1] * 0.8f && pk[3] < pk[2] * 0.8f,
          "roll decay: %.3f %.3f %.3f %.3f", pk[0], pk[1], pk[2], pk[3]);
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
        brk_fx_process(buf + i, 0, 0.0f, bl + i, br + i, n - i < BLOCK ? n - i : BLOCK, 1u << PAD_DUB);
    brk_play(0);
    brk_pad(PAD_DUB, 0);
    for (i = 0; i < BLOCK; i++)
        buf[i] = 0.0f;
    for (i = 0; i < s * 8u; i += BLOCK) {    /* silence in, two bars of it */
        uint32_t j;
        brk_fx_process(buf, 0, 0.0f, bl, br, BLOCK, 0u);
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
static void test_sfx(void)
{
    static float o[BRK_SR * 5u];
    sfx_set_t set;
    uint32_t id, silent = 0u, stuck = 0u, wild = 0u, i;
    char name[64];
    sfx_init();
    sfx_defaults(&set);
    set.level = 1.0f;
    sfx_set(&set);
    for (id = 0; id < SFX_N; id++) {
        uint32_t n = BRK_SR * 3u;
        float pk = 0.0f;
        sfx_trigger((int)id);
        for (i = 0; i < n; i += BLOCK) {
            uint32_t j;
            if (i / BLOCK == (BRK_SR / 2u) / BLOCK)
                sfx_release((int)id);    /* the held ones let go after half a second */
            sfx_render(o + i, BLOCK);
            for (j = 0; j < BLOCK; j++) {
                if (!(fabsf(o[i + j]) <= 1.0f))
                    wild++;
                if (fabsf(o[i + j]) > pk)
                    pk = fabsf(o[i + j]);
            }
        }
        if (pk < 0.05f) {
            silent++;
            printf("  silent: %s (peak %.3f)\n", SFX_NAME[id], pk);
        }
        if (sfx_playing())
            stuck++;
        snprintf(name, sizeof name, "sfx_%02u_%s.wav", id, SFX_NAME[id]);
        wav(name, o, 0, BRK_SR * 2u);
    }
    CHECK(!silent, "every soundboard sound sounds (%u silent)", silent);
    CHECK(!stuck, "every sound ends (%u still playing after 3 s)", stuck);
    CHECK(!wild, "soundboard output stays inside -1..1 (%u samples outside)", wild);
    /* the siren goes on while held */
    sfx_init();
    sfx_trigger(SFX_SIREN);
    for (i = 0; i < BRK_SR * 2u; i += BLOCK)
        sfx_render(o, BLOCK);
    CHECK(sfx_playing() >> SFX_SIREN & 1u, "the siren still wails after 2 s held");
    sfx_release(SFX_SIREN);
    for (i = 0; i < BRK_SR; i += BLOCK)
        sfx_render(o, BLOCK);
    CHECK(!(sfx_playing() >> SFX_SIREN & 1u), "and stops once let go");
}

/* the pull-up: the break spins back, then starts again from step 0 */
static void test_rewind(void)
{
    brk_status_t st;
    uint32_t s = brk_step_len_samples();
    float pk;
    fresh(1);
    brk_play(1);
    render(buf, s * 6u, 0, 0, 0);
    brk_rewind();
    render(buf, BLOCK, 0, 0, 0);
    brk_status(&st);
    CHECK(st.rewind > 0.0f, "a rewind starts (%.2f)", st.rewind);
    pk = render(buf, BRK_SR / 2u, 0, 0, 0);
    CHECK(pk > 0.05f, "and is heard (peak %.3f)", pk);
    render(buf, BRK_SR / 4u, 0, 0, 0);
    brk_status(&st);
    CHECK(st.rewind == 0.0f && st.step >= 0 && st.step < 4, "then the break starts again from the top (step %d)",
          st.step);
}

/* the soundboard through the dub delay: echoes after the sound has stopped */
static void test_sfx_echo(void)
{
    static int16_t line[32768];
    static float a[BLOCK], z[BLOCK];
    brk_fx_t p;
    sfx_set_t ss;
    uint32_t i, j;
    float tail = 0.0f;
    brk_fx_init();
    brk_fx_defaults(&p);
    brk_fx_set(&p);
    brk_fx_delay_buffer(line, 32768u);
    brk_fx_tempo(170.0f);
    sfx_init();
    sfx_defaults(&ss);
    sfx_set(&ss);
    for (j = 0; j < BLOCK; j++)
        z[j] = 0.0f;
    sfx_trigger(SFX_LASER);
    for (i = 0; i < BRK_SR * 2u; i += BLOCK) {
        sfx_render(a, BLOCK);
        brk_fx_process(z, a, 0.8f, bl, br, BLOCK, 0u);
        if (i > BRK_SR)                  /* the laser is long over by now */
            for (j = 0; j < BLOCK; j++)
                if (fabsf(bl[j]) > tail)
                    tail = fabsf(bl[j]);
    }
    CHECK(tail > 0.005f, "soundboard echoes ring on in the dub delay (peak %.3f)", tail);
    brk_fx_delay_buffer(0, 0u);
}

/* the white keys' own loops (brk_keyset): length, ping-pong, one shot, pitch, and the loop back after */
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

int main(int argc, char **argv)
{
    if (argc > 1)
        outdir = argv[1];
    test_straight();
    test_tempo_accuracy();
    test_free_play();
    test_quantised_key();
    test_hold_loops();
    test_key_loops();
    test_clock();
    test_generator();
    test_pads();
    test_fx();
    test_adpcm();
    test_pad_settings();
    test_dub();
    test_styles();
    test_sfx();
    test_rewind();
    test_sfx_echo();
    printf("%d of %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
