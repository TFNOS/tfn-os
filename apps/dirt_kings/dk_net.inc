// DIRT KINGS (app_dirt.c): the race WITH FRIENDS (2-4 boards). The link of link.c (ESP-NOW, and copies over the LAN and
// the VPN to the boards with the app open), app id 'O'.
// - THE ROOM: a board OPENS a race (FRIENDS > OPEN A RACE) and sends ROOM each 0.5 s (the arena, the members; in a race
//   each 1 s, "PLAYS"). The friends see the open races in the list of their FRIENDS page; an open room calls
//   friends_invite (a notice on the boards of the friends on the LAN, and the screen does not go off while the room
//   waits). A board that JOINS sends JOIN each 0.5 s. The host picks the
//   arena (< >) and STARTS (one friend at least). Bots drive the empty places.
// - THE RACE: each board drives its own truck and sends it (TRUCK) each 50 ms. The host drives the bots and puts the
//   pick-ups, and sends them (WORLD) each 50 ms with its own truck and a copy of the trucks of the friends (for a board
//   that does not hear another board). A board shows the other trucks from their last packet, moved on along their
//   speed. Only the board of a truck pushes it when two trucks touch.
// - A PICK-UP: the board of the truck takes it at once and tells the host in its TRUCK packets (the host removes it).
// - THE END: when all the friends are over the line, or 25 s after the first one, the host ends the race (WORLD with
//   the places). Each board gets its prize and keeps its nitro cans (a friend race gives no points of the season);
//   NEXT goes back to the room.
// - A board that sends nothing for 3 s: the host gives its truck to a bot. A host that stops: the friends go back to
//   the list of the races.
#define DN_APP 'O'
#define DN_VER 1
enum {
    DK_ROOM = 1,
    DK_JOIN,
    DK_LEAVE,
    DK_START,
    DK_TRUCK,
    DK_WORLD,
    DK_BYE
};
enum {
    DPTY_NONE,
    DPTY_HOST,
    DPTY_MEMBER
};
#define DN_TICK_MS 50
#define DN_GONE_MS 3000
#define DN_MAXPTY  4
#define DN_TB      21  // the bytes of one truck in a packet
#define DN_NTAKEN  8
static struct {
    bool on;  // the link is on (the FRIENDS pages, a friend race)
    uint8_t me[6];
    int party;        // DPTY_*
    uint8_t host[6];  // a member: the board of the race
    bool listed;      // a member: the host lists this board
    int64_t host_heard, beacon_at, tick_at;
    struct {
        uint8_t mac[6];
        int64_t heard;
    } mem[NTRUCK];
    int nmem;  // the members (the host: [0] = this board)
    struct {
        uint8_t mac[6];
        uint8_t arena, n, plays;
        int64_t seen;
    } pty[DN_MAXPTY];
    int npty;   // the open races
    int arena;  // the arena of the room
    // the race
    bool race, ended;  // a friend race (from START to NEXT after the result)
    int my;            // the place (slot) of this board: local truck i = slot (i + my) % 4
    uint8_t slot_mac[NTRUCK][6];
    bool human[NTRUCK];
    uint8_t grid[NTRUCK];
    int64_t heard[NTRUCK];  // host: the last TRUCK of each slot
    int starts;             // host: START packets still to send
    int ends;               // host: WORLD packets with the end still to send
    uint8_t seq;            // the counter of my truck packets
    // the last state of each other truck (local index), its counter, and its time (s_clock)
    bool have[NTRUCK];
    uint8_t rseq[NTRUCK];
    float tx[NTRUCK], ty[NTRUCK], th[NTRUCK], tvx[NTRUCK], tvy[NTRUCK], tz[NTRUCK], tvz[NTRUCK], tt[NTRUCK];
    uint8_t taken[DN_NTAKEN];
    int ntaken;                   // the pick-ups that this board took (their ids, in TRUCK)
    float first_done;             // host: the race time of the first friend over the line (-1)
    uint8_t final_place[NTRUCK];  // the places at the end (by slot, 1..4)
    char note[48];
    int64_t note_until;  // a line on the FRIENDS pages ("THE HOST IS GONE")
} DN;
static void title_go(void);
static void wheel_center(void);

static bool dn_race(void)
{
    return DN.race;
}
static bool dn_client(void)
{
    return DN.race && DN.my != 0;
}
static int dn_local(int slot)
{
    return (slot - DN.my + NTRUCK) % NTRUCK;
}
static int dn_slot(int local)
{
    return (local + DN.my) % NTRUCK;
}
static bool dn_friend(const uint8_t mac[6])
{
    return friends_find(mac) >= 0;
}
static void dn_note(const char *t, int64_t now)
{
    snprintf(DN.note, sizeof DN.note, "%s", t);
    DN.note_until = now + 4000;
}
// the name of a board for the screen: its FRIENDS name in capitals, 9 letters at most
static void dn_name(const uint8_t mac[6], char *out)
{
    const char *n = friends_name_of(mac);
    n = friends_short_name(n);
    int k = 0;
    for (; *n && k < 9; n++) {
        char c = *n;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out[k++] = c;
        } else if (k && out[k - 1] != ' ' && (c == ' ' || c == '-' || c == '_')) {
            out[k++] = ' ';
        }
    }
    while (k && out[k - 1] == ' ') {
        k--;
    }
    out[k] = 0;
    if (!k) {
        strcpy(out, "FRIEND");
    }
}
static const char *truck_name(int t)
{
    static char nm[NTRUCK][12];
    const truck_t *T = &s_tr[t];
    if (IS_ME(T)) {
        return TR("YOU", "אתה");
    }
    if (DN.race && !T->bot) {
        dn_name(DN.slot_mac[T->col], nm[t]);
        return nm[t];
    }
    return NAMES[T->col];
}

// ---- little endian
static inline void dn_w16(uint8_t **p, int v)
{
    (*p)[0] = (uint8_t)v;
    (*p)[1] = (uint8_t)((unsigned)v >> 8);
    *p += 2;
}
static inline int dn_r16s(const uint8_t **p)
{
    const int v = (int16_t)((*p)[0] | ((*p)[1] << 8));
    *p += 2;
    return v;
}
static inline unsigned dn_r16(const uint8_t **p)
{
    const unsigned v = (*p)[0] | ((*p)[1] << 8);
    *p += 2;
    return v;
}
static int dn_c16(float v)
{
    return (int)clampf(roundf(v), -32767, 32767);
}

// ---- a truck in a packet: slot, counter, x, y, heading, speed, height, the flags, the finish time, steer, nitro cans
static void dn_put(uint8_t **pp, int slot, const truck_t *T, uint8_t seq)
{
    uint8_t *p = *pp;
    *p++ = (uint8_t)slot;
    *p++ = seq;
    dn_w16(&p, dn_c16(T->x * 100));
    dn_w16(&p, dn_c16(T->y * 100));
    float h = T->h < 0 ? T->h + 2 * PI_F : T->h;
    dn_w16(&p, (int)(h * (65536.0f / (2 * PI_F))) & 0xFFFF);
    dn_w16(&p, dn_c16(T->vx * 100));
    dn_w16(&p, dn_c16(T->vy * 100));
    dn_w16(&p, dn_c16(T->z * 100));
    dn_w16(&p, dn_c16(T->vz * 100));
    *p++ = (uint8_t)((T->lap & 7) | (T->half ? 8 : 0) | (T->done ? 16 : 0) | (T->air ? 32 : 0) |
                     (T->nitro_t > 0 ? 64 : 0) | (T->bot ? 128 : 0));
    dn_w16(&p, (int)clampf(T->done_t * 10, 0, 65535));
    *p++ = (uint8_t)(int8_t)clampf(T->steer * 100, -100, 100);
    *p++ = (uint8_t)T->nitro;
    *pp = p;
}
// a truck from a packet (not the truck of this board): the newest state only
static void dn_get(const uint8_t *p)
{
    const int slot = p[0] % NTRUCK, t = dn_local(slot);
    const uint8_t seq = p[1];
    if (t == 0 && !s_tr[0].bot) {
        return;  // (my own truck)
    }
    truck_t *T = &s_tr[t];
    if (!T->remote) {
        return;  // (the host: a bot of its own)
    }
    if (DN.have[t] && (int8_t)(seq - DN.rseq[t]) <= 0) {
        return;
    }
    const uint8_t *q = p + 2;
    DN.tx[t] = dn_r16s(&q) / 100.0f;
    DN.ty[t] = dn_r16s(&q) / 100.0f;
    DN.th[t] = wrap_pi(dn_r16(&q) * (2 * PI_F / 65536.0f));
    DN.tvx[t] = dn_r16s(&q) / 100.0f;
    DN.tvy[t] = dn_r16s(&q) / 100.0f;
    DN.tz[t] = dn_r16s(&q) / 100.0f;
    DN.tvz[t] = dn_r16s(&q) / 100.0f;
    const uint8_t fl = *q++;
    T->done_t = dn_r16(&q) / 10.0f;
    T->steer = (int8_t)*q++ / 100.0f;
    T->nitro = *q++;
    T->lap = fl & 7;
    if (T->lap > LAPS) {
        T->lap = LAPS;
    }
    T->half = (fl & 8) != 0;
    T->done = (fl & 16) != 0;
    T->air = (fl & 32) != 0;
    if (fl & 64) {
        T->nitro_t = 0.2f;
    }
    T->bot = (fl & 128) != 0;
    DN.tt[t] = s_clock;
    if (!DN.have[t]) {  // (the first state: there at once)
        T->x = DN.tx[t];
        T->y = DN.ty[t];
        T->h = DN.th[t];
        T->z = DN.tz[t];
        T->idx = nearest(T->x, T->y, 0, 0);
    }
    DN.have[t] = true;
    DN.rseq[t] = seq;
}
// a truck of another board in each step: on from its last state along its speed, and smooth to it
static void remote_step(truck_t *T, int t, float dt)
{
    if (!DN.have[t]) {
        return;
    }
    float age = clampf(s_clock - DN.tt[t], 0, 0.4f);
    float px = DN.tx[t] + DN.tvx[t] * age, py = DN.ty[t] + DN.tvy[t] * age;
    float ex = px - T->x, ey = py - T->y;
    bool jump = ex * ex + ey * ey > 16;
    if (jump) {
        T->x = px;
        T->y = py;
    } else {
        float k = clampf(dt * 12, 0, 1);
        T->x += ex * k;
        T->y += ey * k;
    }
    T->vx = DN.tvx[t];
    T->vy = DN.tvy[t];
    T->h = wrap_pi(T->h + wrap_pi(DN.th[t] - T->h) * clampf(dt * 14, 0, 1));
    float pz = DN.tz[t] + (T->air ? DN.tvz[t] * age - 10 * age * age : 0);
    T->z += (fmaxf(pz, 0) - T->z) * clampf(dt * 14, 0, 1);
    T->idx = nearest(T->x, T->y, jump ? 0 : T->idx, jump ? 0 : 24);
    surface(T);
    int f = fwd(s_start, T->idx);
    T->prog = T->lap + (float)f / s_n - (T->half || f < s_n / 2 ? 0 : 1);
    if (T->done) {
        T->prog = LAPS + 1.0f - T->done_t / 10000.0f;
    }
    // the dust and the nitro flame, as on its own board
    float c = cosf(T->h), s = sinf(T->h);
    float sp = sqrtf(T->vx * T->vx + T->vy * T->vy);
    if (!T->air && (esp_random() & 7) == 0 && sp > 3 && T->surf != SURF_TRACK) {
        uint16_t col = T->surf == SURF_MUD ? rgb(70, 50, 30) : rgb(210, 180, 140);
        puff(T->x - c * 1.2f, T->y - s * 1.2f, -T->vx * 0.2f + frand(-1, 1), -T->vy * 0.2f + frand(-1, 1), 0.6f, 0.5f,
             col);
    }
    if (T->nitro_t > 0) {
        T->nitro_t -= dt;
        if (esp_random() & 1) {
            puff(T->x - c * 1.5f, T->y - s * 1.5f, -c * 4 + frand(-1, 1), -s * 4 + frand(-1, 1), 0.3f, 0.45f,
                 (esp_random() & 1) ? rgb(120, 200, 255) : rgb(255, 170, 60));
        }
    }
}
static void dn_took(uint8_t id)
{
    DN.taken[DN.ntaken++ % DN_NTAKEN] = id;
}
static bool dn_was_taken(uint8_t id)
{
    for (int i = 0; i < DN_NTAKEN && i < DN.ntaken; i++) {
        if (DN.taken[i] == id) {
            return true;
        }
    }
    return false;
}

// ---- the packets
static void dn_send_room(void)
{
    uint8_t b[4 + 6 * NTRUCK], *p = b;
    *p++ = DN_VER;
    *p++ = DN.race ? 1 : 0;
    *p++ = (uint8_t)DN.arena;
    *p++ = (uint8_t)DN.nmem;
    for (int k = 0; k < DN.nmem; k++) {
        memcpy(p, DN.mem[k].mac, 6);
        p += 6;
    }
    link_send(DN_APP, DK_ROOM, b, (int)(p - b));
}
static void dn_send_to_host(int kind)
{
    uint8_t b[7] = {DN_VER};
    memcpy(b + 1, DN.host, 6);
    link_send(DN_APP, (uint8_t)kind, b, 7);
}
static void dn_send_start(void)
{
    uint8_t b[2 + 8 * NTRUCK], *p = b;
    *p++ = DN_VER;
    *p++ = (uint8_t)DN.arena;
    for (int g = 0; g < NTRUCK; g++) {
        *p++ = DN.human[g];
        memcpy(p, DN.slot_mac[g], 6);
        p += 6;
        *p++ = DN.grid[g];
    }
    link_send(DN_APP, DK_START, b, (int)(p - b));
}
static void dn_send_world(int64_t now)
{
    uint8_t b[LINK_DATA_MAX], *p = b;
    *p++ = DN_VER;
    *p++ = (uint8_t)(DN.ended ? 2 : s_gs == GS_RACE ? 1 : 0);
    for (int g = 0; g < NTRUCK; g++) {
        *p++ = DN.final_place[g];
    }
    uint8_t *np = p++;
    int n = 0;
    DN.seq++;
    for (int t = 0; t < NTRUCK; t++) {
        truck_t *T = &s_tr[t];
        if (T->remote && (!DN.have[t] || now - DN.heard[t] > 300)) {
            continue;  // (a friend: only a fresh copy)
        }
        dn_put(&p, t, T, T->remote ? DN.rseq[t] : DN.seq);
        n++;
    }
    *np = (uint8_t)n;
    uint8_t *ip = p++;
    int ni = 0;
    for (int i = 0; i < NITEM; i++) {
        item_t *I = &s_item[i];
        if (!I->on) {
            continue;
        }
        *p++ = I->id;
        *p++ = (uint8_t)I->kind;
        dn_w16(&p, dn_c16(I->x * 100));
        dn_w16(&p, dn_c16(I->y * 100));
        ni++;
    }
    *ip = (uint8_t)ni;
    link_send(DN_APP, DK_WORLD, b, (int)(p - b));
}
static void dn_send_truck(void)
{
    uint8_t b[3 + DN_TB + DN_NTAKEN], *p = b;
    *p++ = DN_VER;
    dn_put(&p, DN.my, &s_tr[0], ++DN.seq);
    int n = DN.ntaken < DN_NTAKEN ? DN.ntaken : DN_NTAKEN;
    *p++ = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        *p++ = DN.taken[i];
    }
    link_send(DN_APP, DK_TRUCK, b, (int)(p - b));
}

// ---- the pages and the race
static void dn_link_on(void)
{
    if (DN.on) {
        return;
    }
    memset(&DN, 0, sizeof DN);
    link_start();
    link_my_mac(DN.me);
    DN.on = true;
}
// out of the race and out of the party (BYE / LEAVE); the link stays on
static void dn_leave_party(void)
{
    if (DN.party == DPTY_HOST) {
        const uint8_t b[1] = {DN_VER};
        link_send(DN_APP, DK_BYE, b, 1);
        link_send(DN_APP, DK_BYE, b, 1);
    } else if (DN.party == DPTY_MEMBER) {
        dn_send_to_host(DK_LEAVE);
        dn_send_to_host(DK_LEAVE);
    }
    DN.party = DPTY_NONE;
    DN.nmem = 0;
    DN.race = false;
    DN.my = 0;
    for (int t = 0; t < NTRUCK; t++) {
        s_tr[t].remote = false;
    }
}
// out of all the friends pages: the link goes off
static void dn_party_end(void)
{
    if (!DN.on) {
        return;
    }
    dn_leave_party();
    link_stop();
    DN.on = false;
}
static void dn_close(void)
{
    dn_party_end();
}
static void dn_lobby_go(int64_t now)
{
    dn_link_on();
    dn_leave_party();
    if (s_gs != GS_TITLE && s_gs != GS_LOBBY && s_gs != GS_ROOM) {
        title_go();
        dn_link_on();
    }
    s_gs = GS_LOBBY;
    s_gt = 0;
    DN.beacon_at = now - 1000;
}
static void dn_background(int k)
{
    // (the bots race behind the pages on the arena of the room)
    if (s_A != &DK_ARENAS[k] || !s_demo) {
        race_init(k, true);
    }
}
static void dn_open(int64_t now)
{
    DN.party = DPTY_HOST;
    DN.nmem = 1;
    memcpy(DN.mem[0].mac, DN.me, 6);
    DN.arena = s_race_k;
    DN.beacon_at = now - 1000;
    s_gs = GS_ROOM;
    s_gt = 0;
    ESP_LOGI(TAG, "net: opens a race (%s)", DK_ARENAS[DN.arena].name);
}
static void dn_join(int i, int64_t now)
{
    DN.party = DPTY_MEMBER;
    memcpy(DN.host, DN.pty[i].mac, 6);
    DN.host_heard = now;
    DN.arena = DN.pty[i].arena % DK_NARENA;
    DN.listed = false;
    DN.nmem = 0;
    DN.beacon_at = now - 1000;
    s_gs = GS_ROOM;
    s_gt = 0;
    dn_background(DN.arena);
    ESP_LOGI(TAG, "net: joins the race of %s", friends_name_of(DN.host));
}
// the race starts on this board (the host: at START; a member: at the START packet)
static void dn_race_begin(int64_t now)
{
    static const float SKILL[NTRUCK] = {0.97f, 0.95f, 0.92f, 0.89f};
    race_init(DN.arena, false);
    DN.race = true;
    DN.ended = false;
    DN.first_done = -1;
    DN.ntaken = 0;
    memset(DN.final_place, 0, sizeof DN.final_place);
    for (int t = 0; t < NTRUCK; t++) {
        truck_t *T = &s_tr[t];
        int g = dn_slot(t);
        T->col = g;
        T->bot = !DN.human[g];
        T->remote = t != 0 && (DN.my != 0 || DN.human[g]);
        T->skill = SKILL[g];
        T->nitro = t == 0 ? s_cans : 2 + s_tier / 2;
        place_on_grid(T, DN.grid[g]);
        DN.have[t] = false;
        DN.heard[t] = now;
    }
    places();
    s_cam_x = s_tr[0].x;
    s_cam_y = s_tr[0].y;
    s_psi = s_tr[0].h - PI_F / 2;
    s_gs = GS_COUNT;
    s_gt = 0;
    s_touch_on = s_touch_brake = false;
    wheel_center();
    ESP_LOGI(TAG, "net: the race starts (%s), my place %d", DK_ARENAS[DN.arena].name, DN.my);
}
// the host: START in the room
static void dn_start(int64_t now)
{
    memset(DN.human, 0, sizeof DN.human);
    memset(DN.slot_mac, 0, sizeof DN.slot_mac);
    int n = DN.nmem;
    for (int k = 0; k < n; k++) {
        DN.human[k] = true;
        memcpy(DN.slot_mac[k], DN.mem[k].mac, 6);
    }
    // the grid: the bots in front, the friends behind them in a random order (as the player in a race of the season)
    int nb = NTRUCK - n, back[NTRUCK];
    for (int k = 0; k < n; k++) {
        back[k] = nb + k;
    }
    for (int k = n - 1; k > 0; k--) {
        int j = (int)(esp_random() % (k + 1)), x = back[k];
        back[k] = back[j];
        back[j] = x;
    }
    for (int g = 0, b = 0, h = 0; g < NTRUCK; g++) {
        DN.grid[g] = DN.human[g] ? (uint8_t)back[h++] : (uint8_t)b++;
    }
    DN.my = 0;
    DN.starts = 8;
    JPLAY(DK_OPEN);
    dn_race_begin(now);
}
// the end of the race on this board (the places by slot)
static void dn_finish(const uint8_t *place)
{
    DN.ended = true;
    memcpy(DN.final_place, place, NTRUCK);
    bool used[NTRUCK] = {0};
    for (int t = 0; t < NTRUCK; t++) {
        int p = place[dn_slot(t)];
        if (p < 1 || p > NTRUCK || used[p - 1]) {
            places();
            break;
        }  // (bad places: the progress here)
        used[p - 1] = true;
        s_tr[t].place = p;
        s_order[p - 1] = t;
    }
    int pl = s_tr[0].place;
    s_prize = PRIZE[pl - 1] + s_pickup_money;
    s_money += s_prize;
    s_cans = s_tr[0].nitro;
    save_all();
    s_gs = GS_RESULT;
    s_gt = 0;
    sfx_play(pl == 1 ? SFX_WIN : SFX_GAME_OVER);
    ESP_LOGI(TAG, "net: the end of the friend race: place %d, prize %d", pl, s_prize);
}
// NEXT after the result of a friend race: back to the room
static void dn_back_to_room(int64_t now)
{
    DN.race = false;
    DN.my = 0;
    for (int t = 0; t < NTRUCK; t++) {
        s_tr[t].remote = false;
    }
    for (int k = 0; k < DN.nmem; k++) {
        DN.mem[k].heard = now;
    }
    DN.host_heard = now;
    if (DN.party == DPTY_NONE) {
        dn_lobby_go(now);
        return;
    }
    s_gs = GS_ROOM;
    s_gt = 0;
    race_init(DN.arena, true);
}
// a member: the host is gone, or it closed the race
static void dn_host_gone(int64_t now, const char *why)
{
    DN.party = DPTY_NONE;
    DN.nmem = 0;
    if (DN.race && !DN.ended) {
        DN.race = false;
        for (int t = 0; t < NTRUCK; t++) {
            s_tr[t].remote = false;
        }
        title_go();
        dn_link_on();
    } else if (DN.race) {
        return;  // (the result shows: NEXT goes to the list)
    }
    DN.race = false;
    s_gs = GS_LOBBY;
    s_gt = 0;
    dn_note(why, now);
}

static int dn_mem_of(const uint8_t mac[6])
{
    for (int k = 0; k < DN.nmem; k++) {
        if (!memcmp(DN.mem[k].mac, mac, 6)) {
            return k;
        }
    }
    return -1;
}
static int dn_slot_of(const uint8_t mac[6])
{
    for (int g = 0; g < NTRUCK; g++) {
        if (DN.human[g] && !memcmp(DN.slot_mac[g], mac, 6)) {
            return g;
        }
    }
    return -1;
}
static void dn_room_packet(const link_msg_t *m, int64_t now)
{
    const uint8_t *d = m->data;
    if (m->len < 4 || !dn_friend(m->mac)) {
        return;
    }
    const int n = d[3];
    if (n < 1 || n > NTRUCK || m->len < 4 + 6 * n) {
        return;
    }
    int i = 0;
    while (i < DN.npty && memcmp(DN.pty[i].mac, m->mac, 6)) {
        i++;
    }
    if (i == DN.npty) {
        if (DN.npty == DN_MAXPTY) {
            return;
        }
        DN.npty++;
        memcpy(DN.pty[i].mac, m->mac, 6);
    }
    DN.pty[i].plays = d[1];
    DN.pty[i].arena = d[2] % DK_NARENA;
    DN.pty[i].n = (uint8_t)n;
    DN.pty[i].seen = now;
    if (DN.party != DPTY_MEMBER || memcmp(m->mac, DN.host, 6)) {
        return;
    }
    // my race: the members, the arena
    DN.host_heard = now;
    DN.nmem = n;
    DN.listed = false;
    for (int k = 0; k < n; k++) {
        memcpy(DN.mem[k].mac, d + 4 + 6 * k, 6);
        if (!memcmp(DN.mem[k].mac, DN.me, 6)) {
            DN.listed = true;
        }
    }
    if (!DN.race && d[2] % DK_NARENA != DN.arena) {
        DN.arena = d[2] % DK_NARENA;
        if (s_gs == GS_ROOM) {
            dn_background(DN.arena);
        }
    }
}
static void dn_start_packet(const link_msg_t *m, int64_t now)
{
    if (DN.party != DPTY_MEMBER || memcmp(m->mac, DN.host, 6) || (DN.race && !DN.ended) || m->len < 2 + 8 * NTRUCK) {
        return;
    }
    const uint8_t *p = m->data + 1;
    int arena = *p++ % DK_NARENA, my = -1;
    bool human[NTRUCK];
    uint8_t mac[NTRUCK][6], grid[NTRUCK];
    for (int g = 0; g < NTRUCK; g++) {
        human[g] = *p++ != 0;
        memcpy(mac[g], p, 6);
        p += 6;
        grid[g] = *p++ % NTRUCK;
        if (human[g] && !memcmp(mac[g], DN.me, 6)) {
            my = g;
        }
    }
    if (my <= 0) {
        return;  // (this board is not in it)
    }
    DN.arena = arena;
    DN.my = my;
    memcpy(DN.human, human, sizeof human);
    memcpy(DN.slot_mac, mac, sizeof mac);
    memcpy(DN.grid, grid, sizeof grid);
    DN.host_heard = now;
    JPLAY(DK_OPEN);
    dn_race_begin(now);
}
static void dn_world_packet(const link_msg_t *m, int64_t now)
{
    if (!dn_client() || memcmp(m->mac, DN.host, 6) || m->len < 7) {
        return;
    }
    DN.host_heard = now;
    const uint8_t *p = m->data + 1, *e = m->data + m->len;
    int phase = *p++;
    uint8_t place[NTRUCK];
    memcpy(place, p, NTRUCK);
    p += NTRUCK;
    if (DN.ended) {
        return;
    }
    if (phase == 2) {
        dn_finish(place);
        return;
    }
    if (phase == 1 && s_gs == GS_COUNT) {
        s_gs = GS_RACE;
        s_gt = 0;
        sfx_play(SFX_GO);
    }
    int n = *p++;
    for (int k = 0; k < n && p + DN_TB <= e; k++, p += DN_TB) {
        dn_get(p);
    }
    if (p >= e) {
        return;
    }
    int ni = *p++, i = 0;
    for (int k = 0; k < ni && i < NITEM && p + 6 <= e; k++) {
        item_t *I = &s_item[i];
        uint8_t id = *p++, kind = *p++;
        float x = dn_r16s(&p) / 100.0f, y = dn_r16s(&p) / 100.0f;
        if (dn_was_taken(id)) {
            continue;
        }
        if (I->id != id) {
            I->t = 0;
        }
        I->on = true;
        I->id = id;
        I->kind = kind & 1;
        I->x = x;
        I->y = y;
        i++;
    }
    for (; i < NITEM; i++) {
        s_item[i].on = false;
    }
}
static void dn_truck_packet(const link_msg_t *m, int64_t now)
{
    if (!DN.race || DN.ended || m->len < 1 + DN_TB) {
        return;
    }
    const uint8_t *p = m->data + 1;
    int g = p[0] % NTRUCK;
    if (!DN.human[g] || memcmp(DN.slot_mac[g], m->mac, 6) || g == DN.my) {
        return;
    }
    int t = dn_local(g);
    DN.heard[t] = now;
    dn_get(p);
    if (DN.my != 0 || m->len < 2 + DN_TB) {
        return;
    }
    // the host: the pick-ups that the friend took
    const uint8_t *q = p + DN_TB;
    int n = *q++;
    for (int k = 0; k < n && q < m->data + m->len; k++, q++) {
        for (int i = 0; i < NITEM; i++) {
            if (s_item[i].on && s_item[i].id == *q) {
                s_item[i].on = false;
            }
        }
    }
}
// each frame, before the game: the packets, the time outs, the end of the race
static void dn_poll(int64_t now)
{
    if (!DN.on) {
        return;
    }
    link_msg_t m;
    for (int guard = 0; guard < 40 && link_recv(&m); guard++) {
        if (m.app != DN_APP || m.len < 1 || m.data[0] != DN_VER) {
            continue;
        }
        if (m.kind == DK_ROOM) {
            dn_room_packet(&m, now);
        } else if (m.kind == DK_JOIN && DN.party == DPTY_HOST && m.len >= 7 && !memcmp(m.data + 1, DN.me, 6) &&
                   dn_friend(m.mac)) {
            int k = dn_mem_of(m.mac);
            if (k < 0) {
                if (DN.nmem >= NTRUCK || DN.race) {
                    continue;  // (full; or a race runs)
                }
                k = DN.nmem++;
                memcpy(DN.mem[k].mac, m.mac, 6);
                sfx_tone(700, 1050, 90, W_SQUARE, 26);
                ESP_LOGI(TAG, "net: %s joins the race", friends_name_of(m.mac));
            }
            DN.mem[k].heard = now;
        } else if (m.kind == DK_LEAVE && DN.party == DPTY_HOST && m.len >= 7 && !memcmp(m.data + 1, DN.me, 6)) {
            int k = dn_mem_of(m.mac);
            if (k > 0) {
                DN.mem[k] = DN.mem[--DN.nmem];
                ESP_LOGI(TAG, "net: %s leaves the race", friends_name_of(m.mac));
            }
            int g = dn_slot_of(m.mac);
            if (g > 0 && DN.race && !DN.ended) {
                s_tr[g].remote = false;
                s_tr[g].bot = true;
                DN.human[g] = false;
            }
        } else if (m.kind == DK_START) {
            dn_start_packet(&m, now);
        } else if (m.kind == DK_WORLD) {
            dn_world_packet(&m, now);
        } else if (m.kind == DK_TRUCK) {
            dn_truck_packet(&m, now);
        } else if (m.kind == DK_BYE) {
            if (DN.party == DPTY_MEMBER && !memcmp(m.mac, DN.host, 6)) {
                dn_host_gone(now, TR("THE RACE CLOSED", "המרוץ נסגר"));
            }
            int i = 0;
            while (i < DN.npty && memcmp(DN.pty[i].mac, m.mac, 6)) {
                i++;
            }
            if (i < DN.npty) {
                DN.pty[i] = DN.pty[--DN.npty];
            }
        }
    }
    // the races that sent nothing for 2.5 s are not in the list
    int k = 0;
    for (int i = 0; i < DN.npty; i++) {
        if (now - DN.pty[i].seen < 2500) {
            DN.pty[k++] = DN.pty[i];
        }
    }
    DN.npty = k;
    if (DN.party == DPTY_HOST && !DN.race) {
        // (the room: a member that sent no JOIN for 3 s is out)
        k = 1;
        for (int i = 1; i < DN.nmem; i++) {
            if (now - DN.mem[i].heard < DN_GONE_MS) {
                DN.mem[k++] = DN.mem[i];
            }
        }
        DN.nmem = k;
    }
    if (DN.party == DPTY_MEMBER && now - DN.host_heard > DN_GONE_MS) {
        dn_host_gone(now, TR("THE HOST IS GONE", "המארח יצא"));
    }
    if (DN.race && DN.my == 0 && !DN.ended && (s_gs == GS_COUNT || s_gs == GS_RACE)) {
        // the host: a friend that sent nothing for 3 s: a bot drives its truck
        for (int t = 1; t < NTRUCK; t++) {
            if (s_tr[t].remote && now - DN.heard[t] > DN_GONE_MS) {
                s_tr[t].remote = false;
                s_tr[t].bot = true;
                DN.human[t] = false;
                ESP_LOGW(TAG, "net: the board of truck %d is gone: a bot drives it", t);
            }
        }
        // the end: all the friends over the line, or 25 s after the first one
        bool all = true, any = false;
        for (int t = 0; t < NTRUCK; t++) {
            if (DN.human[t]) {
                if (s_tr[t].done) {
                    any = true;
                } else {
                    all = false;
                }
            }
        }
        if (any && DN.first_done < 0) {
            DN.first_done = s_race_t;
        }
        if (s_gs == GS_RACE && (all || (DN.first_done >= 0 && s_race_t - DN.first_done > 25) || s_race_t > 600)) {
            places();
            uint8_t place[NTRUCK];
            for (int t = 0; t < NTRUCK; t++) {
                place[t] = (uint8_t)s_tr[t].place;
            }
            DN.ends = 40;
            dn_finish(place);
        }
    }
}
// each frame, after the game: the sends
static void dn_send(int64_t now)
{
    if (!DN.on) {
        return;
    }
    bool racing = DN.race && (s_gs == GS_COUNT || s_gs == GS_RACE);
    if (DN.party == DPTY_HOST && !DN.race) {
        friends_invite(now);  // (the room waits for the friends: in each frame)
    }
    if (DN.party == DPTY_HOST && now - DN.beacon_at >= (DN.race ? 1000 : 500)) {
        DN.beacon_at = now;
        dn_send_room();
        if (!DN.race) {
            friends_beacon(now);
        }
    } else if (DN.party == DPTY_MEMBER && !racing && now - DN.beacon_at >= 500) {
        DN.beacon_at = now;
        dn_send_to_host(DK_JOIN);
        friends_beacon(now);
    } else if (DN.party == DPTY_NONE && now - DN.beacon_at >= 500) {
        DN.beacon_at = now;
        friends_beacon(now);
    }
    if (!DN.race || now - DN.tick_at < DN_TICK_MS) {
        return;
    }
    DN.tick_at = now;
    if (DN.my == 0) {
        if (DN.starts > 0) {
            DN.starts--;
            dn_send_start();
        }
        if (racing || DN.ends > 0) {
            if (DN.ended) {
                DN.ends--;
            }
            dn_send_world(now);
        }
    } else if (racing) {
        dn_send_truck();
    }
}

// ---- the pages: the list of the races, the room
#if BAR
static const box_t B_OPEN = {438, 30, 190, 58}, B_BACK = {438, 98, 190, 52};
static box_t dn_row(int i)
{
    box_t b = {16, 46 + i * 38, 404, 34};
    return b;
}
static const box_t B_APREV = {16, 44, 44, 40}, B_ANEXT = {376, 44, 44, 40};
static box_t dn_chip(int k)
{
    box_t b = {16 + k * 102, 96, 98, 34};
    return b;
}
#elif T5
static const box_t B_OPEN = {160, 284, 154, 58}, B_BACK = {326, 284, 154, 58};
static box_t dn_row(int i)
{
    box_t b = {36, 78 + i * 58, 568, 52};
    return b;
}
static const box_t B_APREV = {36, 72, 60, 54}, B_ANEXT = {544, 72, 60, 54};
static box_t dn_chip(int k)
{
    box_t b = {36 + (k % 2) * 288, 140 + (k / 2) * 58, 280, 52};
    return b;
}
#elif R_RND
// (the round frame: in the circle of the square frame of 310 px)
static const box_t B_OPEN = {43, 218, 108, 42}, B_BACK = {159, 218, 108, 42};
static box_t dn_row(int i)
{
    box_t b = {40, 90 + i * 42, 230, 38};
    return b;
}
static const box_t B_APREV = {28, 76, 44, 40}, B_ANEXT = {238, 76, 44, 40};
static box_t dn_chip(int k)
{
    box_t b = {40 + (k % 2) * 118, 126 + (k / 2) * 42, 114, 38};
    return b;
}
#else
static const box_t B_OPEN = {20, 186, 136, 46}, B_BACK = {164, 186, 136, 46};
static box_t dn_row(int i)
{
    box_t b = {14, 42 + i * 44, 292, 40};
    return b;
}
static const box_t B_APREV = {14, 40, 44, 40}, B_ANEXT = {262, 40, 44, 40};
static box_t dn_chip(int k)
{
    box_t b = {14 + (k % 2) * 148, 92 + (k / 2) * 42, 144, 38};
    return b;
}
#endif
// a line of the lobby: at x (the round frame: in the middle of the circle)
static void dn_line(const kfont_t *f, int x, int y, const char *t, uint16_t c)
{
    if (R_RND) {
        ui_text_c(f, SW / 2, y, t, c);
    } else {
        text_l(f, x, y, t, c);
    }
}
static void dn_draw(void)
{
    char b[64], nm[12];
    int64_t now = s_now_ms;
    if (BAR) {
        shade(6, 6, 420, 160, 170);
    } else if (T5) {
        shade(20, 16, 600, 260, 170);
    } else {
        shade(8, 8, R_RND ? 294 : 304, R_RND ? 294 : 172, 170);  // (the round frame: the whole circle)
    }
    int tx = BAR ? 16 : T5 ? 36 : R_RND ? 50 : 14, ty = BAR ? 32 : T5 ? 50 : R_RND ? 74 : 30;  // (round: in the circle)
    const kfont_t *fl = T5 ? &KF_NAME : &KF_LABEL;  // (the Tab5: the bigger words)
    const int ls = T5 ? 30 : 22;
    if (s_gs == GS_LOBBY) {
        dn_line(&KF_BIG, tx, ty, TR("RACE WITH FRIENDS", "מרוץ עם חברים"), rgb(242, 194, 30));
        int shown = 0;
        for (int i = 0; i < DN.npty && shown < 3; i++, shown++) {
            box_t r = dn_row(shown);
            bool full = DN.pty[i].n >= NTRUCK || DN.pty[i].plays;
            ui_rbox(r.x, r.y, r.w, r.h, 8, full ? rgb(44, 44, 52) : rgb(60, 90, 60), rgb(220, 220, 200), 1);
            dn_name(DN.pty[i].mac, nm);
            snprintf(b, sizeof b, "%s  %s", nm, DK_ARENAS[DN.pty[i].arena].name);
            text_l(T5 ? &KF_BIG : &KF_NAME, r.x + 10, r.y + r.h / 2 + (T5 ? 10 : 7), b, rgb(255, 255, 255));
            if (DN.pty[i].plays) {
                snprintf(b, sizeof b, "%s", TR("PLAYS", "משחק"));
            } else {
                snprintf(b, sizeof b, "%d/%d", DN.pty[i].n, NTRUCK);
            }
            ui_text_c(T5 ? &KF_BIG : &KF_NAME, r.x + r.w - (T5 ? 50 : 34), r.y + r.h / 2 + (T5 ? 10 : 7), b,
                      rgb(200, 230, 200));
        }
        if (!shown) {
            dn_line(fl, tx, ty + ls + 8,
                    TR("NO OPEN RACE OF A FRIEND NOW. OPEN ONE:", "אין עכשיו מרוץ פתוח של חבר. פתחו אחד:"),
                    rgb(230, 230, 236));
            dn_line(fl, tx, ty + 2 * ls + 8,
                    TR("YOUR FRIENDS OPEN FRIENDS AND JOIN IT.", "החברים נכנסים לחברים ומצטרפים."), rgb(200, 200, 210));
            dn_line(fl, tx, ty + 3 * ls + 8, TR("ADD A FRIEND: SETTINGS > FRIENDS.", "להוספת חבר: הגדרות > חברים."),
                    rgb(170, 170, 180));
        }
        button(B_OPEN, TR("OPEN A RACE", "פתח מרוץ"), rgb(190, 60, 40), true);
        button(B_BACK, TR("BACK", "חזרה"), rgb(60, 60, 80), true);
    } else {
        if (DN.party == DPTY_HOST) {
            snprintf(b, sizeof b, "%s", TR("YOUR RACE", "המרוץ שלך"));
        } else {
            dn_name(DN.host, nm);
            snprintf(b, sizeof b, TR("THE RACE OF %s", "המרוץ של %s"), nm);
        }
        text_l(&KF_NAME, tx, ty - 8, b, rgb(242, 194, 30));
        box_t L = B_APREV, R = B_ANEXT;
        if (DN.party == DPTY_HOST) {
            button(L, "<", rgb(70, 70, 90), true);
            button(R, ">", rgb(70, 70, 90), true);
        }
        ui_text_c(&KF_BIG, (L.x + R.x + R.w) / 2, L.y + L.h / 2 + 10, DK_ARENAS[DN.arena].name, rgb(255, 210, 150));
        for (int k = 0; k < NTRUCK; k++) {
            box_t c = dn_chip(k);
            bool hu = k < DN.nmem;
            rect_a(c.x, c.y, c.w, c.h, 6, rgb(12, 14, 22), 200);
            ui_rbox(c.x + 4, c.y + 5, 6, c.h - 10, 2, *col_of(k), *col_of(k), 1);
            if (hu && !memcmp(DN.mem[k].mac, DN.me, 6)) {
                snprintf(nm, sizeof nm, "%s", TR("YOU", "אתה"));
            } else if (hu) {
                dn_name(DN.mem[k].mac, nm);
            } else {
                snprintf(nm, sizeof nm, "%s", NAMES[k]);
            }
            text_l(fl, c.x + 16, c.y + c.h / 2 + (T5 ? 7 : 6), nm, hu ? rgb(255, 255, 255) : rgb(140, 140, 150));
        }
        if (DN.party == DPTY_HOST) {
            button(B_OPEN, TR("START", "התחל"), rgb(190, 60, 40), DN.nmem >= 2);
            button(B_BACK, TR("CLOSE", "סגור"), rgb(60, 60, 80), true);
            if (DN.nmem < 2 && R_RND) {
                ui_text_c(fl, SW / 2, 278, TR("WAIT FOR A FRIEND TO JOIN", "מחכים שחבר יצטרף"), rgb(200, 200, 210));
            } else if (DN.nmem < 2) {
                text_l(fl, tx,
                       BAR  ? 156
                       : T5 ? 264
                            : 178,
                       TR("WAIT FOR A FRIEND TO JOIN", "מחכים שחבר יצטרף"), rgb(200, 200, 210));
            }
        } else {
            ui_text_c(fl, B_OPEN.x + B_OPEN.w / 2, B_OPEN.y + B_OPEN.h / 2 + 6,
                      DN.listed ? TR("WAIT FOR THE HOST", "מחכים למארח") : TR("JOINS...", "מצטרף..."),
                      rgb(230, 230, 236));
            button(B_BACK, TR("LEAVE", "צא"), rgb(60, 60, 80), true);
        }
    }
    if (now < DN.note_until) {
        ui_text_c(&KF_NAME, BAR ? 216 : SW / 2, BAR ? 160 : T5 ? 264 : R_RND ? 122 : 176, DN.note, rgb(255, 160, 120));
    }
}
static void dn_tap(int x, int y)
{
    int64_t now = s_now_ms;
    if (s_gs == GS_LOBBY) {
        if (in_box(B_BACK, x, y)) {
            sfx_play(SFX_BACK);
            title_go();
            return;
        }
        if (in_box(B_OPEN, x, y)) {
            sfx_play(SFX_CLICK);
            dn_open(now);
            return;
        }
        for (int i = 0; i < DN.npty && i < 3; i++) {
            if (in_box(dn_row(i), x, y)) {
                if (DN.pty[i].n >= NTRUCK || DN.pty[i].plays) {
                    sfx_play(SFX_BACK);
                    return;
                }
                sfx_play(SFX_CLICK);
                dn_join(i, now);
                return;
            }
        }
        return;
    }
    if (in_box(B_BACK, x, y)) {
        sfx_play(SFX_BACK);
        dn_leave_party();
        s_gs = GS_LOBBY;
        s_gt = 0;
        return;
    }
    if (DN.party != DPTY_HOST) {
        return;
    }
    if (in_box(B_OPEN, x, y) && DN.nmem >= 2) {
        dn_start(now);
        return;
    }
    int d = in_box(B_APREV, x, y) ? -1 : in_box(B_ANEXT, x, y) ? 1 : 0;
    if (d) {
        sfx_play(SFX_CLICK);
        DN.arena = (DN.arena + d + DK_NARENA) % DK_NARENA;
        dn_background(DN.arena);
        DN.beacon_at = now - 1000;  // (the members see it at once)
    }
}
