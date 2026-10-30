/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
#include "brk.h"

/* the built-in breaks: one bar of a style, played by a small drum synth into RAM (16 slices) */
uint32_t brk_synth_count(void);
const char *brk_synth_name(uint32_t style);
uint32_t brk_synth_style_len(uint32_t style, uint32_t rate);   /* samples, for a buffer's size */
void brk_synth_style(int16_t *pcm, uint32_t rate, uint32_t style, brk_break_t *b);

/* the first style, by the old interface (the tests) */
uint32_t brk_synth_len(uint32_t rate, float bpm, uint32_t bars);
void brk_synth_break(int16_t *pcm, uint32_t rate, float bpm, uint32_t bars, brk_break_t *b);
