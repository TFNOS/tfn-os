// LINEAR ALGEBRA: beyond: the SVD, quadratic forms, the power method, Markov chains, the graph Laplacian.

// ================================================================ 20 THE SVD
static struct {
    int a, b, c, d;
    float s1, s2;  // the singular values
    float tv;      // the angle of v1 (V is the turn by tv, det V = 1)
    float u1[2], u2[2];
    float ez[1];
} V20;
static void svd_calc(void)
{
    int a = P[0], b = P[1], c = P[2], d = P[3];
    V20.a = a;
    V20.b = b;
    V20.c = c;
    V20.d = d;
    fm_t S, V;
    float w[2];
    S.n = 2;
    S.a[0][0] = (float)(a * a + c * c);
    S.a[0][1] = S.a[1][0] = (float)(a * b + c * d);
    S.a[1][1] = (float)(b * b + d * d);
    fm_jacobi(&S, w, &V);
    // the larger first
    V20.s1 = sqrtf(w[1] > 0 ? w[1] : 0);
    V20.s2 = sqrtf(w[0] > 0 ? w[0] : 0);
    float v1[2] = {V.a[0][1], V.a[1][1]};
    V20.tv = atan2f(v1[1], v1[0]);
    float v2[2] = {-v1[1], v1[0]};  // det V = 1
    for (int k = 0; k < 2; k++) {
        const float *v = k ? v2 : v1;
        float s = k ? V20.s2 : V20.s1;
        float *u = k ? V20.u2 : V20.u1;
        float ax = a * v[0] + b * v[1], ay = c * v[0] + d * v[1];
        if (s > 1e-4f) {
            u[0] = ax / s;
            u[1] = ay / s;
        } else {
            // a zero stretch: u2 at a right angle to u1
            u[0] = -V20.u1[1];
            u[1] = V20.u1[0];
        }
    }
}
static void svd_text(int st)
{
    int a = V20.a, b = V20.b, c = V20.c, d = V20.d;
    switch (st) {
    case 0:
        T(LG("#Turn, stretch, turn\nEvery matrix is a turn $V^T$, a stretch $Σ$ along the axes, and a turn $U$:\n",
             "#סיבוב, מתיחה, סיבוב\nכל מטריצה היא סיבוב $V^T$, מתיחה $Σ$ לאורך הצירים, וסיבוב $U$:\n"));
        T("=A = UΣV^T,   A = %s\n", A2());
        T(LG("Watch the picture do the three moves.", "הסתכלו על התמונה עושה את שלושת הצעדים."));
        break;
    case 1:
        T(LG("#The stretches\nThe singular values are the square roots of the eigenvalues of $A^TA$:\n",
             "#המתיחות\nהערכים הסינגולריים הם השורשים של הערכים העצמיים של $A^TA$:\n"));
        T("=A^TA = %s\n", M2I(a * a + c * c, a * b + c * d, a * b + c * d, b * b + d * d));
        T("=σ_1 ≈ @h{%s},   σ_2 ≈ @h{%s}\n", F(V20.s1, 3), F(V20.s2, 3));
        break;
    case 2:
        T(LG("#The circle becomes an ellipse\nThe unit vectors $@1{v_1}$, $@2{v_2}$ go to $σ_1@1{u_1}$ and "
             "$σ_2@2{u_2}$: the half axes of the ellipse.\n",
             "#המעגל הופך לאליפסה\nוקטורי היחידה $@1{v_1}$, $@2{v_2}$ עוברים ל-$σ_1@1{u_1}$ ול-$σ_2@2{u_2}$: חצאי "
             "הצירים של האליפסה.\n"));
        break;
    case 3:
        T(LG("#Two checks\n", "#שתי בדיקות\n"));
        T("=σ_1σ_2 = |det A| = %s\n", I(abs(a * d - b * c)));
        T("=σ_1^2 + σ_2^2 = a^2 + b^2 + c^2 + d^2 = %s\n", I(a * a + b * b + c * c + d * d));
        break;
    case 4:
        T(LG("#The best rank-one picture\nKeep only the largest stretch: $σ_1@1{u_1}@1{v_1}^T$ is the nearest matrix "
             "of rank one to $A$. A photo keeps its large σ and drops the small ones: that is compression.",
             "#התמונה הטובה ביותר מדרגה אחת\nשומרים רק את המתיחה הגדולה: $σ_1@1{u_1}@1{v_1}^T$ היא המטריצה הקרובה "
             "ביותר ל-$A$ מדרגה אחת. תמונה שומרת את ה-σ הגדולים וזורקת את הקטנים: זו דחיסה."));
        break;
    default:
        T(LG("#Why it is beautiful\nEvery matrix, square or not, of any rank, has an SVD. It gives the rank, the best "
             "approximations, the pseudo-inverse and the principal components of data.",
             "#למה זה יפה\nלכל מטריצה, ריבועית או לא, בכל דרגה, יש פירוק SVD. הוא נותן את הדרגה, את הקירובים "
             "הטובים ביותר, את ההופכית המוכללת ואת הרכיבים העיקריים של נתונים."));
        break;
    }
}
static void svd_pic(rect_t r, int st, float t)
{
    float span = 2.5f * (V20.s1 > 1.1f ? V20.s1 : 1.1f) + 0.4f;
    EZ(&V20.ez[0], span);
    if (s_mini || st < 0) {
        V20.ez[0] = span;
    }
    view_set(r, V20.ez[0], 0, 0);
    // the four phases of 1.4 s: turn by -tv, stretch, turn to A, hold
    float ph = s_mini ? 3.5f : fpart(t / 5.6f) * 4;
    float k1 = ease_io(ph), k2 = ease_io(ph - 1), k3 = ease_io(ph - 2);
    float th = -V20.tv * k1, cs = fcos(th), sn = fsin(th);
    float sx = 1 + (V20.s1 - 1) * k2, sy = 1 + (V20.s2 - 1) * k2;
    // M = diag(sx, sy) R(th)
    float m00 = sx * cs, m01 = -sx * sn, m10 = sy * sn, m11 = sy * cs;
    // the last move: from Σ V^T to A (in a straight way)
    float A00 = (float)V20.a, A01 = (float)V20.b, A10 = (float)V20.c, A11 = (float)V20.d;
    m00 += (A00 - m00) * k3;
    m01 += (A01 - m01) * k3;
    m10 += (A10 - m10) * k3;
    m11 += (A11 - m11) * k3;
    grid_map(m00, m01, m10, m11, 8, rgb(70, 90, 150), 70);
    ellipse(1, 0, 0, 1, 0, 0, 1, rgb(80, 90, 130), 150);
    ellipse(m00, m01, m10, m11, 0, 0, s_mini ? 1.3f : 2.0f, COL_C4, 230);
    float v1x = fcos(V20.tv), v1y = fsin(V20.tv), v2x = -v1y, v2y = v1x;
    float w = s_mini ? 1.3f : 2.2f;
    arrow(0, 0, m00 * v1x + m01 * v1y, m10 * v1x + m11 * v1y, COL_C1, w, 256);
    arrow(0, 0, m00 * v2x + m01 * v2y, m10 * v2x + m11 * v2y, COL_C2, w, 256);
}
static const topic_t TP_SVD = {{"The SVD", "פירוק SVD"},
                               3,
                               4,
                               6,
                               {{"a", -3, 3, 2, 1}, {"b", -3, 3, 2, 1}, {"c", -3, 3, -1, 1}, {"d", -3, 3, 1, 1}},
                               svd_calc,
                               svd_text,
                               svd_pic};

// ================================================================ 21 QUADRATIC FORMS
static struct {
    int a, b, c;
    eig2_t e;
    float ez[3];
} Q21;
static void qf_calc(void)
{
    Q21.a = P[0];
    Q21.b = P[1];
    Q21.c = P[2];
    eig2(Q21.a, Q21.b, Q21.b, Q21.c, &Q21.e);
}
// the kind of the form: 0 a bowl, 1 a cap, 2 a saddle, 3 a valley (one zero), 4 flat (zero)
static int qf_kind(void)
{
    int det = Q21.e.det, tr = Q21.e.tr;
    if (det > 0) {
        return tr > 0 ? 0 : 1;
    }
    if (det < 0) {
        return 2;
    }
    return tr == 0 ? 4 : 3;
}
static void qf_text(int st)
{
    int a = Q21.a, b = Q21.b, c = Q21.c, det = a * c - b * b;
    switch (st) {
    case 0:
        T(LG("#A quadratic form\nA sum of squares and products, with a symmetric matrix inside:\n",
             "#תבנית ריבועית\nסכום של ריבועים ומכפלות, עם מטריצה סימטרית בפנים:\n"));
        T("=q(x, y) = %sx^2 + %sxy + %sy^2 = @m{x&y}@m{%s&%s;%s&%s}@m{x;y}\n", PV(0), I(2 * b), PV(2), PV(0), PV(1),
          PV(1), PV(2));
        break;
    case 1:
        T(LG("#Turn to the axes of S\nIn the basis of the eigenvectors the product $xy$ goes away:\n",
             "#מסובבים לצירים של S\nבבסיס של הווקטורים העצמיים המכפלה $xy$ נעלמת:\n"));
        T("=q = λ_1X^2 + λ_2Y^2,   λ = %s\n", EIGS(&Q21.e, true));
        break;
    case 2: {
        T(LG("#The shape\n", "#הצורה\n"));
        static const char *const K[5][2] = {
                {"Both λ > 0: a bowl. The level lines are ellipses.", "שני λ > 0: קערה. קווי הגובה הם אליפסות."},
                {"Both λ < 0: a cap. The level lines are ellipses.", "שני λ < 0: כיפה. קווי הגובה הם אליפסות."},
                {"λ of two signs: a saddle. The level lines are hyperbolas.",
                 "λ בשני סימנים: אוכף. קווי הגובה הם היפרבולות."},
                {"One λ is zero: a valley. The level lines are straight.", "λ אחד הוא אפס: עמק. קווי הגובה ישרים."},
                {"All zero: flat.", "הכול אפס: שטוח."}};
        T("%s", K[qf_kind()][s_he]);
        break;
    }
    case 3:
        T(LG("#The test\n$S$ is positive definite (a bowl) exactly when $a > 0$ and $ac − b^2 > 0$:\n",
             "#המבחן\n$S$ חיובית מוגדרת (קערה) בדיוק כאשר $a > 0$ וגם $ac − b^2 > 0$:\n"));
        T("=a = %s,   ac − b^2 = %s\n", PV(0), I(det));
        T(qf_kind() == 0 ? LG("*Yes: a bowl.*", "*כן: קערה.*") : LG("No: not a bowl.", "לא: לא קערה."));
        break;
    case 4:
        T(LG("#The second derivative test\nNear a flat point a smooth function looks like the form of its second "
             "derivatives: a bowl is a minimum, a cap a maximum, a saddle neither.",
             "#מבחן הנגזרת השנייה\nליד נקודה שטוחה, פונקציה חלקה נראית כמו התבנית של הנגזרות השניות שלה: קערה היא "
             "מינימום, כיפה היא מקסימום, ואוכף אף אחד מהם."));
        break;
    default:
        T(LG("#Why it is beautiful\nOne symmetric matrix decides the shape of a surface. Energy, the error of a fit "
             "and the curvature of space are all quadratic forms.",
             "#למה זה יפה\nמטריצה סימטרית אחת קובעת את הצורה של משטח. אנרגיה, השגיאה של התאמה והעקמומיות של המרחב "
             "הם כולם תבניות ריבועיות."));
        break;
    }
}
static void qf_pic(rect_t r, int st, float t)
{
    EZ(&Q21.ez[0], (float)Q21.a);
    EZ(&Q21.ez[1], (float)Q21.b);
    EZ(&Q21.ez[2], (float)Q21.c);
    if (s_mini || st < 0) {
        Q21.ez[0] = (float)Q21.a;
        Q21.ez[1] = (float)Q21.b;
        Q21.ez[2] = (float)Q21.c;
    }
    float a = Q21.ez[0], b = Q21.ez[1], c = Q21.ez[2];
    view_set(r, 6.0f, 0, 0);
    // the height as colour (scaled to the largest height in the view), and smooth level lines that flow out
    int step = s_mini ? 3 : 1;
    float inv = 1.0f / s_vs, flow = s_mini ? 0 : fpart(t * 0.25f);
    float lm = fabsf(a) + fabsf(b) + fabsf(c) + 0.5f, R0 = 3.0f;
    float qmax = lm * R0 * R0, dq = qmax / 7.0f, idq = 1.0f / dq, iqm = 1.0f / qmax;
    for (int py = r.y; py < r.y + r.h; py += step) {
        float y = (s_vcy - py - step * 0.5f) * inv;
        uint16_t *row = &g_fb[py * gfx_w];
        for (int px = r.x; px < r.x + r.w; px += step) {
            float x = (px + step * 0.5f - s_vcx) * inv;
            float q = a * x * x + 2 * b * x * y + c * y * y;
            uint16_t col = heat(q * iqm * 1.4f);
            // the distance to the nearest level line, in px: the height step / the slope
            float lv = q * idq - flow;
            float f = lv - (float)(int)(lv < 0 ? lv - 1 : lv);
            float d = (f < 0.5f ? f : 1 - f) * dq;
            float gx = 2 * (a * x + b * y), gy = 2 * (b * x + c * y);
            float g = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
            // (the distance in px is d * s_vs / g: a division only near a line)
            float gg = g * 0.8f + 1e-3f;
            if (d * s_vs < 1.2f * gg) {
                float pxd = d * s_vs / gg;
                col = ui_mix(col, rgb(240, 236, 220), (int)((1.2f - pxd) * 150));
            }
            if (step == 1) {
                if (px >= 0 && px < gfx_w && py >= 0 && py < gfx_h) {
                    row[px] = col;
                }
            } else {
                tfd_rect(px, py, step, step, col, 256);
            }
        }
    }
    // the eigen axes
    float tr = a + c, det = a * c - b * b, disc = sqrtf(tr * tr / 4 - det > 0 ? tr * tr / 4 - det : 0);
    float l1 = tr / 2 + disc, v[2];
    if (fabsf(b) > 1e-3f) {
        unit2(v, b, l1 - a);
    } else {
        unit2(v, a >= c ? 1.0f : 0.0f, a >= c ? 0.0f : 1.0f);
    }
    dashed(-v[0] * 5, -v[1] * 5, v[0] * 5, v[1] * 5, COL_C1, 220);
    dashed(v[1] * 5, -v[0] * 5, -v[1] * 5, v[0] * 5, COL_C2, 220);
}
static const topic_t TP_QF = {{"Quadratic forms", "תבניות ריבועיות"},
                              3,
                              3,
                              6,
                              {{"a", -3, 3, 2, 1}, {"b", -3, 3, 1, 1}, {"c", -3, 3, 2, 1}},
                              qf_calc,
                              qf_text,
                              qf_pic};

// ================================================================ 22 THE POWER METHOD
static struct {
    int a, b, d;
    eig2_t e;
    float it[13][2];  // x_k, k = 0..12 (length 1)
    float err[13];    // the angle to the line of q1 (degrees)
    float ray;        // the Rayleigh quotient of x_12
    float ez[1];
} W22;
static void pow_calc(void)
{
    W22.a = P[0];
    W22.b = P[1];
    W22.d = P[2];
    eig2(W22.a, W22.b, W22.b, W22.d, &W22.e);
    // the vector of the largest |λ|
    const eig2_t *e = &W22.e;
    const float *q1 = fabsf(e->l1) >= fabsf(e->l2) ? e->v1 : e->v2;
    float x = 1, y = 0.35f;
    float L = sqrtf(x * x + y * y);
    x /= L;
    y /= L;
    if (fabsf(x * q1[0] + y * q1[1]) < 0.35f) {
        // a start almost at a right angle to the top eigenvector: the example would be slow, so turn the start
        float nx = 0.7071f * (x - y), ny = 0.7071f * (x + y);
        x = nx;
        y = ny;
    }
    for (int k = 0; k <= 12; k++) {
        W22.it[k][0] = x;
        W22.it[k][1] = y;
        float cs = fabsf(x * q1[0] + y * q1[1]);
        W22.err[k] = acosf(cs > 1 ? 1 : cs) * 57.29578f;
        float nx = W22.a * x + W22.b * y, ny = W22.b * x + W22.d * y;
        L = sqrtf(nx * nx + ny * ny);
        if (L < 1e-6f) {
            break;
        }
        x = nx / L;
        y = ny / L;
    }
    float ax = W22.a * W22.it[12][0] + W22.b * W22.it[12][1], ay = W22.b * W22.it[12][0] + W22.d * W22.it[12][1];
    W22.ray = ax * W22.it[12][0] + ay * W22.it[12][1];
}
static void pow_text(int st)
{
    const eig2_t *e = &W22.e;
    float big = fabsf(e->l1) >= fabsf(e->l2) ? e->l1 : e->l2, small = fabsf(e->l1) >= fabsf(e->l2) ? e->l2 : e->l1;
    switch (st) {
    case 0:
        T(LG("#Multiply, again and again\nStart with any $x_0$, and repeat: multiply by $A$, then make the length 1.\n",
             "#לכפול, שוב ושוב\nמתחילים מ-$x_0$ כלשהו, וחוזרים: כופלים ב-$A$, ואחר כך מקצרים לאורך 1.\n"));
        T("=x_{k+1} = @f{Ax_k}{@n{Ax_k}},   A = @m{%s&%s;%s&%s}\n", PV(0), PV(1), PV(1), PV(2));
        break;
    case 1:
        T(LG("#It turns to the top eigenvector\nThe part along the largest $|λ|$ grows fastest. The angle to it "
             "shrinks "
             "like $|λ_2/λ_1|^k$:\n",
             "#הוא מסתובב לווקטור העצמי העליון\nהחלק לאורך ה-$|λ|$ הגדול גדל הכי מהר. הזווית אליו קטנה כמו "
             "$|λ_2/λ_1|^k$:\n"));
        T("=@f{|λ_2|}{|λ_1|} ≈ @f{%s}{%s} ≈ %s\n", F(fabsf(small), 3), F(fabsf(big), 3),
          F(fabsf(big) > 1e-6f ? fabsf(small / big) : 0, 3));
        break;
    case 2:
        T(LG("#The first steps\nThe angle to the line of the top eigenvector:\n",
             "#הצעדים הראשונים\nהזווית לקו של הווקטור העצמי העליון:\n"));
        T("=k = 0: %s°,   1: %s°,   2: %s°\n", F(W22.err[0], 1), F(W22.err[1], 1), F(W22.err[2], 1));
        T("=3: %s°,   6: %s°,   12: %s°\n", F(W22.err[3], 1), F(W22.err[6], 2), F(W22.err[12], 3));
        break;
    case 3:
        T(LG("#The value\nThe Rayleigh quotient of the last vector gives the eigenvalue:\n",
             "#הערך\nמנת ריילי של הווקטור האחרון נותנת את הערך העצמי:\n"));
        T("=@f{x^TAx}{x^Tx} ≈ %s,   λ_1 = %s\n", F(W22.ray, 3), EIGS(e, true));
        break;
    case 4:
        T(LG("#When it fails\nWhen $|λ_2| = |λ_1|$ there is no winner, for example $λ = 3$ and $−3$: the vector jumps "
             "between two lines.",
             "#מתי זה נכשל\nכאשר $|λ_2| = |λ_1|$ אין מנצח, למשל $λ = 3$ ו-$−3$: הווקטור קופץ בין שני קווים."));
        if (fabsf(fabsf(big) - fabsf(small)) < 1e-4f) {
            T(LG(" *That is the case here.*", " *זה המצב כאן.*"));
        }
        break;
    default:
        T(LG("#Why it is beautiful\nOnly multiplications, and no equations. PageRank, the first principal component "
             "and the lowest tone of a structure are found this way, for matrices with billions of rows.",
             "#למה זה יפה\nרק כפל, בלי משוואות. PageRank, הרכיב העיקרי הראשון והצליל הנמוך של מבנה נמצאים כך, "
             "למטריצות עם מיליארדי שורות."));
        break;
    }
}
static void pow_pic(rect_t r, int st, float t)
{
    const eig2_t *e = &W22.e;
    view_set(r, 2.8f, 0, 0);
    grid_plain(3, rgb(36, 42, 68), 80);
    ellipse(1, 0, 0, 1, 0, 0, 1, rgb(80, 90, 130), 160);
    if (e->D > 0 || (e->D == 0 && !e->scalar)) {
        const float *q1 = fabsf(e->l1) >= fabsf(e->l2) ? e->v1 : e->v2;
        dashed(-q1[0] * 1.4f, -q1[1] * 1.4f, q1[0] * 1.4f, q1[1] * 1.4f, COL_C3, 220);
    }
    // the steps appear one by one (0.55 s each), the old ones fade
    int shown = s_mini ? 6 : (int)(fpart(t / 8.0f) * 15);
    if (shown > 12) {
        shown = 12;
    }
    for (int k = 0; k <= shown; k++) {
        int al = 70 + 186 * (k + 1) / (shown + 1);
        uint16_t c = ui_mix(COL_C1, COL_ACC, k * 255 / 12);
        if (k == shown) {
            arrow(0, 0, W22.it[k][0], W22.it[k][1], c, s_mini ? 1.4f : 2.4f, 256);
        } else {
            seg(0, 0, W22.it[k][0], W22.it[k][1], 1, c, al);
            dot(W22.it[k][0], W22.it[k][1], s_mini ? 1.2f : 2.0f, c, al);
        }
    }
}
static const topic_t TP_POW = {{"The power method", "שיטת החזקות"},
                               3,
                               3,
                               6,
                               {{"a", -3, 4, 3, 1}, {"b", -3, 3, 1, 1}, {"d", -3, 4, 1, 1}},
                               pow_calc,
                               pow_text,
                               pow_pic};

// ================================================================ 23 MARKOV CHAINS
static struct {
    int s[3];   // the stay chances in tenths
    q_t pi[3];  // the long run
    cx_t lam[3];
    float ez[3];
} M23;
static void mark_calc(void)
{
    for (int i = 0; i < 3; i++) {
        M23.s[i] = P[i];
    }
    // π_i ∝ 1 / (10 - s_i)
    q_t w[3], sum = qi(0);
    for (int i = 0; i < 3; i++) {
        w[i] = qn(1, 10 - M23.s[i]);
        sum = q_add(sum, w[i]);
    }
    for (int i = 0; i < 3; i++) {
        M23.pi[i] = q_div(w[i], sum);
    }
    qm_t Pm;
    qm_zero(&Pm, 3, 3);
    for (int i = 0; i < 3; i++) {
        Pm.a[i][i] = qn(M23.s[i], 10);
        Pm.a[(i + 1) % 3][i] = qn(10 - M23.s[i], 10);
    }
    qm_eig(&Pm, M23.lam);
}
static const char *TENTH(int v)
{
    char *o = nb();
    if (v == 0 || v == 10) {
        snprintf(o, 420, "%d", v / 10);
    } else {
        snprintf(o, 420, "0.%d", v);
    }
    return o;
}
static void mark_text(int st)
{
    const int *s = M23.s;
    switch (st) {
    case 0:
        T(LG("#A chain of chances\nThree states. Each day, state $i$ stays with the chance $s_i$, or moves on to the "
             "next state.\n",
             "#שרשרת של סיכויים\nשלושה מצבים. בכל יום, מצב $i$ נשאר בסיכוי $s_i$, או עובר למצב הבא.\n"));
        T("=P = @m{%s&0&%s;%s&%s&0;0&%s&%s}\n", TENTH(s[0]), TENTH(10 - s[2]), TENTH(10 - s[0]), TENTH(s[1]),
          TENTH(10 - s[1]), TENTH(s[2]));
        break;
    case 1:
        T(LG("#Tomorrow\nThe chances of tomorrow are $Pp$ for the chances $p$ of today. Each column of $P$ adds up to "
             "1.",
             "#מחר\nהסיכויים של מחר הם $Pp$ עבור הסיכויים $p$ של היום. כל עמודה של $P$ מסתכמת ב-1."));
        break;
    case 2:
        T(LG("#The long run\nThe chances settle at $π$ with $Pπ = π$: an eigenvector for $λ = 1$.\n",
             "#לטווח הארוך\nהסיכויים מתייצבים על $π$ עם $Pπ = π$: וקטור עצמי עבור $λ = 1$.\n"));
        T("=π = @m{%s;%s;%s}\n", QD(M23.pi[0]), QD(M23.pi[1]), QD(M23.pi[2]));
        break;
    case 3: {
        T(LG("#Why it settles\nThe other eigenvalues are inside the unit circle, so their parts fade:\n",
             "#למה זה מתייצב\nהערכים העצמיים האחרים בתוך מעגל היחידה, ולכן החלקים שלהם דועכים:\n"));
        // the two that are not 1
        int k = 0;
        cx_t o[2] = {{0, 0}, {0, 0}};
        int skip = 0;
        float best = 9;
        for (int i = 0; i < 3; i++) {
            float dd = fabsf(M23.lam[i].re - 1) + fabsf(M23.lam[i].im);
            if (dd < best) {
                best = dd;
                skip = i;
            }
        }
        for (int i = 0; i < 3; i++) {
            if (i != skip && k < 2) {
                o[k++] = M23.lam[i];
            }
        }
        T("=λ = 1,   %s,   %s\n", CX(o[0], 2), CX(o[1], 2));
        T("=|λ| ≈ %s,   %s\n", F(cx_abs(o[0]), 2), F(cx_abs(o[1]), 2));
        break;
    }
    case 4:
        T(LG("#Read it\nState $i$ gets a share that is proportional to $@f{1}{1 − s_i}$: a state that holds you longer "
             "gets more of the time.",
             "#לקרוא את זה\nמצב $i$ מקבל חלק שפרופורציוני ל-$@f{1}{1 − s_i}$: מצב שמחזיק אתכם יותר זמן מקבל יותר "
             "מהזמן."));
        break;
    default:
        T(LG("#Why it is beautiful\nThe ranking of web pages (PageRank) is this vector for the web: a walk that "
             "follows links, and the share of time on each page.",
             "#למה זה יפה\nהדירוג של דפי האינטרנט (PageRank) הוא הווקטור הזה עבור הרשת: הליכה שעוקבת אחרי קישורים, "
             "והחלק של הזמן בכל דף."));
        break;
    }
}
static void mark_pic(rect_t r, int st, float t)
{
    // the distribution from (1, 0, 0) after d days; a new start each 9 s
    float days = s_mini ? 40 : fpart(t / 9.0f) * 18;
    float p[3] = {1, 0, 0};
    int nd = (int)days;
    float fr = days - nd;
    float prev[3] = {1, 0, 0};
    for (int k = 0; k <= nd; k++) {
        memcpy(prev, p, sizeof p);
        float n2[3];
        for (int i = 0; i < 3; i++) {
            int j = (i + 2) % 3;
            n2[i] = M23.s[i] / 10.0f * p[i] + (10 - M23.s[j]) / 10.0f * p[j];
        }
        memcpy(p, n2, sizeof p);
    }
    float now[3];
    for (int i = 0; i < 3; i++) {
        now[i] = prev[i] + (p[i] - prev[i]) * ease_io(fr);
    }
    s_pr = r;
    float cx0 = r.x + r.w / 2.0f, cy0 = r.y + r.h / 2.0f + r.h * 0.04f, R0 = (r.w < r.h ? r.w : r.h) * 0.32f;
    float nx[3], ny[3];
    for (int i = 0; i < 3; i++) {
        float a = -1.5707963f + i * 2.0943951f;
        nx[i] = cx0 + R0 * fcos(a);
        ny[i] = cy0 + R0 * fsin(a);
    }
    uint16_t cc[3] = {COL_C1, COL_C2, COL_C3};
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float w = 0.6f + (10 - M23.s[i]) * 0.35f;
        tfd_line(nx[i], ny[i], nx[j], ny[j], s_mini ? w * 0.6f : w, cc[i], 150);
        // a small head on the way
        float mx = nx[i] + (nx[j] - nx[i]) * 0.62f, my = ny[i] + (ny[j] - ny[i]) * 0.62f;
        tfd_disc(mx, my, s_mini ? 1.5f : 2.8f, cc[i], 220);
    }
    for (int i = 0; i < 3; i++) {
        float base = (r.w < r.h ? r.w : r.h) * 0.06f;
        float rad = base + sqrtf(now[i]) * (r.w < r.h ? r.w : r.h) * 0.14f;
        // the stay loop
        tfd_ring(nx[i] + (nx[i] - cx0) * 0.25f, ny[i] + (ny[i] - cy0) * 0.25f, rad * 0.9f, 0.6f + M23.s[i] * 0.3f,
                 cc[i], 90);
        tfd_disc(nx[i], ny[i], rad, cc[i], 230);
        // the target π as a ring
        float rt = base + sqrtf(q_f(M23.pi[i])) * (r.w < r.h ? r.w : r.h) * 0.14f;
        tfd_ring(nx[i], ny[i], rt, 1.2f, COL_TEXT, 200);
    }
    (void)st;
}
static const topic_t TP_MARK = {{"Markov chains", "שרשראות מרקוב"},
                                3,
                                3,
                                6,
                                {{"s_1", 0, 9, 7, 10}, {"s_2", 0, 9, 5, 10}, {"s_3", 0, 9, 2, 10}},
                                mark_calc,
                                mark_text,
                                mark_pic};

// ================================================================ 24 THE GRAPH LAPLACIAN
enum {
    G_PATH,
    G_CYCLE,
    G_STAR,
    G_FULL,
    G_TWO,
    G_N
};
static struct {
    int kind, n, cut;
    int ne, e[40][2];
    int cut_e;  // the edge that the cut takes out (-1: none)
    qm_t L;
    float w[8];     // the eigenvalues (low to high)
    float fied[8];  // the Fiedler vector
    int pieces;
    int64_t trees;
    float px[8], py[8];  // the places of the nodes (-1 .. 1)
} L24;
static void lap_edges(void)
{
    int n = L24.n, k = L24.kind;
    L24.ne = 0;
#define ED(a, b)                                                                                                       \
    do {                                                                                                               \
        L24.e[L24.ne][0] = (a);                                                                                        \
        L24.e[L24.ne][1] = (b);                                                                                        \
        L24.ne++;                                                                                                      \
    } while (0)
    if (k == G_PATH || k == G_CYCLE) {
        for (int i = 0; i + 1 < n; i++) {
            ED(i, i + 1);
        }
        if (k == G_CYCLE) {
            ED(n - 1, 0);
        }
    } else if (k == G_STAR) {
        for (int i = 1; i < n; i++) {
            ED(0, i);
        }
    } else if (k == G_FULL) {
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                ED(i, j);
            }
        }
    } else {
        ED(0, 1);
        ED(1, 2);
        ED(2, 0);
        ED(3, 4);
        ED(4, 5);
        ED(5, 3);
        ED(2, 3);
    }
#undef ED
    // the edge of the cut
    L24.cut_e = -1;
    if (L24.cut) {
        L24.cut_e = k == G_PATH ? n / 2 - 1 : k == G_TWO ? 6 : k == G_STAR ? n - 2 : 0;
    }
    // the places
    for (int i = 0; i < n; i++) {
        float a = -1.5707963f + 6.2831853f * i / n;
        if (k == G_PATH) {
            L24.px[i] = -0.85f + 1.7f * i / (n - 1);
            L24.py[i] = 0;
        } else if (k == G_STAR) {
            float b = -1.5707963f + 6.2831853f * (i - 1) / (n - 1);
            L24.px[i] = i ? 0.8f * fcos(b) : 0;
            L24.py[i] = i ? 0.8f * fsin(b) : 0;
        } else if (k == G_TWO) {
            static const float X[6] = {-0.85f, -0.85f, -0.3f, 0.3f, 0.85f, 0.85f};
            static const float Yv[6] = {-0.55f, 0.55f, 0, 0, -0.55f, 0.55f};
            L24.px[i] = X[i];
            L24.py[i] = Yv[i];
        } else {
            L24.px[i] = 0.8f * fcos(a);
            L24.py[i] = 0.8f * fsin(a);
        }
    }
}
static void lap_calc(void)
{
    L24.kind = P[0];
    L24.n = L24.kind == G_TWO ? 6 : P[1];
    L24.cut = P[2];
    lap_edges();
    int n = L24.n;
    qm_zero(&L24.L, n, n);
    for (int k = 0; k < L24.ne; k++) {
        if (k == L24.cut_e) {
            continue;
        }
        int a = L24.e[k][0], b = L24.e[k][1];
        L24.L.a[a][a] = q_add(L24.L.a[a][a], qi(1));
        L24.L.a[b][b] = q_add(L24.L.a[b][b], qi(1));
        L24.L.a[a][b] = q_sub(L24.L.a[a][b], qi(1));
        L24.L.a[b][a] = q_sub(L24.L.a[b][a], qi(1));
    }
    static fm_t F, V;
    fm_from(&F, &L24.L);
    fm_jacobi(&F, L24.w, &V);
    L24.pieces = 0;
    for (int i = 0; i < n; i++) {
        if (fabsf(L24.w[i]) < 1e-3f) {
            L24.w[i] = 0;
            L24.pieces++;
        }
        L24.fied[i] = V.a[i][1];
    }
    // Kirchhoff: the determinant of L without its first row and column
    static qm_t R;
    qm_zero(&R, n - 1, n - 1);
    for (int i = 1; i < n; i++) {
        for (int j = 1; j < n; j++) {
            R.a[i - 1][j - 1] = L24.L.a[i][j];
        }
    }
    L24.trees = qm_det(&R).n;
}
static const char *const GKIND[G_N][2] = {{"a path", "מסלול"},
                                          {"a cycle", "מעגל"},
                                          {"a star", "כוכב"},
                                          {"a full graph", "גרף מלא"},
                                          {"two triangles", "שני משולשים"}};
static void lap_text(int st)
{
    int n = L24.n;
    switch (st) {
    case 0:
        T(LG("#A graph as a matrix\n$L = D − A$: the degrees on the diagonal, and $−1$ for each edge.",
             "#גרף כמטריצה\n$L = D − A$: הדרגות על האלכסון, ו-$−1$ לכל קשת."));
        T(LG(" Here: %s of %d nodes.\n", " כאן: %s עם %d צמתים.\n"), GKIND[L24.kind][s_he], n);
        if (n <= 5) {
            T("=L = %s\n", MQ(&L24.L));
        }
        break;
    case 1:
        T(LG("#Its quadratic form\nA sum of squares over the edges:\n", "#התבנית הריבועית שלה\nסכום של ריבועים על "
                                                                        "הקשתות:\n"));
        T("=x^TLx = ∑_{edges}(x_i − x_j)^2 ≥ 0\n");
        T(LG("So no eigenvalue is negative, and $x = (1, …, 1)$ gives $λ = 0$.",
             "לכן אף ערך עצמי אינו שלילי, ו-$x = (1, …, 1)$ נותן $λ = 0$."));
        break;
    case 2:
        T(LG("#The zeros count the pieces\nThe number of zero eigenvalues is the number of connected pieces.\n",
             "#האפסים סופרים את החלקים\nמספר הערכים העצמיים שהם אפס הוא מספר החלקים המחוברים.\n"));
        T("=λ ≈ %s\n", LIST(L24.w, n, 2));
        T(LG("Here: *%d* piece%s. Turn the cut on to take out an edge.", "כאן: *%d* חלקים. הפעילו את החיתוך כדי להוציא "
                                                                         "קשת."),
          L24.pieces, L24.pieces == 1 ? "" : "s");
        break;
    case 3:
        T(LG("#The Fiedler vector\nThe eigenvector of the second smallest eigenvalue colours the nodes. Its sign "
             "splits "
             "the graph where it is weakest.\n",
             "#וקטור פידלר\nהווקטור העצמי של הערך העצמי השני בגודלו (מלמטה) צובע את הצמתים. הסימן שלו חותך את הגרף "
             "במקום החלש ביותר.\n"));
        T("=λ_2 ≈ %s\n", F(L24.w[1], 3));
        break;
    case 4:
        T(LG("#Count the trees\nKirchhoff: the number of spanning trees is any cofactor of $L$, or "
             "$@f{1}{n}λ_2⋯λ_n$:\n",
             "#סופרים עצים\nקירכהוף: מספר העצים הפורשים הוא כל מינור משלים של $L$, או $@f{1}{n}λ_2⋯λ_n$:\n"));
        T("=τ = @h{%s}\n", I(L24.trees));
        break;
    case 5:
        T(LG("#Known spectra\n", "#ספקטרום ידוע\n"));
        T("=@t{path}: 2 − 2cos@f{kπ}{n}@q @t{cycle}: 2 − 2cos@f{2πk}{n}\n");
        T("=@t{star}: 0, 1, …, 1, n@q @t{full}: 0, n, …, n\n");
        break;
    default:
        T(LG("#Why it is beautiful\nThe shape of a network is in the spectrum of one matrix. Spectral clustering, "
             "the heat on a graph and the tones of a drum all use $L$.",
             "#למה זה יפה\nהצורה של רשת נמצאת בספקטרום של מטריצה אחת. אשכול ספקטרלי, החום על גרף והצלילים של תוף "
             "כולם משתמשים ב-$L$."));
        break;
    }
}
static void lap_pic(rect_t r, int st, float t)
{
    int n = L24.n;
    float s = (r.w < r.h ? r.w : r.h) * 0.46f, cx0 = r.x + r.w / 2.0f, cy0 = r.y + r.h / 2.0f;
    float fm = 1e-6f;
    for (int i = 0; i < n; i++) {
        fm = fabsf(L24.fied[i]) > fm ? fabsf(L24.fied[i]) : fm;
    }
    float X[8], Yy[8];
    for (int i = 0; i < n; i++) {
        float bob = s_mini ? 0 : 0.08f * fsin(t * 2.2f) * L24.fied[i] / fm;
        X[i] = cx0 + L24.px[i] * s;
        Yy[i] = cy0 + (L24.py[i] + bob) * s;
    }
    for (int k = 0; k < L24.ne; k++) {
        int a = L24.e[k][0], b = L24.e[k][1];
        if (k == L24.cut_e) {
            // the cut edge: a dashed red line
            for (int i = 0; i < 8; i += 2) {
                float t0 = i / 8.0f, t1 = (i + 1) / 8.0f;
                wu(X[a] + (X[b] - X[a]) * t0, Yy[a] + (Yy[b] - Yy[a]) * t0, X[a] + (X[b] - X[a]) * t1,
                   Yy[a] + (Yy[b] - Yy[a]) * t1, COL_C2, 220);
            }
            continue;
        }
        tfd_line(X[a], Yy[a], X[b], Yy[b], s_mini ? 1.0f : 1.6f, rgb(120, 130, 170), 200);
    }
    for (int i = 0; i < n; i++) {
        uint16_t c = st >= 3 || s_mini || st < 0 ? heat(L24.fied[i] / fm) : COL_C1;
        if (st >= 3 && L24.pieces > 1) {
            c = heat(L24.fied[i] / fm);
        }
        tfd_disc(X[i], Yy[i], s_mini ? 3.0f : 6.5f, c, 256);
        tfd_ring(X[i], Yy[i], s_mini ? 3.0f : 6.5f, 1.2f, rgb(220, 226, 240), 200);
    }
}
static const char *const GK_NAMES[] = {"PATH", "מסלול", "CYCLE", "מעגל", "STAR", "כוכב", "FULL", "מלא", "TWO", "שניים"};
static const char *const CUT_NAMES[] = {"OFF", "כבוי", "ON", "פועל"};
static const topic_t TP_LAP = {{"The graph Laplacian", "הלפלסיאן של גרף"},
                               3,
                               3,
                               7,
                               {{"@t{graph}", 0, 4, 0, 1}, {"n", 3, 8, 6, 1}, {"@t{cut}", 0, 1, 0, 1}},
                               lap_calc,
                               lap_text,
                               lap_pic,
                               {GK_NAMES, NULL, CUT_NAMES, NULL}};
