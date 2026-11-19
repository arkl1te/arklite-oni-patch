/* Arklite: no C runtime; zstd's few uses of <stdlib.h> (ark_zstd_deps.h) */
#ifndef ARK_STDLIB_H
#define ARK_STDLIB_H
#include <stddef.h>
void *ark_zstd_malloc(size_t n);
void ark_zstd_free(void *p);
#define malloc(s) ark_zstd_malloc(s)
#define free(p) ark_zstd_free(p)
#define _byteswap_ushort(x) __builtin_bswap16(x)
#define _byteswap_ulong(x) __builtin_bswap32(x)
#define _byteswap_uint64(x) __builtin_bswap64(x)
#endif
