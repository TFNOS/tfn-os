// LINEAR ALGEBRA: the tricks: the 2 x 2 shortcuts, triangular and block matrices, circulant, tridiagonal Toeplitz,
// rank-one updates, Vandermonde, mirror symmetry, Kronecker products and sums, Gershgorin discs, Cayley-Hamilton.

// ---------------------------------------------------------------- shared pictures
// the cells of a matrix as soft colours, the value in each cell (when there is room); hl = a mask of the cells to
// mark (bit i * 8 + j), a ring of the accent colour round them
static void mat_pic(rect_t r, const float *m, int n, int cols, float vmax, const uint64_t hl, float pulse)
{
    int cw = r.w / cols, ch = r.h / n;
    int c = cw < ch ? cw : ch;
    int x0 = r.x + (r.w - c * cols) / 2, y0 = r.y + (r.h - c * n) / 2;
    const mfont_t *f = c >= 34 ? &LA_F15 : c >= 24 ? &LA_F13 : &LA_F11;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < cols; j++) {
            float v = m[i * cols + j];
            int x = x0 + j * c, y = y0 + i * c;
            tfd_rrect(x + 1, y + 1, c - 2, c - 2, c > 14 ? 4 : 2, heat(v / (vmax > 0 ? vmax : 1)), 256);
            if ((hl >> (i * 8 + j)) & 1) {
                tfd_rrect(x + 1, y + 1, c - 2, 2, 1, COL_ACC, (int)(160 + 90 * pulse));
                tfd_rrect(x + 1, y + c - 3, c - 2, 2, 1, COL_ACC, (int)(160 + 90 * pulse));
            }
            if (!s_mini && c >= 18) {
                char t[24];
                s_f(t, sizeof t, v, 1);
                int w = 0;
                for (const char *p = t; *p;) {
                    w += ty_adv(f, utf8_next(&p));
                }
                int x4 = (x + c / 2) * 4 - w / 2;
                for (const char *p = t; *p;) {
                    x4 += ty_put(f, utf8_next(&p), x4, y + c / 2 + f->asc / 2, rgb(236, 240, 250), 230);
                }
            }
        }
    }
}
// a row of values on a line from lo to hi at the height y (px): dots, a value that repeats stacks up
static void line_dots(rect_t r, float y, float lo, float hi, const float *v, int n, uint16_t c, float rad)
{
    float sx = (r.w - 16) / (hi - lo > 0.01f ? hi - lo : 1);
    wu((float)r.x + 6, y, (float)(r.x + r.w - 6), y, rgb(100, 110, 150), 200);
    for (int k = (int)lo; k <= (int)hi; k++) {
        float x = r.x + 8 + (k - lo) * sx;
        wu(x, y - 2, x, y + 2, rgb(100, 110, 150), 180);
    }
    for (int i = 0; i < n; i++) {
        int same = 0;
        for (int j = 0; j < i; j++) {
            if (fabsf(v[j] - v[i]) < 1e-3f) {
                same++;
            }
        }
        tfd_disc(r.x + 8 + (v[i] - lo) * sx, y - same * rad * 2.2f, rad, c, 256);
    }
}
// a line of values that the text gives: "λ = 1, 3, 3 − k, 3 + k" -> "1, 3, 2, 4"
static const char *LIST(const float *v, int n, int p)
{
    char *o = nb();
    int L = 0;
    for (int i = 0; i < n && L < 400; i++) {
        char t[32];
        float rv = (float)lroundf(v[i]);
        if (fabsf(v[i] - rv) < 1e-4f) {
            s_int(t, sizeof t, (int64_t)rv);
        } else {
            s_f(t, sizeof t, v[i], p);
        }
        L += snprintf(o + L, 420 - L, "%s%s", i ? ", " : "", t);
    }
    return o;
}
static void sortf(float *v, int n)
{
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (v[j] < v[i]) {
                float t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
        }
    }
}

// ================================================================ 9 THE 2 x 2 SHORTCUTS
static struct {
    int a, b, c, d;
    eig2_t e;
    float ez[3];
} H9;
static void s2_calc(void)
{
    H9.a = P[0];
    H9.b = P[1];
    H9.c = P[2];
    H9.d = P[3];
    eig2(H9.a, H9.b, H9.c, H9.d, &H9.e);
}
static void s2_text(int st)
{
    const eig2_t *e = &H9.e;
    int r1 = H9.a + H9.b, r2 = H9.c + H9.d;
    switch (st) {
    case 0:
        T(LG("#Mean and product\nThe two eigenvalues have the mean $m = @f{@t{tr} A}{2}$ and the product $p = det A$. "
             "So they are $m ± d$ with $m^2 − d^2 = p$:\n",
             "#ממוצע ומכפלה\nלשני הערכים העצמיים יש ממוצע $m = @f{@t{tr} A}{2}$ ומכפלה $p = det A$. לכן הם $m ± d$ "
             "עם $m^2 − d^2 = p$:\n"));
        T("=λ = m ± @r{m^2 − p}\n");
        break;
    case 1:
        T(LG("#Your matrix\n", "#המטריצה שלכם\n"));
        T("=A = %s,   m = %s,   p = %s\n", A2(), QD(qn(e->tr, 2)), I(e->det));
        T("=λ = %s ± @r{%s} = @h{%s}\n", QD(qn(e->tr, 2)), QD(qn(e->D, 4)), EIGS(e, true));
        break;
    case 2:
        T(LG("#Equal row sums\nWhen each row has the same sum $s$, then $A@m{1;1} = s@m{1;1}$: $s$ is an eigenvalue, "
             "and the other one is $@t{tr} A − s$.\n",
             "#סכומי שורות שווים\nכאשר לכל שורה אותו סכום $s$, אז $A@m{1;1} = s@m{1;1}$: $s$ הוא ערך עצמי, והשני "
             "הוא $@t{tr} A − s$.\n"));
        if (r1 == r2) {
            T(LG("Here both rows sum to $%s$: $λ = %s$ and $%s$.", "כאן סכום שתי השורות $%s$: $λ = %s$ ו-$%s$."), I(r1),
              I(r1), I(e->tr - r1));
        } else {
            T(LG("Here the rows sum to $%s$ and $%s$. Make them equal to see it.",
                 "כאן סכומי השורות הם $%s$ ו-$%s$. השוו ביניהם כדי לראות את זה."),
              I(r1), I(r2));
        }
        break;
    case 3:
        T(LG("#Quick signs\n", "#סימנים מהירים\n"));
        T(LG("$det A < 0$: one positive and one negative eigenvalue.\n$(@t{tr} A)^2 < 4 det A$: a complex pair (a "
             "turn).\n$@t{tr} A = 0$: $λ = ±@r{−det A}$.\n",
             "$det A < 0$: ערך עצמי חיובי אחד ושלילי אחד.\n$(@t{tr} A)^2 < 4 det A$: זוג מרוכב (סיבוב).\n"
             "$@t{tr} A = 0$: $λ = ±@r{−det A}$.\n"));
        if (e->det < 0) {
            T(LG("*Here: the first one.*", "*כאן: הראשון.*"));
        } else if (e->D < 0) {
            T(LG("*Here: the second one.*", "*כאן: השני.*"));
        } else if (e->tr == 0) {
            T(LG("*Here: the third one.*", "*כאן: השלישי.*"));
        }
        break;
    case 4:
        T(LG("#The plane of trace and determinant\nEach 2 × 2 matrix is a point. Over the parabola $@t{tr}^2 = 4det$ "
             "the eigenvalues are complex; under it, real; under the axis, of two signs.",
             "#מישור העקבה והדטרמיננטה\nכל מטריצה 2 × 2 היא נקודה. מעל הפרבולה $@t{tr}^2 = 4det$ הערכים העצמיים "
             "מרוכבים; מתחתיה, ממשיים; מתחת לציר, בשני סימנים."));
        break;
    default:
        T(LG("#Why it is beautiful\nTwo numbers that you read at a glance, the trace and the determinant, hold all "
             "the spectrum of a 2 × 2 matrix.",
             "#למה זה יפה\nשני מספרים שקוראים במבט אחד, העקבה והדטרמיננטה, מכילים את כל הספקטרום של מטריצה 2 × 2."));
        break;
    }
}
// the plane of the trace (x) and the determinant (y): the parabola, the regions, the point of A
static void s2_pic(rect_t r, int st, float t)
{
    float tr = (float)H9.e.tr, det = (float)H9.e.det;
    EZ(&H9.ez[0], tr);
    EZ(&H9.ez[1], det);
    if (s_mini || st < 0) {
        H9.ez[0] = tr;
        H9.ez[1] = det;
    }
    float X = 9, Yh = 18;  // the trace from -9 to 9, the determinant from -18 to 18 (scaled)
    s_pr = r;
    float sx = r.w / (2 * X), sy = r.h / (2 * Yh);
    float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f;
    // the complex region (over the parabola): soft colour, in columns
    for (int px = r.x; px < r.x + r.w; px += 2) {
        float x = (px - cx) / sx, yp = x * x / 4;
        float sy0 = cy - yp * sy;
        if (sy0 > r.y) {
            tfd_rect(px, r.y, 2, (int)(sy0 - r.y), COL_C4, 34);
        }
    }
    tfd_rect(r.x, (int)cy, r.w, r.h / 2, COL_C2, 22);  // det < 0
    wu((float)r.x, cy, (float)(r.x + r.w), cy, rgb(110, 120, 160), 220);
    wu(cx, (float)r.y, cx, (float)(r.y + r.h), rgb(110, 120, 160), 220);
    float px0 = (float)r.x, py0 = cy - X * X / 4 * sy;
    for (int i = 1; i <= 60; i++) {
        float x = -X + 2 * X * i / 60, nx = cx + x * sx, ny = cy - x * x / 4 * sy;
        tfd_line(px0, py0, nx, ny, s_mini ? 1.2f : 1.8f, COL_C4, 230);
        px0 = nx;
        py0 = ny;
    }
    float mx = cx + H9.ez[0] * sx, my = cy - H9.ez[1] * sy;
    float pr = s_mini ? 2.5f : 4.0f + 0.8f * fsin(t * 4);
    if (!s_mini) {
        tfd_glow(mx, my, 14, COL_ACC, 140);
    }
    tfd_disc(mx, my, pr, COL_ACC, 256);
    if (!s_mini && st >= 0) {
        int keep = s_mbase;
        s_mbase = s_tsz > 1 ? s_tsz - 1 : 0;
        const char *p = "@t{tr}";
        m_list(&p, 0, (r.x + r.w - 20) * 4, (int)cy - 4, true, COL_DIM);
        p = "det";
        m_list(&p, 0, ((int)cx + 4) * 4, r.y + 14, true, COL_DIM);
        s_mbase = keep;
    }
}
static const topic_t TP_2X2 = {{"Shortcuts for 2 × 2", "קיצורי דרך ל-2 × 2"},
                               2,
                               4,
                               6,
                               {{"a", -4, 4, 4, 1}, {"b", -4, 4, 1, 1}, {"c", -4, 4, 2, 1}, {"d", -4, 4, 3, 1}},
                               s2_calc,
                               s2_text,
                               s2_pic};

// ================================================================ 10 TRIANGULAR MATRICES
static struct {
    qm_t T;
    int d[3];
    float ez[3];
} R10;
static void tri_calc(void)
{
    for (int i = 0; i < 3; i++) {
        R10.d[i] = P[i];
    }
    int v[9] = {P[0], 2, -1, 0, P[1], 4, 0, 0, P[2]};
    qm_ints(&R10.T, 3, 3, v);
}
// a basis of the kernel of A (exact); returns the number of vectors
static int qm_null(const qm_t *A, q_t out[][QMAX])
{
    static qm_t R;
    R = *A;
    int rank, piv[QMAX];
    qm_rref(&R, &rank, piv);
    int nk = 0;
    for (int f = 0; f < A->m; f++) {
        if (piv[f]) {
            continue;
        }
        for (int i = 0; i < A->m; i++) {
            out[nk][i] = qi(i == f);
        }
        int r = 0;
        for (int c = 0; c < A->m; c++) {
            if (piv[c]) {
                out[nk][c] = q_neg(R.a[r][f]);
                r++;
            }
        }
        // whole numbers: times the common denominator
        int64_t L = 1;
        for (int i = 0; i < A->m; i++) {
            L = L / gcd64(L, out[nk][i].d) * out[nk][i].d;
        }
        for (int i = 0; i < A->m; i++) {
            out[nk][i] = q_mul(out[nk][i], qi((int)L));
        }
        nk++;
    }
    return nk;
}
static void tri_text(int st)
{
    const int *d = R10.d;
    switch (st) {
    case 0:
        T(LG("#Triangular\nAll the numbers under the diagonal are zero.\n",
             "#משולשית\nכל המספרים מתחת לאלכסון הם אפס.\n"));
        T("=T = @m{%s&2&−1;0&%s&4;0&0&%s}\n", PV(0), PV(1), PV(2));
        break;
    case 1:
        T(LG("#The determinant\nExpand down the first column, again and again: only the diagonal is left.\n",
             "#הדטרמיננטה\nפותחים לפי העמודה הראשונה, שוב ושוב: נשאר רק האלכסון.\n"));
        T("=det T = %s·%s·%s = @h{%s}\n", PVP(0), PVP(1), PVP(2), I(d[0] * d[1] * d[2]));
        break;
    case 2:
        T(LG("#The eigenvalues: the diagonal\n$T − λI$ is triangular too, so its determinant is a product:\n",
             "#הערכים העצמיים: האלכסון\nגם $T − λI$ משולשית, ולכן הדטרמיננטה שלה היא מכפלה:\n"));
        T("=det(T − λI) = (%s − λ)(%s − λ)(%s − λ)\n", I(d[0]), I(d[1]), I(d[2]));
        T("=λ = %s, %s, %s\n", PV(0), PV(1), PV(2));
        break;
    case 3: {
        T(LG("#The eigenvectors\nThe numbers over the diagonal do not change the eigenvalues, only the vectors:\n",
             "#הווקטורים העצמיים\nהמספרים מעל האלכסון לא משנים את הערכים העצמיים, רק את הווקטורים:\n"));
        int shown = 0, L = 0;
        char line[400];
        line[0] = 0;
        for (int k = 0; k < 3; k++) {
            bool rep = false;
            for (int j = 0; j < k; j++) {
                rep |= d[j] == d[k];
            }
            if (rep) {
                continue;
            }
            static qm_t M;
            M = R10.T;
            for (int i = 0; i < 3; i++) {
                M.a[i][i] = q_sub(M.a[i][i], qi(d[k]));
            }
            static q_t v[QMAX][QMAX];
            int nk = qm_null(&M, v);
            for (int j = 0; j < nk && shown < 3; j++, shown++) {
                L += snprintf(line + L, sizeof line - L, "%sλ = %s: %s", shown ? ", " : "", I(d[k]), V3Q(v[j]));
            }
        }
        T("=%s\n", line);
        break;
    }
    case 4:
        T(LG("#Every matrix is triangular\n…in a good basis (Schur): $A = QTQ^*$ with complex numbers. So the "
             "eigenvalues of any matrix sit on a diagonal, and computers find them that way (the QR method).",
             "#כל מטריצה היא משולשית\n…בבסיס טוב (שור): $A = QTQ^*$ עם מספרים מרוכבים. לכן הערכים העצמיים של כל "
             "מטריצה יושבים על אלכסון, וכך המחשבים מוצאים אותם (שיטת QR)."));
        break;
    default:
        T(LG("#Why it is beautiful\nA triangle of zeros turns a hard question into reading the diagonal. Each "
             "method of solution (elimination, LU, QR) works to reach this shape.",
             "#למה זה יפה\nמשולש של אפסים הופך שאלה קשה לקריאה של האלכסון. כל שיטת פתרון (חילוץ, LU, QR) עובדת כדי "
             "להגיע לצורה הזו."));
        break;
    }
}
static void tri_pic(rect_t r, int st, float t)
{
    float m[9];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m[i * 3 + j] = q_f(R10.T.a[i][j]);
        }
    }
    rect_t g = R(r.x, r.y + 4, r.w, r.h * 70 / 100);
    float pulse = 0.5f + 0.5f * fsin(t * 3);
    mat_pic(g, m, 3, 3, 4, (1ull << 0) | (1ull << 9) | (1ull << 18), (st == 1 || st == 2) ? pulse : 0.3f);
    float v[3] = {(float)R10.d[0], (float)R10.d[1], (float)R10.d[2]};
    line_dots(r, r.y + r.h * 0.86f, -3.5f, 3.5f, v, 3, COL_ACC, s_mini ? 2.0f : 3.4f);
}
static const topic_t TP_TRI = {{"Triangular matrices", "מטריצות משולשיות"},
                               2,
                               3,
                               6,
                               {{"d_1", -3, 3, 2, 1}, {"d_2", -3, 3, -1, 1}, {"d_3", -3, 3, 3, 1}},
                               tri_calc,
                               tri_text,
                               tri_pic};

// ================================================================ 11 BLOCK MATRICES
static struct {
    int p, k;
    qm_t M;
    float eig[4];
} B11;
static void blk_calc(void)
{
    int p = P[0], k = P[1];
    B11.p = p;
    B11.k = k;
    int v[16] = {2, 1, p, 0, 1, 2, 1, p, 0, 0, 3, k, 0, 0, k, 3};
    qm_ints(&B11.M, 4, 4, v);
    float e[4] = {1, 3, 3.0f - k, 3.0f + k};
    sortf(e, 4);
    memcpy(B11.eig, e, sizeof e);
}
static void blk_text(int st)
{
    int p = B11.p, k = B11.k;
    switch (st) {
    case 0:
        T(LG("#Blocks\nCut M into 2 × 2 blocks. A zero block under the diagonal makes M *block triangular*.\n",
             "#בלוקים\nחותכים את M לבלוקים 2 × 2. בלוק אפס מתחת לאלכסון הופך את M ל*משולשית בלוקים*.\n"));
        T("=M = @m{A&B;0&D} = @m{2&1&%s&0;1&2&1&%s;0&0&3&%s;0&0&%s&3}\n", PV(0), PV(0), PV(1), PV(1));
        break;
    case 1:
        T(LG("#The determinant\nThe determinant of the blocks on the diagonal, times each other:\n",
             "#הדטרמיננטה\nהדטרמיננטות של הבלוקים שעל האלכסון, כפולות זו בזו:\n"));
        T("=det M = det A · det D = 3 · (9 − %s^2) = @h{%s}\n", PVP(1), I(3 * (9 - k * k)));
        break;
    case 2:
        T(LG("#B does not count\nChange $p$: the determinant and the eigenvalues stay the same. Only the "
             "eigenvectors move.",
             "#B לא משנה\nשנו את $p$: הדטרמיננטה והערכים העצמיים נשארים אותו דבר. רק הווקטורים העצמיים זזים."));
        T("\n=p = %s:   det M = %s\n", PV(0), I(3 * (9 - k * k)));
        break;
    case 3:
        T(LG("#The eigenvalues\nThe eigenvalues of the diagonal blocks, together:\n",
             "#הערכים העצמיים\nהערכים העצמיים של הבלוקים שעל האלכסון, ביחד:\n"));
        T("=@t{eig} M = @{1, 3@} ∪ @{3 − k, 3 + k@} = @{%s@}\n", LIST(B11.eig, 4, 3));
        break;
    case 4: {
        T(LG("#A twin pattern\nFor $@m{A&B;B&A}$ the vectors $(x, x)$ and $(x, −x)$ split it into $A + B$ and "
             "$A − B$:\n",
             "#תבנית תאומה\nעבור $@m{A&B;B&A}$ הווקטורים $(x, x)$ ו-$(x, −x)$ מפרקים אותה ל-$A + B$ ול-$A − B$:\n"));
        T("=det = det(A + B) · det(A − B)\n");
        int dp = (2 + p) * (2 + p) - 2, dm = (2 - p) * (2 - p);
        T("=@d{%s&1;2&%s} · @d{%s&1;0&%s} = %s · %s = @h{%s}\n", I(2 + p), I(2 + p), I(2 - p), I(2 - p), IP(dp), IP(dm),
          I(dp * dm));
        break;
    }
    default:
        T(LG("#Why it is beautiful\nBig matrices are often made of blocks. These rules cut one big problem into small "
             "ones, and a block of zeros is a gift.",
             "#למה זה יפה\nמטריצות גדולות בנויות פעמים רבות מבלוקים. הכללים האלה חותכים בעיה גדולה אחת לבעיות "
             "קטנות, ובלוק של אפסים הוא מתנה."));
        break;
    }
}
static void blk_pic(rect_t r, int st, float t)
{
    float m[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            m[i * 4 + j] = q_f(B11.M.a[i][j]);
        }
    }
    rect_t g = R(r.x, r.y + 4, r.w, r.h * 70 / 100);
    float pulse = 0.5f + 0.5f * fsin(t * 3);
    uint64_t hl = 0;
    if (st == 2 || st == 0) {
        hl = (1ull << 2) | (1ull << 3) | (1ull << 10) | (1ull << 11);  // the block B
    }
    mat_pic(g, m, 4, 4, 4, hl, pulse);
    // the lines of the blocks
    int c = (g.w < g.h ? g.w : g.h) / 4, x0 = g.x + (g.w - 4 * c) / 2, y0 = g.y + (g.h - 4 * c) / 2;
    wu((float)x0 + 2 * c, (float)y0, (float)x0 + 2 * c, (float)y0 + 4 * c, COL_TEXT, 200);
    wu((float)x0, (float)y0 + 2 * c, (float)x0 + 4 * c, (float)y0 + 2 * c, COL_TEXT, 200);
    line_dots(r, r.y + r.h * 0.88f, -1, 7, B11.eig, 4, COL_ACC, s_mini ? 2.0f : 3.4f);
}
static const topic_t TP_BLOCK = {{"Block matrices", "מטריצות בלוקים"},    2,        2,        6,
                                 {{"p", -3, 3, 2, 1}, {"k", 0, 3, 1, 1}}, blk_calc, blk_text, blk_pic};

// ================================================================ 12 CIRCULANT MATRICES
static struct {
    int n, a, b;
    qm_t C;
    cx_t lam[8];
    float det;
    float ez[3];
} C12;
static void circ_calc(void)
{
    int n = P[0], a = P[1], b = P[2];
    C12.n = n;
    C12.a = a;
    C12.b = b;
    qm_zero(&C12.C, n, n);
    for (int i = 0; i < n; i++) {
        C12.C.a[i][i] = qi(2);
        C12.C.a[i][(i + 1) % n] = q_add(C12.C.a[i][(i + 1) % n], qi(a));
        C12.C.a[i][(i + n - 1) % n] = q_add(C12.C.a[i][(i + n - 1) % n], qi(b));
    }
    float pr = 1, pi = 0;
    for (int k = 0; k < n; k++) {
        float th = 6.2831853f * k / n;
        C12.lam[k] = cx(2 + (a + b) * fcos(th), (a - b) * fsin(th));
        float nr = pr * C12.lam[k].re - pi * C12.lam[k].im, ni = pr * C12.lam[k].im + pi * C12.lam[k].re;
        pr = nr;
        pi = ni;
    }
    C12.det = pr;
}
static void circ_text(int st)
{
    int n = C12.n;
    switch (st) {
    case 0:
        T(LG("#Circulant\nEach row is the row above it, moved one place to the right (and round).\n",
             "#מטריצה מעגלית\nכל שורה היא השורה שמעליה, מוזזת מקום אחד ימינה (ומסביב).\n"));
        T("=C = %s\n", MQ(&C12.C));
        break;
    case 1:
        T(LG("#The same eigenvectors for all\nWith $ω = e^{2πi/n}$, the Fourier vectors are eigenvectors of *every* "
             "circulant matrix of size $n$:\n",
             "#אותם וקטורים עצמיים לכולן\nעם $ω = e^{2πi/n}$, וקטורי פורייה הם וקטורים עצמיים של *כל* מטריצה מעגלית "
             "בגודל $n$:\n"));
        T("=f_k = (1, ω^k, ω^{2k}, …, ω^{(n−1)k})\n");
        break;
    case 2: {
        T(LG("#The eigenvalues\nPut $ω^k$ into the first row:\n", "#הערכים העצמיים\nמציבים את $ω^k$ בשורה הראשונה:\n"));
        T("=λ_k = 2 + aω^k + bω^{−k}\n");
        int L = 0;
        char buf[400];
        for (int k = 0; k < n && L < 380; k++) {
            L += snprintf(buf + L, sizeof buf - L, "%s$λ_%d ≈ %s$", k ? ", " : "", k, CX(C12.lam[k], 2));
        }
        T("%s", buf);
        break;
    }
    case 3: {
        float rd = (float)lroundf(C12.det);
        T(LG("#The determinant\nThe product of the eigenvalues (the complex ones come in pairs):\n",
             "#הדטרמיננטה\nמכפלת הערכים העצמיים (המרוכבים באים בזוגות):\n"));
        T("=det C = λ_0λ_1⋯λ_{%d} = @h{%s}\n", n - 1, fabsf(C12.det - rd) < 0.02f ? I((int64_t)rd) : F(C12.det, 3));
        break;
    }
    case 4:
        T(LG("#The symmetric case\nWhen $a = b$, $C$ is symmetric and all its eigenvalues are real:\n",
             "#המקרה הסימטרי\nכאשר $a = b$, $C$ סימטרית וכל הערכים העצמיים שלה ממשיים:\n"));
        T("=λ_k = 2 + 2a cos@f{2πk}{n}\n");
        T(C12.a == C12.b ? LG("*Here $a = b$.*", "*כאן $a = b$.*")
                         : LG("Here $a ≠ b$: make them equal.", "כאן $a ≠ b$: השוו אותם."));
        break;
    default:
        T(LG("#Why it is beautiful\nOne transform, the discrete Fourier transform, makes every circulant matrix "
             "diagonal at once. This is why the FFT makes convolution fast.",
             "#למה זה יפה\nהתמרה אחת, התמרת פורייה הבדידה, הופכת כל מטריצה מעגלית לאלכסונית בבת אחת. זו הסיבה "
             "שה-FFT הופך קונבולוציה למהירה."));
        break;
    }
}
static void circ_pic(rect_t r, int st, float t)
{
    float R0 = fabsf((float)C12.a) + fabsf((float)C12.b);
    float span = 2 * (R0 > 1 ? R0 : 1) + 2.2f;
    EZ(&C12.ez[0], span);
    if (s_mini || st < 0) {
        C12.ez[0] = span;
    }
    view_set(r, C12.ez[0], 2, 0);
    // the axes of the complex plane
    seg(2 - 20, 0, 2 + 20, 0, 1, rgb(90, 100, 140), 200);
    seg(0, -20, 0, 20, 1, rgb(90, 100, 140), 200);
    // the curve 2 + a e^(iθ) + b e^(-iθ) (an ellipse) and the eigenvalues on it
    float a = (float)C12.a, b = (float)C12.b;
    ellipse(a + b, 0, 0, a - b, 2, 0, 1, COL_C4, 170);
    float rot = s_mini ? 0 : t * 0.25f;
    for (int k = 0; k < C12.n; k++) {
        float th = 6.2831853f * k / C12.n + rot;
        float x = 2 + (a + b) * fcos(th), y = (a - b) * fsin(th);
        // the spoke from the middle, then the value
        seg(2, 0, x, y, 1, COL_C1, 110);
        dot(x, y, s_mini ? 2.2f : 4.0f, k == 0 ? COL_ACC : COL_C2, 256);
    }
    dot(2, 0, 2, COL_DIM, 256);
}
static const topic_t TP_CIRC = {{"Circulant matrices", "מטריצות מעגליות"},
                                2,
                                3,
                                6,
                                {{"n", 3, 6, 5, 1}, {"a", -2, 3, 2, 1}, {"b", -2, 3, 1, 1}},
                                circ_calc,
                                circ_text,
                                circ_pic};

// ================================================================ 13 TRIDIAGONAL TOEPLITZ
static struct {
    int n, b, k;
    float lam[9];
    int64_t det;
} T13;
static void toep_calc(void)
{
    int n = P[0], b = P[1], k = P[2];
    if (k > n) {
        k = n;
    }
    T13.n = n;
    T13.b = b;
    T13.k = k;
    for (int j = 1; j <= n; j++) {
        T13.lam[j - 1] = 2 + 2 * b * fcos(3.14159265f * j / (n + 1));
    }
    // D_n = 2 D_(n-1) - b^2 D_(n-2)
    int64_t d0 = 1, d1 = 2;
    for (int i = 2; i <= n; i++) {
        int64_t d2 = 2 * d1 - (int64_t)b * b * d0;
        d0 = d1;
        d1 = d2;
    }
    T13.det = n == 0 ? 1 : d1;
}
static const char *TOEP_M(void)
{
    char *o = nb();
    int n = T13.n, L = 0;
    char bs[24];
    s_int(bs, sizeof bs, T13.b);
    // n > 5: the rows and the columns 1, 2, 3, the dots, n
    int show = n <= 5 ? n : 5;
    L += snprintf(o + L, 420 - L, "@m{");
    for (int i = 0; i < show; i++) {
        for (int j = 0; j < show; j++) {
            bool di = n > 5 && i == 3, dj = n > 5 && j == 3;
            int ri = n > 5 && i == 4 ? n - 1 : i, cj = n > 5 && j == 4 ? n - 1 : j;
            const char *v = ri == cj ? "2" : (ri - cj == 1 || cj - ri == 1) ? bs : "0";
            if (di && dj) {
                v = "⋱";
            } else if (di) {
                v = "⋮";
            } else if (dj) {
                v = "⋯";
            }
            L += snprintf(o + L, 420 - L, "%s%s", v, j + 1 < show ? "&" : "");
        }
        L += snprintf(o + L, 420 - L, "%s", i + 1 < show ? ";" : "}");
    }
    return o;
}
static void toep_text(int st)
{
    int n = T13.n;
    switch (st) {
    case 0:
        T(LG("#Tridiagonal Toeplitz\nThe same number on each diagonal, and only three diagonals:\n",
             "#טופליץ תלת-אלכסונית\nאותו מספר לאורך כל אלכסון, ורק שלושה אלכסונים:\n"));
        T("=T_{%s} = %s\n", PV(0), TOEP_M());
        break;
    case 1:
        T(LG("#Guess a sine\nTry $x_j = sin(jθ)$. With the identity\n=sin(α − θ) + sin(α + θ) = 2 sin α cos θ\n"
             "each row gives\n",
             "#ננחש סינוס\nננסה $x_j = sin(jθ)$. עם הזהות\n=sin(α − θ) + sin(α + θ) = 2 sin α cos θ\n"
             "כל שורה נותנת\n"));
        T("=(Tx)_j = (2 + 2b cos θ) x_j\n");
        break;
    case 2:
        T(LG("#The ends\nThe first and the last row need $sin 0 = 0$ and $sin((n+1)θ) = 0$:\n",
             "#הקצוות\nהשורה הראשונה והאחרונה צריכות $sin 0 = 0$ ו-$sin((n+1)θ) = 0$:\n"));
        T("=θ_k = @f{kπ}{n + 1},   k = 1, …, n\n");
        break;
    case 3:
        T(LG("#All the eigenvalues\n", "#כל הערכים העצמיים\n"));
        T("=λ_k = 2 + 2b cos@f{kπ}{%d}\n", n + 1);
        T("=λ ≈ %s\n", LIST(T13.lam, n, 2));
        T(LG("The picture shows the mode $k = %s$: the still points inside the string: $%d$.",
             "התמונה מראה את האופן $k = %s$: הנקודות השקטות בתוך המיתר: $%d$."),
          PV(2), T13.k - 1);
        break;
    case 4:
        T(LG("#The determinant\nThe corners obey $D_n = 2D_{n−1} − b^2D_{n−2}$:\n",
             "#הדטרמיננטה\nהפינות מקיימות $D_n = 2D_{n−1} − b^2D_{n−2}$:\n"));
        T("=det T_{%s} = @h{%s}\n", PV(0), I(T13.det));
        if (T13.b == -1 || T13.b == 1) {
            T(LG("For $b = ±1$ it is just $n + 1$.", "עבור $b = ±1$ זה פשוט $n + 1$."));
        }
        break;
    default:
        T(LG("#Why it is beautiful\nThese are the modes of a string. The same matrix is the second derivative of "
             "numerical work, and its eigenvalues are the roots of Chebyshev polynomials.",
             "#למה זה יפה\nאלה האופנים של מיתר. אותה מטריצה היא הנגזרת השנייה של חישובים נומריים, והערכים העצמיים "
             "שלה הם השורשים של פולינומי צ'בישב."));
        break;
    }
}
static void toep_pic(rect_t r, int st, float t)
{
    int n = T13.n, k = T13.k;
    s_pr = r;
    float x0 = r.x + 10, x1 = r.x + r.w - 10, cy = r.y + r.h * 0.5f, A = r.h * 0.32f;
    float om = sqrtf(fabsf(T13.lam[k - 1]) + 0.05f) * 2.2f;
    float ph = s_mini ? 1 : fcos(t * om);
    // the walls
    tfd_rect((int)x0 - 3, (int)(cy - A - 6), 3, (int)(2 * A + 12), COL_DIM, 200);
    tfd_rect((int)x1, (int)(cy - A - 6), 3, (int)(2 * A + 12), COL_DIM, 200);
    wu(x0, cy, x1, cy, rgb(60, 66, 100), 200);
    // the continuous sine (faint) and the beads
    float px = x0, py = cy;
    for (int i = 1; i <= 60; i++) {
        float u = (float)i / 60, nx = x0 + (x1 - x0) * u;
        float ny = cy - A * ph * fsin(3.14159265f * k * u);
        wu(px, py, nx, ny, COL_C4, 130);
        px = nx;
        py = ny;
    }
    float bx = x0, by = cy;
    for (int j = 1; j <= n + 1; j++) {
        float u = (float)j / (n + 1), nx = x0 + (x1 - x0) * u;
        float ny = j <= n ? cy - A * ph * fsin(3.14159265f * j * k / (n + 1)) : cy;
        tfd_line(bx, by, nx, ny, s_mini ? 1.2f : 1.8f, COL_C1, 220);
        bx = nx;
        by = ny;
    }
    for (int j = 1; j <= n; j++) {
        float u = (float)j / (n + 1);
        float y = cy - A * ph * fsin(3.14159265f * j * k / (n + 1));
        tfd_disc(x0 + (x1 - x0) * u, y, s_mini ? 2.2f : 4.2f, COL_ACC, 256);
    }
}
static const topic_t TP_TOEP = {{"Tridiagonal Toeplitz", "טופליץ תלת-אלכסונית"},
                                2,
                                3,
                                6,
                                {{"n", 2, 9, 6, 1}, {"b", -2, 2, -1, 1}, {"k", 1, 9, 2, 1}},
                                toep_calc,
                                toep_text,
                                toep_pic};

// ================================================================ 14 RANK-ONE UPDATES
static struct {
    int n, a, b;
    int64_t det;
    float ez[2];
} U14;
static void r1_calc(void)
{
    U14.n = P[0];
    U14.a = P[1];
    U14.b = P[2];
    int64_t d = U14.a + (int64_t)U14.n * U14.b;
    for (int i = 0; i < U14.n - 1; i++) {
        d *= U14.a;
    }
    U14.det = d;
}
static const char *R1_M(int a, int b, int n)
{
    // aI + bJ, n x n (to 5 x 5: else with dots)
    char *o = nb();
    char d[24], f[24];
    s_int(d, sizeof d, a + b);
    s_int(f, sizeof f, b);
    int show = n <= 4 ? n : 4, L = snprintf(o, 420, "@m{");
    for (int i = 0; i < show; i++) {
        for (int j = 0; j < show; j++) {
            const char *v = i == j ? d : f;
            if (n > 4 && (i == 2 || j == 2)) {
                v = i == j ? "⋱" : i == 2 ? "⋮" : "⋯";
            }
            L += snprintf(o + L, 420 - L, "%s%s", v, j + 1 < show ? "&" : "");
        }
        L += snprintf(o + L, 420 - L, "%s", i + 1 < show ? ";" : "}");
    }
    return o;
}
static void r1_text(int st)
{
    int n = U14.n, a = U14.a, b = U14.b;
    switch (st) {
    case 0:
        T(LG("#All ones\n$J$ has a 1 in every place. It is one column times one row: rank one.\n",
             "#כולה אחדות\nב-$J$ יש 1 בכל מקום. היא עמודה אחת כפול שורה אחת: דרגה אחת.\n"));
        T("=J = @1{1}@1{1}^T = %s\n", R1_M(0, 1, n));
        break;
    case 1:
        T(LG("#The spectrum of J\n$J@1{1} = n@1{1}$, and $Jx = 0$ for each $x$ at a right angle to $@1{1}$:\n",
             "#הספקטרום של J\n$J@1{1} = n@1{1}$, ו-$Jx = 0$ לכל $x$ שמאונך ל-$@1{1}$:\n"));
        T("=@t{eig} J = %s, 0, …, 0\n", PV(0));
        T(LG("The value 0 is there $%s$ times.", "הערך 0 מופיע $%s$ פעמים."), I(n - 1));
        break;
    case 2:
        T(LG("#Shift and scale\n$aI + bJ$ has the same eigenvectors, so its eigenvalues are\n",
             "#הזזה וכיווץ\nל-$aI + bJ$ אותם וקטורים עצמיים, ולכן הערכים העצמיים שלה הם\n"));
        T("=a + nb = @h{%s}@q a = %s\n", I(a + n * b), I(a));
        T(LG("The first one time, the second $%s$ times.\n", "הראשון פעם אחת, השני $%s$ פעמים.\n"), I(n - 1));
        T("=aI + bJ = %s\n", R1_M(a, b, n));
        break;
    case 3:
        T(LG("#The determinant at once\nThe product of the eigenvalues, with no elimination:\n",
             "#הדטרמיננטה מיד\nמכפלת הערכים העצמיים, בלי חילוץ:\n"));
        T("=det(aI + bJ) = a^{n−1}(a + nb) = %s^{%d}·%s = @h{%s}\n", PVP(1), n - 1, IP(a + n * b), I(U14.det));
        break;
    case 4:
        T(LG("#The determinant lemma\nFor any invertible $A$ and vectors $u$, $v$:\n",
             "#הלמה של הדטרמיננטה\nלכל $A$ הפיכה ולכל וקטורים $u$, $v$:\n"));
        T("=det(A + uv^T) = det A · (1 + v^TA^{−1}u)\n");
        T(LG("With $A = aI$, $u = b@1{1}$, $v = @1{1}$ this is the formula above.",
             "עם $A = aI$, $u = b@1{1}$, $v = @1{1}$ זו הנוסחה שלמעלה."));
        break;
    default:
        T(LG("#Why it is beautiful\nA change of rank one moves only one eigenvalue. Sherman and Morrison used the "
             "same idea to update an inverse in $n^2$ steps in place of $n^3$.",
             "#למה זה יפה\nשינוי מדרגה אחת מזיז רק ערך עצמי אחד. שרמן ומוריסון השתמשו באותו רעיון כדי לעדכן הופכית "
             "ב-$n^2$ צעדים במקום $n^3$."));
        break;
    }
}
static void r1_pic(rect_t r, int st, float t)
{
    int n = U14.n, a = U14.a, b = U14.b;
    float m[36];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            m[i * n + j] = (float)(i == j ? a + b : b);
        }
    }
    rect_t g = R(r.x, r.y + 4, r.w, r.h * 62 / 100);
    mat_pic(g, m, n, n, 6, 0, 0);
    // the spectrum: n - 1 values at a, one at a + n b (it moves)
    float v[6];
    for (int i = 0; i < n - 1; i++) {
        v[i] = (float)a;
    }
    float far = (float)(a + n * b);
    EZ(&U14.ez[0], far);
    if (s_mini || st < 0) {
        U14.ez[0] = far;
    }
    v[n - 1] = U14.ez[0];
    float lo = -10, hi = 22;
    line_dots(r, r.y + r.h * 0.84f, lo, hi, v, n, COL_C2, s_mini ? 1.8f : 3.0f);
    if (!s_mini) {
        float sx = (r.w - 16) / (hi - lo), y = r.y + r.h * 0.84f + 9;
        float xa = r.x + 8 + (a - lo) * sx, xb = r.x + 8 + (U14.ez[0] - lo) * sx;
        float k = 0.5f + 0.5f * fsin(t * 2);
        tfd_line(xa, y, xa + (xb - xa) * k, y, 1.6f, COL_ACC, 220);
    }
}
static const topic_t TP_R1 = {{"Rank-one updates", "עדכונים מדרגה אחת"},
                              2,
                              3,
                              6,
                              {{"n", 2, 6, 4, 1}, {"a", -3, 4, 2, 1}, {"b", -2, 3, 1, 1}},
                              r1_calc,
                              r1_text,
                              r1_pic};

// ================================================================ 15 VANDERMONDE
static struct {
    int x[3];
    int64_t det;
    q_t c[3];  // the parabola c0 + c1 x + c2 x^2 through (x_i, y_i)
    float ez[3];
} V15;
static const int VDY[3] = {2, -1, 1};
static void vand_calc(void)
{
    for (int i = 0; i < 3; i++) {
        V15.x[i] = P[i];
    }
    int *x = V15.x;
    V15.det = (int64_t)(x[1] - x[0]) * (x[2] - x[0]) * (x[2] - x[1]);
    if (V15.det) {
        // Lagrange: c from the three points
        static qm_t V, W;
        int v[9];
        for (int i = 0; i < 3; i++) {
            v[i * 3] = 1;
            v[i * 3 + 1] = x[i];
            v[i * 3 + 2] = x[i] * x[i];
        }
        qm_ints(&V, 3, 3, v);
        // solve V c = y by Cramer
        q_t d = qm_det(&V);
        for (int k = 0; k < 3; k++) {
            W = V;
            for (int i = 0; i < 3; i++) {
                W.a[i][k] = qi(VDY[i]);
            }
            V15.c[k] = q_div(qm_det(&W), d);
        }
    }
}
static void vand_text(int st)
{
    const int *x = V15.x;
    switch (st) {
    case 0:
        T(LG("#Powers in the rows\n", "#חזקות בשורות\n"));
        T("=V = @m{1&x_1&x_1^2;1&x_2&x_2^2;1&x_3&x_3^2} = @m{1&%s&%s;1&%s&%s;1&%s&%s}\n", PV(0), I(x[0] * x[0]), PV(1),
          I(x[1] * x[1]), PV(2), I(x[2] * x[2]));
        break;
    case 1:
        T(LG("#Guess the determinant\nTwo equal $x$ give two equal rows, and det = 0. So each $(x_j − x_i)$ is a "
             "factor; the degrees match, and\n",
             "#ננחש את הדטרמיננטה\nשני $x$ שווים נותנים שתי שורות שוות, ו-det = 0. לכן כל $(x_j − x_i)$ הוא גורם; "
             "המעלות מתאימות, ו-\n"));
        T("=det V = (x_2 − x_1)(x_3 − x_1)(x_3 − x_2)\n");
        break;
    case 2:
        T(LG("#With your numbers\n", "#עם המספרים שלכם\n"));
        T("=det V = (%s)(%s)(%s) = @h{%s}\n", I(x[1] - x[0]), I(x[2] - x[0]), I(x[2] - x[1]), I(V15.det));
        break;
    case 3:
        T(LG("#One parabola through 3 points\nWhen $det V ≠ 0$ exactly one parabola goes through the points: solve "
             "$Vc = y$.\n",
             "#פרבולה אחת דרך 3 נקודות\nכאשר $det V ≠ 0$ בדיוק פרבולה אחת עוברת דרך הנקודות: פותרים $Vc = y$.\n"));
        if (V15.det) {
            T("=y = (2, −1, 1):   c = @m{%s;%s;%s}\n", QD(V15.c[0]), QD(V15.c[1]), QD(V15.c[2]));
        } else {
            T(LG("*Now two x are equal: no parabola, or many.*", "*עכשיו שני x שווים: אין פרבולה, או שיש הרבה.*"));
        }
        break;
    case 4:
        T(LG("#Roots of unity\nWith the points $1, ω, ω^2, …$ on the unit circle, $V$ is the matrix of the discrete "
             "Fourier transform, and $V^{−1} = @f{1}{n}V^*$.",
             "#שורשי היחידה\nעם הנקודות $1, ω, ω^2, …$ על מעגל היחידה, $V$ היא המטריצה של התמרת פורייה הבדידה, ו-"
             "$V^{−1} = @f{1}{n}V^*$."));
        break;
    default:
        T(LG("#Why it is beautiful\nA determinant that you can read without a calculation. Interpolation, the "
             "Fourier transform and error-correcting codes all rest on this one product.",
             "#למה זה יפה\nדטרמיננטה שאפשר לקרוא בלי חישוב. אינטרפולציה, התמרת פורייה וקודים לתיקון שגיאות נשענים "
             "כולם על המכפלה הזו."));
        break;
    }
}
static void vand_pic(rect_t r, int st, float t)
{
    view_set(r, 8.4f, 0, 0.3f);
    grid_plain(5, rgb(36, 42, 68), 90);
    uint16_t cc[3] = {COL_C1, COL_C2, COL_C3};
    for (int i = 0; i < 3; i++) {
        EZ(&V15.ez[i], (float)V15.x[i]);
        if (s_mini || st < 0) {
            V15.ez[i] = (float)V15.x[i];
        }
    }
    if (V15.det) {
        float c0 = q_f(V15.c[0]), c1 = q_f(V15.c[1]), c2 = q_f(V15.c[2]);
        float px = -5, py = c0 + c1 * px + c2 * px * px;
        for (int i = 1; i <= 80; i++) {
            float x = -5 + 10.0f * i / 80, y = c0 + c1 * x + c2 * x * x;
            if (fabsf(y) < 40 && fabsf(py) < 40) {
                seg(px, py, x, y, s_mini ? 1.3f : 2.0f, COL_C4, 230);
            }
            px = x;
            py = y;
        }
    }
    for (int i = 0; i < 3; i++) {
        float x = V15.ez[i];
        dashed(x, 0, x, (float)VDY[i], cc[i], 160);
        float pulse = s_mini ? 0 : 0.7f * fsin(t * 3 + i * 2);
        dot(x, (float)VDY[i], (s_mini ? 2.4f : 4.2f) + pulse, cc[i], 256);
    }
}
static const topic_t TP_VAND = {{"Vandermonde", "ונדרמונד"},
                                2,
                                3,
                                6,
                                {{"x_1", -3, 3, -1, 1}, {"x_2", -3, 3, 1, 1}, {"x_3", -3, 3, 2, 1}},
                                vand_calc,
                                vand_text,
                                vand_pic};

// ================================================================ 16 MIRROR SYMMETRY (centrosymmetric)
static struct {
    int p, q, r;
    qm_t M;
    eig2_t es, ek;  // the two 2 x 2 problems
    int64_t det;
} M16;
static void mirr_calc(void)
{
    int p = P[0], q = P[1], r = P[2];
    M16.p = p;
    M16.q = q;
    M16.r = r;
    int v[16] = {p, 1, r, q, 2, 3, 1, r, r, 1, 3, 2, q, r, 1, p};
    qm_ints(&M16.M, 4, 4, v);
    eig2(p + q, 1 + r, 2 + r, 4, &M16.es);
    eig2(p - q, 1 - r, 2 - r, 2, &M16.ek);
    M16.det = (int64_t)M16.es.det * M16.ek.det;
}
static void mirr_text(int st)
{
    int p = M16.p, q = M16.q, r = M16.r;
    switch (st) {
    case 0:
        T(LG("#A mirror matrix\nTurn $M$ by 180°: it stays the same.\n",
             "#מטריצת מראה\nמסובבים את $M$ ב-180°: היא נשארת אותו דבר.\n"));
        T("=M = @m{%s&1&%s&%s;2&3&1&%s;%s&1&3&2;%s&%s&1&%s}\n", PV(0), PV(2), PV(1), PV(2), PV(2), PV(1), PV(2), PV(0));
        break;
    case 1:
        T(LG("#Two kinds of vectors\nMirror vectors $(u, v, v, u)$ and anti-mirror vectors $(u, v, −v, −u)$. $M$ "
             "keeps each kind in its kind.",
             "#שני סוגים של וקטורים\nוקטורי מראה $(u, v, v, u)$ ווקטורי אנטי-מראה $(u, v, −v, −u)$. $M$ שומרת כל סוג "
             "בתוך הסוג שלו."));
        break;
    case 2:
        T(LG("#Two small problems\nOn each kind $M$ acts as a 2 × 2 matrix:\n",
             "#שתי בעיות קטנות\nעל כל סוג $M$ פועלת כמטריצה 2 × 2:\n"));
        T("=S = @m{p + q&1 + r;2 + r&4} = @m{%s&%s;%s&4}\n", I(p + q), I(1 + r), I(2 + r));
        T("=K = @m{p − q&1 − r;2 − r&2} = @m{%s&%s;%s&2}\n", I(p - q), I(1 - r), I(2 - r));
        break;
    case 3:
        T(LG("#The eigenvalues\nThe four eigenvalues of $M$ are those of $S$ and $K$:\n",
             "#הערכים העצמיים\nארבעת הערכים העצמיים של $M$ הם אלה של $S$ ושל $K$:\n"));
        T("=@t{eig} S = %s\n", EIGS(&M16.es, true));
        T("=@t{eig} K = %s\n", EIGS(&M16.ek, true));
        break;
    case 4:
        T(LG("#The determinant\n", "#הדטרמיננטה\n"));
        T("=det M = det S · det K = %s · %s = @h{%s}\n", IP(M16.es.det), IP(M16.ek.det), I(M16.det));
        break;
    default:
        T(LG("#Why it is beautiful\nA symmetry of the problem cuts it into smaller problems. With more symmetries, "
             "this is group theory: it explains the spectra of molecules and crystals.",
             "#למה זה יפה\nסימטריה של הבעיה חותכת אותה לבעיות קטנות יותר. עם יותר סימטריות, זו תורת החבורות: היא "
             "מסבירה את הספקטרום של מולקולות ושל גבישים."));
        break;
    }
}
static void mirr_pic(rect_t r, int st, float t)
{
    // the cells turn by 180° round the middle, and land on the same picture
    float k = s_mini ? 0 : ease_io(fpart(t * 0.35f) * 1.6f - 0.3f);
    float th = k * 3.14159265f, cs = fcos(th), sn = fsin(th);
    int c = (r.w < r.h ? r.w : r.h) * 88 / 400;
    float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float v = q_f(M16.M.a[i][j]);
            float ox = (j - 1.5f) * c, oy = (i - 1.5f) * c;
            float x = cx + ox * cs - oy * sn, y = cy + ox * sn + oy * cs;
            tfd_rrect((int)(x - c / 2 + 1), (int)(y - c / 2 + 1), c - 2, c - 2, c > 14 ? 4 : 2, heat(v / 4), 256);
        }
    }
    // the middle: a ring with an arrow of the turn
    if (!s_mini) {
        tfd_arc(cx, cy, c * 0.35f, 2, -1.2f, 1.2f + th, COL_ACC, 200);
    }
    (void)st;
}
static const topic_t TP_MIRR = {{"Mirror symmetry", "סימטריית מראה"},
                                2,
                                3,
                                6,
                                {{"p", -2, 4, 3, 1}, {"q", -2, 3, 1, 1}, {"r", -2, 3, 0, 1}},
                                mirr_calc,
                                mirr_text,
                                mirr_pic};

// ================================================================ 17 KRONECKER PRODUCTS AND SUMS
static struct {
    int p, q, r;
    float la[2], mu[2], prod[4], sum[4];
} K17;
static void kron_calc(void)
{
    K17.p = P[0];
    K17.q = P[1];
    K17.r = P[2];
    K17.la[0] = (float)K17.p;
    K17.la[1] = (float)K17.q;
    K17.mu[0] = (float)(K17.r - 1);
    K17.mu[1] = (float)(K17.r + 1);
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            K17.prod[i * 2 + j] = K17.la[i] * K17.mu[j];
            K17.sum[i * 2 + j] = K17.la[i] + K17.mu[j];
        }
    }
    sortf(K17.prod, 4);
    sortf(K17.sum, 4);
}
static void kron_text(int st)
{
    int p = K17.p, q = K17.q, r = K17.r;
    int dA = p * q, dB = r * r - 1;
    switch (st) {
    case 0:
        T(LG("#The Kronecker product\nPut a copy of $B$ in each place of $A$, times that number:\n",
             "#מכפלת קרונקר\nשמים עותק של $B$ בכל מקום של $A$, כפול המספר שבמקום:\n"));
        T("=A = @m{%s&1;0&%s},   B = @m{%s&1;1&%s}\n", PV(0), PV(1), PV(2), PV(2));
        T("=A ⊗ B = @m{%s&%s&%s&1;%s&%s&1&%s;0&0&%s&%s;0&0&%s&%s}\n", I(p * r), I(p), I(r), I(p), I(p * r), I(r),
          I(q * r), I(q), I(q), I(q * r));
        break;
    case 1:
        T(LG("#Its eigenvalues: all the products\n", "#הערכים העצמיים שלה: כל המכפלות\n"));
        T("=@t{eig} A = %s, %s@q @t{eig} B = %s, %s\n", I(p), I(q), I(r - 1), I(r + 1));
        T("=@t{eig}(A ⊗ B) = λ_iμ_j = @h{%s}\n", LIST(K17.prod, 4, 2));
        break;
    case 2:
        T(LG("#The Kronecker sum\nIts eigenvalues are all the sums:\n", "#סכום קרונקר\nהערכים העצמיים שלו הם כל "
                                                                        "הסכומים:\n"));
        T("=A ⊕ B = A ⊗ I + I ⊗ B\n");
        T("=@t{eig}(A ⊕ B) = λ_i + μ_j = @h{%s}\n", LIST(K17.sum, 4, 2));
        break;
    case 3:
        T(LG("#The determinant\n", "#הדטרמיננטה\n"));
        T("=det(A ⊗ B) = (det A)^2(det B)^2 = %s^2 · %s^2 = @h{%s}\n", IP(dA), IP(dB), I((int64_t)dA * dA * dB * dB));
        break;
    case 4:
        T(LG("#Drums\nThe Laplacian of a grid is the Kronecker sum of the Laplacians of two paths. So the tones of "
             "a box drum are the sums of the tones of two strings.",
             "#תופים\nהלפלסיאן של רשת הוא סכום קרונקר של הלפלסיאנים של שני מסלולים. לכן הצלילים של תוף מלבני הם "
             "הסכומים של הצלילים של שני מיתרים."));
        break;
    default:
        T(LG("#Why it is beautiful\nBig systems made of independent parts have spectra made of the spectra of the "
             "parts: products for $⊗$, sums for $⊕$. Quantum mechanics joins systems this way.",
             "#למה זה יפה\nלמערכות גדולות שבנויות מחלקים בלתי תלויים יש ספקטרום שבנוי מהספקטרום של החלקים: מכפלות "
             "עבור $⊗$, סכומים עבור $⊕$. כך מכניקת הקוונטים מחברת מערכות."));
        break;
    }
}
static void kron_pic(rect_t r, int st, float t)
{
    // three lines: the values of A, of B, and the sums (or the products), with a link from each pair
    s_pr = r;
    bool prod = st == 1 || st == 3 || st == 0;
    const float *res = prod ? K17.prod : K17.sum;
    float lo = -10, hi = 10;
    float sx = (r.w - 16) / (hi - lo);
    float yA = r.y + r.h * 0.22f, yB = r.y + r.h * 0.45f, yR = r.y + r.h * 0.8f;
    line_dots(r, yA, lo, hi, K17.la, 2, COL_C1, s_mini ? 2.2f : 3.6f);
    line_dots(r, yB, lo, hi, K17.mu, 2, COL_C2, s_mini ? 2.2f : 3.6f);
    float k = s_mini ? 1 : 0.5f + 0.5f * fsin(t * 1.5f);
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            float v = prod ? K17.la[i] * K17.mu[j] : K17.la[i] + K17.mu[j];
            float xa = r.x + 8 + (K17.la[i] - lo) * sx, xb = r.x + 8 + (K17.mu[j] - lo) * sx;
            float xr = r.x + 8 + (v - lo) * sx;
            wu(xa, yA, xr, yR, COL_C1, (int)(60 + 80 * k));
            wu(xb, yB, xr, yR, COL_C2, (int)(60 + 80 * k));
        }
    }
    line_dots(r, yR, lo, hi, res, 4, COL_ACC, s_mini ? 2.2f : 3.6f);
    if (!s_mini) {
        int keep = s_mbase;
        s_mbase = s_tsz > 1 ? s_tsz - 1 : 0;
        const char *labs_[3] = {"@1{A}", "@2{B}", prod ? "A ⊗ B" : "A ⊕ B"};
        float ys[3] = {yA, yB, yR};
        for (int i = 0; i < 3; i++) {
            const char *p = labs_[i];
            m_list(&p, 0, (r.x + 6) * 4, (int)ys[i] - 6, true, COL_DIM);
        }
        s_mbase = keep;
    }
}
static const topic_t TP_KRON = {
        {"Kronecker products and sums", "מכפלות וסכומי קרונקר"},      2,         3,         6,
        {{"p", -2, 3, 1, 1}, {"q", -2, 3, 2, 1}, {"r", -2, 2, 0, 1}}, kron_calc, kron_text, kron_pic};

// ================================================================ 18 GERSHGORIN DISCS
static struct {
    int p, q, s;
    qm_t A;
    float c[3], rad[3];
    cx_t lam[3];
    bool zero_out;  // 0 is in no disc
    float ez[2];
} G18;
static void gers_calc(void)
{
    int p = P[0], q = P[1], s = P[2];
    G18.p = p;
    G18.q = q;
    G18.s = s;
    int v[9] = {5, s, 1, 1, p, 1, s, 0, q};
    qm_ints(&G18.A, 3, 3, v);
    G18.zero_out = true;
    for (int i = 0; i < 3; i++) {
        G18.c[i] = q_f(G18.A.a[i][i]);
        float R0 = 0;
        for (int j = 0; j < 3; j++) {
            if (j != i) {
                R0 += fabsf(q_f(G18.A.a[i][j]));
            }
        }
        G18.rad[i] = R0;
        if (fabsf(G18.c[i]) <= R0) {
            G18.zero_out = false;
        }
    }
    qm_eig(&G18.A, G18.lam);
}
static void gers_text(int st)
{
    switch (st) {
    case 0:
        T(LG("#Discs from the rows\nFor each row: a disc round the number on the diagonal, with the radius $R_i$ = "
             "the sum of the sizes of the other numbers in the row.\n",
             "#עיגולים מהשורות\nלכל שורה: עיגול סביב המספר שעל האלכסון, ברדיוס $R_i$ = סכום הגדלים של שאר המספרים "
             "בשורה.\n"));
        T("=A = @m{5&%s&1;1&%s&1;%s&0&%s}\n", PV(2), PV(0), PV(2), PV(1));
        break;
    case 1:
        T(LG("#Every eigenvalue is in a disc\n", "#כל ערך עצמי נמצא בעיגול\n"));
        T("=|λ − a_{ii}| ≤ R_i  @t{ for some }i\n");
        T(LG("The discs (centre, radius):\n", "העיגולים (מרכז, רדיוס):\n"));
        T("=@1{D_1}: 5, %s@q @2{D_2}: %s, %s@q @3{D_3}: %s, %s\n", F(G18.rad[0], 0), I(G18.p), F(G18.rad[1], 0),
          I(G18.q), F(G18.rad[2], 0));
        break;
    case 2:
        T(LG("#Why\nTake an eigenvector $x$ and its largest part $x_i$. Row $i$ of $Ax = λx$ gives\n",
             "#למה\nלוקחים וקטור עצמי $x$ ואת החלק הגדול ביותר שלו $x_i$. שורה $i$ של $Ax = λx$ נותנת\n"));
        T("=|λ − a_{ii}||x_i| ≤ ∑_{j≠i}|a_{ij}||x_j| ≤ R_i|x_i|\n");
        break;
    case 3:
        T(LG("#Here\nThe eigenvalues (the bright points):\n", "#כאן\nהערכים העצמיים (הנקודות הבהירות):\n"));
        T("=λ ≈ %s,   %s,   %s\n", CX(G18.lam[0], 2), CX(G18.lam[1], 2), CX(G18.lam[2], 2));
        break;
    case 4:
        T(LG("#No zero, no problem\nWhen 0 is in no disc, 0 is not an eigenvalue: $A$ is invertible. A matrix "
             "whose diagonal is larger than the rest of each row is always invertible.\n",
             "#אין אפס, אין בעיה\nכאשר 0 לא בשום עיגול, 0 אינו ערך עצמי: $A$ הפיכה. מטריצה שהאלכסון שלה גדול "
             "משאר השורה, בכל שורה, תמיד הפיכה.\n"));
        T(G18.zero_out
                  ? LG("*Here 0 is outside all the discs.*", "*כאן 0 מחוץ לכל העיגולים.*")
                  : LG("Here 0 is inside a disc: the test says nothing.", "כאן 0 בתוך עיגול: הבדיקה לא אומרת כלום."));
        break;
    default:
        T(LG("#Why it is beautiful\nWith no calculation at all, you know where the eigenvalues live. A disc that is "
             "apart from the others holds exactly one of them.",
             "#למה זה יפה\nבלי שום חישוב יודעים איפה הערכים העצמיים גרים. עיגול שנפרד מהאחרים מכיל בדיוק אחד מהם."));
        break;
    }
}
static void gers_pic(rect_t r, int st, float t)
{
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 3; i++) {
        lo = G18.c[i] - G18.rad[i] < lo ? G18.c[i] - G18.rad[i] : lo;
        hi = G18.c[i] + G18.rad[i] > hi ? G18.c[i] + G18.rad[i] : hi;
    }
    float mid = (lo + hi) / 2, span = (hi - lo) + 1.6f;
    EZ(&G18.ez[0], mid);
    EZ(&G18.ez[1], span);
    if (s_mini || st < 0) {
        G18.ez[0] = mid;
        G18.ez[1] = span;
    }
    view_set(r, G18.ez[1], G18.ez[0], 0);
    seg(-40, 0, 40, 0, 1, rgb(90, 100, 140), 200);
    seg(0, -40, 0, 40, 1, rgb(90, 100, 140), 200);
    uint16_t cc[3] = {COL_C1, COL_C2, COL_C3};
    for (int i = 0; i < 3; i++) {
        float rr = G18.rad[i] * s_vs;
        if (rr > 0.5f) {
            tfd_disc(VX(G18.c[i]), VY(0), rr, cc[i], 46);
            tfd_ring(VX(G18.c[i]), VY(0), rr, 1.4f, cc[i], 220);
        }
        dot(G18.c[i], 0, 2, cc[i], 256);
    }
    for (int k = 0; k < 3; k++) {
        float pulse = s_mini ? 0 : 0.8f * fsin(t * 3 + k);
        if (!s_mini) {
            tfd_glow(VX(G18.lam[k].re), VY(G18.lam[k].im), 10, COL_ACC, 120);
        }
        dot(G18.lam[k].re, G18.lam[k].im, (s_mini ? 2.0f : 3.4f) + pulse, COL_ACC, 256);
    }
}
static const topic_t TP_GERS = {{"Gershgorin discs", "עיגולי גרשגורין"},
                                2,
                                3,
                                6,
                                {{"p", -4, 4, -2, 1}, {"q", -4, 4, 1, 1}, {"s", 0, 3, 1, 1}},
                                gers_calc,
                                gers_text,
                                gers_pic};

// ================================================================ 19 CAYLEY-HAMILTON
static struct {
    int a, b, c, d, tr, det;
    int64_t al, be;  // A^5 = al A + be I
    float ez[1];
} H19;
static void ch_calc(void)
{
    H19.a = P[0];
    H19.b = P[1];
    H19.c = P[2];
    H19.d = P[3];
    H19.tr = H19.a + H19.d;
    H19.det = H19.a * H19.d - H19.b * H19.c;
    // A^k = al_k A + be_k I; A^(k+1) = tr A^k - det A^(k-1)
    int64_t a0 = 0, b0 = 1, a1 = 1, b1 = 0;  // A^0, A^1
    for (int k = 1; k < 5; k++) {
        int64_t a2 = H19.tr * a1 - H19.det * a0, b2 = H19.tr * b1 - H19.det * b0;
        a0 = a1;
        b0 = b1;
        a1 = a2;
        b1 = b2;
    }
    H19.al = a1;
    H19.be = b1;
}
static const char *M2I(int64_t a, int64_t b, int64_t c, int64_t d)
{
    char *o = nb();
    char x[4][24];
    s_int(x[0], 24, a);
    s_int(x[1], 24, b);
    s_int(x[2], 24, c);
    s_int(x[3], 24, d);
    snprintf(o, 420, "@m{%s&%s;%s&%s}", x[0], x[1], x[2], x[3]);
    return o;
}
static void ch_text(int st)
{
    int a = H19.a, b = H19.b, c = H19.c, d = H19.d, tr = H19.tr, det = H19.det;
    switch (st) {
    case 0:
        T(LG("#A matrix solves its own equation\nPut $A$ into its characteristic polynomial: the result is the zero "
             "matrix.\n",
             "#מטריצה פותרת את המשוואה של עצמה\nמציבים את $A$ בפולינום האופייני שלה: התוצאה היא מטריצת האפס.\n"));
        T("=A^2 − (@t{tr} A)A + (det A)I = 0\n");
        T("=A = %s,   @t{tr} A = %s,   det A = %s\n", A2(), I(tr), I(det));
        break;
    case 1:
        T(LG("#Check it\n", "#בדיקה\n"));
        T("=%s − %s%s + %s%s = @m{0&0;0&0}\n", M2I(a * a + b * c, a * b + b * d, c * a + d * c, c * b + d * d), IP(tr),
          M2I(a, b, c, d), IP(det), M2I(1, 0, 0, 1));
        break;
    case 2:
        T(LG("#The inverse for free\nWhen $det A ≠ 0$: $A(@t{tr} A · I − A) = (det A)I$, so\n",
             "#ההופכית בחינם\nכאשר $det A ≠ 0$: $A(@t{tr} A · I − A) = (det A)I$, ולכן\n"));
        if (det) {
            qm_t M;
            int v[4] = {d, -b, -c, a};
            qm_ints(&M, 2, 2, v);
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    M.a[i][j] = q_div(M.a[i][j], qi(det));
                }
            }
            T("=A^{−1} = @f{(@t{tr} A)I − A}{det A} = %s\n", MQ(&M));
        } else {
            T(LG("Here $det A = 0$: no inverse.", "כאן $det A = 0$: אין הופכית."));
        }
        break;
    case 3: {
        T(LG("#High powers fall down\nUse $A^2 = (@t{tr} A)A − (det A)I$ again and again: each power is a mix of $A$ "
             "and $I$.\n",
             "#חזקות גבוהות יורדות\nמשתמשים שוב ושוב ב-$A^2 = (@t{tr} A)A − (det A)I$: כל חזקה היא צירוף של $A$ ושל "
             "$I$.\n"));
        int64_t al = H19.al, be = H19.be;
        T("=A^5 = %sA %s %sI = %s\n", I(al), be < 0 ? "−" : "+", I(be < 0 ? -be : be),
          M2I(al * a + be, al * b, al * c, al * d + be));
        break;
    }
    case 4:
        T(LG("#A picture\nFor each vector $x$, the arrow $@3{A^2x}$ is a mix of $@2{Ax}$ and $@1{x}$:\n",
             "#תמונה\nלכל וקטור $x$, החץ $@3{A^2x}$ הוא צירוף של $@2{Ax}$ ושל $@1{x}$:\n"));
        T("=@3{A^2x} = %s@2{Ax} %s %s@1{x}\n", I(tr), det > 0 ? "−" : "+", I(det > 0 ? det : -det));
        break;
    default:
        T(LG("#Why it is beautiful\nEvery square matrix, of any size, solves its own characteristic equation. All "
             "the endless powers of $A$ live in a space of dimension $n$.",
             "#למה זה יפה\nכל מטריצה ריבועית, בכל גודל, פותרת את המשוואה האופיינית של עצמה. כל החזקות האינסופיות של "
             "$A$ גרות במרחב ממימד $n$."));
        break;
    }
}
static void ch_pic(rect_t r, int st, float t)
{
    float a = (float)H19.a, b = (float)H19.b, c = (float)H19.c, d = (float)H19.d;
    float th = s_mini ? 0.6f : t * 0.5f, x = fcos(th), y = fsin(th);
    float ax = a * x + b * y, ay = c * x + d * y;
    float a2x = a * ax + b * ay, a2y = c * ax + d * ay;
    float m = sqrtf(a2x * a2x + a2y * a2y);
    float mm = sqrtf(ax * ax + ay * ay);
    float big = m > mm ? m : mm;
    float span = 2.4f * (big > 1.2f ? big : 1.2f);
    EZ(&H19.ez[0], span);
    if (s_mini || st < 0) {
        H19.ez[0] = span;
    }
    view_set(r, H19.ez[0], 0, 0);
    grid_plain(12, rgb(36, 42, 68), 70);
    // the two parts: tr * Ax and - det * x, and their sum (the parallelogram)
    float px = H19.tr * ax, py = H19.tr * ay, qx = -H19.det * x, qy = -H19.det * y;
    dashed(0, 0, px, py, COL_C2, 120);
    dashed(px, py, px + qx, py + qy, COL_C1, 160);
    float w = s_mini ? 1.4f : 2.2f;
    arrow(0, 0, a2x, a2y, COL_C3, w, 256);
    arrow(0, 0, ax, ay, COL_C2, w, 256);
    arrow(0, 0, x, y, COL_C1, w, 256);
}
static const topic_t TP_CH = {{"Cayley–Hamilton", "קיילי–המילטון"},
                              2,
                              4,
                              6,
                              {{"a", -3, 3, 1, 1}, {"b", -3, 3, 2, 1}, {"c", -3, 3, 3, 1}, {"d", -3, 3, 2, 1}},
                              ch_calc,
                              ch_text,
                              ch_pic};
