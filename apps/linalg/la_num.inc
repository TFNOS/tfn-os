// LINEAR ALGEBRA: the numbers. Exact fractions (q_t) for the values that the text shows, small matrices of
// fractions, float matrices for the pictures, the eigenvalues of a symmetric matrix (Jacobi), the roots of a
// polynomial (Durand-Kerner, complex), the square roots in their simple form (k sqrt r), and the text forms of all of
// them in the markup of la_type.inc. The PC test checks each value with numpy and sympy for all the numbers that a
// user can set.

typedef struct {
    int32_t n, d;  // d > 0, gcd(n, d) = 1
} q_t;
typedef struct {
    float re, im;
} cx_t;

#define QMAX 8  // the largest matrix

typedef struct {
    int n, m;  // the rows, the columns
    q_t a[QMAX][QMAX];
} qm_t;

static int64_t gcd64(int64_t a, int64_t b)
{
    if (a < 0) {
        a = -a;
    }
    if (b < 0) {
        b = -b;
    }
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}
static q_t qn(int64_t n, int64_t d)
{
    if (d < 0) {
        n = -n;
        d = -d;
    }
    if (d == 0) {
        d = 1;  // (never: the callers check a division by zero)
    }
    int64_t g = gcd64(n, d);
    if (g > 1) {
        n /= g;
        d /= g;
    }
    q_t q = {(int32_t)n, (int32_t)d};
    return q;
}
static inline q_t qi(int v)
{
    q_t q = {v, 1};
    return q;
}
static inline q_t q_add(q_t a, q_t b)
{
    return qn((int64_t)a.n * b.d + (int64_t)b.n * a.d, (int64_t)a.d * b.d);
}
static inline q_t q_sub(q_t a, q_t b)
{
    return qn((int64_t)a.n * b.d - (int64_t)b.n * a.d, (int64_t)a.d * b.d);
}
static inline q_t q_mul(q_t a, q_t b)
{
    return qn((int64_t)a.n * b.n, (int64_t)a.d * b.d);
}
static inline q_t q_div(q_t a, q_t b)
{
    return qn((int64_t)a.n * b.d, (int64_t)a.d * b.n);
}
static inline q_t q_neg(q_t a)
{
    a.n = -a.n;
    return a;
}
static inline bool q_z(q_t a)
{
    return a.n == 0;
}
static inline bool q_int(q_t a)
{
    return a.d == 1;
}
static inline float q_f(q_t a)
{
    return (float)a.n / (float)a.d;
}
static inline int q_cmp(q_t a, q_t b)
{
    int64_t l = (int64_t)a.n * b.d, r = (int64_t)b.n * a.d;
    return l < r ? -1 : l > r ? 1 : 0;
}

// ---------------------------------------------------------------- the text forms (UTF-8, the markup of la_type.inc)
#define MINUS "\xe2\x88\x92"  // U+2212

// an integer with the minus sign of the mathematics
static int s_int(char *o, int max, int64_t v)
{
    return v < 0 ? snprintf(o, max, MINUS "%lld", (long long)-v) : snprintf(o, max, "%lld", (long long)v);
}
// a fraction: "3", "−3", "@f{3}{4}" (disp) or "3/4" (inline: the fraction as a slash)
static int s_q(char *o, int max, q_t q, bool disp)
{
    if (q.d == 1) {
        return s_int(o, max, q.n);
    }
    long long n = q.n < 0 ? -(long long)q.n : q.n;
    if (disp) {
        return snprintf(o, max, "%s@f{%lld}{%d}", q.n < 0 ? MINUS : "", n, (int)q.d);
    }
    return snprintf(o, max, "%s%lld/%d", q.n < 0 ? MINUS : "", n, (int)q.d);
}
// a float with p places, no "−0", the places cut at the end ("2.50" -> "2.5", "3.00" -> "3")
static int s_f(char *o, int max, float v, int p)
{
    char b[32];
    float r = v < 0 ? -v : v;
    float lim = p >= 3 ? 0.0005f : p == 2 ? 0.005f : p == 1 ? 0.05f : 0.5f;
    if (r < lim) {
        return snprintf(o, max, "0");
    }
    snprintf(b, sizeof b, "%.*f", p, (double)r);
    int L = (int)strlen(b);
    if (strchr(b, '.')) {
        while (L > 0 && b[L - 1] == '0') {
            b[--L] = 0;
        }
        if (L > 0 && b[L - 1] == '.') {
            b[--L] = 0;
        }
    }
    return snprintf(o, max, "%s%s", v < 0 ? MINUS : "", b);
}
// a complex number "1.62 − 0.59i" (p places)
static int s_cx(char *o, int max, cx_t z, int p)
{
    float lim = p >= 3 ? 0.0005f : 0.005f;
    char a[24], b[24];
    if (z.im < lim && z.im > -lim) {
        return s_f(o, max, z.re, p);
    }
    s_f(b, sizeof b, z.im < 0 ? -z.im : z.im, p);
    const char *bi = strcmp(b, "1") ? b : "";
    if (z.re < lim && z.re > -lim) {
        return snprintf(o, max, "%s%si", z.im < 0 ? MINUS : "", bi);
    }
    s_f(a, sizeof a, z.re, p);
    return snprintf(o, max, "%s %s %si", a, z.im < 0 ? MINUS : "+", bi);
}

// ---------------------------------------------------------------- the square root in its simple form
// v = k * k * r with r free of squares (v >= 0)
static void surd(int64_t v, int64_t *k, int64_t *r)
{
    *k = 1;
    *r = v;
    for (int64_t f = 2; f * f <= *r; f++) {
        while (*r % (f * f) == 0) {
            *r /= f * f;
            *k *= f;
        }
    }
}
// (A ± B sqrt(D)) / C with integers (C > 0): the text "@f{3 ± @r{5}}{2}" and the two values. D < 0: the
// imaginary unit (the result is complex). A text with one value when D = 0. disp = the fraction form.
static int s_pm_root(char *o, int max, int64_t A, int64_t B, int64_t D, int64_t C, bool disp)
{
    char top[96], tA[24], tB[48];
    int64_t k = 0, r = 0;
    bool im = D < 0;
    if (D != 0) {
        surd(im ? -D : D, &k, &r);
    }
    k *= B < 0 ? -B : B;
    if (D == 0 || k == 0) {
        return s_q(o, max, qn(A, C), disp);
    }
    if (r == 1 && !im) {
        // two rational values: "3 and 1" is given by the caller; here the form (A ± k) / C
        q_t p = qn(A + k, C), m = qn(A - k, C);
        char a[40], b[40];
        s_q(a, sizeof a, p, disp);
        s_q(b, sizeof b, m, disp);
        return snprintf(o, max, "%s, %s", a, b);
    }
    // the common factor of A, k and C
    int64_t g = gcd64(gcd64(A, k), C);
    if (g > 1) {
        A /= g;
        k /= g;
        C /= g;
    }
    s_int(tA, sizeof tA, A);
    if (r == 1) {
        if (k == 1) {
            snprintf(tB, sizeof tB, "i");
        } else {
            snprintf(tB, sizeof tB, "%lldi", (long long)k);
        }
    } else if (k == 1) {
        snprintf(tB, sizeof tB, "@r{%lld}%s", (long long)r, im ? "i" : "");
    } else {
        snprintf(tB, sizeof tB, "%lld@r{%lld}%s", (long long)k, (long long)r, im ? "i" : "");
    }
    if (A == 0) {
        snprintf(top, sizeof top, "\xc2\xb1%s", tB);
    } else {
        snprintf(top, sizeof top, "%s \xc2\xb1 %s", tA, tB);
    }
    if (C == 1) {
        return snprintf(o, max, "%s", top);
    }
    if (disp) {
        return snprintf(o, max, "@f{%s}{%lld}", top, (long long)C);
    }
    return snprintf(o, max, "(%s)/%lld", top, (long long)C);
}

// ---------------------------------------------------------------- matrices of fractions
static void qm_zero(qm_t *M, int n, int m)
{
    M->n = n;
    M->m = m;
    for (int i = 0; i < QMAX; i++) {
        for (int j = 0; j < QMAX; j++) {
            M->a[i][j] = qi(0);
        }
    }
}
static void qm_ints(qm_t *M, int n, int m, const int *v)
{
    qm_zero(M, n, m);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            M->a[i][j] = qi(v[i * m + j]);
        }
    }
}
static void qm_eye(qm_t *M, int n)
{
    qm_zero(M, n, n);
    for (int i = 0; i < n; i++) {
        M->a[i][i] = qi(1);
    }
}
// (the matrices of these functions are static: the stack of the app task is small; none of them calls itself)
static void qm_mul(qm_t *R, const qm_t *A, const qm_t *B)
{
    static qm_t T;
    qm_zero(&T, A->n, B->m);
    for (int i = 0; i < A->n; i++) {
        for (int j = 0; j < B->m; j++) {
            q_t s = qi(0);
            for (int k = 0; k < A->m; k++) {
                s = q_add(s, q_mul(A->a[i][k], B->a[k][j]));
            }
            T.a[i][j] = s;
        }
    }
    *R = T;
}
// R = a A + b B
static void qm_lin(qm_t *R, q_t a, const qm_t *A, q_t b, const qm_t *B)
{
    static qm_t T;
    qm_zero(&T, A->n, A->m);
    for (int i = 0; i < A->n; i++) {
        for (int j = 0; j < A->m; j++) {
            T.a[i][j] = q_add(q_mul(a, A->a[i][j]), q_mul(b, B->a[i][j]));
        }
    }
    *R = T;
}
// the determinant (elimination with fractions; n <= QMAX)
static q_t qm_det(const qm_t *A)
{
    static qm_t M;
    M = *A;
    int n = A->n;
    q_t det = qi(1);
    for (int c = 0; c < n; c++) {
        int p = -1;
        for (int r = c; r < n; r++) {
            if (!q_z(M.a[r][c])) {
                p = r;
                break;
            }
        }
        if (p < 0) {
            return qi(0);
        }
        if (p != c) {
            for (int j = 0; j < n; j++) {
                q_t t = M.a[p][j];
                M.a[p][j] = M.a[c][j];
                M.a[c][j] = t;
            }
            det = q_neg(det);
        }
        det = q_mul(det, M.a[c][c]);
        for (int r = c + 1; r < n; r++) {
            if (q_z(M.a[r][c])) {
                continue;
            }
            q_t f = q_div(M.a[r][c], M.a[c][c]);
            for (int j = c; j < n; j++) {
                M.a[r][j] = q_sub(M.a[r][j], q_mul(f, M.a[c][j]));
            }
        }
    }
    return det;
}
// the reduced row echelon form; *rank = the rank, piv[c] = 1 for a pivot column
static void qm_rref(qm_t *M, int *rank, int *piv)
{
    int r = 0;
    for (int c = 0; c < M->m; c++) {
        piv[c] = 0;
    }
    for (int c = 0; c < M->m && r < M->n; c++) {
        int p = -1;
        for (int i = r; i < M->n; i++) {
            if (!q_z(M->a[i][c])) {
                p = i;
                break;
            }
        }
        if (p < 0) {
            continue;
        }
        for (int j = 0; j < M->m; j++) {
            q_t t = M->a[p][j];
            M->a[p][j] = M->a[r][j];
            M->a[r][j] = t;
        }
        q_t f = M->a[r][c];
        for (int j = 0; j < M->m; j++) {
            M->a[r][j] = q_div(M->a[r][j], f);
        }
        for (int i = 0; i < M->n; i++) {
            if (i == r || q_z(M->a[i][c])) {
                continue;
            }
            q_t g = M->a[i][c];
            for (int j = 0; j < M->m; j++) {
                M->a[i][j] = q_sub(M->a[i][j], q_mul(g, M->a[r][j]));
            }
        }
        piv[c] = 1;
        r++;
    }
    *rank = r;
}
// the coefficients of det(xI - A) (Faddeev-LeVerrier, exact): c[0] = 1, c[k] = the coefficient of x^(n-k)
static void qm_charpoly(const qm_t *A, q_t *c)
{
    int n = A->n;
    static qm_t M, AM, I;
    qm_eye(&I, n);
    qm_zero(&M, n, n);
    c[0] = qi(1);
    for (int k = 1; k <= n; k++) {
        // M_k = A M_(k-1) + c_(k-1) I ; c_k = -tr(A M_k) / k
        static qm_t T;
        qm_mul(&T, A, &M);
        qm_lin(&M, qi(1), &T, c[k - 1], &I);
        qm_mul(&AM, A, &M);
        q_t tr = qi(0);
        for (int i = 0; i < n; i++) {
            tr = q_add(tr, AM.a[i][i]);
        }
        c[k] = q_div(q_neg(tr), qi(k));
    }
}

// the markup of a matrix: "@m{1&2;3&4}" (bars = "@d{" for a determinant); disp = the fractions as @f
static int s_qm(char *o, int max, const qm_t *M, bool bars, bool disp)
{
    int L = snprintf(o, max, bars ? "@d{" : "@m{");
    for (int i = 0; i < M->n && L < max; i++) {
        for (int j = 0; j < M->m && L < max; j++) {
            L += s_q(o + L, max - L, M->a[i][j], disp);
            if (j + 1 < M->m && L < max) {
                o[L++] = '&';
            }
        }
        if (i + 1 < M->n && L < max) {
            o[L++] = ';';
        }
    }
    if (L < max - 1) {
        o[L++] = '}';
        o[L] = 0;
    } else {
        o[max - 1] = 0;
    }
    return L;
}

// ---------------------------------------------------------------- float matrices, eigenvalues
typedef struct {
    int n;
    float a[QMAX][QMAX];
} fm_t;

static void fm_from(fm_t *F, const qm_t *M)
{
    F->n = M->n;
    for (int i = 0; i < M->n; i++) {
        for (int j = 0; j < M->n; j++) {
            F->a[i][j] = q_f(M->a[i][j]);
        }
    }
}
// the eigenvalues (w, low to high) and the eigenvectors (the columns of V) of a symmetric matrix (cyclic Jacobi)
static void fm_jacobi(const fm_t *S, float *w, fm_t *V)
{
    int n = S->n;
    static fm_t A;
    A = *S;
    V->n = n;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            V->a[i][j] = i == j ? 1.0f : 0.0f;
        }
    }
    for (int sweep = 0; sweep < 30; sweep++) {
        float off = 0;
        for (int p = 0; p < n; p++) {
            for (int q = p + 1; q < n; q++) {
                off += A.a[p][q] * A.a[p][q];
            }
        }
        if (off < 1e-12f) {
            break;
        }
        for (int p = 0; p < n; p++) {
            for (int q = p + 1; q < n; q++) {
                if (fabsf(A.a[p][q]) < 1e-9f) {
                    continue;
                }
                float th = (A.a[q][q] - A.a[p][p]) / (2.0f * A.a[p][q]);
                float t = (th >= 0 ? 1.0f : -1.0f) / (fabsf(th) + sqrtf(th * th + 1.0f));
                float c = 1.0f / sqrtf(t * t + 1.0f), s = t * c;
                for (int k = 0; k < n; k++) {
                    float akp = A.a[k][p], akq = A.a[k][q];
                    A.a[k][p] = c * akp - s * akq;
                    A.a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; k++) {
                    float apk = A.a[p][k], aqk = A.a[q][k];
                    A.a[p][k] = c * apk - s * aqk;
                    A.a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; k++) {
                    float vkp = V->a[k][p], vkq = V->a[k][q];
                    V->a[k][p] = c * vkp - s * vkq;
                    V->a[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    // the order: low to high (a selection sort of the columns too)
    for (int i = 0; i < n; i++) {
        w[i] = A.a[i][i];
    }
    for (int i = 0; i < n; i++) {
        int m = i;
        for (int j = i + 1; j < n; j++) {
            if (w[j] < w[m]) {
                m = j;
            }
        }
        if (m != i) {
            float t = w[i];
            w[i] = w[m];
            w[m] = t;
            for (int k = 0; k < n; k++) {
                float u = V->a[k][i];
                V->a[k][i] = V->a[k][m];
                V->a[k][m] = u;
            }
        }
    }
    // a sign rule: the largest part of each vector is positive (the pictures do not jump)
    for (int j = 0; j < n; j++) {
        int big = 0;
        for (int k = 1; k < n; k++) {
            if (fabsf(V->a[k][j]) > fabsf(V->a[big][j]) + 1e-4f) {
                big = k;
            }
        }
        if (V->a[big][j] < 0) {
            for (int k = 0; k < n; k++) {
                V->a[k][j] = -V->a[k][j];
            }
        }
    }
}

static inline cx_t cx(float re, float im)
{
    cx_t z = {re, im};
    return z;
}
static inline cx_t cx_add(cx_t a, cx_t b)
{
    return cx(a.re + b.re, a.im + b.im);
}
static inline cx_t cx_sub(cx_t a, cx_t b)
{
    return cx(a.re - b.re, a.im - b.im);
}
static inline cx_t cx_mul(cx_t a, cx_t b)
{
    return cx(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}
static inline cx_t cx_div(cx_t a, cx_t b)
{
    float d = b.re * b.re + b.im * b.im;
    if (d < 1e-30f) {
        d = 1e-30f;
    }
    return cx((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}
static inline float cx_abs(cx_t a)
{
    return sqrtf(a.re * a.re + a.im * a.im);
}
// the n roots of x^n + c[1] x^(n-1) + ... + c[n] (Durand-Kerner, then a polish by Newton); the order: by the real
// part, then by the imaginary part
static void poly_roots(const float *c, int n, cx_t *z)
{
    float R = 1;
    for (int k = 1; k <= n; k++) {
        float v = fabsf(c[k]);
        if (v + 1 > R) {
            R = v + 1;
        }
    }
    cx_t w = cx(0.4f, 0.9f), p = cx(1, 0);
    for (int i = 0; i < n; i++) {
        z[i] = cx(p.re * R * 0.7f, p.im * R * 0.7f);
        p = cx_mul(p, w);
    }
    for (int it = 0; it < 400; it++) {
        float moved = 0;
        for (int i = 0; i < n; i++) {
            cx_t f = cx(1, 0);
            for (int k = 1; k <= n; k++) {
                f = cx_add(cx_mul(f, z[i]), cx(c[k], 0));
            }
            cx_t d = cx(1, 0);
            for (int j = 0; j < n; j++) {
                if (j != i) {
                    d = cx_mul(d, cx_sub(z[i], z[j]));
                }
            }
            cx_t dz = cx_div(f, d);
            z[i] = cx_sub(z[i], dz);
            moved += cx_abs(dz);
        }
        if (moved < 1e-6f) {
            break;
        }
    }
    // a value very near the real line is real (the polynomial has real coefficients)
    for (int i = 0; i < n; i++) {
        if (fabsf(z[i].im) < 1e-3f * (1 + fabsf(z[i].re))) {
            z[i].im = 0;
        }
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            bool sw = z[j].re < z[i].re - 1e-4f || (fabsf(z[j].re - z[i].re) <= 1e-4f && z[j].im < z[i].im);
            if (sw) {
                cx_t t = z[i];
                z[i] = z[j];
                z[j] = t;
            }
        }
    }
}
// the eigenvalues of any small matrix: the roots of its characteristic polynomial
static void qm_eig(const qm_t *A, cx_t *z)
{
    q_t c[QMAX + 1];
    float f[QMAX + 1];
    qm_charpoly(A, c);
    for (int k = 0; k <= A->n; k++) {
        f[k] = q_f(c[k]);
    }
    poly_roots(f, A->n, z);
}
