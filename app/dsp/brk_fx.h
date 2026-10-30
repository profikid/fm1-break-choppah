/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's effects (PRD 4.5): ring modulator -> stereo hi-pass -> flanger -> degrader, then the master DJ filter
 * and a limiter. Mono in (the break), stereo out. Parameters are set from the main loop, read once a block. */
#pragma once
#include <stdint.h>

enum { FX_RING, FX_HPF, FX_FLANGER, FX_DEGRADE, BRK_NFX };

typedef struct {
    uint8_t on[BRK_NFX];
    float ring_hz, ring_mix;             /* 20..2000 Hz, 0..1 */
    float hpf_hz, hpf_res, hpf_spread;   /* 20..8000 Hz, 0..0.95, 0..1 (R's cutoff above L's) */
    float fl_rate, fl_depth, fl_fb, fl_mix;   /* Hz, 0..1, -0.9..0.9, 0..1 */
    float deg_bits, deg_rate, deg_drive; /* 2..16 bits, 1000..44100 Hz, 0..1 */
    float dj;                            /* master DJ filter: -1 low-pass .. 0 off .. +1 high-pass */
} brk_fx_t;

void brk_fx_init(void);
void brk_fx_defaults(brk_fx_t *p);
void brk_fx_set(const brk_fx_t *p);
/* pads: brk_status_t.pads (PAD_FILTER throws the DJ filter to low-pass) */
void brk_fx_process(const float *in, float *l, float *r, uint32_t n, uint32_t pads);
