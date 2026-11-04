/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: small single-precision helpers. No libm and no double: the FM-1 build rejects double-precision code,
 * and the same maths must give the same samples on the device, the host simulator and in the browser. */
#pragma once
#include <stdint.h>

/* block-rate code (settings mapped, defaults, offline shapes): built for size, the per-sample code at -O2 */
#if defined(__clang__)
#define BRK_COLD __attribute__((noinline, minsize))
#else
#define BRK_COLD __attribute__((noinline, cold))
#endif
/* the per-sample code of a unit that defines BRK_ENGINE first (brk_fx.c): run from RAM on the FM-1 (BRK_RAM: section
 * .ram_text, out of the flash cache's way, stored packed), so everything it uses is inlined into it (BRK_INL): RAM
 * code may call nothing (the build checks). Elsewhere (the UI, the other units) plain inline */
#if defined(__PI32V2__)
#define BRK_RAM __attribute__((noinline, section(".ram_text")))
#else
#define BRK_RAM __attribute__((noinline))
#endif
#ifdef BRK_ENGINE
#define BRK_INL static inline __attribute__((always_inline))
#else
#define BRK_INL static inline
#endif

BRK_INL float brk_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
BRK_INL float brk_absf(float v) { return v < 0.0f ? -v : v; }
BRK_INL float brk_soft(float x) { return x / (1.0f + brk_absf(x)); }   /* a soft clip, never past 1 */

/* sin(2*pi*t) for t in turns (any value): a 5th-order odd polynomial on a quarter wave, error < 2e-4 */
BRK_INL float brk_sin_turns(float t)
{
    float x, x2;
    t -= (float)(int32_t)t;              /* -1 < t < 1 */
    if (t < 0.0f)
        t += 1.0f;                       /* 0 <= t < 1 */
    if (t > 0.75f)
        t -= 1.0f;                       /* -0.25 .. 0.75 */
    else if (t > 0.25f)
        t = 0.5f - t;                    /* reflect into -0.25 .. 0.25 */
    x = t * 6.28318531f;
    x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.00833333f - x2 * 0.00019841f)));
}

/* 2^x, |x| < 30: the integer part through the exponent bits, the fraction by a cubic (error < 1e-4) */
BRK_INL float brk_exp2f(float x)
{
    union { float f; uint32_t u; } r;
    int32_t i = (int32_t)x;
    float f;
    if (x < (float)i)
        i--;
    f = x - (float)i;                    /* 0 <= f < 1 */
    r.f = 1.0f + f * (0.69606564f + f * (0.22449433f + f * 0.07944023f));
    r.u += (uint32_t)i << 23;
    return r.f;
}

/* log2(x), x > 0: the exponent bits, the mantissa by a cubic (error < 0.002) */
BRK_INL float brk_log2f(float x)
{
    union { float f; uint32_t u; } r;
    float e, t;
    r.f = x;
    e = (float)(int32_t)((r.u >> 23) & 0xFFu) - 127.0f;
    r.u = (r.u & 0x007FFFFFu) | 0x3F800000u;   /* 1 <= 1 + t < 2 */
    t = r.f - 1.0f;
    return e + t * (1.4234755f + t * (-0.5876945f + t * 0.1642190f));
}

/* tan(pi * f / sr) for a filter's prewarp, f below sr / 2 */
BRK_INL float brk_tan_pi(float f_over_sr)
{
    float t = brk_clampf(f_over_sr, 0.0f, 0.49f) * 0.5f;   /* turns of pi*x: sin and cos of (x / 2) turns */
    float c = brk_sin_turns(t + 0.25f);
    return brk_sin_turns(t) / (c < 1e-4f ? 1e-4f : c);
}

/* xorshift32: the generator's dice (deterministic per seed) */
BRK_INL uint32_t brk_rand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
/* 0 .. 1 */
BRK_INL float brk_randf(uint32_t *s) { return (float)(brk_rand(s) >> 8) * (1.0f / 16777216.0f); }
