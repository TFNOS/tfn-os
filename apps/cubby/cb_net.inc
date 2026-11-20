// CUBBY: A FRIEND ON A SECOND BOARD. Included by app_cubby.c after cb_game.inc.
// Two boards play on the same island together: the lobby, the pairing and the radio of main/net2.c (link app 'Y').
// - Each board drives its own robot. The robot of the friend comes in its packets (each 50 ms: its place, its facing,
//   its speed and the pose of its parts); the board moves the friend robot smoothly to each new pose.
// - THE WORLD (the cubes on the cells, the balloon cubes, the SUNDROPS, the sparks) is the same on the two boards. Each
//   change that a robot makes is an event with a number. The packets of a board repeat its events until the other board
//   gives the number back (no event is lost, and none is done twice).
// - Two changes at the same moment on one cell can make the worlds differ: the HOST (the faster board) sends the hash of
//   its world, and when the hash of the CLIENT stays different for 1.5 s (with no event on the way), the CLIENT asks for
//   the world of the HOST and takes it.
// - The end: THE TITLE (the panel) or the end of the link. The other board plays on alone ("FRIEND LEFT").
#define NET_ID      'Y'
#define NET_VER     2               // (2: the big island of round 3)
#define K_PK        N2_K_APP        // the packet of the CLIENT (the HOST sends the same packet as its state)
#define K_SNAP      (N2_K_APP + 1)  // the world of the HOST (when the CLIENT asks for it)
#define NET_TICK_MS 50

// an event of the world: its number, its type, three small values and a height (mm)
typedef struct __attribute__((packed)) {
    uint8_t seq, type, a, b, c;
    int16_t y;
} nev_t;
#define NEV_MAX 12
// the pose of a robot in a packet
typedef struct __attribute__((packed)) {
    int16_t x, y, z;     // mm
    int16_t yaw;         // x 10000
    int8_t speed, spin;  // x 30, x 10
    uint8_t flags;       // bit 0 on the ground, bits 1-2 the balloon cube under the treads + 1
    uint8_t carry, gauge;
    int8_t rig[19];  // the parts (rig_pack)
} npose_t;
typedef struct __attribute__((packed)) {
    uint8_t ver, seq, ack;  // ack: the newest event of the other board that this board did
    uint8_t req;            // the CLIENT: it asks for the world of the HOST
    uint8_t mac[6];         // the board that sends (its name for the friend: friends_name_of)
    uint32_t hash;          // the hash of the world of the HOST (the CLIENT: 0)
    npose_t pose;
    uint8_t nev;
    nev_t ev[NEV_MAX];
} npk_t;
_Static_assert(sizeof(npk_t) <= N2_STATE_MAX, "the packet must fit");
// the world of the HOST: the cells with cubes (i, j, the count, the kinds 2 bits each), the balloon cubes, the flags
#define SNAP_CELLS 30
typedef struct __attribute__((packed)) {
    uint8_t ncell;
    struct __attribute__((packed)) {
        uint8_t i, j, n;
        uint16_t kinds;
    } cell[SNAP_CELLS];
    struct __attribute__((packed)) {
        uint8_t on, i, j, hung;
        int16_t y, base;
    } flo[NFLO];
    uint8_t drops;
    uint32_t sparks;
} nsnap_t;
_Static_assert(sizeof(nsnap_t) <= N2_STATE_MAX, "the world must fit a packet");

// the friend robot (its last pose, the pose on the screen)
static struct {
    bool on, seen;  // a friend game; a pose came
    v3 p, tp;       // the place on the screen, the place of the last packet
    float yaw, tyaw, speed, spin;
    bool ground;
    int on_float;
    rig_t r, tr;  // the rig on the screen, the rig of the last packet
    char name[12];
} s_fr;
static int s_net;             // n2_role_t: N2_OFF, N2_LOBBY, N2_HOST, N2_CLIENT
static uint8_t s_nseq;        // my packets
static nev_t s_out[NEV_MAX];  // my events that the other board has not given back
static int s_nout;
static uint8_t s_ev_next = 1;  // the number of my next event
static uint8_t s_in_done;      // the newest event of the other board that this board did
static bool s_net_apply;       // (a change of the world from the friend: no event of my own)
static int64_t s_net_tx;
static int s_hash_bad;  // the CLIENT: the packets in a row with another hash and no event on the way
static bool s_snap_req, s_snap_send;
static float s_left_t = -1;  // "FRIEND LEFT" on the screen
static int64_t net_now(void)
{
    return esp_timer_get_time() / 1000;
}
static void net_name(const uint8_t *mac);
static int s_net_rx_n;   // (the log: the packets of the friend)
static bool s_net_fake;  // (the PC test: a link with no radio; the role stays as the test sets it)

// ---------------------------------------------------------------- the events of my robot
static void net_event(int type, int a, int b, int c, float y)
{
    if (s_net != N2_HOST && s_net != N2_CLIENT) {
        return;
    }
    if (s_net_apply) {
        return;
    }
    if (s_nout >= NEV_MAX) {
        // (the other board gives nothing back: the oldest goes; the hash check repairs the world later)
        memmove(s_out, s_out + 1, sizeof(nev_t) * (NEV_MAX - 1));
        s_nout--;
    }
    s_out[s_nout++] = (nev_t){s_ev_next++, (uint8_t)type, (uint8_t)a,
                              (uint8_t)b,  (uint8_t)c,    (int16_t)clampf(y * 1000, -32000, 32000)};
    if (!s_ev_next) {
        s_ev_next = 1;
    }
}
// the SUNDROPS that are found (the count of the flags: the same on both boards)
static void drops_count(void)
{
    int n = 0;
    for (int k = 0; k < NDROP; k++) {
        n += s_drop[k].got;
    }
    s_ndrops = n;
    n = 0;
    for (int k = 0; k < NSPARK; k++) {
        n += s_spark[k].got;
    }
    s_nsparks = n;
}
// an event of the friend into this world
static void net_apply(const nev_t *e)
{
    s_net_apply = true;
    int i = e->a, j = e->b;
    switch (e->type) {
    case NE_STACK_ADD:
        if (cell_in(i, j) && e->c >= CB_TIN && e->c <= CB_FLOAT) {
            stack_add(i, j, e->c);
        }
        break;
    case NE_STACK_POP:
        if (cell_in(i, j) && s_nstk[i][j]) {
            s_nstk[i][j]--;
            s_pop[i][j] = 1;
        }
        break;
    case NE_FLOAT_NEW: {
        // (c: the slot of the balloon cube, + 16 when it hangs in the air)
        int k = e->c & 15;
        if (k < NFLO) {
            flo_t *f = &s_flo[k];
            if (f->on && P.on_float == k) {
                P.on_float = -1;
            }
            f->on = true;
            f->i = i;
            f->j = j;
            f->y = f->base = e->y / 1000.0f;
            f->t = 0;
            f->hung = (e->c >> 4) & 1;
            f->pop = 0;
        }
        break;
    }
    case NE_FLOAT_OFF:
        if (e->c < NFLO) {
            s_flo[e->c].on = false;
            if (P.on_float == e->c) {
                P.on_float = -1;
            }
        }
        break;
    case NE_DROP_GOT:
        if (e->c < NDROP && !s_drop[e->c].got) {
            drop_t *d = &s_drop[e->c];
            d->got = true;
            d->t = 0;
            fx(FX_DROP, 80, 1.0f);
            for (int n = 0; n < 16; n++) {
                part_add(d->p, V3(frs() * 3, frand() * 3, frs() * 3), 1.2f, 0.09f, PT_CONF,
                         n & 1 ? 0xFFD23A : 0xFFFFFF);
            }
            drops_count();
            if (s_ndrops == NDROP && s_win_t < 0) {
                s_win_t = 0;
            }
            music_jingle(s_ndrops == NDROP ? CBM_J_WIN : CBM_J_SUNDROP,
                         s_ndrops == NDROP ? CB_J_WIN_MS : CB_J_SUNDROP_MS);
        }
        break;
    case NE_SPARK_GOT:
        if (e->c < NSPARK && !s_spark[e->c].got) {
            s_spark[e->c].got = true;
            fx(FX_SPARK, 50, 1.0f);
            drops_count();
        }
        break;
    case NE_RESTART:
        s_net_apply = false;
        play_start();
        return;
    default:
        break;
    }
    s_net_apply = false;
}

// ---------------------------------------------------------------- the pose of a robot
static int8_t q8(float v, float k)
{
    return (int8_t)clampi((int)lrintf(v * k), -127, 127);
}
static void rig_pack(const rig_t *r, int8_t *o)
{
    o[0] = q8(r->squash - 1, 200);
    o[1] = q8(r->bob, 1000);
    o[2] = q8(r->body_pitch, 300);
    o[3] = q8(r->roll, 300);
    o[4] = q8(r->head_yaw, 90);
    o[5] = q8(r->head_pitch, 120);
    o[6] = q8(r->head_roll, 300);
    o[7] = q8(r->neck - 1, 120);
    o[8] = q8(r->eye_roll, 300);
    o[9] = q8(r->brow_l, 300);
    o[10] = q8(r->brow_r, 300);
    o[11] = q8(r->brow_up, 1000);
    o[12] = q8(r->iris - 1, 120);
    o[13] = q8(r->blink, 120);
    o[14] = q8(r->arm_l, 40);
    o[15] = q8(r->arm_r, 40);
    o[16] = q8(r->claw, 120);
    o[17] = q8(r->hatch, 120);
    o[18] = q8(r->carry_s, 120);
}
static void rig_unpack(const int8_t *o, rig_t *r)
{
    r->squash = 1 + o[0] / 200.0f;
    r->bob = o[1] / 1000.0f;
    r->body_pitch = o[2] / 300.0f;
    r->roll = o[3] / 300.0f;
    r->head_yaw = o[4] / 90.0f;
    r->head_pitch = o[5] / 120.0f;
    r->head_roll = o[6] / 300.0f;
    r->neck = 1 + o[7] / 120.0f;
    r->eye_roll = o[8] / 300.0f;
    r->brow_l = o[9] / 300.0f;
    r->brow_r = o[10] / 300.0f;
    r->brow_up = o[11] / 1000.0f;
    r->iris = 1 + o[12] / 120.0f;
    r->blink = o[13] / 120.0f;
    r->arm_l = o[14] / 40.0f;
    r->arm_r = o[15] / 40.0f;
    r->claw = o[16] / 120.0f;
    r->hatch = o[17] / 120.0f;
    r->carry_s = o[18] / 120.0f;
}
static int16_t mm16(float v)
{
    return (int16_t)clampi((int)lrintf(v * 1000), -32000, 32000);
}

// ---------------------------------------------------------------- the hash and the copy of the world
static uint32_t world_hash(void)
{
    uint32_t h = 2166136261u;
#define HMIX(v)                                                                                                        \
    do {                                                                                                               \
        h ^= (uint32_t)(v);                                                                                            \
        h *= 16777619u;                                                                                                \
    } while (0)
    for (int i = 0; i < GX; i++) {
        for (int j = 0; j < GZ; j++) {
            int n = s_nstk[i][j];
            if (!n) {
                continue;
            }
            HMIX(i);
            HMIX(j);
            HMIX(n);
            for (int k = 0; k < n; k++) {
                HMIX(s_stk[i][j][k]);
            }
        }
    }
    for (int k = 0; k < NFLO; k++) {
        if (s_flo[k].on) {
            HMIX(k);
            HMIX(s_flo[k].i);
            HMIX(s_flo[k].j);
        }
    }
    for (int k = 0; k < NDROP; k++) {
        HMIX(s_drop[k].got);
    }
    for (int k = 0; k < NSPARK; k++) {
        HMIX(s_spark[k].got);
    }
#undef HMIX
    return h;
}
static void snap_make(nsnap_t *s)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < GX && s->ncell < SNAP_CELLS; i++) {
        for (int j = 0; j < GZ && s->ncell < SNAP_CELLS; j++) {
            int n = s_nstk[i][j];
            if (!n) {
                continue;
            }
            uint16_t kinds = 0;
            for (int k = 0; k < n; k++) {
                kinds |= (uint16_t)((s_stk[i][j][k] & 3) << (2 * k));
            }
            s->cell[s->ncell].i = (uint8_t)i;
            s->cell[s->ncell].j = (uint8_t)j;
            s->cell[s->ncell].n = (uint8_t)n;
            s->cell[s->ncell].kinds = kinds;
            s->ncell++;
        }
    }
    for (int k = 0; k < NFLO; k++) {
        const flo_t *f = &s_flo[k];
        s->flo[k].on = f->on;
        s->flo[k].i = (uint8_t)f->i;
        s->flo[k].j = (uint8_t)f->j;
        s->flo[k].hung = f->hung;
        s->flo[k].y = mm16(f->y);
        s->flo[k].base = mm16(f->base);
    }
    for (int k = 0; k < NDROP; k++) {
        if (s_drop[k].got) {
            s->drops |= (uint8_t)(1 << k);
        }
    }
    for (int k = 0; k < NSPARK; k++) {
        if (s_spark[k].got) {
            s->sparks |= (uint32_t)1 << k;
        }
    }
}
static void snap_take(const nsnap_t *s)
{
    memset(s_nstk, 0, sizeof s_nstk);
    for (int c = 0; c < s->ncell && c < SNAP_CELLS; c++) {
        int i = s->cell[c].i, j = s->cell[c].j, n = s->cell[c].n;
        if (!cell_in(i, j) || n > STK) {
            continue;
        }
        for (int k = 0; k < n; k++) {
            s_stk[i][j][k] = (uint8_t)((s->cell[c].kinds >> (2 * k)) & 3);
        }
        s_nstk[i][j] = (uint8_t)n;
    }
    for (int k = 0; k < NFLO; k++) {
        flo_t *f = &s_flo[k];
        f->on = s->flo[k].on != 0;
        f->i = s->flo[k].i;
        f->j = s->flo[k].j;
        f->hung = s->flo[k].hung != 0;
        f->y = s->flo[k].y / 1000.0f;
        f->base = s->flo[k].base / 1000.0f;
        if (!f->on && P.on_float == k) {
            P.on_float = -1;
        }
    }
    for (int k = 0; k < NDROP; k++) {
        bool g = (s->drops >> k) & 1;
        if (g && !s_drop[k].got) {
            s_drop[k].t = 0;
        }
        s_drop[k].got = g;
    }
    for (int k = 0; k < NSPARK; k++) {
        s_spark[k].got = (s->sparks >> k) & 1;
    }
    drops_count();
    if (s_ndrops == NDROP && s_win_t < 0) {
        s_win_t = 0;
    }
}

// ---------------------------------------------------------------- the packets
static void net_send(int64_t now)
{
    if (now - s_net_tx < NET_TICK_MS) {
        return;
    }
    s_net_tx = now;
    npk_t k;
    memset(&k, 0, sizeof k);
    k.ver = NET_VER;
    k.seq = ++s_nseq;
    k.ack = s_in_done;
    k.req = s_snap_req;
    link_my_mac(k.mac);
    k.hash = s_net == N2_HOST ? world_hash() : 0;
    npose_t *o = &k.pose;
    o->x = mm16(P.p.x);
    o->y = mm16(P.p.y);
    o->z = mm16(P.p.z);
    o->yaw = (int16_t)lrintf(wrap_pi(P.yaw) * 10000);
    o->speed = q8(P.speed, 30);
    o->spin = q8(P.spin, 10);
    o->flags = (uint8_t)((P.ground ? 1 : 0) | ((P.on_float + 1) & 3) << 1);
    o->carry = (uint8_t)R.carry;
    o->gauge = (uint8_t)R.gauge;
    rig_pack(&R, o->rig);
    k.nev = (uint8_t)s_nout;
    memcpy(k.ev, s_out, sizeof(nev_t) * s_nout);
    int len = (int)(sizeof k - sizeof(nev_t) * (NEV_MAX - s_nout));
    if (s_net == N2_HOST) {
        net2_send_state(&k, len, now);
    } else {
        net2_send(K_PK, &k, len);
    }
    if (s_net == N2_HOST && s_snap_send) {
        nsnap_t sn;
        snap_make(&sn);
        net2_send(K_SNAP, &sn, sizeof sn);
        s_snap_send = false;
    }
}
static void net_rx(uint8_t kind, const uint8_t *d, int len)
{
    if (kind == K_SNAP && s_net == N2_CLIENT && len >= (int)sizeof(nsnap_t)) {
        nsnap_t sn;
        memcpy(&sn, d, sizeof sn);
        snap_take(&sn);
        s_snap_req = false;
        s_hash_bad = 0;
        return;
    }
    if (kind != (s_net == N2_HOST ? K_PK : N2_K_STATE)) {
        return;
    }
    int base = (int)(sizeof(npk_t) - sizeof(nev_t) * NEV_MAX);
    if (len < base) {
        return;
    }
    npk_t k;
    memset(&k, 0, sizeof k);
    memcpy(&k, d, len < (int)sizeof k ? len : (int)sizeof k);
    if (k.ver != NET_VER || k.nev > NEV_MAX || len < base + (int)sizeof(nev_t) * k.nev) {
        return;
    }
    s_net_rx_n++;
    // my events that the friend did: out of the list
    int keep = 0;
    for (int q = 0; q < s_nout; q++) {
        if ((int8_t)(s_out[q].seq - k.ack) > 0) {
            s_out[keep++] = s_out[q];
        }
    }
    s_nout = keep;
    // the new events of the friend, in their order
    for (int q = 0; q < k.nev; q++) {
        const nev_t *e = &k.ev[q];
        if ((int8_t)(e->seq - s_in_done) <= 0) {
            continue;
        }
        net_apply(e);
        s_in_done = e->seq;
        if (s_net == N2_OFF) {
            return;
        }
    }
    // the pose of the friend robot
    const npose_t *o = &k.pose;
    s_fr.tp = V3(o->x / 1000.0f, o->y / 1000.0f, o->z / 1000.0f);
    s_fr.tyaw = o->yaw / 10000.0f;
    s_fr.speed = o->speed / 30.0f;
    s_fr.spin = o->spin / 10.0f;
    s_fr.ground = o->flags & 1;
    s_fr.on_float = ((o->flags >> 1) & 3) - 1;
    rig_unpack(o->rig, &s_fr.tr);
    s_fr.tr.carry = o->carry <= CB_FLOAT ? o->carry : 0;
    s_fr.tr.gauge = o->gauge <= CB_FLOAT ? o->gauge : 0;
    if (!s_fr.seen) {
        s_fr.p = s_fr.tp;
        s_fr.yaw = s_fr.tyaw;
        s_fr.r = s_fr.tr;
        s_fr.seen = true;
        net_name(k.mac);
    }
    // the HOST: the CLIENT asks for the world; the CLIENT: the check of the hash
    if (s_net == N2_HOST && k.req) {
        s_snap_send = true;
    }
    if (s_net == N2_CLIENT && k.hash) {
        if (k.hash != world_hash() && s_nout == 0 && k.nev == 0) {
            if (++s_hash_bad >= 30) {
                s_snap_req = true;
                s_hash_bad = 0;
            }
        } else {
            s_hash_bad = 0;
        }
    }
}

// ---------------------------------------------------------------- the lobby, the start, the end
static void net_name(const uint8_t *mac)
{
    const char *n = mac ? friends_name_of(mac) : "FRIEND";
    if (!n) {
        n = "FRIEND";
    }
    n = friends_short_name(n);
    int k = 0;
    for (; *n && k < 9; n++) {
        char c = *n;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            s_fr.name[k++] = c;
        } else if (k && s_fr.name[k - 1] != ' ' && (c == ' ' || c == '-' || c == '_')) {
            s_fr.name[k++] = ' ';
        }
    }
    while (k && s_fr.name[k - 1] == ' ') {
        k--;
    }
    s_fr.name[k] = 0;
    if (!k) {
        strcpy(s_fr.name, "FRIEND");
    }
}
static void net_lobby_open(void)
{
    net2_open(NET_ID, 0);
    s_net = N2_LOBBY;
}
static void net_stop(void)
{
    if (s_net == N2_OFF) {
        return;
    }
    net2_close();
    s_net = N2_OFF;
    s_fr.on = false;
    s_nout = 0;
}
// paired: both boards start the island (the HOST at the start place, the CLIENT next to it)
static void net_begin(void)
{
    memset(&s_fr, 0, sizeof s_fr);
    s_fr.on = true;
    s_fr.on_float = -1;
    s_nout = 0;
    s_ev_next = 1;
    s_in_done = 0;
    s_nseq = 0;
    s_hash_bad = 0;
    s_snap_req = s_snap_send = false;
    s_net_tx = 0;
    net_name(NULL);
    ESP_LOGI(TAG, "CUBBY 2 BOARDS: paired as the %s with %s", s_net == N2_HOST ? "HOST" : "CLIENT", s_fr.name);
    play_start();
    if (s_net == N2_CLIENT) {
        P.p = V3(8.6f, 0, 3.3f);
        P.p0 = P.p;
        P.safe = P.p;
    }
}
// each frame, before the game: the link. The friend robot moves to its last pose
static void net_frame(float rdt)
{
    if (s_left_t >= 0) {
        s_left_t += rdt;
        if (s_left_t > 3) {
            s_left_t = -1;
        }
    }
    if (s_net == N2_OFF) {
        return;
    }
    int64_t now = net_now();
    n2_event_t e = s_net_fake ? N2_EV_NONE : net2_poll(now, net_rx, NULL);
    int role = s_net_fake ? s_net : (int)net2_role();
    if (e == N2_EV_LOST || e == N2_EV_PEER_LOBBY || (role == N2_OFF && s_net != N2_LOBBY)) {
        bool was = s_fr.on;
        net_stop();
        if (was && s_gs == GS_PLAY) {
            s_left_t = 0;
        }
        return;
    }
    s_net = role;
    if (e == N2_EV_PAIRED) {
        net_begin();
    }
    if (s_net != N2_HOST && s_net != N2_CLIENT) {
        return;
    }
    net_send(now);
    if (s_fr.seen) {
        float k = clampf(rdt * 14, 0, 1);
        s_fr.p = vlerp(s_fr.p, s_fr.tp, k);
        s_fr.yaw += wrap_pi(s_fr.tyaw - s_fr.yaw) * k;
        // (the parts: toward the last pose; the treads turn with the speed here)
        rig_t *r = &s_fr.r;
        const rig_t *t = &s_fr.tr;
#define RLERP(f) r->f += (t->f - r->f) * k
        RLERP(squash);
        RLERP(bob);
        RLERP(body_pitch);
        RLERP(roll);
        RLERP(head_yaw);
        RLERP(head_pitch);
        RLERP(head_roll);
        RLERP(neck);
        RLERP(eye_roll);
        RLERP(brow_l);
        RLERP(brow_r);
        RLERP(brow_up);
        RLERP(iris);
        RLERP(blink);
        RLERP(arm_l);
        RLERP(arm_r);
        RLERP(claw);
        RLERP(hatch);
        RLERP(carry_s);
#undef RLERP
        r->carry = t->carry;
        r->gauge = t->gauge;
        r->flash = approach(r->flash, 0, rdt * 2.5f);
        r->tint = 1;
        float vl = s_fr.speed + s_fr.spin * TX, vr = s_fr.speed - s_fr.spin * TX;
        r->tread_l -= vl * rdt;
        r->tread_r -= vr * rdt;
        r->p = s_fr.p;
        r->yaw = s_fr.yaw;
    }
}
// the friend robot stands on the balloon cube k (it goes up with it on both boards)
static bool net_friend_on_float(int k)
{
    return s_fr.on && s_fr.seen && s_fr.ground && s_fr.on_float == k;
}
// the friend robot stands on the cell (i, j) with the top t (a plate)
static bool net_friend_on_cell(int i, int j, float t)
{
    return s_fr.on && s_fr.seen && s_fr.ground && robot_on(s_fr.p.x, s_fr.p.y, s_fr.p.z, i, j, t);
}
// the friend robot is in the cells of a machine
static bool net_friend_in_mech(const mech_t *q)
{
    return s_fr.on && s_fr.seen && robot_in_mech(q, s_fr.p.x, s_fr.p.z);
}
// the robot of this board does not go into the friend robot (only this board moves its own robot)
static void net_push(void)
{
    if (!s_fr.on || !s_fr.seen) {
        return;
    }
    if (absf(P.p.y - s_fr.p.y) > 1.1f) {
        return;
    }
    float dx = P.p.x - s_fr.p.x, dz = P.p.z - s_fr.p.z, d2 = dx * dx + dz * dz;
    const float R2 = 0.72f;
    if (d2 >= R2 * R2) {
        return;
    }
    float d = fsqrt(d2);
    if (d < 1e-3f) {
        dx = 1;
        dz = 0;
        d = 1;
    }
    float nx = s_fr.p.x + dx / d * R2, nz = s_fr.p.z + dz / d * R2;
    if (!box_hits(nx, nz, P.p.y, P.ground)) {
        P.p.x = nx;
        P.p.z = nz;
    }
}
