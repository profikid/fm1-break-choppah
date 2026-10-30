/* SPDX-License-Identifier: MPL-2.0 */
/* IMA ADPCM decoder (adpcm.h), from the IMA/DVI ADPCM recommendation (1992). */
#include "adpcm.h"

static const int8_t INDEX_ADJ[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};

static inline int16_t nibble(adpcm_state_t *st, uint32_t code)
{
    int32_t step = STEP[st->index], diff = step >> 3, p = st->pred, idx = (int32_t)st->index + INDEX_ADJ[code];
    if (code & 4u)
        diff += step;
    if (code & 2u)
        diff += step >> 1;
    if (code & 1u)
        diff += step >> 2;
    p += (code & 8u) ? -diff : diff;
    st->pred = (int16_t)(p > 32767 ? 32767 : p < -32768 ? -32768 : p);
    st->index = (uint8_t)(idx < 0 ? 0 : idx > 88 ? 88 : idx);
    return st->pred;
}

void adpcm_decode(adpcm_state_t *st, const uint8_t *src, int16_t *dst, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] = nibble(st, (i & 1u) ? (uint32_t)(src[i >> 1] >> 4) : (uint32_t)(src[i >> 1] & 15u));
}
