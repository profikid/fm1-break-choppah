/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: MATHCORE (EDIT): LIVE (the black keys play scenes, K1-K4 the macros, the wheel), LANE (one lane of a
 * scene: its rhythm, its slices, its feel) and SCENE (its phrase, run on or dry, its black key). The Slice Map is
 * heard, not set: its K S H P letters are on the slice blocks (screen.c). docs/PRD_MATHCORE.md. (Part of app.c's
 * unity: included there in order.) */

enum { MCP_LIVE, MCP_LANE, MCP_SCENE, MCPARTS };
static const char *const MCPART_NAME[MCPARTS] = {"LIVE", "LANE", "SCENE"};
enum { MKS_RHYTHM, MKS_SOUND, MKS_FEEL, MKS_MORE, MKSETS };   /* LANE's knob sets (PRESETS turns them) */
static const char *const MKS_NAME[MKSETS] = {"RHYTHM", "SOUND", "FEEL", "MORE"};
static const char *const MC_KNOB[MKSETS][4] = {{"LENGTH", "RATE", "ROTATE", "PHASE"},
                                               {"MODE", "PICK", "PITCH", "LEVEL"},
                                               {"CHANCE", "RATCHET", "GATE", "VARY"},
                                               {"ROLE", "RISE", "REVERSE", "LANE"}};
static const char *const MACRO_NAME[4] = {"DENSITY", "MATH", "SPEED", "CHAOS"};
static const char *const ROLE_NAME[MC_ROLES] = {"KICK", "SNARE", "HAT", "PERC"};
static const char ROLE_CH[MC_ROLES] = {'K', 'S', 'H', 'P'};
static const char *const MC_STATE_NAME[3] = {"MUTE", "PLAY", "SOLO"};

static struct {
    uint8_t part;                        /* MCP_* */
    uint8_t scene;                       /* the scene LANE and SCENE edit (and play, from there) */
    uint8_t lane;                        /* the lane LANE edits */
    uint8_t kset;                        /* LANE's knob set */
    uint8_t cursor;                      /* LIVE: the chain's entry ALGORITHM points at (PRESETS moves it) */
    uint8_t on;                          /* the scenes play: 1 live (the black keys, the chain), 2 the scene edited */
    uint8_t base;                        /* LIVE: the groove the fills, risers and drops come back to */
    uint8_t qpos;                        /* the chain's entry playing (when it leads) */
    uint8_t qgone;                       /* it was taken out of the chain: the next is the one now in its place */
    uint8_t qled;                        /* the scene playing came from the chain */
    int8_t held;                         /* LIVE: the black key held (-1 none), its scene, the groove before it */
    uint8_t held_scene, held_base;
    uint32_t held_t;
    int8_t next_sent;                    /* the scene after the one playing, as last told to the engine */
    uint8_t seen_play;                   /* the transport seen playing since the scenes started (a stop ends them) */
    int8_t state[MC_LANES];              /* each lane: -1 muted, 0 playing, 1 soloed (SEL + K1-K4 on LIVE) */
    uint32_t adv_seen, end_seen;         /* the engine's counts, as last seen */
    uint32_t touch_t[4];                 /* LIVE: when each macro was turned (its gauge lights, the wheel shows it) */
} mc = {MCP_LIVE, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, -2, 0, {0, 0, 0, 0}, 0, 0, {0, 0, 0, 0}};
#define MC_TOUCH_MS 1500u
#define MC_HOLD_MS 350u                  /* a black key held longer: momentary (back to the groove on release) */

static mc_scene_t *mc_edit(void) { return &mcs.sc[mc.scene % MC_SCENES]; }
static mc_lane_t *mc_lane(void) { return &mc_edit()->lane[mc.lane % MC_LANES]; }
static char scene_ch(uint32_t s) { return (char)('A' + s % MC_SCENES); }
static int mc_lane_page(void) { return ui.page == PG_MC && mc.part == MCP_LANE; }
static uint32_t mc_mask(uint32_t len) { return len >= 32u ? 0xFFFFFFFFu : (1u << len) - 1u; }
static int mc_once(uint32_t s) { return s < MC_SCENES && (mcs.sc[s].flags & MCS_ONCE); }
static const char *mc_name(uint32_t s)
{
    uint32_t n = s < MC_SCENES ? mcs.sc[s].name : 0u;
    return n && n < MC_NNAMES ? MC_NAME[n] : "";
}
/* the black keys are the pads (not ARP's effects, not MATHCORE's scenes) */
static int pads_page(void)
{
    return ui.page != PG_ARP && !(ui.page == PG_MC && (mc.part == MCP_LIVE || mc.part == MCP_SCENE));
}

static void mc_mute_out(void)
{
    uint32_t l, m = 0u, s = 0u;
    for (l = 0; l < MC_LANES; l++) {
        if (mc.state[l] < 0)
            m |= 1u << l;
        if (mc.state[l] > 0)
            s |= 1u << l;
    }
    brk_mc_mute(m, s);
}

/* the chain's next entry (the first after the last), -1 none */
static int32_t mc_next_pos(void)
{
    if (!mq.n)
        return -1;
    return (int32_t)((mc.qgone ? mc.qpos : mc.qpos + 1u) % mq.n);
}
/* what plays after the scene playing: a black key held, that scene again; a fill, riser or drop, the groove it came
 * from; with a chain, its next entry; else the scene again. Told to the engine when it changes */
static void mc_push_next(const brk_status_t *st)
{
    int32_t nx, cur = st->mc_scene;
    if (mc.on == 2u)
        nx = mc.scene;
    else if (mc.held >= 0 && plat_ms() - mc.held_t >= MC_HOLD_MS)
        nx = mc.held_scene;
    else if (cur >= 0 && mc_once((uint32_t)cur))
        nx = mc.base;
    else if (mq.n) {
        int32_t p = mc_next_pos();
        nx = mq.q[p < 0 ? 0 : p];
    } else
        nx = cur >= 0 ? cur : mc.base;
    if (nx != mc.next_sent) {
        mc.next_sent = (int8_t)nx;
        brk_mc_next(nx);
    }
}

/* PLAY on a MATHCORE page, stopped: LIVE starts the chain at its first entry, or the groove; LANE and SCENE play the
 * scene edited round and round. 0: not started */
static int mc_play(void)
{
    brk_status_t st;
    if (mc.part == MCP_LIVE) {
        mc.on = 1u;
        mc.qpos = 0u;
        mc.qgone = 0u;
        mc.qled = mq.n != 0u;
        if (mq.n && !mc_once(mq.q[0]))
            mc.base = mq.q[0];
        brk_mc_start(mq.n ? mq.q[0] : mc.base);
    } else {
        mc.on = 2u;
        brk_mc_start(mc.scene);
    }
    brk_status(&st);
    mc.adv_seen = st.mc_adv;
    mc.end_seen = st.mc_ended;
    mc.next_sent = -2;
    mc.seen_play = 0u;
    st.mc_scene = (int8_t)(mc.on == 2u ? mc.scene : mq.n ? mq.q[0] : mc.base);
    mc_push_next(&st);
    mc_mute_out();
    return 1;
}

/* every frame: the next scene for the engine, a boundary passed (the chain moves on), the transport stopped */
static void mc_frame(const brk_status_t *st)
{
    if (!mc.on)
        return;
    if (st->playing)
        mc.seen_play = 1u;
    else if (mc.seen_play) {             /* (stopped: not the frame PLAY was pressed in, before the render ran) */
        mc.on = 0u;
        mc.held = -1;
        brk_mc_start(-1);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    } else
        return;
    if (st->mc_ended != mc.end_seen) {   /* (nothing after it: the lanes stopped) */
        mc.end_seen = st->mc_ended;
        mc.on = 0u;
        transport(0, 0);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    if (st->mc_adv != mc.adv_seen) {     /* a new scene: was it the chain's next? */
        mc.adv_seen = st->mc_adv;
        if (mq.n) {
            int32_t p = mc_next_pos();
            if (p >= 0 && st->mc_scene == (int8_t)mq.q[p] && mc.next_sent == st->mc_scene) {
                mc.qpos = (uint8_t)p;
                mc.qgone = 0u;
                mc.qled = 1u;
                if (!mc_once(mq.q[p]))
                    mc.base = mq.q[p];
            }
        }
        mc.next_sent = -2;               /* (told again below) */
        ui.redraw |= RG_BLOCKS | RG_FOOT;
    }
    mc_push_next(st);
}

/* a black key on LIVE: its scene now. Stopped, it starts there and then. Playing, a groove comes in on the next bar
 * and is the groove from then on; a fill, riser or drop on the next beat, played to its bar line, then the groove
 * again. Held past MC_HOLD_MS, any scene is momentary: it plays while held, the groove back on the next beat after */
static void mc_launch(uint32_t key, uint32_t now)
{
    uint32_t s = mq.key[key % NPFX];
    brk_status_t st;
    if (s >= MC_SCENES) {
        app_warn("NOT PLAYED", "NO SCENE ON THIS KEY");
        return;
    }
    brk_status(&st);
    mc.held = (int8_t)key;
    mc.held_scene = (uint8_t)s;
    mc.held_base = mc.base;
    mc.held_t = now;
    if (!mc_once(s))
        mc.base = (uint8_t)s;
    if (!st.playing) {                   /* stopped: play, from this scene */
        mc.on = 1u;
        mc.qled = 0u;
        mc.next_sent = -2;
        mc.seen_play = 0u;
        brk_mc_start((int)s);
        transport(1, 0);
        muted = 0u;
        mc.adv_seen = st.mc_adv;
        mc.end_seen = st.mc_ended;
        mc_mute_out();
    } else {
        mc.on = 1u;
        mc.qled = 0u;
        brk_mc_jump((int)s, mc_once(s) ? 4u : 16u);
    }
    {
        char t[24];
        t[0] = scene_ch(s);
        t[1] = ' ';
        t[2] = 0;
        text_cat(t, mc_name(s));
        app_say(st.playing ? (mc_once(s) ? "NEXT BEAT" : "NEXT BAR") : "PLAY", t);
    }
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void mc_release(uint32_t now)
{
    if (mc.held < 0)
        return;
    if (now - mc.held_t >= MC_HOLD_MS && mc.on == 1u) {   /* momentary: the groove back on the next beat */
        mc.base = mc.held_base;
        brk_mc_jump(mc.base, 4u);
        mc.next_sent = -2;
        app_say("BACK TO", mc_name(mc.base));
    }
    mc.held = -1;
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

/* the chain: SEL + a black key adds its scene (a song made of scenes, played in order and round again), SEL + REC
 * clears it, ALGORITHM points, PRESETS moves the entry pointed at. The scene playing is never cut */
static void mq_said(const char *what, uint32_t s)
{
    char t[16];
    t[0] = scene_ch(s);
    t[1] = 0;
    text_cat(t, "  (");
    int_text(mq.n, t + 4);
    text_cat(t, ")");
    app_say(what, t);
}
static void mq_append(uint32_t key)
{
    uint32_t s = mq.key[key % NPFX];
    if (s >= MC_SCENES) {
        app_warn("NOT CHAINED", "NO SCENE ON THIS KEY");
        return;
    }
    if (mq.n >= MC_QUEUE) {
        app_warn("NOT CHAINED", "THE CHAIN IS FULL");
        return;
    }
    mq.q[mq.n++] = (uint8_t)s;
    mc.cursor = (uint8_t)(mq.n - 1u);
    if (mq.n == 1u) {                    /* (a first entry: it follows what plays now) */
        mc.qpos = 0u;
        mc.qgone = 1u;
    }
    set_dirty = 1u;
    mq_said("CHAINED", s);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void mq_remove_at(uint32_t j)
{
    uint32_t k;
    for (k = j; k + 1u < mq.n; k++)
        mq.q[k] = mq.q[k + 1u];
    mq.n--;
    if (j < mc.qpos)
        mc.qpos--;
    else if (j == mc.qpos)
        mc.qgone = 1u;
    if (mq.n && mc.qpos >= mq.n)
        mc.qpos = 0u;
    if (mc.cursor >= mq.n)
        mc.cursor = (uint8_t)(mq.n ? mq.n - 1u : 0u);
    set_dirty = 1u;
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void mq_remove_last(uint32_t key)
{
    uint32_t s = mq.key[key % NPFX], j;
    for (j = mq.n; j-- > 0u;)
        if (mq.q[j] == s) {
            mq_remove_at(j);
            mq_said("OUT OF THE CHAIN", s);
            return;
        }
    app_warn("NOT REMOVED", "NOT IN THE CHAIN");
}
static void mq_clear(void)
{
    mq.n = 0u;
    mc.cursor = 0u;
    mc.qgone = 1u;
    mc.qled = 0u;
    set_dirty = 1u;
    app_say("CHAIN CLEARED", "THE GROOVE PLAYS ON");
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void mq_move(int32_t d)           /* the entry at the cursor a place later (right) or earlier (left) */
{
    uint32_t a = mc.cursor, b;
    uint8_t t;
    if (mq.n < 2u)
        return;
    b = d > 0 ? (a + 1u) % mq.n : (a + mq.n - 1u) % mq.n;
    t = mq.q[a];
    mq.q[a] = mq.q[b];
    mq.q[b] = t;
    if (!mc.qgone) {
        if (mc.qpos == a)
            mc.qpos = (uint8_t)b;
        else if (mc.qpos == b)
            mc.qpos = (uint8_t)a;
    }
    mc.cursor = (uint8_t)b;
    set_dirty = 1u;
    mq_said("MOVED", mq.q[b]);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

/* the black keys on MATHCORE's LIVE (play; SEL: chain; let go: back from a hold) and SCENE (this key plays the scene
 * edited; SEL: none). 1: they are its */
static int mc_black(uint32_t kdn, uint32_t kup, uint32_t btn, uint32_t now)
{
    uint32_t i;
    if (mc.held >= 0 && kup >> BK[mc.held] & 1u)   /* (let go on any page: a hold ends) */
        mc_release(now);
    if (ui.page != PG_MC || (mc.part != MCP_LIVE && mc.part != MCP_SCENE))
        return 0;
    for (i = 0; i < NPFX; i++) {
        if (!(kdn >> BK[i] & 1u))
            continue;
        if (btn & 1u << B_SEL)
            ui.tap_combo = 1u;           /* (not SEL's card) */
        if (mc.part == MCP_LIVE) {
            if (btn & 1u << B_REC)       /* REC held: its last entry out of the chain */
                mq_remove_last(i);
            else if (btn & 1u << B_SEL)
                mq_append(i);
            else
                mc_launch(i, now);
        } else {
            char t[8] = "KEY ";
            int_text((int32_t)i + 1, t + 4);
            mq.key[i] = btn & 1u << B_SEL ? MC_NOKEY : mc.scene;
            set_dirty = 1u;
            if (mq.key[i] == MC_NOKEY)
                app_say(t, "NO SCENE");
            else {
                char s[10] = "SCENE ";
                s[6] = scene_ch(mc.scene);
                s[7] = 0;
                app_say(t, s);
            }
            ui.redraw |= RG_BLOCKS | RG_FOOT;
        }
    }
    return 1;
}

/* the white keys on LANE: an event on or off (SEL: events 17..32); FEEL: its accent */
static void mc_white(uint32_t i, uint32_t btn)
{
    mc_lane_t *L = mc_lane();
    uint32_t e = i + (btn & 1u << B_SEL ? 16u : 0u), *m = mc.kset == MKS_FEEL ? &L->accent : &L->hits;
    if (btn & 1u << B_SEL)
        ui.tap_combo = 1u;
    *m ^= 1u << e;
    set_dirty = 1u;
    if (e >= L->len)
        app_warn("NOT IN THE CYCLE", "LONGER: RHYTHM K1");
    else
        say_int(mc.kset == MKS_FEEL ? (*m >> e & 1u ? "ACCENT" : "SOFT") : (*m >> e & 1u ? ROLE_NAME[L->role % MC_ROLES]
                : "OFF"), (int32_t)e + 1, "");
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

/* the role's slices in the break playing (MAP and SOUND's PICK) */
static uint32_t mc_role_list(uint32_t role, uint8_t *list)
{
    const brk_break_t *b = brk_get_break();
    return b ? mc_role_slices(roles_now, b->nslices, role % MC_ROLES, list) : 0u;
}

static void mc_value(int i, char *t)     /* MATHCORE's knob i, as the foot shows it */
{
    mc_scene_t *S = mc_edit();
    mc_lane_t *L = mc_lane();
    t[0] = 0;
    if (mc.part == MCP_LIVE) {           /* the macros (SEL held: the lanes' mute / solo) */
        uint32_t v = set.mc_macro[i];
        if (ui.btn & 1u << B_SEL)
            text_cat(t, MC_STATE_NAME[mc.state[i] + 1]);
        else if (i == 1) {               /* MATH: the lanes' lengths at this step */
            const uint8_t *m = MC_MATH_LEN[v * (MC_MATH_STEPS - 1u) / 100u];
            if (!m[0] && !m[3])
                text_cat(t, "OFF");
            else {
                uint32_t l;
                for (l = 0; l < 4u; l++) {
                    char n[4];
                    int_text(m[l] ? m[l] : mcs.sc[0].lane[l].len, n);
                    if (l)
                        text_cat(t, " ");
                    text_cat(t, n);
                }
            }
        } else if (i == 2)
            text_cat(t, v < 34u ? "HALF" : v > 66u ? "DOUBLE" : "NORMAL");
        else {
            int_text((int32_t)v, t);
            text_cat(t, "%");
        }
        return;
    }
    if (mc.part == MCP_SCENE) {
        if (i == 0) {
            int_text(S->bars, t);
            text_cat(t, S->bars == 1u ? " BAR" : " BARS");
        } else if (i == 1)
            text_cat(t, S->flags & MCS_BREAK ? "YES" : "DRY");
        else if (i == 2)
            text_cat(t, "< >");
        else
            text_cat(t, S->flags & MCS_ONCE ? "ONCE" : "LOOP");
        return;
    }
    switch (mc.kset * 4u + (uint32_t)i) {
    case 0: int_text(L->len, t); break;
    case 1: text_cat(t, MC_RATE_NAME[L->rate % MC_RATES]); break;
    case 2: text_cat(t, "< >"); break;
    case 3: int_text(L->phase, t); break;
    case 4: text_cat(t, MC_MODE_NAME[L->mode % MC_MODES]); break;
    case 5: {
        uint8_t list[BRK_MAX_SLICES];
        uint32_t n = mc_role_list(L->role, list);
        if (!n)
            text_cat(t, "NONE");
        else {
            text_cat(t, "SLICE ");
            int_text(list[L->pick % n] + 1, t + 6);
        }
        break;
    }
    case 6: int_text(L->pitch, t); text_cat(t, " ST"); break;
    case 7: int_text(L->level, t); text_cat(t, "%"); break;
    case 8: int_text(L->prob, t); text_cat(t, "%"); break;
    case 9: text_cat(t, "X"); int_text(L->ratchet ? L->ratchet : 1, t + 1); break;
    case 10: int_text(L->gate, t); text_cat(t, "%"); break;
    case 11: int_text(L->vary, t); text_cat(t, "%"); break;
    case 12: text_cat(t, ROLE_NAME[L->role % MC_ROLES]); break;
    case 13:
        if (!L->rise)
            text_cat(t, "OFF");
        else {
            int_text(L->rise, t);
            text_cat(t, " ST");
        }
        break;
    case 14: text_cat(t, L->flags & MCL_REV ? "ON" : "OFF"); break;
    default: text_cat(t, L->flags & MCL_ON ? "ON" : "OFF"); break;
    }
}
static const char *mc_knob_name(uint32_t i)
{
    static const char *const SC[4] = {"PHRASE", "RUN ON", "NEW / COPY", "PLAYS"};
    if (mc.part == MCP_LIVE)
        return ui.btn & 1u << B_SEL ? ROLE_NAME[i] : MACRO_NAME[i];
    return mc.part == MCP_SCENE ? SC[i] : MC_KNOB[mc.kset % MKSETS][i];
}

static void mc_turn(int i, int32_t d)
{
    mc_scene_t *S = mc_edit();
    mc_lane_t *L = mc_lane();
    char t[16];
    uint32_t len = L->len < 1u ? 1u : L->len;
    if (mc.part == MCP_LIVE && ui.btn & 1u << B_SEL) {   /* SEL + a knob: its lane, left MUTE, right SOLO */
        ui.tap_combo = 1u;
        mc.state[i] = (int8_t)clamp(mc.state[i] + (d > 0 ? 1 : -1), -1, 1);
        mc_mute_out();
        app_say(ROLE_NAME[i], MC_STATE_NAME[mc.state[i] + 1]);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    if (mc.part == MCP_LIVE) {           /* the macros: played, not programmed */
        uint8_t *v = &set.mc_macro[i];
        if (i == 2)                      /* SPEED: HALF, NORMAL, DOUBLE */
            *v = (uint8_t)clamp((int32_t)*v + (d > 0 ? 50 : -50), 0, 100);
        else
            *v = (uint8_t)clamp((int32_t)*v + d * 4, 0, 100);
        changed();
        mc.touch_t[i] = plat_ms() | 1u;
        mc_value(i, t);
        app_say(MACRO_NAME[i], t);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    if (mc.part == MCP_SCENE) {
        if (i == 0) {                    /* the phrase: 1, 2, 4, 8 bars */
            uint32_t b = S->bars;
            b = d > 0 ? (b >= 8u ? 8u : b < 1u ? 1u : b * 2u) : (b <= 1u ? 1u : b / 2u);
            S->bars = (uint8_t)b;
        } else if (i == 1)
            S->flags = (uint8_t)(d > 0 ? S->flags | MCS_BREAK : S->flags & ~MCS_BREAK);
        else if (i == 2) {               /* left: the scene back to its start; right: a copy of the one before */
            if (d > 0)
                *S = mcs.sc[(mc.scene + MC_SCENES - 1u) % MC_SCENES];
            else
                mc_scene_defaults(S, mc.scene);
            app_say(d > 0 ? "COPIED" : "NEW SCENE", "");
            set_dirty = 1u;
            ui.redraw |= RG_BLOCKS | RG_FOOT;
            return;
        } else                           /* PLAYS: LOOP (a groove) or ONCE (a fill: then back to the groove) */
            S->flags = (uint8_t)(d > 0 ? S->flags | MCS_ONCE : S->flags & ~MCS_ONCE);
    } else
        switch (mc.kset * 4u + (uint32_t)i) {
        case 0:
            L->len = (uint8_t)clamp((int32_t)L->len + d, 1, (int32_t)MC_EVENTS);
            if (L->phase >= L->len)
                L->phase = 0u;
            break;
        case 1: L->rate = (uint8_t)((L->rate + (d > 0 ? 1u : MC_RATES - 1u)) % MC_RATES); break;
        case 2: {                        /* the cycle's hits and accents round a step (right: later) */
            uint32_t m = mc_mask(len), *p[2] = {&L->hits, &L->accent}, k;
            for (k = 0; k < 2u; k++) {
                uint32_t x = *p[k] & m;
                x = d > 0 ? (x << 1 | x >> (len - 1u)) & m : (x >> 1 | (x & 1u) << (len - 1u)) & m;
                *p[k] = (*p[k] & ~m) | x;
            }
            break;
        }
        case 3: L->phase = (uint8_t)(((int32_t)L->phase + d % (int32_t)len + (int32_t)len) % (int32_t)len); break;
        case 4: L->mode = (uint8_t)((L->mode + (d > 0 ? 1u : MC_MODES - 1u)) % MC_MODES); break;
        case 5: {
            uint8_t list[BRK_MAX_SLICES];
            uint32_t n = mc_role_list(L->role, list);
            L->pick = (uint8_t)(n ? ((uint32_t)L->pick % n + (d > 0 ? 1u : n - 1u)) % n : 0u);
            break;
        }
        case 6: L->pitch = (int8_t)clamp(L->pitch + d, -12, 12); break;
        case 7: L->level = (uint8_t)clamp((int32_t)L->level + d * 5, 0, 100); break;
        case 8: L->prob = (uint8_t)clamp((int32_t)L->prob + d * 5, 0, 100); break;
        case 9: L->ratchet = (uint8_t)clamp((int32_t)(L->ratchet ? L->ratchet : 1u) + (d > 0 ? 1 : -1), 1, 4); break;
        case 10: L->gate = (uint8_t)clamp((int32_t)L->gate + d * 5, 5, 100); break;
        case 11: L->vary = (uint8_t)clamp((int32_t)L->vary + d * 5, 0, 100); break;
        case 12: L->role = (uint8_t)((L->role + (d > 0 ? 1u : MC_ROLES - 1u)) % MC_ROLES); L->pick = 0u; break;
        case 13: L->rise = (int8_t)clamp(L->rise + d, -12, 24); break;
        case 14: L->flags = (uint8_t)(d > 0 ? L->flags | MCL_REV : L->flags & ~MCL_REV); break;
        default: L->flags = (uint8_t)(d > 0 ? L->flags | MCL_ON : L->flags & ~MCL_ON); break;
        }
    set_dirty = 1u;
    mc_value(i, t);
    app_say(mc_knob_name((uint32_t)i), t);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

static void mc_algo(int32_t d)           /* ALGORITHM: LIVE the chain's cursor, LANE the lane, SCENE the scene */
{
    char t[16];
    if (mc.part == MCP_LIVE) {
        if (!mq.n)
            return;
        mc.cursor = (uint8_t)((mc.cursor + (d > 0 ? 1u : mq.n - 1u)) % mq.n);
        t[0] = scene_ch(mq.q[mc.cursor]);
        t[1] = 0;
        say_int("CHAIN", mc.cursor + 1, "");
        app_say(ui.msg[0], t);
    } else if (mc.part == MCP_LANE) {
        mc.lane = (uint8_t)((mc.lane + (d > 0 ? 1u : MC_LANES - 1u)) % MC_LANES);
        app_say("LANE", ROLE_NAME[mc_lane()->role % MC_ROLES]);
    } else {
        mc.scene = (uint8_t)((mc.scene + (d > 0 ? 1u : MC_SCENES - 1u)) % MC_SCENES);
        t[0] = scene_ch(mc.scene);
        t[1] = 0;
        app_say("SCENE", t);
    }
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void mc_presets(int32_t d)        /* PRESETS: LIVE moves the entry at the cursor, LANE turns the knob sets */
{
    if (mc.part == MCP_LIVE)
        mq_move(d);
    else if (mc.part == MCP_LANE) {
        mc.kset = (uint8_t)((mc.kset + (d > 0 ? 1u : MKSETS - 1u)) % MKSETS);
        app_say("LANE", MKS_NAME[mc.kset]);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
    } else
        pattern_pick(d);
}

/* the white keys' lights on LANE: the lane's events of the 16 shown (SEL held: 17..32), the one playing flipped */
static uint32_t mc_keys_lit(const brk_status_t *st)
{
    const mc_lane_t *L = mc_lane();
    uint32_t sh = ui.btn & 1u << B_SEL ? 16u : 0u, m = ((mc.kset == MKS_FEEL ? L->accent : L->hits) >> sh) & 0xFFFFu;
    if (st->mc_scene == (int8_t)mc.scene && st->mc_pos[mc.lane % MC_LANES] >= sh &&
        st->mc_pos[mc.lane % MC_LANES] < sh + 16u)
        m ^= 1u << (st->mc_pos[mc.lane % MC_LANES] - sh);
    return m;
}
/* the black keys' lights: LIVE the scene playing (blinking: the one coming next), SCENE the keys of the scene edited;
 * the keys with a scene glow (mc_black_mid) */
static uint32_t mc_black_lit(const brk_status_t *st)
{
    uint32_t i, m = 0u, blink = st->bar16 >= 0 && (st->bar16 & 2);
    for (i = 0; i < NPFX; i++) {
        uint32_t s = mq.key[i];
        if (s >= MC_SCENES)
            continue;
        if (mc.part == MCP_SCENE ? s == mc.scene
                                 : (st->playing && st->mc_scene == (int8_t)s) ||
                                   (blink && st->playing && st->mc_next == (int8_t)s && st->mc_scene != (int8_t)s))
            m |= 1u << i;
    }
    return m;
}
static uint32_t mc_black_mid(void)
{
    uint32_t i, m = 0u;
    for (i = 0; i < NPFX; i++)
        if (mq.key[i] < MC_SCENES)
            m |= 1u << BK[i];
    return m;
}
/* the foot's pad i on LIVE and SCENE (x: its box, 20 x 14): the key's scene letter. LIVE: playing lit, held white,
 * coming next outlined and blinking on the beat, a fill / riser / drop (ONCE) marked underneath; SCENE: the keys of
 * the scene edited lit */
static void mc_draw_pad(uint32_t i, const brk_status_t *st, int32_t x)
{
    uint32_t s = mq.key[i];
    int have = s < MC_SCENES, run = st->playing && st->mc_scene >= 0;
    int now = have && (mc.part == MCP_SCENE ? s == mc.scene : run && (uint32_t)st->mc_scene == s);
    int held = mc.part == MCP_LIVE && mc.held == (int8_t)i;
    int nx = mc.part == MCP_LIVE && have && run && !now && st->mc_next == (int8_t)s;
    int blink = st->bar16 >= 0 && !(st->bar16 & 2);
    uint16_t fill = held ? C_WHITE : now ? C_HI : C_BLOCK, ink = held || now ? C_BLACK : have ? C_HI : C_LINE;
    char t[2];
    if (nx) {                            /* next: a white frame, its inside lit on the beat */
        cv_rect(x, 0, 20, 14, C_WHITE);
        cv_rect(x + 1, 1, 18, 12, blink ? C_BLOCK_HI : C_BLOCK);
        ink = blink ? C_BLACK : C_WHITE;
    } else
        cv_rect(x, 0, 20, 14, fill);
    t[0] = have ? scene_ch(s) : '-';
    t[1] = 0;
    cv_text(x + (20 - text_w(&FONT_T, t)) / 2, 1, &FONT_T, t, ink);
    if (have && mc_once(s))
        cv_rect(x + 6, 12, 8, 2, held || now ? C_BLACK : C_GRAY);
}

/* ------------------------------------------------------------------ drawing --- */
/* the four lanes of scene s, a row each from y: the role, the cycle (its hits amber, accents white, the event
 * playing outlined), muted dim, soloed marked, a role with no slice an amber "!" */
static void mc_rows(const brk_status_t *st, uint32_t s, int32_t y, int sel)
{
    const mc_scene_t *S = &mcs.sc[s % MC_SCENES];
    uint32_t l, e;
    int playing = st->playing && st->mc_scene == (int8_t)s;
    for (l = 0; l < MC_LANES; l++, y += 12) {
        const mc_lane_t *L = &S->lane[l];
        uint32_t len = L->len < 1u ? 1u : L->len > MC_EVENTS ? MC_EVENTS : L->len, w = 192u / len;
        int on = (L->flags & MCL_ON) != 0u, quiet = mc.state[l] < 0 || (!mc.state[l] && (mc.state[0] > 0 ||
                                     mc.state[1] > 0 || mc.state[2] > 0 || mc.state[3] > 0));
        uint8_t list[BRK_MAX_SLICES];
        char t[4];
        if (w > 12u)
            w = 12u;
        t[0] = ROLE_CH[L->role % MC_ROLES];
        t[1] = 0;
        if ((int)l == sel)
            cv_rect(0, y, 12, 11, C_BLOCK);
        cv_text(2, y, &FONT_XS, t, !on || quiet ? C_DIM : mc.state[l] > 0 ? C_WHITE : C_HI);
        for (e = 0; e < len; e++) {
            int32_t x = 14 + (int32_t)(e * w);
            int hit = L->hits >> e & 1u, acc = L->accent >> e & 1u, now = playing && st->mc_pos[l] == e;
            uint16_t c = hit ? (!on || quiet ? C_DIM : acc ? C_WHITE : C_AMB) : (e & 3u) ? C_BLOCK : C_LINE;
            cv_rect(x, y + 1, (int32_t)w - 1, 9, now ? C_WHITE : c);
            if (now)
                cv_rect(x + 1, y + 2, (int32_t)w - 3, 7, hit ? C_BLOCK_HI : C_BLOCK);
        }
        if (on && !mc_role_list(L->role, list))
            cv_text(212, y, &FONT_XS, "!", C_AMB);   /* (its role has no slices in this break: silent) */
        else if (on) {
            int_text((int32_t)len, t);
            cv_text(212, y, &FONT_XS, t, C_GRAY);
        } else
            cv_text(212, y, &FONT_XS, "-", C_DIM);
    }
}

/* LIVE's picture (240 x 104, drawn in two strips: top is the strip's first row). On the left the wheel: a ring per
 * lane (the kick inside, the hat outside), its cycle's steps round it, the hits lit, the step playing white with a
 * trail, so the polyrhythm is seen turning, and the macros on it: DENSITY's added hits sparkle white and its dropped
 * ones fade, MATH re-divides the rings, CHAOS shakes them and throws sparks, SPEED leaves a longer trail (x2, 1/2 in
 * the middle). On the right the scene playing, what comes next, its phrase, the four macros' gauges (the one turned
 * lit) and the chain. The black keys' scenes are the foot's pads (mc_draw_pad) */
#define WH_X 54
#define WH_Y 52
static const uint8_t WH_R[MC_LANES] = {27u, 35u, 50u, 43u};   /* the rings: kick, snare, hat, perc */
static int mc_touched(uint32_t i, uint32_t now) { return mc.touch_t[i] && now - mc.touch_t[i] < MC_TOUCH_MS; }
static int mc_animating(uint32_t now)    /* stopped, LIVE still moves: CHAOS shaking, a gauge lit (and once more after) */
{
    uint32_t i;
    if (set.mc_macro[3])
        return 1;
    for (i = 0; i < 4u; i++)
        if (mc.touch_t[i] && now - mc.touch_t[i] < MC_TOUCH_MS + 120u)
            return 1;
    return 0;
}
static uint32_t mc_shake(uint32_t a, uint32_t b, uint32_t c)  /* a cheap hash for CHAOS's shake */
{
    uint32_t x = a * 0x9E3779B1u ^ b * 0x85EBCA6Bu ^ c * 0xC2B2AE35u;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    return x ^ x >> 13;
}
static void mc_gauge(int32_t y, uint32_t i, uint32_t now)   /* macro i's row: its name and its gauge */
{
    uint32_t v = set.mc_macro[i], k;
    int hot = mc_touched(i, now);
    uint16_t on = hot ? C_WHITE : C_HI, off = hot ? C_LINE : C_BLOCK;
    if (hot)
        cv_rect(110, y - 1, 130, 12, C_BLOCK);
    cv_text(112, y - 2, &FONT_T, MACRO_NAME[i], hot ? C_WHITE : C_GRAY);
    if (i == 0u) {                       /* DENSITY: from the middle, left fewer, right more */
        int32_t w = ((int32_t)v - 50) * 36 / 50;
        cv_rect(162, y + 3, 74, 4, off);
        cv_rect(w < 0 ? 199 + w : 199, y + 2, w < 0 ? -w : w, 6, on);
        cv_rect(198, y, 2, 10, hot ? C_WHITE : C_GRAY);
    } else if (i == 1u) {                /* MATH: its seven steps */
        uint32_t st = v * (MC_MATH_STEPS - 1u) / 100u;
        for (k = 0; k < MC_MATH_STEPS - 1u; k++)
            cv_rect(162 + (int32_t)k * 11, y + 2, 9, 6, k < st ? on : off);
    } else if (i == 2u) {                /* SPEED: HALF, NORMAL, DOUBLE */
        static const char *const SP[3] = {"1/2", "x1", "x2"};
        uint32_t st = v < 34u ? 0u : v > 66u ? 2u : 1u;
        for (k = 0; k < 3u; k++) {
            cv_rect(162 + (int32_t)k * 25, y, 23, 10, k == st ? on : off);
            cv_text(165 + (int32_t)k * 25, y - 2, &FONT_T, SP[k], k == st ? C_BLACK : C_GRAY);
        }
    } else {                             /* CHAOS: a jagged bar, the more the wilder */
        uint32_t w = v * 74u / 100u, f = now / 70u;
        cv_rect(162, y + 3, 74, 4, off);
        for (k = 0; k < w; k += 2u) {
            int32_t h = 2 + (int32_t)(mc_shake(k, f, 7u) % (2u + v / 12u));
            cv_rect(162 + (int32_t)k, y + 5 - h / 2, 2, h, on);
        }
    }
}
static void mc_live_picture(const brk_status_t *st, int32_t top)
{
    int run = st->playing && st->mc_scene >= 0;
    uint32_t sc = run ? (uint32_t)st->mc_scene : mc.base, l, e, i, now = plat_ms();
    const mc_scene_t *S = &mcs.sc[sc % MC_SCENES];
    uint32_t dens = set.mc_macro[0], chaos = set.mc_macro[3], spd = set.mc_macro[2];
    const uint8_t *ml = MC_MATH_LEN[set.mc_macro[1] * (MC_MATH_STEPS - 1u) / 100u];
    int32_t jit = (int32_t)(chaos * 4u / 100u);
    uint32_t frame = now / 80u, flash = mc_touched(0u, now) && (now / 120u & 1u);
    char t[24];
    cv_oy = -top;
    for (l = 0; l < MC_LANES; l++) {     /* the rings */
        const mc_lane_t *L = &S->lane[l];
        uint32_t len = ml[l] ? ml[l] : L->len < 1u ? 1u : L->len, r = WH_R[l];
        uint32_t trail = spd > 66u ? 3u : spd < 34u ? 1u : 2u, p = run ? st->mc_pos[l] : 0xFFu;
        int on = (L->flags & MCL_ON) != 0u, quiet = mc.state[l] < 0 || (!mc.state[l] && (mc.state[0] > 0 ||
                                     mc.state[1] > 0 || mc.state[2] > 0 || mc.state[3] > 0));
        if (!on)
            continue;
        for (e = 0; e < len; e++) {
            float a = (float)e / (float)len;
            int32_t x = WH_X + (int32_t)((float)r * brk_sin_turns(a) + (a < 0.5f ? 0.5f : -0.5f));
            int32_t y = WH_Y - (int32_t)((float)r * brk_sin_turns(a + 0.25f) + 0.5f);
            uint32_t kind = mc_event_kind(L, l, e, dens), back = (p + len - e) % len;
            if (jit) {                   /* CHAOS: the ring shakes */
                uint32_t h = mc_shake(e, l, frame);
                x += (int32_t)(h % (uint32_t)(2 * jit + 1)) - jit;
                y += (int32_t)(h >> 8 & 0xFFu) % (2 * jit + 1) - jit;
            }
            if (p == e)
                cv_rect(x - 3, y - 3, 7, 7, C_WHITE);
            else if (p != 0xFFu && back < trail && (kind == MCE_HIT || kind == MCE_ACCENT || kind == MCE_ADDED))
                cv_rect(x - 2, y - 2, 5, 5, C_GRAY);   /* (the trail: where it just was) */
            else if (kind == MCE_ADDED)  /* DENSITY up: an added hit, white, flashing while it is turned */
                cv_rect(x - 1 - (int32_t)flash, y - 1 - (int32_t)flash, 3 + 2 * (int32_t)flash, 3 + 2 * (int32_t)flash,
                        quiet ? C_GRAY : C_WHITE);
            else if (kind == MCE_DROPPED) {   /* DENSITY down: a hit left out, its outline only */
                cv_rect(x - 2, y - 2, 4, 1, C_LINE);
                cv_rect(x - 2, y + 1, 4, 1, C_LINE);
            } else if (kind)
                cv_rect(x - 2, y - 2, kind == MCE_ACCENT ? 5 : 4, kind == MCE_ACCENT ? 5 : 4,
                        quiet ? C_DIM : kind == MCE_ACCENT ? C_BLOCK_HI : C_AMB);
            else
                cv_rect(x, y, 2, 2, quiet ? C_LINE : C_DIM);
        }
    }
    for (i = 0; i < chaos / 8u; i++) {   /* CHAOS: sparks */
        uint32_t h = mc_shake(i, frame, 99u), r = 14u + h % 40u;
        float a = (float)(h >> 8 & 1023u) * (1.0f / 1024.0f);
        cv_rect(WH_X + (int32_t)((float)r * brk_sin_turns(a)), WH_Y - (int32_t)((float)r * brk_sin_turns(a + 0.25f)),
                1, 1, h & 0x10000u ? C_WHITE : C_HI);
    }
    t[0] = scene_ch(sc);                 /* the scene, in the middle; SPEED's badge under it */
    t[1] = 0;
    cv_text2(WH_X - text2_w(&FONT_B, t) / 2, WH_Y - 19, &FONT_B, t, run ? C_HI : C_GRAY);
    if (spd < 34u || spd > 66u) {
        const char *b = spd > 66u ? "x2" : "1/2";
        cv_text(WH_X - text_w(&FONT_T, b) / 2, WH_Y + 11, &FONT_T, b, mc_touched(2u, now) ? C_WHITE : C_AMB);
    }
    t[0] = scene_ch(sc);                 /* the scene's name, its phrase so far, what comes next */
    t[1] = ' ';
    t[2] = 0;
    text_cat(t, mc_name(sc));
    cv_text(112, -1, &FONT_XS, t, run ? C_WHITE : C_GRAY);
    if (run) {
        uint32_t bars = S->bars ? S->bars : 1u, w = (uint32_t)st->mc_step * 124u / (bars * 16u);
        cv_rect(112, 15, 124, 3, C_BLOCK);
        cv_rect(112, 15, (int32_t)(w > 124u ? 124u : w), 3, C_HI);
        for (i = 1; i < bars; i++)
            cv_rect(112 + (int32_t)(i * 124u / bars), 13, 1, 7, C_LINE);
    }
    if (run && st->mc_next >= 0 && st->mc_next != st->mc_scene) {
        t[0] = '>';
        t[1] = ' ';
        t[2] = scene_ch((uint32_t)st->mc_next);
        t[3] = ' ';
        t[4] = 0;
        text_cat(t, mc_name((uint32_t)st->mc_next));
        cv_text(112, 20, &FONT_T, t, C_HI);
    } else
        cv_text(112, 20, &FONT_T, run ? "> AGAIN" : "PLAY OR A BLACK KEY", C_DIM);
    for (i = 0; i < 4u; i++)             /* the macros */
        mc_gauge(38 + (int32_t)i * 14, i, now);
    if (mq.n) {                          /* the chain */
        uint32_t first = mc.cursor > 9u ? mc.cursor - 9u : 0u;
        cv_text(112, 92, &FONT_T, ">>", C_DIM);
        for (i = first; i < mq.n && i < first + 10u; i++) {
            int32_t x = 126 + (int32_t)(i - first) * 11;
            int nw = run && mc.qled && i == mc.qpos && !mc.qgone;
            t[0] = scene_ch(mq.q[i]);
            t[1] = 0;
            if (i == mc.cursor)
                cv_rect(x - 1, 93, 10, 11, C_LINE);
            cv_text(x, 92, &FONT_T, t, nw ? C_WHITE : C_AMB);
        }
    }
    cv_oy = 0;
}

static void mc_draw_live(const brk_status_t *st)
{
    cv_begin(240, 52, C_BLACK);
    mc_live_picture(st, 0);
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    mc_live_picture(st, 52);
    cv_blit(0, 140);
}

static void mc_draw_lane(const brk_status_t *st)
{
    const mc_lane_t *L = mc_lane();
    uint32_t e, len = L->len < 1u ? 1u : L->len;
    int playing = st->playing && st->mc_scene == (int8_t)mc.scene;
    char t[40];
    cv_begin(240, 52, C_BLACK);
    t[0] = scene_ch(mc.scene);
    t[1] = 0;
    text_cat(t, "  ");
    text_cat(t, ROLE_NAME[L->role % MC_ROLES]);
    text_cat(t, L->flags & MCL_ON ? "  " : "  OFF  ");
    {
        char n[8];
        int_text((int32_t)len, n);
        text_cat(t, n);
    }
    text_cat(t, " X ");
    text_cat(t, MC_RATE_NAME[L->rate % MC_RATES]);
    cv_text(4, 0, &FONT_XS, t, C_GRAY);
    cv_text(236 - text_w(&FONT_XS, MKS_NAME[mc.kset % MKSETS]), 0, &FONT_XS, MKS_NAME[mc.kset % MKSETS], C_HI);
    for (e = 0; e < MC_EVENTS; e++) {    /* the 32 events: 1..16, then 17..32 (SEL + a key) */
        int32_t x = 1 + (int32_t)(e % 16u) * 15, y = 15 + (int32_t)(e / 16u) * 18;
        int in = e < len, hit = L->hits >> e & 1u, acc = L->accent >> e & 1u, now = playing && st->mc_pos[mc.lane % MC_LANES] == e;
        uint16_t c = !in ? C_BLACK : hit ? (acc ? C_WHITE : C_AMB) : acc && mc.kset == MKS_FEEL ? C_GRAY
                   : (e & 3u) ? C_BLOCK : C_LINE;
        cv_rect(x, y, 13, 15, now ? C_WHITE : c);
        if (now)
            cv_rect(x + 2, y + 2, 9, 11, hit ? C_BLOCK_HI : C_BLOCK);
        if (!in)
            cv_rect(x, y + 7, 13, 1, C_LINE);
        else if (acc && mc.kset == MKS_FEEL)   /* FEEL: the accents marked */
            cv_rect(x + 3, y + 11, 7, 2, hit ? C_BLACK : C_AMB);
    }
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    mc_rows(st, mc.scene, 2, (int)mc.lane);
    cv_blit(0, 140);
}

static void mc_draw_scene(const brk_status_t *st)
{
    const mc_scene_t *S = mc_edit();
    uint32_t i, n = 0u;
    char t[32];
    cv_begin(240, 52, C_BLACK);
    t[0] = scene_ch(mc.scene);
    t[1] = 0;
    cv_rect(2, 2, 44, 44, st->mc_scene == (int8_t)mc.scene && st->playing ? C_BLOCK_HI : C_BLOCK);
    cv_text2(24 - text2_w(&FONT_B, t) / 2, 6, &FONT_B, t, C_HI);
    t[0] = 0;
    int_text(S->bars, t);
    text_cat(t, S->bars == 1u ? " BAR" : " BARS");
    text_cat(t, S->flags & MCS_BREAK ? "  RUNS ON" : "  DRY CHOPS");
    cv_text(54, 2, &FONT_XS, t, C_WHITE);
    t[0] = 0;
    text_cat(t, "KEYS:");
    for (i = 0; i < NPFX; i++)           /* the black keys that queue it */
        if (mq.key[i] == mc.scene) {
            char k[4] = " ";
            int_text((int32_t)i + 1, k + 1);
            text_cat(t, k);
            n++;
        }
    if (!n)
        text_cat(t, " NONE (A BLACK KEY SETS ONE)");
    cv_text(54, 18, &FONT_XS, t, n ? C_HI : C_GRAY);
    for (i = n = 0u; i < mq.n; i++)
        n += mq.q[i] == mc.scene;
    t[0] = 0;
    text_cat(t, "IN THE CHAIN: ");
    int_text((int32_t)n, t + 14);
    cv_text(54, 34, &FONT_XS, t, C_GRAY);
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    mc_rows(st, mc.scene, 2, -1);
    cv_blit(0, 140);
}

static void draw_mc(const brk_status_t *st)
{
    if (mc.part == MCP_LIVE)
        mc_draw_live(st);
    else if (mc.part == MCP_LANE)
        mc_draw_lane(st);
    else
        mc_draw_scene(st);
}
