/* SPDX-License-Identifier: MPL-2.0 */
/* IMA ADPCM (4 bits a sample): how the break bank keeps breaks in flash (PRD 8.2, 8.3). Low nibble first.
 * tools/prep_breaks.py has the matching encoder. */
#pragma once
#include <stdint.h>

typedef struct {
    int16_t pred;
    uint8_t index;                       /* 0..88 */
    uint8_t rsv;
} adpcm_state_t;

/* decodes n samples from src (n / 2 bytes, rounded up) into dst, carrying the state */
void adpcm_decode(adpcm_state_t *st, const uint8_t *src, int16_t *dst, uint32_t n);
