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
#define APP_SPLASH                       /* the first screen at power-on is the app's (app_splash) */
void app_splash(void);
#define APP_PALETTE 1u                   /* gfx.c's amber */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 96u)       /* draw in strips of 96 rows at most: the pool goes to the breaks */
#ifdef APP_VERSION
#define CH_VERSION APP_VERSION
#else
#define CH_VERSION "DEV"
#endif

/* the store (PRD 8.4): settings in Felucca's settings pair, the panel calibration where every fm1-base firmware
 * keeps it. The break bank will take 0x97000..0xD9FFF and PLAT_XDATA1 (M5), patterns 0xDA000 (M2). */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the pool: the break playing, decoded to 16-bit PCM (PRD 8.2). M1 has one buffer; M5 adds the second */
#define CH_PCM_MAX 81920u                /* samples: 3.7 s at 22.05 kHz (2 bars at 137 BPM and up) */
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
