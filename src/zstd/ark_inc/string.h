/* Arklite: no C runtime; zstd's few uses of <string.h> (ark_zstd_deps.h) */
#ifndef ARK_STRING_H
#define ARK_STRING_H
#include <stddef.h>
#define memcpy(d,s,l) __builtin_memcpy((d),(s),(l))
#define memmove(d,s,l) __builtin_memmove((d),(s),(l))
#define memset(p,v,l) __builtin_memset((p),(v),(l))
#define memcmp(a,b,l) __builtin_memcmp((a),(b),(l))
#endif
