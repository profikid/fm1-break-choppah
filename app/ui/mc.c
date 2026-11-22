/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: MATHCORE (EDIT): LIVE (the queue, the black keys append scenes), MAP (the Slice Map, heard: which slices
 * are kicks, snares, hats, the rest), LANE (one lane of a scene: its rhythm, its slices, its feel) and SCENE (its
 * phrase, the break under it, its black key). docs/PRD_MATHCORE.md. (Part of app.c's unity: included there in order.) */

enum { MCP_LIVE, MCP_MAP, MCP_LANE, MCP_SCENE, MCPARTS };
static const char *const MCPART_NAME[MCPARTS] = {"LIVE", "MAP", "LANE", "SCENE"};
enum { MKS_RHYTHM, MKS_SOUND, MKS_FEEL, MKS_MORE, MKSETS };   /* LANE's knob sets (PRESETS turns them) */
static const char *const MKS_NAME[MKSETS] = {"RHYTHM", "SOUND", "FEEL", "MORE"};
static const char *const MC_KNOB[MKSETS][4] = {{"LENGTH", "RATE", "ROTATE", "PHASE"},
                                               {"MODE", "PICK", "PITCH", "LEVEL"},
                                               {"CHANCE", "RATCHET", "GATE", "REVERSE"},
                                               {"ROLE", "SEED", "RESET", "LANE"}};
static const char *const ROLE_NAME[MC_ROLES] = {"KICK", "SNARE", "HAT", "PERC"};
static const char ROLE_CH[MC_ROLES] = {'K', 'S', 'H', 'P'};
static const char *const MC_STATE_NAME[3] = {"MUTE", "PLAY", "SOLO"};

static struct {
    uint8_t part;                        /* MCP_* */
    uint8_t scene;                       /* the scene LANE and SCENE edit (and play, from there) */
    uint8_t lane;                        /* the lane LANE edits */
    uint8_t kset;                        /* LANE's knob set */
    uint8_t slice;                       /* MAP: the slice shown (the white key pressed last) */
    uint8_t cursor;                      /* LIVE: the queue's entry ALGORITHM points at (PRESETS moves it) */
    uint8_t on;                          /* the scenes play: 1 the queue, 2 the scene edited, round and round */
    uint8_t qpos;                        /* the queue's entry playing */
    uint8_t qgone;                       /* it was taken out of the queue: the next is the one now in its place */
    int8_t state[MC_LANES];              /* each lane: -1 muted, 0 playing, 1 soloed (LIVE's K1-K4) */
    uint32_t adv_seen, end_seen;         /* the engine's counts, as last seen */
} mc;

/* the black keys are the pads (not ARP's effects, not MATHCORE's scenes) */
static int pads_page(void)
{
    return ui.page != PG_ARP && !(ui.page == PG_MC && (mc.part == MCP_LIVE || mc.part == MCP_SCENE));
}
static mc_scene_t *mc_edit(void) { return &mcs.sc[mc.scene % MC_SCENES]; }
static mc_lane_t *mc_lane(void) { return &mc_edit()->lane[mc.lane % MC_LANES]; }
static char scene_ch(uint32_t s) { return (char)('A' + s % MC_SCENES); }
static int mc_lane_page(void) { return ui.page == PG_MC && mc.part == MCP_LANE; }
static uint32_t mc_mask(uint32_t len) { return len >= 32u ? 0xFFFFFFFFu : (1u << len) - 1u; }

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

/* the scene after the one playing, to the engine: the queue's next entry (the first after the last), the scene
 * edited again, or none (an empty queue: the scene playing ends the queue) */
static int32_t mc_next_pos(void)
{
    if (!mq.n)
        return -1;
    return (int32_t)((mc.qgone ? mc.qpos : mc.qpos + 1u) % mq.n);
}
static void mc_push_next(void)
{
    int32_t p;
    if (mc.on == 2u) {
        brk_mc_next(mc.scene);
        return;
    }
    p = mc_next_pos();
    brk_mc_next(p < 0 ? -1 : (int)mq.q[p]);
}

/* PLAY on a MATHCORE page, stopped: LIVE starts the queue at its first entry (an empty queue doesn't start), LANE and
 * SCENE play the scene edited round and round, MAP the break as it is. 0: not started */
static int mc_play(void)
{
    if (mc.part == MCP_MAP)
        return 1;
    if (mc.part == MCP_LIVE) {
        if (!mq.n) {
            app_warn("QUEUE NOT STARTED", "ADD A SCENE FIRST");
            return 0;
        }
        mc.on = 1u;
        mc.qpos = 0u;
        mc.qgone = 0u;
        brk_mc_start(mq.q[0]);
    } else {
        mc.on = 2u;
        brk_mc_start(mc.scene);
    }
    mc_push_next();
    {
        brk_status_t st;
        brk_status(&st);
        mc.adv_seen = st.mc_adv;
        mc.end_seen = st.mc_ended;
    }
    mc_mute_out();
    return 1;
}

/* every frame: a boundary passed (the next entry playing now: the one after it to the engine), the queue run out (the
 * transport stops), the transport stopped (the scenes too) */
static void mc_frame(const brk_status_t *st)
{
    if (!mc.on)
        return;
    if (!st->playing) {
        mc.on = 0u;
        brk_mc_start(-1);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    if (st->mc_ended != mc.end_seen) {   /* the queue was cleared: the last scene played out, the transport stops */
        mc.end_seen = st->mc_ended;
        mc.adv_seen = st->mc_adv;
        mc.on = 0u;
        transport(0, 0);
        app_say("QUEUE", "ENDED");
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    if (st->mc_adv != mc.adv_seen) {
        mc.adv_seen = st->mc_adv;
        if (mc.on == 1u && mq.n) {
            int32_t p = mc_next_pos();
            mc.qpos = (uint8_t)(p < 0 ? 0 : p);
            mc.qgone = 0u;
        }
        mc_push_next();
        ui.redraw |= RG_BLOCKS | RG_FOOT;
    }
}

/* the queue: append (a black key on LIVE), take the last of a scene out (SEL + it), clear (SEL + REC), move the
 * entry at the cursor (PRESETS). The scene playing is never cut: an edit shows at its boundary */
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
        app_warn("NOT QUEUED", "NO SCENE ON THIS KEY");
        return;
    }
    if (mq.n >= MC_QUEUE) {
        app_warn("NOT QUEUED", "THE QUEUE IS FULL");
        return;
    }
    mq.q[mq.n++] = (uint8_t)s;
    mc.cursor = (uint8_t)(mq.n - 1u);
    set_dirty = 1u;
    if (mc.on == 1u)
        mc_push_next();
    mq_said("QUEUED", s);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}
static void mq_remove_at(uint32_t j)
{
    uint32_t k;
    for (k = j; k + 1u < mq.n; k++)
        mq.q[k] = mq.q[k + 1u];
    mq.n--;
    if (mc.on == 1u) {
        if (j < mc.qpos)
            mc.qpos--;
        else if (j == mc.qpos)
            mc.qgone = 1u;               /* (it plays out; the entry now in its place is next) */
        if (mq.n && mc.qpos >= mq.n)
            mc.qpos = 0u;
        mc_push_next();
    }
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
            mq_said("REMOVED", s);
            return;
        }
    app_warn("NOT REMOVED", "NOT IN THE QUEUE");
}
static void mq_clear(void)
{
    mq.n = 0u;
    mc.cursor = 0u;
    mc.qgone = 1u;
    if (mc.on == 1u)
        mc_push_next();                  /* (none: the scene playing ends the queue) */
    set_dirty = 1u;
    app_say("QUEUE CLEARED", mc.on == 1u ? "THIS SCENE PLAYS OUT" : "");
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
    if (mc.on == 1u && !mc.qgone) {      /* (the entry playing keeps playing: its place follows it) */
        if (mc.qpos == a)
            mc.qpos = (uint8_t)b;
        else if (mc.qpos == b)
            mc.qpos = (uint8_t)a;
        mc_push_next();
    } else if (mc.on == 1u)
        mc_push_next();
    mc.cursor = (uint8_t)b;
    set_dirty = 1u;
    mq_said("MOVED", mq.q[b]);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

/* the black keys on MATHCORE's LIVE (append; SEL: take out) and SCENE (this key plays the scene edited; SEL: none).
 * 1: they are its */
static int mc_black(uint32_t kdn, uint32_t btn)
{
    uint32_t i;
    if (ui.page != PG_MC || (mc.part != MCP_LIVE && mc.part != MCP_SCENE))
        return 0;
    for (i = 0; i < NPFX; i++) {
        if (!(kdn >> BK[i] & 1u))
            continue;
        if (btn & 1u << B_SEL)
            ui.tap_combo = 1u;           /* (not SEL's card) */
        if (mc.part == MCP_LIVE) {
            if (btn & 1u << B_SEL)
                mq_remove_last(i);
            else
                mq_append(i);
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
    if (mc.part == MCP_LIVE) {
        text_cat(t, MC_STATE_NAME[mc.state[i] + 1]);
        return;
    }
    if (mc.part == MCP_MAP) {            /* the role's slices: how many the break has */
        uint8_t list[BRK_MAX_SLICES];
        int_text((int32_t)mc_role_list((uint32_t)i, list), t);
        return;
    }
    if (mc.part == MCP_SCENE) {
        if (i == 0) {
            int_text(S->bars, t);
            text_cat(t, S->bars == 1u ? " BAR" : " BARS");
        } else if (i == 1)
            text_cat(t, S->flags & MCS_BREAK ? "ON" : "OFF");
        else if (i == 2)
            text_cat(t, "< >");
        else {
            uint32_t l, n = 0u;
            for (l = 0; l < MC_LANES; l++)
                n += S->lane[l].flags & MCL_ON;
            int_text((int32_t)n, t);
            text_cat(t, " ON");
        }
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
    case 11: text_cat(t, L->flags & MCL_REV ? "ON" : "OFF"); break;
    case 12: text_cat(t, ROLE_NAME[L->role % MC_ROLES]); break;
    case 13: int_text(L->seed, t); break;
    case 14: text_cat(t, L->flags & MCL_FREE ? "FREE" : "SCENE"); break;
    default: text_cat(t, L->flags & MCL_ON ? "ON" : "OFF"); break;
    }
}
static const char *mc_knob_name(uint32_t i)
{
    static const char *const SC[4] = {"PHRASE", "BREAK", "NEW / COPY", "LANES"};
    if (mc.part == MCP_LIVE || mc.part == MCP_MAP)
        return ROLE_NAME[i];
    return mc.part == MCP_SCENE ? SC[i] : MC_KNOB[mc.kset % MKSETS][i];
}

static void mc_turn(int i, int32_t d)
{
    mc_scene_t *S = mc_edit();
    mc_lane_t *L = mc_lane();
    char t[16];
    uint32_t len = L->len < 1u ? 1u : L->len;
    if (mc.part == MCP_LIVE) {           /* a lane: left toward MUTE, right toward SOLO (its cycle runs on) */
        mc.state[i] = (int8_t)clamp(mc.state[i] + (d > 0 ? 1 : -1), -1, 1);
        mc_mute_out();
        app_say(ROLE_NAME[i], MC_STATE_NAME[mc.state[i] + 1]);
        ui.redraw |= RG_BLOCKS | RG_FOOT;
        return;
    }
    if (mc.part == MCP_MAP) {            /* (heard from the break: nothing to set) */
        app_say("SLICE MAP", "AUTOMATIC");
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
        } else {                         /* every lane on (right) or all but the kick off (left) */
            uint32_t l;
            for (l = 0; l < MC_LANES; l++)
                S->lane[l].flags = (uint8_t)(d > 0 || !l ? S->lane[l].flags | MCL_ON : S->lane[l].flags & ~MCL_ON);
        }
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
        case 11: L->flags = (uint8_t)(d > 0 ? L->flags | MCL_REV : L->flags & ~MCL_REV); break;
        case 12: L->role = (uint8_t)((L->role + (d > 0 ? 1u : MC_ROLES - 1u)) % MC_ROLES); L->pick = 0u; break;
        case 13: L->seed = (uint8_t)(L->seed + d); break;
        case 14: L->flags = (uint8_t)(d > 0 ? L->flags | MCL_FREE : L->flags & ~MCL_FREE); break;
        default: L->flags = (uint8_t)(d > 0 ? L->flags | MCL_ON : L->flags & ~MCL_ON); break;
        }
    set_dirty = 1u;
    mc_value(i, t);
    app_say(mc_knob_name((uint32_t)i), t);
    ui.redraw |= RG_BLOCKS | RG_FOOT;
}

static void mc_algo(int32_t d)           /* ALGORITHM: LIVE the queue's cursor, MAP the slice, LANE the lane, SCENE the scene */
{
    char t[16];
    if (mc.part == MCP_LIVE) {
        if (!mq.n)
            return;
        mc.cursor = (uint8_t)((mc.cursor + (d > 0 ? 1u : mq.n - 1u)) % mq.n);
        t[0] = scene_ch(mq.q[mc.cursor]);
        t[1] = 0;
        say_int("ENTRY", mc.cursor + 1, "");
        app_say(ui.msg[0], t);
    } else if (mc.part == MCP_MAP) {
        const brk_break_t *b = brk_get_break();
        uint32_t n = b && b->nslices < 16u ? b->nslices : 16u;
        mc.slice = (uint8_t)((mc.slice + (d > 0 ? 1u : n - 1u)) % n);
        say_int("SLICE", mc.slice + 1, "");
    } else if (mc.part == MCP_LANE) {
        mc.lane = (uint8_t)((mc.lane + (d > 0 ? 1u : MC_LANES - 1u)) % MC_LANES);
        app_say("LANE", ROLE_NAME[mc_lane()->role % MC_ROLES]);
    } else {
        mc.scene = (uint8_t)((mc.scene + (d > 0 ? 1u : MC_SCENES - 1u)) % MC_SCENES);
        if (mc.on == 2u)
            mc_push_next();              /* (playing it: in at the boundary) */
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
/* the black keys' lights: LIVE the keys with a scene (the scene playing's flashing), SCENE the keys of the scene edited */
static uint32_t mc_black_lit(const brk_status_t *st)
{
    uint32_t i, m = 0u;
    for (i = 0; i < NPFX; i++) {
        uint32_t s = mq.key[i];
        if (s >= MC_SCENES)
            continue;
        if (mc.part == MCP_SCENE ? s == mc.scene
                                 : !(st->mc_scene == (int8_t)s && st->bar16 >= 0 && (st->bar16 & 2)))
            m |= 1u << i;
    }
    return m;
}
static const char *mc_pad_label(uint32_t i, char *t)   /* the foot's pad i on LIVE and SCENE: its scene's letter */
{
    t[0] = mq.key[i] < MC_SCENES ? scene_ch(mq.key[i]) : '-';
    t[1] = 0;
    return t;
}
static int mc_pad_on(uint32_t i, const brk_status_t *st)
{
    uint32_t s = mq.key[i];
    return s < MC_SCENES && (mc.part == MCP_SCENE ? s == mc.scene : st->mc_scene == (int8_t)s);
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

static void mc_draw_live(const brk_status_t *st)
{
    uint32_t i, first;
    int run = mc.on == 1u && st->playing && st->mc_scene >= 0;
    char t[24];
    cv_begin(240, 52, C_BLACK);
    cv_rect(2, 2, 44, 44, run ? C_BLOCK_HI : C_BLOCK);   /* the scene playing, big */
    t[0] = run ? scene_ch((uint32_t)st->mc_scene) : mq.n ? scene_ch(mq.q[0]) : '-';
    t[1] = 0;
    cv_text2(24 - text2_w(&FONT_B, t) / 2, 6, &FONT_B, t, run ? C_BLACK : C_GRAY);
    cv_text(50, 0, &FONT_XS, run ? "NOW" : mq.n ? "PLAY: START" : "BLACK KEYS: ADD SCENES", run ? C_AMB : C_GRAY);
    if (run && st->mc_next >= 0) {
        t[0] = 0;
        text_cat(t, "NEXT ");
        t[5] = scene_ch((uint32_t)st->mc_next);
        t[6] = 0;
        cv_text(100, 0, &FONT_XS, t, C_HI);
    } else if (run)
        cv_text(100, 0, &FONT_XS, "LAST", C_AMB);
    t[0] = 0;
    int_text(mq.n, t);
    text_cat(t, mq.n == 1u ? " ENTRY" : " ENTRIES");
    cv_text(236 - text_w(&FONT_XS, t), 0, &FONT_XS, t, C_GRAY);
    first = mq.n > 11u && mc.cursor > 5u ? (mc.cursor - 5u < mq.n - 11u ? mc.cursor - 5u : mq.n - 11u) : 0u;
    for (i = first; i < mq.n && i < first + 11u; i++) {  /* the entries: the one playing lit, the cursor outlined */
        int32_t x = 50 + (int32_t)(i - first) * 17;
        int now = run && i == mc.qpos && !mc.qgone, cur = i == mc.cursor;
        t[0] = scene_ch(mq.q[i]);
        t[1] = 0;
        cv_rect(x, 16, 15, 18, cur ? C_WHITE : now ? C_BLOCK_HI : C_BLOCK);
        if (cur)
            cv_rect(x + 1, 17, 13, 16, now ? C_BLOCK_HI : C_BLOCK);
        cv_text(x + (15 - text_w(&FONT_XS, t)) / 2, 18, &FONT_XS, t, now ? C_BLACK : C_HI);
    }
    if (first + 11u < mq.n)
        cv_text(238 - text_w(&FONT_XS, ">"), 18, &FONT_XS, ">", C_GRAY);
    if (run) {                           /* the phrase so far */
        uint32_t bars = mcs.sc[(uint32_t)st->mc_scene % MC_SCENES].bars, tot = (bars ? bars : 1u) * 16u;
        uint32_t w = (uint32_t)st->mc_step * 186u / tot;
        cv_rect(50, 40, 186, 4, C_BLOCK);
        cv_rect(50, 40, (int32_t)(w > 186u ? 186u : w), 4, C_HI);
        for (i = 1; i < (bars ? bars : 1u); i++)
            cv_rect(50 + (int32_t)(i * 186u / bars), 38, 1, 8, C_LINE);
    }
    cv_blit(0, 88);
    cv_begin(240, 52, C_BLACK);
    mc_rows(st, run ? (uint32_t)st->mc_scene : mq.n ? mq.q[0] : 0u, 2, -1);
    cv_blit(0, 140);
}

static void mc_draw_map(const brk_status_t *st)
{
    const brk_break_t *b = brk_get_break();
    uint32_t row, k, r, c, n = b && b->nslices < 16u ? b->nslices : 16u;
    char t[4];
    for (row = 0; row < 2u; row++) {
        cv_begin(240, 52, C_BLACK);
        for (k = row * 8u; k < row * 8u + 8u; k++) {
            int32_t x = 1 + (int32_t)(k % 8u) * 30;
            int on = st->slice == (int32_t)k, have = b && k < n, sel = k == mc.slice;
            cv_rect(x, 0, 28, 50, sel ? C_WHITE : on ? C_BLOCK_HI : C_BLOCK);
            if (sel)
                cv_rect(x + 1, 1, 26, 48, on ? C_BLOCK_HI : C_BLOCK);
            if (!have)
                continue;
            for (c = 0; c < BLK_W - 4u; c++) {
                int32_t h = blk_px[k][c * BLK_W / (BLK_W - 4u)] * 3 / 4;
                cv_rect(x + 3 + (int32_t)c, 14 - h / 2, 1, h + 1, on ? C_BLACK : C_DIM);
            }
            int_text((int32_t)k + 1, t);
            cv_text(x + 3, 24, &FONT_XS, t, on ? C_BLACK : C_GRAY);
            for (r = 0; r < MC_ROLES; r++)   /* its roles: K S H P, lit where heard */
                if (roles_now[k] >> r & 1u) {
                    t[0] = ROLE_CH[r];
                    t[1] = 0;
                    cv_text(x + 2 + (int32_t)r * 6, 36, &FONT_XS, t, on ? C_BLACK : r == 0u ? C_WHITE : C_HI);
                }
        }
        cv_blit(0, 88u + row * 52u);
    }
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
    text_cat(t, S->flags & MCS_BREAK ? "  BREAK UNDER" : "  LANES ALONE");
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
    text_cat(t, "IN THE QUEUE: ");
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
    else if (mc.part == MCP_MAP)
        mc_draw_map(st);
    else if (mc.part == MCP_LANE)
        mc_draw_lane(st);
    else
        mc_draw_scene(st);
}
