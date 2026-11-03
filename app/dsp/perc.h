/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's snare and hi-hat (PRD K-10): two synthesised voices beside the kick (kick.h), each with six settings
 * (0..1), presets, and a lane of the pattern's 16 steps (the app's).
 *   SNARE  a body of two detuned sines dropping onto TUNE, and noise band-passed by TONE; SNAP is how much noise and
 *          crack against the body; DECAY both their tails; DRIVE a soft clip
 *   HAT    six square oscillators at an 808's ratios (TUNE) against noise (TONE), high-passed at CUT; DECAY from a
 *          tight closed hat to an open one; DRIVE
 * Platform-free, like the break engine: settings from the main loop (perc_set, read once a block), hits at sample
 * offsets in the render. */
#pragma once
#include <stdint.h>

enum { PV_SNARE, PV_HAT, PERC_VOICES };
extern const char *const PERC_VOICE_NAME[PERC_VOICES];
typedef struct {
    float level, tune, decay, tone, snap, drive;   /* 0..1; snap is the hat's CUT */
} perc_voice_t;                          /* 24 bytes */
typedef struct {
    perc_voice_t v[PERC_VOICES];
    float volume;                        /* 0..1: the MIXER's PERC fader */
} perc_t;                                /* 52 bytes */

#define PERC_PRESETS 4u
extern const char *const PERC_PRESET_NAME[PERC_VOICES][PERC_PRESETS];
void perc_preset(perc_voice_t *v, uint32_t voice, uint32_t i);
void perc_defaults(perc_t *p);           /* each voice's first preset, volume 0.8 */
float perc_tune_hz(uint32_t voice, float tune);   /* SNARE 120..400 Hz; HAT the lowest of its six, 100..800 Hz */
float perc_decay_ms(uint32_t voice, float decay); /* to -40 dB: SNARE 60..700 ms, HAT 15..900 ms */

void perc_init(void);
void perc_set(const perc_t *p);          /* main loop: read at the next render */
void perc_hit(uint32_t voice);           /* main loop (a MIDI note): at the next render */
/* render: n samples into out (overwritten); hits at the sample offsets at[0..nat-1] (rising), who[k] the voices
 * (bit PV_*) hit there */
void perc_render(float *out, uint32_t n, const uint16_t *at, const uint8_t *who, uint32_t nat);
/* main loop: one hit of the voice as the screen draws it, offline: px[i] the peak (0..height) of the ith of n columns
 * over its first ms milliseconds, at its level (not the volume) */
void perc_shape(uint32_t voice, const perc_voice_t *v, uint8_t *px, uint32_t n, uint32_t ms, uint32_t height);
