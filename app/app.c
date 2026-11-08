/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH on fm1-base: the app's unity build (compiled into fm1-base's device/unity.c after the platform, so gfx.c's
 * cv_* drawing and the plat_* calls are in scope) and the glue between the contract (app.h) and the engine
 * (dsp/brk.c, dsp/brk_fx.c: their own -O2 units).
 *
 * M1 scope (PRD 13): the built-in break, 16 slices on the white keys, the 11 pads on the black keys, play / reset,
 * tempo and tap tempo, pitch on OCT-/OCT+, the generator, the four FX and the DJ filter, settings kept by SAVE.
 * Breaks: the bank built into the image (app/bank_data.h, which ./build.sh generates from breaks/breaks.txt), chosen
 * on ALGORITHM; a build with no bank falls back to one synthesised break. */
#include <stddef.h>
#include "adpcm.h"
#include "brk_bank.h"
#include "brk_math.h"
#include "brk_synth.h"
#include "kick.h"
#include "perc.h"
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
    uint8_t drums_chain;                 /* the drums (kick, snare, hat): 1 through the FX chain, 0 around it */
    uint8_t pattern;                     /* the pattern playing, 0..NPAT-1 (patterns: their own store object) */
    uint8_t bta_on;                      /* Bluetooth audio out (GLOBAL, third part): on at power-on, the last device
                                          * connected again */
    uint8_t bta_rsv;                     /* (the SBC coding's byte: it was a knob once, but the knob is gone; the byte
                                          * stays, so older saves keep their size) */
    uint8_t click;                       /* the metronome's click (TAP K4): its level, 0 (off) .. 100 %, on each beat
                                          * while playing (was a spare byte: older saves have it off) */
    uint8_t rsv;
    brk_padset_t pads;                   /* the pads' own settings (K1-K4 while a pad is held) */
    brk_keyset_t keys[BRK_KEYS];         /* each white key's loop: length, pitch, start, mode (K1-K4 while held) */
    brk_slicefx_t slx[BRK_KEYS];         /* each slice's own effect and level (a key held, FX: its other layer) */
    kick_t kick;                         /* the kick (PRD K-*): its sound and level */
    perc_t perc;                         /* the snare and the hat (PRD K-10): their sounds and level */
    uint8_t rsv2[8];                     /* (v0.52's fills, gone; their bytes kept: older saves still load) */
    uint8_t rsv3[20];                    /* (3 Nov 2026's overlay effects, PRD B-52, gone 8 Nov 2026: their bytes kept) */
    /* (appended 4 Nov 2026: MB COMP's detail per FX slot, PRD A-49a; it moves with its slot) */
    brk_mbc_t mbc[BRK_FX_SLOTS];
    /* (appended 4 Nov 2026: every slot's extras, its MORE and MOD pages, PRD A-49b; they move with their slot) */
    brk_fxx_t fxx[BRK_FX_SLOTS];
    /* (appended 8 Nov 2026: the ARP page's black keys, each an effect slot, PRD B-55) */
    brk_fxslot_t pfx[NPFX];
    brk_fxx_t pfxx[NPFX];
} settings_t;
#define SET_SIZE_MIN offsetof(settings_t, rsv2)   /* the shortest save that loads: CHP7's first size */
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
    brk_padset_defaults(&set.pads);
    for (k = 0; k < BRK_KEYS; k++) {
        brk_keyset_defaults(&set.keys[k]);
        brk_slicefx_defaults(&set.slx[k]);
    }
    kick_defaults(&set.kick);
    perc_defaults(&set.perc);
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
    kick_set(&set.kick);
    perc_set(&set.perc);
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
static volatile uint16_t lane_now[NLANES];   /* the pattern playing's lanes, for the render */
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
static void pat_lanes_out(void)
{
    uint32_t l;
    for (l = 0; l < NLANES; l++)
        lane_now[l] = pats.p[set.pattern % NPAT].lane[l];
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

/* ------------------------------------------------------------------- engine --- */
static int16_t pcm_a[CH_PCM_MAX] PLAT_POOL;
#define CH_DUB_LEN 30720u                /* the dub delay's line: 0.70 s a side, 3/8 down to 97 BPM, 1/2 to 172 */
static int16_t dub_line[CH_DUB_LEN] PLAT_POOL;
static uint32_t pfx_mem[BRK_PAD_V * BRK_PAD_ST_BYTES / 4u] PLAT_POOL;   /* the ARP keys' voices (9.4 KB) */
static brk_break_t brk0;
static brk_break_t brk_cur[2];           /* the bank's break playing (pcm_a holds it), and the next one's: the render
                                          * keeps its own while the next is written */
static uint32_t n_synth;                 /* 0; 1 in a build with no bank: a synthesised break (brk_synth.c) */
static uint32_t n_breaks;                /* then the bank's valid slots */
static int32_t brk_next = -1;            /* a break chosen, waiting for the next bar (break_ask), or -1 */
static uint8_t wave_px[240];             /* the waveform's height per column, for the screen */
#define BLK_N 16u                        /* the slicer view's blocks: one per white key */
#define BLK_W 26u                        /* columns of waveform in a block */
static uint8_t blk_px[BLK_N][BLK_W];     /* each slice's waveform, 0..19 */
static uint8_t blk_n;                    /* blocks with a slice behind them */
static volatile uint8_t muted;
static volatile float meter_brk, meter_kick, meter_perc;   /* the MIXER's channels: the break, the kick, the snare
                                                           * and hat, before the chain */
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

/* the bank built into the image: its slot i (0..), or 0 when absent or not sane */
static const bank_slot_t *bank_slot(uint32_t i)
{
#if CH_HAVE_BANK
    const bank_header_t *h = (const bank_header_t *)CH_BANK;
    const bank_slot_t *s;
    if (h->magic != BANK_MAGIC || h->version != BANK_VERSION || i >= h->nslots || i >= BANK_SLOTS ||
        16u + 168u * (i + 1u) > CH_BANK_SIZE)
        return 0;
    s = (const bank_slot_t *)(CH_BANK + 16u + 168u * i);
    if (s->data_off + s->data_len > CH_BANK_SIZE || s->len > CH_PCM_MAX || s->data_len < (s->len + 1u) / 2u ||
        !s->nslices || s->nslices > BANK_SLICES || !s->bars || s->slice[s->nslices] != s->len || !s->bpm_x100)
        return 0;
    return s;
#else
    (void)i;
    return 0;
#endif
}

/* break i's name, for the screen before it is in */
static const char *break_name(uint32_t i)
{
    static char t[13];
    const bank_slot_t *s = i >= n_synth ? bank_slot(i - n_synth) : 0;
    uint32_t k;
    for (k = 0; k < 12u; k++)
        t[k] = s ? s->name[k] : 0;
    t[12] = 0;
    return s ? t : brk0.name;
}

/* break i into pcm_a and the descriptor the render isn't using (whatever plays from pcm_a meanwhile is muted, see
 * break_poll); hand it to the render after */
static const brk_break_t *break_load(uint32_t i)
{
    const bank_slot_t *s = i >= n_synth ? bank_slot(i - n_synth) : 0;
    brk_break_t *d = brk_get_break() == &brk_cur[0] ? &brk_cur[1] : &brk_cur[0];
    const brk_break_t *b = &brk0;
    uint32_t k;
    if (s) {
        adpcm_state_t st = {0, 0, 0};
#if CH_HAVE_BANK
        adpcm_decode(&st, CH_BANK + s->data_off, pcm_a, s->len);
#endif
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
        b = d;
    } else {                             /* a built-in style, played into RAM now */
        if (i >= n_synth)
            i = 0u;
#if !CH_HAVE_BANK                        /* (a bank build has no synthesised breaks: their code stays out) */
        if (brk_synth_style_len(i, CH_RATE) <= CH_PCM_MAX)
            brk_synth_style(pcm_a, CH_RATE, i, &brk0);
#endif
    }
    set.brk = (uint8_t)i;
    wave_build(b);
    return b;
}

void app_init(void)
{
    brk_init();
    brk_fx_init();
    brk_fx_delay_buffer(dub_line, CH_DUB_LEN);
    kick_init();
    perc_init();
    brk_fx_pad_buffer(pfx_mem);
    n_synth = n_breaks = 0u;             /* the breaks are the bank's (breaks/breaks.txt) */
    while (n_breaks < BANK_SLOTS && bank_slot(n_breaks))
        n_breaks++;
    if (!n_breaks)                       /* a build without a bank still has a break: a synthesised Amen */
        n_synth = n_breaks = 1u;
    settings_defaults();                 /* (what a shorter, older save doesn't reach keeps these) */
    {
        int sz = plat_store_size(OBJ_SETTINGS);
        if (sz < (int)SET_SIZE_MIN || sz > (int)sizeof set || plat_store_load(OBJ_SETTINGS, &set, (uint32_t)sz) != sz)
            set.magic = 0u;
    }
    if (set.magic != SET_MAGIC || set.tempo_x10 < 400u || set.tempo_x10 > 3000u || set.swing < 50u ||
        set.swing > 75u) {
        settings_defaults();
        if (n_breaks > n_synth)
            set.brk = (uint8_t)n_synth;  /* the bank's first break */
    }
    if (set.brk >= n_breaks)
        set.brk = 0u;
    if (plat_store_load(OBJ_PAT, &pats, sizeof pats) != (int)sizeof pats || pats.magic != PAT_MAGIC)
        pat_defaults();
    brk_set_break(break_load(set.brk));  /* boot: the audio isn't running yet */
    settings_apply();
    pat_apply();
}

/* ------------------------------------------------------- MIDI clock, Bluetooth --- */
#define PKT_RT(b) (0x0Fu | (uint32_t)(b) << 8)   /* a real-time byte as a USB-MIDI packet (CIN 0xF) */
#define MIDI_SLICE0 48u                  /* C3: slice 1 */
static void midi_slice(int s, int on);   /* (with the UI: they play as the keys, and REC records them) */
static void midi_drum(uint32_t lane);
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
    uint8_t run, port;                   /* started (FA / FB, not yet FC); where they come from */
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
        uint32_t st = pkt >> 8 & 0xFFu, t = plat_ms();
        if ((st & 0xE0u) == 0x80u) {     /* notes, any channel: 48..63 (C3..D#4) the slices, held as the white keys;
                                          * 36 the kick, 38 the snare, 42 / 44 / 46 the hat (a drum machine's) */
            uint32_t note = pkt >> 16 & 0x7Fu, on = (st & 0xF0u) == 0x90u && (pkt >> 24 & 0x7Fu);
            if (note >= MIDI_SLICE0 && note < MIDI_SLICE0 + 16u)
                midi_slice((int)(note - MIDI_SLICE0), (int)on);
            else if (on && (note == 36u || note == 38u || note == 42u || note == 44u || note == 46u))
                midi_drum(note == 36u ? LANE_KICK : note == 38u ? LANE_SNARE : LANE_HAT);
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
            if (ext.run) {               /* each beat: where the outside clock is, for the render */
                if (ext.n % 24u == 0u) {
                    sync_n16 = ext.n / 6u;
                    sync_ms = t;
                    sync_c++;
                }
                ext.n++;
            }
        } else if (st == 0xFAu || st == 0xFBu) {   /* start (continue starts again too: no song position yet) */
            ext.n = 0u;
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
    static float kick[APP_BLOCK], perc[APP_BLOCK];
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
        if (set.clk_out) {               /* clock out: each tick due as its sample plays (a block from now) */
            uint16_t off[16];
            uint32_t k, nt = brk_clock_ticks(off, 16u);
            for (k = 0; k < nt && clkq_w - clkq_r < CLKQ; k++) {
                clkq_due[clkq_w % CLKQ] = t0 + (uint32_t)((float)(done + off[k] + total) * MS_PER_SAMPLE + 0.5f);
                clkq_w++;
            }
        }
        done += m;
        {                                /* the drums: on the steps of the bar their lanes have, at their samples */
            uint16_t off[8], ka[8], pa[8];
            uint8_t idx[8], who[8];
            uint32_t k, ne = brk_step_events(off, idx, 8u), nk = 0u, np = 0u;
            uint32_t lk = lane_now[LANE_KICK], ls = lane_now[LANE_SNARE], lh = lane_now[LANE_HAT];
            nclick = 0u;
            for (k = 0; k < ne; k++) {
                if (set.click && !(idx[k] & 3u) && nclick < 4u) {   /* the metronome: each beat, the bar's first higher */
                    click_at[nclick] = off[k];
                    click_hi[nclick++] = idx[k] == 0u;
                }
                uint32_t w = (ls >> idx[k] & 1u) << PV_SNARE | (lh >> idx[k] & 1u) << PV_HAT;
                if (lk >> idx[k] & 1u)
                    ka[nk++] = off[k];
                if (w) {
                    pa[np] = off[k];
                    who[np++] = (uint8_t)w;
                }
            }
            kick_render(kick, m, ka, nk);
            perc_render(perc, m, pa, who, np);
            meter_kick = meter(kick, m, meter_kick);
            meter_perc = meter(perc, m, meter_perc);
            for (i = 0; i < m; i++)
                kick[i] += perc[i];
        }
        brk_status(&st);
        /* the break through the DJ filter, the drums joining it into the chain (or around it) */
        brk_fx_process(mono, set.drums_chain ? kick : 0, echo, verb, set.drums_chain ? 0 : kick, l, r, m, st.pads);
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
#include "ui/bta.c"
#include "ui/global.c"
#include "ui/arp.c"
#include "ui/seq.c"
#include "ui/kick.c"
#include "ui/perc.c"
#include "ui/mixer.c"
#include "ui/screen.c"
#include "ui/demo.c"
#include "ui/main.c"
