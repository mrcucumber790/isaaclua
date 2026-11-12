#pragma once
/* not exported by the SDK for sure (the game never imports them): implemented in libc.c */
#define clock isaaclua_clock
#include <stddef.h>
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000000L
time_t time(time_t *t);
clock_t clock(void);
