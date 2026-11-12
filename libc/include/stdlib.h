#pragma once
/* not exported by the SDK for sure (the game never imports them): implemented in libc.c */
#define strtod isaaclua_strtod
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RAND_MAX 0x7fffffff
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void free(void *p);
_Noreturn void abort(void);
double strtod(const char *s, char **end);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
char *getenv(const char *name);
int rand(void);
void srand(unsigned seed);
int abs(int v);
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));
#ifdef __cplusplus
}
#endif
