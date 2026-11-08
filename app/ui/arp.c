/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the ARP page (PRD B-55). The white keys play slices as on HOME; each black key is an effect slot of the FX
 * library (all but MB COMP) over the whole mix after the chain, on while it is held. SEL + a black key latches it
 * (SEL + it again lets it go); latched ones play on on the other pages. The key pressed last, or ALGORITHM, picks the
 * one the page shows in the FX page's view: PRESETS its effect, K1-K4 its settings, ARP again its next part (MAIN,
 * MORE, MOD). The engine has BRK_PAD_V voices: a key pressed gets one (a free one, else the oldest let go, else the
 * oldest), a key let go keeps its voice while it fades or its tail rings. (Part of app.c's unity: included there in
 * order; its state is in fx.c, which draws it.) */

static void pfx_say(uint32_t i)
{
    char n[8] = "PAD ";
    int_text((int32_t)i + 1, n + 4);
    app_say(n, pfx_latched >> i & 1u ? "HOLD" : BRK_FXT_NAME[set.pfx[i % NPFX].type % BRK_NFXT]);
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
        if ((on & ~was) >> i & 1u && !pfx_vox[i] && set.pfx[i].type != FXT_EMPTY) {
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
            brk_fx_set_pad(v, &set.pfx[i], on >> i & 1u);
            brk_fx_set_fxx(BRK_FX_SLOTS + v, &set.pfxx[i]);
        }
}

static void arp_keys(uint32_t kdn, uint32_t kup, uint32_t btn, uint32_t now)
{
    uint32_t i;
    for (i = 0; i < NPFX; i++) {
        uint32_t b = 1u << i;
        if (kup >> BK[i] & 1u)
            pfx_held &= (uint16_t)~b;
        if (ui.page != PG_ARP || !(kdn >> BK[i] & 1u))
            continue;
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

static void arp_select(int32_t d)        /* ALGORITHM on ARP: the key shown (and edited), without playing it */
{
    pfx_sel = (uint8_t)((pfx_sel + (d > 0 ? 1u : NPFX - 1u)) % NPFX);
    pfx_part = 0u;
    page_say();
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void arp_next_part(void)          /* ARP again: the shown key's next part, round */
{
    uint32_t n = fx_npages(set.pfx[pfx_sel % NPFX].type);
    pfx_part = (uint8_t)((pfx_part + 1u) % n);
    page_say();
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static const char *pfx_short(uint32_t i) /* the foot's label: the key's effect in three letters (FILTER its mode) */
{
    static const char *const FX3[BRK_NFXT] = {"-", "LP", "CRN", "DRV", "PCH", "FLG", "RNG", "TRM", "VRB", "DLY", "EQ",
                                              "TPE", "MB", "CLP"};
    const brk_fxslot_t *s = &set.pfx[i % NPFX];
    if (s->type == FXT_FILTER)
        return s->p[2] < 0.33f ? "LP" : s->p[2] < 0.67f ? "BP" : "HP";
    return FX3[s->type % BRK_NFXT];
}
