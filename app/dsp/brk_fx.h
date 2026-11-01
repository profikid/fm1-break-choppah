/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's effects (PRD 4.5): the master chain, four slots in series, each any effect of the library with three
 * settings, a mix and a bypass (the order is the slots'); then the soundboard joins, the DUB pad's delay, the master
 * DJ filter (and the FILTER pad's throw) and a limiter. Mono in (the break), stereo out. Parameters are set from the
 * main loop, read once a block. */
#pragma once
#include <stdint.h>

#define BRK_FX_SLOTS 4u
/* the library. Each effect's three settings are 0..1 (p[0..2]; brk_fx.c maps them), then its mix:
 *   FILTER   cutoff (30 Hz..16 kHz), resonance, mode (low-pass / band-pass / high-pass)
 *   CRUNCH   bits (16..2), rate (44.1 kHz..1 kHz), drive
 *   DRIVE    gain (1..30x), tone (dark..bright), level
 *   PUNCH    amount (a drum-bus compressor), attack (0.1..30 ms), release (20..500 ms)
 *   FLANGER  rate (0.02..10 Hz), depth, feedback (-0.9..+0.9)
 *   RING     frequency (20..2000 Hz), spread (the right side higher), -
 *   TREMOLO  rate (1/2 1/4 1/8 1/16 1/32 1/8T, on the tempo), depth, pan (0 tremolo .. 1 auto-pan)
 *   REVERB   size (decay 0.25..0.97), tone (bright .. dark), pre-delay (0..60 ms); its mix is the reverb's level over
 *            the dry sound. A Dattorro plate (stereo, modulated) at 22.05 kHz inside, 16-bit lines: there is one, so
 *            a second REVERB slot passes the sound on */
enum { FXT_EMPTY, FXT_FILTER, FXT_CRUNCH, FXT_DRIVE, FXT_PUNCH, FXT_FLANGER, FXT_RING, FXT_TREMOLO, FXT_REVERB,
       BRK_NFXT };
extern const char *const BRK_FXT_NAME[BRK_NFXT];
#define BRK_TREM_DIVS 6u
extern const char *const BRK_TREM_DIV_NAME[BRK_TREM_DIVS];

typedef struct {
    uint8_t type;                        /* FXT_* */
    uint8_t on;                          /* 0: bypassed */
    uint8_t rsv[2];
    float p[3];                          /* its settings, 0..1 */
    float mix;                           /* 0..1 */
} brk_fxslot_t;

typedef struct {
    brk_fxslot_t slot[BRK_FX_SLOTS];     /* the chain, in order */
    float wet;                           /* the whole chain against the dry break, 0..1 (PERFORM's FX knob) */
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
void brk_fx_slot_defaults(brk_fxslot_t *s, int type);   /* an effect put into a slot: its own starting settings */
void brk_fx_set(const brk_fx_t *p);
/* the dub delay's line: 16-bit mono in the caller's RAM (the app's pool), and the tempo it follows */
void brk_fx_delay_buffer(int16_t *buf, uint32_t n);
void brk_fx_tempo(float bpm);
float brk_fx_dj_now(void);               /* where the DJ filter is right now, -1..1 (for the screen) */
/* in: the break (through the chain); aux: the soundboard (0 for none), joined after the chain, into the DJ filter
 * and the limiter, and sent into the dub delay at aux_send. pads: brk_status_t.pads */
void brk_fx_process(const float *in, const float *aux, float aux_send, float *l, float *r, uint32_t n,
                    uint32_t pads);
