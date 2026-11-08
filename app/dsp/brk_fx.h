/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's effects (PRD 4.5): the break's DJ filter (and the FILTER pad's throw), then the master chain, four slots
 * in series, each any effect of the library with three settings, a mix and a bypass (the order is the slots'); then
 * the drums around it join, the DUB pad's delay and a limiter. Mono in (the break), stereo out. Parameters are set from
 * the main loop, read once a block. */
#pragma once
#include <stdint.h>
#include "brk_math.h"

#define BRK_FX_SLOTS 4u
/* the ARP page's black keys (PRD B-55): each its own effect of the library (not MB COMP), on while held or latched,
 * over the whole mix after the chain (the drums around it too), before the limiter. BRK_PAD_V voices sound at once:
 * the main loop gives each pad sounding one (brk_fx_set_pad); a voice let go fades out in ~5 ms, REVERB's and DELAY's
 * send instead, so their tails ring on (brk_fx_pad_busy: until they are quiet). They are the engine's slots
 * BRK_FX_SLOTS .. BRK_FX_ALL - 1 for brk_fx_set_fxx, brk_fx_meter and brk_fx_watch; the overload guard sheds them
 * first. One REVERB and one DELAY in all: a pad's passes the sound on when the chain has its own */
#define BRK_PAD_V 4u
#define BRK_FX_ALL (BRK_FX_SLOTS + BRK_PAD_V)
#define BRK_PAD_ST_BYTES 2400u           /* a voice's state: brk_fx_pad_buffer wants BRK_PAD_V of these */
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
BRK_INL float brk_mb_reduce(float over, float slope, float w)
{
    float t;
    if (2.0f * over <= -w)
        return 0.0f;
    if (2.0f * over >= w)
        return over * slope;
    t = over + 0.5f * w;
    return slope * t * t / (2.0f * w);
}
/* Every slot's extra knobs (PRD A-49b), one per slot as brk_mbc_t (the settings only grow at the end). x[0..3]: its
 * effect's MORE page (0..1 each; unused ones stay put), the defaults what the effect always did:
 *   FILTER   drive (1..8x into it), slope (12 / 24 dB), spread (the right's cutoff up to 2 octaves higher)
 *   CRUNCH   tone (a low-pass after it, 1 kHz .. off), dither (noise under the steps)
 *   DRIVE    shape (SOFT TUBE HARD FOLD), bias (even harmonics), low cut (20 Hz .. 800 Hz before it)
 *   PUNCH    ratio (BRK_PUNCH_RATIO), make-up (+0..+12 dB), side-chain high-pass (off .. 300 Hz: the lows don't pump)
 *   FLANGER  delay (0.2..2.7 ms the sweep starts from), spread (the right's phase 0..180 degrees), sync (off, or the
 *            sweep on the tempo: BRK_FLSYNC_NAME)
 *   RING MOD wave (SINE TRI SQUARE), mode (RING / AM)
 *   TREMOLO  shape (SINE TRI SQUARE SAW), smooth (the edges rounded)
 *   REVERB   width (mono .. wide), low cut (off .. 600 Hz into it), mod (the tank's wobble), diffusion
 *   DELAY    width (mono .. full ping-pong), low cut (off .. 1 kHz in its loop), duck (the echoes under the playing)
 *   EQ       low's frequency (40..400 Hz), mid's Q (0.3..4), high's frequency (2..16 kHz), level (-12..+12 dB)
 *   TAPE     flutter, wow's rate (0.1..3 Hz), hiss
 *   CLIP     bias (lopsided: even harmonics), output (-12..0 dB)
 * x[4..7]: its MOD page, a modulator on one of its settings: RATE on the tempo (BRK_MOD_DIV_NAME), DEPTH (0.5 none;
 * below, the other way), SHAPE (BRK_MOD_SHAPE_NAME: ENV follows the slot's own input), TARGET (its K1, K2, K3, MIX) */
#define BRK_FXX 8u
enum { FXX_RATE = 4, FXX_DEPTH, FXX_SHAPE, FXX_TARGET };
typedef struct {
    float x[BRK_FXX];
    uint8_t type;                        /* the effect they are for: another effect's are not used (its defaults are) */
    uint8_t rsv[3];
} brk_fxx_t;
#define BRK_MOD_DIVS 9u
#define BRK_MOD_SHAPES 7u
enum { MOD_SINE, MOD_TRI, MOD_SAW, MOD_RAMP, MOD_SQUARE, MOD_SH, MOD_ENV };
extern const char *const BRK_MOD_DIV_NAME[BRK_MOD_DIVS];
extern const char *const BRK_MOD_SHAPE_NAME[BRK_MOD_SHAPES];
#define BRK_FLSYNCS 6u
extern const char *const BRK_FLSYNC_NAME[BRK_FLSYNCS];
#define BRK_PUNCH_RATIOS 8u
extern const float BRK_PUNCH_RATIO[BRK_PUNCH_RATIOS];
BRK_INL uint32_t brk_idx(float v, uint32_t n)   /* a 0..1 setting as one of n choices */
{
    return (uint32_t)((v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v) * (float)(n - 1u) + 0.5f);
}
/* a modulator's shape at phase ph (turns): -1..1 (S&H and ENV are the engine's: 0 here). Inline: the engine runs
 * it per sample (TREMOLO, RING MOD) */
BRK_INL float brk_mod_wave(uint32_t shape, float ph)
{
    ph -= (float)(int32_t)ph;
    if (ph < 0.0f)
        ph += 1.0f;
    switch (shape) {
    case MOD_SINE: return brk_sin_turns(ph);
    case MOD_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case MOD_SAW: return 1.0f - 2.0f * ph;
    case MOD_RAMP: return 2.0f * ph - 1.0f;
    case MOD_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    default: return 0.0f;
    }
}
void brk_fxx_defaults(brk_fxx_t *x, int type);
/* DRIVE's curves (shape 0..3): SOFT, TUBE (the negative side softer: lopsided), HARD, FOLD (past the top it folds
 * back down), with a bias (its offset taken back off); CLIP's: a cubic's soft curve (flat at 1.5) and a hard edge at
 * 1, blended by the knee (0 soft .. 1 hard), both starting at a slope of 1. Inline: per sample in the engine, and the
 * screen draws the same */
BRK_INL float brk_drive_curve(uint32_t shape, float u, float bias)
{
    float v, z;
    u += bias;
    switch (shape) {
    case 1u: v = u >= 0.0f ? brk_soft(u) : brk_soft(1.8f * u) * 0.5556f; z = brk_soft(bias); break;
    case 2u: v = brk_clampf(u, -0.8f, 0.8f); z = brk_clampf(bias, -0.8f, 0.8f); break;
    case 3u: v = 0.8f * brk_sin_turns(0.25f * u); z = 0.8f * brk_sin_turns(0.25f * bias); break;
    default: v = brk_soft(u); z = brk_soft(bias); break;
    }
    return v - z;
}
BRK_INL float brk_clip_curve(float u, float hard_k)
{
    float soft3, hard;
    u = brk_clampf(u, -1.5f, 1.5f);
    soft3 = u - 0.14814815f * u * u * u;
    hard = brk_clampf(u, -1.0f, 1.0f);
    return soft3 + (hard - soft3) * hard_k;
}
void brk_fx_set_fxx(uint32_t slot, const brk_fxx_t *x);
/* a slot now, for the screen: its level in and out (peaks, falling back), its modulator (-1..1; ENV 0..1), the phase
 * of its cycle, and its four settings as the modulator leaves them (0..1); zeros for a slot that is off */
typedef struct { float in, out, mod, ph, p[4]; } brk_fx_meter_t;
void brk_fx_meter(uint32_t slot, brk_fx_meter_t *m);
/* the slot whose level in and out the engine follows (brk_fx_meter's in, out; the others' stay 0): the one the
 * screen shows, BRK_FX_ALL for none. Following costs the render a little on every sample */
void brk_fx_watch(uint32_t slot);
/* the overload guard (PRD E-7): brk_fx_overload from the audio interrupt when a render ran over 90 % of its time;
 * the chain then bypasses its last slot on (another every ~0.2 s while it keeps happening), each back after 2 s
 * clear. brk_fx_shed: how many are shed now */
void brk_fx_overload(void);
uint32_t brk_fx_shed(void);

void brk_mbc_defaults(brk_mbc_t *m);
void brk_fx_set_mbc(uint32_t slot, const brk_mbc_t *m);   /* from the main loop, as brk_fx_set */
/* slot's MB COMP now, per band (low, mid, high): its gain (1 = no reduction) and its level in (the band's peak
 * followed, 1 = full scale); 1s and 0s when that slot is not an MB COMP that is on */
void brk_fx_mb_meter(uint32_t slot, float *gr, float *lvl);

void brk_fx_init(void);
void brk_fx_defaults(brk_fx_t *p);
void brk_fx_slot_defaults(brk_fxslot_t *s, int type);   /* an effect put into a slot: its own starting settings */
void brk_fx_set(const brk_fx_t *p);
/* the pads' voices' state: BRK_PAD_V * BRK_PAD_ST_BYTES, 4-aligned, in the caller's RAM (the pool); 0: no pads */
void brk_fx_pad_buffer(void *mem);
/* voice v (0..BRK_PAD_V-1): its effect (on is ignored; MB COMP passes), on (held or latched) or let go */
void brk_fx_set_pad(uint32_t v, const brk_fxslot_t *s, uint32_t on);
uint32_t brk_fx_pad_busy(void);          /* bit v: voice v still sounds (on, fading or its tail ringing) */
/* the dub delay's line: 16-bit mono in the caller's RAM (the app's pool), and the tempo it follows */
void brk_fx_delay_buffer(int16_t *buf, uint32_t n);
void brk_fx_tempo(float bpm);
float brk_fx_dj_now(void);               /* where the DJ filter is right now, -1..1 (for the screen) */
/* in: the break, through the DJ filter, then the chain; thru: joins it after the DJ filter, into the chain (the drums
 * through the chain; 0 for none); echo, verb: the slices' throws into the delay and the reverb (brk_sends, the DJ
 * filter on them too; 0 for none); post: joined after the chain, into the limiter, thrown nowhere (the drums around
 * the chain; 0 for none). pads: brk_status_t.pads */
void brk_fx_process(const float *in, const float *thru, const float *echo, const float *verb, const float *post,
                    float *l, float *r, uint32_t n, uint32_t pads);
