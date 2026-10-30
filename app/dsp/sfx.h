/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's soundboard: sixteen sound-system effects for reggae, dancehall and jungle (air horn, dub siren, laser,
 * gunshot, a "FYAH!" shout and more), synthesised live, four at a time. Platform-free like the break engine: the
 * main loop triggers, the audio interrupt renders into a buffer that goes through the effects (and the dub delay). */
#pragma once
#include <stdint.h>

enum { SFX_HORN, SFX_SIREN, SFX_WHOOP, SFX_LASER, SFX_ZAP, SFX_BOOM, SFX_BRAP, SFX_FYAH,
       SFX_HEY, SFX_RISER, SFX_REVCYM, SFX_SUBDROP, SFX_STAB, SFX_SKANK, SFX_SCRATCH, SFX_REWIND, SFX_N };
extern const char *const SFX_NAME[SFX_N];

typedef struct {
    float level;                         /* 0..1 */
    float pitch;                         /* -12..+12 semitones */
    float echo;                          /* 0..1: how much goes into the dub delay */
    float length;                        /* 0.5..2: the sounds' lengths */
} sfx_set_t;

void sfx_init(void);
void sfx_defaults(sfx_set_t *s);
void sfx_set(const sfx_set_t *s);
/* main loop: a key down (the siren and the riser go on while held) and up */
void sfx_trigger(int id);
void sfx_release(int id);
/* audio interrupt: n frames into out[] (overwritten), -1..1 */
void sfx_render(float *out, uint32_t n);
/* the screen: the sounds playing (bit id) and how far each has got, 0..1 */
uint32_t sfx_playing(void);
float sfx_progress(int id);
float sfx_echo(void);
