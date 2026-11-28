/* The render's cost, a lane at a time: what a voice costs the audio interrupt (the FM-1's budget is 5.8 ms a
   block; the block is 256 samples). Run it on the host and compare against a release:
 *
 *   cc -O2 -o /tmp/bench /tmp/bench.c app/dsp/brk.c app/dsp/adpcm.c app/dsp/brk_math.c tests/engine/brk_synth.c \
 *      -I app/dsp -I tests/engine -lm && /tmp/bench
 *
 * The absolute number is the host's; what matters is the ratio (a lane's voice against the rest) and how it
 * moved with the last change. The FM-1's own clock still has to say the last word.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "brk.h"
#include "brk_synth.h"
#ifndef MC_LANE_VOICES
#define MC_LANE_VOICES 2u                /* brk.c's default */
#endif

static int16_t pcm[200000];
static brk_break_t brk;
static mc_scene_t sc[MC_SCENES];
static const uint8_t gruv[12] = { 1u, 12u, 50u, 0u, 0u, 0u, 1u, 0u, 0u, 1u, 0u, 0u };
static uint8_t evnt[MC_EVNT_BYTES * 4];

int main(void)
{
    const uint32_t N = 44100u * 30u;               /* 30 s of audio */
    static float out[256];
    clock_t t0, t1;
    uint32_t i, l, lanes, lane_voices;
    brk_init();
    brk_synth_break(pcm, 22050, 136.9f, 1u, &brk);
    brk.split = 0u;
    brk_set_break(&brk);
    brk_tempo(136.9f);
    brk_swing(0.5f);
    for (l = 0; l < MC_LANES; l++) {
        memset(&sc[0].lane[l], 0, sizeof sc[0].lane[l]);
        sc[0].lane[l].flags = MCL_ON;
        sc[0].lane[l].role = (uint8_t)l;
        sc[0].lane[l].hits = 0xFFu;
        sc[0].lane[l].len = 8u;
        sc[0].lane[l].rate = MCR_8;
        sc[0].lane[l].level = 100u;
        sc[0].lane[l].gate = 100u;
        sc[0].lane[l].prob = 100u;
        sc[0].lane[l].ratchet = 1u;
    }
    sc[0].bars = 1u;
    sc[0].flags = 0u;                        /* the lanes alone: the break's own voice is not under them */
    for (i = 0; i < 4u; i++) {
        uint8_t *e = evnt + i * MC_EVNT_BYTES;
        e[0] = (uint8_t)(i << 6);
        e[1] = 110u;
        e[2] = 0x78u; e[3] = 0xECu;                 /* -5000 us: early */
        e[4] = 0;
        e[5] = (uint8_t)(i * 3u);
        e[6] = (uint8_t)(i * 50u);
        e[7] = 100u;
        e[8] = (uint8_t)(i * 2u);
        e[9] = 0;
    }
    brk_mc_scenes(sc);
    brk_mc_groove(gruv, sizeof gruv, evnt, sizeof evnt);
    brk_mc_start(0);
    brk_mc_next(0);
    brk_play(1);
    printf("%u s of audio each, the best of three runs:\n", N / 44100u);
    {
        double best[MC_LANES + 1u];
        uint32_t k, run;
        for (k = 0; k <= MC_LANES; k++) best[k] = 1e30;
        for (l = 0; l < MC_LANES; l++) sc[0].lane[l].flags = 0u;
        brk_play(0);
        for (i = 0; i < 20000u; i++) brk_render(out, 256u);  /* warm the clock up */
        for (lanes = 0; lanes <= MC_LANES; lanes += 2u) {
            for (l = 0; l < MC_LANES; l++) sc[0].lane[l].flags = (l < lanes) ? MCL_ON : 0u;
            lane_voices = lanes * MC_LANE_VOICES;
            for (run = 0; run < 3u; run++) {
                double us;
                t0 = clock();
                for (i = 0; i < N; i += 256u) brk_render(out, 256u);
                t1 = clock();
                us = 1e6 * (double)(t1 - t0) / CLOCKS_PER_SEC / (double)(N / 256u);
                if (us < best[lanes]) best[lanes] = us;
            }
            printf("  %u lane(s), %u voice(s): %8.2f us a block (256 samples)%s\n", lanes, lane_voices, best[lanes],
                   lanes ? "" : "   (the empty scene: the ticks alone)");
            if (lanes) printf("      the lanes add %+.2f us a block (%+.1f%%)\n", best[lanes] - best[0],
                              100.0 * (best[lanes] - best[0]) / best[0]);
        }
    }
    return 0;
}
