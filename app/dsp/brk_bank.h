/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's break bank v1 (PRD 8.3): how tools/prep_breaks.py lays breaks out in flash. Little-endian.
 *
 *   bank header (16 B) | slot table (nslots x 168 B) | IMA ADPCM data (each slot's, 4-byte aligned)
 *
 * The CRC-32 (IEEE, as zlib's) covers everything after the header. An upload writes the data and the slot table
 * first and the header last, so a cut upload leaves either the old bank or no valid header (PRD A-62). */
#pragma once
#include <stdint.h>

#define BANK_MAGIC 0x31424843u           /* "CHB1" */
#define BANK_VERSION 1u
#define BANK_VERSION2 2u                 /* v2 (PRD_PACKS 3.2, CHOPPAH 1.7): after the slot table a u32 count of chunks,
                                          * each {tag u32, len u32, data, zeros to 4 bytes}, then the slots' ADPCM; a v2
                                          * bank may hold chunks and no slots (bank 2 with the overlay alone) */
#define CHK_PACK 0x4B434150u             /* "PACK": bank_pack_t */
#define CHK_SSET 0x54455353u             /* "SSET": bank_sset_t a slot, in the slots' order (bank 1's, then bank 2's) */
#define CHK_CHAN 0x4E414843u             /* "CHAN" (1.7): MATHCORE's chain and the black keys' scenes, as OBJ_MQ holds
                                          * them after its magic: n, rsv[3], q[64] (scenes 0..15), key[11] (0xFF none),
                                          * rsv: 80 bytes. Without it a pack gets the factory's chain and keys */
#define CHK_SCNE 0x454E4353u             /* "SCNE": MATHCORE's scenes as OBJ_MC holds them (magic, 16 x mc_scene_t) */
#define CHK_GRUV 0x56555247u             /* "GRUV": the groove's context (the timebase, the swing, the scene map) */
#define CHK_EVNT 0x544E5645u             /* "EVNT": the groove's events (mc_event_t) */
#define CHK_GROO 0x4F4F5247u             /* "GROO": a break's own groove (the per-slice character) */
#define CHK_THEM 0x4D454854u             /* "THEM": the screen's colours, 8 x RGB565 (bg panel text dim hi mid wave warn)
                                          * then 16 bytes reserved */
#define CHK_OVLY 0x594C564Fu             /* "OVLY": the overlay, bank_ovly_t then its IMA ADPCM */
#define CHK_LOOP 0x504F4F4Cu             /* "LOOP" (1.8, docs/PRD_LAYER.md 6): u32 n (<= 4), n x bank_loop_t, then the
                                          * ADPCM of the loops not in OVLY; loop 0 may be OVLY's (LPF_OVLY) */
#define CHK_SHOT 0x544F4853u             /* "SHOT" (1.8): u32 n (<= 16), n x bank_shot_t, then their ADPCM */
#define CHK_PADS 0x53444150u             /* "PADS" (1.8): the ARP page's 11 effect pads, each brk_fxslot_t (20 B: rsv[0]
                                          * its TARGET) then brk_fxx_t (36 B): 616 bytes. Without it: the FM-1's own */
#define BANK_SLOTS 16u
#define BANK_SLICES 32u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t nslots;
    uint32_t data_size;                  /* bytes after the header */
    uint32_t crc32;
} bank_header_t;                         /* 16 bytes */

typedef struct {
    char name[12];                       /* NUL-padded, upper case */
    uint32_t bpm_x100;
    uint32_t rate;                       /* Hz */
    uint32_t len;                        /* samples */
    uint32_t data_off;                   /* from the bank's start */
    uint32_t data_len;                   /* bytes of ADPCM: (len + 1) / 2 */
    uint8_t bars;
    uint8_t nslices;
    uint16_t rsv;
    uint32_t slice[BANK_SLICES + 1];     /* sample offsets, slice[nslices] = len */
} bank_slot_t;                           /* 168 bytes */

typedef struct {
    uint8_t id[16];                      /* the pack's UUID */
    char name[16], author[16];           /* NUL-padded, upper case */
    uint32_t created, updated;           /* Unix seconds */
} bank_pack_t;                           /* 56 bytes */

typedef struct {
    uint8_t loop_steps;                  /* 4, 8, 16 or 32 (the whole break): the loop's window, 16ths */
    uint8_t loop_start;                  /* 0..31: where it starts */
    int8_t pitch;                        /* -24..+24 semitones, on top of the global pitch */
    int8_t gain_db;                      /* -24..+12 */
    uint16_t bpm_x100;                   /* the tempo it was played at, over the slot's (0: the slot's) */
    uint8_t swing;                       /* 50..75 %, 0: the global swing */
    uint8_t rsv;
    uint8_t roles[32];                   /* slice i's role bits (1 << ROLE_*), 0xFF: heard (the Slice Map's own) */
} bank_sset_t;                           /* 40 bytes */

typedef struct {
    uint32_t rate, len, bpm_x100;        /* Hz, samples, the tempo it was played at */
    uint8_t bars;                        /* 4, 8 or 16 */
    uint8_t rsv[3];
} bank_ovly_t;                           /* 16 bytes, then (len + 1) / 2 bytes of ADPCM */

enum { LPF_AUTO = 1, LPF_OVLY = 2 };    /* a loop on when PLAY starts; its ADPCM is the OVLY chunk's */
typedef struct {
    char name[8];                        /* NUL-padded, upper case */
    uint32_t rate, len, bpm_x100;        /* Hz, samples, the tempo it was played at */
    uint32_t data_off;                   /* its ADPCM: from the chunk's data (unused with LPF_OVLY) */
    uint8_t bars, flags;                 /* 1..32; LPF_* */
    uint8_t level;                       /* 0..100 % */
    int8_t filter;                       /* -50 low-pass .. 0 .. +50 high-pass */
    uint8_t quant;                       /* LYQ_* (brk.h) */
    uint8_t wstart, wbars;               /* its window: first bar (0..), bars (0: all) */
    uint8_t rsv;
} bank_loop_t;                           /* 32 bytes */
enum { SHF_CUT = 1 };                    /* the break silent while it sounds (a fill) */
typedef struct {
    char name[8];
    uint32_t rate, len, bpm_x100;        /* bpm_x100 0: none (its pitch stays its own); else it follows the tempo */
    uint32_t data_off;                   /* its ADPCM: from the chunk's data */
    uint8_t level, mode, choke, quant;   /* 0..100; SHM_*; 0..4; SHQ_* */
    int8_t pitch;                        /* -24..+24 */
    uint8_t decay, flags, rsv;           /* 0 none, 1 long .. 31 short; SHF_* */
} bank_shot_t;                           /* 32 bytes */

_Static_assert(sizeof(bank_loop_t) == 32, "bank loop");
_Static_assert(sizeof(bank_shot_t) == 32, "bank shot");
_Static_assert(sizeof(bank_header_t) == 16, "bank header");
_Static_assert(sizeof(bank_pack_t) == 56, "bank pack");
_Static_assert(sizeof(bank_sset_t) == 40, "bank sset");
_Static_assert(sizeof(bank_ovly_t) == 16, "bank ovly");
_Static_assert(sizeof(bank_slot_t) == 168, "bank slot");
