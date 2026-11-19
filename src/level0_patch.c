/* level0 patches: a level0 file as Zstandard frames against a level0 file the other PC already has.
 *
 * A joiner whose level0 differs from the host's gets the host's pair (net.c "level0 download"). A
 * mod adds textures and sounds to AE's level0 and changes some instances; against a stock AE pair
 * the rest is found in the stock file and what's new compresses to about half. Measured 2026-11-16
 * (zstd CLI, the same settings as here): a 197.6 MB modded pair came to 8.2 MB as two patches, where
 * the block sync (net.c "level0 sync") sent 35 MB and the full download 197.6 MB; a 217.8 MB pair
 * to 12.1 MB. Building a patch took about 1 s of one core of a Ryzen 9 7950X, applying one 0.2 s.
 *
 * Each file is cut into L0P_SEG pieces, each one zstd frame (level L0P_LEVEL, long-distance
 * matching, a window that reaches over the whole base) with the whole base file as its prefix
 * (ZSTD_CCtx_refPrefix: matches into the base at any offset), so applying one needs the base and
 * one piece of the result mapped at a time: Oni.exe isn't large-address-aware (2 GB), and a 499 MB
 * level0 in one piece would have needed the base and the whole result at once. 64 MB pieces cost
 * about 4% against one piece (32 MB ones 40%: measured).
 *
 * File: L0pHead, then nseg frame sizes (u32), then the frames. Every frame carries zstd's own
 * checksum; the caller checks the whole result's hash too (net.c). zstd's memory comes from a heap
 * of its own, never Oni's (rollback snapshots Oni's heap). zstd is used under its BSD licence
 * (src/zstd/LICENSE.txt, README.txt). */
#include "arklite.h"
#include "zstd/zstd.h"

void *memset(void *, int, unsigned);

__declspec(dllimport) int    WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) u32    WINAPI GetFileSize(HANDLE, u32 *);
__declspec(dllimport) u32    WINAPI SetFilePointer(HANDLE, int, int *, u32);
__declspec(dllimport) int    WINAPI SetEndOfFile(HANDLE);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) int    WINAPI DeleteFileA(const char *);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) HANDLE WINAPI CreateFileMappingA(HANDLE, void *, u32, u32, u32, const char *);
__declspec(dllimport) void * WINAPI MapViewOfFile(HANDLE, u32, u32, u32, u32);
__declspec(dllimport) int    WINAPI UnmapViewOfFile(const void *);
__declspec(dllimport) HANDLE WINAPI HeapCreate(u32, u32, u32);
__declspec(dllimport) void * WINAPI HeapAlloc(HANDLE, u32, u32);
__declspec(dllimport) int    WINAPI HeapFree(HANDLE, u32, void *);
__declspec(dllimport) u32    WINAPI GetLastError(void);

#define L0P_MAGIC   0x505a4b41u    /* "AKZP" */
#define L0P_VERSION 1
#define L0P_SEG     (64u << 20)    /* a multiple of the 64 KB mapping granularity */
#define L0P_LEVEL   6
#define L0P_MAXSEG  64             /* 4 GB */

typedef struct { u32 magic, version, base_size, base_hash, size, hash, seg, nseg; } L0pHead;

/* ---- What zstd needs of a C runtime (src/zstd/ark_zstd_deps.h) -------------------------- */

static HANDLE g_zheap;

void *ark_zstd_malloc(size_t n)
{
	if (!g_zheap) g_zheap = HeapCreate(0, 0, 0);   /* (serialized: the host's patch thread and the main one) */
	return g_zheap ? HeapAlloc(g_zheap, 0, (u32)n) : 0;
}

void *ark_zstd_calloc(size_t n, size_t s)
{
	if (!g_zheap) g_zheap = HeapCreate(0, 0, 0);
	if (s && n > 0xffffffffu / s) return 0;
	return g_zheap ? HeapAlloc(g_zheap, 8 /* HEAP_ZERO_MEMORY */, (u32)(n * s)) : 0;
}

void ark_zstd_free(void *p)
{
	if (p && g_zheap) HeapFree(g_zheap, 0, p);
}

/* memmove: zstd's overlapping copies (memcpy and memset are common.c's) */
__attribute__((naked)) void *memmove(void *d, const void *s, unsigned n)
{
	__asm__ volatile(
		"pushl %esi\n\t"
		"pushl %edi\n\t"
		"movl 12(%esp), %edi\n\t"
		"movl 16(%esp), %esi\n\t"
		"movl 20(%esp), %ecx\n\t"
		"movl %edi, %eax\n\t"
		"cmpl %esi, %edi\n\t"
		"jbe 1f\n\t"
		"leal -1(%esi,%ecx), %esi\n\t"   /* d above s: from the end */
		"leal -1(%edi,%ecx), %edi\n\t"
		"std\n\t"
		"rep movsb\n\t"
		"cld\n\t"
		"jmp 2f\n"
		"1:\n\t"
		"rep movsb\n"
		"2:\n\t"
		"popl %edi\n\t"
		"popl %esi\n\t"
		"ret\n");
}

/* n / d without a 64-bit division (no C runtime) */
static unsigned long long udiv64(unsigned long long n, unsigned long long d)
{
	unsigned long long q = 0;
	int shift = 0;
	if (!d) return 0;
	if (!(n >> 32) && !(d >> 32)) return (u32)n / (u32)d;
	while (!(d >> 63) && (d << 1) <= n) {
		d <<= 1;
		shift++;
	}
	for (; shift >= 0; shift--) {
		q <<= 1;
		if (n >= d) {
			n -= d;
			q |= 1;
		}
		d >>= 1;
	}
	return q;
}

/* MSVC's helper for u64 / u64 on 32-bit x86, which clang calls: {n lo, n hi, d lo, d hi} on the
 * stack, the quotient in edx:eax, the callee pops the 16 bytes */
__attribute__((naked, used)) void _aulldiv(void)
{
	__asm__ volatile(
		"pushl 16(%%esp)\n\t"
		"pushl 16(%%esp)\n\t"
		"pushl 16(%%esp)\n\t"
		"pushl 16(%%esp)\n\t"
		"call %c0\n\t"
		"addl $16, %%esp\n\t"
		"ret $16\n" :: "i"(udiv64));
}

/* ---- Files ------------------------------------------------------------------------------- */

typedef struct { HANDLE f, m; const u8 *p; u32 size; } MapIn;

/* path mapped read-only, whole (an empty file maps to nothing, size 0); 0 when it can't be */
static int map_in(const char *path, MapIn *r)
{
	memset(r, 0, sizeof *r);
	r->f = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 3 /* share read+write: Oni has them open */, 0, 3, 0x80, 0);
	r->m = 0;
	if (r->f == INVALID_HANDLE_VALUE) return 0;
	r->size = GetFileSize(r->f, 0);
	if (r->size == 0xffffffffu) return 0;
	if (!r->size) return 1;
	r->m = CreateFileMappingA(r->f, 0, 2 /* PAGE_READONLY */, 0, 0, 0);
	if (!r->m) return 0;
	r->p = MapViewOfFile(r->m, 4 /* FILE_MAP_READ */, 0, 0, 0);
	return r->p != 0;
}

static void map_out(MapIn *r)
{
	if (r->p) UnmapViewOfFile(r->p);
	if (r->m) CloseHandle(r->m);
	if (r->f && r->f != INVALID_HANDLE_VALUE) CloseHandle(r->f);
	memset(r, 0, sizeof *r);
}

static u32 highbit(u32 v)
{
	u32 b = 0;
	while (v >>= 1) b++;
	return b;
}

/* ---- Making one (the host) ---------------------------------------------------------------- */

/* A patch that makes target (hash target_hash) out of base (hash base_hash), written to out;
 * 1 when it's there, 0 with why */
int l0p_make(const char *base, u32 base_hash, const char *target, u32 target_hash, const char *out, const char **why)
{
	MapIn b, t;
	HANDLE o = INVALID_HANDLE_VALUE, tm = 0;
	ZSTD_CCtx *c = 0;
	u8 *buf = 0;
	u32 table[L0P_MAXSEG], w, cap = (u32)ZSTD_compressBound(L0P_SEG);
	L0pHead h;
	int ok = 0;

	*why = "?";
	memset(&t, 0, sizeof t);
	if (!map_in(base, &b)) { *why = "can't read the base level0"; goto done; }
	t.f = CreateFileA(target, 0x80000000, 3, 0, 3, 0x80, 0);
	if (t.f == INVALID_HANDLE_VALUE || (t.size = GetFileSize(t.f, 0)) == 0xffffffffu) { *why = "can't read the level0 to send"; goto done; }
	memset(&h, 0, sizeof h);
	h.magic = L0P_MAGIC;
	h.version = L0P_VERSION;
	h.base_size = b.size;
	h.base_hash = base_hash;
	h.size = t.size;
	h.hash = target_hash;
	h.seg = L0P_SEG;
	h.nseg = (t.size + L0P_SEG - 1) / L0P_SEG;
	if (h.nseg > L0P_MAXSEG) { *why = "the level0 is too big"; goto done; }
	if (t.size && !(tm = CreateFileMappingA(t.f, 0, 2, 0, 0, 0))) { *why = "can't map the level0 to send"; goto done; }
	if (!(buf = VirtualAlloc(0, cap, 0x3000, 4))) { *why = "out of memory"; goto done; }
	if (!(c = ZSTD_createCCtx())) { *why = "out of memory"; goto done; }
	{   /* the window reaches from a piece's end back over the whole base (it's the piece's prefix) */
		u32 reach = b.size + (t.size < L0P_SEG ? t.size : L0P_SEG), wlog = highbit(reach ? reach : 1) + 1;
		if (wlog < 20) wlog = 20;
		if (wlog > 30) wlog = 30;   /* (ZSTD_WINDOWLOG_MAX_32) */
		if (ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, L0P_LEVEL)) ||
		    ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_windowLog, (int)wlog)) ||
		    ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_enableLongDistanceMatching, 1)) ||
		    ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 1)) ||
		    ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_contentSizeFlag, 1))) { *why = "zstd refused its settings"; goto done; }
	}
	o = CreateFileA(out, 0x40000000 /* GENERIC_WRITE */, 0, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (o == INVALID_HANDLE_VALUE) { *why = "can't write the patch"; goto done; }
	SetFilePointer(o, (int)(sizeof h + h.nseg * 4), 0, 0);   /* (the head and the table at the end) */
	for (u32 i = 0; i < h.nseg; i++) {
		u32 off = i * L0P_SEG, len = t.size - off < L0P_SEG ? t.size - off : L0P_SEG;
		const u8 *src = MapViewOfFile(tm, 4, 0, off, len);
		size_t r;
		if (!src) { *why = "can't map a piece of the level0 (address space)"; goto done; }
		if (ZSTD_isError(ZSTD_CCtx_refPrefix(c, b.p, b.size))) { UnmapViewOfFile(src); *why = "zstd refused the base"; goto done; }
		r = ZSTD_compress2(c, buf, cap, src, len);
		UnmapViewOfFile(src);
		if (ZSTD_isError(r)) { *why = ZSTD_getErrorName(r); goto done; }
		if (!WriteFile(o, buf, (u32)r, &w, 0) || w != (u32)r) { *why = "can't write the patch"; goto done; }
		table[i] = (u32)r;
	}
	SetFilePointer(o, 0, 0, 0);
	if (!WriteFile(o, &h, sizeof h, &w, 0) || w != sizeof h || !WriteFile(o, table, h.nseg * 4, &w, 0) || w != h.nseg * 4) {
		*why = "can't write the patch";
		goto done;
	}
	ok = 1;

done:
	if (c) ZSTD_freeCCtx(c);
	if (buf) VirtualFree(buf, 0, 0x8000);
	if (tm) CloseHandle(tm);
	if (t.f && t.f != INVALID_HANDLE_VALUE) CloseHandle(t.f);
	map_out(&b);
	if (o != INVALID_HANDLE_VALUE) CloseHandle(o);
	if (!ok) DeleteFileA(out);
	return ok;
}

/* The patch's head, if it's a patch for (base_size, base_hash) -> (size, hash); 1 when it is */
int l0p_check(const char *patch, u32 base_size, u32 base_hash, u32 size, u32 hash)
{
	L0pHead h;
	u32 got = 0;
	HANDLE f = CreateFileA(patch, 0x80000000, 1, 0, 3, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return 0;
	int ok = ReadFile(f, &h, sizeof h, &got, 0) && got == sizeof h && h.magic == L0P_MAGIC && h.version == L0P_VERSION &&
	         h.base_size == base_size && h.base_hash == base_hash && h.size == size && h.hash == hash;
	CloseHandle(f);
	return ok;
}

/* ---- Applying one (the joiner) ------------------------------------------------------------ */

/* out made from base with patch (the hash isn't checked here); progress(piece, pieces) after each
 * piece, nonzero from it cancels; 1 when out is whole, 0 (why) or -1 (cancelled) */
int l0p_apply(const char *patch, const char *base, u32 base_hash, const char *out, int (*progress)(u32, u32), const char **why)
{
	MapIn b;
	HANDLE p = INVALID_HANDLE_VALUE, o = INVALID_HANDLE_VALUE, om = 0;
	ZSTD_DCtx *d = 0;
	u8 *frame = 0;
	u32 table[L0P_MAXSEG], got, biggest = 0;
	L0pHead h;
	int ok = 0;

	*why = "?";
	memset(&b, 0, sizeof b);
	p = CreateFileA(patch, 0x80000000, 1, 0, 3, 0x80, 0);
	if (p == INVALID_HANDLE_VALUE || !ReadFile(p, &h, sizeof h, &got, 0) || got != sizeof h || h.magic != L0P_MAGIC ||
	    h.version != L0P_VERSION || h.seg != L0P_SEG || h.nseg > L0P_MAXSEG || h.nseg != (h.size + L0P_SEG - 1) / L0P_SEG ||
	    !ReadFile(p, table, h.nseg * 4, &got, 0) || got != h.nseg * 4) { *why = "the patch is damaged"; goto done; }
	if (h.base_hash != base_hash) { *why = "the patch is for another base level0"; goto done; }
	if (!map_in(base, &b) || b.size != h.base_size) { *why = "can't read the base level0"; goto done; }
	for (u32 i = 0; i < h.nseg; i++)
		if (table[i] > biggest) biggest = table[i];
	if (h.nseg && !(frame = VirtualAlloc(0, biggest, 0x3000, 4))) { *why = "out of memory"; goto done; }
	o = CreateFileA(out, 0xc0000000 /* GENERIC_READ | GENERIC_WRITE */, 0, 0, 2, 0x80, 0);
	if (o == INVALID_HANDLE_VALUE) { *why = "can't write the level0"; goto done; }
	if (h.size) {
		if (SetFilePointer(o, (int)h.size, 0, 0) == 0xffffffffu || !SetEndOfFile(o)) { *why = "no room for the level0 (disk full?)"; goto done; }
		if (!(om = CreateFileMappingA(o, 0, 4 /* PAGE_READWRITE */, 0, h.size, 0))) { *why = "can't map the level0 being made"; goto done; }
	}
	if (!(d = ZSTD_createDCtx()) || ZSTD_isError(ZSTD_DCtx_setParameter(d, ZSTD_d_windowLogMax, 30))) { *why = "out of memory"; goto done; }
	for (u32 i = 0; i < h.nseg; i++) {
		u32 off = i * L0P_SEG, len = h.size - off < L0P_SEG ? h.size - off : L0P_SEG;
		u8 *dst;
		size_t r;
		if (!ReadFile(p, frame, table[i], &got, 0) || got != table[i]) { *why = "the patch is cut short"; goto done; }
		if (!(dst = MapViewOfFile(om, 2 /* FILE_MAP_WRITE */, 0, off, len))) { *why = "can't map a piece of the level0 (address space)"; goto done; }
		if (ZSTD_isError(ZSTD_DCtx_refPrefix(d, b.p, b.size))) { UnmapViewOfFile(dst); *why = "zstd refused the base"; goto done; }
		r = ZSTD_decompressDCtx(d, dst, len, frame, table[i]);   /* (into the mapped piece: zstd needs no window of its own) */
		UnmapViewOfFile(dst);
		if (ZSTD_isError(r)) { *why = ZSTD_getErrorName(r); goto done; }
		if (r != len) { *why = "a piece came out the wrong size"; goto done; }
		if (progress && progress(i + 1, h.nseg)) { ok = -1; *why = "cancelled"; goto done; }
	}
	ok = 1;

done:
	if (d) ZSTD_freeDCtx(d);
	if (frame) VirtualFree(frame, 0, 0x8000);
	if (om) CloseHandle(om);
	if (o != INVALID_HANDLE_VALUE) CloseHandle(o);
	if (p != INVALID_HANDLE_VALUE) CloseHandle(p);
	map_out(&b);
	if (ok != 1) DeleteFileA(out);
	return ok;
}
