// COIL RIDER: the terrain and the physics of the bike (included by app_coil.c).
// THE TERRAIN: the edges of the rings of the level (cr_levels.c) in metres, and a grid of 2 m cells with the edges that
// touch each cell (the queries of the physics).
// THE BIKE: a soft body in fixed steps of 1/360 s. A rigid body (the frame with the rider: a position, an angle) and two
// wheels. Each wheel is a mass on a damped spring to its rest point on the body: soft along the axis of the
// suspension (the rod stretches and compresses: the player sees it and uses it), stiff across it. A wheel is a circle
// that rolls with friction on the edges in any direction (floors, walls, ceilings). The engine turns the rear wheel
// and gives the opposite torque to the body (the front lifts). The lean is a torque on the body and an opposite couple
// on the wheels. The head of the rider is a circle on the body: when it touches the terrain, the run ends (a crash).

// ---------------------------------------------------------------- the terrain
#define MAXE 3000
typedef struct {
    float ax, ay, dx, dy, len;
    uint8_t shape;
} edge_t;
static edge_t E[MAXE];
static int s_ne;
static uint8_t s_smat[64];  // the material of each shape
static int s_nshape;
#define CELL    2.0f
#define ICELL   0.5f
#define MAXCELL 20000
#define MAXIDX  16000
static uint16_t s_cell0[MAXCELL + 1];  // the first index of each cell in s_cidx (the counts: the next start)
static uint16_t s_cidx[MAXIDX];
static int s_gw, s_gh;                    // the grid size (cells)
static float s_gx0, s_gy0;                // the grid origin (m)
static float s_bx0, s_by0, s_bx1, s_by1;  // the play space (m)
static uint32_t s_stamp[MAXE];            // (a query sees an edge one time)
static uint32_t s_qn;

static inline int cell_of(float x, float y)
{
    int cx = (int)((x - s_gx0) * ICELL), cy = (int)((y - s_gy0) * ICELL);
    if (cx < 0) {
        cx = 0;
    }
    if (cy < 0) {
        cy = 0;
    }
    if (cx >= s_gw) {
        cx = s_gw - 1;
    }
    if (cy >= s_gh) {
        cy = s_gh - 1;
    }
    return cy * s_gw + cx;
}
static void terrain_load(const cr_level_t *L)
{
    s_ne = 0;
    s_nshape = L->ns < 32 ? L->ns : 32;  // (the parity of the fill: 32 bits)
    for (int i = 0; i < s_nshape; i++) {
        s_smat[i] = L->smat[i];
    }
    for (int r = 0; r < L->nr; r++) {
        int v0 = L->r[3 * r], n = L->r[3 * r + 1], sh = L->r[3 * r + 2];
        for (int k = 0; k < n && s_ne < MAXE; k++) {
            int a = v0 + k, b = v0 + (k + 1) % n;
            float ax = L->v[2 * a] * (1.0f / CR_VK), ay = L->v[2 * a + 1] * (1.0f / CR_VK);
            float bx = L->v[2 * b] * (1.0f / CR_VK), by = L->v[2 * b + 1] * (1.0f / CR_VK);
            float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
            if (l2 < 1e-6f) {
                continue;
            }
            float il = frsqrt(l2);
            edge_t *e = &E[s_ne++];
            e->ax = ax;
            e->ay = ay;
            e->dx = dx * il;
            e->dy = dy * il;
            e->len = l2 * il;
            e->shape = (uint8_t)sh;
        }
    }
    s_bx0 = L->box[0] * (1.0f / CR_VK);
    s_by0 = L->box[1] * (1.0f / CR_VK);
    s_bx1 = L->box[2] * (1.0f / CR_VK);
    s_by1 = L->box[3] * (1.0f / CR_VK);
    // the grid (a margin of one cell round the play space)
    s_gx0 = s_bx0 - CELL;
    s_gy0 = s_by0 - CELL;
    s_gw = (int)((s_bx1 - s_gx0) * ICELL) + 2;
    s_gh = (int)((s_by1 - s_gy0) * ICELL) + 2;
    while (s_gw * s_gh > MAXCELL) {
        s_gh--;  // (the level tool keeps the levels in the size)
    }
    int nc = s_gw * s_gh;
    memset(s_cell0, 0, sizeof(uint16_t) * (size_t)(nc + 1));
    // two passes: the counts, then the lists
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            int sum = 0;
            for (int c = 0; c < nc; c++) {
                int k = s_cell0[c];
                s_cell0[c] = (uint16_t)sum;
                sum += k;
            }
            s_cell0[nc] = (uint16_t)(sum < MAXIDX ? sum : MAXIDX);
        }
        static uint16_t fill[MAXCELL];
        if (pass == 1) {
            memset(fill, 0, sizeof(uint16_t) * (size_t)nc);
        }
        for (int i = 0; i < s_ne; i++) {
            const edge_t *e = &E[i];
            float bx = e->ax + e->dx * e->len, by = e->ay + e->dy * e->len;
            float x0 = fminf(e->ax, bx) - 1.0f, x1 = fmaxf(e->ax, bx) + 1.0f;  // (a margin of the biggest circle)
            float y0 = fminf(e->ay, by) - 1.0f, y1 = fmaxf(e->ay, by) + 1.0f;
            int cx0 = (int)((x0 - s_gx0) * ICELL), cx1 = (int)((x1 - s_gx0) * ICELL);
            int cy0 = (int)((y0 - s_gy0) * ICELL), cy1 = (int)((y1 - s_gy0) * ICELL);
            cx0 = clampi(cx0, 0, s_gw - 1);
            cx1 = clampi(cx1, 0, s_gw - 1);
            cy0 = clampi(cy0, 0, s_gh - 1);
            cy1 = clampi(cy1, 0, s_gh - 1);
            for (int cy = cy0; cy <= cy1; cy++) {
                for (int cx = cx0; cx <= cx1; cx++) {
                    int c = cy * s_gw + cx;
                    if (pass == 0) {
                        s_cell0[c]++;
                        continue;
                    }
                    int at = s_cell0[c] + fill[c];
                    if (at < MAXIDX && at < s_cell0[c + 1]) {
                        s_cidx[at] = (uint16_t)i;
                        fill[c]++;
                    }
                }
            }
        }
    }
    memset(s_stamp, 0, sizeof s_stamp);
    s_qn = 0;
}

// The contacts of the circle (x, y, r): each edge that is nearer than r. The closest point on an edge, or its first
// vertex (each vertex belongs to the edge that starts at it: a corner gives one contact). fn(...) gets the push out.
typedef struct {
    float nx, ny, pen;
    int e;
} hit_t;
#define MAXHIT 6
static int circle_hits(float x, float y, float r, hit_t *h)
{
    int n = 0;
    s_qn++;
    int c = cell_of(x, y);
    int a = s_cell0[c], b = s_cell0[c + 1];
    float r2 = r * r;
    for (int k = a; k < b; k++) {
        int i = s_cidx[k];
        if (s_stamp[i] == s_qn) {
            continue;
        }
        s_stamp[i] = s_qn;
        const edge_t *e = &E[i];
        float qx = x - e->ax, qy = y - e->ay;
        float t = qx * e->dx + qy * e->dy;
        if (t >= e->len) {
            continue;  // (the end vertex: the next edge has it)
        }
        float px, py;
        if (t <= 0) {
            px = qx;
            py = qy;
        } else {
            px = qx - e->dx * t;
            py = qy - e->dy * t;
        }
        float d2 = px * px + py * py;
        if (d2 >= r2 || d2 < 1e-12f) {
            continue;
        }
        float il = frsqrt(d2), d = d2 * il;
        // (the side: a circle that is on the solid side of an edge (behind it) is not pushed through it)
        float side = qx * e->dy - qy * e->dx;  // + = the air side (the right normal)
        if (t > 0 && side < 0) {
            continue;
        }
        if (n < MAXHIT) {
            h[n].nx = px * il;
            h[n].ny = py * il;
            h[n].pen = r - d;
            h[n].e = i;
            n++;
        }
    }
    return n;
}
static bool circle_touch(float x, float y, float r)
{
    hit_t h[MAXHIT];
    return circle_hits(x, y, r, h) > 0;
}

// ---------------------------------------------------------------- the bike
#define PDT    (1.0f / 360.0f)
#define GRAV   9.81f
#define WR     0.32f     // the radius of a wheel (m)
#define WX     0.56f     // the half of the wheelbase (m)
#define WY     (-0.40f)  // the rest height of the wheel middles under the middle of the body (m)
#define HEAD_X (-0.08f)  // the head of the rider (body frame, the bike faces +x)
#define HEAD_Y 0.66f
#define HEAD_R 0.19f
#define MB     4.5f  // the body (the frame and the rider): the mass, the inertia
#define IB     0.50f
#define MW     0.6f  // a wheel
#define IW     (0.65f * MW * WR * WR)
// the suspension (the tuning knobs)
#define SUS_K      330.0f  // along the axis (N/m): soft, the rod stretches
#define SUS_C      11.0f
#define SUS_KP     4200.0f  // across the axis: stiff
#define SUS_CP     44.0f
#define SUS_CMP    0.32f   // the most compression (m), then a hard stop
#define SUS_EXT    0.30f   // the most stretch (m)
#define ENG_T      16.0f   // the engine torque at the full gas (N m)
#define ENG_REACT  0.50f   // the share of it that turns the body back (the front lifts)
#define WMAX       54.0f   // the top spin of the rear wheel (rad/s: 17 m/s)
#define WREV       14.0f   // the top spin back (rad/s)
#define BRAKE_A    160.0f  // the brake (rad/s2 of the wheel spin)
#define LEAN_T     26.0f   // the lean torque (N m)
#define LEAN_REACT 0.40f   // the share of the lean that the wheels take back (the couple)
#define LEAN_WMAX  4.2f    // the lean stops to add turn speed over this (rad/s)
#define MU         1.15f   // the grip of a tyre
#define CROUCH_D   0.20f   // the crouch: the rest point of each spring goes up by this (m): the body goes down
#define CROUCH_T   0.22f   // the time to load the springs (s)
#define KICK_D     0.24f   // the release: for KICK_T the rest point goes DOWN by this (the springs throw)
#define KICK_T     0.12f

typedef struct {
    float x, y, vx, vy;  // the middle (m, m/s)
    float w, ang;        // the spin (rad/s, + = counterclockwise) and the turn of the drawing
    bool on;             // a contact in the last step
    float nx, ny;        // its normal
} whl_t;
typedef struct {
    float x, y, vx, vy;  // the body middle
    float a, w;          // the angle (rad, + = counterclockwise) and the turn speed
    whl_t wh[2];         // [0] = the wheel at body x < 0, [1] = at body x > 0
    int8_t dir;          // +1 = the bike faces body +x
    float hx;            // the head x in the body frame now (it moves in a turn)
    float lean_f;        // the lean now (-1 = to the left: counterclockwise, +1 = to the right)
    bool crouch;         // the input: the rider loads the springs (both halves held)
    float crouch_f;      // the load now 0..1
    float kick_t;        // the time of the release kick that is left (s)
    float gas;           // the throttle -1..1 (+ = forward)
    float eng;           // the work of the engine now 0..1 (the sound)
    float air_t;         // the time with no wheel on the ground
    float land;          // the hardest hit of a wheel since the last read (m/s)
    bool crash;
    float hit_x, hit_y;  // the place of the crash
} bike_t;

static inline void rot(float a, float *c, float *s)
{
    *c = fcos(a);
    *s = fsin(a);
}
// a point of the body frame in the world
static inline void body_pt(const bike_t *b, float c, float s, float bx, float by, float *x, float *y)
{
    *x = b->x + bx * c - by * s;
    *y = b->y + bx * s + by * c;
}
static void head_pos(const bike_t *b, float *x, float *y)
{
    float c, s;
    rot(b->a, &c, &s);
    body_pt(b, c, s, b->hx, HEAD_Y, x, y);
}
// the bike at rest on the point (x, y) of the ground, the body level
static void bike_place(bike_t *b, float x, float y, int dir)
{
    memset(b, 0, sizeof *b);
    b->dir = (int8_t)(dir < 0 ? -1 : 1);
    b->hx = HEAD_X * b->dir;
    b->x = x;
    b->y = y + WR - WY - 0.07f;
    for (int i = 0; i < 2; i++) {
        b->wh[i].x = x + (i ? WX : -WX);
        b->wh[i].y = y + WR;
    }
}

// the forces of a suspension spring (the wheel i): on the wheel (fx, fy), and on the body the opposite at its rest point
static void spring_force(bike_t *b, int i, float c, float s, float *fwx, float *fwy, float *fbx, float *fby, float *tq)
{
    whl_t *W = &b->wh[i];
    float rx = i ? WX : -WX, ry = WY;
    float px, py;
    body_pt(b, c, s, rx, ry, &px, &py);
    float ax = -s, ay = c;  // the axis of the suspension (the body up)
    float qx = c, qy = s;   // across
    float dx = W->x - px, dy = W->y - py;
    float da = dx * ax + dy * ay, dp = dx * qx + dy * qy;
    float ox = px - b->x, oy = py - b->y;
    float vpx = b->vx - b->w * oy, vpy = b->vy + b->w * ox;  // the speed of the rest point
    float vx = W->vx - vpx, vy = W->vy - vpy;
    float va = vx * ax + vy * ay, vp = vx * qx + vy * qy;
    float shift = b->crouch_f * CROUCH_D - (b->kick_t > 0 ? KICK_D : 0.0f);
    float fa = -SUS_K * (da - shift) - SUS_C * va;
    // the hard stops (da > 0: the wheel is up = the spring is compressed)
    if (da > SUS_CMP) {
        fa -= (da - SUS_CMP) * SUS_K * 30.0f + va * SUS_C * 3.0f;
    }
    if (da < -SUS_EXT) {
        fa -= (da + SUS_EXT) * SUS_K * 30.0f + va * SUS_C * 3.0f;
    }
    float fp = -SUS_KP * dp - SUS_CP * vp;
    float fx = fa * ax + fp * qx, fy = fa * ay + fp * qy;
    *fwx = fx;
    *fwy = fy;
    *fbx = -fx;
    *fby = -fy;
    *tq = ox * -fy - oy * -fx;
}

// one contact of a wheel: the push out, the normal speed off, the friction that rolls the wheel
static void wheel_contact(bike_t *b, whl_t *W, const hit_t *h)
{
    W->x += h->nx * h->pen;
    W->y += h->ny * h->pen;
    float vn = W->vx * h->nx + W->vy * h->ny;
    float jn = 0;
    if (vn < 0) {
        if (-vn > b->land) {
            b->land = -vn;
        }
        jn = -vn * MW;
        W->vx -= h->nx * vn;
        W->vy -= h->ny * vn;
    }
    // (a contact with no speed into the ground still holds the wheel: a small load of the push out)
    jn += h->pen * MW * 30.0f;
    float tx = -h->ny, ty = h->nx;
    float slip = W->vx * tx + W->vy * ty - W->w * WR;
    float jt = -slip * (1.0f / (1.0f / MW + WR * WR / IW));
    float lim = MU * jn;
    if (jt > lim) {
        jt = lim;
    }
    if (jt < -lim) {
        jt = -lim;
    }
    W->vx += tx * jt * (1.0f / MW);
    W->vy += ty * jt * (1.0f / MW);
    W->w += -WR * jt * (1.0f / IW);
    W->on = true;
    W->nx = h->nx;
    W->ny = h->ny;
}

static void bike_step(bike_t *b, float dt)
{
    // the crouch: the load grows while it is held; the release gives a short kick (the springs throw the bike up)
    if (b->crouch) {
        b->crouch_f += dt * (1.0f / CROUCH_T);
        if (b->crouch_f > 1) {
            b->crouch_f = 1;
        }
    } else if (b->crouch_f > 0) {
        if (b->crouch_f > 0.5f) {
            b->kick_t = KICK_T * b->crouch_f;
        }
        b->crouch_f = 0;
    }
    if (b->kick_t > 0) {
        b->kick_t -= dt;
    }
    float c, s;
    rot(b->a, &c, &s);
    // the forces on the body and the wheels
    float bfx = 0, bfy = -GRAV * MB, btq = 0;
    float wfx[2] = {0, 0}, wfy[2] = {-GRAV * MW, -GRAV * MW};
    for (int i = 0; i < 2; i++) {
        float fx, fy, gx, gy, tq;
        spring_force(b, i, c, s, &fx, &fy, &gx, &gy, &tq);
        wfx[i] += fx;
        wfy[i] += fy;
        bfx += gx;
        bfy += gy;
        btq += tq;
    }
    // the lean: a torque on the body; the wheels take back a part of it (a couple)
    {
        float t = -b->lean_f * LEAN_T;             // (+ lean = to the right = clockwise)
        float wl = b->w * (t > 0 ? 1.0f : -1.0f);  // the turn speed in the direction of the lean
        if (wl > LEAN_WMAX) {
            t *= clampf(1 - (wl - LEAN_WMAX), 0, 1);
        }
        btq += t;
        float dx = b->wh[1].x - b->wh[0].x, dy = b->wh[1].y - b->wh[0].y;
        float d2 = dx * dx + dy * dy;
        if (d2 > 0.1f) {
            float f = -t * LEAN_REACT * frcp(d2);
            wfx[1] += f * -dy;
            wfy[1] += f * dx;
            wfx[0] -= f * -dy;
            wfy[0] -= f * dx;
        }
    }
    // the engine and the brake: the rear wheel (behind the direction of the bike)
    int ri = b->dir > 0 ? 0 : 1;
    whl_t *R = &b->wh[ri];
    b->eng = 0;
    float g = b->gas;
    float fw = -R->w * b->dir;  // the spin forward (+ = it drives the bike forward)
    // the speed of the bike along its facing
    float vf = (b->vx * c + b->vy * s) * b->dir;
    if (g > 0) {
        float k = 1 - fw * (1.0f / WMAX);
        if (k > 1) {
            k = 1;
        }
        if (k < 0) {
            k = 0;
        }
        float T = ENG_T * g * k;
        R->w -= b->dir * T * (1.0f / IW) * dt;
        btq += b->dir * T * ENG_REACT;  // (the reaction: the front goes up)
        b->eng = g * (0.4f + 0.6f * k);
    } else if (g < 0) {
        float br = -g;
        if (vf > 0.6f || fw > 2.0f) {
            for (int i = 0; i < 2; i++) {
                float dw = BRAKE_A * br * dt;
                whl_t *W = &b->wh[i];
                if (W->w > dw) {
                    W->w -= dw;
                } else if (W->w < -dw) {
                    W->w += dw;
                } else {
                    W->w = 0;
                }
            }
        } else {
            float k = 1 + fw * (1.0f / WREV);
            if (k > 1) {
                k = 1;
            }
            if (k < 0) {
                k = 0;
            }
            float T = ENG_T * 0.6f * br * k;
            R->w += b->dir * T * (1.0f / IW) * dt;
            btq -= b->dir * T * ENG_REACT;
            b->eng = br * 0.5f;
        }
    }
    // move (semi-implicit Euler)
    b->vx += bfx * (1.0f / MB) * dt;
    b->vy += bfy * (1.0f / MB) * dt;
    b->w += btq * (1.0f / IB) * dt;
    b->x += b->vx * dt;
    b->y += b->vy * dt;
    b->a += b->w * dt;
    for (int i = 0; i < 2; i++) {
        whl_t *W = &b->wh[i];
        W->vx += wfx[i] * (1.0f / MW) * dt;
        W->vy += wfy[i] * (1.0f / MW) * dt;
        W->w -= W->w * 0.05f * dt;
        W->x += W->vx * dt;
        W->y += W->vy * dt;
        W->ang += W->w * dt;
        W->on = false;
        hit_t h[MAXHIT];
        int n = circle_hits(W->x, W->y, WR, h);
        for (int k = 0; k < n; k++) {
            // (the push of an earlier contact can free a later one: test it again)
            float qx = W->x, qy = W->y;
            if (k) {
                const edge_t *e = &E[h[k].e];
                float px = qx - e->ax, py = qy - e->ay, t = px * e->dx + py * e->dy;
                if (t < 0) {
                    t = 0;
                }
                if (t > e->len) {
                    t = e->len;
                }
                float ex = px - e->dx * t, ey = py - e->dy * t, d2 = ex * ex + ey * ey;
                if (d2 >= WR * WR || d2 < 1e-12f) {
                    continue;
                }
                float il = frsqrt(d2);
                h[k].nx = ex * il;
                h[k].ny = ey * il;
                h[k].pen = WR - d2 * il;
            }
            wheel_contact(b, W, &h[k]);
        }
    }
    // the head
    {
        float hx, hy;
        rot(b->a, &c, &s);
        body_pt(b, c, s, b->hx, HEAD_Y, &hx, &hy);
        if (circle_touch(hx, hy, HEAD_R)) {
            b->crash = true;
            b->hit_x = hx;
            b->hit_y = hy;
        }
        if (hx < s_bx0 - 2 || hx > s_bx1 + 2 || hy < s_by0 - 2 || hy > s_by1 + 2) {
            b->crash = true;
            b->hit_x = hx;
            b->hit_y = hy;
        }
    }
    if (b->wh[0].on || b->wh[1].on) {
        b->air_t = 0;
    } else {
        b->air_t += dt;
    }
}
