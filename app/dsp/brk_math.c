/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: one copy of the maths for the block-rate code of every unit (settings mapped, envelopes' rates, filters'
 * coefficients): brk_math.h's inline versions stay for the per-sample code. */
#include "brk_math.h"

BRK_COLD float brk_exp2f_c(float x) { return brk_exp2f(x); }
BRK_COLD float brk_log2f_c(float x) { return brk_log2f(x); }
BRK_COLD float brk_tan_pi_c(float f_over_sr) { return brk_tan_pi(f_over_sr); }
