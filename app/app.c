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
    uint8_t drums_chain;                 /* the lanes (the drum lanes' and the scenes' slices): 1 through the FX
                                          * chain, 0 around it */
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
    uint8_t lane_level;                  /* the MIXER's LANES fader: the lanes' slices, 0..100 % */
    uint8_t mc_rsv[3];
    uint8_t mc_macro[4];                 /* MATHCORE LIVE's K1-K4: DENSITY, MATH, SPEED, CHAOS (0..100) */
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

static void settings_apply(void)
{
    brk_tempo((float)set.tempo_x10 * 0.1f);
    brk_pitch((float)set.pitch);
    brk_swing((float)set.swing * 0.01f);
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
#define MCS_MAGIC 0x3253434Du            /* "MCS2" (the scenes as they come: A..P filled, 22 Nov 2026) */
#define MQ_MAGIC 0x3151434Du             /* "MCQ1" */
#define MC_QUEUE 64u
#define MC_NOKEY 0xFFu                   /* a black key with no scene */
static struct {
    uint32_t magic;
    mc_scene_t sc[MC_SCENES];
} mcs;
_Static_assert(sizeof mcs <= 3840u, "a store object holds 3840 bytes");
static struct {
    uint32_t magic;
    uint8_t n;                           /* entries queued */
    uint8_t rsv[3];
    uint8_t q[MC_QUEUE];                 /* each a scene, 0..MC_SCENES-1, played in order and round again */
    uint8_t key[NPFX];                   /* each black key's scene (MC_NOKEY none) */
    uint8_t rsv2[5];
} mq;
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
}
/* the scenes as they come (A..P): grooves, fills, a riser, a breakdown, a drop, of different lengths, ready to play
 * from the black keys. A lane is a string: x a hit, X an accented one, . none (its length the string's) */
static const char *const MC_NAME[] = {"", "ROLLER", "AMEN", "STEPPER", "MATH 5/7", "FILL", "CHOP FILL", "RISER",
                                      "BREAKDOWN", "DOUBLE", "7/8 BOUNCE", "DROP", "ROLLER+BRK", "AMEN+BRK",
                                      "MATH+BRK", "DOUBLE+BRK", "BOUNCE+BRK"};
#define MC_NNAMES (sizeof MC_NAME / sizeof MC_NAME[0])
typedef struct {
    uint8_t name, bars, flags;
    struct {
        const char *p;                   /* 0: the lane off */
        uint8_t rate, mode, prob, ratchet, level;
        int8_t rise;
        uint8_t rev;
    } l[MC_LANES];
} mc_preset_t;
#define L_(p, r, m, pr, ra, lv, ri, rv) {p, r, m, pr, ra, lv, ri, rv}
#define OFF_ {0, MCR_16, 0, 100, 1, 100, 0, 0}
static const mc_preset_t MC_PRESET[11] = {
    {1, 2, 0, {L_("X.........X.....", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
               L_("....X.......X...", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
               L_("x.X.x.X.x.X.x.X.", MCR_16, MCM_ROUND, 100, 1, 80, 0, 0),
               L_("..............x.", MCR_16, MCM_ROUND, 60, 1, 70, 0, 0)}},
    {2, 2, 0, {L_("X.X.......XX....", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
               L_("....X..x.x..X..x", MCR_16, MCM_ACCENT, 100, 1, 100, 0, 0),
               L_("xxxxxxxxxxxxxxxx", MCR_16, MCM_RANDOM, 70, 1, 70, 0, 0), OFF_}},
    {3, 2, 0, {L_("X.........X.....X.....X.........", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
               L_("........X...............X.......", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
               L_("x.x.x.x.x.x.x.x.", MCR_16, MCM_ROUND, 100, 1, 75, 0, 0),
               L_("...x.......x..x.", MCR_16, MCM_RANDOM, 50, 1, 70, 0, 0)}},
    {4, 4, 0, {L_("X..x.", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
               L_("....X..", MCR_16, MCM_ALT, 100, 1, 100, 0, 0),
               L_("x.xx.x.xx.x", MCR_32, MCM_RANDOM, 100, 1, 65, 0, 0),
               L_("x..x..x..x.x.", MCR_16, MCM_ROUND, 100, 1, 80, 0, 0)}},
    {5, 1, MCS_ONCE, {L_("X...............", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                      L_("x.x.x.x.x.x.x.x.xxxxxxxxXXXXXXXX", MCR_32, MCM_ACCENT, 100, 1, 100, 3, 0),
                      OFF_,
                      L_("........x.x.xxxx", MCR_16, MCM_ROUND, 100, 2, 80, 0, 0)}},
    {6, 1, MCS_ONCE, {L_("X..X..X..X..X.X.", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                      L_("..X..X..X..X.XXX", MCR_16, MCM_RANDOM, 85, 2, 100, 0, 0),
                      L_("xxxxxxxxxxxxxxxxxxxxxxxx", MCR_32T, MCM_RANDOM, 60, 1, 70, 0, 0), OFF_}},
    {7, 4, MCS_ONCE, {L_("X...X...X.X.XXXX", MCR_4, MCM_FIXED, 100, 1, 100, 0, 0),
                      L_("X", MCR_8, MCM_ROUND, 100, 1, 100, 12, 0),
                      L_("x", MCR_8, MCM_ROUND, 100, 1, 60, 7, 0), OFF_}},
    {8, 4, MCS_BREAK, {L_("X...............X...............", MCR_8, MCM_FIXED, 100, 1, 100, 0, 0), OFF_,
                       L_("x..", MCR_8T, MCM_ROUND, 100, 1, 55, 0, 0),
                       L_("x....", MCR_8, MCM_RANDOM, 100, 1, 60, 0, 0)}},
    {9, 2, 0, {L_("X..X..X.", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
               L_("....X.......X...", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
               L_("xXxxXxxXxxXxxXxx", MCR_32, MCM_ROUND, 100, 1, 70, 0, 0),
               L_("x.....", MCR_16T, MCM_ROUND, 100, 1, 75, 0, 0)}},
    {10, 2, 0, {L_("X..X.X.", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                L_("...X......X...", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                L_("x.xx.xx.xx.x", MCR_16T, MCM_RANDOM, 100, 1, 70, 0, 0),
                L_("x...x.x..", MCR_16, MCM_ROUND, 70, 1, 75, 0, 0)}},
    {11, 1, MCS_ONCE, {L_("X...............", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0),
                       L_("X...............", MCR_16, MCM_FIXED, 100, 1, 100, 0, 0), OFF_,
                       L_("........X.......", MCR_16, MCM_FIXED, 100, 1, 100, 0, 1)}},
};
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
    const mc_preset_t *p = &MC_PRESET[i < 11u ? i : i == 11u ? 0u : i == 12u ? 1u : i == 13u ? 3u : i == 14u ? 8u : 9u];
    uint32_t l;
    s->bars = p->bars;
    s->flags = (uint8_t)(p->flags | (i >= 11u ? MCS_BREAK : 0u));   /* L..P: grooves with the break under them */
    s->name = (uint8_t)(i < 11u ? p->name : 12u + (i - 11u));
    s->rsv = 0u;
    for (l = 0; l < MC_LANES; l++) {
        mc_lane_t *L = &s->lane[l];
        mc_lane_defaults(L, l);
        if (!p->l[l].p)
            continue;
        mc_lane_from(L, p->l[l].p);
        L->flags = (uint8_t)(MCL_ON | (p->l[l].rev ? MCL_REV : 0u));
        L->rate = p->l[l].rate;
        L->mode = p->l[l].mode;
        L->prob = p->l[l].prob;
        L->ratchet = p->l[l].ratchet;
        L->level = p->l[l].level;
        L->rise = p->l[l].rise;
        L->seed = (uint8_t)(L->seed + i * 13u);
    }
}
static void mc_defaults(void)
{
    uint32_t i;
    mcs.magic = MCS_MAGIC;
    for (i = 0; i < MC_SCENES; i++)
        mc_scene_defaults(&mcs.sc[i], i);
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
    if (plat_store_load(OBJ_MQ, &mq, sizeof mq) != (int)sizeof mq || mq.magic != MQ_MAGIC || mq.n > MC_QUEUE)
        mq_defaults();
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
static uint32_t n_breaks;                /* the custom break, then the bank's valid slots */
static int32_t brk_next = -1;            /* a break chosen, waiting for the next bar (break_ask), or -1 */
static uint8_t wave_px[240];             /* the waveform's height per column, for the screen */
#define BLK_N 16u                        /* the slicer view's blocks: one per white key */
#define BLK_W 26u                        /* columns of waveform in a block */
static uint8_t blk_px[BLK_N][BLK_W];     /* each slice's waveform, 0..19 */
static uint8_t blk_n;                    /* blocks with a slice behind them */
static volatile uint8_t muted;
static volatile float meter_brk, meter_lanes;   /* the MIXER's channels: the break, the lanes, before the chain */
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
    if (!bank || h->magic != BANK_MAGIC || h->version != BANK_VERSION || i >= h->nslots || i >= BANK_SLOTS ||
        16u + 168u * (i + 1u) > size)
        return 0;
    s = (const bank_slot_t *)(bank + 16u + 168u * i);
    if (s->data_off + s->data_len > size || s->len > CH_PCM_MAX || s->data_len < (s->len + 1u) / 2u ||
        !s->nslices || s->nslices > BANK_SLICES || !s->bars || s->slice[s->nslices] != s->len || !s->bpm_x100)
        return 0;
    return s;
}

/* the custom break (PRD A-60): one break of the player's, a bank of one slot in the flash's data area, written over
 * USB from the site's break manager (app_sysex, ui/main.c). Present, it is break 1, loaded at power-on */
#define CU_BASE 0x97000u                 /* the data area's start, below the store objects (0xDA000 on) */
#define CU_SECTORS 14u                   /* 56 KB: a slot and CH_PCM_MAX samples of ADPCM */
#define CU_CAP (CU_SECTORS * 4096u)
static uint32_t n_user;                  /* 1: break 0 is the custom one (checked whole: cu_scan) */

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
static void cu_scan(void)
{
    const uint8_t *p = plat_data(CU_BASE);
    const bank_header_t *h = (const bank_header_t *)p;
    n_user = p && h->nslots == 1u && h->data_size <= CU_CAP - 16u && bank_check(p, CU_CAP, 0u) &&
             crc32_of(p + 16, h->data_size) == h->crc32 ? 1u : 0u;
}

/* break i's slot and its bank: the custom break first, then the bank built into the image */
static const bank_slot_t *slot_of(uint32_t i, const uint8_t **bank)
{
    *bank = 0;
    if (i < n_user) {
        *bank = plat_data(CU_BASE);
        return bank_check(*bank, CU_CAP, 0u);
    }
    i -= n_user;
#if CH_HAVE_BANK
    *bank = CH_BANK;
    return bank_check(CH_BANK, CH_BANK_SIZE, i);
#else
    return 0;
#endif
}

/* the breaks: the custom one, the bank's (no bank, no breaks: silence, nothing synthesised in their place) */
static void breaks_count(void)
{
    const uint8_t *bk;
    cu_scan();
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
        b = d;
    }
    set.brk = (uint8_t)i;
    if (b)
        wave_build(b);
    mc_auto_roles(b, roles_now);         /* the Slice Map: the kicks, snares, hats and the rest, heard */
    brk_roles(roles_now, BRK_MAX_SLICES);
    return b;
}

void app_init(void)
{
    brk_init();
    brk_fx_init();
    brk_fx_delay_buffer(dub_line, CH_DUB_LEN);
    brk_fx_pad_buffer(pfx_mem);
    breaks_count();                      /* the custom break, the bank's (breaks/breaks.txt) */
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
    if (set.brk >= n_breaks || n_user)   /* a custom break: always the one at power-on */
        set.brk = 0u;
    if (plat_store_load(OBJ_PAT, &pats, sizeof pats) != (int)sizeof pats || pats.magic != PAT_MAGIC)
        pat_defaults();
    mc_load();
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
    static float mono[APP_BLOCK], l[APP_BLOCK], r[APP_BLOCK], echo[APP_BLOCK], verb[APP_BLOCK];
    static float lanes[APP_BLOCK];
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
        brk_lanes_out(lanes);            /* the lanes' slices: a channel of their own */
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
        {                                /* the MIXER's LANES fader */
            static float lvl = 1.0f;
            float want = (float)set.lane_level * 0.01f;
            for (i = 0; i < m; i++) {
                lvl += (want - lvl) * 0.002f;
                lanes[i] *= lvl;
            }
            meter_lanes = meter(lanes, m, meter_lanes);
        }
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
        /* the break through the DJ filter, the lanes joining it into the chain (or around it) */
        brk_fx_process(mono, set.drums_chain ? lanes : 0, echo, verb, set.drums_chain ? 0 : lanes, l, r, m, st.pads);
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
