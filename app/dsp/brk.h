/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's break engine: a loop cut into slices, played in time by a 16th-note sequencer, rearranged by a
 * generator and mangled by performance pads. No platform code: it builds with plain gcc (tests/engine) as well as
 * for the FM-1 (an -O2 unit, fm1.toml o2) and the browser.
 *
 * Threads: the brk_* setters are called from the main loop (the UI), brk_render from the audio interrupt. Key
 * presses go through a lock-free single-producer queue; parameters are single words written whole. */
#pragma once
#include <stdint.h>

#define BRK_SR 44100u                    /* the render's rate */
#define BRK_STEPS_PER_BAR 16u
#define BRK_MAX_SLICES 32u
#define BRK_MAX_STEPS 64u                /* a pattern: up to four bars of 16ths */
#define BRK_NO_SLICE (-1)

/* the black keys, left to right (PRD 5.1) */
enum { PAD_ROLL8, PAD_ROLL16, PAD_ROLL32, PAD_REVERSE, PAD_FREEZE, PAD_HALF, PAD_TAPESTOP, PAD_GATE, PAD_RANDOM,
       PAD_FILTER, PAD_DUB, BRK_NPADS };

/* a break in RAM: mono int16 PCM at its own rate, its tempo, and where its slices start (sample offsets, rising,
 * slice[nslices] = len) */
typedef struct {
    const int16_t *pcm;
    uint32_t len;
    uint32_t rate;                       /* Hz */
    uint32_t bpm_x100;                   /* the tempo it was played at, 13690 = 136.90 BPM */
    uint32_t bars;                       /* whole bars in the loop (1..4) */
    uint32_t nslices;                    /* 1..BRK_MAX_SLICES */
    uint32_t slice[BRK_MAX_SLICES + 1];
    char name[13];
} brk_break_t;

/* one step of a pattern (PRD B-21); slice BRK_NO_SLICE follows the loop */
typedef struct {
    int8_t slice;
    uint8_t on;
    int8_t pitch;                        /* semitones */
    uint8_t flags;                       /* STEP_REVERSE ... */
    uint8_t ratchet;                     /* 1..4 */
    uint8_t prob;                        /* 0..100 */
    uint8_t rsv[2];
} brk_step_t;
#define STEP_REVERSE 1u

typedef struct {
    float complexity, anchor, roll, fill;    /* 0..1 (PRD A-10) */
    uint32_t phrase_bars;                     /* 0 off, 2, 4, 8, 16 */
} brk_gen_t;

/* the pads' own settings, tuned with K1-K4 while a pad is held (PRD 5.1) */
#define BRK_ROLL_DIVS 8u                 /* 1/4 1/8 1/16 1/32 1/64 1/8T 1/16T 1/32T */
#define BRK_GATE_DIVS 3u                 /* 1/8 1/16 1/32 */
typedef struct {
    uint8_t roll_div[3];                 /* each roll pad's rate (0..BRK_ROLL_DIVS-1): 1/8, 1/16, 1/32 by default */
    uint8_t frz_len;                     /* freeze loop: 0 the whole slice, 1 half, 2 a quarter, 3 an eighth */
    uint8_t gate_div;                    /* 0..BRK_GATE_DIVS-1 */
    uint8_t roll_level;                  /* the roll's repeats, % of full: 5..100, ghost notes low (0, in older saves:
                                          * 100) */
    uint8_t rsv[2];
    float roll_decay;                    /* -0.9..0.9: > 0 each repeat this much quieter, < 0 louder (a swell up to
                                          * full) */
    float roll_pitch;                    /* -3..+3 semitones a repeat */
    float rev_speed;                     /* 0.5..2 */
    float frz_pitch;                     /* -12..+12 semitones */
    float half_speed;                    /* 0.25..0.75 */
    float stop_beats;                    /* tape stop: 0.25..4 beats down */
    float spin_steps;                    /* and 0.25..8 16ths back up */
    float gate_len;                      /* 0.1..0.9 of each gate */
    float rnd_amount;                    /* 0..1: the chance a step plays a random slice */
    float rnd_pitch;                     /* 0..12 semitones of random pitch */
} brk_padset_t;
extern const char *const BRK_ROLL_DIV_NAME[BRK_ROLL_DIVS];

/* the white keys' own settings, tuned with K1-K4 while a key is held (PRD B-10a). Held, a key loops a window that
 * starts in its slice: LENGTH slices long (1/8 .. 16 of them), from START sixteenths into the slice, at PITCH, in
 * MODE. The loop restarts every LENGTH on the tempo's grid, playing or stopped. */
#define BRK_KEYS 16u
#define BRK_KEY_LENS 8u                  /* 1/8 1/4 1/2 1 2 4 8 16 slices */
enum { KEY_LOOP, KEY_PINGPONG, KEY_REVERSE, KEY_ONESHOT, BRK_KEY_MODES };
typedef struct {
    uint8_t len;                         /* 0..BRK_KEY_LENS-1: 3 = one slice */
    int8_t pitch;                        /* -12..+12 semitones */
    uint8_t start;                       /* 0..15: sixteenths of the slice in */
    uint8_t mode;                        /* KEY_* */
} brk_keyset_t;                          /* 4 bytes: written to the render as one word */
extern const float BRK_KEY_SLICES[BRK_KEY_LENS];
extern const char *const BRK_KEY_MODE_NAME[BRK_KEY_MODES];
extern const char *const BRK_GATE_DIV_NAME[BRK_GATE_DIVS];

/* what the UI shows (a copy, read any time) */
typedef struct {
    int32_t step;                        /* in the pattern, -1 stopped before the first */
    int32_t slice;                       /* sounding, -1 none */
    uint32_t bar;
    float pos;                           /* the playhead in the break's samples */
    uint32_t pads;                       /* bit PAD_*: active */
    uint8_t playing;
    uint8_t roll_div;                    /* the roll's rate (BRK_ROLL_DIVS), while one is held */
    uint16_t roll_n;                     /* repeats since the roll began */
    int32_t roll_slice;                  /* the slice the roll repeats, -1 none */
    float tape;                          /* tape speed: 1 running .. 0 stopped */
    float gate;                          /* the gate's level now, 0..1 */
    float phase;                         /* how far into the step, 0..1 */
    float rewind;                        /* a rewind's progress, 0..1 (0: none) */
    int32_t key;                         /* the white key looping (its slice), -1 none */
    float key_lo, key_w;                 /* its window: from key_lo, key_w samples of the break (it may run on round
                                          * the break's end) */
    float key_phase;                     /* how far into this repeat, 0..1 */
    uint16_t key_n;                      /* repeats since the key went down */
    uint8_t key_rev;                     /* this repeat plays backwards */
} brk_status_t;

void brk_init(void);
void brk_set_break(const brk_break_t *b);   /* main loop; takes effect at once (call stopped, or at a bar) */
const brk_break_t *brk_get_break(void);

void brk_play(int on);
void brk_reset(void);                    /* to the pattern's first step (the next step played is step 0) */
void brk_rewind(void);                   /* the pull-up: the break spins back, faster and faster, then starts again
                                          * from the top of the bar */
void brk_tempo(float bpm);               /* 40..300 */
float brk_get_tempo(void);
void brk_pitch(float semitones);         /* -24..+24, on top of the tempo's repitch */
/* how pitch sounds (PRD E-9): SPEED, the classic repitch (up is shorter, down longer), or KEEP LENGTH: the voices
 * run at the tempo's speed and two overlapping grains of grain_ms (10..80) read them at the pitch, so a slice keeps
 * its length. Applies to every pitch in semitones (OCT, steps, a key's, the roll's and the freeze's); the tempo's
 * repitch, HALF, REVERSE's speed, the tape stop and the rewind stay speed */
void brk_pitch_mode(int keep_length, float grain_ms);
void brk_swing(float s);                 /* 0.5 straight .. 0.75 */
/* the gate (PRD B-30): how much of each slice the loop plays sounds, 0.05..1 (1: all of it). A slice gated at g
 * fades out once it has played g of its own length (at whatever speed), so it follows the tempo and the pitch. For
 * the loop's slices, ratchets and the RANDOM pad; key loops and rolls have their own lengths */
void brk_gate(float g);

/* the white keys: a press plays slice s at the next step (or at once when stopped: free play); held, it loops its
 * window (brk_keyset) until released, the last key pressed winning */
void brk_key(int slice, int down);
void brk_keyset_defaults(brk_keyset_t *k);   /* one slice, no pitch, from the slice's start, LOOP */
void brk_keyset(uint32_t key, const brk_keyset_t *k);   /* main loop; the next repeat follows it */
void brk_pad(int pad, int down);
void brk_gen(const brk_gen_t *g);
void brk_padset_defaults(brk_padset_t *p);
void brk_padset(const brk_padset_t *p);  /* main loop; the render follows it at once, also while a pad is held */
void brk_seed(uint32_t seed);

/* the pattern: steps 0..len-1 (len 0 = the break's own length in steps), all "follow, on" by default */
void brk_pattern_len(uint32_t len);
void brk_step_set(uint32_t i, const brk_step_t *s);
void brk_step_get(uint32_t i, brk_step_t *s);
void brk_pattern_clear(void);
void brk_randomize(void);                /* writes random slices into the pattern (PRD A-11) */

/* MIDI clock (PRD N-1, N-2). Out: while playing, the render marks MIDI's 24 ticks a beat (straight, whatever the
 * swing; the first on step 0): brk_clock_ticks, called after brk_render in the same context, gives the sample offsets
 * of the last render's. In: brk_sync, called from the render before brk_render, says the outside clock's 16th n16
 * (counted from its start) began age samples ago; the loop eases its steps onto it (or jumps, when far off). The
 * tempo itself comes from brk_tempo. */
uint32_t brk_clock_ticks(uint16_t *off, uint32_t max);
void brk_sync(uint32_t n16, float age);

/* audio interrupt: n mono frames into out[] (-1..1 floats). The FX and the output stage are the caller's
 * (brk_fx.h) */
void brk_render(float *out, uint32_t n);
void brk_status(brk_status_t *st);
uint32_t brk_renders(void);              /* renders so far: two more since brk_set_break(0) = the old break let go */

/* the host tests' view */
uint32_t brk_step_len_samples(void);
