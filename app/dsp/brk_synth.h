/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
#include "brk.h"

/* the samples brk_synth_break writes, for a buffer's size */
uint32_t brk_synth_len(uint32_t rate, float bpm, uint32_t bars);
/* renders the built-in break into pcm (brk_synth_len samples) and describes it in *b: 16 slices a bar */
void brk_synth_break(int16_t *pcm, uint32_t rate, float bpm, uint32_t bars, brk_break_t *b);
