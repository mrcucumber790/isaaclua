/* isaaclua libc shim: the parts of libc that Lua needs and that the Nintendo SDK is not known to export.
 *
 * Everything the game itself imports (malloc, snprintf, setjmp, pow, sin, ...) is taken from the SDK.
 * The rest lives here under isaaclua_* names (see the #defines in include/), so it can never clash
 * with or interpose the SDK's own symbols:
 *   - FILE: read-only files on nn::fs (whole file read on fopen), enough for luaL_loadfile/require;
 *   - strtod (decimal; Lua parses hex floats itself), strcoll ("C" locale), clock;
 *   - double math (exp/log/atan family) with plain argument reduction + series, ~1 ulp. */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/nn.h"

const char *modfs_safe_path(const char *in, char *out, size_t cap);  // src/modfs.c
const char *modfs_resolve(const char *path, char *buf, size_t cap);    // src/modfs.c

/* ------------------------------------------------------------------ FILE */

struct isaaclua_FILE {
    char *buf;
    size_t size, pos;
    int eof;
};

FILE *fopen(const char *path, const char *mode) {
    if (!mode || mode[0] != 'r' || strchr(mode, '+')) return NULL;  // read-only
    char safe[1024];
    nnfs_FileHandle h;
    if (!path || !modfs_safe_path(path, safe, sizeof safe) || nnfs_OpenFile(&h, safe, NNFS_OPEN_READ) != 0) {
        /* a mod's own file in another case (require "Scripts.Foo" for scripts/foo.lua): PC ignores case */
        char real[1024];
        if (!path || strncmp(path, "rom:/mods/", 10) != 0 || !modfs_resolve(path, real, sizeof real) ||
            !modfs_safe_path(real, safe, sizeof safe) || nnfs_OpenFile(&h, safe, NNFS_OPEN_READ) != 0) {
            errno = 2;  // ENOENT
            return NULL;
        }
    }
    int64_t size = 0;
    FILE *f = NULL;
    if (nnfs_GetFileSize(&size, h) == 0 && size >= 0 && size < (64 << 20)) {
        f = (FILE *)calloc(1, sizeof *f);
        char *buf = (char *)malloc((size_t)size + 1);
        size_t got = 0;
        if (f && buf && (size == 0 || nnfs_ReadFile(&got, h, 0, buf, (size_t)size) == 0)) {
            f->buf = buf;
            f->size = got;
        } else {
            free(buf);
            free(f);
            f = NULL;
        }
    }
    nnfs_CloseFile(h);
    if (!f) errno = 5;  // EIO
    return f;
}

FILE *freopen(const char *path, const char *mode, FILE *f) {
    (void)path, (void)mode;  // text and binary are the same here
    if (f) f->pos = 0, f->eof = 0;
    return f;
}

int fclose(FILE *f) {
    if (!f) return EOF;
    free(f->buf);
    free(f);
    return 0;
}

int getc(FILE *f) {
    if (!f || f->pos >= f->size) {
        if (f) f->eof = 1;
        return EOF;
    }
    return (unsigned char)f->buf[f->pos++];
}

size_t fread(void *p, size_t sz, size_t n, FILE *f) {
    if (!f || !sz) return 0;
    size_t want = sz * n, left = f->size - f->pos;
    if (want > left) want = left - left % sz, f->eof = 1;
    memcpy(p, f->buf + f->pos, want);
    f->pos += want;
    return want / sz;
}

int feof(FILE *f) { return f ? f->eof : 1; }
int ferror(FILE *f) { (void)f; return 0; }
char *fgets(char *s, int n, FILE *f) {  // only debug.debug() uses it (on stdin = NULL)
    (void)s, (void)n, (void)f;
    return NULL;
}

/* ------------------------------------------------------- misc libc bits */

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

clock_t clock(void) {
    int64_t f = nnosGetSystemTickFrequency();
    if (f <= 0) return 0;
    int64_t t = nnosGetSystemTick();
    return (clock_t)((t / f) * CLOCKS_PER_SEC + (t % f) * CLOCKS_PER_SEC / f);
}

/* ------------------------------------------------------------------ math */

typedef union { double d; uint64_t u; } dbits;

double floor(double x) { return __builtin_floor(x); }
double ceil(double x) { return __builtin_ceil(x); }
double fabs(double x) { return __builtin_fabs(x); }
double sqrt(double x) { return __builtin_sqrt(x); }
float floorf(float x) { return __builtin_floorf(x); }
float ceilf(float x) { return __builtin_ceilf(x); }
float fabsf(float x) { return __builtin_fabsf(x); }

double frexp(double x, int *e) {
    dbits b = {x};
    int ex = (int)(b.u >> 52 & 0x7ff);
    if (ex == 0) {  // zero or subnormal
        if (x == 0.0) { *e = 0; return x; }
        x = frexp(x * 0x1p64, e);
        *e -= 64;
        return x;
    }
    if (ex == 0x7ff) { *e = 0; return x; }  // inf / nan
    *e = ex - 1022;
    b.u = (b.u & 0x800fffffffffffffull) | (uint64_t)1022 << 52;
    return b.d;
}

static double pow2i(int n) {  // 2^n for -1022 <= n <= 1023
    dbits b;
    b.u = (uint64_t)(n + 1023) << 52;
    return b.d;
}

double ldexp(double x, int n) {
    while (n > 1023) { x *= 0x1p1023; n -= 1023; if (isinf(x)) return x; }
    while (n < -1022) { x *= 0x1p-1022; n += 1022; if (x == 0.0) return x; }
    return x * pow2i(n);
}

double modf(double x, double *ip) {
    if (isinf(x)) { *ip = x; return 0.0 * x; }
    double i = x >= 0 ? floor(x) : ceil(x);
    *ip = i;
    return x - i;
}

static const double kLn2Hi = 6.93147180369123816490e-01, kLn2Lo = 1.90821492927058770002e-10;
static const double kPi = 3.14159265358979323846, kPi2 = 1.57079632679489661923;

double exp(double x) {
    if (isnan(x)) return x;
    if (x > 709.782712893384) return HUGE_VAL;
    if (x < -745.1332191019412) return 0.0;
    double kf = floor(x * 1.4426950408889634 + 0.5);
    int k = (int)kf;
    double r = (x - kf * kLn2Hi) - kf * kLn2Lo;  // |r| <= ln2/2
    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 18; i++) {
        term *= r / i;
        sum += term;
    }
    return ldexp(sum, k);
}

double log(double x) {
    if (isnan(x)) return x;
    if (x < 0.0) return __builtin_nan("");
    if (x == 0.0) return -HUGE_VAL;
    if (isinf(x)) return x;
    int e;
    double m = frexp(x, &e);  // [0.5, 1)
    if (m < 0.70710678118654752440) m *= 2.0, e--;
    double f = m - 1.0, s = f / (2.0 + f), s2 = s * s;
    double sum = 0.0, p = s;
    for (int i = 1; i < 30; i += 2) {  // 2 atanh(s) = log(m)
        sum += p / i;
        p *= s2;
    }
    return e * kLn2Hi + (e * kLn2Lo + 2.0 * sum);
}

double log2(double x) { return log(x) * 1.44269504088896340736; }
double log10(double x) { return log(x) * 0.43429448190325182765; }

static double atan_small(double t) {  // |t| <= tan(pi/12)
    double t2 = t * t, p = t, sum = 0.0;
    for (int i = 1; i < 40; i += 2) {
        sum += (i & 2) ? -p / i : p / i;
        p *= t2;
    }
    return sum;
}

double atan(double x) {
    if (isnan(x)) return x;
    int neg = x < 0;
    if (neg) x = -x;
    double r;
    if (x > 1.0) {
        r = kPi2 - atan(1.0 / x);
    } else if (x > 0.26794919243112270647) {
        const double s3 = 1.73205080756887729353;
        r = kPi / 6.0 + atan_small((x * s3 - 1.0) / (s3 + x));
    } else {
        r = atan_small(x);
    }
    return neg ? -r : r;
}

double atan2(double y, double x) {
    if (isnan(x) || isnan(y)) return x + y;
    if (x == 0.0) {
        if (y == 0.0) return __builtin_signbit(x) ? (__builtin_signbit(y) ? -kPi : kPi) : y;
        return y > 0 ? kPi2 : -kPi2;
    }
    if (isinf(x) && isinf(y)) {
        double a = x > 0 ? kPi / 4 : 3 * kPi / 4;
        return y > 0 ? a : -a;
    }
    double a = atan(y / x);
    if (x > 0) return a;
    return __builtin_signbit(y) ? a - kPi : a + kPi;
}

double asin(double x) { return atan2(x, sqrt((1.0 - x) * (1.0 + x))); }
double acos(double x) { return atan2(sqrt((1.0 - x) * (1.0 + x)), x); }
double tan(double x) { return sin(x) / cos(x); }

double sinh(double x) {
    if (fabs(x) < 1e-5) return x + x * x * x / 6.0;
    double e = exp(x);
    return 0.5 * (e - 1.0 / e);
}
double cosh(double x) {
    double e = exp(x);
    return 0.5 * (e + 1.0 / e);
}
double tanh(double x) {
    if (x > 22.0) return 1.0;
    if (x < -22.0) return -1.0;
    if (fabs(x) < 1e-5) return x - x * x * x / 3.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}

/* ---------------------------------------------------------------- strtod */

double strtod(const char *s, char **end) {
    const char *p = s;
    while (*p == ' ' || (unsigned)(*p - '\t') < 5) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'f') {
        p += 3;
        if (strncmp(p, "inity", 5) == 0 || strncmp(p, "INITY", 5) == 0) p += 5;
        if (end) *end = (char *)p;
        return neg ? -HUGE_VAL : HUGE_VAL;
    }
    if ((p[0] | 32) == 'n' && (p[1] | 32) == 'a' && (p[2] | 32) == 'n') {
        if (end) *end = (char *)(p + 3);
        return __builtin_nan("");
    }
    uint64_t mant = 0;
    int digits = 0, exp10 = 0, any = 0;
    for (; (unsigned)(*p - '0') < 10; p++, any = 1) {
        if (digits < 19) mant = mant * 10 + (uint64_t)(*p - '0'), digits += mant != 0;
        else exp10++;
    }
    if (*p == '.') {
        p++;
        for (; (unsigned)(*p - '0') < 10; p++, any = 1) {
            if (digits < 19) mant = mant * 10 + (uint64_t)(*p - '0'), digits += mant != 0, exp10--;
        }
    }
    if (!any) {
        if (end) *end = (char *)s;
        return 0.0;
    }
    if ((*p | 32) == 'e') {
        const char *q = p + 1;
        int eneg = 0, ev = 0;
        if (*q == '+' || *q == '-') eneg = *q++ == '-';
        if ((unsigned)(*q - '0') < 10) {
            for (; (unsigned)(*q - '0') < 10; q++)
                if (ev < 100000) ev = ev * 10 + (*q - '0');
            exp10 += eneg ? -ev : ev;
            p = q;
        }
    }
    if (end) *end = (char *)p;
    double v = (double)mant;
    if (mant == 0) {
        v = 0.0;
    } else if (exp10 >= 0 && exp10 <= 22 && mant < (1ull << 53)) {
        v *= pow(10.0, exp10);  // exact
    } else if (exp10 < 0 && exp10 >= -22 && mant < (1ull << 53)) {
        v /= pow(10.0, -exp10);  // correctly rounded
    } else if (exp10 > 308) {
        v = v * pow(10.0, exp10 - 300) * 1e300;
    } else if (exp10 < -300) {
        v = v * pow(10.0, exp10 + 300) * 1e-300;
    } else {
        v *= pow(10.0, exp10);
    }
    if (isinf(v) || (v == 0.0 && mant != 0)) errno = 34;  // ERANGE
    return neg ? -v : v;
}
