// GUMMY SIEGE: the effects (included by app_gummy.c). The MP3 sounds of tfsfx (the strength of each hit comes from
// the events of tfphys), the particles of the looks (no physics: the trail, the dust, the stars of a knock-out, the
// confetti, the drops), the points that float up, the salsa on the cloth, and the camera.

// ================================================================ the sounds (the data pack: /sdcard/gummy/sfx/)
enum {
    FX_WHOOSH,
    FX_GUMMY,
    FX_GUMMY_BIG,
    FX_CRUNCH,
    FX_CRUNCH2,
    FX_SNAP,
    FX_WOOD,
    FX_CHIPHIT,
    FX_GLASS,
    FX_SHATTER,
    FX_SPLASH,
    FX_CAN,
    FX_POP,
    FX_KO,
    FX_SPLIT,
    FX_DASH,
    FX_STICK,
    FX_BALLOON,
    FX_DIP,
    FX_STRETCH,
    FX_WIN,
    FX_LOSE,
    FX_STAR,
    FX_CLICK,
    FX_N
};
static const char *const FX_FILE[FX_N] = {
        "whoosh", "gummy",   "gummy_big", "crunch",  "crunch2", "snap", "wood",  "chiphit",
        "glass",  "shatter", "splash",    "can",     "pop",     "ko",   "split", "dash",
        "stick",  "balloon", "dip",       "stretch", "win",     "lose", "star",  "click",
};
static bool s_sound = true;  // the effects (the panel item SOUND)
static int s_fx_step;        // the hit sounds of this step (at most 4)
static void snd(int id, int vol, float var)
{
    if (!s_sound || vol <= 0) {
        return;
    }
    tfsfx_play_var(id, clampi(vol, 1, 100), var);
}

// ================================================================ the particles of the looks
enum {
    FP_SPARK,
    FP_PUFF,
    FP_STAR,
    FP_CONFETTI,
    FP_DROP,
    FP_HELMET,
    FP_RING,
    FP_GHOST,
    FP_CRUMB,
    FP_STREAK
};
typedef struct {
    float x, y, vx, vy;  // m, m/s (the world)
    float life, max;     // s
    float size, a, va;   // the size (m), the angle and its speed
    uint16_t col;
    uint8_t kind, var;
} fxp_t;
#define FXP_N 220
static fxp_t s_fxp[FXP_N];
static int s_nfxp;
static fxp_t *fxp_add(int kind, float x, float y, float vx, float vy, float life, float size, uint16_t col)
{
    static int rr;
    // (full: a particle goes, one slot after the other; no move of the pool)
    fxp_t *p = s_nfxp < FXP_N ? &s_fxp[s_nfxp++] : &s_fxp[rr++ % FXP_N];
    memset(p, 0, sizeof *p);
    p->kind = (uint8_t)kind;
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->life = 0;
    p->max = life;
    p->size = size;
    p->col = col;
    return p;
}
// the points that float up (the HUD draws them at their world point)
typedef struct {
    float x, y, t;
    int pts;
    uint8_t kind;
} pop_t;
#define POP_N 12
static pop_t s_pop[POP_N];
static int s_npop;
// the salsa (and the cheese) on the cloth
typedef struct {
    float x, w, t;
    uint8_t kind;
} decal_t;
#define DECAL_N 10
static decal_t s_decal[DECAL_N];
static int s_ndecal;
// the ghost of a bear that goes (a squash and a pop)
static float s_ghost_x, s_ghost_y, s_ghost_t = -1;
static int s_ghost_k;
static float s_bear_sq, s_bear_sqv;  // the squash of the bear in the air (a damped spring)
static float s_flash;                // a white flash of a big hit (0..1)

static void fx_score(float x, float y, int pts, int kind)
{
    for (int i = 0; i < s_npop; i++) {
        pop_t *q = &s_pop[i];
        if (q->t < 0.6f && fabsf(q->x - x) < 1.3f && fabsf(q->y - y) < 1.0f) {
            q->pts += pts;
            if (kind > q->kind) {
                q->kind = (uint8_t)kind;
            }
            q->t = minf(q->t, 0.1f);
            return;
        }
    }
    if (s_npop >= POP_N) {
        memmove(s_pop, s_pop + 1, sizeof(pop_t) * (POP_N - 1));
        s_npop = POP_N - 1;
    }
    pop_t *p = &s_pop[s_npop++];
    p->x = x;
    p->y = y;
    p->t = 0;
    p->pts = pts;
    p->kind = (uint8_t)kind;
}
static uint16_t chip_col(void)
{
    static const uint8_t C[5][3] = {{242, 184, 74}, {255, 217, 138}, {217, 69, 26}, {232, 150, 50}, {200, 120, 40}};
    const uint8_t *c = C[rnd32() % 5];
    return rgbc(c[0], c[1], c[2]);
}
static void fx_break(const tp_event_t *e)
{
    const thing_t *T = thing_of(e->user);
    float v = clampf(e->value, 2, 20);
    switch (T->kind) {
    case OK_JAR:
        snd(FX_SHATTER, 70 + (int)(v * 2), 0.06f);
        break;
    case OK_STICK:
        snd(FX_SNAP, 50 + (int)(v * 3), 0.1f);
        break;
    case OK_GIFT:
        snd(FX_WOOD, 55 + (int)(v * 3), 0.1f);
        snd(FX_CRUNCH2, 40, 0.15f);
        break;
    default:
        snd((rnd32() & 1) ? FX_CRUNCH : FX_CRUNCH2, 50 + (int)(v * 3), 0.12f);
        break;
    }
    // dust and crumbs of the looks round the point of the break
    int n = T->kind == OK_JAR ? 2 : 4;
    for (int i = 0; i < n; i++) {
        float a = rndf() * 6.2832f, sp = 0.5f + rndf() * 1.6f;
        fxp_add(FP_PUFF, e->p.x + rnds() * 0.1f, e->p.y + rnds() * 0.1f, cosf(a) * sp, sinf(a) * sp + 0.4f,
                0.6f + rndf() * 0.5f, 0.07f + rndf() * 0.06f, rgbc(240, 150, 70));
    }
    for (int i = 0; i < 8; i++) {
        float a = rndf() * 6.2832f, sp = 1.5f + rndf() * 4.0f;
        fxp_t *p = fxp_add(FP_CRUMB, e->p.x, e->p.y, cosf(a) * sp, sinf(a) * sp + 1.5f, 0.8f + rndf() * 0.9f,
                           0.02f + rndf() * 0.025f, chip_col());
        p->va = rnds() * 12;
    }
    s_shake = maxf(s_shake, minf(5.0f, 1.2f + v * 0.25f));
}
static void fx_ko(int ti, float x, float y)
{
    const thing_t *T = thing_of(ti);
    snd(FX_KO, 85, 0.08f);
    snd(FX_POP, 70, 0.05f);
    // the helmet flies off, stars go round, a puff of crumbs
    fxp_t *h =
            fxp_add(FP_HELMET, x, y + 0.2f, rnds() * 1.5f, 4.5f + rndf() * 1.5f, 1.6f, ENEMY[T->kind - OK_KNIGHT].s, 0);
    h->va = rnds() * 9;
    h->var = (uint8_t)(T->kind - OK_KNIGHT);
    for (int i = 0; i < 6; i++) {
        float a = i * 1.0472f;
        fxp_add(FP_STAR, x + cosf(a) * 0.25f, y + 0.3f + sinf(a) * 0.12f, cosf(a) * 1.2f, 1.2f + sinf(a) * 0.6f, 0.9f,
                0.07f, rgbc(255, 236, 120));
    }
    for (int i = 0; i < 6; i++) {
        float a = rndf() * 6.2832f, sp = 0.8f + rndf() * 2.2f;
        fxp_add(FP_PUFF, x, y, cosf(a) * sp, sinf(a) * sp + 0.6f, 0.5f + rndf() * 0.5f, 0.07f + rndf() * 0.08f,
                rgbc(255, 196, 90));
    }
    fxp_add(FP_RING, x, y, 0, 0, 0.45f, 0.9f, rgbc(255, 230, 160));
    s_shake = maxf(s_shake, 4.0f);
    s_flash = maxf(s_flash, 0.25f);
}
static void fx_hit(const tp_hit_t *h, float strength)
{
    if (strength < 1.6f || s_fx_step >= 4) {
        return;
    }
    int vol = clampi((int)((strength - 1.0f) * 9.0f), 8, 100);
    for (int sd = 0; sd < 2; sd++) {
        int sc = sd ? h->snd_b : h->snd_a;
        switch (sc) {
        case TP_SND_GUMMY:
            snd(strength > 9 ? FX_GUMMY_BIG : FX_GUMMY, vol, 0.12f);
            break;
        case TP_SND_CHIP:
            snd(FX_CHIPHIT, vol * 4 / 5, 0.15f);
            break;
        case TP_SND_WOOD:
            if (h->mat_a != M_TABLE && h->mat_b != M_TABLE) {
                snd(FX_WOOD, vol * 3 / 4, 0.12f);
            }
            break;
        case TP_SND_GLASS:
            snd(FX_GLASS, vol, 0.1f);
            break;
        case TP_SND_METAL:
            snd(FX_CAN, vol, 0.08f);
            break;
        default:
            break;
        }
    }
    s_fx_step++;
    // a big hit of the bear: the screen shakes, the jelly squashes, a ring
    bool bear = (h->mat_a == M_HEAVY || h->mat_a == M_GUMMY || h->mat_a == M_BOUNCY || h->mat_a == M_STICKY ||
                 h->mat_a == M_LIGHT || h->mat_b == M_HEAVY || h->mat_b == M_GUMMY || h->mat_b == M_BOUNCY ||
                 h->mat_b == M_STICKY || h->mat_b == M_LIGHT);
    if (bear && strength > 4) {
        s_shake = maxf(s_shake, minf(7.0f, strength * 0.45f));
        s_bear_sq = maxf(s_bear_sq, minf(1.0f, strength * 0.07f));
        if (strength > 10) {
            fxp_add(FP_RING, h->p.x, h->p.y, 0, 0, 0.35f, 0.6f, rgbc(255, 255, 255));
        }
    }
}
static void fx_power(int kind, float x, float y)
{
    switch (kind) {
    case BK_RED:
        snd(FX_DASH, 80, 0.05f);
        break;
    case BK_GREEN:
        snd(FX_SPLIT, 85, 0.05f);
        break;
    case BK_YELLOW:
        snd(FX_DASH, 90, 0.05f);
        break;
    case BK_ORANGE:
        if (s_sound) {
            sfx_tone(220, 660, 180, W_SINE, 70);
        }
        snd(FX_GUMMY, 60, 0.1f);
        break;
    case BK_CLEAR:
        snd(FX_WHOOSH, 85, 0.05f);
        break;
    default:
        break;
    }
    for (int i = 0; i < 10; i++) {
        float a = rndf() * 6.2832f, sp = 1 + rndf() * 2;
        fxp_add(FP_SPARK, x, y, cosf(a) * sp, sinf(a) * sp, 0.5f + rndf() * 0.3f, 0.05f, rgbc(255, 250, 220));
    }
    fxp_add(FP_RING, x, y, 0, 0, 0.3f, 0.7f, rgbc(BEAR[kind].lt[0], BEAR[kind].lt[1], BEAR[kind].lt[2]));
    s_bear_sq = maxf(s_bear_sq, 0.6f);
}
static void fx_splat(float x, float y)
{
    snd(FX_SPLASH, 90, 0.06f);
    if (s_ndecal >= DECAL_N) {
        memmove(s_decal, s_decal + 1, sizeof(decal_t) * (DECAL_N - 1));
        s_ndecal = DECAL_N - 1;
    }
    decal_t *d = &s_decal[s_ndecal++];
    d->x = x;
    d->w = 0.45f + rndf() * 0.25f;
    d->t = 0;
    d->kind = 0;
    for (int i = 0; i < 26; i++) {
        float a = 0.3f + rndf() * 2.5f, sp = 1.5f + rndf() * 4.0f;
        static const uint8_t C[3][3] = {{214, 31, 31}, {63, 154, 42}, {243, 231, 200}};
        const uint8_t *c = C[i % 7 == 0 ? 1 : i % 9 == 0 ? 2 : 0];
        fxp_add(FP_DROP, x, y, cosf(a) * sp, sinf(a) * sp, 0.6f + rndf() * 0.8f, 0.03f + rndf() * 0.03f,
                rgbc(c[0], c[1], c[2]));
    }
    s_shake = maxf(s_shake, 5.0f);
}
static void fx_launch(void)
{
    snd(FX_WHOOSH, 85, 0.06f);
}
static void fx_bear_pop(float x, float y, int kind)
{
    s_ghost_x = x;
    s_ghost_y = y;
    s_ghost_t = 0;
    s_ghost_k = kind < 0 ? 0 : kind;
    snd(FX_POP, 45, 0.1f);
}
static void fx_balloon(int ti, float x, float y)
{
    (void)ti;
    snd(FX_BALLOON, 95, 0.06f);
    for (int i = 0; i < 14; i++) {
        float a = rndf() * 6.2832f, sp = 1 + rndf() * 3;
        fxp_t *p = fxp_add(FP_CONFETTI, x, y, cosf(a) * sp, sinf(a) * sp + 1, 1.2f + rndf(), 0.05f,
                           hsv((int)(rndf() * 360), 200, 255));
        p->va = rnds() * 10;
    }
    fxp_add(FP_RING, x, y, 0, 0, 0.25f, 0.6f, rgbc(255, 255, 255));
}
static void fx_dip(float x, float y)
{
    snd(FX_DIP, 75, 0.1f);
    for (int i = 0; i < 8; i++) {
        float a = 0.6f + rndf() * 1.9f, sp = 1 + rndf() * 2;
        fxp_add(FP_DROP, x, y, cosf(a) * sp, sinf(a) * sp, 0.5f + rndf() * 0.4f, 0.04f, rgbc(255, 176, 30));
    }
}
static void fx_stick(float x, float y)
{
    snd(FX_STICK, 85, 0.05f);
    fxp_add(FP_RING, x, y, 0, 0, 0.3f, 0.45f, rgbc(255, 255, 255));
}

// the step of the particles of the looks (dt: the game time)
static void fx_step(float dt)
{
    int k = 0;
    for (int i = 0; i < s_nfxp; i++) {
        fxp_t *p = &s_fxp[i];
        p->life += dt;
        if (p->life >= p->max) {
            continue;
        }
        float g = 0, drag = 0;
        switch (p->kind) {
        case FP_CRUMB:
        case FP_DROP:
        case FP_HELMET:
            g = 9.8f;
            drag = 0.2f;
            break;
        case FP_CONFETTI:
            g = 2.5f;
            drag = 2.2f;
            break;
        case FP_PUFF:
            g = -0.3f;
            drag = 2.6f;
            break;
        case FP_STAR:
            g = 0.6f;
            drag = 1.5f;
            break;
        case FP_SPARK:
            g = 1.0f;
            drag = 1.8f;
            break;
        default:
            break;
        }
        p->vy -= g * dt;
        float f = 1.0f - drag * dt;
        if (f < 0) {
            f = 0;
        }
        p->vx *= f;
        p->vy *= f;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->a += p->va * dt;
        if ((p->kind == FP_CRUMB || p->kind == FP_DROP || p->kind == FP_CONFETTI) && p->y < 0.01f && p->vy < 0) {
            p->y = 0.01f;
            p->vy *= -0.2f;
            p->vx *= 0.4f;
        }
        s_fxp[k++] = *p;
    }
    s_nfxp = k;
    k = 0;
    for (int i = 0; i < s_npop; i++) {
        s_pop[i].t += dt;
        if (s_pop[i].t < 1.4f) {
            s_pop[k++] = s_pop[i];
        }
    }
    s_npop = k;
    for (int i = 0; i < s_ndecal; i++) {
        s_decal[i].t += dt;
    }
    if (s_ghost_t >= 0) {
        s_ghost_t += dt;
        if (s_ghost_t > 0.45f) {
            s_ghost_t = -1;
        }
    }
    s_bear_sqv += (-s_bear_sq * 260.0f - s_bear_sqv * 9.0f) * dt;
    s_bear_sq += s_bear_sqv * dt;
    s_flash = maxf(0, s_flash - dt * 2.5f);
}

// ================================================================ the camera
// The camera: the world point at the middle of the screen and the zoom (px for one metre = PPM x zoom). The targets
// move with a soft follow; the shake is a noise in px.
typedef struct {
    float x, y, z;
} view_t;
static view_t s_cam = {6, 1.5f, 1};
static float s_lift;  // the camera over the table (px): the table line, the cloth and the far picture move
                      // down by it (a zoom on a bear high in the air); 0 = the table line at s_tsy
#define SL_ROOM  (GS_BOX ? 0.7f : 2.2f)  // the room at the left of the sling in the aim view (m)
#define TABLE_SY s_tsy                   // the line of the table on the screen (px; frame_geom)
#define ZMIN     s_zmin
#define ZMAX     (GS_RND ? 2.60f * GS_RS : 2.60f)
// the view that shows the box x0..x1 (m), from the table to the height top (m)
static view_t view_fit(float x0, float x1, float top, float zmax)
{
    view_t v;
    float w = maxf(1.0f, x1 - x0), h = maxf(1.0f, top);
    float z = minf((float)SW / (w * PPM), (TABLE_SY - 18.0f) / (h * PPM));
    if (GS_RND) {
        z = minf(SW * 0.80f / (w * PPM), (TABLE_SY - SH * 0.16f) / (h * PPM));  // (the part of the circle)
    }
    v.z = clampf(z, ZMIN, zmax);
    v.x = (x0 + x1) * 0.5f;
    v.y = (TABLE_SY - SH * 0.5f) / (PPM * v.z);
    return v;
}
static inline float cam_sx(float x)
{
    return (float)SW * 0.5f + (x - s_cam.x) * PPM * s_cam.z;
}
static inline float cam_sy(float y)
{
    return (float)SH * 0.5f - (y - s_cam.y) * PPM * s_cam.z;
}
static inline float cam_ppm(void)
{
    return PPM * s_cam.z;
}
// the box of the fort: all the things of the level that are not static (and the static blocks too)
static float s_fort_x0, s_fort_x1, s_fort_top;
static void fort_box(void)
{
    float x0 = 1e9f, x1 = -1e9f, top = 1.0f;
    for (int i = 0; i < L->n && i < THING_MAX; i++) {
        const thing_t *T = thing_of(i);
        float xa = T->x - 0.5f, xb = T->x + 0.5f, yt = T->y + 0.5f;
        if (T->kind == OK_BLOCK || T->kind == OK_DIP) {
            xa = T->x;
            xb = T->x + T->w;
            yt = T->y + T->h;
        }
        x0 = minf(x0, xa);
        x1 = maxf(x1, xb);
        top = maxf(top, yt);
    }
    if (x0 > x1) {
        x0 = 8;
        x1 = 12;
    }
    s_fort_x0 = x0;
    s_fort_x1 = x1;
    s_fort_top = top;
}
// the view of the aim: the sling at the left, and the fort when it fits (the BOX: less room at the left of the sling)
static view_t view_aim(void)
{
    if (GS_RND) {
        // the round layout: the sling at RND_SLX of the frame, the fort to RND_XR (the finger pulls in the circle at the
        // left of the sling and under it)
        float w = maxf(1.0f, s_fort_x1 + 0.8f - SL_X), h = maxf(s_fort_top + 0.6f, 3.0f);
        float z = minf(SW * (RND_XR - RND_SLX) / (w * PPM), (TABLE_SY - SH * 0.16f) / (h * PPM));
        view_t v;
        v.z = clampf(z, ZMIN, 1.15f * GS_RS);
        v.x = SL_X + SW * (0.5f - RND_SLX) / (PPM * v.z);
        v.y = (TABLE_SY - SH * 0.5f) / (PPM * v.z);
        return v;
    }
    float l = SL_X - SL_ROOM;
    view_t v = view_fit(l, s_fort_x1 + 0.8f, maxf(s_fort_top + 0.6f, 3.0f), 1.15f);
    if (v.z <= ZMIN + 0.001f) {
        v.x = l + (float)SW * 0.5f / (PPM * v.z);  // (the sling stays in the frame)
    }
    return v;
}
