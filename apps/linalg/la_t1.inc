// LINEAR ALGEBRA: the chapters of the basics: vectors, a matrix as a map, the determinant as an area, the rank and
// the kernel.

// a column of two numbers, "@m{a;b}"
static const char *V2(int64_t a, int64_t b)
{
    char *o = nb();
    char x[24], y[24];
    s_int(x, sizeof x, a);
    s_int(y, sizeof y, b);
    snprintf(o, 420, "@m{%s;%s}", x, y);
    return o;
}
static const char *V2Q(q_t a, q_t b)
{
    char *o = nb();
    char x[64], y[64];
    s_q(x, sizeof x, a, true);
    s_q(y, sizeof y, b, true);
    snprintf(o, 420, "@m{%s;%s}", x, y);
    return o;
}
// the screen angle of a vector of the plane (radians, the clock way as tfd_arc)
static float scr_ang(float x, float y)
{
    return atan2f(-y, x);
}
// an arc between two vectors at the origin (radius r px)
static void arc_between(float ux, float uy, float wx, float wy, float r, uint16_t c)
{
    float a0 = scr_ang(ux, uy), a1 = scr_ang(wx, wy);
    float d = a1 - a0;
    while (d > 3.14159265f) {
        d -= 6.2831853f;
    }
    while (d < -3.14159265f) {
        d += 6.2831853f;
    }
    if (d < 0) {
        float t = a0;
        a0 = a0 + d;
        a1 = t;
    } else {
        a1 = a0 + d;
    }
    tfd_arc(VX(0), VY(0), r, 1.6f, a0, a1, c, 220);
}

// ================================================================ 1 VECTORS
static struct {
    int u1, u2;
    q_t c;
    float e[8];  // the eases: u, cu, w (the picture)
} V1;
#define W1 (-1)
#define W2 2
static void vec_calc(void)
{
    V1.u1 = P[0];
    V1.u2 = P[1];
    V1.c = qn(P[2], 2);
}
static void vec_text(int st)
{
    int u1 = V1.u1, u2 = V1.u2;
    q_t c = V1.c;
    int dot = u1 * W1 + u2 * W2, n2 = u1 * u1 + u2 * u2;
    switch (st) {
    case 0:
        T(LG("#A vector\nAn arrow from the origin. Its two numbers are the steps along $x$ and along $y$.\n",
             "#וקטור\nחץ שיוצא מהראשית. שני המספרים שלו הם הצעדים לאורך $x$ ולאורך $y$.\n"));
        T("=@1{u} = @m{%s;%s}\n", PV(0), PV(1));
        T(LG("Change $u_1$ and $u_2$: the arrow follows.", "שנו את $u_1$ ואת $u_2$: החץ זז איתם."));
        break;
    case 1:
        T(LG("#Scale it\n$c@1{u}$ stays on the line of $@1{u}$ and is $|c|$ times as long. A negative $c$ turns it "
             "round.\n",
             "#כפל במספר\n$c@1{u}$ נשאר על הקו של $@1{u}$ וארוך פי $|c|$. מספר $c$ שלילי הופך את הכיוון.\n"));
        T("=c@1{u} = %s · %s = %s\n", PV(2), V2(u1, u2), V2Q(q_mul(c, qi(u1)), q_mul(c, qi(u2))));
        break;
    case 2:
        T(LG("#Add\nPut $@2{w}$ at the tip of $@1{u}$: the sum goes to the far corner of the parallelogram.\n",
             "#חיבור\nשמים את $@2{w}$ בקצה של $@1{u}$: הסכום מגיע לפינה הרחוקה של המקבילית.\n"));
        T("=@1{u} + @2{w} = %s + %s = @3{%s}\n", V2(u1, u2), V2(W1, W2), V2(u1 + W1, u2 + W2));
        break;
    case 3:
        T(LG("#The dot product\nMultiply the parts and add. Zero tells that the arrows are at a right angle.\n",
             "#המכפלה הסקלרית\nכופלים את החלקים ומחברים. אפס אומר שהחצים מאונכים.\n"));
        T("=@1{u}·@2{w} = %s·%s + %s·%s = @h{%s}\n", IP(u1), IP(W1), IP(u2), IP(W2), I(dot));
        if (dot == 0 && n2) {
            T(LG("Here it is zero: $@1{u} ⊥ @2{w}$.", "כאן היא אפס: $@1{u}$ מאונך ל-$@2{w}$."));
        }
        break;
    case 4: {
        T(LG("#Length and angle\nThe length comes from Pythagoras; the angle from the dot product.\n",
             "#אורך וזווית\nהאורך בא מפיתגורס, והזווית מהמכפלה הסקלרית.\n"));
        int64_t k, r;
        surd(n2, &k, &r);
        char root[48];
        if (r == 1) {
            snprintf(root, sizeof root, "%lld", (long long)k);
        } else if (k == 1) {
            snprintf(root, sizeof root, "@r{%lld}", (long long)r);
        } else {
            snprintf(root, sizeof root, "%lld@r{%lld}", (long long)k, (long long)r);
        }
        T("=|@1{u}| = @r{%s^2 + %s^2} = %s\n", IP(u1), IP(u2), root);
        if (n2 == 0) {
            T(LG("The zero vector has no direction: no angle.", "לווקטור האפס אין כיוון, ולכן אין זווית."));
        } else {
            float cs = dot / sqrtf((float)n2 * 5.0f);
            float deg = acosf(cs < -1 ? -1 : cs > 1 ? 1 : cs) * 57.29578f;
            T("=cos θ = @f{@1{u}·@2{w}}{|@1{u}||@2{w}|} = @f{%s}{@r{%d}@r{5}} ≈ %s,   θ ≈ %s°\n", I(dot), n2, F(cs, 3),
              F(deg, 1));
        }
        break;
    }
    default:
        T(LG("#Why it is beautiful\nOnly two rules: add, and multiply by a number. Arrows, sounds, pictures and "
             "polynomials obey the same two rules, so one theory covers them all.",
             "#למה זה יפה\nרק שני כללים: לחבר, ולכפול במספר. חצים, צלילים, תמונות ופולינומים מצייתים לאותם שני "
             "הכללים, ולכן תורה אחת מכסה את כולם."));
        break;
    }
}
static void vec_pic(rect_t r, int st, float t)
{
    view_set(r, 8.4f, 0, 0);
    grid_plain(5, rgb(44, 52, 84), 120);
    float c = q_f(V1.c);
    EZ(&V1.e[0], (float)V1.u1);
    EZ(&V1.e[1], (float)V1.u2);
    EZ(&V1.e[2], c);
    float ux = V1.e[0], uy = V1.e[1], cc = V1.e[2];
    if (s_mini || st < 0) {
        ux = (float)V1.u1;
        uy = (float)V1.u2;
        cc = c;
        st = 2;
    }
    float w = s_mini ? 1.6f : 2.4f;
    if (st == 5) {
        // all the mixes a u + b w: a lattice that the two arrows make
        for (int i = -4; i <= 4; i++) {
            for (int j = -4; j <= 4; j++) {
                float x = i * ux + j * W1, y = i * uy + j * W2;
                float ph = fsin(t * 2 - (i + j) * 0.5f) * 0.5f + 0.5f;
                dot(x, y, 1.6f + ph * 1.6f, ui_mix(COL_C4, COL_ACC, (int)(ph * 255)), 220);
            }
        }
        arrow(0, 0, ux, uy, COL_C1, w, 256);
        arrow(0, 0, W1, W2, COL_C2, w, 256);
        return;
    }
    if (st == 1) {
        seg(-ux * 6, -uy * 6, ux * 6, uy * 6, 1, COL_C1, 70);
        arrow(0, 0, ux, uy, rgb(60, 110, 140), w, 256);
        arrow(0, 0, cc * ux, cc * uy, COL_C1, w + 0.6f, 256);
        label(cc * ux, cc * uy, 0, -10, "c@1{u}", COL_TEXT);
        return;
    }
    if (st == 2) {
        float p[8] = {0, 0, ux, uy, ux + W1, uy + W2, W1, W2};
        fpoly(p, 4, COL_C3, 40);
        dashed(ux, uy, ux + W1, uy + W2, COL_C2, 200);
        dashed(W1, W2, ux + W1, uy + W2, COL_C1, 200);
        arrow(0, 0, ux + W1, uy + W2, COL_C3, w, 256);
    }
    arrow(0, 0, W1, W2, COL_C2, w, 256);
    arrow(0, 0, ux, uy, COL_C1, w, 256);
    label(ux, uy, 8, -8, "@1{u}", COL_TEXT);
    label(W1, W2, -8, -8, "@2{w}", COL_TEXT);
    if (st == 3 || st == 4) {
        if (V1.u1 * W1 + V1.u2 * W2 == 0 && (V1.u1 || V1.u2)) {
            // a right angle: a small square
            float L = 0.45f, a = sqrtf(ux * ux + uy * uy), b = sqrtf(5.0f);
            float p[8] = {
                    0,          0,         ux / a * L, uy / a * L, ux / a * L + W1 / b * L, uy / a * L + W2 / b * L,
                    W1 / b * L, W2 / b * L};
            fpoly(p, 4, COL_ACC, 120);
        } else if (V1.u1 || V1.u2) {
            arc_between(ux, uy, W1, W2, 16, COL_ACC);
        }
    }
    if (st == 4) {
        dashed(0, 0, ux, 0, COL_DIM, 200);
        dashed(ux, 0, ux, uy, COL_DIM, 200);
    }
    (void)t;
}
static const topic_t TP_VEC = {{"Vectors", "וקטורים"},
                               0,
                               3,
                               6,
                               {{"u_1", -3, 3, 2, 1}, {"u_2", -3, 3, 1, 1}, {"c", -6, 6, 3, 2}},
                               vec_calc,
                               vec_text,
                               vec_pic};

// ================================================================ 2 A MATRIX IS A MAP
static struct {
    int a, b, c, d;
    float e[4];
} M2;
static void map_calc(void)
{
    M2.a = P[0];
    M2.b = P[1];
    M2.c = P[2];
    M2.d = P[3];
}
static const char *A2(void)  // the matrix with the numbers that changed in the accent colour
{
    char *o = nb();
    snprintf(o, 420, "@m{%s&%s;%s&%s}", PV(0), PV(1), PV(2), PV(3));
    return o;
}
static void map_text(int st)
{
    int a = M2.a, b = M2.b, c = M2.c, d = M2.d;
    switch (st) {
    case 0:
        T(LG("#A matrix moves the plane\nEach point $x$ goes to $Ax$. The grid shows where the whole plane goes.\n",
             "#מטריצה מזיזה את המישור\nכל נקודה $x$ עוברת ל-$Ax$. הרשת מראה לאן עובר כל המישור.\n"));
        T("=A = %s\n", A2());
        break;
    case 1:
        T(LG("#The first column\nThe arrow $@1{e_1}$ goes to the first column of $A$.\n",
             "#העמודה הראשונה\nהחץ $@1{e_1}$ עובר לעמודה הראשונה של $A$.\n"));
        T("=A@1{e_1} = A@m{1;0} = @1{%s}\n", V2(a, c));
        break;
    case 2:
        T(LG("#The second column\nThe arrow $@2{e_2}$ goes to the second column.\n",
             "#העמודה השנייה\nהחץ $@2{e_2}$ עובר לעמודה השנייה.\n"));
        T("=A@2{e_2} = A@m{0;1} = @2{%s}\n", V2(b, d));
        break;
    case 3:
        T(LG("#Any point\n$x = x_1@1{e_1} + x_2@2{e_2}$, so $Ax$ is the same mix of the two columns.\n",
             "#כל נקודה\n$x = x_1@1{e_1} + x_2@2{e_2}$, ולכן $Ax$ הוא אותו צירוף של שתי העמודות.\n"));
        T("=A@3{@m{1;1}} = @1{%s} + @2{%s} = @3{%s}\n", V2(a, c), V2(b, d), V2(a + b, c + d));
        break;
    case 4:
        T(LG("#Lines stay lines\nThe grid goes to a grid of parallelograms, and the origin stays. That is what "
             "*linear* means:\n",
             "#ישרים נשארים ישרים\nהרשת עוברת לרשת של מקביליות, והראשית נשארת במקומה. זו המשמעות של *לינארי*:\n"));
        T("=A(x + y) = Ax + Ay,   A(cx) = cAx\n");
        break;
    case 5: {
        T(LG("#One map after another\nFirst $A$, then $R$, a turn by 90°: the product $RA$ does both.\n",
             "#העתקה אחרי העתקה\nקודם $A$, ואחר כך $R$, סיבוב ב-90°: המכפלה $RA$ עושה את שתיהן.\n"));
        int rm[4] = {-c, -d, a, b};
        T("=RA = @m{0&−1;1&0}%s = @m{%s&%s;%s&%s}\n", A2(), I(rm[0]), I(rm[1]), I(rm[2]), I(rm[3]));
        bool same = a == d && b == -c;
        T(same ? LG("Here $AR = RA$: this $A$ turns and stretches as $R$ does.",
                    "כאן $AR = RA$: המטריצה הזו מסובבת ומותחת כמו $R$.")
               : LG("The order counts: here $AR ≠ RA$.", "הסדר חשוב: כאן $AR ≠ RA$."));
        break;
    }
    default:
        T(LG("#Why it is beautiful\nA whole map of the plane fits in four numbers, and one map after another is "
             "just a product of matrices.",
             "#למה זה יפה\nהעתקה של כל המישור נכנסת בארבעה מספרים, והעתקה אחרי העתקה היא פשוט מכפלה של מטריצות."));
        break;
    }
}
// the grid of a 2 x 2 map that grows from the identity (k = 0) to the map (k = 1)
static void map_grid(float a, float b, float c, float d, float k, bool square)
{
    float A = 1 + (a - 1) * k, B = b * k, C = c * k, D = 1 + (d - 1) * k;
    grid_plain(5, rgb(40, 46, 74), 90);
    grid_map(A, B, C, D, 6, rgb(80, 110, 190), 110);
    if (square) {
        float p[8] = {0, 0, A, C, A + B, C + D, B, D};
        fpoly(p, 4, COL_C4, 70);
    }
    float w = s_mini ? 1.6f : 2.4f;
    arrow(0, 0, A, C, COL_C1, w, 256);
    arrow(0, 0, B, D, COL_C2, w, 256);
}
static void map_pic(rect_t r, int st, float t)
{
    view_set(r, 9.0f, 0, 0);
    EZ(&M2.e[0], (float)M2.a);
    EZ(&M2.e[1], (float)M2.b);
    EZ(&M2.e[2], (float)M2.c);
    EZ(&M2.e[3], (float)M2.d);
    float a = M2.e[0], b = M2.e[1], c = M2.e[2], d = M2.e[3];
    if (s_mini || st < 0) {
        float k = 0.5f + 0.5f * fsin(t * 1.2f);
        map_grid((float)M2.a, (float)M2.b, (float)M2.c, (float)M2.d, k, true);
        return;
    }
    float k = ease_io((s_now - s_step_t) / 1200.0f);
    if (st != 0) {
        k = 1;
    }
    if (st == 5) {
        // A, then the turn: R A = [-c -d; a b]
        float q = ease_io((s_now - s_step_t - 500) / 1200.0f);
        float th = q * 1.5707963f, cs = fcos(th), sn = fsin(th);
        float A = cs * a - sn * c, B = cs * b - sn * d, C = sn * a + cs * c, D = sn * b + cs * d;
        map_grid(A, B, C, D, 1, true);
        return;
    }
    map_grid(a, b, c, d, k, true);
    if (st == 1) {
        label(a, c, 8, -8, "A@1{e_1}", COL_TEXT);
    }
    if (st == 2) {
        label(b, d, 8, -8, "A@2{e_2}", COL_TEXT);
    }
    if (st == 3) {
        dashed(a, c, a + b, c + d, COL_C2, 200);
        dashed(b, d, a + b, c + d, COL_C1, 200);
        dot(1, 1, 3, rgb(90, 120, 90), 256);
        dot(a + b, c + d, 4, COL_C3, 256);
    }
}
static const topic_t TP_MAP = {{"A matrix is a map", "מטריצה היא העתקה"},
                               0,
                               4,
                               7,
                               {{"a", -3, 3, 2, 1}, {"b", -3, 3, -1, 1}, {"c", -3, 3, 1, 1}, {"d", -3, 3, 1, 1}},
                               map_calc,
                               map_text,
                               map_pic};

// ================================================================ 3 THE DETERMINANT IS AN AREA
static struct {
    int a, b, c, d, det;
    float e[4];
} D3;
static void det_calc(void)
{
    D3.a = P[0];
    D3.b = P[1];
    D3.c = P[2];
    D3.d = P[3];
    D3.det = D3.a * D3.d - D3.b * D3.c;
}
static void det_text(int st)
{
    int a = D3.a, b = D3.b, c = D3.c, d = D3.d, det = D3.det;
    switch (st) {
    case 0:
        T(LG("#The area factor\nThe unit square goes to the parallelogram of the two columns. Its area is $|det A|$.\n",
             "#מקדם השטח\nריבוע היחידה עובר למקבילית של שתי העמודות. השטח שלה הוא $|det A|$.\n"));
        T("=det A = @d{%s&%s;%s&%s} = %s·%s − %s·%s = @h{%s}\n", PV(0), PV(1), PV(2), PV(3), IP(a), IP(d), IP(b), IP(c),
          I(det));
        break;
    case 1:
        T(LG("#The sign\n", "#הסימן\n"));
        if (det > 0) {
            T(LG("$det A > 0$: the map keeps the turn of the plane. The way from $@1{Ae_1}$ to $@2{Ae_2}$ still goes "
                 "against the clock.",
                 "$det A > 0$: ההעתקה שומרת על כיוון הסיבוב של המישור. הדרך מ-$@1{Ae_1}$ ל-$@2{Ae_2}$ עדיין נגד "
                 "כיוון השעון."));
        } else if (det < 0) {
            T(LG("$det A < 0$: the map turns the plane over, as a mirror does. The way from $@1{Ae_1}$ to "
                 "$@2{Ae_2}$ now goes with the clock.",
                 "$det A < 0$: ההעתקה הופכת את המישור, כמו מראה. הדרך מ-$@1{Ae_1}$ ל-$@2{Ae_2}$ הולכת עכשיו עם "
                 "כיוון השעון."));
        } else {
            T(LG("$det A = 0$: the parallelogram is flat. There is no turn at all.",
                 "$det A = 0$: המקבילית שטוחה. אין סיבוב בכלל."));
        }
        T("\n=det A = %s\n", I(det));
        break;
    case 2:
        T(LG("#Zero\nWhen $det A = 0$ the plane falls onto a line or a point. No map can bring it back: $A$ has no "
             "inverse.\n",
             "#אפס\nכאשר $det A = 0$ המישור נופל לקו או לנקודה. שום העתקה לא מחזירה אותו: ל-$A$ אין הופכית.\n"));
        T(det ? LG("Now $det A = %s$: set $ad = bc$ to see it.", "עכשיו $det A = %s$: קבעו $ad = bc$ כדי לראות את זה.")
              : LG("Now $det A = 0$: the columns are on one line.", "עכשיו $det A = 0$: העמודות על קו אחד."),
          I(det));
        break;
    case 3: {
        T(LG("#The inverse\nWhen $det A ≠ 0$ there is a formula:\n", "#ההופכית\nכאשר $det A ≠ 0$ יש נוסחה:\n"));
        T("=A^{-1} = @f{1}{det A}@m{d&−b;−c&a}\n");
        if (det) {
            qm_t M;
            int v[4] = {d, -b, -c, a};
            qm_ints(&M, 2, 2, v);
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    M.a[i][j] = q_div(M.a[i][j], qi(det));
                }
            }
            T("=A^{-1} = %s\n", MQ(&M));
        } else {
            T(LG("Here $det A = 0$: no inverse.", "כאן $det A = 0$: אין הופכית."));
        }
        break;
    }
    case 4: {
        T(LG("#Products\nThe area factors multiply: $det(AB) = det A · det B$. With the stretch $B$:\n",
             "#מכפלות\nמקדמי השטח נכפלים: $det(AB) = det A · det B$. עם המתיחה $B$:\n"));
        int ab[4] = {2 * a, b, 2 * c, d};
        T("=@m{%s&%s;%s&%s}@m{2&0;0&1} = @m{%s&%s;%s&%s}\n", I(a), I(b), I(c), I(d), I(ab[0]), I(ab[1]), I(ab[2]),
          I(ab[3]));
        T("=det(AB) = %s = %s·2\n", I(2 * det), IP(det));
        break;
    }
    case 5:
        T(LG("#In 3D: the volume\nThree columns make a box, and its volume is the determinant. A block form gives it "
             "at once:\n",
             "#בתלת-ממד: נפח\nשלוש עמודות יוצרות תיבה, והנפח שלה הוא הדטרמיננטה. צורת בלוקים נותנת אותו מיד:\n"));
        T("=@d{%s&%s&0;%s&%s&0;0&0&2} = 2(ad − bc) = %s\n", PV(0), PV(1), PV(2), PV(3), I(2 * det));
        break;
    default:
        T(LG("#Why it is beautiful\nOne number tells if $Ax = b$ has exactly one answer, how areas and volumes "
             "change, and if the map turns space over. The change of variables in an integral uses it too.",
             "#למה זה יפה\nמספר אחד אומר אם ל-$Ax = b$ יש פתרון יחיד, איך שטחים ונפחים משתנים, ואם ההעתקה הופכת "
             "את המרחב. גם החלפת משתנים באינטגרל משתמשת בו."));
        break;
    }
}
static void det_pic(rect_t r, int st, float t)
{
    view_set(r, 9.0f, 0, 0);
    EZ(&D3.e[0], (float)D3.a);
    EZ(&D3.e[1], (float)D3.b);
    EZ(&D3.e[2], (float)D3.c);
    EZ(&D3.e[3], (float)D3.d);
    float a = D3.e[0], b = D3.e[1], c = D3.e[2], d = D3.e[3];
    if (s_mini || st < 0) {
        a = (float)D3.a;
        b = (float)D3.b;
        c = (float)D3.c;
        d = (float)D3.d;
    }
    float det = a * d - b * c;
    if (st == 4) {
        // the stretch B first: the first column doubles
        float k = ease_io((s_now - s_step_t - 300) / 1000.0f);
        a *= 1 + k;
        c *= 1 + k;
        det = a * d - b * c;
    }
    grid_plain(5, rgb(40, 46, 74), 100);
    float sq[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    fpoly(sq, 4, rgb(120, 130, 170), 50);
    float p[8] = {0, 0, a, c, a + b, c + d, b, d};
    uint16_t fc = det > 0.05f ? COL_C1 : det < -0.05f ? COL_C2 : COL_GREY;
    float pulse = s_mini ? 0 : 0.15f * fsin(t * 3);
    fpoly(p, 4, fc, (int)(90 + 40 * pulse));
    float w = s_mini ? 1.6f : 2.4f;
    seg(a, c, a + b, c + d, 1, fc, 220);
    seg(b, d, a + b, c + d, 1, fc, 220);
    arrow(0, 0, a, c, COL_C1, w, 256);
    arrow(0, 0, b, d, COL_C2, w, 256);
    if (st == 1 && (fabsf(a) + fabsf(c)) > 0.1f && (fabsf(b) + fabsf(d)) > 0.1f) {
        arc_between(a, c, b, d, 18, COL_ACC);
    }
    char lab[40];
    snprintf(lab, sizeof lab, "%s", I(st == 4 ? 2 * D3.det : D3.det));
    label((a + b) / 2, (c + d) / 2, 0, 5, lab, COL_TEXT);
}
static const topic_t TP_DET = {{"Determinant = area", "דטרמיננטה = שטח"},
                               0,
                               4,
                               7,
                               {{"a", -3, 3, 3, 1}, {"b", -3, 3, 1, 1}, {"c", -3, 3, 1, 1}, {"d", -3, 3, 2, 1}},
                               det_calc,
                               det_text,
                               det_pic};

// ================================================================ 4 RANK AND KERNEL
static struct {
    qm_t A, Rr;
    int rank, piv[3];
    float ker[2][3];  // a basis of the kernel (nk vectors)
    int nk;
    q_t kq[2][3];
} K4;
static void rank_calc(void)
{
    int p = P[0], q = P[1];
    int v[9] = {1, 2, 3, 2, 4, 6 + p, 1, 2 + q, 3};
    qm_ints(&K4.A, 3, 3, v);
    K4.Rr = K4.A;
    qm_rref(&K4.Rr, &K4.rank, K4.piv);
    // the kernel: one vector for each free column
    K4.nk = 0;
    for (int f = 0; f < 3; f++) {
        if (K4.piv[f]) {
            continue;
        }
        q_t x[3] = {qi(0), qi(0), qi(0)};
        x[f] = qi(1);
        int r = 0;
        for (int c = 0; c < 3; c++) {
            if (K4.piv[c]) {
                x[c] = q_neg(K4.Rr.a[r][f]);
                r++;
            }
        }
        for (int i = 0; i < 3; i++) {
            K4.kq[K4.nk][i] = x[i];
            K4.ker[K4.nk][i] = q_f(x[i]);
        }
        K4.nk++;
    }
}
static const char *V3Q(const q_t *x)
{
    char *o = nb();
    char a[48], b[48], c[48];
    s_q(a, sizeof a, x[0], true);
    s_q(b, sizeof b, x[1], true);
    s_q(c, sizeof c, x[2], true);
    snprintf(o, 420, "@m{%s;%s;%s}", a, b, c);
    return o;
}
static void rank_text(int st)
{
    int r = K4.rank;
    switch (st) {
    case 0:
        T(LG("#Rank\nThe rank is the number of independent columns: the dimension of all the outputs $Ax$ (the "
             "column space).\n",
             "#דרגה\nהדרגה היא מספר העמודות הבלתי תלויות: המימד של כל הפלטים $Ax$ (מרחב העמודות).\n"));
        T("=A = @m{1&2&3;2&4&6+p;1&2+q&3} = @m{1&2&3;2&4&%s;1&%s&3}\n", HV(0, 6 + P[0]), HV(1, 2 + P[1]));
        break;
    case 1:
        T(LG("#Row reduce\nRow operations do not change the rank. The pivots (the first 1 of each row) count it.\n",
             "#דירוג\nפעולות שורה לא משנות את הדרגה. הצירים (ה-1 הראשון בכל שורה) סופרים אותה.\n"));
        T("=A → %s,   @t{rank} A = @h{%d}\n", MQ(&K4.Rr), r);
        break;
    case 2:
        T(LG("#The picture\n", "#התמונה\n"));
        if (r == 1) {
            T(LG("All the outputs are on one line: the other columns are multiples of the first.",
                 "כל הפלטים על קו אחד: שאר העמודות הן כפולות של הראשונה."));
        } else if (r == 2) {
            T(LG("All the outputs are on one plane: one column is a mix of the other two.",
                 "כל הפלטים על מישור אחד: עמודה אחת היא צירוף של שתי האחרות."));
        } else {
            T(LG("The outputs fill all of space: the three columns are independent.",
                 "הפלטים ממלאים את כל המרחב: שלוש העמודות בלתי תלויות."));
        }
        T(LG(" Make $p$ or $q$ zero, or both, and watch the rank fall.",
             " שימו אפס ב-$p$ או ב-$q$, או בשניהם, וראו את הדרגה יורדת."));
        break;
    case 3:
        T(LG("#The kernel\nAll the $x$ with $Ax = 0$: the map crushes these directions to the origin.\n",
             "#הגרעין\nכל ה-$x$ שעבורם $Ax = 0$: ההעתקה מועכת את הכיוונים האלה לראשית.\n"));
        if (K4.nk == 0) {
            T("=@t{ker} A = @{0@}\n");
            T(LG("Only $x = 0$: nothing is crushed.", "רק $x = 0$: שום דבר לא נמעך."));
        } else if (K4.nk == 1) {
            T("=@t{ker} A = @t{span} @4{%s}\n", V3Q(K4.kq[0]));
        } else {
            T("=@t{ker} A = @t{span} @4{%s}, @4{%s}\n", V3Q(K4.kq[0]), V3Q(K4.kq[1]));
        }
        break;
    case 4:
        T(LG("#Rank plus nullity\nEach of the 3 dimensions of the input is kept or crushed:\n",
             "#דרגה ועוד אפסות\nכל אחד מ-3 המימדים של הקלט נשמר או נמעך:\n"));
        T("=@t{rank} A + @t{dim ker} A = %d + %d = 3\n", r, K4.nk);
        break;
    default:
        T(LG("#Why it is beautiful\nThe rank of the rows is always the rank of the columns, which is far from "
             "obvious. And $Ax = b$ has an answer exactly when $b$ is in the column space.",
             "#למה זה יפה\nהדרגה של השורות תמיד שווה לדרגה של העמודות, וזה רחוק מלהיות מובן מאליו. ול-$Ax = b$ יש "
             "פתרון בדיוק כאשר $b$ במרחב העמודות."));
        break;
    }
}
static void rank_pic(rect_t r, int st, float t)
{
    view_set(r, 5.2f, 0, -0.2f);
    s_yaw = 0.5f + t * 0.35f;
    s_pitch = 0.38f;
    axes3(2.4f);
    // the columns, each scaled into the view (their directions are what counts)
    float col[3][3], mx = 0;
    for (int j = 0; j < 3; j++) {
        float L = 0;
        for (int i = 0; i < 3; i++) {
            col[j][i] = q_f(K4.A.a[i][j]);
            L += col[j][i] * col[j][i];
        }
        L = sqrtf(L);
        if (L > mx) {
            mx = L;
        }
    }
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            col[j][i] *= 2.3f / mx;
        }
    }
    uint16_t cc[3] = {COL_C1, COL_C2, COL_C3};
    // the column space: a line, a plane, or all of space (a soft cube)
    int pv[3], np = 0;
    for (int j = 0; j < 3; j++) {
        if (K4.piv[j]) {
            pv[np++] = j;
        }
    }
    if (K4.rank == 1) {
        float a[3], b[3];
        for (int i = 0; i < 3; i++) {
            a[i] = -col[0][i] * 1.4f;
            b[i] = col[0][i] * 1.4f;
        }
        seg3(a, b, 3.0f, COL_C1, 70);
    } else if (K4.rank == 2) {
        // an orthogonal frame of the plane of the two pivot columns
        float *u = col[pv[0]], *v = col[pv[1]], uu[3], vv[3];
        float lu = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        for (int i = 0; i < 3; i++) {
            uu[i] = u[i] / lu;
        }
        float dp = uu[0] * v[0] + uu[1] * v[1] + uu[2] * v[2];
        for (int i = 0; i < 3; i++) {
            vv[i] = v[i] - dp * uu[i];
        }
        float lv = sqrtf(vv[0] * vv[0] + vv[1] * vv[1] + vv[2] * vv[2]);
        for (int i = 0; i < 3; i++) {
            uu[i] *= 2.4f;
            vv[i] = vv[i] / lv * 2.4f;
        }
        plane3(uu, vv, COL_C1, 50);
    } else {
        const float e[3][3] = {{1.6f, 0, 0}, {0, 1.6f, 0}, {0, 0, 1.6f}};
        plane3(e[0], e[1], COL_C3, 22);
        plane3(e[1], e[2], COL_C3, 22);
        plane3(e[0], e[2], COL_C3, 22);
    }
    // the kernel (violet)
    if (st == 3 || st == 4 || s_mini || st < 0) {
        if (K4.nk == 1) {
            float a[3], b[3], L = 0;
            for (int i = 0; i < 3; i++) {
                L += K4.ker[0][i] * K4.ker[0][i];
            }
            L = sqrtf(L);
            for (int i = 0; i < 3; i++) {
                a[i] = -K4.ker[0][i] / L * 2.6f;
                b[i] = K4.ker[0][i] / L * 2.6f;
            }
            seg3(a, b, 2.4f, COL_C4, 230);
        } else if (K4.nk == 2) {
            float u[3], v[3], lu = 0, lv = 0;
            for (int i = 0; i < 3; i++) {
                lu += K4.ker[0][i] * K4.ker[0][i];
                lv += K4.ker[1][i] * K4.ker[1][i];
            }
            lu = sqrtf(lu);
            lv = sqrtf(lv);
            for (int i = 0; i < 3; i++) {
                u[i] = K4.ker[0][i] / lu * 1.9f;
                v[i] = K4.ker[1][i] / lv * 1.9f;
            }
            plane3(u, v, COL_C4, 70);
        }
    }
    for (int j = 0; j < 3; j++) {
        arrow3(col[j], cc[j], s_mini ? 1.4f : 2.2f);
    }
}
static const topic_t TP_RANK = {{"Rank and kernel", "דרגה וגרעין"},       0,         2,         6,
                                {{"p", -2, 2, 0, 1}, {"q", -2, 2, 0, 1}}, rank_calc, rank_text, rank_pic};
