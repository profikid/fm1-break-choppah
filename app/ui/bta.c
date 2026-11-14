/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: Bluetooth audio out: GLOBAL's third part (fm1-base's A2DP source, or a pretend one on the host). (Part
 * of app.c's unity: included there in order.) */

/* ---- Bluetooth audio out (PRD A-72; fm1-base's A2DP source, plat_bta_*): GLOBAL's third part. K1 on / off, K2 the
 * device (the remembered ones first, then what a scan found), K3 a scan, K4 right connect / left hang up (SEL held,
 * left: forget a remembered one). On at power-on as saved, connecting the last device again. The host has no radio:
 * there it is "not in this build", or with the scenario command bta_sim a pretend one for the page */
#define BTA_LIST 8u
static uint8_t bta_sim_on;
#if !FM1B_BT_AUDIO
static struct {                          /* the pretend radio (host scenarios) */
    int st;
    uint32_t t, nfound, nknown;
    char known[4][18];
} sim;
static const char *const SIM_FOUND[2] = {"WH-1000XM4", "JBL FLIP 6"};
static void sim_copy(char *d, const char *s)
{
    uint32_t i;
    for (i = 0; i < 17u && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
#endif
static int ch_bta_have(void)                /* a radio for it in this build */
{
#if FM1B_BT_AUDIO
    return 1;
#else
    return bta_sim_on;
#endif
}
static int ch_bta_state(void)
{
#if FM1B_BT_AUDIO
    return plat_bta_state();
#else
    return bta_sim_on ? sim.st : PLAT_BTA_OFF;
#endif
}
static void ch_bta_on(int on)
{
#if FM1B_BT_AUDIO
    plat_bta_on(on);
#else
    sim.st = on ? PLAT_BTA_READY : PLAT_BTA_OFF;
#endif
}
static void ch_bta_scan(void)
{
#if FM1B_BT_AUDIO
    plat_bta_scan(1);
#else
    sim.st = PLAT_BTA_SCANNING;
    sim.t = plat_ms();
    sim.nfound = 0u;
#endif
}
static int ch_bta_found(int i, char *name)
{
#if FM1B_BT_AUDIO
    return plat_bta_found(i, name);
#else
    if (i < 0 || (uint32_t)i >= sim.nfound)
        return 0;
    sim_copy(name, SIM_FOUND[i]);
    return 1;
#endif
}
static int ch_bta_known(int i, char *name)
{
#if FM1B_BT_AUDIO
    return plat_bta_known(i, name);
#else
    if (i < 0 || (uint32_t)i >= sim.nknown)
        return 0;
    sim_copy(name, sim.known[i]);
    return 1;
#endif
}
#if !FM1B_BT_AUDIO
static void sim_remember(const char *n)  /* the device connected: first in the remembered list */
{
    uint32_t i, at = sim.nknown < 4u ? sim.nknown : 3u;
    for (i = 0; i < sim.nknown; i++) {
        uint32_t k;
        for (k = 0; n[k] && n[k] == sim.known[i][k]; k++)
            ;
        if (!n[k] && !sim.known[i][k])
            at = i;
    }
    for (i = at; i > 0u; i--)
        sim_copy(sim.known[i], sim.known[i - 1u]);
    sim_copy(sim.known[0], n);
    if (at >= sim.nknown)
        sim.nknown++;
    sim.st = PLAT_BTA_PAGING;
    sim.t = plat_ms();
}
#endif
static void ch_bta_connect(int found_i, int known_i)   /* one of them, the other -1 */
{
#if FM1B_BT_AUDIO
    if (known_i >= 0)
        (void)plat_bta_reconnect(known_i);
    else
        plat_bta_pick(found_i);
#else
    char n[18];
    if (known_i >= 0 ? ch_bta_known(known_i, n) : ch_bta_found(found_i, n))
        sim_remember(n);
#endif
}
static void ch_bta_hangup(void)
{
#if FM1B_BT_AUDIO
    plat_bta_hangup();
#else
    sim.st = PLAT_BTA_READY;
#endif
}
static void ch_bta_forget(int i)
{
#if FM1B_BT_AUDIO
    plat_bta_forget(i);
#else
    uint32_t k;
    if (i < 0 || (uint32_t)i >= sim.nknown)
        return;
    for (k = (uint32_t)i; k + 1u < sim.nknown; k++)
        sim_copy(sim.known[k], sim.known[k + 1u]);
    sim.nknown--;
#endif
}

/* the list K2 picks from: the remembered devices, then the found ones not among them; entry i -> its name, and
 * which it is (*known >= 0: remembered device *known; else found device *found) -> 0: no entry i */
static int same_name(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static int ch_bta_entry(uint32_t i, char *name, int *known, int *found)
{
    char n[18], m[18];
    int k, f, nk = 0;
    for (k = 0; ch_bta_known(k, n); k++, nk++)
        if ((uint32_t)k == i) {
            name[0] = 0;
            text_cat(name, n);
            *known = k;
            *found = -1;
            return 1;
        }
    i -= (uint32_t)nk;
    for (f = 0; ch_bta_found(f, n); f++) {
        int dup = 0;
        for (k = 0; ch_bta_known(k, m); k++)
            if (same_name(n, m))
                dup = 1;
        if (dup)
            continue;
        if (!i--) {
            name[0] = 0;
            text_cat(name, n);
            *known = -1;
            *found = f;
            return 1;
        }
    }
    return 0;
}
static uint32_t ch_bta_count(void)
{
    char n[18];
    int k, f;
    uint32_t c = 0;
    while (c < BTA_LIST && ch_bta_entry(c, n, &k, &f))
        c++;
    return c;
}

static const char *ch_bta_state_name(int st)
{
    static const char *const N[] = {"OFF", "SCANNING", "FOUND", "NONE FOUND", "CONNECTING", "LINKED", "SETTING UP",
                                    "STREAMING", "REFUSED", "NO DEVICE YET", "FAILED", "READY"};
    return st >= 0 && st <= PLAT_BTA_READY ? N[st] : "?";
}
static void ch_bta_value(int i, char *t)
{
    char n[18];
    int k, f, st = ch_bta_state();
    t[0] = 0;
    if (!ch_bta_have()) {
        if (i == 0)
            text_cat(t, "NOT BUILT");
        return;
    }
    if (i == 0)
        text_cat(t, st == PLAT_BTA_OFF ? "OFF" : "ON");
    else if (i == 1) {
        if (ch_bta_entry(ui.bta_sel, n, &k, &f)) {
            n[9] = 0;                    /* (the foot's width) */
            text_cat(t, n);
        } else
            text_cat(t, "-");
    } else if (i == 2)
        text_cat(t, st == PLAT_BTA_SCANNING ? "SCANNING" : st == PLAT_BTA_OFF ? "-" : "TURN");
    else
        text_cat(t, st == PLAT_BTA_STREAMING || st == PLAT_BTA_SETUP || st == PLAT_BTA_LINKED ||
                    st == PLAT_BTA_PAGING ? "< HANG UP" : st == PLAT_BTA_OFF ? "-" : "CONNECT >");
}
static void ch_bta_turn(int i, int32_t d)
{
    char n[18];
    int k, f, st = ch_bta_state();
    if (!ch_bta_have()) {
        app_warn("BT AUDIO", "NOT IN THIS BUILD");
        return;
    }
    if (i == 0) {
        uint8_t on = d > 0;
        if (on == (st != PLAT_BTA_OFF))
            return;
        ch_bta_on(on);
        set.bta_on = on;
        set_dirty = 1u;
        app_say("BT AUDIO", on ? "ON" : "OFF");
    } else if (st == PLAT_BTA_OFF)
        app_warn("BT AUDIO", "TURN K1 ON");
    else if (i == 1) {
        uint32_t c = ch_bta_count();
        ui.bta_sel = (uint8_t)(c ? (uint32_t)clamp((int32_t)ui.bta_sel + (d > 0 ? 1 : -1), 0, (int32_t)c - 1) : 0u);
        if (ch_bta_entry(ui.bta_sel, n, &k, &f))
            app_say(k >= 0 ? "KNOWN" : "FOUND", n);
    } else if (i == 2) {
        ch_bta_scan();
        app_say("BT AUDIO", "SCANNING");
    } else if (d > 0) {                  /* connect the device chosen */
        if (ch_bta_entry(ui.bta_sel, n, &k, &f)) {
            ch_bta_connect(f, k);
            ui.bta_sel = 0u;             /* (it becomes the last one: first in the list) */
            app_say("CONNECT", n);
        } else
            app_warn("BT AUDIO", "SCAN FIRST (K3)");
    } else if (ui.btn & 1u << B_SEL) {   /* SEL held, left: forget a remembered device */
        if (ch_bta_entry(ui.bta_sel, n, &k, &f) && k >= 0) {
            ch_bta_forget(k);
            ui.bta_sel = 0u;
            app_say("FORGOT", n);
        }
    } else {
        ch_bta_hangup();
        app_say("BT AUDIO", "HUNG UP");
    }
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
/* every frame: the pretend radio's steps; at boot, on as saved, then the last device once it is ready */
static void ch_bta_poll(uint32_t now)
{
    static int last_st = -1;
    if (ch_bta_state() != last_st) {     /* (the foot's K3 is SCAN by the state) */
        last_st = ch_bta_state();
        foot_dirty = 1u;
    }
#if !FM1B_BT_AUDIO
    if (bta_sim_on) {
        if (sim.st == PLAT_BTA_SCANNING && now - sim.t > 2000u) {
            sim.nfound = 2u;
            sim.st = PLAT_BTA_FOUND;
        } else if (sim.st == PLAT_BTA_PAGING && now - sim.t > 800u) {
            sim.st = PLAT_BTA_SETUP;
            sim.t = now;
        } else if (sim.st == PLAT_BTA_SETUP && now - sim.t > 500u)
            sim.st = PLAT_BTA_STREAMING;
    }
#endif
    (void)now;
    if (ui.bta_booted || !ch_bta_have())
        return;
    if (!set.bta_on) {
        ui.bta_booted = 1u;
        return;
    }
    if (ch_bta_state() == PLAT_BTA_OFF)
        ch_bta_on(1);
    else if (ch_bta_state() == PLAT_BTA_READY) {
        char n[18];
        if (ch_bta_known(0, n))
            ch_bta_connect(-1, 0);
        ui.bta_booted = 1u;
    }
}
/* GLOBAL's third part: the state and the device; the list, the one K2 points at outlined */
static void draw_bta(void)
{
    char n[18], t[32];
    int st = ch_bta_state(), k, f;
    uint32_t i, c = ch_bta_count(), first;
    uint16_t sc = st == PLAT_BTA_STREAMING ? C_HI : st == PLAT_BTA_FAILED || st == PLAT_BTA_REFUSED ? C_AMB
                  : st == PLAT_BTA_OFF ? C_DIM : C_WHITE;
    cv_begin(240, 52, C_BLACK);
    cv_text(6, 2, &FONT_T, "BT AUDIO", C_GRAY);
    cv_text(96, 2, &FONT_T, ch_bta_have() ? ch_bta_state_name(st) : "NOT IN THIS BUILD", sc);
    t[0] = 0;
    if (st == PLAT_BTA_STREAMING && ch_bta_known(0, n)) {
        text_cat(t, "TO ");
        text_cat(t, n);
    } else if (st == PLAT_BTA_OFF)
        text_cat(t, ch_bta_have() ? "K1: ON" : "");
    else if (!c)
        text_cat(t, "K3: SCAN");
    cv_text(96, 15, &FONT_XS, t, st == PLAT_BTA_STREAMING ? C_HI : C_DIM);
    cv_text(6, 30, &FONT_XS, st == PLAT_BTA_STREAMING ? "FM-1 SPEAKER MUTED; 150-300 MS LATE" :
            "FOR LISTENING: 150-300 MS LATE", C_DIM);
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    first = ui.bta_sel > 2u ? ui.bta_sel - 2u : 0u;   /* three rows of the list, the chosen one in view */
    for (i = first; i < first + 3u && i < c; i++) {
        int32_t y = (int32_t)(i - first) * 16;
        int sel = i == ui.bta_sel;
        ch_bta_entry(i, n, &k, &f);
        if (sel)
            cv_rect(4, y + 1, 232, 15, C_LINE);
        cv_text(10, y + 2, &FONT_XS, n, sel ? C_WHITE : C_GRAY);
        cv_text(180, y + 2, &FONT_XS, k == 0 ? "LAST" : k > 0 ? "KNOWN" : "NEW", sel ? C_HI : C_DIM);
    }
    if (!c && st != PLAT_BTA_OFF)
        cv_text(10, 2, &FONT_XS, st == PLAT_BTA_SCANNING ? "LOOKING (10 S)..." : "NO DEVICES YET", C_DIM);
    cv_blit(0, 140);
}
