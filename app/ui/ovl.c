/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the OVERLAY page (ARP, PRD B-52). Each white key an effect over everything that plays (dsp/ovl.c), on
 * while held, any together; with LATCH a press switches its effect on and off (and latched ones stay on on the other
 * pages). K1 the amount of the effect touched last, K2 the rate of the ones on the beat, K3 the sweeps' length,
 * K4 LATCH. (Part of app.c's unity: included there in order.) */

static uint32_t ovl_held, ovl_latched;   /* bit OV_*: held down on this page; switched on (LATCH) */
static uint8_t ovl_last;                 /* the effect touched last: K1 its amount */

static void ovl_push(void) { ovl_set(ovl_held | ovl_latched, set.ovl_amt, set.ovl_rate, set.ovl_sweep); }

static void ovl_keys(uint32_t keys, uint32_t kdn)   /* every input: the white keys on the page, then the engine */
{
    uint32_t i, held = 0u;
    if (ui.page == PG_OVL)
        for (i = 0; i < 16u; i++) {
            if (kdn >> WK[i] & 1u) {
                ovl_last = (uint8_t)i;
                if (set.ovl_latch)
                    ovl_latched ^= 1u << i;
                app_say(OVL_NAME[i], set.ovl_latch ? (ovl_latched >> i & 1u ? "ON" : "OFF") : "");
                ui.redraw |= RG_BLOCKS | RG_FOOT;   /* (K1: its amount) */
            }
            if (!set.ovl_latch && keys >> WK[i] & 1u)
                held |= 1u << i;
        }
    if (held != ovl_held)
        ui.redraw |= RG_BLOCKS;
    ovl_held = held;
    ovl_push();
}
static void ovl_value(int i, char *t)
{
    t[0] = 0;
    if (i == 0) {
        int_text(set.ovl_amt[ovl_last & 15u], t);
        text_cat(t, " %");
    } else if (i == 1)
        text_cat(t, OVL_RATE_NAME[set.ovl_rate & 3u]);
    else if (i == 2)
        text_cat(t, OVL_SWEEP_NAME[set.ovl_sweep & 3u]);
    else
        text_cat(t, set.ovl_latch ? "ON" : "OFF");
}
static void ovl_turn(int i, int32_t d)
{
    char t[16];
    if (i == 0) {
        uint8_t *a = &set.ovl_amt[ovl_last & 15u];
        *a = (uint8_t)clamp((int32_t)*a + d * 2, 0, 100);
    } else if (i == 1)
        set.ovl_rate = (uint8_t)clamp((int32_t)set.ovl_rate + (d > 0 ? 1 : -1), 0, 3);
    else if (i == 2)
        set.ovl_sweep = (uint8_t)clamp((int32_t)set.ovl_sweep + (d > 0 ? 1 : -1), 0, 3);
    else {
        set.ovl_latch = d > 0 ? 1u : 0u;
        if (!set.ovl_latch)
            ovl_latched = 0u;            /* (back to held: what was latched lets go) */
    }
    changed();
    ovl_push();
    ovl_value(i, t);
    app_say(i == 0 ? OVL_NAME[ovl_last & 15u] : i == 1 ? "RATE" : i == 2 ? "SWEEP" : "LATCH", t);
    ui.redraw |= RG_BLOCKS;
}

/* the OVERLAY page's strip: sixteen cards in key order, four by four; one sounding lit, one latched outlined amber,
 * the one K1 tunes outlined white; each with a bar of its amount and its key */
static void draw_ovl(void)
{
    uint32_t band, i, on = ovl_sounding() | ovl_held | ovl_latched;
    char t[4];
    for (band = 0; band < 2u; band++) {
        cv_begin(240, 52, C_BLACK);
        for (i = band * 8u; i < band * 8u + 8u; i++) {
            int32_t x = 1 + (int32_t)(i % 4u) * 60, y = (int32_t)((i / 4u) & 1u) * 26;
            int lit = on >> i & 1u, lat = ovl_latched >> i & 1u, sel = i == ovl_last;
            cv_rect(x, y, 57, 24, lit ? C_HI : lat ? C_AMB : sel ? C_WHITE : C_LINE);
            cv_rect(x + 1, y + 1, 55, 22, lit ? C_HI : C_BLOCK);
            t[0] = 0;
            int_text((int32_t)i + 1, t);
            cv_text(x + 4, y + 1, &FONT_XS, OVL_NAME[i], lit ? C_BLACK : sel ? C_HI : C_GRAY);
            cv_text(x + 54 - text_w(&FONT_T, t), y + 12, &FONT_T, t, lit ? C_BLACK : C_DIM);   /* its key */
            cv_rect(x + 4, y + 18, 34, 2, lit ? C_BLOCK_HI : C_LINE);                         /* its amount */
            cv_rect(x + 4, y + 18, (int32_t)set.ovl_amt[i] * 34 / 100, 2, lit ? C_BLACK : lat ? C_AMB : C_GRAY);
        }
        cv_blit(0, 88u + band * 52u);
    }
}
