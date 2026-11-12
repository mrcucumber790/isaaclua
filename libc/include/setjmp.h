#pragma once
/* nnmusl aarch64 jmp_buf is 312 bytes (musl layout); we reserve more. */
typedef unsigned long jmp_buf[64];
int setjmp(jmp_buf) __attribute__((returns_twice));
_Noreturn void longjmp(jmp_buf, int);
