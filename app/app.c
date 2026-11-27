/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH on fm1-base: the app's unity build (compiled into fm1-base's device/unity.c after the platform, so gfx.c's
 * cv_* drawing and the plat_* calls are in scope) and the glue between the contract (app.h) and the engine
 * (dsp/brk.c, dsp/brk_fx.c: their own -O2 units).
 *
 * M1 scope (PRD 13): the built-in break, 16 slices on the white keys, the 11 pads on the black keys, play / reset,
 * tempo and tap tempo, pitch on OCT-/OCT+, the generator, the four FX and the DJ filter, settings kept by SAVE.
 * Breaks: the bank built into the image (app/bank_data.h, which ./build.sh generates from breaks/breaks.txt), chosen
 * on ALGORITHM. Every sound is the break's: the drum lanes and MATHCORE's scenes play its slices (docs/PRD_MATHCORE.md);
 * nothing is synthesised (a build with no bank is silent). */
#include <stddef.h>
#include "adpcm.h"
#include "brk_bank.h"
#include "brk_math.h"
#if defined(__has_include)
#if __has_include("bank_data.h")
#include "bank_data.h"
#define CH_HAVE_BANK 1
#endif
#endif
#ifndef CH_HAVE_BANK
#define CH_HAVE_BANK 0
#endif

/* the theme (PRD_PACKS PT-001, PT-002): a pack break's screen in its pack's colours (THEM), any other break's in the
 * factory's amber. fm1-base keeps its five steps in RAM (gfx.c pal[]); the ground and the slice blocks are CHOPPAH's */
static uint16_t th_bg, th_block = RGB(24, 24, 30), th_block_hi = RGB(255, 184, 70);
static uint8_t th_dirty, th_pack;        /* th_dirty: the whole screen again */
#undef C_BLACK
#define C_BLACK th_bg

/* ------------------------------------------------------------------ settings --- */
#define NPFX 11u                         /* the ARP page's black keys: an effect slot each (PRD B-55) */
#define SET_MAGIC 0x37504843u            /* "CHP7" (the soundboard gone, the drums and patterns in): another magic
                                          * loads defaults */
/* From CHP7 on the settings only grow at the end: a shorter save (an older firmware's) loads into the front and the
 * fields after it keep their defaults (SET_SIZE_MIN: CHP7's size; an append keeps it). A change that isn't an append
 * needs a new magic (and loses the saves). */
typedef struct {
    uint32_t magic;
    uint16_t tempo_x10;                  /* 400..3000 */
    int8_t pitch;                        /* semitones */
    uint8_t swing;                       /* 50..75 % */
    brk_gen_t gen;
    brk_fx_t fx;
    uint8_t brk;                         /* the break: 0 the synthesised one, 1.. the bank's slots */
    uint8_t clk_in;                      /* MIDI clock in: 0 off, 1 follow (the tempo, start / stop) */
    uint8_t clk_out;                     /* MIDI clock out: 0 off, 1 on (24 a beat while playing, start / stop) */
    uint8_t ble_off;                     /* Bluetooth MIDI: 0 on (the default), 1 off */
    uint8_t midi_to;                     /* where MIDI goes: 0 USB and Bluetooth, 1 USB, 2 Bluetooth */
    uint8_t pitch_keep;                  /* pitch: 1 KEEP LENGTH (granular; the default), 2 SPEED (repitch) */
    uint8_t grain_ms;                    /* its grains, 10..80 ms */
    uint8_t gate;                        /* how much of each slice sounds, 5..100 % (100: off) */
    uint8_t snap;                        /* where a key lands while playing: SNAP_SL index + 1 */
    uint8_t brk_level;                   /* the MIXER's BREAK fader, 0..100 % */
    uint8_t drums_chain;                 /* (the drums' route through the FX chain or around it: gone with them, the
                                          * lanes playing the break itself; unused) */
    uint8_t pattern;                     /* the pattern playing, 0..NPAT-1 (patterns: their own store object) */
    uint8_t bta_on;                      /* (Bluetooth audio out's on byte, PRD A-72: gone 21 Nov 2026, kept so older
                                          * saves keep their size; unused) */
    uint8_t bta_rsv;                     /* (the SBC coding's byte: it was a knob once, but the knob is gone; the byte
                                          * stays, so older saves keep their size) */
    uint8_t click;                       /* the metronome's click (TAP K4): its level, 0 (off) .. 100 %, on each beat
                                          * while playing (was a spare byte: older saves have it off) */
    uint8_t rsv;
    brk_padset_t pads;                   /* the pads' own settings (K1-K4 while a pad is held) */
    brk_keyset_t keys[BRK_KEYS];         /* each white key's loop: length, pitch, start, mode (K1-K4 while held) */
    brk_slicefx_t slx[BRK_KEYS];         /* each slice's own effect and level (a key held, FX: its other layer) */
    uint8_t rsv_kick[136];               /* (the synthesised kick's settings, PRD K-*: gone with MATHCORE M0, 22 Nov
                                          * 2026; their bytes kept, older saves still load, never read) */
    uint8_t rsv_perc[52];                /* (the synthesised snare's and hat's, PRD K-10: the same) */
    uint8_t rsv2[8];                     /* (v0.52's fills, gone; their bytes kept: older saves still load) */
    uint8_t rsv3[20];                    /* (3 Nov 2026's overlay effects, PRD B-52, gone 8 Nov 2026: their bytes kept) */
    /* (appended 4 Nov 2026: MB COMP's detail per FX slot, PRD A-49a; it moves with its slot) */
    brk_mbc_t mbc[BRK_FX_SLOTS];
    /* (appended 4 Nov 2026: every slot's extras, its MORE and MOD pages, PRD A-49b; they move with their slot) */
    brk_fxx_t fxx[BRK_FX_SLOTS];
    /* (appended 8 Nov 2026: the ARP page's black keys, each an effect slot, PRD B-55) */
    brk_fxslot_t pfx[NPFX];
    brk_fxx_t pfxx[NPFX];
    /* (appended 13 Nov 2026: GLOBAL's CHANNEL part, PRD A-72..A-74, A-77; 0 = OMNI, nothing out: older saves too) */
    uint8_t midi_ch;                     /* the channel notes, CC and program changes come in on (and go out on):
                                          * 0 any (out: 1), 1..16 that one */
    uint8_t midi_out;                    /* MO_*: what goes out besides the clock */
    uint8_t rsv4[2];
    /* (appended 21 Nov 2026: the split, PRD S-030, GLOBAL's SPLIT part; 0 = off, older saves too) */
    uint8_t split;                       /* 1: the break is two: bar 1 of break A (brk), bar 2 of break B */
    uint8_t split_b;                     /* break B: its number + 1 (0: the break after A) */
    uint8_t split_bars;                  /* bit 0: A's second bar, bit 1: B's second bar (0: their first) */
    uint8_t rsv5;
    /* (appended 22 Nov 2026: MATHCORE, docs/PRD_MATHCORE.md) */
    uint8_t lane_level;                  /* the MIXER's OVERLAY fader, 0..100 % (24 Nov 2026, PRD_PACKS PO-003; the LANES
                                          * fader of the first MATHCORE build before: its byte, its default 100) */
    uint8_t ov_route;                    /* the overlay through the FX chain (0) or around it (1) (PO-005) */
    uint8_t mc_rsv[2];
    uint8_t mc_macro[4];                 /* MATHCORE LIVE's K1-K4: DENSITY, MATH, SPEED, CHAOS (0..100) */
    /* (appended 26 Nov 2026: the layer, docs/PRD_LAYER.md; older saves get the defaults) */
    uint8_t ly_lv;                       /* GLOBAL's LAYER: loop voices, 1..2 (LY-053: 1, the fallback) */
    uint8_t ly_sv;                       /* shot voices, 1..4 (3) */
    uint8_t shot_level;                  /* the MIXER's SEL + K2: the shots' level, 0..100 % */
    uint8_t ly_hold;                     /* a loop key held this long (x 10 ms) plays while held; shorter: a tap */
} settings_t;
#define SET_SIZE_MIN offsetof(settings_t, rsv2)   /* the shortest save that loads: CHP7's first size */
_Static_assert(offsetof(settings_t, rsv2) == 524u && offsetof(settings_t, mbc) == 552u,
               "the drums' bytes keep their room: older saves load into the same places");
_Static_assert(sizeof(settings_t) <= 3840u, "a store object holds 3840 bytes");
static settings_t set;
static uint8_t set_dirty;
static uint8_t foot_dirty;               /* a setting changed: the slicer's foot shows the new value */

static void settings_defaults(void)
{
    uint32_t k;
    uint8_t *z = (uint8_t *)&set;
    for (k = 0; k < sizeof set; k++)     /* (every field: no leftovers from a save that didn't load) */
        z[k] = 0u;
    set.magic = SET_MAGIC;
    set.tempo_x10 = 1700u;               /* jungle tempo */
    set.swing = 50u;
    set.gen.anchor = 0.7f;
    set.gen.roll = 0.3f;
    set.gen.fill = 0.5f;
    set.gen.phrase_bars = 4u;
    brk_fx_defaults(&set.fx);
    set.pitch_keep = 1u;
    set.grain_ms = 30u;
    set.gate = 100u;
    set.snap = 5u;                       /* 1 slice */
    set.brk_level = 100u;
    set.drums_chain = 1u;
    set.lane_level = 100u;
    set.ly_lv = 1u;
    set.ly_sv = 3u;
    set.shot_level = 100u;
    set.ly_hold = 25u;
    set.mc_macro[0] = 50u;               /* DENSITY as written, MATH off, SPEED as written, no CHAOS */
    set.mc_macro[1] = 0u;
    set.mc_macro[2] = 50u;
    set.mc_macro[3] = 0u;
    brk_padset_defaults(&set.pads);
    for (k = 0; k < BRK_KEYS; k++) {
        brk_keyset_defaults(&set.keys[k]);
        brk_slicefx_defaults(&set.slx[k]);
    }
    for (k = 0; k < BRK_FX_SLOTS; k++) {
        brk_mbc_defaults(&set.mbc[k]);
        brk_fxx_defaults(&set.fxx[k], set.fx.slot[k].type);
    }
    {   /* the ARP page's keys: three filters (low, band, high), crunch, drive, ring mod, flanger, a gate, tape wow,
         * a delay and a reverb throw */
        static const uint8_t T[NPFX] = {FXT_FILTER, FXT_FILTER, FXT_FILTER, FXT_CRUNCH, FXT_DRIVE, FXT_RING,
                                        FXT_FLANGER, FXT_TREMOLO, FXT_TAPE, FXT_DELAY, FXT_REVERB};
        static const float P[NPFX][4] = {{0.40f, 0.55f, 0.0f, 1.0f}, {0.62f, 0.40f, 1.0f, 1.0f},
                                         {0.60f, 0.70f, 0.5f, 1.0f}, {0.75f, 0.60f, 0.3f, 1.0f},
                                         {0.60f, 0.50f, 0.6f, 1.0f}, {0.45f, 0.30f, 0.0f, 1.0f},
                                         {0.45f, 0.80f, 0.85f, 1.0f}, {0.60f, 1.00f, 0.0f, 1.0f},
                                         {0.50f, 0.50f, 0.70f, 1.0f}, {2.0f / 7.0f, 0.60f, 0.5f, 0.8f},
                                         {0.75f, 0.40f, 0.25f, 0.6f}};
        for (k = 0; k < NPFX; k++) {
            brk_fx_slot_defaults(&set.pfx[k], T[k]);
            set.pfx[k].on = 1u;
            set.pfx[k].p[0] = P[k][0];
            set.pfx[k].p[1] = P[k][1];
            set.pfx[k].p[2] = P[k][2];
            set.pfx[k].mix = P[k][3];
            brk_fxx_defaults(&set.pfxx[k], T[k]);
        }
    }
}

static int pitch_keeps(void) { return set.pitch_keep != 2u; }   /* KEEP LENGTH unless SPEED was chosen */

/* the snap: where a white key pressed while playing lands, in slices (GLOBAL, second half, K3) */
#define SNAPS 7u
static const float SNAP_SL[SNAPS] = {0.0f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
static const char *const SNAP_NAME[SNAPS] = {"OFF", "1/8 SLICE", "1/4 SLICE", "1/2 SLICE", "1 SLICE", "2 SLICES",
                                             "4 SLICES"};
static const char *const SNAP_SHORT[SNAPS] = {"OFF", "1/8 SL", "1/4 SL", "1/2 SL", "1 SL", "2 SL", "4 SL"};
static uint32_t snap_index(void) { return set.snap && set.snap <= SNAPS ? set.snap - 1u : 4u; }

static int8_t slot_pitch;                /* the break playing's own pitch and swing (its pack's), on top of the global ones */
static uint8_t slot_swing;
static void settings_apply(void)
{
    brk_tempo((float)set.tempo_x10 * 0.1f);
    brk_pitch((float)(set.pitch + slot_pitch));
    brk_swing((float)(slot_swing ? slot_swing : set.swing) * 0.01f);
    brk_gen(&set.gen);
    brk_fx_set(&set.fx);
    {
        uint32_t k;
        for (k = 0; k < BRK_FX_SLOTS; k++) {
            brk_fx_set_mbc(k, &set.mbc[k]);
            brk_fx_set_fxx(k, &set.fxx[k]);
        }
    }
    brk_fx_tempo((float)set.tempo_x10 * 0.1f);
    brk_padset(&set.pads);
    brk_pitch_mode(pitch_keeps(), (float)(set.grain_ms ? set.grain_ms : 30u));
    brk_gate((float)(set.gate ? set.gate : 100u) * 0.01f);
    brk_snap(SNAP_SL[snap_index()]);
    brk_mc_macro(set.mc_macro[0], set.mc_macro[1], set.mc_macro[2], set.mc_macro[3]);
    {
        uint32_t k;
        for (k = 0; k < BRK_KEYS; k++) {
            brk_keyset(k, &set.keys[k]);
            brk_slicefx(k, &set.slx[k]);
        }
    }
}

static void changed(void)
{
    set_dirty = 1u;
    foot_dirty = 1u;
    settings_apply();
}

/* ------------------------------------------------------------------ patterns --- */
/* the pattern sequencer (PRD B-20..B-22): 16 patterns, each 32 steps of the break (2 bars: slice or follow, pitch,
 * ratchet, reverse; the engine plays them, brk_step_set) and 16-step lanes for the kick, the snare and the hat (each
 * bar). Saved with SAVE in their own store object. PRESETS picks one: while playing it takes over at the end of the
 * loop */
#define NPAT 16u
#define PAT_STEPS 32u
#define PAT_MAGIC 0x31544150u            /* "PAT1" */
enum { LANE_KICK, LANE_SNARE, LANE_HAT, NLANES };
typedef struct {
    uint8_t st[PAT_STEPS][4];            /* per step: slice + 2 (0 off, 1 follow the loop, 2.. slice 1..), pitch
                                          * (int8), flags (bit 0 reverse, bits 1-2 ratchet - 1), 0 */
    uint16_t lane[NLANES];               /* bit n: the drum hits on step n of each bar */
    uint16_t rsv;
} pat_t;                                 /* 136 bytes */
static struct {
    uint32_t magic;
    pat_t p[NPAT];
} pats;
_Static_assert(sizeof pats <= 3840u, "a store object holds 3840 bytes");
static int8_t pat_next = -1;             /* a pattern picked while playing: it starts at the loop's end */

static void pat_defaults(void)
{
    uint32_t i, k;
    pats.magic = PAT_MAGIC;
    for (i = 0; i < NPAT; i++) {
        pat_t *p = &pats.p[i];
        for (k = 0; k < PAT_STEPS; k++) {
            p->st[k][0] = 1u;            /* follow the loop */
            p->st[k][1] = p->st[k][2] = p->st[k][3] = 0u;
        }
        p->lane[LANE_KICK] = p->lane[LANE_SNARE] = p->lane[LANE_HAT] = 0u;   /* the drums start empty */
        p->rsv = 0u;
    }
}
static void pat_step_out(uint32_t k)     /* step k of the pattern playing, to the engine */
{
    const uint8_t *b = pats.p[set.pattern % NPAT].st[k];
    brk_step_t s;
    s.slice = (int8_t)(b[0] >= 2u ? (int32_t)b[0] - 2 : BRK_NO_SLICE);
    s.on = (uint8_t)(b[0] != 0u);
    s.pitch = (int8_t)b[1];
    s.flags = (uint8_t)(b[2] & 1u ? STEP_REVERSE : 0u);
    s.ratchet = (uint8_t)(((b[2] >> 1) & 3u) + 1u);
    s.prob = 100u;
    s.rsv[0] = s.rsv[1] = 0u;
    brk_step_set(k, &s);
}
static void pat_lanes_out(void)          /* the pattern's drum lanes: the engine plays their roles' slices */
{
    const pat_t *p = &pats.p[set.pattern % NPAT];
    brk_drum_lanes(p->lane[LANE_KICK], p->lane[LANE_SNARE], p->lane[LANE_HAT]);
}
static void pat_apply(void)              /* the pattern playing, all of it to the engine */
{
    uint32_t k;
    for (k = 0; k < PAT_STEPS; k++)
        pat_step_out(k);
    pat_lanes_out();
}
static void pat_read_back(void)          /* the engine's steps into the pattern playing (after RANDOMIZE) */
{
    uint32_t k;
    for (k = 0; k < PAT_STEPS; k++) {
        brk_step_t s;
        uint8_t *b = pats.p[set.pattern % NPAT].st[k];
        brk_step_get(k, &s);
        b[0] = (uint8_t)(!s.on ? 0u : s.slice >= 0 ? (uint32_t)s.slice + 2u : 1u);
        b[1] = (uint8_t)s.pitch;
        b[2] = (uint8_t)((s.flags & STEP_REVERSE ? 1u : 0u) | ((uint32_t)(s.ratchet ? s.ratchet - 1u : 0u) & 3u) << 1);
    }
}

/* ----------------------------------------------------------------- MATHCORE --- */
/* the scenes (A..P: four sample lanes each, docs/PRD_MATHCORE.md 6.1) in their own store object; the queue and the
 * black keys' shortcuts in another (the queue names scenes: one scene, queued as often as wanted) */
#define MCS_MAGIC 0x3553434Du            /* "MCS5" (more fills, triplet snare rolls, 23 Nov 2026: the presets again) */
#define MQ_MAGIC 0x3151434Du             /* "MCQ1" */
#define MC_QUEUE 64u
#define MC_NOKEY 0xFFu                   /* a black key with no scene */
static struct {
    uint32_t magic;
    mc_scene_t sc[MC_SCENES];
} mcs;
_Static_assert(sizeof mcs <= 3840u, "a store object holds 3840 bytes");
/* the layer's settings as ARP edits them (docs/PRD_LAYER.md 3.3), a loop's and a shot's: the pack's when it came */
typedef struct {
    uint8_t level;                       /* 0..100 */
    int8_t filter;                       /* -50 .. +50 */
    uint8_t quant, wstart, wbars, au;    /* LYQ_*; the window (wbars 0: all); on when PLAY starts */
    uint8_t rsv[2];
} ly_lset_t;
typedef struct {
    uint8_t level, mode, choke, quant;   /* 0..100, SHM_*, 0..4, SHQ_* */
    int8_t pitch;                        /* -24..24 */
    uint8_t decay, cut, rsv;             /* 0..31; the break silent under it */
} ly_sset_t;
typedef struct {
    uint32_t magic;
    uint8_t n;                           /* entries queued */
    uint8_t rsv[3];
    uint8_t q[MC_QUEUE];                 /* each a scene, 0..MC_SCENES-1, played in order and round again */
    uint8_t key[NPFX];                   /* each black key's scene (MC_NOKEY none) */
    uint8_t rsv2[5];
    bank_sset_t ss[BANK_SLOTS];          /* (1.7, PRD_PACKS PS-003, PE-001) each pack break's settings as edited on
                                          * HOME's LOOP: the pack's own (its SSET) when it came, saved with SAVE */
    /* (1.8, docs/PRD_LAYER.md) the layer as ARP edits it, and the pack's effect pads (PADS, D5), the pack's own when
     * it came (ly_from_pack); a 1.7 save loads into the front (MQ_SIZE_17) */
    uint8_t ly_pads;                     /* 1: ARP plays the pack's pads below (while its breaks play), 0: the FM-1's */
    uint8_t ly_rsv[3];
    ly_lset_t lset[BRK_LY_LOOPS];
    ly_sset_t sset[BRK_LY_SHOTS];
    brk_fxslot_t pfx[NPFX];
    brk_fxx_t pfxx[NPFX];
} mq_t;
static mq_t mq;
#define MQ_SIZE_16 88u                   /* the queue as 1.5.5 - 1.6 saved it: it loads into the front */
#define MQ_SIZE_17 offsetof(mq_t, ly_pads)   /* as 1.7 saved it (728 B): the layer from the pack */
_Static_assert(offsetof(mq_t, ly_pads) == 728u, "1.7's queue keeps its size");
static uint8_t ly_init;                  /* the layer's settings not loaded: the pack's (mc_pack) */
_Static_assert(sizeof mq <= 3840u, "a store object holds 3840 bytes");
static uint8_t roles_now[BRK_MAX_SLICES];   /* the Slice Map of the break playing (heard: mc_auto_roles) */

static void mc_lane_defaults(mc_lane_t *l, uint32_t role)
{
    uint8_t *z = (uint8_t *)l;
    uint32_t i;
    for (i = 0; i < sizeof *l; i++)
        z[i] = 0u;
    l->len = 16u;
    l->rate = MCR_16;
    l->prob = 100u;
    l->ratchet = 1u;
    l->mode = MCM_FIXED;
    l->gate = 100u;
    l->level = 100u;
    l->role = (uint8_t)role;
    l->seed = (uint8_t)(role * 37u + 11u);
    {                                    /* VARY: the hats and snares most, the kick a little */
        static const uint8_t V[MC_ROLES] = {20u, 60u, 70u, 45u};
        l->vary = V[role % MC_ROLES];
    }
}
/* the scenes as they come (A..P): grooves, fills, a riser, a breakdown, a drop, of different lengths, ready to play
 * from the black keys. A lane is a string: x a hit, X an accented one, . none (its length the string's). They re-cut
 * the break on its own voice: the grooves run on into the break between their hits (MCS_BREAK), the fills, the riser
 * and the drop are dry chops; L..P are the grooves dry */
/* a scene's name is its index here (mc_scene_t.name, a pack's SCNE): only ever append, never reorder; 17.. since
 * 1.7, an older CHOPPAH shows them blank. At most 14 letters (LIVE's name line) */
static const char *const MC_NAME[] = {"", "ROLLER", "AMEN", "SNARE ROLLER", "SNARES", "MATH 5/7", "FILL",
                                      "TRIPLET ROLL", "SNARE RUSH", "CHOP FILL", "RISER", "DROP", "STEPPER",
                                      "DOUBLE", "7/8 BOUNCE", "BREAKDOWN", "TRIPLET ROLLER", "HEMIOLA", "POLY 3/2",
                                      "POLY 4/3", "POLY 5/4", "POLY 7/4", "POLY 5/3", "POLY 7/8", "AMEN 3/5",
                                      "PRIME MATH", "PHASE DRIFT", "GHOST ROLLS", "SHUFFLE VS 16", "3/4 IN 4/4",
                                      "6/8 STEPPER", "AMEN IN 5S", "HALFTIME 7", "TRIPLET HATS", "JUNGLE",
                                      "TWO STEP", "HALFTIME", "DOUBLE TIME", "SHUFFLE", "GHOST NOTES", "BREAKBEAT",
                                      "BROKEN BEAT", "FOUR TO FLOOR", "MINIMAL", "STUTTER", "GLITCH", "REVERSE",
                                      "CUT UP", "HAT ROLL", "KICK FILL", "SNARE FILL", "BUILD", "INTRO", "OUTRO"};
#define MC_NNAMES (sizeof MC_NAME / sizeof MC_NAME[0])
typedef struct {
    uint8_t bars, flags;
    uint8_t prole;                       /* the PERC lane's role + 1 (0: PERC): a second snare lane for the rolls */
    struct {
        const char *p;                   /* 0: the lane off */
        uint8_t rate, mode, prob, ratchet, level;
        int8_t rise;
        uint8_t fl;                      /* MCL_REV, MCL_SWELL */
    } l[MC_LANES];
} mc_preset_t;
#define L_(p, r, m, pr, ra, lv, ri, f) {p, r, m, pr, ra, lv, ri, f}
#define OFF_ {0, MCR_16, 0, 100, 1, 100, 0, 0}
#define SN_ (ROLE_SNARE + 1u)
#define RUN MCS_BREAK
#define ONCE MCS_ONCE
/* A..K on the black keys: five grooves, four fills, the riser, the drop; L..P more grooves (SCENE puts them on a
 * key). The triplet rolls are a snare lane at 1/8 with ratchet x3: each of its hits three 1/16T snares. A fill keeps
 * its climax in its second half: launched late in a bar (it plays from where the bar is) it still lands */
static const mc_preset_t MC_PRESET[MC_SCENES] = {
    /* A ROLLER */
    {2, RUN, 0, {L_("X.........X.....", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                 L_("....X.......X...", MCR_16, MCM_ALT, 100, 1, 100, 0, 0),
                 L_("X.xxX.x.X.xxX.xx", MCR_16, MCM_ROUND, 100, 1, 80, 0, 0),
                 L_("..............x.", MCR_16, MCM_ROUND, 60, 1, 70, 0, 0)}},
    /* B AMEN */
    {2, RUN, 0, {L_("X.X.......XX....", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                 L_("....X..x.x..X..x", MCR_16, MCM_ACCENT, 100, 1, 100, 0, 0),
                 L_("XxxxXxxxXxxxXxXx", MCR_16, MCM_ROUND, 90, 1, 75, 0, 0), OFF_}},
    /* C SNARE ROLLER: a shuffling roller, a triplet snare roll closing every second bar (and a pickup roll) */
    {2, RUN, SN_, {L_("X.........X.....X.........X.x...", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                   L_("....X.......X...", MCR_16, MCM_ALT, 100, 1, 100, 0, 0),
                   L_("X.xX.xX.xX.xX.xX.xX.xX.x", MCR_16T, MCM_ROUND, 100, 1, 70, 0, 0),
                   L_("...........x..XX", MCR_8, MCM_ROUND, 100, 3, 85, 0, 0)}},
    /* D SNARES: snares everywhere, round all the break's snare hits, a triplet roll every bar's end */
    {2, RUN, SN_, {L_("X.........X.....", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                   L_("..x.X.xx.xx.X.xx", MCR_16, MCM_ROUND, 100, 1, 100, 0, 0),
                   L_("X...x...X...x...", MCR_16, MCM_ROUND, 100, 1, 60, 0, 0),
                   L_("......xX", MCR_8, MCM_ACCENT, 100, 3, 90, 0, 0)}},
    /* E MATH 5/7 */
    {4, RUN, 0, {L_("X..x.", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                 L_("....X..", MCR_16, MCM_ALT, 100, 1, 100, 0, 0),
                 L_("x.xx.x.xx.x", MCR_32, MCM_RANDOM, 100, 1, 65, 0, 0),
                 L_("x..x..x..x.x.", MCR_16, MCM_ROUND, 100, 1, 80, 0, 0)}},
    /* F FILL: two beats of groove, a beat building in triplets, a beat of full triplet snares, climbing */
    {1, ONCE, 0, {L_("X.......X.......", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                  L_("x..x..X..x.xx.xXxxXxXXXX", MCR_16T, MCM_ACCENT, 100, 1, 100, 4, MCL_SWELL),
                  OFF_,
                  L_("........x.x..x.x", MCR_16, MCM_ROUND, 100, 1, 70, 0, 0)}},
    /* G TRIPLET ROLL: a bar of triplet snares, swelling and climbing a fourth */
    {1, ONCE, 0, {L_("X...............", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                  L_("xxxxXXXX", MCR_8, MCM_ACCENT, 100, 3, 100, 5, MCL_SWELL), OFF_, OFF_}},
    /* H SNARE RUSH: one snare an 8th, then 16ths, 32nds, 64ths: the classic roll into the drop */
    {1, ONCE, 0, {L_("X...............", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                  L_("X", MCR_8, MCM_ROUND, 100, 1, 100, 7, 0), OFF_, OFF_}},
    /* I CHOP FILL: 3-3-3-3-2-2 cuts, snares answering with flams */
    {1, ONCE, 0, {L_("X..X..X..X..X.X.", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                  L_("..X..X..X..X.XXX", MCR_16, MCM_ROUND, 100, 2, 100, 0, 0),
                  L_("x.x.x.x.x.x.x.x.x.x.x.x.", MCR_16T, MCM_RANDOM, 60, 1, 60, 0, 0), OFF_}},
    /* J RISER: 4 bars, the snares from 1/8 to 1/64, the hats behind them */
    {4, ONCE, 0, {L_("X...X...X.X.XXXX", MCR_4, MCM_FIXED, 100, 1, 100, 0, 0),
                  L_("X", MCR_8, MCM_ROUND, 100, 1, 100, 12, 0),
                  L_("x", MCR_8, MCM_ROUND, 100, 1, 60, 7, 0), OFF_}},
    /* K DROP: one hit, a slice backwards into the space */
    {1, ONCE, 0, {L_("X...............", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                  L_("X...............", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0), OFF_,
                  L_("........X.......", MCR_16, MCM_FIXED, 100, 1, 100, 0, MCL_REV)}},
    /* L STEPPER (half time) */
    {2, RUN, 0, {L_("X.........X.....X.....X.........", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                 L_("........X...............X.......", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                 L_("X.x.X.xxX.x.X.xx", MCR_16, MCM_ROUND, 100, 1, 75, 0, 0),
                 L_("...x.......x..x.", MCR_16, MCM_RANDOM, 50, 1, 70, 0, 0)}},
    /* M DOUBLE: 3-3-2 kicks, 1/32 hats */
    {2, RUN, 0, {L_("X..X..X.", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                 L_("....X..x....X.x.", MCR_16, MCM_ALT, 100, 1, 100, 0, 0),
                 L_("XxxxXxxXxxXxXxxx", MCR_32, MCM_ROUND, 100, 1, 70, 0, 0),
                 L_("x.....", MCR_16T, MCM_ROUND, 100, 1, 75, 0, 0)}},
    /* N 7/8 BOUNCE */
    {2, RUN, 0, {L_("X..X.X.", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                 L_("...X......X...", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                 L_("x.xx.xx.xx.x", MCR_16T, MCM_RANDOM, 100, 1, 70, 0, 0),
                 L_("x...x.x..", MCR_16, MCM_ROUND, 70, 1, 75, 0, 0)}},
    /* O BREAKDOWN: a sparse pulse, the break running on between */
    {4, RUN, 0, {L_("X...............X...............", MCR_8, MCM_FIXED, 100, 1, 100, 0, 0), OFF_,
                 L_("x..", MCR_8T, MCM_ROUND, 100, 1, 55, 0, 0),
                 L_("x....", MCR_8, MCM_RANDOM, 100, 1, 60, 0, 0)}},
    /* P TRIPLET ROLLER: all in triplets, a triplet snare roll every second bar */
    {2, RUN, SN_, {L_("X.....X..X..X.....X.X...", MCR_8T, MCM_FIXED, 100, 1, 100, 0, 0),
                   L_("...X.....X..", MCR_8T, MCM_ALT, 100, 1, 100, 0, 0),
                   L_("X.xX.xX.xX.xX.xX.xX.xX.x", MCR_16T, MCM_ROUND, 100, 1, 70, 0, 0),
                   L_("..............XX", MCR_8, MCM_ACCENT, 100, 3, 90, 0, 0)}},
};
#undef RUN
#undef ONCE
static void mc_lane_from(mc_lane_t *L, const char *p)   /* a lane's hits and accents from its string */
{
    uint32_t e;
    L->hits = L->accent = 0u;
    for (e = 0; p[e] && e < MC_EVENTS; e++) {
        if (p[e] != '.')
            L->hits |= 1u << e;
        if (p[e] == 'X')
            L->accent |= 1u << e;
    }
    L->len = (uint8_t)(e ? e : 1u);
}
static void mc_scene_defaults(mc_scene_t *s, uint32_t i)
{
    const mc_preset_t *p = &MC_PRESET[i % MC_SCENES];
    uint32_t l;
    s->bars = p->bars;
    s->flags = p->flags;
    s->name = (uint8_t)(i % MC_SCENES + 1u);
    s->rsv = 0u;
    for (l = 0; l < MC_LANES; l++) {
        mc_lane_t *L = &s->lane[l];
        mc_lane_defaults(L, l == 3u && p->prole ? p->prole - 1u : l);
        if (!p->l[l].p)
            continue;
        mc_lane_from(L, p->l[l].p);
        L->flags = (uint8_t)(MCL_ON | p->l[l].fl);
        L->rate = p->l[l].rate;
        L->mode = p->l[l].mode;
        L->prob = p->l[l].prob;
        L->ratchet = p->l[l].ratchet;
        L->level = p->l[l].level;
        L->rise = p->l[l].rise;
        L->seed = (uint8_t)(L->seed + i * 13u);
        if (L->rise || L->ratchet > 2u)
            L->vary /= 3u;               /* (a roll or a riser stays one: a touch of velocity only) */
        else if (p->flags & MCS_ONCE)
            L->vary /= 2u;               /* (a fill is busy already) */
    }
}

static void mc_defaults(void)
{
    uint32_t i;
    mcs.magic = MCS_MAGIC;
    for (i = 0; i < MC_SCENES; i++)
        mc_scene_defaults(&mcs.sc[i], i);
}
static void ss_defaults(bank_sset_t *s)   /* a break as it is: the whole loop, no change */
{
    uint32_t k;
    uint8_t *z = (uint8_t *)s;
    for (k = 0; k < sizeof *s; k++)
        z[k] = 0u;
    s->loop_steps = 32u;
    for (k = 0; k < 32u; k++)
        s->roles[k] = 0xFFu;
}
static void mq_defaults(void)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)&mq;
    for (i = 0; i < sizeof mq; i++)
        z[i] = 0u;
    mq.magic = MQ_MAGIC;
    for (i = 0; i < NPFX; i++)           /* the first eleven scenes (PRD_MATHCORE 7.3) */
        mq.key[i] = (uint8_t)i;
    for (i = 0; i < BANK_SLOTS; i++)
        ss_defaults(&mq.ss[i]);
}
static uint32_t crc32_of(const uint8_t *p, uint32_t n);
static uint32_t mcs_saved, mq_saved;     /* the CRCs of the scenes and the queue as saved: SAVE writes them when changed */
static int mc_save(void)                 /* SAVE: the scenes and the queue, each only if it changed (0: done) */
{
    uint32_t a = crc32_of((const uint8_t *)&mcs, sizeof mcs), b = crc32_of((const uint8_t *)&mq, sizeof mq);
    int rc = 0;
    if (a != mcs_saved) {
        rc |= plat_store_save(OBJ_MC, &mcs, sizeof mcs);
        if (!rc)
            mcs_saved = a;
    }
    if (b != mq_saved) {
        int r2 = plat_store_save(OBJ_MQ, &mq, sizeof mq);
        if (!r2)
            mq_saved = b;
        rc |= r2;
    }
    return rc;
}
static void mc_load(void)                /* boot: the saved scenes and queue, checked (else the defaults) */
{
    uint32_t i;
    if (plat_store_load(OBJ_MC, &mcs, sizeof mcs) != (int)sizeof mcs || mcs.magic != MCS_MAGIC)
        mc_defaults();
    mq_defaults();                       /* (a 1.6 save fills the front: the settings after it stay as they are) */
    {
        int got = plat_store_load(OBJ_MQ, &mq, sizeof mq);
        if ((got != (int)sizeof mq && got != (int)MQ_SIZE_16 && got != (int)MQ_SIZE_17) || mq.magic != MQ_MAGIC ||
            mq.n > MC_QUEUE)
            mq_defaults();
        else if (got == (int)MQ_SIZE_16)
            for (i = 0; i < BANK_SLOTS; i++)
                ss_defaults(&mq.ss[i]);
        ly_init = got != (int)sizeof mq;
    }
    for (i = 0; i < mq.n; i++)           /* (an entry out of range: the first scene) */
        if (mq.q[i] >= MC_SCENES)
            mq.q[i] = 0u;
    for (i = 0; i < NPFX; i++)
        if (mq.key[i] >= MC_SCENES)
            mq.key[i] = MC_NOKEY;
    mcs_saved = crc32_of((const uint8_t *)&mcs, sizeof mcs);   /* (as loaded, or the defaults: nothing to write yet) */
    mq_saved = crc32_of((const uint8_t *)&mq, sizeof mq);
    brk_mc_scenes(mcs.sc);
}


/* ------------------------------------------------------------------- engine --- */
static int16_t pcm_a[CH_PCM_MAX] PLAT_POOL;
#define CH_DUB_LEN 30720u                /* the dub delay's line: 0.70 s a side, 3/8 down to 97 BPM, 1/2 to 172 */
static int16_t dub_line[CH_DUB_LEN] PLAT_POOL;
static uint32_t pfx_mem[BRK_PAD_V * BRK_PAD_ST_BYTES / 4u] PLAT_POOL;   /* the ARP keys' voices (8.6 KB) */
static brk_break_t brk_cur[2];           /* the bank's break playing (pcm_a holds it), and the next one's: the render
                                          * keeps its own while the next is written */
static uint32_t n_breaks;                /* the pack's breaks, then the bank's valid slots */
static int32_t brk_next = -1;            /* a break chosen, waiting for the next bar (break_ask), or -1 */
static uint8_t wave_px[240];             /* the waveform's height per column, for the screen */
#define BLK_N 16u                        /* the slicer view's blocks: one per white key */
#define BLK_W 26u                        /* columns of waveform in a block */
static uint8_t blk_px[BLK_N][BLK_W];     /* each slice's waveform, 0..19 */
static uint8_t blk_n;                    /* blocks with a slice behind them */
static volatile uint8_t muted;
static volatile float meter_brk;         /* the MIXER's channel: the break, before the chain */
static volatile float meter_ov;          /* the overlay's, after its fader */
static volatile float meter_l, meter_r;  /* the output's peaks as it leaves (after MASTER: 1 = full scale), falling
                                          * ~20 dB a second: the render writes, the header's meter reads */
static uint16_t click_at[4];             /* the render: this block's metronome clicks (their samples, the bar's first) */
static uint8_t click_hi[4];
static uint32_t nclick;
static float click_env, click_ph, click_inc;

static void wave_build(const brk_break_t *b)
{
    uint32_t x, i;
    for (x = 0; x < 240u; x++) {
        uint32_t a = b->len * x / 240u, z = b->len * (x + 1u) / 240u, pk = 0u;
        for (i = a; i < z; i++) {
            int32_t s = b->pcm[i] < 0 ? -b->pcm[i] : b->pcm[i];
            if ((uint32_t)s > pk)
                pk = (uint32_t)s;
        }
        wave_px[x] = (uint8_t)(pk * 24u / 32768u);
    }
    blk_n = (uint8_t)(b->nslices < BLK_N ? b->nslices : BLK_N);
    for (x = 0; x < BLK_N; x++) {
        uint32_t a = x < blk_n ? b->slice[x] : 0u, z = x < blk_n ? b->slice[x + 1] : 0u, c;
        for (c = 0; c < BLK_W; c++) {
            uint32_t i0 = a + (z - a) * c / BLK_W, i1 = a + (z - a) * (c + 1u) / BLK_W, pk = 0u;
            for (i = i0; i < i1; i++) {
                int32_t v = b->pcm[i] < 0 ? -b->pcm[i] : b->pcm[i];
                if ((uint32_t)v > pk)
                    pk = (uint32_t)v;
            }
            blk_px[x][c] = (uint8_t)(pk * 19u / 32768u);
        }
    }
}

/* a bank's slot i (0..), or 0 when absent or not sane (size: the bytes the bank may span) */
static const bank_slot_t *bank_check(const uint8_t *bank, uint32_t size, uint32_t i)
{
    const bank_header_t *h = (const bank_header_t *)bank;
    const bank_slot_t *s;
    if (!bank || h->magic != BANK_MAGIC || (h->version != BANK_VERSION && h->version != BANK_VERSION2) ||
        i >= h->nslots || i >= BANK_SLOTS ||
        16u + 168u * (i + 1u) > size)
        return 0;
    s = (const bank_slot_t *)(bank + 16u + 168u * i);
    if (s->data_off + s->data_len > size || s->len > CH_PCM_MAX || s->data_len < (s->len + 1u) / 2u ||
        !s->nslices || s->nslices > BANK_SLICES || !s->bars || s->slice[s->nslices] != s->len || !s->bpm_x100)
        return 0;
    return s;
}

/* the pack (PRD S-003, v1.6; the custom break of 1.4-1.5 was a pack of one): breaks of the player's in two banks of the built-in
 * bank's format, written over USB from the site's break manager (app_sysex, ui/main.c). Bank 1 is the data area below
 * the store objects (0xD6000 on), where the custom break lived (a 1.4-1.5 custom break reads as a pack of one); bank 2
 * is PLAT_XDATA1, where the update loader looks for a record at bytes 0xF00..0xF07 of each sector: "AT" never stands at
 * 0xF06 there (app_sysex refuses a write that would put it there; the page nudges such a byte). The pack's breaks come
 * first, bank 1's then bank 2's, then the built-in ones; the first is the break at power-on */
#define PK_BANKS 2u
static const uint32_t PK_BASE[PK_BANKS] = {0x97000u, PLAT_XDATA1_LO};
static const uint32_t PK_SECT[PK_BANKS] = {63u, 23u};      /* 252 KB, 92 KB */
#define PK_CAP(k) (PK_SECT[k] * 4096u)
static uint32_t pk_n[PK_BANKS];          /* each bank's breaks (checked whole: pk_scan) */
static uint32_t n_user;                  /* the pack's breaks, both banks: breaks 0 .. n_user - 1 */

static uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t crc32_of(const uint8_t *p, uint32_t n)   /* zlib's */
{
    uint32_t c = ~0u, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
/* the pack's v2 chunks (PRD_PACKS 3.2): its name and id, each slot's settings, its scenes, its theme, its overlay; 0
 * when the pack has none (a v1 pack: the factory's) */
static const bank_pack_t *pk_pack;
static const bank_sset_t *pk_sset;
static uint32_t pk_nsset;
static const uint8_t *pk_scne, *pk_them, *pk_chan;
static const uint8_t *pk_gruv, *pk_evnt;      /* the groove-event layer (PRD_MATHCORE_GROOVE.md) */
static const bank_ovly_t *pk_ovly;
static const uint8_t *pk_loop, *pk_shot, *pk_pads;   /* the layer (1.8): LOOP, SHOT, PADS; pk_layer: their banks */
static uint32_t pk_loop_len, pk_shot_len, pk_layer;
static uint32_t pk_scne_len, pk_tag;     /* pk_tag: the pack's id, hashed (0: no pack of v2) */
static uint32_t pk_gruv_len, pk_evnt_len;
static void pk_chunks(const uint8_t *p, uint32_t size)
{
    const bank_header_t *h = (const bank_header_t *)p;
    uint32_t at = 16u + 168u * h->nslots, n, i;
    if (h->version != BANK_VERSION2 || at + 4u > size)
        return;
    n = rd32le(p + at);
    at += 4u;
    for (i = 0; i < n && i < 16u && at + 8u <= size; i++) {
        uint32_t tag = rd32le(p + at), len = rd32le(p + at + 4u);
        const uint8_t *d = p + at + 8u;
        if (len > size - at - 8u)
            return;
        if (tag == CHK_PACK && len >= sizeof(bank_pack_t))
            pk_pack = (const bank_pack_t *)d;
        else if (tag == CHK_SSET) {
            pk_sset = (const bank_sset_t *)d;
            pk_nsset = len / sizeof(bank_sset_t);
        } else if (tag == CHK_SCNE) {
            pk_scne = d;
            pk_scne_len = len;
        } else if (tag == CHK_GRUV) {
            pk_gruv = d;
            pk_gruv_len = len;
        } else if (tag == CHK_EVNT) {
            pk_evnt = d;
            pk_evnt_len = len;
        } else if (tag == CHK_THEM && len >= 16u)
            pk_them = d;
        else if (tag == CHK_CHAN && len >= 80u)
            pk_chan = d;
        else if (tag == CHK_OVLY && len >= sizeof(bank_ovly_t) &&
                 len - sizeof(bank_ovly_t) >= (((const bank_ovly_t *)d)->len + 1u) / 2u)
            pk_ovly = (const bank_ovly_t *)d;
        else if (tag == CHK_LOOP && len >= 4u && !pk_loop) {
            pk_loop = d;
            pk_loop_len = len;
        } else if (tag == CHK_SHOT && len >= 4u && !pk_shot) {
            pk_shot = d;
            pk_shot_len = len;
        } else if (tag == CHK_PADS && len >= NPFX * (sizeof(brk_fxslot_t) + sizeof(brk_fxx_t)))
            pk_pads = d;
        if (tag == CHK_OVLY || tag == CHK_LOOP || tag == CHK_SHOT)
            pk_layer |= 1u << (p == plat_data(PK_BASE[1]));
        at += 8u + len + (-len & 3u);
    }
}
static void pk_scan(void)
{
    uint32_t k, i;
    n_user = 0u;
    pk_pack = 0; pk_sset = 0; pk_scne = 0; pk_them = 0; pk_ovly = 0; pk_chan = 0; pk_gruv = 0; pk_evnt = 0;
    pk_loop = pk_shot = pk_pads = 0;
    pk_nsset = pk_scne_len = pk_tag = pk_loop_len = pk_shot_len = pk_layer = 0u;
    pk_gruv_len = pk_evnt_len = 0u;
    for (k = 0; k < PK_BANKS; k++) {
        const uint8_t *p = plat_data(PK_BASE[k]);
        const bank_header_t *h = (const bank_header_t *)p;
        pk_n[k] = 0u;
        if (!p || h->magic != BANK_MAGIC || (!h->nslots && h->version != BANK_VERSION2) || h->nslots > BANK_SLOTS ||
            h->data_size > PK_CAP(k) - 16u || crc32_of(p + 16, h->data_size) != h->crc32)
            continue;
        for (i = 0; i < h->nslots && bank_check(p, 16u + h->data_size, i); i++)
            ;
        if (i != h->nslots)
            continue;
        pk_n[k] = i;
        n_user += pk_n[k];
        pk_chunks(p, 16u + h->data_size);
        if (!pk_tag && pk_n[k])          /* a pack without an id (v1): its bank's CRC tells it from another */
            pk_tag = h->crc32 | 1u;
    }
    if (pk_pack)
        pk_tag = crc32_of(pk_pack->id, 16u) | 1u;
}

/* a slot's settings (PRD_PACKS 4): the pack's break i's, or 0 (a v1 pack, a built-in break) */
static const bank_sset_t *sset_of(uint32_t i)
{
    return i < n_user && i < BANK_SLOTS ? &mq.ss[i] : 0;   /* (mc_pack put the pack's there) */
}
static void ss_from_pack(uint32_t i)    /* break i's settings as its pack has them (LOOP's SEL + a knob: back to them) */
{
    uint32_t k;
    if (i >= BANK_SLOTS)
        return;
    if (i < pk_nsset)
        for (k = 0; k < sizeof(bank_sset_t); k++)
            ((uint8_t *)&mq.ss[i])[k] = ((const uint8_t *)&pk_sset[i])[k];
    else
        ss_defaults(&mq.ss[i]);
}
static uint8_t tile_src[BRK_MAX_SLICES + 1];   /* the window's slice i came from the break's slice tile_src[i] */

/* the loop's window (PRD_PACKS PS-002): steps 16ths from step start, repeated over the break's two bars, so the loop,
 * the generator, MATHCORE and SEQ all play the window without knowing it */
static void slot_window(brk_break_t *d, int16_t *pcm, uint32_t steps, uint32_t start)
{
    uint32_t len = d->len, wl, ws, k, c, m = 0u, n = 0u, at;
    uint32_t rel[BRK_MAX_SLICES + 1];
    uint8_t src[BRK_MAX_SLICES + 1];
    for (k = 0; k <= BRK_MAX_SLICES; k++)
        tile_src[k] = (uint8_t)k;
    if (!steps || steps >= 32u || 32u % steps)
        return;
    if (start + steps > 32u)
        start = 32u - steps;
    wl = len * steps / 32u;
    ws = len * start / 32u;
    if (wl < 64u)
        return;
    for (k = 0; k < d->nslices && m < BRK_MAX_SLICES; k++) {   /* the slices in the window (and the one it starts in) */
        uint32_t a = d->slice[k], b = d->slice[k + 1u];
        if (b <= ws || a >= ws + wl)
            continue;
        rel[m] = a > ws ? a - ws : 0u;
        src[m++] = (uint8_t)k;
    }
    for (k = 0; k < wl; k++)             /* the window to the front, then again to the end */
        pcm[k] = pcm[ws + k];
    for (at = wl; at < len; at++)
        pcm[at] = pcm[at - wl];
    for (c = 0; c < 32u / steps; c++)
        for (k = 0; k < m && n < BRK_MAX_SLICES; k++) {
            d->slice[n] = c * wl + rel[k];
            tile_src[n++] = src[k];
        }
    d->nslices = n ? n : 1u;
    d->slice[d->nslices] = len;
}

/* a pack break's settings, on the break just decoded (not a split) */
static void slot_apply(brk_break_t *d, int16_t *pcm, uint32_t i)
{
    const bank_sset_t *ss = sset_of(i);
    uint32_t k;
    slot_pitch = 0;
    slot_swing = 0u;
    for (k = 0; k <= BRK_MAX_SLICES; k++)
        tile_src[k] = (uint8_t)k;
    if (!ss)
        return;
    if (ss->gain_db) {                   /* the level, into the samples */
        int32_t g = (int32_t)(brk_exp2f_c((float)ss->gain_db * 0.16609640f) * 4096.0f);
        for (k = 0; k < d->len; k++) {
            int32_t v = (pcm[k] * g) >> 12;
            pcm[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
    if (ss->bpm_x100 >= 4000u)
        d->bpm_x100 = ss->bpm_x100;
    slot_window(d, pcm, ss->loop_steps, ss->loop_start);
    slot_pitch = ss->pitch < -24 ? -24 : ss->pitch > 24 ? 24 : ss->pitch;
    slot_swing = ss->swing >= 50u && ss->swing <= 75u ? ss->swing : 0u;
}

/* the pack's scenes (PRD_PACKS PM-001, PM-002): a pack of v2 brings its own (or the factory's); the scenes and chain
 * saved are the pack's they were made with (its id hashed into mq.rsv), so another pack never inherits them */
static int mc_pack_core(void)            /* 1: the pack's own came in (another pack, or the first) */
{
    uint32_t tag = pk_tag & 0xFFFFFFu, had = mq.rsv[0] | mq.rsv[1] << 8 | (uint32_t)mq.rsv[2] << 16, i;
    if (tag == had)
        return 0;
    if (!had && !pk_pack && !pk_scne) {  /* saved before 1.7 with a pack of v1: those scenes are this pack's */
        for (i = 0; i < BANK_SLOTS; i++)
            ss_from_pack(i);
        mq.rsv[0] = (uint8_t)tag;
        mq.rsv[1] = (uint8_t)(tag >> 8);
        mq.rsv[2] = (uint8_t)(tag >> 16);
        return 1;
    }
    if (pk_scne && pk_scne_len == sizeof mcs && rd32le(pk_scne) == MCS_MAGIC) {
        uint8_t *z = (uint8_t *)&mcs;
        for (i = 0; i < sizeof mcs; i++)
            z[i] = pk_scne[i];
    } else
        mc_defaults();
    mq_defaults();
    if (pk_chan) {                       /* the pack's chain and black keys (CHAN), each checked */
        mq.n = (uint8_t)(pk_chan[0] <= MC_QUEUE ? pk_chan[0] : MC_QUEUE);
        for (i = 0; i < mq.n; i++)
            mq.q[i] = (uint8_t)(pk_chan[4u + i] < MC_SCENES ? pk_chan[4u + i] : 0u);
        for (i = 0; i < NPFX; i++)
            mq.key[i] = (uint8_t)(pk_chan[68u + i] < MC_SCENES ? pk_chan[68u + i] : MC_NOKEY);
    }
    for (i = 0; i < BANK_SLOTS; i++)
        ss_from_pack(i);
    mq.rsv[0] = (uint8_t)tag;
    mq.rsv[1] = (uint8_t)(tag >> 8);
    mq.rsv[2] = (uint8_t)(tag >> 16);
    brk_mc_scenes(mcs.sc);
    brk_mc_groove(pk_gruv, pk_gruv_len, pk_evnt, pk_evnt_len);   /* the groove beside the scenes */
    return 1;
}

/* ------------------------------------------------------------------ the layer --- */
/* docs/PRD_LAYER.md: the pack's loops (L1..L4: LOOP's, or 1.7's OVLY as L1) and one-shots (S1..S16, SHOT's), from the
 * flash; their settings in mq (ly_lset_t, ly_sset_t), the pack's when it came; ARP plays them (ui/arp.c) */
static struct { const uint8_t *d; uint32_t len, bars; char name[9]; } lyL[BRK_LY_LOOPS];
static struct { const uint8_t *d; uint32_t len, rate, bpm; char name[9]; } lyS[BRK_LY_SHOTS];
static uint32_t ly_nl, ly_ns;            /* loops, shots: the keys up to them have one */
static const bank_loop_t *ly_lrec(uint32_t i)
{
    return pk_loop && i < rd32le(pk_loop) && 4u + 32u * (i + 1u) <= pk_loop_len ?
           (const bank_loop_t *)(pk_loop + 4u + 32u * i) : 0;
}
static const bank_shot_t *ly_srec(uint32_t i)
{
    return pk_shot && i < rd32le(pk_shot) && 4u + 32u * (i + 1u) <= pk_shot_len ?
           (const bank_shot_t *)(pk_shot + 4u + 32u * i) : 0;
}
static void ly_name(char *t, const char *n)
{
    uint32_t k;
    for (k = 0; k < 8u && n[k] >= ' ' && n[k] < 127; k++)
        t[k] = n[k];
    t[k] = 0;
}
BRK_COLD static void ly_scan(void)       /* the sources, checked (after pk_scan) */
{
    uint32_t i;
    ly_nl = ly_ns = 0u;
    for (i = 0; i < BRK_LY_LOOPS; i++) {
        const bank_loop_t *r = ly_lrec(i);
        lyL[i].d = 0;
        lyL[i].name[0] = 0;
        if (r && r->flags & LPF_OVLY && pk_ovly) {   /* its ADPCM: OVLY's */
            lyL[i].d = (const uint8_t *)(pk_ovly + 1);
            lyL[i].len = pk_ovly->len;
        } else if (r && r->data_off < pk_loop_len && (r->len + 1u) / 2u <= pk_loop_len - r->data_off) {
            lyL[i].d = pk_loop + r->data_off;
            lyL[i].len = r->len;
        } else if (!pk_loop && !i && pk_ovly) {   /* 1.7's overlay: L1 */
            lyL[i].d = (const uint8_t *)(pk_ovly + 1);
            lyL[i].len = pk_ovly->len;
            lyL[i].bars = pk_ovly->bars;
            ly_name(lyL[i].name, "OVERLAY");
        }
        if (r && lyL[i].d) {
            lyL[i].bars = r->bars;
            ly_name(lyL[i].name, r->name);
        }
        if (lyL[i].d && lyL[i].bars && lyL[i].len >= 64u)
            ly_nl = i + 1u;
        else
            lyL[i].d = 0;
    }
    for (i = 0; i < BRK_LY_SHOTS; i++) {
        const bank_shot_t *r = ly_srec(i);
        lyS[i].d = 0;
        lyS[i].name[0] = 0;
        if (r && r->len >= 2u && r->data_off < pk_shot_len && (r->len + 1u) / 2u <= pk_shot_len - r->data_off) {
            lyS[i].d = pk_shot + r->data_off;
            lyS[i].len = r->len;
            lyS[i].rate = r->rate;
            lyS[i].bpm = r->bpm_x100;
            ly_name(lyS[i].name, r->name);
            ly_ns = i + 1u;
        }
    }
}
BRK_COLD static void ly_from_pack(void)  /* the layer's settings and the effect pads as the pack has them */
{
    uint32_t i, k;
    for (i = 0; i < BRK_LY_LOOPS; i++) {
        const bank_loop_t *r = ly_lrec(i);
        ly_lset_t *l = &mq.lset[i];
        l->level = r ? (r->level > 100u ? 100u : r->level) : 100u;
        l->filter = r ? (int8_t)(r->filter < -50 ? -50 : r->filter > 50 ? 50 : r->filter) : 0;
        l->quant = r ? (uint8_t)(r->quant % LYQ_N) : LYQ_BAR;
        l->wstart = r ? r->wstart : 0u;
        l->wbars = r ? r->wbars : 0u;
        l->au = r ? (uint8_t)(r->flags & LPF_AUTO) : (uint8_t)(!pk_loop && !i && pk_ovly);   /* (1.7's: always on) */
        l->rsv[0] = l->rsv[1] = 0u;
    }
    for (i = 0; i < BRK_LY_SHOTS; i++) {
        const bank_shot_t *r = ly_srec(i);
        ly_sset_t *t = &mq.sset[i];
        t->level = r ? (r->level > 100u ? 100u : r->level) : 100u;
        t->mode = r ? (uint8_t)(r->mode % SHM_N) : SHM_ONE;
        t->choke = r ? (uint8_t)(r->choke > 4u ? 0u : r->choke) : 0u;
        t->quant = r ? (uint8_t)(r->quant % SHQ_N) : SHQ_NOW;
        t->pitch = r ? (int8_t)(r->pitch < -24 ? -24 : r->pitch > 24 ? 24 : r->pitch) : 0;
        t->decay = r ? (uint8_t)(r->decay & 31u) : 0u;
        t->cut = r ? (uint8_t)(r->flags & SHF_CUT) : 0u;
        t->rsv = 0u;
    }
    mq.ly_pads = pk_pads != 0;
    for (i = 0; pk_pads && i < NPFX; i++) {   /* PADS: each pad's slot, then its extras (an effect it can't be: empty) */
        const uint8_t *q = pk_pads + i * (sizeof(brk_fxslot_t) + sizeof(brk_fxx_t));
        for (k = 0; k < sizeof(brk_fxslot_t); k++)
            ((uint8_t *)&mq.pfx[i])[k] = q[k];
        for (k = 0; k < sizeof(brk_fxx_t); k++)
            ((uint8_t *)&mq.pfxx[i])[k] = q[sizeof(brk_fxslot_t) + k];
        if (mq.pfx[i].type >= BRK_NFXT || mq.pfx[i].type == FXT_MBCOMP)
            mq.pfx[i].type = FXT_EMPTY;
        mq.pfx[i].rsv[0] = 0u;           /* (TARGET: the whole mix, until LY-035) */
    }
}
static uint32_t ly_lcfg(uint32_t i)
{
    const ly_lset_t *l = &mq.lset[i % BRK_LY_LOOPS];
    uint32_t wb = l->wbars ? l->wbars : 16u;
    return BRK_LY_LCFG(l->level, l->filter, l->wstart & 15u, wb > 16u ? 16u : wb, l->quant & 3u, l->au & 1u);
}
static uint32_t ly_scfg(uint32_t i)
{
    const ly_sset_t *t = &mq.sset[i % BRK_LY_SHOTS];
    return BRK_LY_SCFG(t->level, t->pitch, t->decay & 31u, t->mode % SHM_N, t->choke & 7u, t->quant % SHQ_N,
                       t->cut & 1u);
}
BRK_COLD static void ly_apply(void)      /* the layer to the engine: the sources, their settings, the voices */
{
    uint32_t i;
    for (i = 0; i < BRK_LY_LOOPS; i++) {
        brk_ly_loop_cfg(i, ly_lcfg(i));
        brk_ly_loop(i, lyL[i].d, lyL[i].len, lyL[i].bars);
    }
    for (i = 0; i < BRK_LY_SHOTS; i++) {
        brk_ly_shot_cfg(i, ly_scfg(i));
        brk_ly_shot(i, lyS[i].d, lyS[i].len, lyS[i].rate, lyS[i].bpm);
    }
    brk_ly_voices(set.ly_lv ? set.ly_lv : 1u, set.ly_sv ? set.ly_sv : 3u);
}
static void arp_pads_changed(void);      /* (ui/arp.c: the pads' table changed: the held and latched ones let go) */
/* the ARP page's effect pads (D5): the pack's while its breaks play, when it brought its own; else the FM-1's */
static int pfx_pack(void) { return mq.ly_pads && set.brk < n_user; }
static brk_fxslot_t *pfx_at(uint32_t i) { return pfx_pack() ? &mq.pfx[i % NPFX] : &set.pfx[i % NPFX]; }
static brk_fxx_t *pfxx_at(uint32_t i) { return pfx_pack() ? &mq.pfxx[i % NPFX] : &set.pfxx[i % NPFX]; }
static void mc_pack(void)                /* the pack's scenes, chain, loop settings, layer and pads */
{
    uint8_t sig = (uint8_t)(1u | (pk_loop != 0) << 1 | (pk_shot != 0) << 2 | (pk_pads != 0) << 3 | (pk_ovly != 0) << 4);
    int core = mc_pack_core(), boot = ly_init;
    if (core || boot || mq.ly_rsv[0] != sig)   /* (a pack's banks come one by one: its layer as it lands) */
        ly_from_pack();
    mq.ly_rsv[0] = sig;
    if (boot && !core)                   /* (a save without the layer: the pack's, nothing new to write) */
        mq_saved = crc32_of((const uint8_t *)&mq, sizeof mq);
    ly_init = 0u;
    ly_apply();
    arp_pads_changed();
}

static void theme_apply(uint32_t pack)
{
    const uint8_t *t = pack ? pk_them : 0;
    uint32_t k;
    uint16_t c[8];
    if ((t != 0) == (th_pack != 0) && !t)
        return;
    if (t) {
        for (k = 0; k < 8u; k++)
            c[k] = (uint16_t)(t[2u * k] | t[2u * k + 1u] << 8);   /* bg panel text dim hi mid wave warn */
        th_bg = c[0];
        pal[0] = c[1];                   /* rules: the panel */
        pal[1] = c[3];                   /* inactive */
        pal[2] = c[2];                   /* labels: the text */
        pal[3] = c[5];                   /* secondary: mid */
        pal[4] = c[4];                   /* values, curves: hi */
        th_block = c[1];
        th_block_hi = c[6];
    } else {
        palette_set(APP_PALETTE);
        th_bg = 0u;
        th_block = RGB(24, 24, 30);
        th_block_hi = RGB(255, 184, 70);
    }
    th_pack = t != 0;
    th_dirty = 1u;
}

/* the Slice Map as the pack says (PS-004): a role set by hand wins over the one heard */
static void slot_roles(uint32_t i, const brk_break_t *b)
{
    const bank_sset_t *ss = sset_of(i);
    uint32_t k;
    if (!ss || !b)
        return;
    for (k = 0; k < b->nslices && k < BRK_MAX_SLICES; k++) {
        uint8_t r = ss->roles[tile_src[k] & 31u];
        if (r != 0xFFu)
            roles_now[k] = r & 15u;
    }
}

/* break i's slot and its bank: the pack first, then the bank built into the image */
static const bank_slot_t *slot_of(uint32_t i, const uint8_t **bank)
{
    uint32_t k;
    *bank = 0;
    for (k = 0; k < PK_BANKS; k++) {
        if (i < pk_n[k]) {
            *bank = plat_data(PK_BASE[k]);
            return bank_check(*bank, PK_CAP(k), i);
        }
        i -= pk_n[k];
    }
#if CH_HAVE_BANK
    *bank = CH_BANK;
    return bank_check(CH_BANK, CH_BANK_SIZE, i);
#else
    return 0;
#endif
}

/* the breaks: the pack's, the bank's (no bank, no breaks: silence, nothing synthesised in their place) */
static void breaks_count(void)
{
    const uint8_t *bk;
    pk_scan();
    ly_scan();                           /* the pack's loops and shots (to the engine: mc_pack's ly_apply) */
    n_breaks = 0u;
    while (n_breaks < BANK_SLOTS && slot_of(n_user + n_breaks, &bk))
        n_breaks++;
    n_breaks += n_user;
}

/* break i's name, for the screen before it is in */
static const char *break_name(uint32_t i)
{
    static char t[13];
    const uint8_t *bk;
    const bank_slot_t *s = slot_of(i, &bk);
    uint32_t k;
    for (k = 0; k < 12u; k++)
        t[k] = s ? s->name[k] : 0;
    t[12] = 0;
    return s ? t : "NO BREAK";
}

/* the split (PRD S-030): break B's number */
static uint32_t split_b_of(uint32_t a)
{
    uint32_t b = set.split_b ? set.split_b - 1u : a + 1u;
    return b < n_breaks ? b : n_user < n_breaks ? n_user : 0u;
}

/* bar `bar` of a slot (its first n samples, n = a bar) decoded into dst; the bars before it are decoded over dst and
 * forgotten (IMA ADPCM only reads forwards). from: where the bar begins in the slot */
static uint32_t bar_decode(const uint8_t *bk, const bank_slot_t *s, uint32_t bar, int16_t *dst, uint32_t n,
                           uint32_t *from)
{
    adpcm_state_t st = {0, 0, 0};
    const uint8_t *src = bk + s->data_off;
    uint32_t bars = s->bars ? s->bars : 1u, skip;
    if (bar >= bars)
        bar = bars - 1u;
    skip = *from = (s->len / bars * bar) & ~1u;   /* (even: whole bytes) */
    while (skip) {
        uint32_t k = skip < (n & ~1u) ? skip : n & ~1u;
        adpcm_decode(&st, src, dst, k);
        src += k / 2u;
        skip -= k;
    }
    adpcm_decode(&st, src, dst, n);
    return n;
}

/* the split's half of a slot: its slices in the bar, re-based to `base` and scaled from the bar's n samples to m,
 * at most 16; into d->slice from k. Returns the count */
static uint32_t split_slices(const bank_slot_t *s, uint32_t bar, uint32_t from, uint32_t n, uint32_t m,
                             uint32_t base, brk_break_t *d, uint32_t k)
{
    uint32_t bars = s->bars ? s->bars : 1u, j0, j1, st, j, c = 0u;
    if (bar >= bars)
        bar = bars - 1u;
    j0 = s->nslices * bar / bars;
    j1 = s->nslices * (bar + 1u) / bars;
    st = (j1 - j0 + 15u) / 16u;          /* (a 32-slice bar: every other marker) */
    for (j = j0; j < j1; j += st) {
        uint32_t x = c && s->slice[j] > from ? (s->slice[j] - from) * m / n : 0u;   /* (< 2^32: two bars' samples) */
        x += base;
        if (c && x <= d->slice[k + c - 1u])
            continue;                    /* (markers that met when scaled) */
        if (x >= base + m)
            break;
        d->slice[k + c++] = x;
    }
    return c;
}

/* the split: bar 1 of break a (as it is), then bar 2 of break b repitched to a's bar's length (it plays in a's
 * tempo), in pcm_a. b's bar is decoded to pcm_a's end, then read forwards into place behind a's: the reads stay ahead
 * of the writes (la + lb + 4 <= CH_PCM_MAX). 0: one isn't in the bank */
static const brk_break_t *split_load(uint32_t a, uint32_t b, brk_break_t *d)
{
    const uint8_t *ka, *kb;
    const bank_slot_t *sa = slot_of(a, &ka), *sb = slot_of(b, &kb);
    uint32_t la, lb, fa, fb, o, k, na, nb;
    int16_t *raw;
    float r, pos;
    if (!sa || !sb)
        return 0;
    la = sa->len / (sa->bars ? sa->bars : 1u);
    lb = sb->len / (sb->bars ? sb->bars : 1u);
    if (la > (CH_PCM_MAX - 8u) / 2u)
        la = (CH_PCM_MAX - 8u) / 2u;     /* (a few samples off the slowest: nothing to hear, the steps re-cut it) */
    if (la + lb + 6u + lb / la > CH_PCM_MAX)
        lb = CH_PCM_MAX - 6u - la - 2u;
    if (la < 64u || lb < 64u || lb > 7u * la)
        return 0;                        /* (r < 8: the reads stay ahead of the writes to the end) */
    bar_decode(ka, sa, set.split_bars & 1u, pcm_a, la, &fa);
    raw = pcm_a + CH_PCM_MAX - lb;
    bar_decode(kb, sb, (set.split_bars >> 1) & 1u, raw, lb, &fb);
    r = (float)lb / (float)la;
    for (o = 0, pos = 0.0f; o < la; o++, pos = (float)o * r) {   /* 4-point Hermite, as the voices read */
        int32_t i = (int32_t)pos, y;
        float t = pos - (float)i, xm = (float)raw[i > 0 ? i - 1 : 0], x0 = (float)raw[i];
        float x1 = (float)raw[(uint32_t)i + 1u < lb ? i + 1 : i], x2 = (float)raw[(uint32_t)i + 2u < lb ? i + 2 : i];
        float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        y = (int32_t)(((c3 * t + c2) * t + c1) * t + x0);
        pcm_a[la + o] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
    }
    na = split_slices(sa, set.split_bars & 1u, fa, la, la, 0u, d, 0u);
    nb = split_slices(sb, (set.split_bars >> 1) & 1u, fb, lb, la, la, d, na);
    d->pcm = pcm_a;
    d->len = 2u * la;
    d->rate = sa->rate;
    d->bpm_x100 = sa->bpm_x100;          /* (two bars of a's: its tempo) */
    d->bars = 2u;
    d->nslices = na + nb;
    d->slice[na + nb] = 2u * la;
    d->split = na;
    for (k = o = 0; o < 5u && sa->name[o] && sa->name[o] != ' '; o++)   /* "AMEN+THINK" */
        d->name[k++] = sa->name[o];
    d->name[k++] = '+';
    for (o = 0; k < 12u && o < 12u && sb->name[o] && sb->name[o] != ' '; o++)
        d->name[k++] = sb->name[o];
    d->name[k] = 0;
    return d;
}

/* break i into pcm_a and the descriptor the render isn't using (whatever plays from pcm_a meanwhile is muted, see
 * break_poll); hand it to the render after. With the split on, i is break A and the break is the two (split_load) */
static const brk_break_t *break_load(uint32_t i)
{
    const uint8_t *bk;
    const bank_slot_t *s = slot_of(i, &bk);
    brk_break_t *d = brk_get_break() == &brk_cur[0] ? &brk_cur[1] : &brk_cur[0];
    const brk_break_t *b = 0;
    uint32_t k;
    if (s && set.split && split_load(i, split_b_of(i), d)) {
        set.brk = (uint8_t)i;
        wave_build(d);
        mc_auto_roles(d, roles_now);     /* its Slice Map, heard */
        brk_roles(roles_now, BRK_MAX_SLICES);
        return d;
    }
    if (s) {
        adpcm_state_t st = {0, 0, 0};
        adpcm_decode(&st, bk + s->data_off, pcm_a, s->len);
        d->pcm = pcm_a;
        d->len = s->len;
        d->rate = s->rate;
        d->bpm_x100 = s->bpm_x100;
        d->bars = s->bars;
        d->nslices = s->nslices;
        for (k = 0; k <= s->nslices; k++)
            d->slice[k] = s->slice[k];
        for (k = 0; k < 12u; k++)
            d->name[k] = s->name[k];
        d->name[12] = 0;
        d->split = 0u;
        slot_apply(d, pcm_a, i);         /* a pack break's own loop, level, tempo, pitch and swing */
        b = d;
    }
    set.brk = (uint8_t)i;
    if (b)
        wave_build(b);
    mc_auto_roles(b, roles_now);         /* the Slice Map: the kicks, snares, hats and the rest, heard */
    slot_roles(i, b);
    theme_apply(i < n_user);             /* the pack's colours while its breaks play (Q1: a built-in break, amber) */
    {                                    /* the pack's pads, or the FM-1's: on the way from one to the other, let go */
        static int pw = -1;
        if (pfx_pack() != pw) {
            pw = pfx_pack();
            arp_pads_changed();
        }
    }
    brk_overlay_on(i < n_user);          /* its overlay under its breaks only */
    brk_roles(roles_now, BRK_MAX_SLICES);
    brk_pitch((float)(set.pitch + slot_pitch));
    brk_swing((float)(slot_swing ? slot_swing : set.swing) * 0.01f);
    return b;
}

void app_init(void)
{
    brk_init();
    brk_fx_init();
    brk_fx_delay_buffer(dub_line, CH_DUB_LEN);
    brk_fx_pad_buffer(pfx_mem);
    breaks_count();                      /* the pack, the bank's (breaks/breaks.txt) */
    settings_defaults();                 /* (what a shorter, older save doesn't reach keeps these) */
    {
        int sz = plat_store_size(OBJ_SETTINGS);
        if (sz < (int)SET_SIZE_MIN || sz > (int)sizeof set || plat_store_load(OBJ_SETTINGS, &set, (uint32_t)sz) != sz)
            set.magic = 0u;
    }
    if (set.magic != SET_MAGIC || set.tempo_x10 < 400u || set.tempo_x10 > 3000u || set.swing < 50u ||
        set.swing > 75u) {
        settings_defaults();         /* (the bank's first break) */
    }
    if (set.brk >= n_breaks || n_user)   /* a pack: its first break, always the one at power-on */
        set.brk = 0u;
    if (plat_store_load(OBJ_PAT, &pats, sizeof pats) != (int)sizeof pats || pats.magic != PAT_MAGIC)
        pat_defaults();
    mc_load();
    mc_pack();
    brk_set_break(break_load(set.brk));  /* boot: the audio isn't running yet */
    settings_apply();
    pat_apply();
}

/* ------------------------------------------------------- MIDI clock, Bluetooth --- */
#define PKT_RT(b) (0x0Fu | (uint32_t)(b) << 8)   /* a real-time byte as a USB-MIDI packet (CIN 0xF) */
#define MIDI_SLICE0 48u                  /* C3: slice 1 */
#define MIDI_PAD0 64u                    /* E4: black key 1 (to D5: black key 11) */
#define MIDI_BRK0 24u                    /* C1: break 1 (to B1: break 12) */
#define CC_K1 20u                        /* CC 20..23: K1..K4 turned (by as much as the value moves) */
#define CC_FX0 24u                       /* CC 24..39: the chain's slots 1..4, each SET 1, SET 2, SET 3, MIX */
#define MO_NOTES 1u                      /* set.midi_out: the panel's keys as notes */
#define MO_CC 2u                         /* the knobs as CC */
#define MO_PC 4u                         /* pattern and break changes as program changes */
static uint32_t midi_pad_held, midi_pad_down;   /* black keys by note (bit i: key i): held, and pressed since the
                                          * last frame (so a note shorter than a frame still presses the key) */
static int32_t midi_enc[4];              /* K1..K4 turned by CC 20..23 since the last frame */
static uint8_t cc_in[4], cc_out[4];      /* their last value + 1 (0: none yet; out starts from 64) */
static uint8_t mo_quiet;                 /* MIDI in acting: nothing sent back out for it */
static void break_ask(uint32_t i);       /* (with the UI) */
static void pattern_go(int32_t want);
static void midi_fx_cc(uint32_t i, uint32_t v);

static int midi_ch_ok(uint32_t st) { return !set.midi_ch || (st & 15u) + 1u == set.midi_ch; }
static uint32_t midi_dest(void);
/* a channel message out on the channel (OMNI: 1), when GLOBAL's CHANNEL part sends that kind */
static void midi_send3(uint32_t st, uint32_t a, uint32_t b, uint32_t kind)
{
    if (!(set.midi_out & kind) || mo_quiet)
        return;
    st |= set.midi_ch ? set.midi_ch - 1u : 0u;
    plat_midi_send(st >> 4 | st << 8 | (a & 0x7Fu) << 16 | (b & 0x7Fu) << 24, midi_dest());
}
static void midi_cc(uint32_t c, uint32_t v)   /* a CC in: K1..K4 turned, or a chain slot's setting */
{
    if (c >= CC_K1 && c < CC_K1 + 4u) {
        uint32_t i = c - CC_K1;
        if (cc_in[i])                    /* (the first only says where the fader is) */
            midi_enc[i] += (int32_t)v - (int32_t)(cc_in[i] - 1u);
        cc_in[i] = (uint8_t)(v + 1u);
    } else if (c >= CC_FX0 && c < CC_FX0 + 16u)
        midi_fx_cc(c - CC_FX0, v);
}
static void midi_k_out(uint32_t i, int32_t d)   /* K1..K4 turned on the panel: CC 20..23, a value moved by the turn */
{
    int32_t v = (cc_out[i] ? (int32_t)cc_out[i] - 1 : 64) + d;
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    cc_out[i] = (uint8_t)(v + 1);
    midi_send3(0xB0u, CC_K1 + i, (uint32_t)v, MO_CC);
}
static void midi_pc(uint32_t n)          /* program change in: 0..15 the pattern, 16..31 the break */
{
    if (n < NPAT)
        pattern_go((int32_t)n);
    else if (n - 16u < n_breaks && n >= 16u)
        break_ask(n - 16u);
}
static void midi_slice(int s, int on);   /* (with the UI: they play as the keys, and REC records them) */
static void midi_drum(uint32_t role);
#define MS_PER_SAMPLE (1000.0f / (float)BRK_SR)
#define CLKQ 64u
/* (times in plat_ms: the 1 kHz tick sends at that grain anyway, a beat of ticks averages the tempo, and the host
 * simulator keeps plat_ms in its own time) */
static volatile uint32_t clkq_due[CLKQ]; /* clock out: when each tick is due (plat_ms); the render writes them */
static volatile uint32_t clkq_w, clkq_r; /* and the 1 kHz tick sends them */
static volatile uint32_t sync_n16, sync_ms, sync_c;   /* clock in: the outside 16th begun, when, and a count */
static uint32_t sync_seen;
static struct {
    uint32_t ring[24], i;                /* the last 24 ticks' times: a beat */
    uint32_t last, n;                    /* the last tick; ticks since the start (FA) */
    float bpm;                           /* the tempo they give, smoothed */
    uint32_t spp;                        /* the song position given (F2), in 16ths: where a continue starts */
    uint8_t run, port;                   /* started (FA / FB, not yet FC); where they come from */
    uint8_t cont;                        /* a continue: 16ths still to place the loop on */
} ext;

static uint32_t midi_dest(void)
{
    return set.midi_to == 1u ? TO_USB : set.midi_to == 2u ? TO_BLE : TO_USB | TO_BLE;
}

static int ext_active(void)              /* following an outside clock: on, and ticks in the last half second */
{
    return set.clk_in && ext.i && plat_ms() - ext.last < 500u;
}

/* PLAY: the transport, and with clock out on, MIDI start / stop after it. HOME + PLAY (restart): back to bar 1,
 * and while playing a start again */
static void transport(int on, int restart)
{
    if (restart)
        brk_reset();
    else
        brk_play(on);
    if (set.clk_out && (on || !restart))
        plat_midi_send(PKT_RT(on ? 0xFAu : 0xFCu), midi_dest());
}

int app_ble_at_boot(void) { return !set.ble_off; }

/* main loop, every pass: MIDI in. With clock in on, ticks set the tempo (a beat's worth, smoothed) and every beat
 * the loop's phase (brk_sync, in the render); start, continue and stop drive the transport */
void app_loop(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = pkt >> 8 & 0xFFu, t = plat_ms(), d1 = pkt >> 16 & 0x7Fu, d2 = pkt >> 24 & 0x7Fu;
        if (st >= 0x80u && st < 0xF0u && !midi_ch_ok(st))
            continue;                    /* another channel's (GLOBAL's CHANNEL part: not OMNI) */
        mo_quiet = 1u;
        if ((st & 0xF0u) == 0xB0u)
            midi_cc(d1, d2);
        else if ((st & 0xF0u) == 0xC0u)
            midi_pc(d1);
        mo_quiet = 0u;
        if ((st & 0xE0u) == 0x80u) {     /* notes, on the channel: 48..63 (C3..D#4) the slices, held as the white keys;
                                          * 64..74 (E4..D5) the black keys; 24..35 (C1..B1) the breaks; 36 a KICK
                                          * slice, 38 / 40 a SNARE, 42 / 44 / 46 a HAT, 37 / 39 a PERC one (a drum
                                          * machine's notes: the Slice Map's slices) */
            uint32_t note = d1, on = (st & 0xF0u) == 0x90u && d2;
            mo_quiet = 1u;
            if (on && note >= MIDI_BRK0 && note < MIDI_BRK0 + 12u && note - MIDI_BRK0 < n_breaks)
                break_ask(note - MIDI_BRK0);
            mo_quiet = 0u;
            if (note >= MIDI_SLICE0 && note < MIDI_SLICE0 + 16u)
                midi_slice((int)(note - MIDI_SLICE0), (int)on);
            else if (note >= MIDI_PAD0 && note < MIDI_PAD0 + 11u) {   /* 64..74 (E4..D5): the black keys, held
                                          * as the panel's (input() reads them with the keys: every page's own) */
                uint32_t b = 1u << (note - MIDI_PAD0);
                if (on) {
                    midi_pad_held |= b;
                    midi_pad_down |= b;
                } else
                    midi_pad_held &= ~b;
            }
            else if (on && note >= 36u && note <= 46u && (0x55Fu >> (note - 36u) & 1u))   /* 36 37 38 39 40 42 44 46 */
                midi_drum(note == 36u ? ROLE_KICK : note == 38u || note == 40u ? ROLE_SNARE
                          : note == 37u || note == 39u ? ROLE_PERC : ROLE_HAT);
            continue;
        }
        if (!set.clk_in)
            continue;
        if (st == 0xF8u) {
            uint32_t old = ext.ring[ext.i % 24u];
            ext.ring[ext.i % 24u] = t;
            ext.i++;
            ext.last = t;
            ext.port = (uint8_t)port;
            if (ext.i > 24u && t != old) {
                float bpm = 60000.0f / (float)(t - old);   /* 24 ticks: a beat */
                if (bpm >= 40.0f && bpm <= 300.0f) {
                    ext.bpm = ext.bpm > 0.0f ? ext.bpm + (bpm - ext.bpm) * 0.25f : bpm;
                    brk_tempo(ext.bpm);
                    brk_fx_tempo(ext.bpm);
                    set.tempo_x10 = (uint16_t)(ext.bpm * 10.0f + 0.5f);   /* (shown; not a change to save) */
                }
            }
            if (ext.run) {               /* each beat: where the outside clock is, for the render (after a
                                          * continue, the next two 16ths too: it starts where the song is) */
                if (ext.n % 24u == 0u || (ext.cont && ext.n % 6u == 0u)) {
                    if (ext.cont)
                        ext.cont--;
                    sync_n16 = ext.n / 6u;
                    sync_ms = t;
                    sync_c++;
                }
                ext.n++;
            }
        } else if (st == 0xF2u)          /* song position (16ths): where a continue starts */
            ext.spp = d1 | d2 << 7;
        else if (st == 0xFAu || st == 0xFBu) {   /* start: from the top; continue: from the song position */
            ext.n = st == 0xFBu ? ext.spp * 6u : 0u;
            ext.cont = (uint8_t)(st == 0xFBu && ext.spp ? 2u : 0u);
            if (st == 0xFAu)
                ext.spp = 0u;
            ext.run = 1u;
            brk_play(0);
            brk_play(1);
            muted = 0u;
        } else if (st == 0xFCu) {
            ext.run = 0u;
            brk_play(0);
        }
    }
}

/* 1 kHz: the clock ticks the render marked, each sent at its moment */
void app_tick(void)
{
    uint32_t now = plat_ms();
    while (clkq_r != clkq_w && (int32_t)(now - clkq_due[clkq_r % CLKQ]) >= 0) {
        plat_midi_send(PKT_RT(0xF8u), midi_dest());
        clkq_r++;
    }
}

/* a meter: this block's peak, or the last one falling (about 20 dB a second) */
static float meter(const float *x, uint32_t n, float last)
{
    float pk = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float a = x[i] < 0.0f ? -x[i] : x[i];
        if (a > pk)
            pk = a;
    }
    last *= 0.987f;
    return pk > last ? pk : last;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float mono[APP_BLOCK], l[APP_BLOCK], r[APP_BLOCK], echo[APP_BLOCK], verb[APP_BLOCK], ovl[APP_BLOCK];

    float gain = muted ? 0.0f : (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f;
    brk_status_t st;
    uint32_t t0 = plat_ms(), done = 0u, total = n;
    if (sync_c != sync_seen) {           /* the outside clock's beat, and how long ago it came */
        float age = (float)(t0 - sync_ms) / MS_PER_SAMPLE;
        sync_seen = sync_c;
        if (age < (float)BRK_SR * 0.25f)
            brk_sync(sync_n16, age);
    }
    while (n) {
        uint32_t m = n < APP_BLOCK ? n : APP_BLOCK, i;
        brk_sends(echo, verb);           /* the slices' throws into the delay and the reverb */
        brk_overlay_out(ovl);            /* the pack's overlay, beside the break */
        brk_render(mono, m);
        {                                /* the MIXER's BREAK fader (eased: no zipper), on the break and its throws */
            static float lvl = 1.0f;
            float want = (float)set.brk_level * 0.01f;
            for (i = 0; i < m; i++) {
                lvl += (want - lvl) * 0.002f;
                mono[i] *= lvl;
                echo[i] *= lvl;
                verb[i] *= lvl;
            }
            meter_brk = meter(mono, m, meter_brk);
        }
        meter_ov = meter(ovl, m, meter_ov);   /* (the layer: its LOOPS and SHOTS faders in the engine, brk_ly_gain) */
        if (set.clk_out) {               /* clock out: each tick due as its sample plays (a block from now) */
            uint16_t off[16];
            uint32_t k, nt = brk_clock_ticks(off, 16u);
            for (k = 0; k < nt && clkq_w - clkq_r < CLKQ; k++) {
                clkq_due[clkq_w % CLKQ] = t0 + (uint32_t)((float)(done + off[k] + total) * MS_PER_SAMPLE + 0.5f);
                clkq_w++;
            }
        }
        done += m;
        {                                /* the metronome's clicks: on the beats begun, the bar's first higher */
            uint16_t off[8];
            uint8_t idx[8];
            uint32_t k, ne = brk_step_events(off, idx, 8u);
            nclick = 0u;
            for (k = 0; k < ne; k++)
                if (set.click && !(idx[k] & 3u) && nclick < 4u) {
                    click_at[nclick] = off[k];
                    click_hi[nclick++] = idx[k] == 0u;
                }
        }
        brk_status(&st);
        /* the break through the DJ filter into the chain (the drum lanes and MATHCORE play the break itself) */
        brk_fx_process(mono, set.ov_route ? 0 : ovl, echo, verb, set.ov_route ? ovl : 0, l, r, m, st.pads);   /* (the
                                          * overlay into the chain after the DJ filter, or around it, PO-005) */
        if (nclick || click_env > 1e-3f) {   /* the metronome's click: dry, over everything, both sides */
            uint32_t j = 0u;
            for (i = 0; i < m; i++) {
                float x;
                if (j < nclick && click_at[j] <= i) {
                    click_env = 1.0f;
                    click_ph = 0.0f;
                    click_inc = (click_hi[j] ? 2400.0f : 1500.0f) / (float)BRK_SR;
                    j++;
                }
                x = (float)set.click * 0.006f * click_env * brk_sin_turns(click_ph);   /* (100 %: 0.6) */
                click_ph += click_inc;
                if (click_ph >= 1.0f)
                    click_ph -= 1.0f;
                click_env *= 0.9982f;    /* (~12 ms to -20 dB) */
                l[i] += x;
                r[i] += x;
            }
            if (click_env < 1e-3f)
                click_env = 0.0f;
        }
        {                                /* the header's meter: as it leaves, after MASTER */
            float out = gain * (1.0f / 8388608.0f), a = meter(l, m, 0.0f) * out, b = meter(r, m, 0.0f) * out;
            meter_l = a > meter_l * 0.987f ? a : meter_l * 0.987f;
            meter_r = b > meter_r * 0.987f ? b : meter_r * 0.987f;
        }
        for (i = 0; i < m; i++) {
            lr[2u * i] = (int32_t)(l[i] * gain);
            lr[2u * i + 1u] = (int32_t)(r[i] * gain);
        }
        lr += 2u * m;
        n -= m;
    }
}

/* (the audio interrupt, right after a render that took over 90 % of its time: PRD E-7) */
void app_overload(void) { brk_fx_overload(); }

void app_panic(void)
{
    muted = 1u;
    brk_play(0);
}

/* ----------------------------------------------------------------------- UI --- */
#include "ui/common.c"
#include "ui/fx.c"
#include "ui/pads.c"
#include "ui/keys.c"
#include "ui/global.c"
#include "ui/arp.c"
#include "ui/seq.c"
#include "ui/mc.c"
#include "ui/mixer.c"
#include "ui/screen.c"
#include "ui/demo.c"
#include "ui/main.c"
