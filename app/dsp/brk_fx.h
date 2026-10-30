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
    /* the FILTER pad's throw */
    float flt_target, flt_res, flt_speed;    /* -1 low-pass .. +1 high-pass, 0..0.95, 0..1 */
    /* the DUB pad's tempo-synced delay */
    uint8_t dub_div;                     /* 0..BRK_DUB_DIVS-1: 1/8 3/16 1/4 3/8 */
    uint8_t rsv[3];
    float dub_fb, dub_tone, dub_mix;     /* 0..0.95, 0..1 (dark .. bright), 0..1 */
} brk_fx_t;
#define BRK_DUB_DIVS 4u
extern const char *const BRK_DUB_DIV_NAME[BRK_DUB_DIVS];

void brk_fx_init(void);
void brk_fx_defaults(brk_fx_t *p);
void brk_fx_set(const brk_fx_t *p);
/* the dub delay's line: 16-bit mono in the caller's RAM (the app's pool), and the tempo it follows */
void brk_fx_delay_buffer(int16_t *buf, uint32_t n);
void brk_fx_tempo(float bpm);
float brk_fx_dj_now(void);               /* where the DJ filter is right now, -1..1 (for the screen) */
/* pads: brk_status_t.pads (PAD_FILTER throws the DJ filter to low-pass) */
void brk_fx_process(const float *in, float *l, float *r, uint32_t n, uint32_t pads);
