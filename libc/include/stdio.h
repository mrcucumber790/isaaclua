#pragma once
/* not exported by the SDK for sure (the game never imports them): implemented in libc.c */
#define fopen isaaclua_fopen
#define freopen isaaclua_freopen
#define fclose isaaclua_fclose
#define getc isaaclua_getc
#define fread isaaclua_fread
#define feof isaaclua_feof
#define ferror isaaclua_ferror
#define fgets isaaclua_fgets
/* isaaclua libc shim. printf family = Nintendo SDK (nnmusl) exports; FILE = our read-only nn::fs wrapper. */
#include <stddef.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct isaaclua_FILE FILE;
#define EOF (-1)
#define BUFSIZ 1024
#define stdin ((FILE *)0)
int snprintf(char *s, size_t n, const char *fmt, ...);
int sprintf(char *s, const char *fmt, ...);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
int fclose(FILE *f);
int getc(FILE *f);
size_t fread(void *p, size_t sz, size_t n, FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
char *fgets(char *s, int n, FILE *f);
#ifdef __cplusplus
}
#endif
