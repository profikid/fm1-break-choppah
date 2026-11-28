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
    uint32_t split;                      /* a split break (PRD S-030): slices below it are its first half (bar 1,
                                          * break A), the rest its second (bar 2, break B); 0 none */
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
    float roll_pitch;                    /* -12..+12 semitones a repeat (the sum held to +-36) */
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
#define BRK_KEY_LENS 10u                 /* 1/8 1/4 1/2 1 2 4 8 16 slices, then 1.5 and 3 (3/16 and 3/8 of the bank's
                                          * 2-bar breaks, 16 slices each: the jungle chops: added 14 Nov 2026 at the end so saved lengths keep theirs;
                                          * LENGTH turns through them in order, BRK_KEY_ORDER) */
enum { KEY_LOOP, KEY_PINGPONG, KEY_REVERSE, KEY_ONESHOT, BRK_KEY_MODES };
typedef struct {
    uint8_t len;                         /* 0..BRK_KEY_LENS-1: 3 = one slice */
    int8_t pitch;                        /* -12..+12 semitones */
    uint8_t start;                       /* 0..15: sixteenths of the slice in */
    uint8_t mode;                        /* KEY_* */
} brk_keyset_t;                          /* 4 bytes: written to the render as one word */
extern const float BRK_KEY_SLICES[BRK_KEY_LENS];
extern const uint8_t BRK_KEY_ORDER[BRK_KEY_LENS];   /* the lengths shortest first: 0 1 2 3 8 4 9 5 6 7 */

/* each slice's own effect (PRD B-32), whenever that slice plays (the loop, a key, a roll): a filter, crunch, drive or
 * ring mod on the slice alone, or a throw of it into the FX chain's delay (ECHO) or reverb (VERB); and its own level.
 * amount, tone and level are 0..100 */
enum { SLX_OFF, SLX_LOPASS, SLX_HIPASS, SLX_CRUNCH, SLX_DRIVE, SLX_RING, SLX_ECHO, SLX_VERB, BRK_SLX };
extern const char *const BRK_SLX_NAME[BRK_SLX];
typedef struct {
    uint8_t type;                        /* SLX_* */
    uint8_t amount;                      /* LO-PASS: darker; HI-PASS: thinner; CRUNCH, DRIVE: more; RING: its pitch;
                                          * ECHO, VERB: the send */
    uint8_t tone;                        /* the filters' resonance; RING's mix */
    uint8_t level;                       /* the slice's level */
} brk_slicefx_t;                         /* 4 bytes: a word to the render */
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
    int32_t key;                         /* the white key looping (its slice), -1 none */
    float key_lo, key_w;                 /* its window: from key_lo, key_w samples of the break (it may run on round
                                          * the break's end) */
    float key_phase;                     /* how far into this repeat, 0..1 */
    uint16_t key_n;                      /* repeats since the key went down */
    uint8_t key_rev;                     /* this repeat plays backwards */
    int8_t bar16;                        /* the step's place in the bar, 0..15 (as brk_step_events), -1 none yet */
    int8_t half;                         /* the split's half in force (brk_half): -1 both, 0 the first, 1 the second */
    int32_t layer;                       /* the layer's key looping (its slice), -1 none (PRD S-032) */
    float layer_lo, layer_w, layer_phase;   /* its window and how far into this repeat, as the key's */
    float layer_pos;                     /* its playhead in the break's samples, -1 quiet */
    /* MATHCORE (docs/PRD_MATHCORE.md) */
    int8_t mc_scene;                     /* the scene playing, -1 none */
    int8_t mc_next;                      /* the scene after it (as asked), -1 none: the lanes stop at the boundary */
    uint16_t mc_step;                    /* 16ths into the scene */
    uint32_t mc_adv;                     /* scene boundaries passed since the start (a count) */
    uint32_t mc_ended;                   /* a count: the queue ran out at a boundary (the transport should stop) */
    uint8_t mc_pos[4];                   /* each lane's place in its cycle (0..len-1) */
    uint8_t mc_hits[4];                  /* each lane's hits (a count: the screen flashes on a change) */
    uint8_t mc_missing;                  /* bit l: lane l (or the role hit) found no slice of its role: silent */
    int8_t mc_slice[4];                  /* each lane's last slice (the lanes sound together: MG-001) */
} brk_status_t;

/* ---------------------------------------------------------------- MATHCORE --- */
/* The break's slices tagged with roles (the Slice Map), and lanes that play them: the pattern's own drum lanes
 * (kick, snare, hat, 16 steps a bar) or the four polymetric lanes of a scene, scenes launched and chained
 * (docs/PRD_MATHCORE.md). Every hit is a slice of the break on the break's own voice, cutting the one before (a scene
 * re-cuts the break; nothing is laid over it, nothing is synthesised). All the lanes count from
 * one master timeline (MC_TICKS ticks a 16th, counted from the transport's start), so cycles of 5, 7, 11 and 13 run
 * together without drifting. */
#define MC_LANES 4u
#define MC_EVENTS 32u                    /* a lane's cycle: 1..32 events */
#define MC_SCENES 16u                    /* scenes A..P */
#define MC_TICKS 12u                     /* master ticks in a 16th: 1/32 is 6, 1/16T 8, 1/32T 4 */
/* the groove-event layer (docs/PRD_MATHCORE_GROOVE.md): the events beside the scenes. GRUV: the context and the
   scene map (8 bytes, then 4 a scene). EVNT: the events, ten bytes each. GROO: a break's own groove, so it
   travels in the file and not only in the hub's database (the SSET is in the device's saved state: it stays) */
#define MC_GRUV_BYTES 8u
#define MC_EVNT_BYTES 10u
#define MCE_F_GHOST 1u                   /* a groove event's flags (the kinds have MCE_ of their own) */
#define MCE_F_ACCENT 2u
#define MCE_F_ART 0x30u                  /* the articulation: bits 4-5 */
#define MCE_F_LANE 0xC0u                 /* the lane: bits 6-7 */
typedef struct __attribute__((packed)) {
    uint8_t flags;                       /* the ghost, the accent, the articulation, the lane */
    uint8_t velocity;                    /* 0..127 */
    int16_t micro_us;                    /* the offset from the grid, signed (negative: early) */
    int8_t pitch;                        /* -24..+24 */
    uint8_t slice;                       /* the source slice */
    uint8_t timbre;                      /* 0..255 */
    uint8_t gate;                        /* 0..100 */
    uint8_t pos;                         /* the lane's event index */
    uint8_t rsv;
} mc_event_t;                           /* 10 bytes */
#define MC_CAPS_VELOCITY 1u              /* what the device can do (the status' last bytes) */
#define MC_CAPS_MICRO 2u
#define MC_CAPS_ARTICULATION 4u
#define MC_CAPS_TIMBRE 8u
#define MC_CAPS_LAYERING 16u
#define MC_CAPS_EARLY 32u
enum { ROLE_KICK, ROLE_SNARE, ROLE_HAT, ROLE_PERC, MC_ROLES };   /* bit 1 << role in a slice's role byte */
enum { MCR_32, MCR_16, MCR_8, MCR_4, MCR_32T, MCR_16T, MCR_8T, MC_RATES };   /* a lane's event: 1/32 .. 1/4 */
extern const uint8_t MC_RATE_TICKS[MC_RATES];
extern const char *const MC_RATE_NAME[MC_RATES];
/* how a lane picks among its role's slices: always one (FIXED), each in turn (ROUND), at random (RANDOM, the lane's
 * seed: the same seed, the same choices), two in turn (ALT), or one for soft hits and the next for accents (ACCENT) */
enum { MCM_FIXED, MCM_ROUND, MCM_RANDOM, MCM_ALT, MCM_ACCENT, MC_MODES };
extern const char *const MC_MODE_NAME[MC_MODES];
#define MCL_ON 1u                        /* the lane plays in its scene */
#define MCL_REV 2u                       /* its slices backwards */
#define MCL_FREE 4u                      /* it counts from the transport's start, not the scene's (no reset) */
#define MCL_SWELL 8u                     /* its RISE climbs and swells over the phrase without speeding up (a roll
                                          * that builds) */
typedef struct {
    uint32_t hits;                       /* bit e: an event at e */
    uint32_t accent;                     /* bit e: e accented (no accents at all: every hit full) */
    uint8_t len;                         /* events in the cycle, 1..MC_EVENTS */
    uint8_t rate;                        /* MCR_* */
    uint8_t phase;                       /* the cycle starts this many events in */
    uint8_t prob;                        /* 0..100 %: the chance an event plays (deterministic: the seed) */
    uint8_t ratchet;                     /* 1..8 hits an event */
    int8_t pitch;                        /* -12..+12 semitones (as speed) */
    uint8_t flags;                       /* MCL_* */
    uint8_t mode;                        /* MCM_* */
    uint8_t pick;                        /* which of the role's slices FIXED, ALT and ACCENT start from */
    uint8_t gate;                        /* 5..100 % of each slice sounds */
    uint8_t level;                       /* 0..100 % (unaccented hits of a lane with accents: 55 % of it) */
    uint8_t role;                        /* ROLE_* */
    uint8_t seed;
    int8_t rise;                         /* a riser: over the scene's phrase the lane speeds up (its rate halved each
                                          * quarter of the phrase, to 1/64), swells and climbs this many semitones
                                          * (0: none) */
    uint8_t vary;                        /* 0..100: how much it varies by itself, as a drummer would: on the hats
                                          * velocity, other hat slices, a skipped off-beat, a little roll, laid back
                                          * a touch; on the snares ghost notes round the backbeat, flams, the other
                                          * snare hits; a kick's pickup now and then. More in a phrase's last bar.
                                          * From the seed: the same seed, the same variations */
    uint8_t rsv;
} mc_lane_t;                             /* 24 bytes */
#define MCS_BREAK 1u                     /* RUN ON: a hit runs on into the break until the next (else dry chops) */
#define MCS_ONCE 2u                      /* a fill, a riser, a drop: plays once, then back to the groove */
typedef struct {
    mc_lane_t lane[MC_LANES];
    uint8_t bars;                        /* its phrase: 1, 2, 4 or 8 bars, then the next scene */
    uint8_t flags;                       /* MCS_* */
    uint8_t name;                        /* its name (MC_NAME), 0 none */
    uint8_t rsv;
} mc_scene_t;                            /* 100 bytes */

void brk_roles(const uint8_t *roles, uint32_t n);   /* slice i's role bits, for the break playing (main loop) */
void mc_auto_roles(const brk_break_t *b, uint8_t *roles);   /* the Slice Map heard from b's audio (BRK_MAX_SLICES
                                          * role bytes): KICK, SNARE and HAT each at least once (main loop) */
void brk_drum_lanes(uint16_t kick, uint16_t snare, uint16_t hat);   /* the pattern's drum lanes: bit n, step n of
                                          * each bar: the loop plays the role's first slice there instead of its own
                                          * (one voice: the snare wins, then the kick, then the hat). A scene playing
                                          * replaces the pattern */
void brk_role_hit(uint32_t role);        /* the role's first slice on the break's voice, at the next render (MIDI) */
void brk_mc_scenes(const mc_scene_t *s); /* the scene table (MC_SCENES), read by the render; edited in place */
void brk_mc_groove(const uint8_t *gruv, uint32_t gruv_len, const uint8_t *evnt, uint32_t evnt_len);
                                        /* the groove-event layer: the context and the events (null: none) */
uint8_t brk_mc_caps(void);               /* what the device can do (the bits above) */
void brk_mc_start(int scene);            /* the scene to play from the next step (with brk_play: the first); -1 none */
void brk_mc_next(int scene);             /* the scene after the one playing (-1: the lanes stop at its end) */
void brk_mc_mute(uint32_t mute, uint32_t solo);   /* lanes muted (bit l) and soloed: their phase runs on */
/* a launch while playing: scene from the next step on a multiple of quant 16ths (4 a beat, 16 a bar), counted as
 * if it had begun with the bar it lands in (a fill launched on beat 3 plays its last half and ends on the bar line;
 * a groove taken back on a beat rejoins in phase) */
void brk_mc_jump(int scene, uint32_t quant);
/* the performance macros, over every lane of every scene (0..100, 50 as written): DENSITY (fewer hits .. more, the
 * extra ones the same each cycle), MATH (0 the lanes' own lengths .. 100 cycles of 5, 7, 11, 13), SPEED (< 34 half,
 * > 66 double), CHAOS (random slices, ratchets, reverses) */
void brk_mc_macro(uint32_t density, uint32_t math, uint32_t speed, uint32_t chaos);
enum { MCE_NONE, MCE_HIT, MCE_ACCENT, MCE_ADDED, MCE_DROPPED };
uint32_t mc_event_kind(const mc_lane_t *L, uint32_t lane, uint32_t pos, uint32_t density);   /* (the screen's too) */
#define MC_MATH_STEPS 8u
extern const uint8_t MC_MATH_LEN[MC_MATH_STEPS][4];   /* MATH's lane lengths at each of its steps (0: the lane's own) */
uint32_t mc_role_slices(const uint8_t *roles, uint32_t n, uint32_t role, uint8_t *list);   /* the slices of a role,
                                          * in order, into list (MC_EVENTS); their count */

void brk_init(void);
void brk_set_break(const brk_break_t *b);   /* main loop; takes effect at once (call stopped, or at a bar) */
void brk_mute(int on);                   /* the break silent (an 8 ms fade), the clock and the steps running on */
void brk_swap_break(const brk_break_t *b);   /* b takes over at the next step (stopped: the next render), its
                                          * voices clean, and the mute ends. Swapping while the old break
                                          * plays muted lets the main loop rewrite its PCM meanwhile */
const brk_break_t *brk_get_break(void);

void brk_play(int on);
void brk_reset(void);                    /* to the pattern's first step (the next step played is step 0) */
void brk_tempo(float bpm);               /* 40..300 */
float brk_get_tempo(void);
void brk_pitch(float semitones);         /* -24..+24, on top of the tempo's repitch */
/* how pitch sounds (PRD E-9): SPEED, the classic repitch (up is shorter, down longer), or KEEP LENGTH: the voices
 * run at the tempo's speed and two overlapping grains of grain_ms (10..80) read them at the pitch, so a slice keeps
 * its length. Applies to every pitch in semitones (OCT, steps, a key's, the roll's and the freeze's); the tempo's
 * repitch, HALF, REVERSE's speed and the tape stop stay speed */
void brk_pitch_mode(int keep_length, float grain_ms);
/* a split break (brk_break_t.split, PRD S-030 to S-032): a white key held in the half the key looping isn't in layers
 * on voices of its own (one key a half; within a half the last pressed wins). brk_half keeps the loop to one half
 * (0, 1; -1 both, the default): the change waits for the next bar line, and the loop's own slices (the pattern's,
 * the generator's, RANDOM's) fold into that half; keys still play any slice. Without a split it does nothing */
void brk_half(int h);
int brk_get_half(void);                  /* the half asked for */
void brk_swing(float s);                 /* 0.5 straight .. 0.75 */
/* the gate (PRD B-30): how much of each slice the loop plays sounds, 0.05..1 (1: all of it). A slice gated at g
 * fades out once it has played g of its own length (at whatever speed), so it follows the tempo and the pitch. For
 * the loop's slices, ratchets and the RANDOM pad; key loops and rolls have their own lengths */
void brk_gate(float g);
/* the snap (PRD B-31): where a white key pressed while playing begins, in slices of the break: 0 at once, else at the
 * next point of a grid that many slices apart from the pattern's start (1/8 .. 4; 0.5 is the next 16th on a 2-bar
 * break of 16 slices, the default). Whole steps follow the swing; less than a step is timed straight */
void brk_snap(float slices);

/* the white keys: a press plays slice s at the next snap point (brk_snap; at once when stopped: free play); held, it
 * loops its window (brk_keyset) until released, the last key pressed winning */
void brk_key(int slice, int down);
void brk_keyset_defaults(brk_keyset_t *k);   /* one slice, no pitch, from the slice's start, LOOP */
void brk_keyset(uint32_t key, const brk_keyset_t *k);   /* main loop; the next repeat follows it */
void brk_slicefx_defaults(brk_slicefx_t *f);   /* off, level 100 */
void brk_slicefx(uint32_t slice, const brk_slicefx_t *f);   /* main loop; the slice's next hit follows it */
/* the render, before brk_render: where it writes the slices' throws (ECHO, VERB) for the effects; 0: none */
void brk_sends(float *echo, float *verb);
/* the overlay (PRD_PACKS 7, PO-001): a longer loop of the pack's (4, 8 or 16 bars of IMA ADPCM, read from flash as it
 * plays: never decoded whole) under the breaks, on the transport's bars (bar b plays its bar b mod bars), repitched
 * with the tempo; TAPE STOP, HALF and STOP reach it, nothing else of the pads. brk_overlay: main loop (0: none; it
 * walks the data once to mark where decoding can start); brk_overlay_on: while a break of its pack plays;
 * brk_overlay_out: before each brk_render, where its samples go (0: nowhere) */
void brk_overlay(const uint8_t *adpcm, uint32_t len, uint32_t bars);
void brk_overlay_on(int on);
void brk_overlay_out(float *o);
/* the layer (docs/PRD_LAYER.md): the pack's loops (up to BRK_LY_LOOPS, the overlay is loop 0) and one-shots (up to
 * BRK_LY_SHOTS), both read from flash as they play, into brk_overlay_out's samples. A loop sounds while it is on, the
 * transport plays and brk_overlay_on says so; on its own bars always (in phase), through its window. A shot plays
 * from its start, at its rate (with the tempo when it has one), its pitch, decay, mode and choke; a CUT shot silences
 * the break while it sounds (a fill), the break running on underneath. Main loop: brk_ly_loop / brk_ly_shot set the
 * sources (0: none; a loop's walk marks where decoding can start), the *_cfg words their settings (BRK_LY_* fields),
 * brk_ly_want turns a loop on or off at a quantise point (LYQ_*), brk_ly_hit fires a shot or lets its key go,
 * brk_ly_voices the voices allowed (GLOBAL's LAYER: the render's cost, PRD_LAYER LY-053), brk_ly_gain the MIXER's
 * loops and shots levels. brk_ly_clear: everything let go (before the flash it reads is erased) */
#define BRK_LY_LOOPS 4u
#define BRK_LY_LV 2u                     /* loop voices at most */
#define BRK_LY_SHOTS 16u
#define BRK_LY_SV 4u                     /* shot voices at most */
enum { LYQ_NOW, LYQ_BEAT, LYQ_BAR, LYQ_LOOP, LYQ_N };   /* a loop's quantise: at once, the beat, the bar, its window */
enum { SHQ_NOW, SHQ_16, SHQ_BEAT, SHQ_N };             /* a shot's */
enum { SHM_ONE, SHM_GATE, SHM_LOOP, SHM_N };           /* a shot's mode: plays out, stops on release, loops held */
/* a loop's cfg word: level 0..100 | (filter + 50) << 7 (-50 low-pass .. 0 open .. +50 high-pass) | window start bar
 * << 14 (0..15) | window bars - 1 << 18 (0..15; past the loop's bars: all of it) | quantise << 23 | auto << 25 (on
 * when PLAY starts) */
#define BRK_LY_LCFG(lv, flt, ws, wl, q, au) ((uint32_t)(lv) | (uint32_t)((flt) + 50) << 7 | (uint32_t)(ws) << 14 | \
                                             (uint32_t)((wl) - 1u) << 18 | (uint32_t)(q) << 23 | (uint32_t)(au) << 25)
/* a shot's: level 0..100 | (pitch + 24) << 7 | decay << 13 (0 none, 1 long .. 31 short) | mode << 18 | choke << 20
 * (0 none, 1..4) | quantise << 23 | cut << 25 (silences the break while it sounds) */
#define BRK_LY_SCFG(lv, pt, dc, md, ch, q, cut) ((uint32_t)(lv) | (uint32_t)((pt) + 24) << 7 | (uint32_t)(dc) << 13 | \
                                                 (uint32_t)(md) << 18 | (uint32_t)(ch) << 20 | (uint32_t)(q) << 23 | \
                                                 (uint32_t)(cut) << 25)
void brk_ly_loop(uint32_t i, const uint8_t *adpcm, uint32_t len, uint32_t bars);
void brk_ly_loop_cfg(uint32_t i, uint32_t cfg);
void brk_ly_want(uint32_t i, int on, uint32_t q);
void brk_ly_shot(uint32_t i, const uint8_t *adpcm, uint32_t len, uint32_t rate, uint32_t bpm_x100);
void brk_ly_shot_cfg(uint32_t i, uint32_t cfg);
void brk_ly_hit(uint32_t i, int down);
void brk_ly_voices(uint32_t loops, uint32_t shots);
void brk_ly_gain(float loops, float shots);
void brk_ly_clear(void);
/* what the layer does, for the screen: loops on, waiting for their point, each one's place in its window (0..255),
 * the shots sounding, the break cut */
typedef struct {
    uint8_t on, pend, cut, rsv;
    uint8_t ph[BRK_LY_LOOPS];
    uint16_t shots, rsv2;
} brk_ly_status_t;
void brk_ly_status(brk_ly_status_t *s);
void brk_pad(int pad, int down);
void brk_gen(const brk_gen_t *g);
void brk_padset_defaults(brk_padset_t *p);
void brk_padset(const brk_padset_t *p);  /* main loop; the render follows it at once, also while a pad is held */
void brk_seed(uint32_t seed);

/* the pattern: the break's own length in steps, all "follow, on" by default (the app's sequencer writes it) */
void brk_step_set(uint32_t i, const brk_step_t *s);
void brk_step_get(uint32_t i, brk_step_t *s);
void brk_randomize(void);                /* writes random slices into the pattern (PRD A-11) */

/* MIDI clock (PRD N-1, N-2). Out: while playing, the render marks MIDI's 24 ticks a beat (straight, whatever the
 * swing; the first on step 0): brk_clock_ticks, called after brk_render in the same context, gives the sample offsets
 * of the last render's. In: brk_sync, called from the render before brk_render, says the outside clock's 16th n16
 * (counted from its start) began age samples ago; the loop eases its steps onto it (or jumps, when far off). The
 * tempo itself comes from brk_tempo. */
uint32_t brk_clock_ticks(uint16_t *off, uint32_t max);
void brk_sync(uint32_t n16, float age);
/* the steps begun in the last render (called after brk_render, in its context): their sample offsets, and each one's
 * place in the bar (0..15, counted from the reset like the outside clock's, whatever the loop's length). The kick's
 * lane (kick.h) plays from these. */
uint32_t brk_step_events(uint16_t *off, uint8_t *idx, uint32_t max);

/* audio interrupt: n mono frames into out[] (-1..1 floats). The FX and the output stage are the caller's
 * (brk_fx.h) */
void brk_render(float *out, uint32_t n);
void brk_status(brk_status_t *st);
uint32_t brk_renders(void);              /* renders so far: two more since brk_set_break(0) = the old break let go */

/* the host tests' view */
uint32_t brk_step_len_samples(void);
