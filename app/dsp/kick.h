/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's kick (PRD K-*): a synthesised kick of four layers for gabber, hardcore and techno over the break.
 *   SUB    the low foundation: a sine dropping to its pitch, a long tail
 *   PUNCH  the hit and the body: a sine swept fast from far above (909 style)
 *   GRIT   the dirty mids: FM through a drive, noise mixed in by its tone
 *   CLICK  the transient: a short burst of noise and a click
 * Each layer has a level, pitch, decay and tone, and an effect of its own (drive, crunch, filter, ring mod) with a
 * wet mix; the layers sum into INTENSITY (a drive into a clipper: the distorted gabber kick). Platform-free like the
 * break engine. Settings from the main loop (kick_set, read once a block); triggers at sample offsets in the render. */
#pragma once
#include <stdint.h>

enum { KL_SUB, KL_PUNCH, KL_GRIT, KL_CLICK, KICK_LAYERS };
enum { KFX_OFF, KFX_DRIVE, KFX_CRUNCH, KFX_FILTER, KFX_RING, KICK_FX };
extern const char *const KICK_LAYER_NAME[KICK_LAYERS];
extern const char *const KICK_FX_NAME[KICK_FX];

typedef struct {
    float level;                         /* 0..1 */
    float pitch;                         /* 0..1: SUB 30..90 Hz, PUNCH 50..200 Hz, GRIT 40..400 Hz, CLICK 1..12 kHz */
    float decay;                         /* 0..1: to -40 dB in SUB 60..1500 ms, PUNCH 20..400, GRIT 20..800, CLICK
                                          * 1..40 */
    float tone;                          /* 0..1: SUB its saturation, PUNCH its sweep, GRIT FM depth and noise,
                                          * CLICK noise or click */
    uint8_t fx;                          /* KFX_* */
    uint8_t rsv[3];
    float fa, fb, wet;                   /* the effect's two settings and its mix, 0..1 */
} kick_layer_t;                          /* 32 bytes */

typedef struct {
    kick_layer_t l[KICK_LAYERS];
    float intensity;                     /* 0..1: the sum's drive into the clipper (0 clean) */
    float volume;                        /* 0..1 */
    uint16_t steps;                      /* the kick's 16 steps (bit n: step n of each bar) */
    uint8_t chain;                       /* 1: through the FX chain with the break; 0: around it */
    uint8_t rsv;
} kick_t;                                /* 140 bytes */

#define KICK_PRESETS 6u
extern const char *const KICK_PRESET_NAME[KICK_PRESETS];
void kick_preset(kick_t *k, uint32_t i);   /* the sound of a built-in kick (steps and chain kept) */
void kick_defaults(kick_t *k);           /* ROTTERDAM, four to the floor, through the chain */
float kick_pitch_hz(uint32_t layer, float pitch);   /* a layer's pitch knob (0..1) in Hz, for the screen */
float kick_decay_ms(uint32_t layer, float decay);   /* its decay knob in ms (to -40 dB) */
/* the effects' settings as the screen shows them: FILTER's cutoff and RING's frequency in Hz, CRUNCH's bits */
float kick_fx_hz(uint32_t fx, float a);
uint32_t kick_fx_bits(float a);

void kick_init(void);
void kick_set(const kick_t *k);          /* main loop: read at the next render */
void kick_hit(void);                     /* main loop (a MIDI note): a kick at the next render */
/* render: n samples into out (overwritten); hits at the sample offsets at[0..nat-1] (rising) */
void kick_render(float *out, uint32_t n, const uint16_t *at, uint32_t nat);
/* the kick's shape, for the screen: n peaks (0..1) of the first ms milliseconds, rendered offline (main loop) */
void kick_shape(const kick_t *k, uint8_t *px, uint32_t n, uint32_t ms, uint32_t height);
