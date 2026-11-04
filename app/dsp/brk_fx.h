/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's effects (PRD 4.5): the break's DJ filter (and the FILTER pad's throw), then the master chain, four slots
 * in series, each any effect of the library with three settings, a mix and a bypass (the order is the slots'); then
 * the drums around it join, the DUB pad's delay and a limiter. Mono in (the break), stereo out. Parameters are set from
 * the main loop, read once a block. */
#pragma once
#include <stdint.h>
#include "brk_math.h"

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
 *            a second REVERB slot passes the sound on
 *   DELAY    time on the tempo (1/16 1/8 3/16 1/4 3/8 1/2 1/8T 1/4T), feedback, tone (dark .. bright repeats); its mix
 *            is the SEND into it (the echoes come back at full): a stereo ping-pong at 22.05 kHz on the DUB pad's
 *            line (0.74 s at most). One: it is also where the DUB pad and the slices' ECHO throw; without a DELAY slot the delay sits after the chain with the DUB pad's settings
 *   EQ       low (a shelf at 120 Hz), mid (a bell), high (a shelf at 8 kHz): -12..+12 dB each, 0.5 flat; its mix is
 *            the mid's frequency (200 Hz..5 kHz)
 *   TAPE     drive (0..24 dB into a tape curve, even harmonics, made up), warmth (the head bump and the top rolled
 *            off), wow (and flutter: the pitch wanders)
 *   MB COMP  a 3-band (multiband) compressor: low, mid, high (split at 200 Hz and 2.5 kHz by default), each band's
 *            amount (a macro: its threshold and its ratio from untouched to brk_mbc_t's, by default -30 dB and 8:1,
 *            made up by half its reduction at full scale); per band its own attack, release and gain, the splits, a
 *            knee, an output and a solo in brk_mbc_t (brk_fx_set_mbc); the bands sum back to the input when nothing
 *            is pressed. Its mix is the parallel mix (the pressed sound against the dry). brk_fx_mb_meter: its
 *            reduction and level per band
 *   CLIP     drive (0..+24 dB into it), knee (soft .. hard), ceiling (0..-12 dB): a clipper, the level held at the
 *            ceiling */
enum { FXT_EMPTY, FXT_FILTER, FXT_CRUNCH, FXT_DRIVE, FXT_PUNCH, FXT_FLANGER, FXT_RING, FXT_TREMOLO, FXT_REVERB,
       FXT_DELAY, FXT_EQ, FXT_TAPE, FXT_MBCOMP, FXT_CLIP, BRK_NFXT };
#define BRK_DLY_DIVS 8u
extern const char *const BRK_DLY_DIV_NAME[BRK_DLY_DIVS];
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

/* MB COMP's detail (PRD A-49a), one per slot (kept apart from brk_fxslot_t: the settings only grow at the end).
 * The slot's p[0..2] stay each band's AMOUNT, a macro scaling the band's threshold and ratio from untouched (0) to as
 * set here (1); its mix stays the parallel mix. Everything 0..1 as the slots' settings, mapped by brk_mb_*() below */
enum { MB_LOW, MB_MID, MB_HIGH, MB_BANDS };
typedef struct {
    float thr[MB_BANDS];                 /* threshold at full amount: -48 .. 0 dB */
    float ratio[MB_BANDS];               /* at full amount: BRK_MB_RATIO's steps, 1:1 .. 20:1, LIMIT */
    float att[MB_BANDS];                 /* attack 0.1 .. 100 ms */
    float rel[MB_BANDS];                 /* release 10 .. 1000 ms */
    float gain[MB_BANDS];                /* the band's level after it: -12 .. +12 dB (0.5: none) */
    float xlo, xhi;                      /* the splits: low / mid 40 .. 800 Hz, mid / high 1 .. 12 kHz */
    float knee;                          /* hard .. 12 dB soft */
    float out;                           /* the output: -12 .. +12 dB (0.5: none) */
    uint8_t solo;                        /* 0 every band, 1 + MB_*: that band alone (no dry) */
    uint8_t rsv[3];
} brk_mbc_t;
#define BRK_MB_RATIOS 15u
extern const float BRK_MB_RATIO[BRK_MB_RATIOS];   /* the last is LIMIT (1000:1) */
static inline float brk_mb_thr_db(float v) { return 48.0f * v - 48.0f; }
static inline float brk_mb_ratio(float v)
{
    uint32_t i = (uint32_t)((v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v) * (float)(BRK_MB_RATIOS - 1u) + 0.5f);
    return BRK_MB_RATIO[i];
}
static inline float brk_mb_att_ms(float v) { return 0.1f * brk_exp2f(v * 9.9658f); }   /* 0.1 .. 100 */
static inline float brk_mb_rel_ms(float v) { return 10.0f * brk_exp2f(v * 6.6439f); }  /* 10 .. 1000 */
static inline float brk_mb_db24(float v) { return (v - 0.5f) * 24.0f; }                /* gain, out: -12 .. +12 */
static inline float brk_mb_xlo_hz(float v) { return 40.0f * brk_exp2f(v * 4.3219f); }  /* 40 .. 800 */
static inline float brk_mb_xhi_hz(float v) { return 1000.0f * brk_exp2f(v * 3.585f); } /* 1k .. 12k */
static inline float brk_mb_knee_db(float v) { return 12.0f * v; }
/* the reduction for a level `over` its threshold (any unit, dB or octaves), slope 1 - 1/ratio, knee width w (the same
 * unit; 0 hard): none below the knee, a quadratic through it, then over * slope */
static inline float brk_mb_reduce(float over, float slope, float w)
{
    float t;
    if (2.0f * over <= -w)
        return 0.0f;
    if (2.0f * over >= w)
        return over * slope;
    t = over + 0.5f * w;
    return slope * t * t / (2.0f * w);
}
void brk_mbc_defaults(brk_mbc_t *m);
void brk_fx_set_mbc(uint32_t slot, const brk_mbc_t *m);   /* from the main loop, as brk_fx_set */
/* slot's MB COMP now, per band (low, mid, high): its gain (1 = no reduction) and its level in (the band's peak
 * followed, 1 = full scale); 1s and 0s when that slot is not an MB COMP that is on */
void brk_fx_mb_meter(uint32_t slot, float *gr, float *lvl);

void brk_fx_init(void);
void brk_fx_defaults(brk_fx_t *p);
void brk_fx_slot_defaults(brk_fxslot_t *s, int type);   /* an effect put into a slot: its own starting settings */
void brk_fx_set(const brk_fx_t *p);
/* the dub delay's line: 16-bit mono in the caller's RAM (the app's pool), and the tempo it follows */
void brk_fx_delay_buffer(int16_t *buf, uint32_t n);
void brk_fx_tempo(float bpm);
float brk_fx_dj_now(void);               /* where the DJ filter is right now, -1..1 (for the screen) */
/* the first MB COMP slot's gain now, per band (low, mid, high; 1 = no reduction), for the screen; 1s for none */
void brk_fx_gr(float *gr);
/* in: the break, through the DJ filter, then the chain; thru: joins it after the DJ filter, into the chain (the drums
 * through the chain; 0 for none); echo, verb: the slices' throws into the delay and the reverb (brk_sends, the DJ
 * filter on them too; 0 for none); post: joined after the chain, into the limiter, thrown nowhere (the drums around
 * the chain; 0 for none). pads: brk_status_t.pads */
void brk_fx_process(const float *in, const float *thru, const float *echo, const float *verb, const float *post,
                    float *l, float *r, uint32_t n, uint32_t pads);
