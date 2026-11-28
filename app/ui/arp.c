/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the ARP page (PRD B-55, docs/PRD_LAYER.md). The white keys play slices as on HOME; each black key is an
 * effect slot of the FX library (all but MB COMP) over the whole mix after the chain, on while it is held. SEL + a
 * black key latches it (SEL + it again lets it go); latched ones play on on the other pages. The key pressed last, or
 * ALGORITHM, picks the one the page shows in the FX page's view: PRESETS its effect, K1-K4 its settings, ARP again its
 * next part (MAIN, MORE, MOD). The engine has BRK_PAD_V voices: a key pressed gets one (a free one, else the oldest
 * let go, else the oldest), a key let go keeps its voice while it fades or its tail rings.
 * The layer: a pack with loops has them on black keys 1-4 (a tap latches at the loop's point, held it plays while
 * held, in phase); a pack with one-shots has them on the white keys (SEL + a white key: the slice). ARP held on
 * another page gives its keys while held (LY-024). The pads are the pack's when it brought its own (pfx_at).
 * (Part of app.c's unity: included there in order; the pads' state is in fx.c, which draws them.) */

enum { AK_PAD, AK_LOOP, AK_SHOT };
static uint8_t arp_kind, arp_li, arp_si, ly_part;   /* the key ARP shows (a pad: pfx_sel), its part (MAIN, MORE) */
static uint8_t arp_mom, arp_used;        /* ARP held on another page: its keys meanwhile; a key played meanwhile */
static uint8_t lk_mode[BRK_LY_LOOPS];    /* a loop key: 0 up, 1 down (a tap so far), 2 held (playing while held) */
static uint32_t lk_t[BRK_LY_LOOPS];      /* when it went down */
static uint32_t lk_sent[BRK_LY_LOOPS];   /* when its last change went to the engine */
static uint8_t ly_lat;                   /* the loops latched (by taps; AUTO's at PLAY) */
static uint32_t arp_wk, arp_bk;          /* the white and black keys ARP took down (their release is ARP's too) */
static int arp_here(void) { return ui.page == PG_ARP || arp_mom; }
static int ly_loopkeys(void) { return ly_nl != 0u; }   /* black keys 1-4 are the loops */

/* LY-030: the physical key identity is part of every spoken layer announcement.
 * Keep the same compact wording on key-down, latch/hold transitions and shot triggers. */
static void ly_say_loop(uint32_t i, const char *state)
{
    char n[24] = {'L', '0', ' ', 0};
    if (i >= BRK_LY_LOOPS)
        return;
    n[1] = (char)('1' + i);
    text_cat(n, lyL[i].name);
    app_say(n, state);
}
static void ly_say_shot(uint32_t i)
{
    char n[24] = {'S', '0', ' ', 0};
    if (i >= BRK_LY_SHOTS)
        return;
    if (i < 9u)
        n[1] = (char)('1' + i);
    else {
        n[1] = '1';
        n[2] = (char)('0' + (i - 9u));
        n[3] = ' ';
        n[4] = 0;
    }
    text_cat(n, lyS[i].name);
    app_say(n, "");
}

static void pfx_say(uint32_t i)
{
    char n[8] = "PAD ";
    int_text((int32_t)i + 1, n + 4);
    app_say(n, pfx_latched >> i & 1u ? "HOLD" : BRK_FXT_NAME[pfx_at(i)->type % BRK_NFXT]);
}

static uint32_t pfx_pick(uint32_t on, uint32_t busy)   /* the voice a key pressed gets */
{
    uint32_t v, pass, best = BRK_PAD_V;
    for (v = 0; v < BRK_PAD_V; v++)
        if (!pfx_owner[v] && !(busy >> v & 1u))
            return v;
    for (pass = 0; pass < 2u && best == BRK_PAD_V; pass++)   /* the oldest let go (its tail cut), else the oldest */
        for (v = 0; v < BRK_PAD_V; v++) {
            uint32_t o = pfx_owner[v];
            if (!pass && o && on >> (o - 1u) & 1u)
                continue;
            if (best == BRK_PAD_V || (int32_t)(pfx_age[v] - pfx_age[best]) < 0)
                best = v;
        }
    return best;
}

/* every input: voices for the keys just turned on, free ones back, each voice's key's effect to the engine */
static void pfx_update(uint32_t now)
{
    static uint32_t was;
    uint32_t on = pfx_on(), busy = brk_fx_pad_busy(), v, i;
    for (v = 0; v < BRK_PAD_V; v++) {    /* a key let go whose voice is quiet: the voice free */
        i = pfx_owner[v];
        if (i && !(on >> (i - 1u) & 1u) && !(busy >> v & 1u)) {
            pfx_vox[i - 1u] = 0u;
            pfx_owner[v] = 0u;
        }
    }
    for (i = 0; i < NPFX; i++)           /* (only as a key turns on: a key that lost its voice waits for a press) */
        if ((on & ~was) >> i & 1u && !pfx_vox[i] && pfx_at(i)->type != FXT_EMPTY) {
            v = pfx_pick(on, busy);
            if (pfx_owner[v]) {          /* taken over: that key quiet (a latched one let go, so it shows) */
                pfx_vox[pfx_owner[v] - 1u] = 0u;
                pfx_latched &= (uint16_t)~(1u << (pfx_owner[v] - 1u));
            }
            pfx_owner[v] = (uint8_t)(i + 1u);
            pfx_vox[i] = (uint8_t)(v + 1u);
            pfx_age[v] = now;
        }
    was = on;
    for (v = 0; v < BRK_PAD_V; v++)
        if (pfx_owner[v]) {
            i = pfx_owner[v] - 1u;
            brk_fx_set_pad(v, pfx_at(i), on >> i & 1u);
            brk_fx_set_fxx(BRK_FX_SLOTS + v, pfxx_at(i));
        }
}

static void arp_pads_changed(void)       /* the pads' table changed (another pack, or its breaks left): all let go */
{
    pfx_held = pfx_latched = 0u;
    pfx_update(plat_ms());
}

static void ly_loop_key(uint32_t i, int down, uint32_t btn, uint32_t now)
{
    uint8_t b = (uint8_t)(1u << i);
    if (down) {
        lk_mode[i] = 1u;
        lk_t[i] = now;
        arp_kind = AK_LOOP;
        if (arp_li != i)
            ly_part = 0u;
        arp_li = (uint8_t)i;
        if (btn & 1u << B_SEL) {         /* SEL + the key: off at once (a drop: the next tap brings it in on its point) */
            ui.tap_combo = 1u;
            lk_mode[i] = 0u;
            ly_lat &= (uint8_t)~b;
            brk_ly_want(i, 0, LYQ_NOW);
        }
        ly_say_loop(i, ly_lat & b ? "ON" : "OFF");
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    lk_sent[i] = now;
    if (lk_mode[i] == 1u) {              /* a tap: latched, or let go, at its point */
        ly_lat ^= b;
        brk_ly_want(i, ly_lat & b, mq.lset[i].quant);
        ly_say_loop(i, ly_lat & b ? "LATCHED" : "OFF");
    } else if (lk_mode[i] == 2u) {       /* held: off as it is let go */
        ly_lat &= (uint8_t)~b;
        brk_ly_want(i, 0, LYQ_NOW);
    }
    lk_mode[i] = 0u;
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void ly_frame(uint32_t now)       /* every input: a loop key held long enough plays while held */
{
    uint32_t i, hold = (set.ly_hold ? set.ly_hold : 25u) * 10u;
    for (i = 0; i < BRK_LY_LOOPS; i++)
        if (lk_mode[i] == 1u && now - lk_t[i] >= hold) {
            lk_mode[i] = 2u;
            brk_ly_want(i, 1, LYQ_NOW);  /* (in phase, at once) */
            ly_say_loop(i, "HELD");
        }
    {                                    /* the latches as the engine has them (AUTO at PLAY, every loop off at STOP,
                                          * a voice given to another loop): each once its change is in */
        brk_ly_status_t ls;
        brk_ly_status(&ls);
        for (i = 0; i < BRK_LY_LOOPS; i++)
            if (!lk_mode[i] && !(ls.pend >> i & 1u) && now - lk_sent[i] > 100u)
                ly_lat = (uint8_t)((ly_lat & ~(1u << i)) | (ls.on & 1u << i));
    }
}

/* the ARP keys: the loops, the shots, the pads. They take the keys they use (kdn, kup lose them) */
static void arp_keys(uint32_t *kdn, uint32_t *kup, uint32_t btn, uint32_t now)
{
    uint32_t i;
    ly_frame(now);
    for (i = 0; i < 16u; i++) {          /* white keys: the shots (SEL: the slices) */
        uint32_t m = 1u << WK[i];
        if (*kup & m && arp_wk & m) {
            arp_wk &= ~m;
            *kup &= ~m;
            brk_ly_hit(i, 0);
        }
        if (!(*kdn & m) || !arp_here() || !ly_ns || btn & 1u << B_SEL)
            continue;
        *kdn &= ~m;
        if (i >= ly_ns || !lyS[i].d)
            continue;
        arp_wk |= m;
        arp_used = 1u;
        brk_ly_hit(i, 1);
        arp_kind = AK_SHOT;
        if (arp_si != i)
            ly_part = 0u;
        arp_si = (uint8_t)i;
        ly_say_shot(i);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
    }
    for (i = 0; i < NPFX; i++) {
        uint32_t b = 1u << i, m = 1u << BK[i];
        if (*kup & m) {
            pfx_held &= (uint16_t)~b;
            if (arp_bk & m) {
                arp_bk &= ~m;
                *kup &= ~m;
                if (i < BRK_LY_LOOPS && lk_mode[i])
                    ly_loop_key(i, 0, btn, now);
            }
        }
        if (!arp_here() || !(*kdn & m))
            continue;
        *kdn &= ~m;
        arp_bk |= m;
        arp_used = 1u;
        if (ly_loopkeys() && i < BRK_LY_LOOPS) {   /* black keys 1-4: the loops (a key without one: nothing) */
            if (i < ly_nl && lyL[i].d)
                ly_loop_key(i, 1, btn, now);
            continue;
        }
        arp_kind = AK_PAD;
        if (pfx_sel != i) {              /* another key: its view from its first part */
            pfx_sel = (uint8_t)i;
            pfx_part = 0u;
        }
        if (btn & 1u << B_SEL) {         /* SEL + the key: latched, or let go */
            pfx_latched ^= (uint16_t)b;
            ui.tap_combo = 1u;
        } else
            pfx_held |= (uint16_t)b;
        pfx_say(i);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
    }
    pfx_update(now);
}

/* ALGORITHM on ARP: the key shown (and edited), without playing it: the loops, the pads, the shots in a row */
static void arp_select(int32_t d)
{
    uint32_t nl = ly_nl, ns = ly_ns, n = nl + NPFX + ns, at;
    at = arp_kind == AK_LOOP ? arp_li : arp_kind == AK_SHOT ? nl + NPFX + arp_si : nl + pfx_sel;
    do
        at = (at + (d > 0 ? 1u : n - 1u)) % n;
    while (at >= nl && at < nl + NPFX && ly_loopkeys() && at - nl < BRK_LY_LOOPS);   /* (pads 1-4 under loops) */
    if (at < nl) {
        arp_kind = AK_LOOP;
        arp_li = (uint8_t)at;
    } else if (at < nl + NPFX) {
        arp_kind = AK_PAD;
        pfx_sel = (uint8_t)(at - nl);
        pfx_part = 0u;
    } else {
        arp_kind = AK_SHOT;
        arp_si = (uint8_t)(at - nl - NPFX);
    }
    ly_part = 0u;
    page_say();
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void arp_next_part(void)          /* ARP again: the shown key's next part, round */
{
    if (arp_kind != AK_PAD)
        ly_part ^= 1u;
    else
        pfx_part = (uint8_t)((pfx_part + 1u) % fx_npages(pfx_at(pfx_sel)->type));
    page_say();
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

/* the shown loop's or shot's knobs: MAIN, MORE (docs/PRD_LAYER.md 3.3) */
static const char *const LYQ_NAME[LYQ_N] = {"NOW", "BEAT", "BAR", "LOOP"};
static const char *const SHQ_NAME[SHQ_N] = {"OFF", "1/16", "BEAT"};
static const char *const SHM_NAME[SHM_N] = {"ONE", "GATE", "LOOP"};
static const char *ly_knob_name(uint32_t i)
{
    static const char *const N[4][4] = {{"LEVEL", "FILTER", "QUANT", "AUTO"}, {"BARS", "FROM", "", ""},
                                        {"LEVEL", "PITCH", "DECAY", "BREAK"}, {"MODE", "CHOKE", "QUANT", ""}};
    return N[(arp_kind == AK_SHOT) * 2u + ly_part][i & 3u];
}
static void ly_value(uint32_t i, char *t)
{
    t[0] = 0;
    if (arp_kind == AK_LOOP) {
        const ly_lset_t *l = &mq.lset[arp_li % BRK_LY_LOOPS];
        int32_t v = ly_part ? (i == 0u ? (int32_t)(l->wbars ? l->wbars : lyL[arp_li].bars) : (int32_t)l->wstart + 1)
                    : i == 0u ? (int32_t)l->level : i == 1u ? (int32_t)l->filter : 0;
        if (ly_part && i > 1u)
            return;
        if (!ly_part && i == 2u)
            text_cat(t, LYQ_NAME[l->quant % LYQ_N]);
        else if (!ly_part && i == 3u)
            text_cat(t, l->au ? "ON" : "OFF");
        else if (!ly_part && i == 1u && !v)
            text_cat(t, "OPEN");
        else {
            if (!ly_part && i == 1u)
                text_cat(t, v < 0 ? "LP " : "HP ");
            int_text(v < 0 ? -v : v, t + (!ly_part && i == 1u ? 3 : 0));
            if (!ly_part && !i)
                text_cat(t, "%");
        }
        return;
    }
    {
        const ly_sset_t *h = &mq.sset[arp_si % BRK_LY_SHOTS];
        if (ly_part) {
            if (i == 0u)
                text_cat(t, SHM_NAME[h->mode % SHM_N]);
            else if (i == 1u && h->choke)
                int_text(h->choke, t);
            else if (i == 1u)
                text_cat(t, "OFF");
            else if (i == 2u)
                text_cat(t, SHQ_NAME[h->quant % SHQ_N]);
        } else if (i == 0u) {
            int_text(h->level, t);
            text_cat(t, "%");
        } else if (i == 1u) {
            if (h->pitch > 0)
                text_cat(t, "+");
            int_text(h->pitch, t + (h->pitch > 0));
        } else if (i == 2u) {
            if (h->decay)
                int_text(h->decay, t);
            else
                text_cat(t, "OFF");
        } else
            text_cat(t, h->cut ? "CUT" : "OVER");
    }
}
static int32_t step_to(int32_t v, int32_t d, int32_t lo, int32_t hi) { return clamp(v + (d > 0 ? 1 : -1), lo, hi); }
static void ly_turn(uint32_t i, int32_t d)
{
    char t[16];
    if (arp_kind == AK_LOOP) {
        ly_lset_t *l = &mq.lset[arp_li % BRK_LY_LOOPS];
        int32_t bars = (int32_t)lyL[arp_li].bars;
        if (ly_part) {
            if (i == 0u)
                l->wbars = (uint8_t)step_to(l->wbars ? l->wbars : bars, d, 1, bars > 16 ? 16 : bars);
            else if (i == 1u)
                l->wstart = (uint8_t)step_to(l->wstart, d, 0, bars - 1 < 15 ? bars - 1 : 15);
            else
                return;
        } else if (i == 0u)
            l->level = (uint8_t)clamp((int32_t)l->level + d * 2, 0, 100);
        else if (i == 1u)
            l->filter = (int8_t)clamp((int32_t)l->filter + d, -50, 50);
        else if (i == 2u)
            l->quant = (uint8_t)step_to(l->quant, d, 0, LYQ_N - 1);
        else
            l->au = d > 0;
        brk_ly_loop_cfg(arp_li, ly_lcfg(arp_li));
    } else {
        ly_sset_t *h = &mq.sset[arp_si % BRK_LY_SHOTS];
        if (ly_part) {
            if (i == 0u)
                h->mode = (uint8_t)step_to(h->mode, d, 0, SHM_N - 1);
            else if (i == 1u)
                h->choke = (uint8_t)step_to(h->choke, d, 0, 4);
            else if (i == 2u)
                h->quant = (uint8_t)step_to(h->quant, d, 0, SHQ_N - 1);
            else
                return;
        } else if (i == 0u)
            h->level = (uint8_t)clamp((int32_t)h->level + d * 2, 0, 100);
        else if (i == 1u)
            h->pitch = (int8_t)clamp((int32_t)h->pitch + d, -24, 24);
        else if (i == 2u)
            h->decay = (uint8_t)clamp((int32_t)h->decay + d, 0, 31);
        else
            h->cut = d > 0;
        brk_ly_shot_cfg(arp_si, ly_scfg(arp_si));
    }
    ly_value(i, t);
    app_say(ly_knob_name(i), t);
    changed();
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

/* the ARP page's picture with a loop or a shot shown (88..191): the loops, each its place in its window; the shown
 * one's settings; the shots, lit as they sound */
static void draw_layer(void)
{
    brk_ly_status_t ls;
    uint32_t i, band;
    char t[24];
    brk_ly_status(&ls);
    for (band = 0; band < 2u; band++) {
        cv_begin(240, 52, C_BLACK);
        if (!band)
            for (i = 0; i < BRK_LY_LOOPS; i++) {   /* L1..L4: name, bars, its place, its state */
                int32_t y = (int32_t)i * 13, on = ls.on >> i & 1u, pe = ls.pend >> i & 1u;
                int sel = arp_kind == AK_LOOP && arp_li == i;
                char n[4] = {'L', (char)('1' + i), 0, 0};
                cv_text(4, y, &FONT_XS, n, sel ? C_WHITE : C_GRAY);
                if (!lyL[i].d) {
                    cv_rect(30, y + 6, 206, 1, C_LINE);
                    continue;
                }
                cv_text(22, y, &FONT_XS, lyL[i].name, on ? C_HI : C_GRAY);
                int_text((int32_t)lyL[i].bars, t);
                text_cat(t, "B");
                cv_text(84, y, &FONT_XS, t, C_DIM);
                cv_rect(108, y + 3, 100, 6, C_BLOCK);
                if (on)
                    cv_rect(108 + ls.ph[i] * 100 / 256, y + 2, 2, 8, C_HI);
                cv_text(212, y, &FONT_XS, pe ? "NEXT" : on ? "ON" : "", pe ? C_AMB : C_HI);
            }
        else {
            if (arp_kind == AK_SHOT) {   /* the shot shown: its name, mode, pitch, CUT */
                const ly_sset_t *h = &mq.sset[arp_si % BRK_LY_SHOTS];
                t[0] = 'S';
                int_text((int32_t)arp_si + 1, t + 1);
                text_cat(t, " ");
                text_cat(t, lyS[arp_si].name);
                cv_text(4, 0, &FONT_T, t, C_WHITE);
                cv_text(150, 2, &FONT_XS, h->cut ? "CUTS THE BREAK" : SHM_NAME[h->mode % SHM_N], h->cut ? C_AMB : C_DIM);
            } else if (arp_kind == AK_LOOP) {
                t[0] = 'L';
                int_text((int32_t)arp_li + 1, t + 1);
                text_cat(t, " ");
                text_cat(t, lyL[arp_li].name);
                cv_text(4, 0, &FONT_T, t, C_WHITE);
                cv_text(150, 2, &FONT_XS, ly_lat >> arp_li & 1u ? "LATCHED" : "TAP OR HOLD", C_DIM);
            }
            for (i = 0; i < 16u; i++) {  /* the shots on the white keys */
                int32_t x = 2 + (int32_t)i * 15;
                int lit = ls.shots >> i & 1u, has = i < ly_ns && lyS[i].d;
                cv_rect(x, 22, 13, 16, lit ? C_HI : has ? C_BLOCK : C_BLACK);
                if (has && mq.sset[i].cut)
                    cv_rect(x, 36, 13, 2, C_AMB);
                if (arp_kind == AK_SHOT && arp_si == i)
                    cv_rect(x, 40, 13, 2, C_WHITE);
            }
            cv_text(4, 43, &FONT_XS, ly_ns ? "SHOTS ON THE WHITE KEYS" : "NO SHOTS IN THIS PACK", C_DIM);
        }
        cv_blit(0, 88u + band * 52u);
    }
}
static const char *pfx_short(uint32_t i) /* the foot's label: the key's effect in three letters (FILTER its mode) */
{
    static const char *const FX3[BRK_NFXT] = {"-", "LP", "CRN", "DRV", "PCH", "FLG", "RNG", "TRM", "VRB", "DLY", "EQ",
                                              "TPE", "MB", "CLP"};
    const brk_fxslot_t *s = pfx_at(i);
    if (s->type == FXT_FILTER)
        return s->p[2] < 0.33f ? "LP" : s->p[2] < 0.67f ? "BP" : "HP";
    return FX3[s->type % BRK_NFXT];
}
