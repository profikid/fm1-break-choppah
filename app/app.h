/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c) and its settings. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "brk.h"
#include "brk_fx.h"

#define APP_NAME "CHOPPAH"
#define APP_TAGLINE "AMEN CHOPPER"
#define APP_SPLASH_RGB RGB(255, 184, 70)
#define APP_BIG_FONT FONT_B             /* the platform's own screens' big face: the 31-pixel one (20 KB of flash) left
                                          * out for Bluetooth audio; CHOPPAH draws its name with FONT_B doubled */
#define APP_SPLASH                       /* the first screen at power-on is the app's (app_splash) */
void app_splash(void);
#define APP_PALETTE 1u                   /* gfx.c's amber */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 60u)       /* draw in strips of 60 rows at most: the pool goes to the breaks (and the
                                          * Bluetooth audio queue) */
#ifdef APP_VERSION
#define CH_VERSION APP_VERSION
#else
#define CH_VERSION "DEV"
#endif

/* MIDI and Bluetooth (PRD N-1, N-2, N-5): the GLOBAL page's clock in / out and Bluetooth MIDI. The platform calls
 * app_loop every pass of the main loop (MIDI in), app_tick at 1 kHz (clock out on time); Bluetooth starts at
 * power-on as the saved setting says, advertising as "CHOPPAH XXXX" */
#define APP_LOOP
void app_loop(void);
#define APP_TICK
void app_tick(void);
int app_ble_at_boot(void);
#define APP_BLE_AT_BOOT (app_ble_at_boot())
#define BLE_DEVICE_NAME "CHOPPAH"
/* Bluetooth audio out (fm1.toml bt_audio, PRD A-72): its packet queue (40 x 332 B) in the pool, not RAM (the RAM is
 * the tighter here: the reverb's lines are in it) */
#define BP_MQ_ATTR PLAT_POOL

/* the store (PRD 8.4): settings in Felucca's settings pair, the panel calibration where every fm1-base firmware
 * keeps it. The break bank will take 0x97000..0xD9FFF and PLAT_XDATA1 (M5), patterns 0xDA000 (M2). */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };   /* OBJ_BLE: Bluetooth's address and pairing */
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* the pool: the break playing, decoded to 16-bit PCM (PRD 8.2). M1 has one buffer; M5 adds the second */
#define CH_PCM_MAX 106496u               /* samples: 4.8 s at 22.05 kHz (2 bars at 99.4 BPM and up: Funky Drummer) */
#define CH_RATE 22050u                   /* the built-in break's rate */

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen */

/* the host simulator's own commands (app_host_cmd in app.c) */
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
