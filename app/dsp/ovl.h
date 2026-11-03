/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's overlay effects (ARP, PRD B-52): sixteen effects over the whole mix, one a white key, on while held (or
 * latched). Each fades in and out over ~5 ms, any can sound together; the order is fixed (tone, then dirt, then
 * filters, then movement, then level). Platform-free: the main loop sets which are on and their amounts (ovl_set,
 * words read each block); the render runs ovl_process on the stereo mix after the FX chain. */
#ifndef OVL_H
#define OVL_H
#include <stdint.h>

enum {
    OV_DIVE, OV_RISE, OV_RADIO, OV_WAH, OV_CRUSH, OV_LOFI, OV_DIST, OV_RING,
    OV_CHOP, OV_GATE, OV_DUCK, OV_PAN, OV_BOOM, OV_WOBBLE, OV_JET, OV_CUT, OVL_N
};
extern const char *const OVL_NAME[OVL_N];
extern const char *const OVL_RATE_NAME[4];   /* 1/4 1/8 1/16 1/32 */
extern const char *const OVL_SWEEP_NAME[4];  /* 1/4 BAR .. 2 BARS */

#define OVL_LINE 1024u                   /* WOBBLE's and JET's delay line: samples a side */

void ovl_init(int16_t *line);            /* the line: 2 * OVL_LINE (the pool); everything off */
/* main loop: on (bit OV_*), each one's amount (0..100), the rate of the ones on the beat (0..3: 1/4 .. 1/32: CHOP;
 * GATE twice as fast, WAH half, PAN a quarter), the sweeps' length (0..3: 1/4 bar .. 2 bars: DIVE, RISE) */
void ovl_set(uint32_t on, const uint8_t *amount, uint32_t rate, uint32_t sweep);
/* the render: l and r in place. beat: where the block starts, in beats (playing: the engine's; stopped: a clock of
 * its own); spb: samples a beat */
void ovl_process(float *l, float *r, uint32_t n, float beat, float spb);
uint32_t ovl_sounding(void);             /* bit OV_*: sounding now (fading out too) */

#endif
