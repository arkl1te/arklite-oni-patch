/* Arklite's libc for zstd (BSD licence, see LICENSE-zstd.txt): no C runtime in Arklite.dll. Pre-empts
 * every block of zstd's common/zstd_deps.h. Allocation goes to zstd_ark.c's ark_zstd_malloc/free. */
#ifndef ARK_ZSTD_DEPS_H
#define ARK_ZSTD_DEPS_H
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#define ZSTD_DEPS_COMMON
#define ZSTD_memcpy(d,s,l) __builtin_memcpy((d),(s),(l))
#define ZSTD_memmove(d,s,l) __builtin_memmove((d),(s),(l))
#define ZSTD_memset(p,v,l) __builtin_memset((p),(v),(l))
void *ark_zstd_malloc(size_t n);
void *ark_zstd_calloc(size_t n, size_t s);
void ark_zstd_free(void *p);
#define ZSTD_DEPS_MALLOC
#define ZSTD_malloc(s) ark_zstd_malloc(s)
#define ZSTD_calloc(n,s) ark_zstd_calloc((n), (s))
#define ZSTD_free(p) ark_zstd_free((p))
#define ZSTD_DEPS_MATH64
#define ZSTD_div64(dividend, divisor) ((dividend) / (divisor))
#define ZSTD_DEPS_ASSERT
#define assert(x) ((void)0)
#define ZSTD_DEPS_IO
#define ZSTD_DEBUG_PRINT(...) ((void)0)
#define ZSTD_DEPS_STDINT
#endif
