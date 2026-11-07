// The simulator: included before each C file of the firmware and before tests/host/stubs.c (-include, through
// sim_force.h for the firmware). It makes the results the same on each PC:
//  - rand() and srand() go to sim_rtos.c: the C library of each operating system has its own generator and RAND_MAX;
//  - time() goes to sim_rtos.c: with the fixed clock (tfnsim_options: TFNSIM_FIXED) it gives the fixed time of day of
//    the test;
//  - sin, cos, atan2, exp, log, pow ... go to sim_math.c: the C library of each operating system can give another last
//    bit, and then a picture changes by some pixels.
#pragma once
#include <math.h>
#include <stdlib.h>
#include <time.h>

int sim_rand(void);
void sim_srand(unsigned seed);
time_t sim_time(time_t *out);

#undef RAND_MAX
#define RAND_MAX 32767
#define rand() sim_rand()
#define srand(s) sim_srand(s)
#define time(t) sim_time(t)

double simm_sin(double x);
double simm_cos(double x);
double simm_tan(double x);
double simm_atan(double x);
double simm_atan2(double y, double x);
double simm_asin(double x);
double simm_acos(double x);
double simm_exp(double x);
double simm_exp2(double x);
double simm_log(double x);
double simm_log2(double x);
double simm_log10(double x);
double simm_pow(double x, double y);
double simm_tanh(double x);
double simm_hypot(double x, double y);
float simm_sinf(float x);
float simm_cosf(float x);
float simm_tanf(float x);
float simm_atanf(float x);
float simm_atan2f(float y, float x);
float simm_asinf(float x);
float simm_acosf(float x);
float simm_expf(float x);
float simm_exp2f(float x);
float simm_logf(float x);
float simm_log2f(float x);
float simm_log10f(float x);
float simm_powf(float x, float y);
float simm_tanhf(float x);
float simm_hypotf(float x, float y);

#define sin(x) simm_sin(x)
#define cos(x) simm_cos(x)
#define tan(x) simm_tan(x)
#define atan(x) simm_atan(x)
#define atan2(y, x) simm_atan2(y, x)
#define asin(x) simm_asin(x)
#define acos(x) simm_acos(x)
#define exp(x) simm_exp(x)
#define exp2(x) simm_exp2(x)
#define log(x) simm_log(x)
#define log2(x) simm_log2(x)
#define log10(x) simm_log10(x)
#define pow(x, y) simm_pow(x, y)
#define tanh(x) simm_tanh(x)
#define hypot(x, y) simm_hypot(x, y)
#define sinf(x) simm_sinf(x)
#define cosf(x) simm_cosf(x)
#define tanf(x) simm_tanf(x)
#define atanf(x) simm_atanf(x)
#define atan2f(y, x) simm_atan2f(y, x)
#define asinf(x) simm_asinf(x)
#define acosf(x) simm_acosf(x)
#define expf(x) simm_expf(x)
#define exp2f(x) simm_exp2f(x)
#define logf(x) simm_logf(x)
#define log2f(x) simm_log2f(x)
#define log10f(x) simm_log10f(x)
#define powf(x, y) simm_powf(x, y)
#define tanhf(x) simm_tanhf(x)
#define hypotf(x, y) simm_hypotf(x, y)
