/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the controls (knobs, keys, buttons), the lights, the foot and the frame. (Part of app.c's unity:
 * included there in order.) */

static void knob_turn(int k, int32_t d)
{
    brk_fx_t *f = &set.fx;
    if (ui.page == PG_GLOBAL) {
        global_turn(k - EN_K1, d);
        return;
    }
    if (ui.page == PG_KICK) {
        kick_knob_turn(k - EN_K1, d);
        return;
    }
    if (ui.page == PG_SEQ) {
        seq_turn(k - EN_K1, d);
        return;
    }
    if (ui.page == PG_PERC) {
        perc_turn(k - EN_K1, d);
        return;
    }
    if (ui.page == PG_MIX) {             /* LEVELS: break, kick, snare and hat, FX; GROOVE: swing, the drums' route */
        char t[16];
        if (ui.mpage) {
            if (k == EN_K1) {
                set.swing = (uint8_t)clamp((int32_t)set.swing + d, 50, 75);
                say_int("SWING", set.swing, "%");
            } else if (k == EN_K2) {
                set.drums_chain = d > 0 ? 0u : 1u;   /* right: around the chain, left: through it */
                app_say("DRUMS", set.drums_chain ? "THRU FX" : "AROUND FX");
            } else
                return;
        } else if (k == EN_K1) {
            set.brk_level = (uint8_t)clamp((int32_t)set.brk_level + d * 2, 0, 100);
            say_int("BREAK", set.brk_level, "%");
        } else if (k == EN_K2) {
            set.kick.volume = knob_f(set.kick.volume, d, 0.02f, 0.0f, 1.0f);
            pct_text(set.kick.volume, t);
            app_say("KICK", t);
        } else if (k == EN_K3) {         /* PERC: the snare and the hat */
            set.perc.volume = knob_f(set.perc.volume, d, 0.02f, 0.0f, 1.0f);
            say_int("PERC", (int32_t)(set.perc.volume * 100.0f + 0.5f), "%");
        } else {                         /* FX: the whole chain against the dry sound */
            f->wet = knob_f(f->wet, d, 0.02f, 0.0f, 1.0f);
            say_int("FX", (int32_t)(f->wet * 100.0f + 0.5f), "%");
        }
        ui.redraw |= RG_BLOCKS;
    } else if (ui.page == PG_HOME) {
        if (k == EN_K1) {                /* the gate: how much of every slice sounds (complexity: GENERATOR) */
            set.gate = (uint8_t)clamp((int32_t)(set.gate ? set.gate : 100u) + d * 5, 5, 100);
            if (set.gate >= 100u)
                app_say("GATE", "OFF");
            else
                say_int("GATE", set.gate, "%");
        } else if (k == EN_K2) {         /* the break's DJ filter: left low-pass, right high-pass */
            f->dj = knob_f(f->dj, d, 0.02f, -1.0f, 1.0f);
            say_int("DJ FILTER", (int32_t)(f->dj * 100.0f), "");
        } else
            return;
    } else if (ui.page == PG_GEN) {
        float *p = k == EN_K1 ? &set.gen.complexity : k == EN_K2 ? &set.gen.anchor : k == EN_K3 ? &set.gen.roll
                 : &set.gen.fill;
        static const char *const N[4] = {"COMPLEXITY", "ANCHOR", "ROLL", "FILL"};
        *p = knob_f(*p, d, 0.02f, 0.0f, 1.0f);
        say_int(N[k - EN_K1], (int32_t)(*p * 100.0f), "%");
    } else {                             /* FX: the focused slot's three settings and its mix */
        fx_knob_turn(ui.fx_focus, k - EN_K1, d);
        return;
    }
    changed();
}

/* a break change (ALGORITHM): its name at once, the break itself at the next bar. Playing, break_poll waits for
 * the bar's last 16th, mutes the break (it runs on, the clock and the drums too), decodes the new one over pcm_a
 * once the fade is done, and the render swaps it in on the next step: the bar's first. Stopped: at once */
static void break_ask(uint32_t i)
{
    set.brk = (uint8_t)i;                /* turning on fast just moves the target */
    brk_next = (int32_t)i;
    set_dirty = 1u;
    app_say(playing_now() ? "NEXT BREAK" : "BREAK", break_name(i));
    ui.redraw |= RG_HEAD;
}

/* GLOBAL's RESET ALL: every setting and pattern to the start, saved (the Bluetooth pairings stay: OBJ_BLE) */
static void reset_all(void)
{
    uint8_t ble_was_off = set.ble_off;
    settings_defaults();
    if (n_breaks > n_synth)
        set.brk = (uint8_t)n_synth;      /* the bank's first break */
    pat_defaults();
    if (ble_was_off)
        plat_ble_enable(1);              /* (Bluetooth MIDI on again, as at the start) */
    break_ask(set.brk);
    settings_apply();
    pat_apply();
    pat_next = -1;
    (void)plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    (void)plat_store_save(OBJ_PAT, &pats, sizeof pats);
    set_dirty = 0u;
    app_say("ALL CLEARED", "");
    ui.redraw |= RG_ALL;
}
static void break_poll(void)
{
    static uint32_t mark;
    static uint8_t muting;
    if (brk_next < 0)
        return;
    if (!muting) {
        brk_status_t st;
        brk_status(&st);
        if (st.playing && st.bar16 >= 0 && st.bar16 != 15)
            return;                      /* playing: not before the bar's last 16th */
        brk_mute(1);
        mark = brk_renders();
        muting = 1u;
        return;
    }
    if (brk_renders() - mark < 2u)       /* the fade out done */
        return;
    brk_swap_break(break_load((uint32_t)brk_next));
    brk_next = -1;
    muting = 0u;
    ui.redraw |= RG_ALL;
}

/* white keys held into a page where they don't play slices: let the engine's loops go */
static void slices_let_go(uint32_t keys)
{
    uint32_t i;
    if (!slices_page())
        return;
    for (i = 0; i < 16u; i++)
        if (keys >> WK[i] & 1u)
            brk_key((int)i, 0);
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms(), i;
    uint32_t kdn = keys & ~ui.keys, kup = ui.keys & ~keys, bdn = btn & ~ui.btn;
    int k;
    if (keys != ui.keys || btn != ui.btn)
        ui.touched = 1u;                 /* (read here too between frames: app_input_only) */
    for (i = 0; i < 16u; i++) {          /* white keys: slices, or on the soundboard its sounds */
        if (ui.page == PG_KICK) {        /* the KICK page: the kick's 16 steps, on and off */
            if (kdn >> WK[i] & 1u)
                lane_toggle(LANE_KICK, i);
            continue;
        }
        if (ui.page == PG_PERC) {        /* the SNARE / HAT page: that voice's steps */
            if (kdn >> WK[i] & 1u)
                lane_toggle(LANE_SNARE + ui.pvoice, i);
            continue;
        }
        if (ui.page == PG_SEQ) {         /* the SEQ page: a break step to edit, or a drum lane's steps */
            if (kdn >> WK[i] & 1u)
                seq_key(i);
            continue;
        }
        if (kdn >> WK[i] & 1u) {
            if (ui.rec && playing_now())
                rec_slice(i);            /* REC: into the pattern too */
            brk_key((int)i, 1);
            ui.key_focus = (int8_t)i;    /* the engine loops the key pressed last */
            ui.key_since = now;
            ui.key_turned = 0u;
        }
        if (kup >> WK[i] & 1u)
            brk_key((int)i, 0);
    }
    if (ui.key_focus >= 0 && (!slices_page() || !(keys >> WK[ui.key_focus] & 1u))) {
        ui.key_focus = -1;               /* let go: the lowest key still held loops (as in the engine), or none */
        if (slices_page())
            for (i = 16u; i-- > 0u;)
                if (keys >> WK[i] & 1u) {
                    ui.key_focus = (int8_t)i;
                    ui.key_since = now;
                }
    }
    for (i = 0; i < 11u; i++) {          /* black keys: the pads, held; the last one pressed has the screen */
        if (kdn >> BK[i] & 1u) {
            brk_pad((int)i, 1);
            ui.pad_focus = (int8_t)i;
            ui.pad_since = now;
        }
        if (kup >> BK[i] & 1u)
            brk_pad((int)i, 0);
    }
    if (ui.pad_focus >= 0 && !(keys >> BK[ui.pad_focus] & 1u)) {   /* let go: another pad still held, or none */
        ui.pad_focus = -1;
        for (i = 0; i < 11u; i++)
            if (keys >> BK[i] & 1u)
                ui.pad_focus = (int8_t)i;
    }
    if (bdn & 1u << B_PLAY) {
        brk_status_t st;
        brk_status(&st);
        if (btn & 1u << B_HOME) {        /* HOME + PLAY: back to bar 1 */
            transport(st.playing, 1);
            app_say("RESET", "");
        } else
            transport(!st.playing, 0);
        muted = 0u;
    }
    if (bdn & 1u << B_HOME) {
        ui.page = PG_HOME;
        ui.redraw |= 8u;               /* RG_FOOT: the page */
    }
    if (bdn & 1u << B_GLO) {             /* GLOBAL: MIDI clock and Bluetooth; again, the pitch; again, BT audio */
        ui.gpage = ui.page == PG_GLOBAL ? (uint8_t)((ui.gpage + 1u) % 3u) : 0u;
        ui.page = PG_GLOBAL;
        ui.redraw |= 8u | 4u;            /* RG_FOOT | RG_BLOCKS */
        app_say("GLOBAL", ui.gpage == 2u ? "BT AUDIO" : ui.gpage ? "PITCH, SNAP" : "MIDI");
    }
    if (bdn & 1u << B_EDIT && btn & 1u << B_SEL) {   /* SEL + EDIT: the drums through the FX chain, or around it */
        set.drums_chain ^= 1u;
        app_say("DRUMS", set.drums_chain ? "THRU FX" : "AROUND FX");
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        changed();
    } else if (bdn & 1u << B_EDIT) {     /* EDIT: the KICK page; again, its next sub-page */
        if (ui.page == PG_KICK)
            ui.kpage = (uint8_t)((ui.kpage + 1u) % KPAGES);
        else {
            slices_let_go(keys);
            ui.kpage = KP_MIX;
        }
        ui.page = PG_KICK;
        ui.redraw |= RG_FOOT | RG_BLOCKS;
        app_say("KICK", KPAGE_NAME[ui.kpage]);
    }
    if (bdn & 1u << B_LFO) {             /* LFO: the MIXER (the break, the kick, FX main, FX overlay); again, groove */
        ui.mpage = ui.page == PG_MIX ? (uint8_t)!ui.mpage : 0u;
        ui.page = PG_MIX;
        ui.redraw |= RG_FOOT | RG_BLOCKS;
        app_say("MIXER", ui.mpage ? "GROOVE" : "LEVELS");
    }
    if (bdn & 1u << B_ENV) {             /* ENV: the SNARE / HAT page; again, its next sub-page */
        if (ui.page == PG_PERC)
            ui.ppage ^= 1u;
        else {
            slices_let_go(keys);
            ui.ppage = 0u;
        }
        ui.page = PG_PERC;
        ui.redraw |= RG_FOOT | RG_BLOCKS;
        app_say(PERC_VOICE_NAME[ui.pvoice % PERC_VOICES], ui.ppage ? "SHAPE" : "SOUND");
    }
    if (bdn & 1u << B_SEQ) {             /* SEQ: the pattern's steps and lanes */
        if (ui.page != PG_SEQ)
            slices_let_go(keys);
        ui.page = PG_SEQ;
        ui.redraw |= RG_FOOT | RG_BLOCKS;
        app_say("SEQ", SEQ_LANE_NAME[ui.seq_lane % NSEQL]);
    }
    if (bdn & 1u << B_REC && btn & 1u << B_SEL) {   /* SEL + REC: clear (on SEQ its lane; else the pattern) */
        seq_clear(ui.page == PG_SEQ);
    } else if (bdn & 1u << B_REC) {      /* REC: record the keys and MIDI notes into the pattern while playing */
        ui.rec ^= 1u;
        app_say("REC", ui.rec ? "ON" : "OFF");
        ui.redraw |= RG_HEAD;
    }
    if (bdn & 1u << B_ARP) {
        if (btn & 1u << B_SEL) {         /* SEL + ARP: a random pattern */
            brk_randomize();
            pat_read_back();             /* (into the pattern playing: SAVE keeps it) */
            set_dirty = 1u;
            app_say("RANDOMIZE", "");
        }
        ui.page = PG_GEN;
        ui.redraw |= 8u;               /* RG_FOOT: the page */
    }
    if (bdn & 1u << B_FX && key_tuned() >= 0) {   /* FX with a key held: its knobs, loop <-> slice effect */
        ui.key_layer ^= 1u;
        ui.key_turned = 1u;
        app_say("KEY", ui.key_layer ? "SLICE FX" : "LOOP");
        foot_dirty = 1u;
    } else if (bdn & 1u << B_FX) {       /* FX: the chain's page; again: the focused slot on / bypassed */
        if (ui.page == PG_FX) {
            brk_fxslot_t *sl = &set.fx.slot[ui.fx_focus & 3u];
            if (sl->type == FXT_EMPTY)
                app_say("EMPTY", "PRESETS");
            else {
                sl->on ^= 1u;
                app_say(BRK_FXT_NAME[sl->type], sl->on ? "ON" : "BYPASS");
                changed();
            }
        } else
            app_say("FX CHAIN", "");
        ui.page = PG_FX;
        ui.redraw |= 8u | 4u;            /* RG_FOOT | RG_BLOCKS */
    }
    if (bdn & 1u << B_SEL && key_tuned() >= 0) {   /* SEL while a key is held: its loop back to the defaults */
        if (ui.key_layer)
            brk_slicefx_defaults(&set.slx[key_tuned()]);
        else
            brk_keyset_defaults(&set.keys[key_tuned()]);
        ui.key_turned = 1u;
        changed();
        app_say(ui.key_layer ? "SLICE FX" : "KEY", "RESET");
    } else if (bdn & 1u << B_SEL) {      /* a tap, if it comes up alone (below) */
        ui.tap_pend = 1u;
        ui.tap_t = now;
        ui.tap_combo = (uint8_t)((btn & ~(1u << B_SEL)) != 0u);   /* (another button already held: a combination) */
    }
    if (ui.tap_pend && (btn & 1u << B_SEL) && (bdn & ~(1u << B_SEL)))
        ui.tap_combo = 1u;               /* another button with SEL held */
    if (ui.tap_pend && !(btn & 1u << B_SEL)) {   /* SEL up: alone and short, a tap at its press */
        ui.tap_pend = 0u;
        if (!ui.tap_combo && now - ui.tap_t < 800u) {
            if (ext_active())
                app_say("TEMPO", "EXT CLOCK");
            else
                tap_tempo(ui.tap_t);
        }
    }
    if (bdn & 1u << B_SAVE) {
        app_say("SAVE", plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0 &&
                        plat_store_save(OBJ_PAT, &pats, sizeof pats) == 0 ? "DONE" : "FAILED");
        set_dirty = 0u;
    }
    /* OCT- / OCT+: pitch in semitones, repeating faster while held; both: back to 0 */
    if ((btn & (1u << B_OCTDN | 1u << B_OCTUP)) == (1u << B_OCTDN | 1u << B_OCTUP)) {
        if (bdn & (1u << B_OCTDN | 1u << B_OCTUP) && set.pitch) {
            set.pitch = 0;
            changed();
            app_say("PITCH", "0");
        }
    } else if (btn & (1u << B_OCTDN | 1u << B_OCTUP)) {
        int32_t dir = btn & 1u << B_OCTUP ? 1 : -1;
        if (bdn & (1u << B_OCTDN | 1u << B_OCTUP)) {
            ui.oct_t = now + 400u;
            ui.oct_rep = 0u;
        } else if (now >= ui.oct_t) {
            ui.oct_rep++;
            ui.oct_t = now + (ui.oct_rep < 4u ? 150u : 50u);
        } else
            dir = 0;
        if (dir) {
            set.pitch = (int8_t)clamp(set.pitch + dir, -24, 24);
            changed();
            say_int("PITCH", set.pitch, " ST");
        }
    }
    for (k = 0; k < NE; k++) {
        int32_t d = plat_enc(k);
        if (!d)
            continue;
        ui.touched = 1u;
        if (btn & 1u << B_SEL)
            ui.tap_combo = 1u;           /* (SEL + a knob is no tap) */
        if (k == EN_SELECT && ext_active()) {
            app_say("TEMPO", "EXT CLOCK");   /* the outside clock has the tempo */
        } else if (k == EN_SELECT) {     /* tempo: 1 BPM a detent, 0.1 with SEL held */
            set.tempo_x10 = (uint16_t)clamp((int32_t)set.tempo_x10 + d * (btn & 1u << B_SEL ? 1 : 10), 400, 3000);
            changed();
            {
                char t[16];
                tenths_text(set.tempo_x10, t);
                app_say("BPM", t);
            }
        } else if (k == EN_ALGO) {       /* the break; on the FX page the slot (SEL held: moves it along) */
            if (ui.page == PG_KICK) {    /* the KICK page: the layer its LAYER and LAYER FX pages edit */
                ui.klayer = (uint8_t)((ui.klayer + (d > 0 ? 1u : KICK_LAYERS - 1u)) % KICK_LAYERS);
                app_say("LAYER", KICK_LAYER_NAME[ui.klayer]);
                ui.redraw |= RG_BLOCKS | RG_FOOT;
            } else if (ui.page == PG_SEQ) {   /* SEQ: the lane its keys edit */
                ui.seq_lane = (uint8_t)((ui.seq_lane + (d > 0 ? 1u : NSEQL - 1u)) % NSEQL);
                app_say("SEQ", SEQ_LANE_NAME[ui.seq_lane]);
                ui.redraw |= RG_BLOCKS | RG_FOOT;
            } else if (ui.page == PG_PERC) {   /* SNARE / HAT: the voice */
                ui.pvoice ^= 1u;
                app_say("VOICE", PERC_VOICE_NAME[ui.pvoice]);
                ui.redraw |= RG_BLOCKS | RG_FOOT;
            } else if (ui.page == PG_FX)
                fx_algo(d, btn & 1u << B_SEL);
            else {                     /* the next or previous break */
                break_ask((set.brk + (d > 0 ? 1u : n_breaks - 1u)) % n_breaks);
            }
        } else if (k == EN_PRESET && ui.page == PG_FX)
            fx_type_turn(d);             /* the focused slot's effect */
        else if (k == EN_PRESET && ui.page == PG_PERC) {   /* a built-in snare or hat */
            uint8_t *pr = &ui.ppreset[ui.pvoice % PERC_VOICES];
            *pr = (uint8_t)((*pr + (d > 0 ? 1u : PERC_PRESETS - 1u)) % PERC_PRESETS);
            perc_preset(&set.perc.v[ui.pvoice % PERC_VOICES], ui.pvoice, *pr);
            app_say(PERC_VOICE_NAME[ui.pvoice % PERC_VOICES], PERC_PRESET_NAME[ui.pvoice % PERC_VOICES][*pr]);
            ui.redraw |= RG_BLOCKS | RG_FOOT;
            changed();
        } else if (k == EN_PRESET && ui.page == PG_KICK) {   /* a built-in kick (its volume kept) */
            ui.kpreset = (uint8_t)((ui.kpreset + (d > 0 ? 1u : KICK_PRESETS - 1u)) % KICK_PRESETS);
            kick_preset(&set.kick, ui.kpreset);
            app_say("KICK", KICK_PRESET_NAME[ui.kpreset]);
            ui.redraw |= RG_BLOCKS | RG_FOOT;
            changed();
        }
        else if (k == EN_PRESET)
            pattern_pick(d);             /* elsewhere: the pattern */
        else if (key_tuned() >= 0)       /* a white key held: K1-K4 tune its loop */
            key_knob_turn(key_tuned(), k - EN_K1, d);
        else if (ui.pad_focus >= 0)      /* a pad held: K1-K4 tune it */
            pad_knob_turn(ui.pad_focus, k - EN_K1, d);
        else
            knob_turn(k, d);
    }
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

static void leds(void)
{
    brk_status_t st;
    uint32_t b = 0u, k = 0u, i;
    brk_status(&st);
    if (st.playing)
        b |= 1u << B_PLAY;
    static const uint8_t PAGE_BTN[NPAGES] = {B_HOME, B_ARP, B_FX, B_GLO, B_EDIT, B_LFO, B_SEQ, B_ENV};
    b |= 1u << PAGE_BTN[ui.page];
    if (ui.rec)
        b |= 1u << B_REC;
    if (set_dirty)
        b |= 1u << B_SAVE;
    if (!slices_page()) {                /* a lane's page: its steps lit, the step playing flipped */
        uint32_t m = keys_lit(&st);
        for (i = 0; i < 16u; i++)
            if (m >> i & 1u)
                k |= 1u << WK[i];
    } else if (st.slice >= 0 && st.slice < 16)
        k |= 1u << WK[st.slice];
    for (i = 0; i < 11u; i++)
        if (st.pads >> i & 1u)
            k |= 1u << BK[i];
    plat_glow(1);
    plat_leds(b, k);
}

/* ------------------------------------------------------------ slicer view --- */
static void knob_value(int i, char *t)  /* the page's knob i (0..3), as shown under its name */
{
    t[0] = 0;
    if (ui.page == PG_GLOBAL) {
        global_value(i, t);
        return;
    }
    if (ui.page == PG_KICK) {
        kick_value(i, t);
        return;
    }
    if (ui.page == PG_SEQ) {
        seq_value(i, t);
        return;
    }
    if (ui.page == PG_PERC) {
        perc_value(i, t);
        return;
    }
    if (ui.page == PG_MIX) {
        mix_value(ui.mpage, i, t);
        return;
    }
    if (ui.page == PG_HOME) {
        if (i == 0) {
            if (!set.gate || set.gate >= 100u)
                text_cat(t, "OFF");
            else {
                int_text(set.gate, t);
                text_cat(t, "%");
            }
        } else if (i == 1) {
            int32_t v = (int32_t)(set.fx.dj * 100.0f);
            if (v > -2 && v < 2)
                text_cat(t, "OFF");
            else {
                text_cat(t, v < 0 ? "LP " : "HP ");
                int_text(v < 0 ? -v : v, t + 3);
            }
        }
    } else if (ui.page == PG_GEN) {
        float v = i == 0 ? set.gen.complexity : i == 1 ? set.gen.anchor : i == 2 ? set.gen.roll : set.gen.fill;
        int_text((int32_t)(v * 100.0f), t);
        text_cat(t, "%");
    } else
        fx_value(ui.fx_focus, i, t);
}

/* the foot: the pads, then the page and its knobs with their values */
static void draw_foot(const brk_status_t *st)
{
    static const char *const KN[NPAGES][4] = {{"GATE", "FILTER", "", ""},
                                              {"CMPLX", "ANCHOR", "ROLL", "FILL"},
                                              {"P1", "P2", "P3", "MIX"},
                                              {"CLOCK IN", "CLOCK OUT", "BLUETOOTH", "MIDI TO"},
                                              {"", "", "", ""},
                                              {"BREAK", "KICK", "PERC", "FX"},
                                              {"SLICE", "PITCH", "HITS", "REVERSE"},
                                              {"", "", "", ""}};
    uint32_t i;
    char t[16];
    cv_begin(240, 48, C_BLACK);
    for (i = 0; i < 11u; i++) {
        int on = (st->pads >> i) & 1u;
        int32_t x = 2 + (int32_t)i * 21;
        cv_rect(x, 0, 20, 14, on ? C_HI : C_BLOCK);
        cv_text(x + (20 - text_w(&FONT_T, PAD_NAME[i])) / 2, 1, &FONT_T, PAD_NAME[i], on ? C_BLACK : C_GRAY);
    }
    for (i = 0; i < 4u; i++) {
        static const char *const KN_PITCH[4] = {"PITCH", "GRAIN", "SNAP", "RESET ALL"};
        static const char *const KN_GROOVE[4] = {"SWING", "DRUMS", "", ""};
        static const char *const KN_BTA[4] = {"BT AUDIO", "DEVICE", "SCAN", "CONNECT"};
        int bst = ui.page == PG_GLOBAL && ui.gpage == 2u && ch_bta_state() == PLAT_BTA_STREAMING;
        const char *name = ui.page == PG_GLOBAL && ui.gpage == 2u ? (bst && i == 2 ? "TEST L/R" : bst && i == 1
                           ? "CODEC" : KN_BTA[i]) : ui.page == PG_GLOBAL && ui.gpage
                           ? KN_PITCH[i] : ui.page == PG_MIX && ui.mpage
                           ? KN_GROOVE[i] : KN[ui.page][i];
        if (ui.shown >= 100) {           /* a white key held: its loop, or its slice's effect */
            cv_text(4 + (int32_t)i * 60, 18, &FONT_T, ui.key_layer ? SLX_KNOB[i] : KEY_KNOB[i], C_HI);
            if (ui.key_layer)
                slx_knob_value(ui.shown - 100, (int)i, t);
            else
                key_knob_value(ui.shown - 100, (int)i, t);
            cv_text(4 + (int32_t)i * 60, 30, &FONT_XS, t, C_WHITE);
            continue;
        }
        if (ui.shown >= 0) {             /* a pad held: its own settings */
            name = PAD_KNOB[ui.shown][i];
            if (!name)
                continue;
            if (ui.shown == PAD_DUB && delay_slot() >= 0)
                name = fx_param_name((uint32_t)delay_slot(), (int)i);   /* (TIME FEEDBK TONE SEND) */
            cv_text(4 + (int32_t)i * 60, 18, &FONT_T, name, C_HI);
            pad_knob_value(ui.shown, (int)i, t);
            cv_text(4 + (int32_t)i * 60, 30, &FONT_XS, t, C_WHITE);
            continue;
        }
        if (ui.page == PG_FX)            /* the focused slot's effect: its settings, then MIX */
            name = fx_param_name(ui.fx_focus, (int)i);
        else if (ui.page == PG_KICK)
            name = kick_knob_name((int)i);
        else if (ui.page == PG_PERC)
            name = PERC_KNOB[ui.pvoice % PERC_VOICES][ui.ppage][i];
        else if (ui.page == PG_SEQ && ui.seq_lane >= SL_KICK)
            name = "";
        cv_text(4 + (int32_t)i * 60, 18, &FONT_T, name, ui.page == PG_FX
                ? (set.fx.slot[ui.fx_focus & 3u].on ? C_HI : C_DIM) : C_GRAY);
        knob_value((int)i, t);
        cv_text(4 + (int32_t)i * 60, 30, &FONT_XS, t, C_WHITE);
    }
    cv_blit(0, 192);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.last_step = ui.last_slice = -2;
    ui.intro = 1u;                       /* the start screen first */
    ui.intro_t0 = plat_ms();
    ui.intro_drawn = 0u;
    ui.touched = 0u;
    ui.pad_focus = ui.key_focus = -1;
    ui.shown = -1;
    ui.redraw = RG_HEAD;
}

void app_frame(void)
{
    brk_status_t st;
    const brk_break_t *b;
    uint32_t now = plat_ms(), hdr;
    input();
    break_poll();
    leds();
    if (ui.intro) {                      /* any key or button opens the slicer (PLAY also starts the break) */
        if (ui.touched || now - ui.intro_t0 > 16u * INTRO_STEP + INTRO_HOLD)
            intro_end();
        else {
            intro_draw(now);
            return;
        }
    }
    ui.touched = 0u;
    ch_bta_poll(now);
    brk_status(&st);
    b = brk_get_break();
    if (pat_next >= 0 && (!st.playing || (st.step >= 0 && (uint32_t)st.step + 1u >= pat_len()))) {
        set.pattern = (uint8_t)pat_next;  /* the pattern picked: in for the loop's next round (its last step plays) */
        pat_next = -1;
        pat_apply();
        set_dirty = 1u;
        ui.redraw |= RG_BLOCKS;
    }
    if (st.step != ui.last_step || st.slice != ui.last_slice) {   /* the playhead moved */
        ui.last_step = st.step;
        ui.last_slice = st.slice;
        ui.redraw |= RG_WAVE | RG_BLOCKS;
    }
    if (ui.keys != ui.last_keys) {
        ui.last_keys = ui.keys;
        ui.redraw |= RG_BLOCKS;
    }
    if (ui.page == PG_GLOBAL && now - ui.panel_drawn >= 200u)   /* the GLOBAL page's panel: five times a second */
        ui.redraw |= RG_BLOCKS;
    if (ui.page == PG_MIX && now - ui.mix_drawn >= 33u)   /* the MIXER's meters: about 30 times a second */
        ui.redraw |= RG_BLOCKS;
    if (ui.page == PG_FX && set.fx.slot[ui.fx_focus & 3u].type == FXT_MBCOMP && now - ui.panel_drawn >= 50u)
        ui.redraw |= RG_BLOCKS;          /* MULTIBAND focused: its reduction, twenty times a second */
    if (foot_dirty) {
        foot_dirty = 0u;
        ui.redraw |= RG_FOOT;
    }
    {                                    /* the strip: a held key's loop, a held pad's animation, or the blocks */
        int16_t want = -1;
        int kt = key_tuned();
        if (kt >= 0 && (ui.key_turned || now - ui.key_since >= KEY_SHOW_MS))
            want = (int16_t)(100 + kt);
        else if (ui.pad_focus >= 0)
            want = ui.pad_focus;
        if (want != ui.shown) {
            if (want >= 0 && ui.shown < 0)
                lcd_fill(0, 86, 240, 106, C_BLACK);
            ui.shown = want;
            ui.redraw |= RG_FOOT | (want >= 0 ? RG_FX : RG_BLOCKS);
            if (want < 0)
                lcd_fill(0, 86, 240, 106, C_BLACK);
        }
    }
    if (ui.shown >= 0) {                 /* the animation runs at about 30 frames a second */
        if (ui.shown == PAD_TAPESTOP)
            ui.reel += 0.06f * st.tape;
        if (now - ui.fx_drawn >= 33u)
            ui.redraw |= RG_FX;
        ui.redraw &= ~(uint8_t)RG_BLOCKS;
    }
    if (st.pads != ui.last_pads) {
        ui.last_pads = st.pads;
        ui.redraw |= RG_FOOT;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0u;
        ui.redraw |= RG_HEAD;
    }
    /* the header's own state: playing, the tempo, the break */
    hdr = (uint32_t)st.playing | (uint32_t)set.tempo_x10 << 1 | (uint32_t)set.brk << 13 | (b ? 1u : 0u) << 20 |
          (uint32_t)ext_active() << 21 | (uint32_t)set.pattern << 22 | (uint32_t)ui.rec << 26 |
          (uint32_t)(pat_next >= 0) << 27;
    if (hdr != ui.last_hdr) {
        ui.last_hdr = hdr;
        ui.redraw |= RG_HEAD;
    }
    if (ui.redraw & RG_HEAD)
        draw_head(&st, b, now);
    if (ui.meter_x > 0 && now - ui.meter_drawn >= 33u) {   /* the level meter, about 30 times a second */
        draw_meter();
        ui.meter_drawn = now;
    }
    if (!ui.redraw)
        return;
    if (ui.redraw & RG_WAVE)
        draw_wave(&st, b);
    if (ui.redraw & RG_FX) {
        if (ui.shown >= 100)
            draw_key(ui.shown - 100, &st, now);
        else if (ui.shown >= 0)
            draw_fx(ui.shown, &st, now);
        ui.fx_drawn = now;
    }
    if (ui.redraw & RG_BLOCKS) {
        if (ui.page == PG_SEQ)
            draw_seq(&st);
        else if (ui.page == PG_PERC)
            draw_perc(&st);
        else if (ui.page == PG_GLOBAL) {
            draw_global(&st);
            ui.panel_drawn = now;
        } else if (ui.page == PG_FX) {
            draw_chain();
            ui.panel_drawn = now;
        } else if (ui.page == PG_KICK)
            draw_kick(&st);
        else if (ui.page == PG_MIX) {
            draw_mixer();
            ui.mix_drawn = now;
        } else
            draw_blocks(&st, ui.keys);
    }
    if (ui.redraw & RG_FOOT)
        draw_foot(&st);
    ui.redraw = 0u;
}

#ifdef PLAT_HOST
/* the host simulator's commands: "splash" draws the power-on screen (the device draws it before app_init; the
 * simulator never does) */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (!strcmp(cmd, "splash")) {
        app_splash();
        return 1;
    }
    if (!strcmp(cmd, "bta_sim")) {       /* bta_sim: a pretend Bluetooth audio (the host has no radio) for the page */
        bta_sim_on = 1u;
        return 1;
    }
    if (!strcmp(cmd, "expect_bpm")) {    /* expect_bpm LO HI (whole BPM): the tempo the engine plays at */
        int32_t x100 = (int32_t)(brk_get_tempo() * 100.0f + 0.5f), bad = x100 < atoi(a) * 100 || x100 > atoi(b) * 100;
        printf("%s tempo %d.%02d BPM (%s..%s)\n", bad ? "FAIL" : "  ok", (int)(x100 / 100), (int)(x100 % 100), a, b);
        return 1;
    }
    return 0;
}
#endif
