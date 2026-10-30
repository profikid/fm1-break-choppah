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
#define BANK_SLOTS 8u
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

_Static_assert(sizeof(bank_header_t) == 16, "bank header");
_Static_assert(sizeof(bank_slot_t) == 168, "bank slot");
