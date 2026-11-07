// The simulator: the functions of math.h that the firmware uses (sin, cos, atan2, exp, log, pow ...), the same on each
// PC. The C library of each operating system computes them in its own way, and the last bit of a result can be
// another one: then a picture of the firmware changes by some pixels. These functions use only the add, subtract,
// multiply, divide and square root of IEEE 754 doubles (no fused multiply-add: build.py gives -ffp-contract=off), so
// each PC gets the same bits. sim_det.h sends the calls of the firmware here. The error is a few units in the last
// place of a double (the float results: at most 1 unit from the C library). The board has its own library: the
// pictures of the simulator are not the same as on the board to the last bit.
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define PI       3.14159265358979311600e+00
#define PI_2     1.57079632679489655800e+00
#define LN2      6.93147180559945286227e-01
#define LN10     2.30258509299404590109e+00
// (fdlibm: pi/2 and ln 2 in parts, so that n * part is exact for the range reductions)
#define PIO2_1   1.57079632673412561417e+00
#define PIO2_2   6.07710050630396597660e-11
#define PIO2_3   2.02226624871116645580e-21
#define LN2_HI   6.93147180369123816490e-01
#define LN2_LO   1.90821492927058770002e-10

// ---------------------------------------------------------------- sin, cos, tan
// sin(r) and cos(r) for |r| <= pi/4: the Taylor series (the error of the first term not used is below 1e-16)
static double k_sin(double r)
{
    double z = r * r;
    double p = -1.0 / 1307674368000.0;                  // -1/15!
    p = p * z + 1.0 / 6227020800.0;
    p = p * z - 1.0 / 39916800.0;
    p = p * z + 1.0 / 362880.0;
    p = p * z - 1.0 / 5040.0;
    p = p * z + 1.0 / 120.0;
    p = p * z - 1.0 / 6.0;
    return r + r * z * p;
}

static double k_cos(double r)
{
    double z = r * r;
    double p = 1.0 / 20922789888000.0;                  // 1/16!
    p = p * z - 1.0 / 87178291200.0;
    p = p * z + 1.0 / 479001600.0;
    p = p * z - 1.0 / 3628800.0;
    p = p * z + 1.0 / 40320.0;
    p = p * z - 1.0 / 720.0;
    p = p * z + 1.0 / 24.0;
    p = p * z - 0.5;
    return 1.0 + z * p;
}

// x = n * pi/2 + r, |r| <= pi/4 (the result: n)
static int64_t reduce(double x, double *r)
{
    double n = floor(x * (2.0 / PI) + 0.5);
    *r = ((x - n * PIO2_1) - n * PIO2_2) - n * PIO2_3;
    return (int64_t)n;
}

double simm_sin(double x)
{
    if (!isfinite(x)) {
        return x - x;
    }
    double r;
    int64_t q = reduce(x, &r) & 3;
    double v = (q & 1) ? k_cos(r) : k_sin(r);
    return q >= 2 ? -v : v;
}

double simm_cos(double x)
{
    if (!isfinite(x)) {
        return x - x;
    }
    double r;
    int64_t q = reduce(x, &r) & 3;
    double v = (q & 1) ? k_sin(r) : k_cos(r);
    return (q == 1 || q == 2) ? -v : v;
}

double simm_tan(double x)
{
    return simm_sin(x) / simm_cos(x);
}

// ---------------------------------------------------------------- atan, atan2, asin, acos
// atan(t) for 0 <= t <= 1: two half-angle steps (|t| <= tan(pi/16)), then the series to t^31
static double k_atan(double t)
{
    for (int i = 0; i < 2; i++) {
        t = t / (1.0 + sqrt(1.0 + t * t));
    }
    double z = t * t;
    double p = 0.0;
    for (int k = 15; k >= 1; k--) {
        p = p * z + ((k & 1) ? -1.0 : 1.0) / (2 * k + 1);
    }
    return 4.0 * (t + t * z * p);
}

double simm_atan(double x)
{
    if (isnan(x)) {
        return x;
    }
    double a = fabs(x);
    double v = a > 1.0 ? PI_2 - k_atan(1.0 / a) : k_atan(a);
    return x < 0 ? -v : v;
}

double simm_atan2(double y, double x)
{
    if (isnan(x) || isnan(y)) {
        return x + y;
    }
    if (x == 0.0) {
        if (y == 0.0) {
            return signbit(x) ? copysign(PI, y) : copysign(0.0, y);
        }
        return y > 0 ? PI_2 : -PI_2;
    }
    double v = simm_atan(y / x);
    if (x > 0) {
        return v;
    }
    return signbit(y) ? v - PI : v + PI;
}

double simm_asin(double x)
{
    return simm_atan2(x, sqrt((1.0 - x) * (1.0 + x)));
}

double simm_acos(double x)
{
    return simm_atan2(sqrt((1.0 - x) * (1.0 + x)), x);
}

// ---------------------------------------------------------------- exp, log, pow
double simm_exp(double x)
{
    if (isnan(x)) {
        return x;
    }
    if (x > 709.78) {
        return HUGE_VAL;
    }
    if (x < -745.2) {
        return 0.0;
    }
    double n = floor(x * (1.0 / LN2) + 0.5);
    double r = (x - n * LN2_HI) - n * LN2_LO;           // |r| <= 0.35: the series to r^15
    double p = 1.0;
    for (int k = 15; k >= 1; k--) {
        p = 1.0 + p * r / k;
    }
    return ldexp(p, (int)n);
}

double simm_log(double x)
{
    if (isnan(x) || x < 0) {
        return NAN;
    }
    if (x == 0) {
        return -HUGE_VAL;
    }
    if (isinf(x)) {
        return x;
    }
    int e;
    double m = frexp(x, &e);                            // x = m * 2^e, 0.5 <= m < 1
    if (m < 0.70710678118654752440) {
        m *= 2.0;
        e--;
    }
    double s = (m - 1.0) / (m + 1.0);                   // |s| <= 0.172: the series to s^23
    double z = s * s;
    double p = 0.0;
    for (int k = 11; k >= 1; k--) {
        p = p * z + 1.0 / (2 * k + 1);
    }
    double lm = 2.0 * (s + s * z * p);
    return e * LN2_HI + (lm + e * LN2_LO);
}

static bool is_odd_int(double y)
{
    return fabs(y) < 9007199254740992.0 && floor(y) == y && fmod(y, 2.0) != 0.0;
}

double simm_pow(double x, double y)
{
    if (y == 0.0 || x == 1.0) {
        return 1.0;
    }
    if (isnan(x) || isnan(y)) {
        return x + y;
    }
    double sign = 1.0;
    if (x < 0) {
        if (floor(y) != y) {
            return NAN;
        }
        sign = is_odd_int(y) ? -1.0 : 1.0;
        x = -x;
    }
    if (x == 0.0) {
        return y > 0 ? sign * 0.0 : sign * HUGE_VAL;
    }
    return sign * simm_exp(y * simm_log(x));
}

double simm_exp2(double x)
{
    return simm_exp(x * LN2);
}

double simm_log2(double x)
{
    return simm_log(x) / LN2;
}

double simm_log10(double x)
{
    return simm_log(x) / LN10;
}

double simm_tanh(double x)
{
    if (fabs(x) > 20.0) {
        return x > 0 ? 1.0 : -1.0;
    }
    double e = simm_exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}

double simm_hypot(double x, double y)
{
    if (isinf(x) || isinf(y)) {
        return HUGE_VAL;
    }
    return sqrt(x * x + y * y);
}

// ---------------------------------------------------------------- the float versions
#define FLOAT_1(name)                       \
    float simm_##name##f(float x)           \
    {                                       \
        return (float)simm_##name(x);       \
    }
#define FLOAT_2(name)                       \
    float simm_##name##f(float y, float x)  \
    {                                       \
        return (float)simm_##name(y, x);    \
    }
FLOAT_1(sin)
FLOAT_1(cos)
FLOAT_1(tan)
FLOAT_1(atan)
FLOAT_2(atan2)
FLOAT_1(asin)
FLOAT_1(acos)
FLOAT_1(exp)
FLOAT_1(exp2)
FLOAT_1(log)
FLOAT_1(log2)
FLOAT_1(log10)
FLOAT_2(pow)
FLOAT_1(tanh)
FLOAT_2(hypot)
