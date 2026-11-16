// GLASSHOUSE: the scene of a level. The build (the text, the physics, the painted layers and the pictures), the sync of
// the world layer with the bodies (a part that moves goes out of the layer, a part that rests goes in), the live
// drawing (the parts that move, the pieces, the ropes, the water, the shards, the cracks that grow) and the handles of
// the moves. Included by app_glass.c.

// ---------------------------------------------------------------- the memory of the picture
static void *gl_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) {
        memset(p, 0, n);
    }
    return p;
}
static void view_free(void)
{
    heap_caps_free(s_sky);
    heap_caps_free(s_bare);
    heap_caps_free(s_l2);
    heap_caps_free(s_l1);
    heap_caps_free(s_l05);
    heap_caps_free(s_pool);
    heap_caps_free(s_dec);
    heap_caps_free(s_sh);
    heap_caps_free(s_bare_row);
    s_sky = s_l2 = s_l1 = s_l05 = NULL;
    s_bare = NULL;
    s_bare_row = NULL;
    s_pool = NULL;
    s_dec = NULL;
    s_sh = NULL;
}
static bool view_alloc(void)
{
    if (s_sky) {
        return true;
    }
    s_sky = (uint16_t *)gl_alloc((size_t)WW * WH * 2);
    s_bare_row = (uint32_t *)gl_alloc(sizeof(uint32_t) * (H2 + 1));
    s_l2 = (uint16_t *)gl_alloc((size_t)W2 * H2 * 2);
    s_l1 = (uint16_t *)gl_alloc((size_t)WW * WH * 2);
    s_l05 = (uint16_t *)gl_alloc((size_t)WQ * HQ * 2);
    s_pool = (uint8_t *)gl_alloc(POOL_BYTES);
    s_dec = (decal_t *)gl_alloc(sizeof(decal_t) * MAXDECAL);
    s_sh = (shard_t *)gl_alloc(sizeof(shard_t) * MAXSHARD);
    if (!s_sky || !s_bare_row || !s_l2 || !s_l1 || !s_l05 || !s_pool || !s_dec || !s_sh) {
        view_free();
        return false;
    }
    return true;
}
static float floor_at(float x)
{
    (void)x;
    return FLOOR;
}
static int s_npud, s_pud_n;  // the puddles on the stand, and the drops that landed
static void puddles(void)
{
    for (int i = 0; i < W->npart && s_npud < 70; i++) {
        const tp_part_t *q = &W->part[i];
        if (q->kind != TP_PART_WATER || !q->touch) {
            continue;
        }
        float x = px_x(q->p.x), y = px_y(q->p.y);
        if (y < FLOOR - 4 || x < STAND_X0 || x > STAND_X1) {
            continue;
        }
        if (++s_pud_n % 6) {
            continue;
        }
        float w = 2.5f + (float)(s_pud_n % 5) * 0.8f;
        float p[12] = {x - w, FLOOR - 0.2f, x - w * 0.5f, FLOOR - 0.9f, x + w * 0.5f, FLOOR - 0.9f,
                       x + w, FLOOR - 0.2f, x + w * 0.5f, FLOOR + 0.5f, x - w * 0.5f, FLOOR + 0.5f};
        decal_add(p, 6, C3(150, 205, 232), C3(235, 250, 255));
        s_npud++;
    }
}

// ---------------------------------------------------------------- the smooth motion (as GUMMY SIEGE)
// The physics has steps of 1/60 s; a frame shows 0 to 3 of them (fewer in the slow motion). Each frame draws a body
// between its place before the last step and its place now, by the time that waits for the next step.
typedef struct {
    tp_xf_t xf;
    float mass;
    uint8_t ok;
} prev_t;
static prev_t *s_prev;
static void interp_keep(void)
{
    if (!s_prev || !W) {
        return;
    }
    for (int i = 0; i < W->body_cap; i++) {
        const tp_body_t *b = &W->body[i];
        s_prev[i].ok = i < W->body_hi && (b->flags & TP_ALIVE) && b->kind == TP_DYNAMIC;
        s_prev[i].xf = b->xf;
        s_prev[i].mass = b->mass;
    }
}
static float s_interp = 1;  // 0..1: the part of the next step that the frame shows
static tp_xf_t body_xf(const tp_body_t *b)
{
    int i = (int)(b - W->body);
    if (!s_prev || i < 0 || i >= W->body_cap || !s_prev[i].ok || s_prev[i].mass != b->mass) {
        return b->xf;
    }
    const tp_xf_t *P = &s_prev[i].xf;
    float dx = b->xf.p.x - P->p.x, dy = b->xf.p.y - P->p.y;
    if (dx * dx + dy * dy > 1.0f) {
        return b->xf;
    }
    float t = s_interp;
    tp_xf_t r;
    r.p.x = P->p.x + dx * t;
    r.p.y = P->p.y + dy * t;
    float c = P->q.c + (b->xf.q.c - P->q.c) * t, s = P->q.s + (b->xf.q.s - P->q.s) * t, l = sqrtf(c * c + s * s);
    r.q.c = l > 0.5f ? c / l : b->xf.q.c;
    r.q.s = l > 0.5f ? s / l : b->xf.q.s;
    return r;
}
static void xf_px(const tp_xf_t *xf, float *x, float *y, float *a)
{
    *x = px_x(xf->p.x);
    *y = px_y(xf->p.y);
    *a = -atan2f(xf->q.s, xf->q.c);
}

// ---------------------------------------------------------------- the build of the picture of a level
static uint32_t s_sig[MAXP + 2];  // the state that the picture of the object shows (its hurt)
static uint32_t obj_sig(void)
{
    return G.o.hurt < 0.35f ? 0 : 1 + (uint32_t)(G.o.hurt * 8);
}
// the body of a picture: a part, the object or its stand
static tp_body_t *pic_body(int i)
{
    if (i < MAXP) {
        return part_body(i);
    }
    return tp_body(W, i == SPR_OBJ ? G.o.id : G.o.stand);
}
static int pic_z(int i)
{
    if (i < MAXP) {
        return LV->p[i].z;
    }
    return 2;  // (the object stands behind the glass of the cells)
}
// all pictures at their places now (the start, the replay, a new try)
static void scene_rest(void)
{
    s_nz = 0;
    for (int z = 0; z <= 9; z++) {
        for (int i = 0; i < LV->np; i++) {
            if (pic_z(i) == z) {
                s_zorder[s_nz++] = (uint8_t)i;
            }
        }
        for (int i = SPR_OBJ; i <= SPR_STAND; i++) {
            if (pic_z(i) == z) {
                s_zorder[s_nz++] = (uint8_t)i;
            }
        }
    }
    for (int q = 0; q < s_nz; q++) {
        int i = s_zorder[q];
        tp_body_t *b = pic_body(i);
        s_bk[i].in = false;
        if (!b || !s_spr_ok[i]) {
            continue;
        }
        float x, y, a;
        body_px(b, &x, &y, &a);
        s_bk[i] = (bk_t){true, x, y, a};
    }
    for (int i = 0; i < LV->np; i++) {
        if (is_cell(i)) {
            art_cell_update(i);
        }
    }
    art_obj();
    s_sig[SPR_OBJ] = obj_sig();
    s_nsh = 0;
    s_npud = s_pud_n = 0;
    compose();
}
// paint the layers and the pictures of the level (after w_build)
static int64_t s_pt0;  // (the time of the steps of the paint: the log of the build)
static int s_pms[8];
static void pstep(int k)
{
    int64_t t = esp_timer_get_time();
    s_pms[k] = (int)((t - s_pt0) / 1000);
    s_pt0 = t;
}
static void scene_paint(void)
{
    s_pt0 = esp_timer_get_time();
    s_pool_used = 0;
    memset(s_spr_ok, 0, sizeof s_spr_ok);
    s_nglow = 0;
    art_sky(LV->sky);
    pstep(0);
    // (the world layer is the canvas of the bare stand; then the bare stand is packed)
    for (int i = 0; i < W2 * H2; i++) {
        s_l2[i] = KEY;
    }
    cv_set(s_l2, NULL, W2, H2, 2, 0, 0, s_sky);
    s_rng = 5;
    // (the scenery that stands behind the ground first: the house inside, the crane, the tower lines)
    for (int i = 0; i < LV->nd; i++) {
        const char *k = LV->d[i].kind;
        if (!strcmp(k, "inside") || !strcmp(k, "palm") || !strcmp(k, "crane") || !strcmp(k, "shafts")) {
            art_deco(&LV->d[i]);
        }
    }
    pstep(1);
    art_ground(LV->ground, STAND_X0, STAND_X1);
    pstep(2);
    for (int i = 0; i < LV->nd; i++) {
        const char *k = LV->d[i].kind;
        if (strcmp(k, "inside") && strcmp(k, "palm") && strcmp(k, "crane") && strcmp(k, "shafts")) {
            art_deco(&LV->d[i]);
        }
    }
    // the soft shadows of the parts that stand on the stand (the key light is high at the left)
    for (int i = 0; i < LV->np; i++) {
        const pdef_t *P = &LV->p[i];
        if (P->y + P->h / 2 < FLOOR - 3 || P->kind == PK_BALL) {
            continue;
        }
        float sh[8] = {P->x - P->w / 2 + 2, FLOOR - 1.5f, P->x + P->w / 2 + 5, FLOOR - 1.5f,
                       P->x + P->w / 2 + 2, FLOOR + 1.5f, P->x - P->w / 2,     FLOOR + 1.5f};
        cv_poly(sh, 4, C3(10, 20, 14), 70);
    }
    // the front of the stand with the brass plate for the name
    int tw = ui_text_w(&KF_LABEL, LV->name[0]);
    int tw2 = ui_text_w(&KF_LABEL, LV->name[1]);
    pstep(3);
    art_front(STAND_X0, STAND_X1, clampf((float)(tw > tw2 ? tw : tw2) + 24, 100, 300));
    bare_pack(s_l2);
    pstep(4);
    for (int i = 0; i < LV->np; i++) {
        art_part(i);
    }
    // (the pictures of the cells show the panes as they are now: all whole)
    for (int i = 0; i < LV->np; i++) {
        for (int k = 0; k < NSUB; k++) {
            s_pst[i][k] = is_cell(i) ? pane_shown(i, k) : 0;
        }
    }
    art_obj();
    art_stand();
    pstep(5);
    scene_rest();
    pstep(6);
    ESP_LOGI(TAG, "  paint: sky %d, back %d, ground %d, scenery %d, front %d, parts %d, compose %d ms", s_pms[0],
             s_pms[1], s_pms[2], s_pms[3], s_pms[4], s_pms[5], s_pms[6]);
}

// ---------------------------------------------------------------- the sync of the world layer (each frame)
static uint8_t s_live[MAXP + 2];  // 1 = the picture moves (drawn in each frame)
static void scene_sync(void)
{
    float now = (float)G.step * DT_STEP;
    (void)now;
    for (int q = 0; q < s_nz; q++) {
        int i = s_zorder[q];
        s_live[i] = 0;
        if (!s_spr_ok[i]) {
            continue;
        }
        tp_body_t *b = pic_body(i);
        if (!b) {
            bake_out(i);
            continue;
        }
        float x, y, a;
        body_px(b, &x, &y, &a);
        if (b->kind != TP_DYNAMIC) {
            if (!s_bk[i].in) {
                bake_in(i, x, y, a);
            }
            continue;
        }
        float da = fabsf(a - s_bk[i].a);
        while (da > PI_) {
            da = fabsf(da - TAU_);
        }
        bool moved = fabsf(x - s_bk[i].x) > 0.3f || fabsf(y - s_bk[i].y) > 0.3f || da > 0.006f;
        bool asleep = !(b->flags & TP_AWAKE);
        if (s_bk[i].in && moved) {
            bake_out(i);
        }
        if (!s_bk[i].in && asleep) {
            bake_in(i, x, y, a);
        }
        s_live[i] = !s_bk[i].in;
    }
    // the cells whose panes changed, the object that cracked: paint them again
    for (int i = 0; i < LV->np; i++) {
        if (!is_cell(i) || !s_spr_ok[i]) {
            continue;
        }
        if (art_cell_update(i)) {
            bake_refresh(i);
        }
    }
    uint32_t os = obj_sig();
    if (os != s_sig[SPR_OBJ]) {
        s_sig[SPR_OBJ] = os;
        art_obj();
        bake_refresh(SPR_OBJ);
    }
    dirty_flush();
}

// ---------------------------------------------------------------- the live drawing
static void draw_live_parts(void)
{
    for (int q = 0; q < s_nz; q++) {
        int i = s_zorder[q];
        if (!s_live[i]) {
            continue;
        }
        tp_body_t *b = pic_body(i);
        if (!b) {
            continue;
        }
        tp_xf_t xf = body_xf(b);
        float x, y, a;
        xf_px(&xf, &x, &y, &a);
        draw_spr_live(i, x, y, a, 256);
    }
}
// the cracks that grow (a cell in the layer or a cell that moves)
static void draw_growing_cracks(void)
{
    float now = (float)G.step * DT_STEP;
    for (int i = 0; i < LV->np; i++) {
        if (!is_cell(i)) {
            continue;
        }
        const prt_t *R = &G.p[i];
        tp_body_t *b = part_body(i);
        if (!b) {
            continue;
        }
        tp_xf_t xf = s_live[i] ? body_xf(b) : b->xf;
        float c = xf.q.c, s = xf.q.s, bx = px_x(xf.p.x), by = px_y(xf.p.y);
        for (int k = 0; k < R->npane; k++) {
            if (R->sub[k] != 1) {
                continue;
            }
            float age = now - R->crack_t[k];
            if (age >= 0.55f || age < 0) {
                continue;
            }
            float x0, y0, x1, y1;
            pane_box(&LV->p[i], k, &x0, &y0, &x1, &y1);
            seg_t sg[160];
            int n = crack_segs(R->cseed[k], R->cx[k], R->cy[k], x0, y0, x1, y1, sg, 160, 2 + age * 46);
            for (int m = 0; m < n; m++) {
                float ax = bx + c * sg[m].x0 + s * sg[m].y0, ay = by - s * sg[m].x0 + c * sg[m].y0;
                float ex = bx + c * sg[m].x1 + s * sg[m].y1, ey = by - s * sg[m].x1 + c * sg[m].y1;
                tfd_line(scr_x(ax), scr_y(ay), scr_x(ex), scr_y(ey), fmaxf(0.8f, 0.6f * s_cam.z), C3(250, 255, 255),
                         235);
            }
            float hx = bx + c * R->cx[k] + s * R->cy[k], hy = by - s * R->cx[k] + c * R->cy[k];
            if (age < 0.25f) {
                tfd_glow(scr_x(hx), scr_y(hy), 10 * s_cam.z, C3(255, 255, 255), (int)(230 * (1 - age / 0.25f)));
            }
        }
    }
}
// the colour of the material of a part (its pieces)
static uint16_t part_colour(int pi, bool *glass)
{
    *glass = false;
    if (pi == -4) {
        *glass = true;
        return C3(230, 245, 250);
    }
    if (pi < 0 || pi >= LV->np) {
        return C_WOOD;
    }
    switch (LV->p[pi].kind) {
    case PK_PANE:
    case PK_BOTTLE:
    case PK_LENS:
    case PK_LAMP:
    case PK_CELL:
    case PK_ROOF:
        *glass = true;
        return HX(GLASS_TINT[pi % 3]);
    case PK_BRICK:
        return C_BRICK;
    case PK_STONE:
    case PK_SLAB:
        return HX(0x9a968c);
    case PK_POT:
        return C_TERRA;
    case PK_TANK:
        return C_WOOD;
    case PK_BALL:
    case PK_WEIGHT:
    case PK_POST:
    case PK_GIRDER:
    case PK_BELL:
    case PK_BUCKET:
        return HX(0x5a5e66);
    default:
        return C_WOOD;
    }
}
// the pieces of a break (the bodies of generation 1 and 2): polygons (they stay bodies: the replay must see the same
// world, so the picture never removes a body)
static void draw_pieces(void)
{
    for (int i = 0; i < W->body_hi; i++) {
        tp_body_t *b = &W->body[i];
        if (!(b->flags & TP_ALIVE) || b->gen == 0 || b->kind != TP_DYNAMIC) {
            continue;
        }
        bool glass;
        uint16_t col = part_colour(b->user, &glass);
        tp_xf_t xf = body_xf(b);
        float p[16];
        int n = 0;
        if (b->shape == TP_POLY) {
            for (int k = 0; k < b->n && k < 8; k++) {
                float lx = b->vl[k].x, ly = b->vl[k].y;
                float wx = xf.p.x + xf.q.c * lx - xf.q.s * ly, wy = xf.p.y + xf.q.s * lx + xf.q.c * ly;
                p[2 * n] = px_x(wx);
                p[2 * n + 1] = px_y(wy);
                n++;
            }
        }
        if (n < 3) {
            continue;
        }
        float fade = b->life0 > 0 ? clampf(b->life / b->life0 * 2, 0, 1) : 1;
        float sp[16];
        for (int k = 0; k < n; k++) {
            sp[2 * k] = scr_x(p[2 * k]);
            sp[2 * k + 1] = scr_y(p[2 * k + 1]);
        }
        tfd_poly(sp, n, col, (int)((glass ? 110 : 256) * fade));
        uint16_t edge = glass ? C3(255, 255, 255) : cmul(col, 0.6f);
        for (int k = 0; k < n; k++) {
            int q = (k + 1) % n;
            tfd_line(sp[2 * k], sp[2 * k + 1], sp[2 * q], sp[2 * q + 1], 0.8f, edge, (int)((glass ? 200 : 150) * fade));
        }
        if (glass) {
            float k = fsin(xf.q.s * 3 + s_clock * 2 + i);
            if (k > 0.85f) {
                tfd_glow(sp[0], sp[1], 6 * s_cam.z, C3(255, 255, 255), (int)(200 * (k - 0.85f) / 0.15f));
            }
        }
    }
}
// the particles of the world: water, crumbs, sparks, shards
static void draw_particles(void)
{
    for (int i = 0; i < W->npart; i++) {
        const tp_part_t *q = &W->part[i];
        float x = scr_x(px_x(q->p.x)), y = scr_y(px_y(q->p.y));
        if (x < -4 || y < -4 || x > gfx_w + 4 || y > gfx_h + 4) {
            continue;
        }
        float a = q->life0 > 0 ? clampf(q->life / q->life0 * 3, 0, 1) : 1;
        switch (q->kind) {
        case TP_PART_WATER: {
            float k = 0.03f * PXM * s_cam.z;
            tfd_line(x - q->v.x * k, y + q->v.y * k, x, y, 1.3f * fmaxf(1, s_cam.z * 0.7f), C3(205, 240, 255),
                     (int)(200 * a));
            break;
        }
        case TP_PART_SPARK:
            tfd_disc(x, y, 0.9f, C3(255, 230, 170), (int)(255 * a));
            break;
        case TP_PART_SHARD:
            tfd_disc(x, y, 0.8f, C3(235, 252, 255), (int)(230 * a));
            break;
        default: {
            uint16_t c = q->mat == TP_MAT_GLASS ? C3(230, 250, 255) : q->mat == TP_MAT_STONE ? HX(0x9a968c) : C_WOODLT;
            tfd_disc(x, y, 0.8f * fmaxf(1, s_cam.z * 0.6f), c, (int)(230 * a));
            break;
        }
        }
    }
    // the shards of the panes
    for (int i = 0; i < s_nsh; i++) {
        const shard_t *S = &s_sh[i];
        float c = fcos(S->a), s = fsin(S->a), r = S->s;
        float p[6] = {S->x + c * r,        S->y + s * r * 0.5f, S->x - c * r * 0.6f + s * r * 0.5f,
                      S->y - s * r * 0.4f, S->x - c * r * 0.2f, S->y + 0.7f};
        for (int k = 0; k < 3; k++) {
            p[2 * k] = scr_x(p[2 * k]);
            p[2 * k + 1] = scr_y(p[2 * k + 1]);
        }
        tfd_poly(p, 3, s_glass_tint[S->tint], 120);
        float g = fsin(S->a * 2 + s_clock * 3);
        tfd_line(p[0], p[1], p[2], p[3], 0.7f, C3(255, 255, 255), 150 + (int)(100 * fmaxf(0, g)));
        if (g > 0.9f) {
            tfd_glow(p[0], p[1], 5 * s_cam.z, C3(255, 255, 255), (int)(200 * (g - 0.9f) / 0.1f));
        }
    }
}
// the joints: the ropes, the chains, the cables, the rods, the pins
static void draw_rope(float x0, float y0, float x1, float y1, float slack, int look)
{
    float mx = (x0 + x1) / 2, my = (y0 + y1) / 2 + slack;
    float px = scr_x(x0), py = scr_y(y0);
    int n = look == 1 ? 18 : 10;
    for (int k = 1; k <= n; k++) {
        float t = (float)k / n, u = 1 - t;
        float x = u * u * x0 + 2 * u * t * mx + t * t * x1, y = u * u * y0 + 2 * u * t * my + t * t * y1;
        float sx = scr_x(x), sy = scr_y(y);
        if (look == 1) {
            tfd_ring((px + sx) / 2, (py + sy) / 2, fmaxf(0.9f, 0.9f * s_cam.z), fmaxf(0.6f, 0.45f * s_cam.z),
                     k % 2 ? C3(141, 136, 125) : C3(77, 74, 68), 256);
        } else {
            uint16_t c = look == 2 ? C3(60, 58, 54) : C_ROPE;
            tfd_line(px, py, sx, sy, fmaxf(1.0f, 0.8f * s_cam.z), c, 256);
            if (look == 0 && s_cam.z > 1.4f) {
                tfd_line(px, py - 0.4f, sx, sy - 0.4f, 0.5f, C3(240, 214, 160), 140);
            }
        }
        px = sx;
        py = sy;
    }
}
static void draw_joints(void)
{
    float now = (float)G.step * DT_STEP;
    for (int k = 0; k < LV->nj; k++) {
        const jdef_t *J = &LV->j[k];
        tp_joint_t *j = G.j[k].id != TP_NONE ? tp_joint(W, G.j[k].id) : NULL;
        if (!j) {
            // (a rope that went: a stub hangs from a fixed anchor for 2 s, then the rest of it)
            if (J->kind == J_ROPE && J->a < 0 && G.j[k].snap_t > 0) {
                float t = clampf((now - G.j[k].snap_t) / 0.6f, 0, 1);
                float l = (J->len > 0 ? J->len : 20) * 0.3f;
                draw_rope(J->x1, J->y1, J->x1 + (1 - t) * 4, J->y1 + l * (0.5f + 0.5f * t), 0, J->look);
            }
            continue;
        }
        tp_vec_t a, b;
        tp_joint_anchors(W, j, &a, &b);
        // (the anchors of a body that moves: from its drawn place)
        tp_body_t *ba = j->a >= 0 ? &W->body[j->a] : NULL, *bb = j->b >= 0 ? &W->body[j->b] : NULL;
        if (ba && ba->kind == TP_DYNAMIC) {
            tp_xf_t xf = body_xf(ba);
            a.x = xf.p.x + xf.q.c * j->la.x - xf.q.s * j->la.y;
            a.y = xf.p.y + xf.q.s * j->la.x + xf.q.c * j->la.y;
        }
        if (bb && bb->kind == TP_DYNAMIC) {
            tp_xf_t xf = body_xf(bb);
            b.x = xf.p.x + xf.q.c * j->lb.x - xf.q.s * j->lb.y;
            b.y = xf.p.y + xf.q.s * j->lb.x + xf.q.c * j->lb.y;
        }
        float x0 = px_x(a.x), y0 = px_y(a.y), x1 = px_x(b.x), y1 = px_y(b.y);
        if (J->kind == J_ROPE) {
            float d = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)), L = j->maxlen * PXM;
            float slack = L > d ? sqrtf(fmaxf(0, L * L - d * d)) * 0.5f : 0;
            draw_rope(x0, y0, x1, y1, slack, J->look);
        } else if (J->kind == J_ROD) {
            tfd_line(scr_x(x0), scr_y(y0), scr_x(x1), scr_y(y1), fmaxf(1.2f, 1.2f * s_cam.z), C3(60, 62, 66), 256);
        } else if (J->kind == J_HINGE || J->kind == J_MOTOR) {
            float r = fmaxf(1.4f, 1.6f * s_cam.z);
            tfd_disc(scr_x(x0), scr_y(y0), r, C3(42, 40, 38), 256);
            tfd_disc(scr_x(x0) - r * 0.3f, scr_y(y0) - r * 0.3f, r * 0.4f, C3(200, 190, 170), 200);
        }
    }
}
// the pipes (a valve) and the winches (a drum)
static void draw_emitters(void)
{
    for (int i = 0; i < LV->ne; i++) {
        const edef_t *E = &LV->e[i];
        float x, y;
        if (E->kind == 0) {
            x = E->x;
            y = E->y;
        } else {
            const jdef_t *J = &LV->j[(int)E->rope];
            x = J->b < 0 ? J->x2 : J->x1;
            y = J->b < 0 ? J->y2 : J->y1;
        }
        float sx = scr_x(x), sy = scr_y(y), z = s_cam.z;
        if (E->kind == 0) {
            float l = sqrtf(E->dx * E->dx + E->dy * E->dy) + 1e-3f;
            tfd_line(sx - E->dx / l * 6 * z, sy - E->dy / l * 6 * z, sx, sy, 3 * z, C3(110, 106, 99), 256);
            float ang = G.e[i].on ? s_clock * 6 : 0;
            tfd_ring(sx - E->dx / l * 6 * z, sy - E->dy / l * 6 * z - 4 * z, 2.6f * z, 0.9f * z, C3(190, 50, 40), 256);
            tfd_line(sx - E->dx / l * 6 * z + fcos(ang) * 2.6f * z,
                     sy - E->dy / l * 6 * z - 4 * z + fsin(ang) * 2.6f * z,
                     sx - E->dx / l * 6 * z - fcos(ang) * 2.6f * z,
                     sy - E->dy / l * 6 * z - 4 * z - fsin(ang) * 2.6f * z, 0.7f * z, C3(190, 50, 40), 256);
        } else {
            // a winch: an iron frame on the stand, a drum with the turns of the rope, a crank that turns
            float ang = (G.e[i].on ? s_clock * 5 : 0), base = scr_y(FLOOR);
            float fr[6] = {sx - 6 * z, base, sx + 6 * z, base, sx, sy - 1 * z};
            tfd_poly(fr, 3, C3(44, 60, 55), 256);
            tfd_line(sx - 7 * z, base, sx + 7 * z, base, 1.2f * z, C3(30, 40, 36), 256);
            tfd_disc(sx, sy, 4.2f * z, C3(58, 53, 48), 256);
            for (int k = -2; k <= 2; k++) {
                tfd_line(sx - 3.6f * z, sy + k * 1.3f * z, sx + 3.6f * z, sy + k * 1.3f * z, 0.6f * z, C_ROPE, 200);
            }
            tfd_line(sx, sy, sx + fcos(ang) * 6 * z, sy + fsin(ang) * 6 * z, 0.9f * z, C3(150, 140, 120), 256);
            tfd_disc(sx + fcos(ang) * 6 * z, sy + fsin(ang) * 6 * z, 1.2f * z, C3(190, 50, 40), 256);
            tfd_disc(sx, sy, 1 * z, C_BRASS, 256);
        }
    }
}
// the lamps of the scenery: a warm light that flickers
static void draw_glows(void)
{
    for (int i = 0; i < s_nglow; i++) {
        float f = 0.85f + 0.15f * noise1(s_clock * 7 + i * 13);
        tfd_glow(scr_x(s_glow[i][0]), scr_y(s_glow[i][1]), s_glow[i][2] * s_cam.z * f, C3(255, 184, 80),
                 (int)(120 * f));
    }
}

// ---------------------------------------------------------------- the handles of the moves
static bool hand_live(int h)
{
    return h >= 0 && h < LV->nh && !G.used[h] && w_move_ok(h);
}
static void hand_point(int h, float *sx, float *sy)
{
    float x, y;
    move_point(h, &x, &y);
    *sx = scr_x(x);
    *sy = scr_y(y);
}
// the handle nearest to a screen point (within r px), or -1
static int hand_at(float x, float y, float r)
{
    int best = -1;
    float bd = r * r;
    for (int h = 0; h < LV->nh; h++) {
        if (!hand_live(h)) {
            continue;
        }
        float hx, hy;
        hand_point(h, &hx, &hy);
        float d = (hx - x) * (hx - x) + (hy - y) * (hy - y);
        if (d < bd) {
            bd = d;
            best = h;
        }
    }
    return best;
}
// the sign of a move in a ring: scissors (cut), a pin (pull), a star (crack), an arrow (kick), a valve (open), a gear (start)
static void hand_icon(int kind, float x, float y, float r, uint16_t c, int a, float dx, float dy)
{
    switch (kind) {
    case MV_CUT:
        tfd_line(x - r * 0.5f, y - r * 0.5f, x + r * 0.5f, y + r * 0.5f, 1.4f, c, a);
        tfd_line(x - r * 0.5f, y + r * 0.5f, x + r * 0.5f, y - r * 0.5f, 1.4f, c, a);
        tfd_ring(x - r * 0.55f, y + r * 0.55f, r * 0.22f, 1, c, a);
        tfd_ring(x + r * 0.55f, y + r * 0.55f, r * 0.22f, 1, c, a);
        break;
    case MV_PULL:
        tfd_line(x, y - r * 0.6f, x, y + r * 0.5f, 1.6f, c, a);
        tfd_ring(x, y - r * 0.6f, r * 0.25f, 1.2f, c, a);
        tfd_line(x - r * 0.35f, y + r * 0.1f, x, y - r * 0.25f, 1.2f, c, a);
        tfd_line(x + r * 0.35f, y + r * 0.1f, x, y - r * 0.25f, 1.2f, c, a);
        break;
    case MV_CRACK:
        for (int k = 0; k < 5; k++) {
            float an = k * 1.2566f + 0.3f;
            tfd_line(x, y, x + fcos(an) * r * 0.7f, y + fsin(an) * r * 0.7f, 1.1f, c, a);
        }
        break;
    case MV_KICK: {
        float l = sqrtf(dx * dx + dy * dy) + 1e-3f, ux = dx / l, uy = dy / l;
        tfd_line(x - ux * r * 0.6f, y - uy * r * 0.6f, x + ux * r * 0.6f, y + uy * r * 0.6f, 1.6f, c, a);
        tfd_line(x + ux * r * 0.6f, y + uy * r * 0.6f, x + ux * r * 0.1f - uy * r * 0.4f,
                 y + uy * r * 0.1f + ux * r * 0.4f, 1.6f, c, a);
        tfd_line(x + ux * r * 0.6f, y + uy * r * 0.6f, x + ux * r * 0.1f + uy * r * 0.4f,
                 y + uy * r * 0.1f - ux * r * 0.4f, 1.6f, c, a);
        break;
    }
    case MV_OPEN:
        tfd_ring(x, y, r * 0.5f, 1.3f, c, a);
        tfd_line(x - r * 0.5f, y, x + r * 0.5f, y, 1.1f, c, a);
        tfd_line(x, y - r * 0.5f, x, y + r * 0.5f, 1.1f, c, a);
        break;
    default:
        tfd_ring(x, y, r * 0.42f, 1.6f, c, a);
        for (int k = 0; k < 6; k++) {
            float an = k * 1.0472f + s_clock;
            tfd_line(x + fcos(an) * r * 0.42f, y + fsin(an) * r * 0.42f, x + fcos(an) * r * 0.68f,
                     y + fsin(an) * r * 0.68f, 1.6f, c, a);
        }
        break;
    }
}
