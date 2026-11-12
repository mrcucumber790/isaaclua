#pragma once
/* not exported by the SDK for sure (the game never imports them): implemented in libc.c */
#define floor isaaclua_floor
#define ceil isaaclua_ceil
#define fabs isaaclua_fabs
#define sqrt isaaclua_sqrt
#define tan isaaclua_tan
#define asin isaaclua_asin
#define acos isaaclua_acos
#define atan isaaclua_atan
#define atan2 isaaclua_atan2
#define sinh isaaclua_sinh
#define cosh isaaclua_cosh
#define tanh isaaclua_tanh
#define exp isaaclua_exp
#define log isaaclua_log
#define log2 isaaclua_log2
#define log10 isaaclua_log10
#define frexp isaaclua_frexp
#define ldexp isaaclua_ldexp
#define modf isaaclua_modf
#define floorf isaaclua_floorf
#define ceilf isaaclua_ceilf
#define fabsf isaaclua_fabsf
#ifdef __cplusplus
extern "C" {
#endif
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
double floor(double), ceil(double), fabs(double), sqrt(double), fmod(double, double), pow(double, double);
double sin(double), cos(double), tan(double), asin(double), acos(double), atan(double), atan2(double, double);
double sinh(double), cosh(double), tanh(double);
double exp(double), log(double), log2(double), log10(double);
double frexp(double, int *), ldexp(double, int), modf(double, double *);
float floorf(float), ceilf(float), fabsf(float), sqrtf(float);
#ifdef __cplusplus
}
#endif
