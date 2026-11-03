/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the GLOBAL page: MIDI clock, Bluetooth MIDI, pitch, snap, RESET ALL. (Part of app.c's unity: included
 * there in order.) */

static void reset_all(void);             /* (below, with the break's change) */
/* the GLOBAL page's knobs: clock in, clock out, Bluetooth, where MIDI goes */
static const char *const MIDI_TO_NAME[3] = {"USB+BT", "USB", "BT"};
static void global_value(int i, char *t)
{
    t[0] = 0;
    if (ui.gpage == 2u) {
        ch_bta_value(i, t);
        return;
    }
    if (ui.gpage) {                      /* the pitch: speed or keep length, the grains */
        if (i == 0)
            text_cat(t, pitch_keeps() ? "KEEP LEN" : "SPEED");
        else if (i == 1) {
            int_text(set.grain_ms ? set.grain_ms : 30, t);
            text_cat(t, " MS");
        } else if (i == 2)
            text_cat(t, SNAP_SHORT[snap_index()]);
        else
            text_cat(t, ui.reset_t && plat_ms() - ui.reset_t < 3000u ? "AGAIN!" : "TURN 2X");
        return;
    }
    text_cat(t, i == 0 ? (set.clk_in ? "FOLLOW" : "OFF") : i == 1 ? (set.clk_out ? "ON" : "OFF")
                : i == 2 ? (set.ble_off ? "OFF" : "ON") : MIDI_TO_NAME[set.midi_to % 3u]);
}

static void global_turn(int i, int32_t d)
{
    static const char *const NAME[4] = {"CLOCK IN", "CLOCK OUT", "BLUETOOTH", "MIDI TO"};
    char t[16];
    if (ui.gpage == 2u) {
        ch_bta_turn(i, d);
        return;
    }
    if (ui.gpage) {
        if (i == 0)
            set.pitch_keep = d > 0 ? 1u : 2u;   /* right KEEP LENGTH, left SPEED */
        else if (i == 1)
            set.grain_ms = (uint8_t)clamp((int32_t)(set.grain_ms ? set.grain_ms : 30u) + d * 5, 10, 80);
        else if (i == 2)
            set.snap = (uint8_t)(clamp((int32_t)snap_index() + (d > 0 ? 1 : -1), 0, SNAPS - 1) + 1);
        else {                           /* RESET ALL: a turn asks, a second within 3 s clears */
            if (ui.reset_t && plat_ms() - ui.reset_t < 3000u) {
                ui.reset_t = 0u;
                reset_all();
            } else {
                ui.reset_t = plat_ms() | 1u;
                app_say("RESET ALL?", "TURN K4 AGAIN");
            }
            foot_dirty = 1u;
            return;
        }
        global_value(i, t);
        app_say(i == 0 ? "PITCH" : i == 1 ? "GRAIN" : "SNAP", i == 0 && pitch_keeps() ? "KEEP LENGTH"
                : i == 2 ? SNAP_NAME[snap_index()] : t);
        changed();
        return;
    }
    if (i == 0) {
        set.clk_in = d > 0;
        if (!set.clk_in) {               /* the tempo is the knob's again */
            ext.i = ext.n = 0u;
            ext.run = 0u;
            ext.bpm = 0.0f;
        }
    } else if (i == 1)
        set.clk_out = d > 0;
    else if (i == 2) {
        uint8_t off = d < 0;
        if (off != set.ble_off) {
            set.ble_off = off;
            plat_ble_enable(!off);       /* off ends the link and stops advertising */
        }
    } else
        set.midi_to = (uint8_t)clamp((int32_t)set.midi_to + (d > 0 ? 1 : -1), 0, 2);
    global_value(i, t);
    app_say(NAME[i], t);
    changed();
}

/* the GLOBAL page's panel: Bluetooth, the clock coming in, the clock going out, MIDI through each port */
static void draw_global(const brk_status_t *st)
{
    static const char *const PORT_NAME[NPORTS] = {"USB", "BT", "TRS"};
    char t[32], v[16];
    uint32_t p;
    int bs = plat_ble_state();
    if (ui.gpage == 2u) {
        draw_bta();
        return;
    }
    if (ui.gpage) {                      /* the pitch: what the mode does, the pitch now */
        cv_begin(240, 52, C_BLACK);
        cv_text(6, 2, &FONT_T, "PITCH", C_GRAY);
        cv_text(96, 2, &FONT_T, pitch_keeps() ? "KEEP LENGTH" : "SPEED", C_HI);
        cv_text(96, 16, &FONT_XS, pitch_keeps() ? "SLICES KEEP LENGTH" : "PITCH CHANGES LENGTH", C_DIM);
        cv_text(6, 30, &FONT_T, "NOW", C_GRAY);
        t[0] = set.pitch > 0 ? '+' : 0;
        t[1] = 0;
        int_text(set.pitch, t + (set.pitch > 0));
        text_cat(t, " ST  (OCT- / OCT+)");
        cv_text(96, 30, &FONT_T, t, C_WHITE);
        cv_blit(0, 88);
        cv_begin(240, 52, C_BLACK);
        cv_text(6, 2, &FONT_T, "GRAIN", C_GRAY);
        int_text(set.grain_ms ? set.grain_ms : 30, t);
        text_cat(t, " MS");
        cv_text(96, 2, &FONT_T, t, pitch_keeps() ? C_WHITE : C_DIM);
        cv_text(96, 14, &FONT_XS, "LOWER: TIGHTER HITS", C_DIM);
        cv_text(6, 30, &FONT_T, "SNAP", C_GRAY);   /* where a key lands, and what that is in this break */
        t[0] = 0;
        text_cat(t, SNAP_NAME[snap_index()]);
        if (snap_index()) {
            const brk_break_t *b = brk_get_break();
            text_cat(t, " = ");
            sixteenths_text(SNAP_SL[snap_index()] * key_sps(b), v);
            text_cat(t, v);
        }
        cv_text(96, 30, &FONT_T, t, C_HI);
        cv_blit(0, 140);
        return;
    }
    cv_begin(240, 52, C_BLACK);          /* Bluetooth, and the clock in */
    cv_text(6, 2, &FONT_T, "BLUETOOTH", C_GRAY);
    t[0] = 0;
    text_cat(t, set.ble_off ? "OFF" : bs == BLE_STARTING ? "STARTING" : bs == BLE_FAILED ? "FAILED"
                : plat_ble_connected() ? "CONNECTED" : plat_ble_linked() ? "LINKING" : bs == BLE_READY
                ? "READY TO PAIR" : "OFF");
    cv_text(96, 2, &FONT_T, t, plat_ble_connected() ? C_HI : set.ble_off ? C_DIM : C_WHITE);
    if (!set.ble_off && !plat_ble_connected())
        cv_text(96, 14, &FONT_XS, "NAME: CHOPPAH ....", C_DIM);
    cv_text(6, 30, &FONT_T, "CLOCK IN", C_GRAY);
    t[0] = 0;
    if (!set.clk_in)
        text_cat(t, "OFF");
    else if (!ext_active())
        text_cat(t, "WAITING FOR CLOCK");
    else {
        tenths_text((int32_t)(ext.bpm * 10.0f + 0.5f), v);
        text_cat(t, v);
        text_cat(t, " BPM ");
        text_cat(t, PORT_NAME[ext.port % NPORTS]);
        text_cat(t, ext.run ? "  RUNNING" : "  STOPPED");
    }
    cv_text(96, 30, &FONT_T, t, ext_active() ? C_HI : set.clk_in ? C_WHITE : C_DIM);
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);          /* the clock out, and MIDI through each port (lit: in the last 0.3 s) */
    cv_text(6, 2, &FONT_T, "CLOCK OUT", C_GRAY);
    t[0] = 0;
    if (!set.clk_out)
        text_cat(t, "OFF");
    else {
        text_cat(t, "TO ");
        text_cat(t, MIDI_TO_NAME[set.midi_to % 3u]);
        text_cat(t, st->playing ? "  SENDING" : "  (WHILE PLAYING)");
    }
    cv_text(96, 2, &FONT_T, t, set.clk_out && st->playing ? C_HI : set.clk_out ? C_WHITE : C_DIM);
    cv_text(6, 30, &FONT_T, "MIDI", C_GRAY);
    for (p = 0; p < NPORTS; p++) {
        int32_t x = 96 + (int32_t)p * 48;
        int in = plat_midi_ago((int)p, 0) < 300u, out = p != PORT_TRS && plat_midi_ago((int)p, 1) < 300u;
        cv_rect(x, 28, 44, 18, C_BLOCK);
        cv_text(x + 4, 30, &FONT_T, PORT_NAME[p], in || out ? C_WHITE : C_DIM);
        cv_rect(x + 34, 31, 4, 4, in ? C_HI : C_LINE);       /* in */
        cv_rect(x + 34, 38, 4, 4, out ? C_HI : C_LINE);      /* out */
    }
    cv_blit(0, 140);
}

static void global_part(uint32_t g)      /* GLO (again: the next part) and ALGORITHM on GLOBAL */
{
    ui.gpage = (uint8_t)g;
    ui.page = PG_GLOBAL;
    ui.redraw |= RG_FOOT | RG_BLOCKS;
    page_say();
}
