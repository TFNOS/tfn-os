// LINEAR ALGEBRA: the spectral chapters: eigenvectors, the characteristic polynomial, diagonalization, symmetric
// matrices and the spectral theorem.

// the eigenvalues of a 2 x 2 integer matrix [a b; c d]: tr, det, the discriminant
typedef struct {
    int tr, det, D;      // D = tr^2 - 4 det
    float l1, l2;        // the real values (l1 >= l2), or the real part (D < 0)
    float im;            // the imaginary part (D < 0)
    float v1[2], v2[2];  // unit eigenvectors (D > 0; D = 0: v1 only, v2 = v1)
    bool one_line;       // D = 0 and the matrix is not a multiple of I
    bool scalar;         // the matrix is a multiple of I
} eig2_t;
static void unit2(float *v, float x, float y)
{
    float L = sqrtf(x * x + y * y);
    if (L < 1e-6f) {
        v[0] = 1;
        v[1] = 0;
        return;
    }
    v[0] = x / L;
    v[1] = y / L;
    // the sign rule of the pictures: the larger part positive
    if ((fabsf(v[0]) >= fabsf(v[1]) && v[0] < 0) || (fabsf(v[1]) > fabsf(v[0]) && v[1] < 0)) {
        v[0] = -v[0];
        v[1] = -v[1];
    }
}
// an eigenvector of [a b; c d] for the value l
static void eigvec2(int a, int b, int c, int d, float l, float *v)
{
    if (b != 0 || fabsf(l - a) > 1e-6f) {
        if (b != 0) {
            unit2(v, (float)b, l - a);
            return;
        }
    }
    if (c != 0) {
        unit2(v, l - d, (float)c);
        return;
    }
    // a diagonal matrix: the axes
    if (fabsf(l - a) < 1e-6f) {
        unit2(v, 1, 0);
    } else {
        unit2(v, 0, 1);
    }
}
static void eig2(int a, int b, int c, int d, eig2_t *e)
{
    memset(e, 0, sizeof *e);
    e->tr = a + d;
    e->det = a * d - b * c;
    e->D = e->tr * e->tr - 4 * e->det;
    e->scalar = b == 0 && c == 0 && a == d;
    if (e->D >= 0) {
        float r = sqrtf((float)e->D);
        e->l1 = (e->tr + r) / 2;
        e->l2 = (e->tr - r) / 2;
        eigvec2(a, b, c, d, e->l1, e->v1);
        if (e->D > 0) {
            eigvec2(a, b, c, d, e->l2, e->v2);
        } else {
            e->v2[0] = e->v1[0];
            e->v2[1] = e->v1[1];
            e->one_line = !e->scalar;
        }
    } else {
        e->l1 = e->l2 = e->tr / 2.0f;
        e->im = sqrtf((float)-e->D) / 2;
    }
}
// the text of the two eigenvalues: exact when they are rational or a simple root, "λ = (5 ± √13)/2"
static const char *EIGS(const eig2_t *e, bool disp)
{
    char *o = nb();
    if (e->D == 0) {
        s_q(o, 420, qn(e->tr, 2), disp);
        return o;
    }
    s_pm_root(o, 420, e->tr, 1, e->D, 2, disp);
    return o;
}
// the largest stretch of [a b; c d] (the first singular value): the size of the view of a picture
static float smax2(float a, float b, float c, float d)
{
    float S = a * a + b * b + c * c + d * d, det = a * d - b * c;
    float q = S * S - 4 * det * det;
    return sqrtf((S + sqrtf(q > 0 ? q : 0)) / 2);
}

// ================================================================ 5 EIGENVECTORS
static struct {
    int a, b, c, d;
    eig2_t e;
    float ez[5];
} E5;
static void eig_calc(void)
{
    E5.a = P[0];
    E5.b = P[1];
    E5.c = P[2];
    E5.d = P[3];
    eig2(E5.a, E5.b, E5.c, E5.d, &E5.e);
}
// an eigenvector of [a b; c d] for λ = l2x / 2 (exact): small integers
static void eigvec_exact(int a, int b, int c, int d, int l2x, int64_t *x, int64_t *y)
{
    if (b != 0) {
        *x = 2 * b;
        *y = l2x - 2 * a;
    } else if (c != 0) {
        *x = l2x - 2 * d;
        *y = 2 * c;
    } else {
        *x = l2x == 2 * a ? 1 : 0;
        *y = l2x == 2 * a ? 0 : 1;
    }
    int64_t g = gcd64(*x, *y);
    if (g > 1) {
        *x /= g;
        *y /= g;
    }
    if (*x < 0 || (*x == 0 && *y < 0)) {
        *x = -*x;
        *y = -*y;
    }
}
// "λ1 = 3: x1 = [1; 1]" (exact when λ is rational, else rounded)
static void eig_vec_line(int k, float l, const float *v)
{
    const eig2_t *e = &E5.e;
    int64_t kk, rr;
    surd(e->D, &kk, &rr);
    if (rr == 1) {
        int l2x = (int)(k == 1 ? e->tr + kk : e->tr - kk);
        int64_t x, y;
        eigvec_exact(E5.a, E5.b, E5.c, E5.d, l2x, &x, &y);
        T("=@%d{λ_%d} = %s:   @%d{x_%d} = %s\n", k, k, QD(qn(l2x, 2)), k, k, V2(x, y));
    } else {
        char vv[96];
        snprintf(vv, sizeof vv, "@m{%s;%s}", F(v[0], 3), F(v[1], 3));
        T("=@%d{λ_%d} ≈ %s:   @%d{x_%d} ≈ %s\n", k, k, F(l, 3), k, k, vv);
    }
}
static void eig_text(int st)
{
    const eig2_t *e = &E5.e;
    switch (st) {
    case 0:
        T(LG("#Most arrows turn\nWatch $@1{x}$ go round the circle and $@2{Ax}$ follow it. Most of the time $@2{Ax}$ "
             "points in a new direction.\n",
             "#רוב החצים מסתובבים\nהסתכלו על $@1{x}$ שמסתובב על המעגל ועל $@2{Ax}$ שעוקב אחריו. ברוב הזמן $@2{Ax}$ "
             "מצביע לכיוון חדש.\n"));
        T("=A = %s\n", A2());
        break;
    case 1:
        T(LG("#Some arrows only stretch\nOn a few lines $Ax = λx$: the map only stretches or flips. These are the "
             "*eigenvectors*, and $λ$ is the *eigenvalue*.",
             "#יש חצים שרק נמתחים\nעל כמה קווים $Ax = λx$: ההעתקה רק מותחת או הופכת. אלה *הווקטורים העצמיים*, "
             "ו-$λ$ הוא *הערך העצמי*."));
        if (e->D < 0) {
            T(LG(" This $A$ has none: it turns every arrow.", " ל-$A$ הזו אין כאלה: היא מסובבת כל חץ."));
        } else if (e->scalar) {
            T(LG(" This $A$ stretches every arrow the same: all are eigenvectors.",
                 " ה-$A$ הזו מותחת כל חץ באותה מידה: כולם וקטורים עצמיים."));
        }
        break;
    case 2:
        T(LG("#The equation\n$Ax = λx$ means $(A − λI)x = 0$ with $x ≠ 0$. That needs a matrix with no inverse:\n",
             "#המשוואה\n$Ax = λx$ פירושו $(A − λI)x = 0$ עם $x ≠ 0$. לשם כך המטריצה צריכה להיות בלי הופכית:\n"));
        T("=det(A − λI) = @d{%s − λ&%s;%s&%s − λ} = 0\n", I(E5.a), I(E5.b), I(E5.c), I(E5.d));
        break;
    case 3:
        T(LG("#Solve it\nFor a 2 × 2 matrix this is a quadratic in $λ$:\n",
             "#פותרים\nעבור מטריצה 2 × 2 זו משוואה ריבועית ב-$λ$:\n"));
        T("=λ^2 − %sλ + %s = 0\n", IP(e->tr), IP(e->det));
        T("=λ = @f{%s ± %s}{2} = @h{%s}\n", I(e->tr), ROOT(e->D), EIGS(e, true));
        break;
    case 4:
        T(LG("#The vectors\nPut each $λ$ back into $(A − λI)x = 0$:\n",
             "#הווקטורים\nמציבים כל $λ$ בחזרה ב-$(A − λI)x = 0$:\n"));
        if (e->D < 0) {
            T(LG("Here $λ$ is complex: the vectors are complex too, and no real line stays.",
                 "כאן $λ$ מרוכב: גם הווקטורים מרוכבים, ושום קו ממשי לא נשאר במקומו."));
        } else if (e->scalar) {
            T(LG("$A = λI$: every vector is an eigenvector.", "$A = λI$: כל וקטור הוא וקטור עצמי."));
        } else if (e->D == 0) {
            eig_vec_line(1, e->l1, e->v1);
            T(LG("One value twice, but only one line: this map is a shear.",
                 "ערך אחד פעמיים, אבל קו אחד בלבד: ההעתקה הזו היא גזירה."));
        } else {
            eig_vec_line(1, e->l1, e->v1);
            eig_vec_line(2, e->l2, e->v2);
        }
        break;
    case 5:
        T(LG("#Two checks\nThe sum of the eigenvalues is the trace, and their product is the determinant:\n",
             "#שתי בדיקות\nסכום הערכים העצמיים הוא העקבה, והמכפלה שלהם היא הדטרמיננטה:\n"));
        T("=λ_1 + λ_2 = @t{tr} A = %s,   λ_1 λ_2 = det A = %s\n", I(e->tr), I(e->det));
        if (e->D < 0) {
            T(LG("Complex values come as a pair $p ± qi$: the map turns by an angle and stretches.",
                 "ערכים מרוכבים באים בזוג $p ± qi$: ההעתקה מסתובבת בזווית ומותחת."));
        }
        break;
    default:
        T(LG("#Why it is beautiful\nOn its eigenvectors the map is only a stretch. Vibrations, quantum states, the "
             "ranking of web pages and the stability of a system are all questions about these lines.",
             "#למה זה יפה\nעל הווקטורים העצמיים ההעתקה היא רק מתיחה. רעידות, מצבים קוונטיים, דירוג של דפי אינטרנט "
             "ויציבות של מערכת הם כולם שאלות על הקווים האלה."));
        break;
    }
}
static void eig_pic(rect_t r, int st, float t)
{
    float a = (float)E5.a, b = (float)E5.b, c = (float)E5.c, d = (float)E5.d;
    float span = 2.4f * (smax2(a, b, c, d) > 1.2f ? smax2(a, b, c, d) : 1.2f) + 0.6f;
    EZ(&E5.ez[0], span);
    if (s_mini || st < 0) {
        E5.ez[0] = span;
    }
    view_set(r, E5.ez[0], 0, 0);
    grid_plain(8, rgb(36, 42, 68), 90);
    // the circle and its image
    ellipse(1, 0, 0, 1, 0, 0, 1, rgb(90, 100, 140), 200);
    ellipse(a, b, c, d, 0, 0, 1, COL_C4, 150);
    const eig2_t *e = &E5.e;
    float L = E5.ez[0];
    if (e->D >= 0 && !e->scalar) {
        dashed(-e->v1[0] * L, -e->v1[1] * L, e->v1[0] * L, e->v1[1] * L, COL_C3, 200);
        if (e->D > 0) {
            dashed(-e->v2[0] * L, -e->v2[1] * L, e->v2[0] * L, e->v2[1] * L, COL_ACC, 200);
        }
    }
    // x goes round; Ax follows; a glow when Ax is on the line of x
    float th = t * 0.7f, x = fcos(th), y = fsin(th);
    float ax = a * x + b * y, ay = c * x + d * y;
    float cr = fabsf(x * ay - y * ax), n = sqrtf(ax * ax + ay * ay);
    bool on = n < 1e-3f || cr / (n + 1e-6f) < 0.06f;
    float w = s_mini ? 1.4f : 2.2f;
    if (on && !s_mini) {
        tfd_glow(VX(ax), VY(ay), 16, COL_ACC, 200);
    }
    arrow(0, 0, ax, ay, COL_C2, w, 256);
    arrow(0, 0, x, y, COL_C1, w, 256);
}
static const topic_t TP_EIG = {{"Eigenvectors", "וקטורים עצמיים"},
                               1,
                               4,
                               7,
                               {{"a", -3, 3, 2, 1}, {"b", -3, 3, 1, 1}, {"c", -3, 3, 1, 1}, {"d", -3, 3, 2, 1}},
                               eig_calc,
                               eig_text,
                               eig_pic};

// ================================================================ 6 THE CHARACTERISTIC POLYNOMIAL
static struct {
    qm_t A;
    q_t c[4];    // λ^3 + c1 λ^2 + c2 λ + c3
    float w[3];  // the roots (low to high)
    float ez[3];
} C6;
static void char_calc(void)
{
    int p = P[0], q = P[1];
    int v[9] = {2, p, 0, p, 2, 1, 0, 1, q};
    qm_ints(&C6.A, 3, 3, v);
    qm_charpoly(&C6.A, C6.c);
    fm_t F, V;
    fm_from(&F, &C6.A);
    fm_jacobi(&F, C6.w, &V);
}
static const char *root_txt(float v)
{
    // an integer root exactly, else 3 places
    float rv = (float)lroundf(v);
    if (fabsf(v - rv) < 1e-4f) {
        return I((int64_t)rv);
    }
    return F(v, 3);
}
static void char_text(int st)
{
    const q_t *c = C6.c;
    switch (st) {
    case 0:
        T(LG("#One polynomial for all the eigenvalues\nThe eigenvalues are the roots of\n",
             "#פולינום אחד לכל הערכים העצמיים\nהערכים העצמיים הם השורשים של\n"));
        T("=p(λ) = det(λI − A),   A = @m{2&%s&0;%s&2&1;0&1&%s}\n", PV(0), PV(0), PV(1));
        break;
    case 1:
        T(LG("#Its coefficients\nThey are sums that you can see in $A$: the trace, the 2 × 2 minors on the diagonal "
             "($M$), and the determinant.\n",
             "#המקדמים\nהם סכומים שרואים ב-$A$: העקבה, המינורים 2 × 2 על האלכסון ($M$), והדטרמיננטה.\n"));
        T("=p(λ) = λ^3 − (@t{tr} A)λ^2 + Mλ − det A\n");
        T("=p(λ) = λ^3 %s %sλ^2 %s %sλ %s %s\n", q_cmp(c[1], qi(0)) < 0 ? "−" : "+", I(labs(c[1].n)),
          q_cmp(c[2], qi(0)) < 0 ? "−" : "+", I(labs(c[2].n)), q_cmp(c[3], qi(0)) < 0 ? "−" : "+", I(labs(c[3].n)));
        break;
    case 2:
        T(LG("#The roots\nWhere the graph meets the axis:\n", "#השורשים\nאיפה שהגרף פוגש את הציר:\n"));
        T("=@1{λ_1} ≈ %s,   @2{λ_2} ≈ %s,   @3{λ_3} ≈ %s\n", root_txt(C6.w[0]), root_txt(C6.w[1]), root_txt(C6.w[2]));
        break;
    case 3:
        T(LG("#Checks\nThe sum is the trace and the product is the determinant:\n",
             "#בדיקות\nהסכום הוא העקבה והמכפלה היא הדטרמיננטה:\n"));
        T("=λ_1 + λ_2 + λ_3 = @t{tr} A = %s\n", I(4 + P[1]));
        T("=λ_1 λ_2 λ_3 = det A = %s\n", Q(q_neg(c[3])));
        break;
    case 4: {
        float a = 2.0f - P[0], b = 2.0f + P[0];
        T(LG("#Interlacing\nThe top left 2 × 2 block has the eigenvalues $2 ± p$. They sit between the eigenvalues of "
             "$A$:\n",
             "#שזירה\nלבלוק 2 × 2 השמאלי העליון יש ערכים עצמיים $2 ± p$. הם יושבים בין הערכים העצמיים של $A$:\n"));
        T("=@1{%s} ≤ @4{%s} ≤ @2{%s} ≤ @4{%s} ≤ @3{%s}\n", F(C6.w[0], 2), I((int)a), F(C6.w[1], 2), I((int)b),
          F(C6.w[2], 2));
        break;
    }
    default:
        T(LG("#Why it is beautiful\nA symmetric matrix has only real roots: its graph always crosses the axis $n$ "
             "times. And each smaller block fits between them, like the teeth of two combs.",
             "#למה זה יפה\nלמטריצה סימטרית יש רק שורשים ממשיים: הגרף שלה תמיד חוצה את הציר $n$ פעמים. וכל בלוק קטן "
             "יותר נכנס ביניהם, כמו שיניים של שני מסרקים."));
        break;
    }
}
static float char_p(float x)
{
    return ((x + q_f(C6.c[1])) * x + q_f(C6.c[2])) * x + q_f(C6.c[3]);
}
static void char_pic(rect_t r, int st, float t)
{
    float lo = C6.w[0] - 1.2f, hi = C6.w[2] + 1.2f;
    EZ(&C6.ez[0], lo);
    EZ(&C6.ez[1], hi);
    if (s_mini || st < 0) {
        C6.ez[0] = lo;
        C6.ez[1] = hi;
    }
    lo = C6.ez[0];
    hi = C6.ez[1];
    // the height: the largest |p| on the range
    float pm = 1;
    for (int i = 0; i <= 40; i++) {
        float v = fabsf(char_p(lo + (hi - lo) * i / 40));
        if (v > pm) {
            pm = v;
        }
    }
    // a view with other scales on x and y: draw in px by hand
    s_pr = r;
    float sx = r.w / (hi - lo), sy = (r.h * 0.42f) / pm;
    float y0 = r.y + r.h * 0.5f;
    tfd_clip(r.x, r.y, r.x + r.w, r.y + r.h);
    wu((float)r.x, y0, (float)(r.x + r.w), y0, rgb(110, 120, 160), 220);
    for (int k = (int)lo; k <= (int)hi + 1; k++) {
        float x = r.x + (k - lo) * sx;
        wu(x, y0 - 3, x, y0 + 3, rgb(110, 120, 160), 200);
    }
    float px = (float)r.x, py = y0 - char_p(lo) * sy;
    for (int i = 1; i <= 80; i++) {
        float xv = lo + (hi - lo) * i / 80, nx = r.x + (xv - lo) * sx, ny = y0 - char_p(xv) * sy;
        if (fabsf(ny - py) < r.h * 2) {
            tfd_line(px, py, nx, ny, s_mini ? 1.4f : 2.0f, COL_C4, 230);
        }
        px = nx;
        py = ny;
    }
    uint16_t cc[3] = {COL_C1, COL_C2, COL_C3};
    for (int k = 0; k < 3; k++) {
        float x = r.x + (C6.w[k] - lo) * sx;
        float pulse = s_mini ? 0 : 0.6f * fsin(t * 3 + k);
        tfd_disc(x, y0, (s_mini ? 2.5f : 4.0f) + pulse, cc[k], 256);
    }
    if (st == 4) {
        for (int s = -1; s <= 1; s += 2) {
            float v = 2.0f + s * P[0], x = r.x + (v - lo) * sx;
            wu(x, y0 - 14, x, y0 + 14, COL_C4, 256);
            wu(x + 1, y0 - 14, x + 1, y0 + 14, COL_C4, 256);
        }
    }
}
static const topic_t TP_CHAR = {{"The characteristic polynomial", "הפולינום האופייני"},
                                1,
                                2,
                                6,
                                {{"p", 0, 3, 1, 1}, {"q", -2, 4, 2, 1}},
                                char_calc,
                                char_text,
                                char_pic};

// ================================================================ 7 DIAGONALIZATION
static struct {
    int l1, l2, n;
    qm_t A, An;
    float ez[4];
} G7;
static void diag_calc(void)
{
    G7.l1 = P[0];
    G7.l2 = P[1];
    G7.n = P[2];
    int l1 = G7.l1, l2 = G7.l2;
    int v[4] = {2 * l1 - l2, -2 * l1 + 2 * l2, l1 - l2, -l1 + 2 * l2};
    qm_ints(&G7.A, 2, 2, v);
    // A^n = P D^n P^-1 with P = [2 1; 1 1], P^-1 = [1 -1; -1 2]
    int64_t p1 = 1, p2 = 1;
    for (int i = 0; i < G7.n; i++) {
        p1 *= l1;
        p2 *= l2;
    }
    int an[4] = {(int)(2 * p1 - p2), (int)(-2 * p1 + 2 * p2), (int)(p1 - p2), (int)(-p1 + 2 * p2)};
    qm_ints(&G7.An, 2, 2, an);
}
static void diag_text(int st)
{
    int l1 = G7.l1, l2 = G7.l2;
    switch (st) {
    case 0:
        T(LG("#Change the point of view\nIn the basis of its eigenvectors, $A$ is only a stretch:\n",
             "#לשנות את נקודת המבט\nבבסיס של הווקטורים העצמיים שלה, $A$ היא רק מתיחה:\n"));
        T("=A = PDP^{-1}\n");
        T("=%s = @m{@1{2}&@2{1};@1{1}&@2{1}}@m{%s&0;0&%s}@m{1&−1;−1&2}\n", MQ(&G7.A), PV(0), PV(1));
        break;
    case 1:
        T(LG("#The columns of P\nThey are the eigenvectors:\n", "#העמודות של P\nהן הווקטורים העצמיים:\n"));
        T("=A@1{@m{2;1}} = @m{%s;%s} = %s@1{@m{2;1}}\n", I(2 * l1), I(l1), PVP(0));
        T("=A@2{@m{1;1}} = @m{%s;%s} = %s@2{@m{1;1}}\n", I(l2), I(l2), PVP(1));
        break;
    case 2:
        T(LG("#Powers become easy\nOnly the diagonal gets the power:\n",
             "#חזקות נעשות קלות\nרק האלכסון מקבל את החזקה:\n"));
        T("=A^{%s} = P@m{%s^{%s}&0;0&%s^{%s}}P^{-1} = %s\n", PV(2), PVP(0), PV(2), PVP(1), PV(2), MQ(&G7.An));
        break;
    case 3:
        T(LG("#Why it works\nIn $A^n = PDP^{-1}PDP^{-1}⋯$ each $P^{-1}P$ in the middle is $I$. Only $D^n$ stays "
             "between $P$ and $P^{-1}$.",
             "#למה זה עובד\nב-$A^n = PDP^{-1}PDP^{-1}⋯$ כל $P^{-1}P$ באמצע הוא $I$. רק $D^n$ נשאר בין $P$ "
             "ל-$P^{-1}$."));
        break;
    case 4:
        T(LG("#When it fails\nSome matrices do not have enough eigenvectors. The shear $@m{1&1;0&1}$ has $λ = 1$ "
             "twice but only one line of eigenvectors: no basis makes it a stretch.",
             "#מתי זה נכשל\nלחלק מהמטריצות אין מספיק וקטורים עצמיים. לגזירה $@m{1&1;0&1}$ יש $λ = 1$ פעמיים אבל רק "
             "קו אחד של וקטורים עצמיים: שום בסיס לא הופך אותה למתיחה."));
        if (l1 == l2) {
            T(LG("\n*Here $λ_1 = λ_2$, and $A = λI$ is diagonal already.*",
                 "\n*כאן $λ_1 = λ_2$, ו-$A = λI$ כבר אלכסונית.*"));
        }
        break;
    default:
        T(LG("#Why it is beautiful\nDiagonalization cuts a system with links into parts with no links. The Fibonacci "
             "numbers, the growth of a population and $e^{At}$ all come from $D^n$.",
             "#למה זה יפה\nלכסון מפרק מערכת עם קשרים לחלקים בלי קשרים. מספרי פיבונאצ'י, גידול של אוכלוסייה ו-$e^{At}$ "
             "כולם באים מ-$D^n$."));
        break;
    }
}
static void diag_pic(rect_t r, int st, float t)
{
    float l1 = (float)G7.l1, l2 = (float)G7.l2;
    float m = fabsf(l1) > fabsf(l2) ? fabsf(l1) : fabsf(l2);
    float span = 2.0f * (2.3f * (m > 1 ? m : 1)) + 1.0f;
    EZ(&G7.ez[0], span);
    if (s_mini || st < 0) {
        G7.ez[0] = span;
    }
    view_set(r, G7.ez[0], 0, 0);
    grid_plain(10, rgb(34, 40, 64), 70);
    // the lattice of the eigenvectors v1 = (2, 1), v2 = (1, 1), and its image: a stretch along each
    float k = 0.5f - 0.5f * fcos(t * 1.4f);
    float s1 = 1 + (l1 - 1) * k, s2 = 1 + (l2 - 1) * k;
    for (int i = -4; i <= 4; i++) {
        seg(i * s1 * 2 - 6 * s2, i * s1 - 6 * s2, i * s1 * 2 + 6 * s2, i * s1 + 6 * s2, 1, rgb(80, 110, 180), 90);
        seg(i * s2 - 6 * s1 * 2, i * s2 - 6 * s1, i * s2 + 6 * s1 * 2, i * s2 + 6 * s1, 1, rgb(80, 110, 180), 90);
    }
    float p[8] = {0, 0, 2 * s1, s1, 2 * s1 + s2, s1 + s2, s2, s2};
    fpoly(p, 4, COL_C4, 70);
    float w = s_mini ? 1.4f : 2.2f;
    arrow(0, 0, 2 * s1, s1, COL_C1, w, 256);
    arrow(0, 0, s2, s2, COL_C2, w, 256);
    if (!s_mini && st >= 0) {
        label(2 * s1, s1, 10, -6, "@1{v_1}", COL_TEXT);
        label(s2, s2, -10, -6, "@2{v_2}", COL_TEXT);
    }
}
static const topic_t TP_DIAG = {{"Diagonalization", "לכסון"},
                                1,
                                3,
                                6,
                                {{"λ_1", -3, 3, 2, 1}, {"λ_2", -3, 3, 1, 1}, {"n", 1, 8, 5, 1}},
                                diag_calc,
                                diag_text,
                                diag_pic};

// ================================================================ 8 SYMMETRIC MATRICES
static struct {
    int a, b, d;
    eig2_t e;
    float ez[6];
} S8;
static void sym_calc(void)
{
    S8.a = P[0];
    S8.b = P[1];
    S8.d = P[2];
    eig2(S8.a, S8.b, S8.b, S8.d, &S8.e);
}
static void sym_text(int st)
{
    const eig2_t *e = &S8.e;
    switch (st) {
    case 0:
        T(LG("#Symmetric: $S = S^T$\nThe matrix is its own mirror across the diagonal.\n",
             "#סימטרית: $S = S^T$\nהמטריצה היא תמונת הראי של עצמה סביב האלכסון.\n"));
        T("=S = @m{%s&%s;%s&%s}\n", PV(0), PV(1), PV(1), PV(2));
        break;
    case 1:
        T(LG("#The eigenvalues are real\nUnder the root there is a sum of squares, so it is never negative:\n",
             "#הערכים העצמיים ממשיים\nמתחת לשורש יש סכום של ריבועים, ולכן הוא אף פעם לא שלילי:\n"));
        T("=λ = @f{a + d}{2} ± @r{@p{@f{a − d}{2}}^2 + b^2}\n");
        T("=λ = @h{%s}\n", EIGS(e, true));
        break;
    case 2:
        T(LG("#The eigenvectors are at right angles\n", "#הווקטורים העצמיים מאונכים\n"));
        if (e->D == 0) {
            T(LG("Here $S = λI$: every direction is an eigenvector.", "כאן $S = λI$: כל כיוון הוא וקטור עצמי."));
        } else {
            T("=@1{q_1} ≈ @m{%s;%s},   @2{q_2} ≈ @m{%s;%s}\n", F(e->v1[0], 3), F(e->v1[1], 3), F(e->v2[0], 3),
              F(e->v2[1], 3));
            T("=@1{q_1}·@2{q_2} = 0\n");
        }
        break;
    case 3:
        T(LG("#The spectral theorem\n$S$ is a sum of stretches along perpendicular lines:\n",
             "#משפט הפירוק הספקטרלי\n$S$ היא סכום של מתיחות לאורך קווים מאונכים:\n"));
        T("=S = QΛQ^T = λ_1@1{q_1}@1{q_1}^T + λ_2@2{q_2}@2{q_2}^T\n");
        T("=λ_1 ≈ %s,   λ_2 ≈ %s\n", F(e->l1, 3), F(e->l2, 3));
        break;
    case 4:
        T(LG("#The ellipse\n$S$ takes the unit circle to an ellipse. Its axes are on $@1{q_1}$ and $@2{q_2}$, and "
             "their half lengths are $|λ_1|$ and $|λ_2|$.",
             "#האליפסה\n$S$ מעבירה את מעגל היחידה לאליפסה. הצירים שלה על $@1{q_1}$ ועל $@2{q_2}$, ואורכי החצאים "
             "שלהם הם $|λ_1|$ ו-$|λ_2|$."));
        break;
    default:
        T(LG("#Why it is beautiful\nEvery symmetric matrix, of any size, has real eigenvalues and a full set of "
             "perpendicular eigenvectors. Energy, inertia and the covariance of data are all symmetric.",
             "#למה זה יפה\nלכל מטריצה סימטרית, בכל גודל, יש ערכים עצמיים ממשיים וקבוצה מלאה של וקטורים עצמיים "
             "מאונכים. אנרגיה, התמד, והשונות המשותפת של נתונים הם כולם סימטריים."));
        break;
    }
}
static void sym_pic(rect_t r, int st, float t)
{
    const eig2_t *e = &S8.e;
    float m = fabsf(e->l1) > fabsf(e->l2) ? fabsf(e->l1) : fabsf(e->l2);
    float span = 2.4f * (m > 1.2f ? m : 1.2f) + 0.6f;
    EZ(&S8.ez[0], span);
    EZ(&S8.ez[1], (float)S8.a);
    EZ(&S8.ez[2], (float)S8.b);
    EZ(&S8.ez[3], (float)S8.d);
    if (s_mini || st < 0) {
        S8.ez[0] = span;
        S8.ez[1] = (float)S8.a;
        S8.ez[2] = (float)S8.b;
        S8.ez[3] = (float)S8.d;
    }
    view_set(r, S8.ez[0], 0, 0);
    grid_plain(8, rgb(36, 42, 68), 80);
    float a = S8.ez[1], b = S8.ez[2], d = S8.ez[3];
    ellipse(1, 0, 0, 1, 0, 0, 1, rgb(90, 100, 140), 200);
    ellipse(a, b, b, d, 0, 0, s_mini ? 1.4f : 2.0f, COL_C4, 230);
    // the axes: the eigenvectors of the eased matrix
    float tr = a + d, det = a * d - b * b, disc = sqrtf(tr * tr / 4 - det > 0 ? tr * tr / 4 - det : 0);
    float l1 = tr / 2 + disc, l2 = tr / 2 - disc, v[2];
    if (fabsf(b) > 1e-3f) {
        unit2(v, b, l1 - a);
    } else {
        unit2(v, a >= d ? 1.0f : 0.0f, a >= d ? 0.0f : 1.0f);
    }
    float w = s_mini ? 1.4f : 2.2f;
    if (!s_mini) {
        // the right angle
        float L = 0.18f * S8.ez[0] / 4;
        float p[8] = {0, 0, v[0] * L, v[1] * L, v[0] * L - v[1] * L, v[1] * L + v[0] * L, -v[1] * L, v[0] * L};
        fpoly(p, 4, COL_ACC, 120);
    }
    arrow(0, 0, v[0] * l1, v[1] * l1, COL_C1, w, 256);
    arrow(0, 0, -v[1] * l2, v[0] * l2, COL_C2, w, 256);
    // a point on the circle and its image
    float th = t * 0.6f, x = fcos(th), y = fsin(th);
    dot(x, y, s_mini ? 1.5f : 2.5f, rgb(170, 180, 210), 256);
    dot(a * x + b * y, b * x + d * y, s_mini ? 2.0f : 3.2f, COL_ACC, 256);
    (void)e;
}
static const topic_t TP_SYM = {{"Symmetric matrices", "מטריצות סימטריות"},
                               1,
                               3,
                               6,
                               {{"a", -3, 4, 3, 1}, {"b", -3, 3, 1, 1}, {"d", -3, 4, 1, 1}},
                               sym_calc,
                               sym_text,
                               sym_pic};
