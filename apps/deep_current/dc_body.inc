// DEEP CURRENT: the creature (included by app_deep.c). A chain of segments that follows the head (each point keeps its
// distance to the point before it), with a wave that runs down the body. Each segment carries one trait (its organ);
// the head carries the JAWS and the LURE. The traits set the physics of the swim: a long body is fast but turns
// wide, fins turn sharp, a shell sinks, stingers drag. A bite of a hunter tears off the segment that it bites (a
// shell plate stops it, spines prick the hunter): the body is the life.

#define MAXSEG 18
typedef struct {
    float x[MAXSEG + 1], y[MAXSEG + 1];  // 0 = the head (the world wraps in x)
    uint8_t kind[MAXSEG + 1];            // the trait of each segment (T_NONE = a plain segment)
    uint8_t plate[MAXSEG + 1];           // the shell plates that are left (SHELL)
    float grow[MAXSEG + 1];              // the grow-in of a new part (0 -> 1)
    int n;                               // the body segments 1..n (0 is the head)
    int jaws, lure;                      // the organs of the head
    float head_grow;                     // the grow-in of a new head organ
    float a, sp;                         // the heading, the swim speed
    float vx, vy;                        // the velocity now (the swim, the dash, the pushes, the current)
    float kx, ky;                        // a push that fades
    float dash_t, dash_cd;               // the dash: the time left, the cool-down left
    float wave;                          // the phase of the wave of the body
    float hurt;                          // no damage while > 0 (after a bite)
    float pulse;                         // a light that runs down the body (a gulp): the place in segments
    uint8_t pulse_c[3];
    float boot_cd;  // the BOOT organs: the cool-down
    float stun;     // a sting: no control while > 0
    float dead;     // 0 = alive, else the time since the death
    float S, vmax, turn, acc, gape, alert, light, dash_v, dash_cool, len, sink;  // the physics (body_derive)
    uint8_t cnt[T_N];
    uint8_t scheme;  // 0 = the player (cyan light), 1 = the rival (warm light)
} body_t;
static body_t B;  // the player
static body_t R;  // the rival (zones 3 and 4)

static float seg_space(const body_t *b, int i)
{
    float s = 7.8f * b->S * (i > 8 ? 1 - (i - 8) * 0.05f : 1);
    if (b->kind[i] == T_TAIL) {
        s *= 1.25f;
    }
    return s * (0.35f + 0.65f * b->grow[i]);
}
static float seg_r(const body_t *b, int i)
{
    int n = b->n;
    float f = n > 0 ? (float)i / n : 0;
    float s = sinf(3.14159265f * fminf(1, f * 0.92f + 0.12f));
    return b->S * (3 + 7.6f * powf(fmaxf(s, 0), 0.8f));
}
static void body_derive(body_t *b)
{
    memset(b->cnt, 0, sizeof b->cnt);
    for (int i = 1; i <= b->n; i++) {
        b->cnt[b->kind[i]]++;
    }
    int n = b->n;
    b->S = 0.58f + 0.048f * (n > 16 ? 16 : n);
    float S = b->S;
    int tail = b->cnt[T_TAIL], fin = b->cnt[T_FIN], spine = b->cnt[T_SPINE], shell = b->cnt[T_SHELL];
    int sting = b->cnt[T_STING], jet = b->cnt[T_JET], glass = b->cnt[T_GLASS];
    b->vmax = (56 + 5.0f * n + 13 * tail) * (1 - 0.025f * fin) * (1 - 0.035f * spine) * (1 - 0.045f * sting) *
              (1 - 0.05f * shell);
    b->turn = (3.4f + 0.75f * fin) / (1 + 0.045f * n + 0.11f * tail);
    b->acc = fmaxf(0.9f, 2.4f + 0.45f * fin - 0.32f * shell);
    b->gape = S * (4.4f + 2.6f * b->jaws);
    b->alert = clampf(1 - 0.28f * glass, 0.35f, 1);
    b->light = (26 + 4.0f * n) * S * (1 + 1.4f * b->lure);
    b->dash_v = b->vmax * (1.0f + 0.55f * jet);
    b->dash_cool = 1.25f / (1 + 0.45f * jet);
    b->sink = 5.0f * shell;
    float L = 0;
    for (int i = 1; i <= n; i++) {
        L += seg_space(b, i);
    }
    b->len = L + seg_r(b, 0) + seg_r(b, n);
}
static void body_make(body_t *b, float x, float y, int nseg, int scheme)
{
    memset(b, 0, sizeof *b);
    b->n = nseg;
    b->scheme = (uint8_t)scheme;
    for (int i = 0; i <= MAXSEG; i++) {
        b->grow[i] = 1;
        b->kind[i] = T_NONE;
    }
    b->head_grow = 1;
    body_derive(b);
    for (int i = 0; i <= nseg; i++) {
        b->x[i] = wrapx(x - i * 6.0f * b->S);
        b->y[i] = y;
    }
    b->pulse = -10;
}
// the mouth: a point in front of the head, and its reach
static void body_mouth(const body_t *b, float *mx, float *my, float *reach)
{
    float r0 = seg_r(b, 0), f = r0 + b->S * (1.5f + 1.8f * b->jaws);
    *mx = wrapx(b->x[0] + cosf(b->a) * f);
    *my = b->y[0] + sinf(b->a) * f;
    *reach = b->S * (3.0f + 1.6f * b->jaws) * (b->dash_t > 0 ? 1.35f : 1.0f);
}
// the place (0..1 of the body) where a new part grows: fins near the head, stingers and tails at the end
static float part_place(int t)
{
    switch (t) {
    case T_FIN:
        return 0.2f;
    case T_SPINE:
    case T_SHELL:
    case T_GLASS:
        return 0.5f;
    case T_INK:
    case T_JET:
        return 0.62f;
    case T_STING:
    case T_TAIL:
        return 1.0f;
    default:
        return 0.55f;
    }
}
// a new part (a growth): a head organ makes the head stronger and the body one plain segment longer
static void body_add(body_t *b, int t)
{
    if (t == T_JAWS || t == T_LURE) {
        if (t == T_JAWS) {
            b->jaws++;
        } else {
            b->lure = 1;
        }
        b->head_grow = 0;
        t = T_NONE;
    }
    if (b->n >= MAXSEG) {
        body_derive(b);
        return;
    }
    int at = 1 + (int)lroundf(part_place(t) * b->n);
    if (at > b->n + 1) {
        at = b->n + 1;
    }
    if (at < 1) {
        at = 1;
    }
    for (int j = b->n + 1; j > at; j--) {
        b->x[j] = b->x[j - 1];
        b->y[j] = b->y[j - 1];
        b->kind[j] = b->kind[j - 1];
        b->plate[j] = b->plate[j - 1];
        b->grow[j] = b->grow[j - 1];
    }
    // (the new point starts between its two neighbours: the chain then opens it slowly)
    int p = at - 1;
    b->x[at] = b->x[p];
    b->y[at] = b->y[p];
    b->kind[at] = (uint8_t)t;
    b->plate[at] = t == T_SHELL ? 2 : 0;
    b->grow[at] = 0;
    b->n++;
    body_derive(b);
}
// a bite of a hunter on segment i: 0 = stopped (a shell, or the time of no damage), 1 = spines (the hunter is hurt),
// 2 = the segment is torn off, 3 = the creature dies (no segment was left)
enum {
    BITE_STOP,
    BITE_SPINE,
    BITE_TORN,
    BITE_DEAD
};
static int body_bite(body_t *b, int i, int *kind)
{
    *kind = T_NONE;
    if (b->hurt > 0 || b->dead > 0) {
        return BITE_STOP;
    }
    if (b->n == 0) {
        b->dead = 0.001f;
        return BITE_DEAD;
    }
    if (i < 1) {
        i = 1;
    }
    if (i > b->n) {
        i = b->n;
    }
    *kind = b->kind[i];
    if (b->kind[i] == T_SHELL && b->plate[i] > 0) {
        b->plate[i]--;
        b->hurt = 0.7f;
        return BITE_STOP;
    }
    if (b->kind[i] == T_SPINE) {
        b->hurt = 0.8f;
        return BITE_SPINE;
    }
    for (int j = i; j < b->n; j++) {
        b->x[j] = b->x[j + 1];
        b->y[j] = b->y[j + 1];
        b->kind[j] = b->kind[j + 1];
        b->plate[j] = b->plate[j + 1];
        b->grow[j] = b->grow[j + 1];
    }
    b->n--;
    body_derive(b);
    b->hurt = 1.3f;
    return BITE_TORN;
}
// the nearest segment (1..n) to a point, and its distance (the head when the body is only the head)
static int body_near(const body_t *b, float x, float y, float *dist)
{
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i <= b->n; i++) {
        float d = sqr(wrapd(b->x[i] - x)) + sqr(b->y[i] - y);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    *dist = sqrtf(bd);
    return best;
}
static bool body_dash(body_t *b)
{
    if (b->dash_cd > 0 || b->stun > 0 || b->dead > 0) {
        return false;
    }
    b->dash_t = 0.35f;
    b->dash_cd = b->dash_cool;
    return true;
}
// a swim step: (dx, dy) is the wanted way (a unit vector), m the thrust 0..1
static void body_move(body_t *b, float dt, float dx, float dy, float m)
{
    if (b->dead > 0) {
        m = 0;
        b->dead += dt;
    }
    if (b->stun > 0) {
        m *= 0.15f;
        b->stun -= dt;
    }
    if (m > 0.02f) {
        float want = atan2f(dy, dx), da = angd(want - b->a);
        float tr = b->turn * (1.35f - 0.55f * clampf(b->sp / b->vmax, 0, 1));
        b->a = angd(b->a + clampf(da, -tr * dt, tr * dt));
    }
    float target = m * b->vmax, k = target > b->sp ? b->acc : 1.6f;
    b->sp += (target - b->sp) * clampf(k * dt, 0, 1);
    float ca = cosf(b->a), sa = sinf(b->a), dv = 0;
    if (b->dash_t > 0) {
        dv = b->dash_v * (b->dash_t / 0.35f);
        b->dash_t -= dt;
    }
    b->vx = ca * (b->sp + dv) + b->kx + Z->flow * 0.6f;
    b->vy = sa * (b->sp + dv) + b->ky + b->sink * (1 - m) + (b->dead > 0 ? 9 : 0);
    float kk = expf(-3 * dt);
    b->kx *= kk;
    b->ky *= kk;
    b->x[0] = wrapx(b->x[0] + b->vx * dt);
    b->y[0] += b->vy * dt;
    // the limits of the zone: the surface of the shallows, the top and the bottom (a soft push back; the bottom is
    // the floor of the abyss, or the deep water that is too deep until the current opens)
    float top = 6, bot = Z->h - 8;
    if (Z->far & FAR_VENTS) {
        bot = floor_y(b->x[0]) - seg_r(b, 0) - 2;
    }
    if (b->y[0] < top) {
        b->y[0] = top;
        if (b->ky < 0) {
            b->ky = 0;
        }
        if (b->vy < 0) {
            b->a = angd(b->a + (b->a < 0 ? 0.6f : -0.6f) * dt * 4);
        }
    }
    if (b->y[0] > bot) {
        b->y[0] = bot;
        if (b->ky > 0) {
            b->ky = 0;
        }
    }
    // the chain: each point keeps its distance to the point before it (the look page)
    for (int i = 1; i <= b->n; i++) {
        float ddx = wrapd(b->x[i] - b->x[i - 1]), ddy = b->y[i] - b->y[i - 1], d = sqrtf(ddx * ddx + ddy * ddy);
        if (d < 1e-4f) {
            ddx = -ca;
            ddy = -sa;
            d = 1;
        }
        float s = seg_space(b, i);
        b->x[i] = wrapx(b->x[i - 1] + ddx / d * s);
        b->y[i] = b->y[i - 1] + ddy / d * s;
        if (b->grow[i] < 1) {
            b->grow[i] = fminf(1, b->grow[i] + dt / 0.9f);
        }
    }
    if (b->head_grow < 1) {
        b->head_grow = fminf(1, b->head_grow + dt / 0.9f);
    }
    b->wave += dt * (3.0f + 6.5f * clampf((b->sp + dv) / b->vmax, 0, 1.6f));
    if (b->dash_cd > 0) {
        b->dash_cd -= dt;
    }
    if (b->hurt > 0) {
        b->hurt -= dt;
    }
    if (b->boot_cd > 0) {
        b->boot_cd -= dt;
    }
    if (b->pulse < b->n + 3) {
        b->pulse += dt * 16;
    }
    // a new part opens the body slowly: the length changes, so the physics follow it
    bool growing = b->head_grow < 1;
    for (int i = 1; i <= b->n && !growing; i++) {
        growing = b->grow[i] < 1;
    }
    if (growing) {
        body_derive(b);
    }
}

// ================================================================ the creature on the screen (the look page)
static const float CY[3] = {99, 234, 255}, WH[3] = {232, 251, 255}, PKc[3] = {255, 95, 200}, VIc[3] = {183, 124, 255};
static const float RV_BODY[3] = {255, 150, 96}, RV_RIM[3] = {255, 226, 190}, RV_GUT[3] = {255, 60, 92};
typedef struct {
    float x, y, ux, uy, nx, ny, r;
} bpt_t;
static bpt_t s_bp[MAXSEG + 1];
static void body_points(const body_t *b, float swim)
{
    int n = b->n;
    for (int i = 0; i <= n; i++) {
        int q = i < n ? i + 1 : n, o = i > 0 ? i - 1 : 0;
        float dx = wrapd(b->x[o] - b->x[q]), dy = b->y[o] - b->y[q], d = sqrtf(dx * dx + dy * dy);
        float ux = d > 1e-4f ? dx / d : cosf(b->a), uy = d > 1e-4f ? dy / d : sinf(b->a);
        if (n == 0) {
            ux = cosf(b->a);
            uy = sinf(b->a);
        }
        float nx = -uy, ny = ux;
        float wave = sinf(b->wave - i * 0.62f) * 2.6f * b->S * (n > 0 ? (float)i / n : 0) * swim;
        bpt_t *p = &s_bp[i];
        p->x = sx_of(b->x[i]) + nx * wave * C.z;
        p->y = sy_of(b->y[i]) + ny * wave * C.z;
        p->ux = ux;
        p->uy = uy;
        p->nx = nx;
        p->ny = ny;
        p->r = seg_r(b, i) * C.z * (i == 0 ? 1 : 0.35f + 0.65f * b->grow[i]);
    }
}
// light (a colour 0..255 x alpha) of the body: the player is cyan, the rival is warm
static void body_col(const body_t *b, int which, float *c)
{
    const float *s = b->scheme ? (which == 0   ? RV_BODY
                                  : which == 1 ? RV_RIM
                                               : RV_GUT)
                               : (which == 0   ? CY
                                  : which == 1 ? WH
                                               : PKc);
    c[0] = s[0];
    c[1] = s[1];
    c[2] = s[2];
}
static void fin_pair(const bpt_t *p, float L, float fl, int cr, int cg, int cb, float k)
{
    for (int sd = -1; sd <= 1; sd += 2) {
        float bx = p->x + p->nx * sd * p->r * 0.8f, by = p->y + p->ny * sd * p->r * 0.8f;
        float tx = bx - p->ux * L * 0.8f + p->nx * sd * L * (0.9f + fl * sd * 0.3f),
              ty = by - p->uy * L * 0.8f + p->ny * sd * L * (0.9f + fl * sd * 0.3f);
        float pts[2 * 12];
        pts[0] = bx + p->ux * 3 * C.z;
        pts[1] = by + p->uy * 3 * C.z;
        int m = quad_pts(pts, 1, pts[0], pts[1], tx + p->ux * 6 * C.z, ty + p->uy * 6 * C.z, tx, ty, 6);
        pts[2 * m] = bx - p->ux * 5 * C.z;
        pts[2 * m + 1] = by - p->uy * 5 * C.z;
        m++;
        poly_add(pts, m, cr, cg, cb, (int)(41 * k));
        poly_line_add(pts, m, (int)(170 * 0.6f * k), (int)(255 * 0.6f * k), (int)(240 * 0.6f * k));
        for (int r = 1; r < 4; r++) {
            float ax = bx - p->ux * (r - 2) * 1.6f * C.z, ay = by - p->uy * (r - 2) * 1.6f * C.z;
            line_add(ax, ay, lerpf(bx, tx, 0.9f) + p->ux * (2 - r) * 2 * C.z,
                     lerpf(by, ty, 0.9f) + p->uy * (2 - r) * 2 * C.z, (int)(170 * 0.3f * k), (int)(255 * 0.3f * k),
                     (int)(240 * 0.3f * k));
        }
    }
}
// draw a body. lit: 0..1 (the death puts the lights out from the tail); gl: the grow-in light of a new part
static void body_draw(const body_t *b, float swim, float lit)
{
    if (b->n < 0) {
        return;
    }
    body_points(b, swim);
    int n = b->n;
    float z = C.z, k = clampf(lit, 0, 1);
    float bc[3], rc[3], gc[3];
    body_col(b, 0, bc);
    body_col(b, 1, rc);
    body_col(b, 2, gc);
    bool glass = b->cnt[T_GLASS] > 0;
    float gk = glass ? 0.62f : 1.0f;
    // the tail fluke
    {
        const bpt_t *t = &s_bp[n];
        float fl = sinf(b->wave - n * 0.62f) * 0.4f;
        for (int sd = -1; sd <= 1; sd += 2) {
            float pts[2 * 10];
            pts[0] = t->x;
            pts[1] = t->y;
            float cx = t->x - t->ux * 8 * b->S * z + t->nx * sd * 4 * b->S * z,
                  cy = t->y - t->uy * 8 * b->S * z + t->ny * sd * 4 * b->S * z;
            float ex = t->x - t->ux * 12 * b->S * z + t->nx * sd * (9 + fl * sd * 4) * b->S * z,
                  ey = t->y - t->uy * 12 * b->S * z + t->ny * sd * (9 + fl * sd * 4) * b->S * z;
            int m = quad_pts(pts, 1, t->x, t->y, cx, cy, ex, ey, 5);
            pts[2 * m] = t->x - t->ux * 6 * b->S * z;
            pts[2 * m + 1] = t->y - t->uy * 6 * b->S * z;
            m++;
            poly_add(pts, m, (int)(bc[0] * 0.4f + 40), (int)bc[1], (int)bc[2], (int)(46 * k * gk));
            poly_line_add(pts, m, (int)(rc[0] * 0.5f * 0.62f * k), (int)(250 * 0.5f * k), (int)(255 * 0.5f * k));
        }
    }
    // the fins (FIN segments), the stingers (tentacles that trail behind the STING segments)
    for (int i = 1; i <= n; i++) {
        const bpt_t *p = &s_bp[i];
        float gi = b->grow[i] * k;
        if (b->kind[i] == T_FIN) {
            fin_pair(p, 13 * b->S * z * b->grow[i], sinf(b->wave - i * 0.62f + 1) * 0.5f, 110, 255, 230, gi);
        }
        if (b->kind[i] == T_STING) {
            for (int t = -1; t <= 1; t++) {
                float x0 = p->x + p->nx * t * p->r * 0.5f, y0 = p->y + p->ny * t * p->r * 0.5f;
                float L = (16 + 5 * (t == 0)) * b->S * z * b->grow[i];
                float px0 = x0, py0 = y0;
                for (int j = 1; j <= 7; j++) {
                    float f = j / 7.0f, w = sinf(b->wave * 0.7f - j * 0.8f + t * 1.7f) * 2.2f * b->S * z * f;
                    float px = x0 - p->ux * L * f + p->nx * w, py = y0 - p->uy * L * f + p->ny * w;
                    line_add(px0, py0, px, py, (int)(255 * 0.45f * gi), (int)(95 * 0.45f * gi),
                             (int)(200 * 0.45f * gi));
                    px0 = px;
                    py0 = py;
                }
                dot_add(px0, py0, (int)(255 * gi), (int)(140 * gi), (int)(220 * gi));
            }
        }
    }
    // the segments: clear membranes with a soft rim, an organ inside in the colour of its trait, lights on the side
    float pulse = b->pulse;
    for (int i = n; i >= 0; i--) {
        const bpt_t *p = &s_bp[i];
        float r = p->r;
        if (r < 0.4f) {
            continue;
        }
        float hot = fmaxf(0, 1 - fabsf(i - pulse) / 1.6f);
        // (the death: the lights go out from the tail to the head)
        float li = k;
        if (b->dead > 0) {
            li = clampf(1 - (b->dead * 1.6f - (n - i) * 0.12f), 0, 1) * k;
        }
        float a0 = (0.06f + hot * 0.3f) * gk, a1 = (0.14f + hot * 0.3f) * gk, a2 = 0.42f + hot * 0.4f;
        float c2[3] = {lerpf(bc[0], WH[0], 0.4f), lerpf(bc[1], WH[1], 0.4f), lerpf(bc[2], WH[2], 0.4f)};
        if (glass) {
            c2[0] = 210;
            c2[1] = 232;
            c2[2] = 255;
        }
        lut_t L[LUT_N];
        float hb[3] = {lerpf(bc[0], b->pulse_c[0], hot), lerpf(bc[1], b->pulse_c[1], hot),
                       lerpf(bc[2], b->pulse_c[2], hot)};
        lut_3(L, hb, a0 * li, 0.72f, hb, a1 * li, c2, a2 * li, r);
        ell_lut(p->x, p->y, p->ux, p->uy, r * 1.32f, r, L, true);
        // the rim on the front half and the inner line on the back half
        int arc = (int)(r * 2.2f) + 4;
        float rimk = (0.35f + hot * 0.5f) * li;
        for (int j = 0; j <= arc; j++) {
            float an = -1.95f + 3.9f * j / arc, cu = cosf(an) * r * 1.32f, sv = sinf(an) * r;
            dot_add(p->x + p->ux * cu + p->nx * sv, p->y + p->uy * cu + p->ny * sv,
                    (int)(lerpf(bc[0], WH[0], 0.5f) * rimk), (int)(lerpf(bc[1], WH[1], 0.5f) * rimk),
                    (int)(lerpf(bc[2], WH[2], 0.5f) * rimk));
        }
        // the organ: the colour of the trait of the segment (a plain segment: the pink to violet gut of the look)
        int t = i == 0 ? T_NONE : b->kind[i];
        float oc[3];
        if (t == T_NONE) {
            float f = n > 0 ? (float)i / n : 0;
            if (b->scheme) {
                oc[0] = gc[0];
                oc[1] = gc[1];
                oc[2] = gc[2];
            } else {
                for (int j = 0; j < 3; j++) {
                    oc[j] = lerpf(PKc[j], VIc[j], f);
                }
            }
        } else {
            for (int j = 0; j < 3; j++) {
                oc[j] = TRAIT[t].c[j];
            }
        }
        if (i > 0) {
            float ga = (0.30f + hot * 0.3f) * li * b->grow[i];
            ell_add(p->x + p->nx * r * 0.12f, p->y + p->ny * r * 0.12f, p->ux, p->uy, r * 0.55f, r * 0.34f, (int)oc[0],
                    (int)oc[1], (int)oc[2], ga);
            if (t != T_NONE) {
                glow(p->x, p->y, r * 0.9f, (int)oc[0], (int)oc[1], (int)oc[2], 0.18f * li * b->grow[i]);
            }
        }
        float blink = 0.5f + 0.5f * sinf(s_time * 6 - i * 0.9f);
        dot_add(p->x + p->ny * 0 + p->nx * r * 0.62f, p->y + p->ny * r * 0.62f,
                (int)(WH[0] * (0.35f + 0.6f * blink) * li), (int)(WH[1] * (0.35f + 0.6f * blink) * li),
                (int)(WH[2] * (0.35f + 0.6f * blink) * li));
        if ((i & 1) == 0) {
            dot_add(p->x - p->nx * r * 0.7f - p->ux * r * 0.4f, p->y - p->ny * r * 0.7f - p->uy * r * 0.4f,
                    (int)(VIc[0] * (0.3f + 0.5f * blink) * li), (int)(VIc[1] * (0.3f + 0.5f * blink) * li),
                    (int)(VIc[2] * (0.3f + 0.5f * blink) * li));
        }
        // the organs that show on the outside
        if (t == T_SPINE && i > 0) {
            float gi = b->grow[i] * li;
            for (int s = -1; s <= 1; s += 2) {
                float L2 = (6 + 2.5f * (s > 0)) * b->S * z * gi;
                float bx = p->x - p->nx * r * 0.9f + p->ux * s * r * 0.4f,
                      by = p->y - p->ny * r * 0.9f + p->uy * s * r * 0.4f;
                float tx = bx - p->nx * L2 - p->ux * L2 * 0.45f, ty = by - p->ny * L2 - p->uy * L2 * 0.45f;
                line_add(bx, by, tx, ty, (int)(210 * 0.7f * gi), (int)(190 * 0.7f * gi), (int)(255 * 0.7f * gi));
                glow(tx, ty, 3.5f * z, 183, 124, 255, 0.75f * gi);
            }
        }
        if (t == T_SHELL && i > 0) {
            float gi = b->grow[i] * li;
            int pl = b->plate[i];
            float arcr = r * 1.18f;
            for (int j = 0; j <= 14; j++) {
                float an = 3.14159f * (0.15f + 0.7f * j / 14.0f);
                float cu = cosf(an) * arcr * 1.2f, sv = -sinf(an) * arcr;
                float x = p->x + p->ux * cu + p->nx * sv, y = p->y + p->uy * cu + p->ny * sv;
                if (pl == 0 && (j % 4 == 1)) {
                    continue;
                }
                glow(x, y, 1.8f * z, 255, 196, 90, (pl > 0 ? 0.55f : 0.25f) * gi);
                if (pl == 1 && j == 7) {
                    line_add(x, y, x + p->nx * 3 * z, y + p->ny * 3 * z, 80, 40, 10);
                }
            }
        }
        if (t == T_JET && i > 0) {
            float gi = b->grow[i] * li;
            float jx = p->x + p->nx * r * 0.9f - p->ux * r * 0.6f, jy = p->y + p->ny * r * 0.9f - p->uy * r * 0.6f;
            glow(jx, jy, 3.4f * z * (1 + (b->dash_t > 0) * 1.4f), 255, 148, 64, 0.8f * gi);
        }
        if (t == T_INK && i > 0) {
            glow(p->x, p->y, r * 0.6f, 255, 75, 110, 0.35f * b->grow[i] * li);
        }
    }
    // the head: the eyes, the jaws (or a small mouth), the lure
    const bpt_t *h = &s_bp[0];
    float hx = h->x, hy = h->y, ux = h->ux, uy = h->uy, nx = h->nx, ny = h->ny, S = b->S * z;
    float lk = b->dead > 0 ? clampf(1 - b->dead * 0.9f, 0, 1) * k : k;
#define HP(u, v, X, Y)                                                                                                 \
    do {                                                                                                               \
        X = hx + ux * (u) * S + nx * (v) * S;                                                                          \
        Y = hy + uy * (u) * S + ny * (v) * S;                                                                          \
    } while (0)
    float ex, ey;
    HP(3.4f, -2.6f, ex, ey);
    ell_add(ex, ey, 1, 0, 1.3f * S, 1.3f * S, 255, 255, 255, 0.95f * lk);
    glow(ex, ey, 5 * S, (int)bc[0], (int)bc[1], (int)bc[2], 0.7f * lk);
    HP(4.6f, -1.0f, ex, ey);
    ell_add(ex, ey, 1, 0, 0.9f * S, 0.9f * S, 255, 255, 255, 0.95f * lk);
    if (b->jaws > 0) {
        float j = (0.6f + 0.4f * fminf(b->jaws, 3) / 3.0f) * b->head_grow;
        float open = 0.25f + 0.25f * fmaxf(0, sinf(s_time * 3)) + (b->dash_t > 0 ? 0.5f : 0);
        for (int sd = -1; sd <= 1; sd += 2) {
            float pts[2 * 12], ax, ay, cx1, cy1, tx, ty, cx2, cy2, bx, by;
            HP(4.5f, sd * 1.4f, ax, ay);
            HP(7 + 6 * j, sd * (5 + open * 9) * (0.7f + 0.3f * j), cx1, cy1);
            HP(6 + 12 * j, sd * (1.2f + open * 4), tx, ty);
            HP(7 + 5 * j, sd * (2.6f + open * 4), cx2, cy2);
            HP(5.5f, sd * 0.2f, bx, by);
            pts[0] = ax;
            pts[1] = ay;
            int m = quad_pts(pts, 1, ax, ay, cx1, cy1, tx, ty, 5);
            m = quad_pts(pts, m, tx, ty, cx2, cy2, bx, by, 4);
            poly_add(pts, m, 225, 240, 255, (int)(205 * lk));
            glow(tx, ty, 3.4f * S, 232, 251, 255, 0.7f * j * lk);
        }
    } else {
        // (a small mouth: an arc in front of the eyes)
        for (int q = 0; q <= 8; q++) {
            float an = -1.0f + 2.0f * q / 8;
            float px = hx + (ux * (5.8f + cosf(an) * 1.6f) + nx * sinf(an) * 1.6f) * S,
                  py = hy + (uy * (5.8f + cosf(an) * 1.6f) + ny * sinf(an) * 1.6f) * S;
            dot_add(px, py, (int)(200 * 0.6f * lk), (int)(250 * 0.6f * lk), (int)(255 * 0.6f * lk));
        }
    }
    if (b->lure > 0) {
        float j = b->head_grow, bob = sinf(s_time * 2.4f) * 2;
        float sx0, sy0, cx1, cy1, lx, ly;
        HP(1, -6, sx0, sy0);
        HP(6 * j, -20 * j + bob, cx1, cy1);
        HP(16 * j, -12 * j + bob, lx, ly);
        float px0 = sx0, py0 = sy0;
        for (int q = 1; q <= 8; q++) {
            float t = q / 8.0f, u = 1 - t;
            float px = u * u * sx0 + 2 * u * t * cx1 + t * t * lx, py = u * u * sy0 + 2 * u * t * cy1 + t * t * ly;
            line_add(px0, py0, px, py, (int)(200 * 0.6f * lk), (int)(240 * 0.6f * lk), (int)(255 * 0.6f * lk));
            px0 = px;
            py0 = py;
        }
        float lp = 0.75f + 0.25f * sinf(s_time * 5);
        const uint8_t *lc = TRAIT[T_LURE].c;
        glow(lx, ly, 22 * j * lp * S, lc[0], lc[1], lc[2], 0.5f * j * lk);
        glow(lx, ly, 6 * j * S, 232, 251, 255, 0.95f * j * lk);
    }
#undef HP
}
