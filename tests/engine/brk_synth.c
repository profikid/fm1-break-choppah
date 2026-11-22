/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's built-in breaks: a small drum synth plays one bar of a style into RAM when the break is chosen, so the
 * firmware has breaks to chop with no recording on board (PRD Q1). The styles are drawn after the shapes of
 * well-known breaks and grooves (the Amen's kicks and ghost snares, Think's open hats, Apache's toms, a funky
 * drummer's syncopation, a jungle roller); they are new patterns, not samples. */
#include "brk_synth.h"
#include "brk_math.h"

#define GAIN 15000.0f                  /* full scale of one voice at velocity 1 */

/* one bar of 16ths in two lanes. Drums: K kick, k soft kick, S snare, s ghost snare, T tom, R rim, B kick + rim,
 * C clap. Cymbals: H accented hat, h hat, O open hat, X crash */
typedef struct {
    const char *name;
    float bpm;
    const char *drums, *cym;
    float kick_hz, snare_hz, hat_decay;
} style_t;

static const style_t STYLE[] = {
    {"SYNTH AMEN", 136.9f, "K.K.S..s.sKKS..s", "HhHhHhHhHhHhHhHh", 150.0f, 190.0f, 0.9980f},
    {"SYNTH THINK", 105.0f, "K..K..S.k.K..S.s", "HhOhHhHhHhOhHhHh", 130.0f, 210.0f, 0.9982f},
    {"SYNTH APACHE", 112.0f, "K.T.S.TTK.T.S.Ts", "h.h.h.h.h.h.h.h.", 120.0f, 230.0f, 0.9978f},
    {"SYNTH FUNKY", 100.0f, "K.K.S..sK.Ks.S.s", "HhHhHhHOHhHhHhHh", 140.0f, 200.0f, 0.9981f},
    {"JUNGLE ROLL", 172.0f, "K.....S..KK...S.", "h.h.h.h.h.h.h.hO", 160.0f, 180.0f, 0.9975f},
};
#define NSTYLES (sizeof STYLE / sizeof STYLE[0])

static inline void mix(int16_t *d, float x)
{
    int32_t v = (int32_t)*d + (int32_t)(x * GAIN);
    *d = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static void add_kick(int16_t *buf, uint32_t len, uint32_t at, uint32_t rate, float vel, float f0)
{
    float ph = 0.0f, f = f0, amp = vel, click = 0.6f * vel;
    uint32_t i;
    for (i = at; i < len && amp > 0.0005f; i++) {
        mix(&buf[i], (brk_sin_turns(ph) * amp + click * brk_sin_turns(ph * 3.1f)) * 0.9f);
        ph += f / (float)rate;
        f = 45.0f + (f - 45.0f) * 0.9993f;   /* the pitch falls to 45 Hz */
        amp *= 0.99975f;
        click *= 0.97f;
    }
}

static void add_snare(int16_t *buf, uint32_t len, uint32_t at, uint32_t rate, float vel, float tone_hz,
                      uint32_t *seed)
{
    float ph = 0.0f, tone = 0.55f * vel, noise = 0.8f * vel, lp = 0.0f, hp = 0.0f;
    uint32_t i;
    for (i = at; i < len && noise > 0.0005f; i++) {
        float n = brk_randf(seed) * 2.0f - 1.0f;
        lp += (n - lp) * 0.55f;              /* a crude band: the noise without its very top and bottom */
        hp = lp - hp * 0.02f;
        mix(&buf[i], brk_sin_turns(ph) * tone + hp * noise);
        ph += tone_hz / (float)rate;
        tone *= 0.9991f;
        noise *= 0.99935f;
    }
}

static void add_tom(int16_t *buf, uint32_t len, uint32_t at, uint32_t rate, float vel)
{
    float ph = 0.0f, f = 180.0f, amp = 0.8f * vel;
    uint32_t i;
    for (i = at; i < len && amp > 0.0005f; i++) {
        mix(&buf[i], brk_sin_turns(ph) * amp);
        ph += f / (float)rate;
        f = 95.0f + (f - 95.0f) * 0.9995f;
        amp *= 0.99965f;
    }
}

static void add_rim(int16_t *buf, uint32_t len, uint32_t at, uint32_t rate, float vel, uint32_t *seed)
{
    float ph = 0.0f, amp = 0.7f * vel;
    uint32_t i;
    for (i = at; i < len && amp > 0.0005f; i++) {
        mix(&buf[i], (brk_sin_turns(ph) * 0.8f + (brk_randf(seed) - 0.5f) * 0.4f) * amp);
        ph += 1700.0f / (float)rate;
        amp *= 0.9975f;
    }
}

static void add_clap(int16_t *buf, uint32_t len, uint32_t at, uint32_t rate, float vel, uint32_t *seed)
{
    uint32_t i, burst = rate / 100u;     /* three quick bursts, then the tail */
    float amp = 0.6f * vel, lp = 0.0f;
    for (i = at; i < len && amp > 0.0005f; i++) {
        uint32_t t = i - at;
        float n = brk_randf(seed) * 2.0f - 1.0f, e = t < 3u * burst ? ((t % burst) < burst / 2u ? 1.0f : 0.3f) : 1.0f;
        lp += (n - lp) * 0.5f;
        mix(&buf[i], (n - lp) * amp * e);
        if (t >= 3u * burst)
            amp *= 0.9993f;
    }
}

static void add_hat(int16_t *buf, uint32_t len, uint32_t at, float vel, float decay, uint32_t *seed)
{
    float amp = 0.22f * vel, prev = 0.0f;
    uint32_t i;
    for (i = at; i < len && amp > 0.0005f; i++) {
        float n = brk_randf(seed) * 2.0f - 1.0f;
        mix(&buf[i], (n - prev) * amp);     /* first difference: a high-pass, the hat's hiss */
        prev = n;
        amp *= decay;
    }
}

uint32_t brk_synth_count(void) { return (uint32_t)NSTYLES; }

const char *brk_synth_name(uint32_t style) { return STYLE[style % NSTYLES].name; }

uint32_t brk_synth_style_len(uint32_t style, uint32_t rate)
{
    return (uint32_t)((float)rate * 60.0f / (STYLE[style % NSTYLES].bpm * 4.0f)) * 16u;
}

void brk_synth_style(int16_t *pcm, uint32_t rate, uint32_t style, brk_break_t *b)
{
    const style_t *y = &STYLE[style % NSTYLES];
    uint32_t step_len = (uint32_t)((float)rate * 60.0f / (y->bpm * 4.0f));
    uint32_t len = step_len * 16u, i, s, seed = 0xA3E1u + style * 7919u;
    for (i = 0; i < len; i++)
        pcm[i] = 0;
    for (s = 0; s < 16u; s++) {
        char d = y->drums[s], c = y->cym[s];
        uint32_t at = s * step_len;
        if (c == 'H' || c == 'h')
            add_hat(pcm, len, at, c == 'H' ? 1.0f : 0.6f, y->hat_decay, &seed);
        else if (c == 'O')
            add_hat(pcm, len, at, 1.0f, 0.99975f, &seed);
        else if (c == 'X')
            add_hat(pcm, len, at, 1.6f, 0.99990f, &seed);
        if (d == 'K' || d == 'B')
            add_kick(pcm, len, at, rate, 1.0f, y->kick_hz);
        else if (d == 'k')
            add_kick(pcm, len, at, rate, 0.55f, y->kick_hz);
        if (d == 'S')
            add_snare(pcm, len, at, rate, 1.0f, y->snare_hz, &seed);
        else if (d == 's')
            add_snare(pcm, len, at, rate, 0.35f, y->snare_hz, &seed);
        else if (d == 'T')
            add_tom(pcm, len, at, rate, 1.0f);
        else if (d == 'R' || d == 'B')
            add_rim(pcm, len, at, rate, 1.0f, &seed);
        else if (d == 'C')
            add_clap(pcm, len, at, rate, 1.0f, &seed);
    }
    b->pcm = pcm;
    b->len = len;
    b->rate = rate;
    b->bpm_x100 = (uint32_t)(y->bpm * 100.0f + 0.5f);
    b->bars = 1u;
    b->nslices = 16u;
    for (i = 0; i <= b->nslices; i++)
        b->slice[i] = len / b->nslices * i + (len % b->nslices) * i / b->nslices;   /* 32-bit only */
    for (i = 0; i < 12u && y->name[i]; i++)
        b->name[i] = y->name[i];
    b->name[i] = 0;
}

/* the first style (the tests' and the old interface) */
uint32_t brk_synth_len(uint32_t rate, float bpm, uint32_t bars)
{
    return (uint32_t)((float)rate * 60.0f / (bpm * 4.0f)) * 16u * bars;
}

void brk_synth_break(int16_t *pcm, uint32_t rate, float bpm, uint32_t bars, brk_break_t *b)
{
    (void)bpm;
    (void)bars;
    brk_synth_style(pcm, rate, 0u, b);
}
