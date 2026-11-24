/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the GLOBAL page: MIDI clock, Bluetooth MIDI, pitch, snap, RESET ALL, the channel; and HOME's third part,
 * the split. (Part of app.c's unity: included there in order.) */

static void reset_all(void);             /* (below, with the break's change) */
#define GPARTS 3u                        /* MIDI, PITCH, CHANNEL */
static const char *const MO_NAME[3] = {"NOTES", "CC", "PC"};
/* the GLOBAL page's knobs: clock in, clock out, Bluetooth, where MIDI goes */
static const char *const MIDI_TO_NAME[3] = {"USB+BT", "USB", "BT"};
static void global_value(int i, char *t)
{
    t[0] = 0;
    if (ui.gpage == 2u) {                /* CHANNEL: in (and out), and what goes out */
        if (i == 0) {
            if (set.midi_ch)
                int_text(set.midi_ch, t);
            else
                text_cat(t, "OMNI");
        } else
            text_cat(t, set.midi_out >> (i - 1) & 1u ? "ON" : "OFF");
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
    if (ui.gpage == 2u) {                /* K1 the channel (OMNI, 1..16), K2..K4 notes, CC, program changes out */
        if (i == 0)
            set.midi_ch = (uint8_t)clamp((int32_t)set.midi_ch + d, 0, 16);
        else if (d > 0)
            set.midi_out |= (uint8_t)(1u << (i - 1));
        else
            set.midi_out &= (uint8_t)~(1u << (i - 1));
        global_value(i, t);
        app_say(i ? MO_NAME[i - 1] : "CHANNEL", t);
        ui.redraw |= RG_BLOCKS;
        changed();
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
                app_warn("RESET ALL?", "TURN K4 AGAIN");
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
    if (ui.gpage == 2u) {                /* CHANNEL: the channel, what goes out, the map */
        cv_begin(240, 52, C_BLACK);
        cv_text(6, 2, &FONT_T, "CHANNEL", C_GRAY);
        global_value(0, t);
        cv_text(96, 2, &FONT_T, t, C_HI);
        cv_text(96, 14, &FONT_XS, set.midi_ch ? "IN AND OUT ON IT" : "IN: ANY. OUT: 1", C_DIM);
        cv_text(6, 30, &FONT_T, "OUT", C_GRAY);
        for (p = 0; p < 3u; p++) {
            int32_t x = 96 + (int32_t)p * 48;
            int on = set.midi_out >> p & 1u;
            cv_rect(x, 28, 44, 18, on ? C_HI : C_BLOCK);
            cv_text(x + 4, 30, &FONT_T, MO_NAME[p], on ? C_BLACK : C_DIM);
        }
        cv_blit(0, 88);
        cv_begin(240, 52, C_BLACK);      /* the map, in short */
        cv_text(6, 2, &FONT_XS, "NOTES 48-63 SLICES  64-74 PADS", C_WHITE);
        cv_text(6, 14, &FONT_XS, "24-35 BREAKS  36 K 38 S 42 H 39 P SLICES", C_WHITE);
        cv_text(6, 26, &FONT_XS, "CC 20-23 K1-K4  24-39 FX SLOTS 1-4", C_WHITE);
        cv_text(6, 38, &FONT_XS, "PC 0-15 PATTERN  16-31 BREAK", C_WHITE);
        cv_blit(0, 140);
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

/* HOME's third part, the SPLIT (PRD S-030): K1 on / off, K2 break B, K3 A's bar, K4 B's bar; the panel shows the two
 * breaks and their bars, and how the keys work */
static void split_value(int i, char *t)
{
    t[0] = 0;
    if (i == 0)
        text_cat(t, set.split ? "ON" : "OFF");
    else if (i == 1)
        text_cat(t, break_name(split_b_of(set.brk)));
    else
        text_cat(t, set.split_bars >> (i - 2) & 1u ? "BAR 2" : "BAR 1");
}

static void split_turn(int i, int32_t d)
{
    char t[16];
    static const char *const SN[4] = {"SPLIT", "BREAK B", "BAR OF A", "BAR OF B"};
    uint32_t was = set.split | (uint32_t)set.split_b << 8 | (uint32_t)set.split_bars << 16;
    if (i == 0)
        set.split = d > 0;
    else if (i == 1)
        set.split_b = (uint8_t)((split_b_of(set.brk) + (d > 0 ? 1u : n_breaks - 1u)) % n_breaks + 1u);
    else if (d > 0)
        set.split_bars |= (uint8_t)(1u << (i - 2));
    else
        set.split_bars &= (uint8_t)~(1u << (i - 2));
    split_value(i, t);
    app_say(SN[i], t);
    if (was != (set.split | (uint32_t)set.split_b << 8 | (uint32_t)set.split_bars << 16)) {
        if (!set.split)
            brk_half(-1);
        if (set.split || i == 0)         /* the break again, at the next bar (break_poll) */
            break_ask(set.brk);
    }
    ui.redraw |= RG_BLOCKS;
    changed();
}

static void draw_split(void)
{
    char t[32];
    const brk_break_t *b = brk_get_break();
    uint32_t k;
    cv_begin(240, 52, C_BLACK);
    for (k = 0; k < 2u; k++) {
        int32_t y = 2 + (int32_t)k * 26;
        t[0] = 0;
        text_cat(t, break_name(k ? split_b_of(set.brk) : set.brk));
        cv_text(6, y, &FONT_T, k ? "BREAK B" : "BREAK A", C_GRAY);
        cv_text(6, y + 12, &FONT_XS, k ? "BAR 2: KEYS 9-16" : "BAR 1: KEYS 1-8", C_DIM);
        cv_text(96, y, &FONT_T, t, set.split ? C_HI : C_DIM);
        cv_text(96, y + 12, &FONT_XS, set.split_bars >> k & 1u ? "ITS SECOND BAR" : "ITS FIRST BAR", C_DIM);
    }
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    cv_text(6, 2, &FONT_T, "SPLIT", C_GRAY);
    cv_text(96, 2, &FONT_T, !set.split ? "OFF" : b && b->split ? "ON" : "WAITING", set.split ? C_HI : C_DIM);
    cv_text(6, 20, &FONT_XS, "A KEY: LOOP ITS BAR.  SEL + KEY: BOTH", set.split ? C_WHITE : C_DIM);
    cv_text(6, 32, &FONT_XS, "AN A KEY AND A B KEY HELD: LAYERED", set.split ? C_WHITE : C_DIM);
    cv_blit(0, 140);
}

/* HOME's fourth part, LOOP (1.7, PRD_PACKS PS-003): a pack break's own loop, as its pack set it and as you change it
 * here (saved with SAVE, per pack): K1 LENGTH, the window (2 bars .. 1/4 bar), K2 its first step, K3 pitch, K4 gain. SEL + a
 * knob: back to the pack's. The break follows at the next bar. Built-in breaks: as they are */
static const char *const LOOP_LEN[4] = {"2 BARS", "1 BAR", "1/2 BAR", "1/4 BAR"};
static uint32_t loop_len_i(uint32_t steps) { return steps >= 32u ? 0u : steps >= 16u ? 1u : steps >= 8u ? 2u : 3u; }
static void loop_value(int i, char *t)
{
    const bank_sset_t *ss = sset_of(set.brk);
    t[0] = 0;
    if (!ss) {
        text_cat(t, i ? "" : "BUILT IN");
        return;
    }
    if (i == 0)
        text_cat(t, LOOP_LEN[loop_len_i(ss->loop_steps)]);
    else if (i == 1) {
        text_cat(t, "STEP ");
        int_text((int32_t)ss->loop_start + 1, t + 5);
    } else {
        int32_t v = i == 2 ? ss->pitch : ss->gain_db;
        if (v > 0)
            text_cat(t, "+");
        int_text(v, t + (v > 0));
        if (i == 3)
            text_cat(t, " DB");
    }
}
static void loop_turn(int i, int32_t d)
{
    char t[16];
    static const char *const LN[4] = {"LENGTH", "FROM", "PITCH", "GAIN"};
    bank_sset_t *ss = set.brk < n_user && set.brk < BANK_SLOTS ? &mq.ss[set.brk] : 0;
    if (!ss) {
        app_say("LOOP", "PACK BREAKS ONLY");
        return;
    }
    if (ui.btn & 1u << B_SEL) {          /* SEL + a knob: the pack's own again */
        ss_from_pack(set.brk);
        app_say("LOOP", "AS THE PACK HAS IT");
    } else {
        if (i == 0)
            ss->loop_steps = (uint8_t)(32u >> clamp((int32_t)loop_len_i(ss->loop_steps) + (d > 0 ? 1 : -1), 0, 3));
        else if (i == 1)
            ss->loop_start = (uint8_t)clamp((int32_t)ss->loop_start + d, 0, 32 - (int32_t)ss->loop_steps);
        else if (i == 2)
            ss->pitch = (int8_t)clamp((int32_t)ss->pitch + d, -24, 24);
        else
            ss->gain_db = (int8_t)clamp((int32_t)ss->gain_db + d, -24, 12);
        if (ss->loop_start + ss->loop_steps > 32u)
            ss->loop_start = (uint8_t)(32u - ss->loop_steps);
        loop_value(i, t);
        app_say(LN[i], t);
    }
    break_ask(set.brk);                  /* the break again, at the next bar */
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void draw_loop(void)
{
    const bank_sset_t *ss = sset_of(set.brk);
    char t[24];
    uint32_t k;
    cv_begin(240, 52, C_BLACK);
    cv_text(6, 2, &FONT_T, "BREAK", C_GRAY);
    cv_text(96, 2, &FONT_T, break_name(set.brk), C_HI);
    for (k = 0; k < 32u; k++) {          /* the two bars' 16ths, the window lit */
        int in = ss && k >= ss->loop_start && k < (uint32_t)ss->loop_start + ss->loop_steps;
        cv_rect(6 + (int32_t)k * 7, 24, 6, 14, !ss || in ? C_BLOCK_HI : C_BLOCK);
        if (!(k & 3u))
            cv_rect(6 + (int32_t)k * 7, 40, 1, 4, k & 15u ? C_LINE : C_GRAY);
    }
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    if (!ss)
        cv_text(6, 4, &FONT_XS, "A PACK BREAK'S OWN LOOP, PITCH AND GAIN: LOAD A PACK", C_DIM);
    else {
        for (k = 0; k < 4u; k++) {
            static const char *const LN[4] = {"LENGTH", "FROM", "PITCH", "GAIN"};
            loop_value((int)k, t);
            cv_text(6 + (int32_t)k * 58, 2, &FONT_XS, LN[k], C_GRAY);
            cv_text(6 + (int32_t)k * 58, 14, &FONT_T, t, C_HI);
        }
        cv_text(6, 36, &FONT_XS, "SEL + A KNOB: AS THE PACK HAS IT", C_DIM);
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
