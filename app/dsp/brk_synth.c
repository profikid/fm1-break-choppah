/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's built-in break: an Amen-style bar played by a small synthesised kit into RAM at boot, so the firmware
 * makes noise with no recorded break on board (PRD Q1). The pattern is the well-known shape of the Amen's first bar
 * (kicks on 1 and the "and" of 3, snares on 2 and 4 with ghosts), not a sample of it. */
#include "brk_synth.h"
#include "brk_math.h"

#define GAIN 15000.0f                  /* full scale of one voice at velocity 1 */

static inline void mix(int16_t *d, float x)
{
    int32_t v = (int32_t)*d + (int32_t)(x * GAIN);
    *d = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

/* one bar of 16ths: K kick, S snare, s ghost snare, H hat (every step gets a hat) */
static const char BAR[2][17] = {"K.K.S..s.sKKS..s", "K.K.S..s.sK.S.Ss"};

static void add_kick(int16_t *buf, uint32_t len, uint32_t at, uint32_t rate, float vel)
{
    float ph = 0.0f, f = 150.0f, amp = vel, click = 0.6f * vel;
    uint32_t i;
    for (i = at; i < len && amp > 0.0005f; i++) {
        mix(&buf[i], (brk_sin_turns(ph) * amp + click * brk_sin_turns(ph * 3.1f)) * 0.9f);
        ph += f / (float)rate;
        f = 45.0f + (f - 45.0f) * 0.9993f;   /* the pitch falls from 150 Hz to 45 */
        amp *= 0.99975f;
        click *= 0.97f;
    }
}

static void add_snare(int16_t *buf, uint32_t len, uint32_t at, uint32_t rate, float vel, uint32_t *seed)
{
    float ph = 0.0f, tone = 0.55f * vel, noise = 0.8f * vel, lp = 0.0f, hp = 0.0f;
    uint32_t i;
    for (i = at; i < len && noise > 0.0005f; i++) {
        float n = brk_randf(seed) * 2.0f - 1.0f;
        lp += (n - lp) * 0.55f;              /* a crude band: the noise without its very top and bottom */
        hp = lp - hp * 0.02f;
        mix(&buf[i], brk_sin_turns(ph) * tone + hp * noise);
        ph += 190.0f / (float)rate;
        tone *= 0.9991f;
        noise *= 0.99935f;
    }
}

static void add_hat(int16_t *buf, uint32_t len, uint32_t at, uint32_t rate, float vel, uint32_t *seed)
{
    float amp = 0.22f * vel, prev = 0.0f;
    uint32_t i;
    (void)rate;
    for (i = at; i < len && amp > 0.0005f; i++) {
        float n = brk_randf(seed) * 2.0f - 1.0f;
        mix(&buf[i], (n - prev) * amp);     /* first difference: a high-pass, the hat's hiss */
        prev = n;
        amp *= 0.9980f;
    }
}

void brk_synth_break(int16_t *pcm, uint32_t rate, float bpm, uint32_t bars, brk_break_t *b)
{
    uint32_t step_len = (uint32_t)((float)rate * 60.0f / (bpm * 4.0f));
    uint32_t len = step_len * 16u * bars, i, s, seed = 0xA3E1u;
    for (i = 0; i < len; i++)
        pcm[i] = 0;
    for (s = 0; s < 16u * bars; s++) {
        char c = BAR[(s / 16u) & 1u][s % 16u];
        uint32_t at = s * step_len;
        add_hat(pcm, len, at, rate, (s & 1u) ? 0.6f : 1.0f, &seed);
        if (c == 'K')
            add_kick(pcm, len, at, rate, 1.0f);
        else if (c == 'S')
            add_snare(pcm, len, at, rate, 1.0f, &seed);
        else if (c == 's')
            add_snare(pcm, len, at, rate, 0.35f, &seed);
    }
    b->pcm = pcm;
    b->len = len;
    b->rate = rate;
    b->bpm_x100 = (uint32_t)(bpm * 100.0f + 0.5f);
    b->bars = bars;
    b->nslices = 16u * bars > BRK_MAX_SLICES ? BRK_MAX_SLICES : 16u * bars;
    for (i = 0; i <= b->nslices; i++)
        b->slice[i] = len / b->nslices * i + (len % b->nslices) * i / b->nslices;   /* 32-bit only */
    {
        static const char NAME[] = "SYNTH AMEN";
        for (i = 0; i < sizeof NAME; i++)
            b->name[i] = NAME[i];
    }
}

uint32_t brk_synth_len(uint32_t rate, float bpm, uint32_t bars)
{
    return (uint32_t)((float)rate * 60.0f / (bpm * 4.0f)) * 16u * bars;
}
