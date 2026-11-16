// CUBBY: the robot and the other models (included by app_cubby.c). The robot is a set of rigid parts (two treads,
// the body, the hatch, the neck, the head, two eyes with brows, two arms with claws, the lights of the front panel)
// that a rig puts together in each frame. Also the cubes, the junk piles, the SUNDROP and the spark.
// The robot faces +z; +x is its right; the origin is on the ground between the treads.

enum {
    M_TREAD,
    M_CLEAT,
    M_BODY,
    M_HATCH,
    M_NECK,
    M_YOKE,
    M_EYE,
    M_LENS,
    M_IRIS,
    M_BROW,
    M_ARM,
    M_FINGER,
    M_LAMP_TIN,
    M_LAMP_SPRING,
    M_LAMP_FLOAT,
    M_CUBE_TIN,
    M_SPRING_BASE,
    M_SPRING_COIL,
    M_FLOAT_BOX,
    M_BALLOON_P,
    M_BALLOON_Y,
    M_BALLOON_G,
    M_BALLOON_B,
    M_STRING,
    M_PILE_TIN,
    M_PILE_SPRING,
    M_POST,
    M_SUNDROP,
    M_SPARK,
    M_BIRD,
    M_CUBE_TIN_LO,
    M_COIL_LO,
    M_PILE_TIN_LO,
    M_PILE_SPRING_LO,
    M_BODY_F,
    M_HATCH_F,
    M_COUNT
};
static model_t s_mod[M_COUNT];

// the tread: a stadium (length TL, height TH) and the width TW, along z
#define TL      1.0f
#define TH      0.38f
#define TW      0.22f
#define TR      (TH * 0.5f)
#define TX      0.40f
#define T_LOOP  (2 * (TL - 2 * TR) + TAU_F * TR)
#define N_CLEAT 7

// a point of the tread loop at the arc s (0 = the bottom at the back, then forward along the bottom, up the front, back
// along the top, down the back): the place (z, y) and the outward normal (nz, ny)
static void tread_at(float s, float *z, float *y, float *nz, float *ny)
{
    const float run = TL - 2 * TR, arc = PI_F * TR;
    while (s < 0) {
        s += T_LOOP;
    }
    while (s >= T_LOOP) {
        s -= T_LOOP;
    }
    if (s < run) {
        *z = -run * 0.5f + s;
        *y = 0;
        *nz = 0;
        *ny = -1;
        return;
    }
    s -= run;
    if (s < arc) {
        float a = -PI_F * 0.5f + s / TR;
        *nz = fcos(a);
        *ny = fsin(a);
        *z = run * 0.5f + *nz * TR;
        *y = TR + *ny * TR;
        return;
    }
    s -= arc;
    if (s < run) {
        *z = run * 0.5f - s;
        *y = TH;
        *nz = 0;
        *ny = 1;
        return;
    }
    s -= run;
    float a = PI_F * 0.5f + s / TR;
    *nz = fcos(a);
    *ny = fsin(a);
    *z = -run * 0.5f + *nz * TR;
    *y = TR + *ny * TR;
}
static void build_tread(void)
{
    float cb[3], cs[3], cr[3], ch[3];
    col3(cb, 0x2C3146);
    col3(cs, 0x3C4566);
    col3(cr, 0x55608A);
    col3(ch, 0xFFC63A);
    const int N = 10;
    int band = s_bnv;
    for (int i = 0; i <= N; i++) {
        float z, y, nz, ny;
        tread_at(T_LOOP * i / N, &z, &y, &nz, &ny);
        mb_v(V3(-TW * 0.5f, y, z), V3(0, ny, nz), cb, 1);
        mb_v(V3(TW * 0.5f, y, z), V3(0, ny, nz), cb, 1);
    }
    for (int i = 0; i < N; i++) {
        int a = band + i * 2;
        mb_q(a, a + 1, a + 3, a + 2);
    }
    for (int side = 0; side < 2; side++) {
        float x = side ? TW * 0.5f : -TW * 0.5f;
        v3 n = V3(side ? 1 : -1, 0, 0);
        int m = mb_v(V3(x, TR, 0), n, cs, 1);
        int b0 = s_bnv;
        for (int i = 0; i <= N; i++) {
            float z, y, nz, ny;
            tread_at(T_LOOP * i / N, &z, &y, &nz, &ny);
            mb_v(V3(x, TR + (y - TR) * 0.92f, z * 0.96f), n, i % 2 ? cr : cs, 1);
        }
        for (int i = 0; i < N; i++) {
            mb_t(m, b0 + i, b0 + i + 1);
        }
        // the hubs of the wheels: three yellow caps on each side
        for (int k = 0; k < 3; k++) {
            float zc = (k - 1) * (TL - 2 * TR) * 0.5f;
            float r = k == 1 ? 0.075f : 0.105f;
            int hm = mb_v(V3(x * 1.08f, TR, zc), n, ch, 1);
            int h0 = s_bnv;
            for (int i = 0; i <= 5; i++) {
                float a = TAU_F * i / 5;
                mb_v(V3(x * 1.04f, TR + fsin(a) * r, zc + fcos(a) * r),
                     vnorm(vadd(n, V3(0, fsin(a) * 0.5f, fcos(a) * 0.5f))), ch, 1);
            }
            for (int i = 0; i < 5; i++) {
                mb_t(hm, h0 + i, h0 + i + 1);
            }
        }
    }
    model_add(&s_mod[M_TREAD]);
}
static void robot_build(void)
{
    mb_begin();
    build_tread();
    // a cleat of the tread
    s_bmat = MT_MATTE;
    prim_box(V3(0, 0, 0), V3(TW * 0.52f, 0.022f, 0.042f), 0x59638C);
    model_add(&s_mod[M_CLEAT]);
    // the body: the yellow box, the orange belly band, the front panel, the blue sun panel on the top
    s_bmat = MT_PLASTIC;
    prim_cbox(V3(0, 0, 0), V3(0.34f, 0.30f, 0.31f), 0.09f, 0xFFB42C);
    prim_cbox(V3(0, -0.22f, 0), V3(0.352f, 0.06f, 0.322f), 0.035f, 0xFF7A36);
    prim_box(V3(0, 0.07f, 0.30f), V3(0.22f, 0.12f, 0.03f), 0xFFF0CC);
    s_bmat = MT_GLASS;
    prim_box(V3(0, 0.30f, -0.04f), V3(0.25f, 0.02f, 0.19f), 0x2F5ED8);
    // the back: a plate with three vents
    s_bmat = MT_PLASTIC;
    prim_box(V3(0, 0.02f, -0.315f), V3(0.21f, 0.15f, 0.02f), 0xF29A22);
    for (int k = 0; k < 3; k++) {
        prim_box(V3(0, 0.09f - k * 0.07f, -0.336f), V3(0.15f, 0.012f, 0.008f), 0x6A3A10);
    }
    model_add(&s_mod[M_BODY]);
    // the hatch: its hinge at the bottom edge (the origin)
    s_bmat = MT_PLASTIC;
    prim_cbox(V3(0, 0.09f, 0.0f), V3(0.24f, 0.09f, 0.02f), 0.015f, 0xF08E1E);
    prim_box(V3(0, 0.15f, 0.02f), V3(0.10f, 0.012f, 0.01f), 0x8A4A10);
    model_add(&s_mod[M_HATCH]);
    // the neck (from y 0 up to 0.24)
    s_bmat = MT_METAL;
    prim_cyl(V3(0, 0, 0), 0, 0.26f, 0.05f, 0.04f, 6, 0, 0x7C86A0, 0xA2ACC4, 0);
    prim_cyl(V3(0, 0, 0), -0.02f, 0.04f, 0.09f, 0.08f, 8, 2, 0x5A6380, 0x7C86A0, 0x8C96B0);
    model_add(&s_mod[M_NECK]);
    // the yoke of the head: the bar between the eyes
    prim_box(V3(0, 0.03f, 0), V3(0.15f, 0.035f, 0.05f), 0x8C96B0);
    model_add(&s_mod[M_YOKE]);
    // an eye: a short white barrel along z with a soft front rim (the lens and the iris are apart: they blink)
    s_bmat = MT_PLASTIC;
    {
        v3 p[4] = {V3(0, 0, -0.15f), V3(0, 0, -0.07f), V3(0, 0, 0.10f), V3(0, 0, 0.14f)};
        float rr[4] = {0.12f, 0.162f, 0.168f, 0.154f};
        prim_tube(p, rr, 4, 7, 0xD8DFEA, 0xF7F9FD, true);
    }
    model_add(&s_mod[M_EYE]);
    s_bmat = MT_GLASS;
    prim_ell(V3(0, 0, 0.133f), V3(0.132f, 0.132f, 0.035f), 2, 7, 0x1A2444, 0x0C1226);
    model_add(&s_mod[M_LENS]);
    s_bmat = MT_GLOW;
    prim_disc(V3(0, 0, 0.166f), V3(0, 0, 1), 0.064f, 8, 0xA8FFFF, 0x3CDCFF);
    model_add(&s_mod[M_IRIS]);
    s_bmat = MT_PLASTIC;
    prim_box(V3(0, 0, 0), V3(0.16f, 0.02f, 0.07f), 0xFFB42C);
    model_add(&s_mod[M_BROW]);
    // an arm: a flat bar down from the shoulder (the origin), and a round shoulder joint
    s_bmat = MT_METAL;
    prim_box(V3(0, -0.14f, 0), V3(0.035f, 0.15f, 0.04f), 0x9AA4BC);
    {
        // (the shoulder joint: a disc on the side of the body, its axis along x)
        xf_t sj;
        xf_make(&sj, V3(0, 0, 0), 0, 0, PI_F * 0.5f, V3(1, 1, 1));
        mb_frame(&sj);
        prim_cyl(V3(0, 0, 0), -0.04f, 0.04f, 0.08f, 0.08f, 6, 3, 0x6C7694, 0x6C7694, 0xFFB42C);
        mb_noframe();
    }
    model_add(&s_mod[M_ARM]);
    prim_box(V3(0, -0.06f, 0), V3(0.022f, 0.065f, 0.03f), 0x5E6884);
    model_add(&s_mod[M_FINGER]);
    // the lights of the panel (the kind of cube in the body)
    s_bmat = MT_GLOW;
    prim_disc(V3(0, 0, 0.01f), V3(0, 0, 1), 0.036f, 6, 0xFFD0A0, 0xFF6A4A);
    model_add(&s_mod[M_LAMP_TIN]);
    prim_disc(V3(0, 0, 0.01f), V3(0, 0, 1), 0.036f, 6, 0xFFC0E8, 0xFF4AA8);
    model_add(&s_mod[M_LAMP_SPRING]);
    prim_disc(V3(0, 0, 0.01f), V3(0, 0, 1), 0.036f, 6, 0xC8FFFF, 0x3AD8FF);
    model_add(&s_mod[M_LAMP_FLOAT]);

    // ---- the cubes (1 unit, the origin in the middle)
    s_bmat = MT_METAL;
    // (the TIN cube: silver metal with the colours of the pressed cans: round lids on its faces)
    prim_cbox(V3(0, 0, 0), V3(0.47f, 0.47f, 0.47f), 0.08f, 0xC9D6EA);
    {
        static const uint32_t LID[4] = {0xFF4E50, 0xFFD23C, 0x3E9EFF, 0x4CD86E};
        static const float LP[5][5] = {{0, 1, 0, -0.18f, 0.14f},
                                       {0, 1, 0, 0.2f, -0.16f},
                                       {1, 0, 0, 0.12f, 0.1f},
                                       {-1, 0, 0, -0.1f, -0.15f},
                                       {0, 0, 1, 0.15f, 0.12f}};
        for (int i = 0; i < 5; i++) {
            v3 n = V3(LP[i][0], LP[i][1], LP[i][2]);
            v3 u = absf(n.y) > 0.5f ? V3(1, 0, 0) : V3(0, 1, 0), w = vcross(n, u);
            v3 c0 = vadd(vmul(n, 0.475f), vadd(vmul(u, LP[i][3]), vmul(w, LP[i][4])));
            prim_disc(c0, n, 0.13f, 7, LID[i & 3], LID[(i + 1) & 3]);
            prim_disc(vadd(c0, vmul(vadd(u, w), -0.17f)), n, 0.08f, 6, LID[(i + 2) & 3], LID[(i + 2) & 3]);
        }
    }
    model_add(&s_mod[M_CUBE_TIN]);
    s_bmat = MT_METAL;
    prim_cbox(V3(0, 0, 0), V3(0.47f, 0.47f, 0.47f), 0.08f, 0xC9D6EA);
    model_add(&s_mod[M_CUBE_TIN_LO]);
    s_bmat = MT_PLASTIC;
    prim_cbox(V3(0, -0.2f, 0), V3(0.47f, 0.27f, 0.47f), 0.09f, 0xFF4FAE);
    prim_cbox(V3(0, -0.06f, 0), V3(0.48f, 0.04f, 0.48f), 0.03f, 0xFFE0F2);
    model_add(&s_mod[M_SPRING_BASE]);
    {
        // the coil (from y 0 to 0.36 above the base top) and its plate
        v3 p[19];
        float rr[19];
        for (int i = 0; i <= 18; i++) {
            float t = i / 18.0f, a = t * TAU_F * 3;
            p[i] = V3(fcos(a) * 0.30f, 0.04f + t * 0.28f, fsin(a) * 0.30f);
            rr[i] = 0.05f;
        }
        s_bmat = MT_METAL;
        prim_tube(p, rr, 19, 4, 0xE8EEF8, 0xFFFFFF, false);
        s_bmat = MT_PLASTIC;
        prim_cbox(V3(0, 0.36f, 0), V3(0.40f, 0.04f, 0.40f), 0.03f, 0xFF79C2);
        model_add(&s_mod[M_SPRING_COIL]);
        for (int i = 0; i <= 9; i++) {
            float t = i / 9.0f, a = t * TAU_F * 3;
            p[i] = V3(fcos(a) * 0.30f, 0.04f + t * 0.28f, fsin(a) * 0.30f);
            rr[i] = 0.06f;
        }
        s_bmat = MT_METAL;
        prim_tube(p, rr, 10, 3, 0xE8EEF8, 0xFFFFFF, false);
        s_bmat = MT_PLASTIC;
        prim_box(V3(0, 0.36f, 0), V3(0.40f, 0.04f, 0.40f), 0xFF79C2);
        model_add(&s_mod[M_COIL_LO]);
    }
    s_bmat = MT_PLASTIC;
    prim_cbox(V3(0, 0, 0), V3(0.47f, 0.47f, 0.47f), 0.17f, 0x58E2FF);
    model_add(&s_mod[M_FLOAT_BOX]);
    prim_ell(V3(0, 0, 0), V3(0.2f, 0.25f, 0.2f), 3, 7, 0xFFA8D4, 0xFF4F9E);
    model_add(&s_mod[M_BALLOON_P]);
    prim_ell(V3(0, 0, 0), V3(0.2f, 0.25f, 0.2f), 3, 7, 0xFFF0A0, 0xFFC82E);
    model_add(&s_mod[M_BALLOON_Y]);
    prim_ell(V3(0, 0, 0), V3(0.2f, 0.25f, 0.2f), 3, 7, 0xD4FFA0, 0x6ADC3A);
    model_add(&s_mod[M_BALLOON_G]);
    prim_ell(V3(0, 0, 0), V3(0.2f, 0.25f, 0.2f), 3, 7, 0xB0E8FF, 0x3A9CFF);
    model_add(&s_mod[M_BALLOON_B]);
    s_bmat = MT_MATTE;
    prim_cyl(V3(0, 0, 0), -1.0f, 0, 0.008f, 0.008f, 3, 0, 0xFFFFFF, 0xFFFFFF, 0);
    model_add(&s_mod[M_STRING]);

    // ---- the junk piles
    s_bmat = MT_METAL;
    {
        static const float CAN[8][6] = {// x, z, lie (0 up, 1 on its side), yaw, colour index, y
                                        {0, 0, 0, 0, 0, 0},
                                        {0.24f, 0.05f, 0, 0, 1, 0},
                                        {-0.2f, 0.12f, 0, 0, 2, 0},
                                        {0.05f, -0.24f, 0, 0, 3, 0},
                                        {0.1f, 0.1f, 0, 0, 2, 0.24f},
                                        {-0.3f, -0.2f, 1, 0.6f, 1, 0},
                                        {0.35f, -0.25f, 1, 2.0f, 0, 0},
                                        {-0.1f, -0.05f, 0, 0, 3, 0.24f}};
        static const uint32_t CC[4][2] = {
                {0xFF4E50, 0xD63040}, {0xFFD23C, 0xF0A818}, {0x3E9EFF, 0x2A70D8}, {0x4CD86E, 0x2EA850}};
        // (the far pile: the cans that show from far, 5 sides)
        for (int lo = 0; lo < 2; lo++) {
            for (int i = 0; i < 8; i++) {
                const float *c = CAN[i];
                if (lo && (i == 2 || i == 3 || i == 7)) {
                    continue;
                }
                xf_t x;
                xf_make(&x, V3(c[0], c[5] + (c[2] > 0 ? 0.11f : 0), c[1]), c[3], c[2] > 0 ? PI_F * 0.5f : 0, 0,
                        V3(1, 1, 1));
                mb_frame(&x);
                int k = (int)c[4];
                prim_cyl(V3(0, 0, 0), c[2] > 0 ? -0.12f : 0, c[2] > 0 ? 0.12f : 0.24f, 0.105f, 0.105f, lo ? 5 : 6, 2,
                         CC[k][1], CC[k][0], 0xD8E2F0);
            }
            mb_noframe();
            model_add(&s_mod[lo ? M_PILE_TIN_LO : M_PILE_TIN]);
        }
    }
    s_bmat = MT_MATTE;
    {
        // (an old striped mattress: pink with white stripes, and two coils out of it)
        prim_cbox(V3(0, 0.13f, 0), V3(0.62f, 0.13f, 0.42f), 0.07f, 0xFF9AC8);
        for (int k = 0; k < 3; k++) {
            prim_box(V3(-0.36f + k * 0.36f, 0.135f, 0), V3(0.07f, 0.135f, 0.425f), 0xFFFFFF);
        }
        s_bmat = MT_METAL;
        for (int k = 0; k < 2; k++) {
            v3 p[10];
            float rr[10];
            float cx = -0.25f + k * 0.45f, cz = k ? 0.1f : -0.1f;
            for (int i = 0; i <= 9; i++) {
                float t = i / 9.0f, a = t * TAU_F * 2.5f;
                p[i] = V3(cx + fcos(a) * 0.1f, 0.24f + t * 0.3f, cz + fsin(a) * 0.1f);
                rr[i] = 0.025f;
            }
            prim_tube(p, rr, 10, 3, 0xC8D2E2, 0xFFFFFF, false);
        }
        model_add(&s_mod[M_PILE_SPRING]);
        // (the far one: the mattress as a box with the stripes, one coil)
        s_bmat = MT_MATTE;
        prim_box(V3(0, 0.13f, 0), V3(0.62f, 0.13f, 0.42f), 0xFF9AC8);
        for (int k = 0; k < 3; k++) {
            prim_box(V3(-0.36f + k * 0.36f, 0.135f, 0), V3(0.07f, 0.135f, 0.425f), 0xFFFFFF);
        }
        s_bmat = MT_METAL;
        {
            v3 p[7];
            float rr[7];
            for (int i = 0; i <= 6; i++) {
                float t = i / 6.0f, a = t * TAU_F * 2.5f;
                p[i] = V3(-0.25f + fcos(a) * 0.1f, 0.24f + t * 0.3f, -0.1f + fsin(a) * 0.1f);
                rr[i] = 0.03f;
            }
            prim_tube(p, rr, 7, 3, 0xC8D2E2, 0xFFFFFF, false);
        }
        model_add(&s_mod[M_PILE_SPRING_LO]);
    }
    s_bmat = MT_MATTE;
    prim_cyl(V3(0, 0, 0), 0, 1.1f, 0.05f, 0.04f, 6, 2, 0x9A6440, 0xC08A5A, 0xC08A5A);
    prim_ell(V3(0, 0.02f, 0), V3(0.22f, 0.06f, 0.22f), 3, 8, 0xB07A50, 0x8A5A3A);
    model_add(&s_mod[M_POST]);

    // ---- the friend robot (a second board): its body and its hatch in teal and blue
    s_bmat = MT_PLASTIC;
    prim_cbox(V3(0, 0, 0), V3(0.34f, 0.30f, 0.31f), 0.09f, 0x2FD8E6);
    prim_cbox(V3(0, -0.22f, 0), V3(0.352f, 0.06f, 0.322f), 0.035f, 0x2A62E0);
    prim_box(V3(0, 0.07f, 0.30f), V3(0.22f, 0.12f, 0.03f), 0xE6FFFF);
    s_bmat = MT_GLASS;
    prim_box(V3(0, 0.30f, -0.04f), V3(0.25f, 0.02f, 0.19f), 0xFF5AA0);
    s_bmat = MT_PLASTIC;
    prim_box(V3(0, 0.02f, -0.315f), V3(0.21f, 0.15f, 0.02f), 0x24A8D0);
    for (int k = 0; k < 3; k++) {
        prim_box(V3(0, 0.09f - k * 0.07f, -0.336f), V3(0.15f, 0.012f, 0.008f), 0x103C5A);
    }
    model_add(&s_mod[M_BODY_F]);
    prim_cbox(V3(0, 0.09f, 0.0f), V3(0.24f, 0.09f, 0.02f), 0.015f, 0x22B4D8);
    prim_box(V3(0, 0.15f, 0.02f), V3(0.10f, 0.012f, 0.01f), 0x0E3A58);
    model_add(&s_mod[M_HATCH_F]);

    // ---- the SUNDROP, the spark, a bird
    s_bmat = MT_GLOW;
    prim_ell(V3(0, 0, 0), V3(0.27f, 0.32f, 0.27f), 4, 8, 0xFFF4B0, 0xFFA82A);
    model_add(&s_mod[M_SUNDROP]);
    s_bmat = MT_METAL;
    {
        xf_t x;
        xf_make(&x, V3(0, 0, 0), 0, PI_F * 0.5f, 0, V3(1, 1, 1));
        mb_frame(&x);
        prim_cyl(V3(0, 0, 0), -0.035f, 0.035f, 0.13f, 0.13f, 6, 3, 0xFFB420, 0xFFD24A, 0xFFE07A);
        prim_cyl(V3(0, 0, 0), -0.04f, 0.04f, 0.05f, 0.05f, 6, 3, 0xC08010, 0xC08010, 0x8A5A10);
        mb_noframe();
        model_add(&s_mod[M_SPARK]);
    }
    s_bmat = MT_MATTE;
    {
        float cw[3];
        col3(cw, 0xFFFFFF);
        // a gull: a body and two wings (the wings move: two models would cost more; the bird is far and small)
        prim_ell(V3(0, 0, 0), V3(0.08f, 0.07f, 0.22f), 3, 6, 0xFFFFFF, 0xD8E0EA);
        int a = mb_v(V3(0, 0.02f, 0.05f), V3(0, 1, 0), cw, 1), b = mb_v(V3(-0.55f, 0.12f, -0.02f), V3(0, 1, 0), cw, 1);
        int c = mb_v(V3(0, 0.02f, -0.1f), V3(0, 1, 0), cw, 1);
        mb_t(a, b, c);
        a = mb_v(V3(0, 0.02f, 0.05f), V3(0, -1, 0), cw, 1);
        b = mb_v(V3(-0.55f, 0.12f, -0.02f), V3(0, -1, 0), cw, 1);
        c = mb_v(V3(0, 0.02f, -0.1f), V3(0, -1, 0), cw, 1);
        mb_t(a, b, c);
        a = mb_v(V3(0, 0.02f, 0.05f), V3(0, 1, 0), cw, 1);
        b = mb_v(V3(0.55f, 0.12f, -0.02f), V3(0, 1, 0), cw, 1);
        c = mb_v(V3(0, 0.02f, -0.1f), V3(0, 1, 0), cw, 1);
        mb_t(a, b, c);
        a = mb_v(V3(0, 0.02f, 0.05f), V3(0, -1, 0), cw, 1);
        b = mb_v(V3(0.55f, 0.12f, -0.02f), V3(0, -1, 0), cw, 1);
        c = mb_v(V3(0, 0.02f, -0.1f), V3(0, -1, 0), cw, 1);
        mb_t(a, b, c);
        model_add(&s_mod[M_BIRD]);
    }
    s_bmat = MT_MATTE;
}

// ---------------------------------------------------------------- the rig
typedef struct {
    v3 p;                    // the place (the ground under the treads)
    float yaw;               // the facing (0 = +z)
    float pitch, roll;       // the tilt of the whole robot (a climb, a lean)
    float squash;            // 1 = none; less = the body is low and wide
    float bob;               // the body up and down on the treads
    float body_pitch;        // the body leans on its treads (speed, a brake)
    float tread_l, tread_r;  // the arc of the cleats on each tread
    float head_yaw, head_pitch, head_roll;
    float neck;            // 1 = normal, up to 1.7 (curious)
    float eye_roll;        // + = the outer ends of the eyes go down (sad)
    float brow_l, brow_r;  // the turn of each brow (+ = the inner end up)
    float brow_up;         // the brows up (surprise)
    float iris;            // the size of the iris (1 = normal)
    float blink;           // 0 open .. 1 closed
    float arm_l, arm_r;    // the shoulder angles: 0 down, pi/2 forward, more = up
    float claw;            // 0 closed .. 1 open
    float hatch;           // 0 closed .. 1 open
    int carry;             // the kind of cube in the arms (0 none)
    float carry_s;         // its scale
    float flash;           // a white flash (0..1)
    int gauge;             // the kind on the lights of the panel (0 none)
    float tint;            // the light of the robot (1 = normal)
} rig_t;

static void draw_cube_model(int kind, const xf_t *x, float coil, float tint, float flash, float t);

static void xf_tr(xf_t *o, const xf_t *a, v3 t)
{
    xf_t b;
    xf_ident(&b);
    b.t = t;
    xf_mul(o, a, &b);
}
static void xf_rs(xf_t *o, const xf_t *a, v3 t, float yaw, float pitch, float roll, v3 s)
{
    xf_t b;
    xf_make(&b, t, yaw, pitch, roll, s);
    xf_mul(o, a, &b);
}
// the robot of the rig r: who 0 = CUBBY of this board, 1 = the robot of the friend (its own colours and light slots)
static void robot_draw_who(const rig_t *r, int who)
{
    xf_t root, x, body, head, eye, w;
    const int sb = who ? 40 : 0;
    // (far away: no small parts; they are a few px there)
    const bool far = xf_pt(&C.w2c, r->p).z > 10.0f;
    xf_make(&root, r->p, r->yaw, r->pitch, r->roll, V3(1, 1, 1));
    float tn = r->tint, fl = r->flash;
    // the treads and their cleats
    for (int side = 0; side < 2; side++) {
        float sx = side ? TX : -TX;
        xf_tr(&x, &root, V3(sx, 0, 0));
        dl_model_s(&s_mod[M_TREAD], &x, tn, fl, sb + side);
        float ph = side ? r->tread_r : r->tread_l;
        for (int k = 0; k < N_CLEAT && !far; k++) {
            float z, y, nz, ny;
            tread_at(ph + T_LOOP * k / N_CLEAT, &z, &y, &nz, &ny);
            if (ny < -0.5f) {
                continue;  // (the cleats under the tread do not show)
            }
            // (the turn about x that takes +y to the normal (0, ny, nz) of the loop: cos = ny, sin = nz)
            xf_t b;
            const float m9[9] = {1, 0, 0, 0, ny, -nz, 0, nz, ny};
            memcpy(b.r, m9, sizeof m9);
            b.t = V3(0, y + ny * 0.018f, z + nz * 0.018f);
            xf_mul(&w, &x, &b);
            dl_model_s(&s_mod[M_CLEAT], &w, tn, fl, sb + 2 + side * N_CLEAT + k);
        }
    }
    // the body (the squash keeps the volume: low and wide)
    float sq = r->squash, sw = frsqrt(sq > 0.3f ? sq : 0.3f);
    xf_rs(&x, &root, V3(0, TH + r->bob, 0), 0, r->body_pitch, 0, V3(sw, sq, sw));
    xf_tr(&body, &x, V3(0, 0.30f, -0.02f));
    dl_model_s(&s_mod[who ? M_BODY_F : M_BODY], &body, tn, fl, sb + 20);
    // the hatch (the hinge at its bottom edge, at the front)
    xf_rs(&w, &body, V3(0, -0.24f, 0.315f), 0, r->hatch * 1.6f, 0, V3(1, 1, 1));
    dl_model_s(&s_mod[who ? M_HATCH_F : M_HATCH], &w, tn, fl, sb + 21);
    // the lights of the panel: the colour of the cube in the body
    for (int k = 0; k < 3 && !far; k++) {
        int m = r->gauge == CB_TIN      ? M_LAMP_TIN
                : r->gauge == CB_SPRING ? M_LAMP_SPRING
                : r->gauge == CB_FLOAT  ? M_LAMP_FLOAT
                                        : M_LAMP_TIN;
        xf_tr(&w, &body, V3(-0.12f + k * 0.12f, 0.10f, 0.332f));
        dl_model(&s_mod[m], &w, r->gauge ? tn : 0.22f, fl);
    }
    // the arms (the shoulders at the sides of the body) and the claws
    for (int side = 0; side < 2; side++) {
        float sx = side ? 0.385f : -0.385f, a = side ? r->arm_r : r->arm_l;
        xf_rs(&x, &body, V3(sx, 0.02f, 0.06f), 0, -a, 0, V3(1, 1, 1));
        dl_model_s(&s_mod[M_ARM], &x, tn, fl, sb + 22 + side);
        for (int f = 0; f < 2 && !far; f++) {
            float sp = (f ? 1 : -1) * (0.15f + r->claw * 0.5f);
            xf_rs(&w, &x, V3(0, -0.28f, f ? 0.025f : -0.025f), 0, -sp, 0, V3(1, 1, 1));
            dl_model_s(&s_mod[M_FINGER], &w, tn, fl, sb + 24 + side * 2 + f);
        }
    }
    // the cube in the arms
    if (r->carry) {
        // (over the head: the camera behind the robot sees it; it comes out of the front, then goes up)
        float lift = clampf((r->arm_l - 1.4f) / 1.5f, 0, 1);
        float cs = r->carry_s * (0.82f - 0.06f * lift);
        xf_rs(&w, &body, V3(0, -0.02f + 1.16f * lift, 0.62f - 0.56f * lift), 0, 0, 0, V3(cs, cs, cs));
        draw_cube_model(r->carry, &w, 1, tn, fl, 0);
    }
    // the neck and the head
    xf_rs(&x, &body, V3(0, 0.30f, 0.03f), 0, 0, 0, V3(1, r->neck, 1));
    dl_model_s(&s_mod[M_NECK], &x, tn, fl, sb + 28);
    xf_rs(&head, &body, V3(0, 0.30f + 0.26f * r->neck, 0.03f), r->head_yaw, r->head_pitch, r->head_roll, V3(1, 1, 1));
    dl_model_s(&s_mod[M_YOKE], &head, tn, fl, sb + 29);
    for (int side = 0; side < 2; side++) {
        float sx = side ? 0.185f : -0.185f, sg = side ? 1.0f : -1.0f;
        xf_rs(&eye, &head, V3(sx, 0.09f, 0.02f), 0, 0, -sg * r->eye_roll, V3(1, 1, 1));
        dl_model_s(&s_mod[M_EYE], &eye, tn, fl, sb + 30 + side);
        if (far) {
            continue;
        }
        float op = 1 - r->blink * 0.92f;
        xf_rs(&w, &eye, V3(0, 0, 0), 0, 0, 0, V3(1, op, 1));
        dl_model_s(&s_mod[M_LENS], &w, tn, fl, sb + 32 + side);
        float ir = r->iris;
        xf_rs(&w, &eye, V3(0, 0, 0), 0, 0, 0, V3(ir, ir * op, 1));
        dl_model_s(&s_mod[M_IRIS], &w, 1, fl, sb + 34 + side);
        float br = side ? r->brow_r : r->brow_l;
        xf_rs(&w, &eye, V3(0, 0.188f + r->brow_up - r->blink * 0.06f, 0.03f), 0, 0, sg * br, V3(1, 1, 1));
        dl_model_s(&s_mod[M_BROW], &w, tn, fl, sb + 36 + side);
    }
}

#define robot_draw(r) robot_draw_who(r, 0)
// a cube of a kind in the frame x: coil = the height of the spring (1 = free), t = the time (the balloons bob)
static void draw_cube_model(int kind, const xf_t *x, float coil, float tint, float flash, float t)
{
    xf_t w;
    // (far away: the models with fewer triangles)
    bool far = xf_pt(&C.w2c, x->t).z > 9.0f;
    if (kind == CB_TIN) {
        dl_model(&s_mod[far ? M_CUBE_TIN_LO : M_CUBE_TIN], x, tint, flash);
    } else if (kind == CB_SPRING) {
        dl_model(&s_mod[M_SPRING_BASE], x, tint, flash);
        xf_rs(&w, x, V3(0, -0.02f, 0), 0, 0, 0, V3(1, coil, 1));
        dl_model(&s_mod[far ? M_COIL_LO : M_SPRING_COIL], &w, tint, flash);
    } else if (kind == CB_FLOAT) {
        dl_model(&s_mod[M_FLOAT_BOX], x, tint, flash);
        static const int BM[3] = {M_BALLOON_P, M_BALLOON_Y, M_BALLOON_G};
        for (int k = 0; k < 3; k++) {
            float a = k * 2.1f + 0.4f;
            float bx = fcos(a) * 0.28f + fsin(t * 1.3f + k) * 0.04f, bz = fsin(a) * 0.28f,
                  by = 1.15f + 0.12f * k + fsin(t * 1.7f + k * 2) * 0.05f;
            xf_tr(&w, x, V3(bx, by, bz));
            dl_model(&s_mod[BM[k]], &w, tint, flash);
            xf_rs(&w, x, V3(bx, by - 0.24f, bz), 0, 0, 0, V3(1, (by - 0.24f - 0.47f), 1));
            dl_model(&s_mod[M_STRING], &w, tint, 0);
        }
    }
}
