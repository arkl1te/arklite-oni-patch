/* Text beyond ASCII in multiplayer: names and chat in Spanish, Russian (Cyrillic), Japanese and
 * Chinese, typed with any keyboard layout or IME, sent to the other players and drawn by Oni.
 *
 * The bytes. Oni's text is bytes, and its text system reads a byte >= 0x80 (but 0xff, the caret)
 * and the byte after it as one 16-bit character: the high byte picks one of the font's 256 glyph
 * arrays (TSFT +0x14), the low byte the glyph in it; a missing array draws nothing (retail
 * 0x42dbd6, 0x42cabb, 0x42d8ef; OniFoxed TSiString_GetNextCharacter, TSiFont_GetGlyph). Stock
 * Tahoma only has array 0. Arklite's text keeps ASCII as it is and writes every other character
 * as two bytes, both >= 0x80, so no byte of one ever reads as ASCII (a space, '/', ':', Oni's
 * style code '\', a NUL) to code that walks bytes: code k (text_map.h, tools/gen_text_map.py) is
 * 0x80 + k / 128, 0x80 + k % 128. 16256 codes: Latin-1 and Extended-A, Greek, Cyrillic,
 * punctuation, kana, fullwidth forms, every Han character of GB2312 and of JIS X 0208/cp932, and
 * the commonest 4435 more of Big5 (all of its level 1). Names and chat travel between the PCs in
 * these bytes (net.c), so both need the same Arklite build (the source hash already says so).
 * Outside the game they become UTF-8: the log (common.c log_msg), the dedicated server's console
 * (server.c), Arklite.ini (\uXXXX escapes when written, so no Windows code page can mangle it;
 * UTF-8 typed by hand reads too).
 *
 * Every text Arklite takes in (typed, from the network, from the ini) goes through text_clean,
 * which drops anything but printable ASCII and whole codes, and str_cpy never cuts one in half:
 * a lone lead byte before the NUL would make Oni read past the end of the string.
 *
 * The glyphs. text_frame gives every Tahoma font (each TSFT loaded, and big_font.c's when it makes
 * one) glyph arrays 0x80..0xfe of its own memory. Every glyph is a box as wide as its advance and
 * as tall as the font (origin (0, ascender)), as Bungie's are, so measuring (0x42e920 and the rest
 * read the advances straight from the arrays) is right before any pixel exists. The pixels are made
 * when Oni first draws the glyph: its two calls that read a glyph's bitmap, 0x42cb80 (into the
 * font's cache texture, from the string drawer at 0x42dd8b) and 0x42cc40 (straight into a texture,
 * 0x42d993), come here first. GDI draws the character (white on black, grayscale antialiased, its
 * baseline on the font's ascender) and the coverage goes into our memory as Oni's bitmaps are: a
 * byte a pixel, 255 empty, 0 full ink, rows as wide as the glyph. Oni finds them at font + 0x418 +
 * offset * 4 (0x42cdad, no bounds check), so the offset is set to reach our memory, 4-aligned with
 * the font. A glyph never gets wider than the font's cache cell (cell width = the widest of the
 * font's own 256 glyphs, ~1.3 em with Tahoma's '%'), so a full-width ideograph fits.
 *
 * Which font draws a character: Tahoma when it has it (Latin, Greek, Cyrillic), else the first of
 * a list that does (GetGlyphIndicesW): CJK faces ordered by the user's Windows language (Japanese
 * first for Japanese, Traditional for Taiwan and Hong Kong, Simplified otherwise), Windows' and
 * the common Linux ones (Wine sees the desktop's fonts). A CJK face's ideographs all share one
 * advance (they're square), so one is measured for all of them: measuring 20000 glyphs a size
 * takes seconds under Wine. Characters no face has draw as Tahoma's box.
 *
 * Typing. Oni's message loop (0x4036d5) uses PeekMessageA and DispatchMessageA, and so does
 * Daodan's (AE's binkw32.dll replaces Oni's input pump with DDiPlatform_InputEvent_GetEvent): a
 * character outside the Windows code page became '?' before anything saw it. text_install points
 * both modules' imports of those at PeekMessageW and DispatchMessageW (Windows converts for Oni's own,
 * ANSI, window procedure as before), and Arklite's window procedures are Unicode ones (chat.c,
 * mp_names.c, menu_wheel.c, and this file's), so WM_CHAR brings the real character. An IME's
 * finished text (WM_IME_COMPOSITION, GCS_RESULTSTR) is turned into WM_CHARs here; DefWindowProcA
 * would have made code-page bytes of it. While the chat is open what's being composed shows in
 * its field (text_composition) and the IME's own composition window is left out; for the F2 name
 * and the menus' fields the IME shows it. The IME is only on while one of those has the keyboard
 * (ImmAssociateContext), so W, A, S, D in a match never start a composition. */
#include "arklite.h"
#include "text_map.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);
__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) u16    WINAPI GetUserDefaultUILanguage(void);
__declspec(dllimport) u32    WINAPI GetTickCount(void);

/* ---- GDI ----------------------------------------------------------------------------------- */

typedef struct {
	u32 biSize; int biWidth, biHeight; u16 biPlanes, biBitCount;
	u32 biCompression, biSizeImage; int biXPelsPerMeter, biYPelsPerMeter; u32 biClrUsed, biClrImportant;
	u32 bmiColors[1];
} BITMAPINFO;

typedef struct { int cx, cy; } SIZE;

__declspec(dllimport) HANDLE WINAPI CreateCompatibleDC(HANDLE);
__declspec(dllimport) int    WINAPI DeleteDC(HANDLE);
__declspec(dllimport) HANDLE WINAPI CreateDIBSection(HANDLE, const BITMAPINFO *, u32, void **, HANDLE, u32);
__declspec(dllimport) HANDLE WINAPI SelectObject(HANDLE, HANDLE);
__declspec(dllimport) int    WINAPI DeleteObject(HANDLE);
__declspec(dllimport) HANDLE WINAPI CreateFontW(int, int, int, int, int, u32, u32, u32, u32, u32, u32, u32, u32, const u16 *);
__declspec(dllimport) u32    WINAPI SetTextColor(HANDLE, u32);
__declspec(dllimport) int    WINAPI SetBkMode(HANDLE, int);
__declspec(dllimport) u32    WINAPI SetTextAlign(HANDLE, u32);
__declspec(dllimport) int    WINAPI TextOutW(HANDLE, int, int, const u16 *, int);
__declspec(dllimport) int    WINAPI GetTextExtentPoint32W(HANDLE, const u16 *, int, SIZE *);
__declspec(dllimport) int    WINAPI GetTextFaceW(HANDLE, int, u16 *);
__declspec(dllimport) u32    WINAPI GetGlyphIndicesW(HANDLE, const u16 *, int, u16 *, u32);
__declspec(dllimport) int    WINAPI GetCharWidth32W(HANDLE, u32, u32, int *);
__declspec(dllimport) int    WINAPI GdiFlush(void);

#define FW_NORMAL           400
#define FW_BOLD             700
#define DEFAULT_CHARSET     1
#define OUT_TT_PRECIS       4
#define ANTIALIASED_QUALITY 4
#define TRANSPARENT         1
#define TA_BASELINE         24
#define GGI_MARK_NONEXISTING_GLYPHS 1

/* ---- User32 and the IME -------------------------------------------------------------------- */

typedef long (WINAPI *WNDPROC)(void *, u32, u32, long);
__declspec(dllimport) long WINAPI SetWindowLongW(void *, int, long);
__declspec(dllimport) long WINAPI CallWindowProcW(WNDPROC, void *, u32, u32, long);
__declspec(dllimport) long WINAPI SendMessageW(void *, u32, u32, long);
__declspec(dllimport) int  WINAPI GetClientRect(void *, int *);

#define GWL_WNDPROC              (-4)
#define WM_CHAR                  0x102
#define WM_IME_STARTCOMPOSITION  0x10d
#define WM_IME_ENDCOMPOSITION    0x10e
#define WM_IME_COMPOSITION       0x10f
#define WM_IME_CHAR              0x286
#define GCS_COMPSTR              0x8
#define GCS_CURSORPOS            0x80
#define GCS_RESULTSTR            0x800
#define NI_COMPOSITIONSTR        0x15
#define CPS_CANCEL               0x4
#define CFS_POINT                0x2
#define CFS_EXCLUDE              0x80

typedef struct { u32 style; int x, y; int left, top, right, bottom; } COMPOSITIONFORM;
typedef struct { u32 index, style; int x, y; int left, top, right, bottom; } CANDIDATEFORM;

static void *(WINAPI *ImmGetContext)(void *);
static int   (WINAPI *ImmReleaseContext)(void *, void *);
static void *(WINAPI *ImmAssociateContext)(void *, void *);
static long  (WINAPI *ImmGetCompositionStringW)(void *, u32, void *, u32);
static int   (WINAPI *ImmSetCompositionWindow)(void *, COMPOSITIONFORM *);
static int   (WINAPI *ImmSetCandidateWindow)(void *, CANDIDATEFORM *);
static int   (WINAPI *ImmNotifyIME)(void *, u32, u32, u32);

#define ONgGameWindow       (*(void **)0x63100c)        /* ONgPlatformData.gameWindow */

/* ---- Oni ----------------------------------------------------------------------------------- */

#define TAG_TSFT            0x54534654
#define TSiGlyph_DrawToCell ((void (ONI_FC *)(u8 *, u8 *, void *, u32, u32))0x42cb80)
#define TSiGlyph_Draw       ((void (ONI_FC *)(u8 *, u8 *, u32, u32, u32, u32, u32))0x42cc40)
#define TScGlyphToCellCall  0x42dd8b
#define TScGlyphDrawCall    0x42d993
#define FONT_SIZE(f)        (*(u16 *)((f) + 0x6))
#define FONT_STYLE(f)       (*(u32 *)((f) + 0x8))
#define FONT_ASCENDER(f)    (*(short *)((f) + 0xc))
#define FONT_DESCENDER(f)   (*(short *)((f) + 0xe))
#define FONT_ARRAYS(f)      ((u8 **)((f) + 0x14))
#define FONT_HEAD           0x418
#define GLYPH               0x14
#define ARRAY_BYTES         (256 * GLYPH)
#define LEAD0               0x80
#define LEADS               ((TEXT_MAP_COUNT + 127) / 128)
#define G_CODE(g)           (*(u16 *)((g) + 0))
#define G_ADVANCE(g)        (*(u16 *)((g) + 2))
#define G_WIDTH(g)          (*(short *)((g) + 4))
#define G_HEIGHT(g)         (*(short *)((g) + 6))
#define G_ORIGIN_X(g)       (*(short *)((g) + 8))
#define G_ORIGIN_Y(g)       (*(short *)((g) + 10))
#define G_OFFSET(g)         (*(u32 *)((g) + 12))
#define G_CELL(g)           (*(u32 *)((g) + 16))

/* ---- The codes ----------------------------------------------------------------------------- */

static u16 *g_code;   /* by BMP character: its code + 1, 0: none */

static int map_ready(void)
{
	u16 *t;
	if (g_code) return 1;
	if (!(t = VirtualAlloc(0, 0x10000 * 2, 0x3000, 4))) return 0;
	for (int k = 0; k < TEXT_MAP_COUNT; k++) t[g_text_map[k]] = (u16)(k + 1);
	g_code = t;
	return 1;
}

static u32 code_char(const u8 *p)
{
	u32 k = (u32)(p[0] - LEAD0) * 128 + (u32)(p[1] - 0x80);
	return p[0] >= LEAD0 && p[0] != 0xff && p[1] >= 0x80 && k < TEXT_MAP_COUNT ? g_text_map[k] : 0;
}

int text_put(char *d, u32 c)
{
	u32 k;
	if (c >= 0x20 && c < 0x7f) {
		d[0] = (char)c;
		return 1;
	}
	if (c > 0xffff || !map_ready() || !g_code[c]) return 0;
	k = g_code[c] - 1u;
	d[0] = (char)(LEAD0 + k / 128);
	d[1] = (char)(0x80 + k % 128);
	return 2;
}

/* As Oni reads it: a byte >= 0x80 (not 0xff) and any byte after it are one character */
u32 text_get(const char *s, int *n)
{
	const u8 *p = (const u8 *)s;
	u32 c;
	if (!p[0]) { *n = 0; return 0; }
	if (p[0] < 0x80) { *n = 1; return p[0]; }
	if (p[0] != 0xff && p[1]) {
		*n = 2;
		c = code_char(p);
		return c ? c : '?';
	}
	*n = 1;
	return '?';
}

int text_next(const char *s, int i)
{
	int n;
	text_get(s + i, &n);
	return i + n;
}

int text_prev(const char *s, int i)
{
	int at = 0, last = 0;
	while (at < i && s[at]) {
		last = at;
		at = text_next(s, at);
	}
	return last;
}

int text_wide(const char *s, int i) { return (u8)s[i] >= LEAD0; }

/* Printable ASCII and whole codes; never longer than size - 1, never a code cut in half. In place
 * is fine (d == s). Returns the length. */
int text_clean(char *d, unsigned size, const char *s)
{
	const u8 *p = (const u8 *)s;
	unsigned n = 0;
	while (*p) {
		if (*p >= 0x20 && *p < 0x7f) {
			if (n + 1 >= size) break;
			d[n++] = (char)*p++;
		} else if (*p >= LEAD0 && p[1] && code_char(p)) {
			if (n + 2 >= size) break;
			d[n++] = (char)p[0];
			d[n++] = (char)p[1];
			p += 2;
		} else {
			p++;
		}
	}
	if (size) d[n] = 0;
	return (int)n;
}

/* Windows-1252 0x80..0x9f, for bytes that aren't UTF-8 */
static const u16 g_cp1252[32] = {
	0x20ac, 0x81, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8d, 0x017d, 0x8f,
	0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x9d, 0x017e, 0x0178,
};

/* One character of UTF-8 (a byte that isn't, as Windows-1252); *n its bytes */
static u32 utf8_get(const u8 *p, int *n)
{
	u32 c;
	int len, i;
	if (p[0] < 0x80) { *n = 1; return p[0]; }
	if ((p[0] & 0xe0) == 0xc0) len = 2, c = p[0] & 0x1f;
	else if ((p[0] & 0xf0) == 0xe0) len = 3, c = p[0] & 0x0f;
	else if ((p[0] & 0xf8) == 0xf0) len = 4, c = p[0] & 0x07;
	else len = 0, c = 0;
	for (i = 1; i < len && (p[i] & 0xc0) == 0x80; i++) c = c << 6 | (p[i] & 0x3f);
	if (len && i == len && c >= (len == 2 ? 0x80u : len == 3 ? 0x800u : 0x10000u)) {
		*n = len;
		return c;
	}
	*n = 1;
	return p[0] < 0xa0 ? g_cp1252[p[0] - 0x80] : p[0];
}

int text_from_utf8(char *d, unsigned size, const char *s)
{
	const u8 *p = (const u8 *)s;
	unsigned n = 0;
	char b[2];
	while (*p) {
		int len, k;
		u32 c = utf8_get(p, &len);
		p += len;
		if (c < 0x20) continue;
		k = text_put(b, c);
		if (!k) k = text_put(b, '?');
		if (n + (unsigned)k >= size) break;
		memcpy(d + n, b, (unsigned)k);
		n += (unsigned)k;
	}
	if (size) d[n] = 0;
	return (int)n;
}

/* s[0, n) (n < 0: to its NUL) as UTF-8. Every byte that isn't part of a code passes as it is
 * (paths in the log keep their bytes). */
int text_to_utf8_n(char *d, unsigned size, const char *s, int n)
{
	const u8 *p = (const u8 *)s, *end = n < 0 ? 0 : p + n;
	unsigned at = 0;
	while (end ? p < end : *p) {
		u32 c = *p >= LEAD0 && (end ? p + 1 < end : 1) && p[1] ? code_char(p) : 0;
		u8 b[3];
		int k;
		if (c) {
			if (c < 0x800) b[0] = (u8)(0xc0 | c >> 6), b[1] = (u8)(0x80 | (c & 0x3f)), k = 2;
			else b[0] = (u8)(0xe0 | c >> 12), b[1] = (u8)(0x80 | (c >> 6 & 0x3f)), b[2] = (u8)(0x80 | (c & 0x3f)), k = 3;
			p += 2;
		} else {
			b[0] = *p++;
			k = 1;
		}
		if (at + (unsigned)k >= size) break;
		memcpy(d + at, b, (unsigned)k);
		at += (unsigned)k;
	}
	if (size) d[at] = 0;
	return (int)at;
}

int text_to_utf8(char *d, unsigned size, const char *s) { return text_to_utf8_n(d, size, s, -1); }

/* s[0, n) (n < 0: to its NUL) as UTF-16; ASCII control characters pass */
int text_to_utf16_n(u16 *d, unsigned count, const char *s, int n)
{
	const u8 *p = (const u8 *)s, *end = n < 0 ? 0 : p + n;
	unsigned at = 0;
	while ((end ? p < end : *p) && at + 1 < count) {
		u32 c = *p >= LEAD0 && (end ? p + 1 < end : 1) && p[1] ? code_char(p) : 0;
		if (c) {
			d[at++] = (u16)c;
			p += 2;
		} else {
			d[at++] = *p < 0x80 ? *p : '?';
			p++;
		}
	}
	if (count) d[at] = 0;
	return (int)at;
}

int text_to_utf16(u16 *d, unsigned count, const char *s) { return text_to_utf16_n(d, count, s, -1); }

int text_has_wide(const char *s)
{
	for (; *s; s++)
		if ((u8)*s >= LEAD0) return 1;
	return 0;
}

/* Arklite.ini: \uXXXX for every character beyond ASCII (and \\ for \), plain ASCII as it is;
 * reading takes those, UTF-8 and Windows-1252 */
void ini_text(const char *section, const char *key, const char *fallback, char *out, unsigned size)
{
	char raw[512], b[2];
	unsigned n = 0;
	const char *p = raw;

	GetPrivateProfileStringA(section, key, fallback, raw, sizeof raw, ini_path());
	while (*p) {
		u32 c;
		int len, k;
		if (p[0] == '\\' && p[1] == '\\') {
			c = '\\';
			len = 2;
		} else if (p[0] == '\\' && (p[1] == 'u' || p[1] == 'U')) {
			int i;
			c = 0;
			for (i = 2; i < 6; i++) {
				char h = p[i];
				int v = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
				if (v < 0) break;
				c = c << 4 | (u32)v;
			}
			if (i < 6) c = '\\', len = 1;
			else len = 6;
		} else {
			c = utf8_get((const u8 *)p, &len);
		}
		p += len;
		if (c < 0x20) continue;
		k = text_put(b, c);
		if (!k) k = text_put(b, '?');
		if (n + (unsigned)k >= size) break;
		memcpy(out + n, b, (unsigned)k);
		n += (unsigned)k;
	}
	if (size) out[n] = 0;
}

void ini_text_write(const char *section, const char *key, const char *value)
{
	static const char hex[] = "0123456789abcdef";
	char out[512];
	unsigned n = 0;
	while (*value && n + 7 < sizeof out) {
		int len;
		u32 c = text_get(value, &len);
		if (c >= 0x80) {
			out[n++] = '\\';
			out[n++] = 'u';
			for (int sh = 12; sh >= 0; sh -= 4) out[n++] = hex[c >> sh & 15];
		} else {
			if (c == '\\') out[n++] = '\\';
			out[n++] = (char)c;
		}
		value += len;
	}
	out[n] = 0;
	WritePrivateProfileStringA(section, key, out, ini_path());
}

/* ---- Faces --------------------------------------------------------------------------------- */

#define FACES_MAX 40
static const u16 *g_face_name[FACES_MAX];
static int g_faces;                 /* found on this PC, in order */
static u8 *g_face_of;               /* by code: the face that draws it */
static u16 g_han_adv_char[FACES_MAX];   /* an ideograph the face has: its advance is all of them */
static int g_faces_ready, g_faces_failed;

#define W(s) ((const u16 *)L##s)

/* After Tahoma and before the CJK faces: the Latin, Greek and Cyrillic Tahoma may lack */
static const u16 *const g_latin_faces[] = { W("Segoe UI"), W("Arial"), W("Microsoft Sans Serif"), W("DejaVu Sans"),
                                             W("Liberation Sans"), W("Noto Sans"), 0 };
static const u16 *const g_japanese[] = { W("Meiryo"), W("Yu Gothic"), W("MS UI Gothic"), W("MS Gothic"), W("MS PGothic"),
                                         W("Noto Sans CJK JP"), W("Noto Sans JP"), W("Source Han Sans JP"), W("IPAPGothic"),
                                         W("IPAGothic"), W("VL PGothic"), W("TakaoPGothic"), 0 };
static const u16 *const g_simplified[] = { W("Microsoft YaHei"), W("Microsoft YaHei UI"), W("SimHei"), W("SimSun"),
                                           W("NSimSun"), W("Noto Sans CJK SC"), W("Noto Sans SC"), W("Source Han Sans SC"),
                                           W("Source Han Sans CN"), W("WenQuanYi Micro Hei"), W("WenQuanYi Zen Hei"),
                                           W("Droid Sans Fallback"), 0 };
static const u16 *const g_traditional[] = { W("Microsoft JhengHei"), W("Microsoft JhengHei UI"), W("PMingLiU"), W("MingLiU"),
                                            W("Noto Sans CJK TC"), W("Noto Sans TC"), W("Source Han Sans TC"),
                                            W("Source Han Sans TW"), W("AR PL UMing TW"), 0 };
static const u16 *const g_last[] = { W("Arial Unicode MS"), W("Noto Sans CJK KR"), W("Unifont"), 0 };

static int round_i(float v) { return (int)(v + 0.5f); }

static HANDLE gdi_font(const u16 *face, int points, int style)
{
	return CreateFontW(-round_i(points * 96.0f / 72.0f), 0, 0, 0, style == 1 ? FW_BOLD : FW_NORMAL, style == 2, 0, 0,
	                   DEFAULT_CHARSET, OUT_TT_PRECIS, 0, ANTIALIASED_QUALITY, 0, face);
}

static u16 lower(u16 c) { return c >= 'A' && c <= 'Z' ? (u16)(c + 32) : c; }

/* GDI picks another face when it doesn't have the one asked for */
static int face_is(HANDLE dc, const u16 *want)
{
	u16 got[64] = { 0 };
	int i;
	if (!GetTextFaceW(dc, 64, got)) return 0;
	for (i = 0; want[i]; i++)
		if (lower(got[i]) != lower(want[i])) return 0;
	return !got[i];
}

static void face_list_add(const u16 *const *list, int *n, const u16 **out)
{
	for (; *list && *n < FACES_MAX; list++) {
		int dup = 0;
		for (int i = 0; i < *n; i++) dup |= out[i] == *list;
		if (!dup) out[(*n)++] = *list;
	}
}

static void log_face(const char *what, const u16 *face)
{
	char a[64];
	int i;
	for (i = 0; face[i] && i < 63; i++) a[i] = face[i] < 0x80 ? (char)face[i] : '?';
	a[i] = 0;
	log_msg("text: %s %s", what, a);
}

/* Which face draws each code (once) */
static int faces_ready(void)
{
	const u16 *cand[FACES_MAX];
	int ncand = 0, missing = 0, lang = GetUserDefaultUILanguage(), primary = lang & 0x3ff, sub = lang >> 10;
	u16 *chars, *glyphs;
	HANDLE dc;
	u32 t0 = GetTickCount();

	if (g_faces_ready || g_faces_failed) return g_faces_ready;
	g_faces_failed = 1;
	if (!map_ready()) return 0;
	static const u16 *const tahoma[] = { W("Tahoma"), 0 };
	face_list_add(tahoma, &ncand, cand);
	face_list_add(g_latin_faces, &ncand, cand);
	if (primary == 0x11) {   /* Japanese */
		face_list_add(g_japanese, &ncand, cand);
		face_list_add(g_simplified, &ncand, cand);
		face_list_add(g_traditional, &ncand, cand);
	} else if (primary == 0x04 && (sub == 1 || sub == 3 || sub == 5)) {   /* Chinese: Taiwan, Hong Kong, Macao */
		face_list_add(g_traditional, &ncand, cand);
		face_list_add(g_simplified, &ncand, cand);
		face_list_add(g_japanese, &ncand, cand);
	} else {
		face_list_add(g_simplified, &ncand, cand);
		face_list_add(g_japanese, &ncand, cand);
		face_list_add(g_traditional, &ncand, cand);
	}
	face_list_add(g_last, &ncand, cand);

	g_face_of = VirtualAlloc(0, TEXT_MAP_COUNT, 0x3000, 4);
	chars = VirtualAlloc(0, TEXT_MAP_COUNT * 4, 0x3000, 4);
	dc = CreateCompatibleDC(0);
	if (!g_face_of || !chars || !dc) {
		log_msg("text: no memory or DC for the fonts");
		if (dc) DeleteDC(dc);
		return 0;
	}
	glyphs = chars + TEXT_MAP_COUNT;
	for (int k = 0; k < TEXT_MAP_COUNT; k++) {
		chars[k] = g_text_map[k];
		g_face_of[k] = 0xff;
	}
	for (int c = 0; c < ncand && g_faces < FACES_MAX; c++) {
		HANDLE f = gdi_font(cand[c], 14, 0), old;
		int took = 0;
		if (!f) continue;
		old = SelectObject(dc, f);
		if (face_is(dc, cand[c]) && GetGlyphIndicesW(dc, chars, TEXT_MAP_COUNT, glyphs, GGI_MARK_NONEXISTING_GLYPHS) != 0xffffffffu) {
			g_han_adv_char[g_faces] = 0;
			for (int k = 0; k < TEXT_MAP_COUNT; k++) {
				u16 ch = chars[k];
				if (glyphs[k] == 0xffff || (glyphs[k] == 0 && c)) continue;
				if (ch >= 0x4e00 && ch <= 0x9fff && !g_han_adv_char[g_faces]) g_han_adv_char[g_faces] = ch;
				if (g_face_of[k] == 0xff) g_face_of[k] = (u8)g_faces, took++;
			}
			if (took || !c) {
				g_face_name[g_faces++] = cand[c];
				log_face(took ? "font" : "font (draws nothing new)", cand[c]);
				if (!c && !took) log_msg("text: Tahoma has none of the characters?");
			}
		}
		SelectObject(dc, old);
		DeleteObject(f);
	}
	for (int k = 0; k < TEXT_MAP_COUNT; k++)
		if (g_face_of[k] == 0xff) g_face_of[k] = 0, missing++;
	DeleteDC(dc);
	VirtualFree(chars, 0, 0x8000);
	if (!g_faces) {
		log_msg("text: no Tahoma here: names and chat stay ASCII");
		return 0;
	}
	log_msg("text: %d fonts draw the %d characters beyond ASCII (%d in none of them), found in %u ms", g_faces, TEXT_MAP_COUNT,
	        missing, GetTickCount() - t0);
	g_faces_failed = 0;
	g_faces_ready = 1;
	return 1;
}

/* ---- Fonts --------------------------------------------------------------------------------- */

#define FONTS_MAX 40

typedef struct {
	u8  *font;
	u8  *arrays;                          /* LEADS glyph arrays: arrays 0x80.. of the font */
	u8  made[(TEXT_MAP_COUNT + 7) / 8];   /* the code's pixels are drawn */
} Font;

static Font g_font[FONTS_MAX];
static int  g_nfonts;
static u32  g_tsft_seen = 0xffffffffu;

/* Pixels: never freed (a font's few KB stay if big_font.c drops it) */
static u8 *g_pool;
static u32 g_pool_used, g_pool_size;

static u8 *pool_get(u32 n, u32 align_to)
{
	u32 at = (g_pool_used + 3) & ~3u;
	at += align_to & 3;
	if (!g_pool || at + n > g_pool_size) {
		u32 size = n + 8 > 0x40000 ? n + 8 : 0x40000;
		u8 *p = VirtualAlloc(0, size, 0x3000, 4);
		if (!p) return 0;
		g_pool = p;
		g_pool_size = size;
		g_pool_used = 0;
		at = align_to & 3;
	}
	g_pool_used = at + n;
	return g_pool + at;
}

/* The advances of the codes face draws, in one Oni font's size and style */
static void set_advances(HANDLE dc, u8 *arrays, int face, int asc, int height)
{
	static const u32 ranges[][2] = { { 0x00a0, 0x052f }, { 0x2000, 0x26ff }, { 0x3000, 0x33ff }, { 0xfe00, 0xffef } };
	static int widths[0x700];
	int han = -1;

	if (g_han_adv_char[face]) {
		int w = 0;
		if (GetCharWidth32W(dc, g_han_adv_char[face], g_han_adv_char[face], &w)) han = w;
	}
	for (int r = 0; r < 4; r++) {
		u32 lo = ranges[r][0], hi = ranges[r][1], any = 0;
		for (int k = 0; k < TEXT_MAP_COUNT && !any; k++)
			any = g_face_of[k] == face && g_text_map[k] >= lo && g_text_map[k] <= hi;
		if (!any || !GetCharWidth32W(dc, lo, hi, widths)) continue;
		for (int k = 0; k < TEXT_MAP_COUNT; k++) {
			u32 c = g_text_map[k];
			if (g_face_of[k] == face && c >= lo && c <= hi) G_ADVANCE(arrays + (k / 128) * ARRAY_BYTES + (0x80 + k % 128) * GLYPH) = (u16)widths[c - lo];
		}
	}
	for (int k = 0; k < TEXT_MAP_COUNT; k++) {
		u32 c = g_text_map[k];
		u8 *g = arrays + (k / 128) * ARRAY_BYTES + (0x80 + k % 128) * GLYPH;
		int w = -1;
		if (g_face_of[k] != face) continue;
		if ((c >= 0x3400 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff)) {
			w = han;
			if (w < 0) {
				SIZE s = { 0, 0 };
				u16 wc = (u16)c;
				w = GetTextExtentPoint32W(dc, &wc, 1, &s) ? s.cx : 0;
			}
			G_ADVANCE(g) = (u16)w;
		} else if (!((c >= 0x00a0 && c <= 0x052f) || (c >= 0x2000 && c <= 0x26ff) || (c >= 0x3000 && c <= 0x33ff) || (c >= 0xfe00 && c <= 0xffef))) {
			SIZE s = { 0, 0 };
			u16 wc = (u16)c;
			G_ADVANCE(g) = (u16)(GetTextExtentPoint32W(dc, &wc, 1, &s) ? s.cx : 0);
		}
		G_CODE(g) = (u16)((LEAD0 + k / 128) << 8 | (0x80 + k % 128));
		G_WIDTH(g) = (short)G_ADVANCE(g);
		G_HEIGHT(g) = (short)height;
		G_ORIGIN_X(g) = 0;
		G_ORIGIN_Y(g) = (short)asc;
	}
}

static Font *font_of(const u8 *font)
{
	for (int i = 0; i < g_nfonts; i++)
		if (g_font[i].font == font && FONT_ARRAYS(font)[LEAD0] == g_font[i].arrays) return &g_font[i];
	return 0;
}

/* Gives an Oni font the glyph arrays 0x80.. (big_font.c calls it for the sizes it makes) */
void text_font_add(u8 *font)
{
	Font *r = 0;
	HANDLE dc;
	int points, style, asc, desc;

	if (!font || !faces_ready() || font_of(font)) return;
	for (int a = LEAD0; a < LEAD0 + LEADS; a++)
		if (FONT_ARRAYS(font)[a]) {
			log_msg("text: a font (%d point) has its own glyph array %02x: left as it is", FONT_SIZE(font), a);
			return;
		}
	for (int i = 0; i < g_nfonts && !r; i++)
		if (!g_font[i].font) r = &g_font[i];
	if (!r) {
		if (g_nfonts == FONTS_MAX) {
			log_msg("text: more than %d fonts, the rest draw ASCII only", FONTS_MAX);
			return;
		}
		r = &g_font[g_nfonts++];
	}
	points = FONT_SIZE(font);
	style = (int)FONT_STYLE(font);
	asc = FONT_ASCENDER(font);
	desc = FONT_DESCENDER(font);
	if (points <= 0 || points > 200 || asc <= 0 || desc < 0) return;
	memset(r, 0, sizeof *r);
	r->arrays = VirtualAlloc(0, LEADS * ARRAY_BYTES, 0x3000, 4);
	if (!r->arrays || !(dc = CreateCompatibleDC(0))) {
		log_msg("text: no memory for a font's glyphs");
		return;
	}
	for (int f = 0; f < g_faces; f++) {
		HANDLE gf = gdi_font(g_face_name[f], points, style), old;
		if (!gf) continue;
		old = SelectObject(dc, gf);
		set_advances(dc, r->arrays, f, asc, asc + desc);
		SelectObject(dc, old);
		DeleteObject(gf);
	}
	DeleteDC(dc);
	r->font = font;
	for (int a = 0; a < LEADS; a++) FONT_ARRAYS(font)[LEAD0 + a] = r->arrays + a * ARRAY_BYTES;
}

/* The advance of the two-byte character at s in Oni's Tahoma of that size and style (0: none) */
int text_advance(int points, int style, const char *s)
{
	const u8 *p = (const u8 *)s;
	u32 k = (u32)(p[0] - LEAD0) * 128 + (u32)(p[1] - 0x80);
	if (p[0] < LEAD0 || p[1] < 0x80 || k >= TEXT_MAP_COUNT) return 0;
	for (int i = 0; i < g_nfonts; i++) {
		u8 *f = g_font[i].font;
		if (f && FONT_SIZE(f) == points && (int)FONT_STYLE(f) == style)
			return G_ADVANCE(g_font[i].arrays + (k / 128) * ARRAY_BYTES + (0x80 + k % 128) * GLYPH);
	}
	return 0;
}

void text_font_remove(u8 *font)
{
	Font *r = font_of(font);
	if (!r) return;
	for (int a = 0; a < LEADS; a++) FONT_ARRAYS(font)[LEAD0 + a] = 0;
	VirtualFree(r->arrays, 0, 0x8000);
	memset(r, 0, sizeof *r);
}

/* Oni's fonts: every font of the Tahoma family (level 0's TSFF), and any other TSFT loaded */
#define TAG_TSFF            0x54534646
#define TMrInstance_GetDataPtr ((u16 (ONI_FC *)(u32, const char *, void *))0x4232e0)
#define FAMILY_COUNT(y)     (*(u32 *)((y) + 0x14))
#define FAMILY_FONTS(y)     ((u8 **)((y) + 0x18))

static void fonts_scan(void)
{
	static int logged;
	u8 *family = 0;
	u32 n = TMrInstance_GetTagCount(TAG_TSFT), had = (u32)g_nfonts, in_family = 0;

	static u32 last;
	u32 now = GetTickCount();
	if (g_tsft_seen != 0xffffffffu && now - last < 1000) return;   /* a lookup by name: once a second */
	last = now;
	if (TMrInstance_GetDataPtr(TAG_TSFF, "Tahoma", &family)) family = 0;
	if (family && FAMILY_COUNT(family) <= 64) in_family = FAMILY_COUNT(family);
	if (n + in_family * 0x10000 == g_tsft_seen) return;
	g_tsft_seen = n + in_family * 0x10000;
	for (u32 i = 0; i < in_family; i++)
		if (FAMILY_FONTS(family)[i]) text_font_add(FAMILY_FONTS(family)[i]);
	for (u32 i = 0; i < n; i++) {
		u8 *f = 0;
		if (!TMrInstance_GetDataPtr_ByNumber(TAG_TSFT, i, (void **)&f) && f) text_font_add(f);
	}
	if (g_faces_ready && (g_nfonts != (int)had || !logged)) {
		log_msg("text: %d fonts draw names and chat beyond ASCII (Tahoma family: %u, fonts loaded: %u)", g_nfonts, in_family, n);
		logged = 1;
	}
}

/* ---- Pixels when Oni first draws a glyph --------------------------------------------------- */

static HANDLE g_dc, g_dib, g_dib_old;
static u8 *g_bits;
static int g_dib_w, g_dib_h;

static int dib(int w, int h)
{
	BITMAPINFO bmi = { 0 };
	if (g_dc && w <= g_dib_w && h <= g_dib_h) return 1;
	if (!g_dc && !(g_dc = CreateCompatibleDC(0))) return 0;
	if (g_dib) {
		SelectObject(g_dc, g_dib_old);
		DeleteObject(g_dib);
		g_dib = 0;
	}
	w = w > 64 ? w + 16 : 80;
	h = h > 64 ? h + 16 : 80;
	bmi.biSize = 40;
	bmi.biWidth = w;
	bmi.biHeight = -h;
	bmi.biPlanes = 1;
	bmi.biBitCount = 32;
	g_dib = CreateDIBSection(g_dc, &bmi, 0, (void **)&g_bits, 0, 0);
	if (!g_dib || !g_bits) return 0;
	g_dib_old = SelectObject(g_dc, g_dib);
	SetTextColor(g_dc, 0xffffff);
	SetBkMode(g_dc, TRANSPARENT);
	SetTextAlign(g_dc, TA_BASELINE);
	g_dib_w = w;
	g_dib_h = h;
	return 1;
}

/* glyph's pixels, if it's one of ours and not drawn yet (or wider than max_w) */
static void glyph_ready(u8 *glyph, u8 *font, int max_w)
{
	Font *r = font_of(font);
	u32 at, k;
	int lead, trail, w, h, asc;
	u8 *px;
	HANDLE gf, old;
	u16 wc;

	if (!r || glyph < r->arrays || glyph >= r->arrays + LEADS * ARRAY_BYTES) return;
	at = (u32)(glyph - r->arrays);
	lead = (int)(at / ARRAY_BYTES);
	trail = (int)(at % ARRAY_BYTES / GLYPH);
	if (trail < 0x80) return;
	k = (u32)lead * 128 + (u32)(trail - 0x80);
	if (k >= TEXT_MAP_COUNT) return;
	w = G_ADVANCE(glyph);
	if (max_w > 0 && w > max_w) w = max_w;
	if ((r->made[k >> 3] >> (k & 7) & 1) && G_WIDTH(glyph) <= w) return;
	asc = FONT_ASCENDER(font);
	h = asc + FONT_DESCENDER(font);
	r->made[k >> 3] |= (u8)(1 << (k & 7));
	G_WIDTH(glyph) = (short)w;
	if (w <= 0 || h <= 0 || !dib(w + 8, h)) {
		G_WIDTH(glyph) = 0;
		return;
	}
	px = pool_get((u32)(w * h), (u32)font);
	if (!px) {
		G_WIDTH(glyph) = 0;
		return;
	}
	gf = gdi_font(g_face_name[g_face_of[k]], FONT_SIZE(font), (int)FONT_STYLE(font));
	old = gf ? SelectObject(g_dc, gf) : 0;
	memset(g_bits, 0, (u32)(g_dib_w * g_dib_h * 4));
	GdiFlush();
	wc = g_text_map[k];
	TextOutW(g_dc, 0, asc, &wc, 1);
	GdiFlush();
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const u8 *p = g_bits + (y * g_dib_w + x) * 4;
			u8 c = p[0] > p[1] ? p[0] : p[1];
			if (p[2] > c) c = p[2];
			px[y * w + x] = (u8)(255 - c);
		}
	if (gf) {
		SelectObject(g_dc, old);
		DeleteObject(gf);
	}
	G_HEIGHT(glyph) = (short)h;
	G_ORIGIN_X(glyph) = 0;
	G_ORIGIN_Y(glyph) = (short)asc;
	G_OFFSET(glyph) = (u32)(px - (font + FONT_HEAD)) / 4;   /* wraps around: Oni adds it back */
}

static void ONI_FC glyph_to_cell(u8 *glyph, u8 *font, void *priv, u32 x, u32 y)
{
	glyph_ready(glyph, font, priv ? (int)*(u32 *)priv : 0);   /* the cell's width */
	TSiGlyph_DrawToCell(glyph, font, priv, x, y);
}

static void ONI_FC glyph_draw(u8 *glyph, u8 *font, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	glyph_ready(glyph, font, 0);
	TSiGlyph_Draw(glyph, font, a, b, c, d, e);
}

/* ---- Daodan's single-byte reading ------------------------------------------------------------ *
 * AE's Daodan (binkw32.dll, option language.nomultibyte, on by default) turns the 16 double-byte
 * tests of Oni's text code (the drawers 0x42d8ed and 0x42dac0, the formatter 0x42e210, the
 * measurer 0x42e920) from `je` into `jmp`: every byte is a character of its own, so the localized
 * games' Windows-1252 text (German, French, Spanish...) keeps its accents. Arklite's text needs
 * the pairs, so while Arklite draws its own (mp_hud.c's HUD, the F1 board, Arklite's menu dialogs)
 * the tests are Oni's again (text_multibyte_begin/end, nested), and Daodan's after. Without
 * Daodan's patch (no AE, or the option off) Oni reads pairs everywhere and nothing is switched. */
static const u32 g_mb_site[16] = { 0x42d8f8, 0x42d9ad, 0x42dbe2, 0x42dec3, 0x42e2ab, 0x42e2c4, 0x42e379, 0x42e48c,
                                   0x42e4d0, 0x42e4f4, 0x42e646, 0x42e695, 0x42e944, 0x42e95d, 0x42e98e, 0x42e9dc };
static int g_mb_state = -1;   /* -1 not looked yet, 0 nothing to switch, 1 Daodan's patch is in */
static int g_mb_depth;

static void multibyte_detect(void)
{
	int je = 0, jmp = 0;
	u32 old;
	if (g_mb_state >= 0) return;
	for (int i = 0; i < 16; i++) {
		u8 b = *(const u8 *)g_mb_site[i];
		je += b == 0x74;
		jmp += b == 0xeb;
	}
	if (jmp == 16) {
		VirtualProtect((void *)0x42d000, 0x2000, PAGE_EXECUTE_READWRITE, &old);   /* written often: once */
		log_msg("text: Daodan reads Oni's text a byte at a time (language.nomultibyte): Arklite's own reads pairs");
		g_mb_state = 1;
	} else {
		if (je != 16) log_msg("text: Oni's double-byte tests aren't as expected (%d je, %d jmp): left as they are", je, jmp);
		g_mb_state = 0;
	}
}

static void multibyte_set(u8 b)
{
	for (int i = 0; i < 16; i++) *(u8 *)g_mb_site[i] = b;
	FlushInstructionCache(GetCurrentProcess(), (void *)0x42d8f8, 0x42e9dc - 0x42d8f8 + 1);
}

void text_multibyte_begin(void)
{
	multibyte_detect();
	if (g_mb_state == 1 && g_mb_depth++ == 0) multibyte_set(0x74);
}

void text_multibyte_end(void)
{
	if (g_mb_state == 1 && g_mb_depth > 0 && --g_mb_depth == 0) multibyte_set(0xeb);
}

/* ---- Oni's copies of n characters ------------------------------------------------------------ *
 * Oni copies a line of n characters with 0x426620 (ECX to, EDX from, [size], [n]; ret 8), which
 * copies n BYTES: the formatter's line breaking (0x42e55b, a segment of the 0x100-byte buffers the
 * drawer then reads n characters from) and a list box's selected text (0x46da02, n from
 * TSrString_GetLength 0x42f090). With two-byte characters the copy came out short and the drawer
 * read past its end: the NUL and stale bytes became glyphs, which took cache cells from letters
 * on screen (a name's last letters went blank, typed accents showed as other letters) and the
 * line's end was lost. Oni only ever shipped single-byte languages. These copy n characters, read
 * the way the caller counted them: the formatter as Daodan's switch has it now (its test at
 * 0x42e2ab), the list box always in pairs (Daodan leaves the string functions alone). */
#define UUrString_CopyN     ((void (ONI_FC *)(char *, const char *, u32, u32))0x426620)
#define TScSegmentCopyCall  0x42e55b
#define WMcListTextCopyCall 0x46da02

static void copy_chars(char *d, const char *s, u32 size, u32 chars, int pairs)
{
	u32 at = 0;
	for (; chars && s[at]; chars--) {
		u32 n = pairs && (u8)s[at] >= 0x80 && (u8)s[at] != 0xff && s[at + 1] ? 2 : 1;
		if (at + n >= size) break;   /* room for the NUL; never half a character */
		d[at] = s[at];
		if (n == 2) d[at + 1] = s[at + 1];
		at += n;
	}
	if (size) d[at] = 0;
}

static void ONI_FC segment_copy(char *d, const char *s, u32 size, u32 chars)
{
	copy_chars(d, s, size, chars, *(const u8 *)0x42e2ab != 0xeb);
}

static void ONI_FC list_text_copy(char *d, const char *s, u32 size, u32 chars)
{
	copy_chars(d, s, size, chars, 1);
}

/* ---- Typing -------------------------------------------------------------------------------- */

static WNDPROC g_old_proc;
static void *g_hwnd, *g_himc;
static int g_ime_on = -1, g_target;
static char g_comp[CHAT_MAX];
static int g_spot[3] = { -1, -1, -1 };   /* text_ime_spot's last place (sent again after the IME is switched) */
static int g_comp_cursor;

const char *text_composition(void) { return g_target == TEXT_TARGET_CHAT ? g_comp : ""; }

static int ime_ready(void)
{
	static int tried;
	HANDLE imm;
	if (tried) return ImmAssociateContext != 0;
	tried = 1;
	if (!(imm = LoadLibraryA("imm32.dll"))) return 0;
	ImmGetContext = GetProcAddress(imm, "ImmGetContext");
	ImmReleaseContext = GetProcAddress(imm, "ImmReleaseContext");
	ImmGetCompositionStringW = GetProcAddress(imm, "ImmGetCompositionStringW");
	ImmSetCompositionWindow = GetProcAddress(imm, "ImmSetCompositionWindow");
	ImmSetCandidateWindow = GetProcAddress(imm, "ImmSetCandidateWindow");
	ImmNotifyIME = GetProcAddress(imm, "ImmNotifyIME");
	ImmAssociateContext = GetProcAddress(imm, "ImmAssociateContext");
	if (!ImmGetContext || !ImmReleaseContext || !ImmGetCompositionStringW || !ImmSetCompositionWindow ||
	    !ImmSetCandidateWindow || !ImmNotifyIME)
		ImmAssociateContext = 0;
	if (!ImmAssociateContext) log_msg("text: no IME functions (imm32.dll)");
	return ImmAssociateContext != 0;
}

/* The IME's text as ours; n: its UTF-16 characters */
static void comp_set(const u16 *w, int n, int cursor)
{
	int at = 0;
	char b[2];
	g_comp_cursor = 0;
	for (int i = 0; i < n; i++) {
		int k = text_put(b, w[i]);
		if (!k || at + k >= (int)sizeof g_comp) continue;
		memcpy(g_comp + at, b, (unsigned)k);
		at += k;
		if (i + 1 == cursor) g_comp_cursor = at;
	}
	g_comp[at] = 0;
	if (cursor <= 0) g_comp_cursor = 0;
	if (cursor >= n) g_comp_cursor = at;
}

static long WINAPI window_proc(void *hwnd, u32 msg, u32 wp, long lp)
{
	if (g_target) {
		switch (msg) {
		case WM_IME_STARTCOMPOSITION:
			g_comp[0] = 0;
			if (g_target == TEXT_TARGET_CHAT) return 0;   /* drawn in the chat's field: not the IME's window */
			break;
		case WM_IME_ENDCOMPOSITION:
			g_comp[0] = 0;
			if (g_target == TEXT_TARGET_CHAT) return 0;
			break;
		case WM_IME_COMPOSITION: {
			void *imc = ImmGetContext ? ImmGetContext(hwnd) : 0;
			u16 w[CHAT_MAX];
			if (imc && (lp & GCS_RESULTSTR)) {
				long bytes = ImmGetCompositionStringW(imc, GCS_RESULTSTR, w, sizeof w);
				int n = bytes > 0 ? (int)(bytes / 2) : 0;
				g_comp[0] = 0;
				for (int i = 0; i < n; i++) SendMessageW(hwnd, WM_CHAR, w[i], 1);
			}
			if (imc && (lp & GCS_COMPSTR)) {
				long bytes = ImmGetCompositionStringW(imc, GCS_COMPSTR, w, sizeof w);
				long cur = (lp & GCS_CURSORPOS) ? ImmGetCompositionStringW(imc, GCS_CURSORPOS, 0, 0) : bytes / 2;
				comp_set(w, bytes > 0 ? (int)(bytes / 2) : 0, (int)cur);
			}
			if (imc) ImmReleaseContext(hwnd, imc);
			if (g_target == TEXT_TARGET_CHAT) return 0;
			lp &= ~GCS_RESULTSTR;   /* the IME's window still updates; its result was ours */
			break;
		}
		case WM_IME_CHAR:   /* one finished character (some IMEs) */
			SendMessageW(hwnd, WM_CHAR, wp, 1);
			return 0;
		case WM_CHAR:
			if (g_target == TEXT_TARGET_FIELD && wp >= 0x80 && ui_field_type(wp)) return 0;
			break;
		}
	}
	return CallWindowProcW(g_old_proc, hwnd, msg, wp, lp);
}

/* The IME on only while something takes text; its windows near the chat's field */
static void ime_frame(void)
{
	int on = g_target != 0;
	if (!g_hwnd || !ime_ready()) return;
	if (on == g_ime_on) return;
	if (on) {
		ImmAssociateContext(g_hwnd, g_himc);
	} else {
		void *imc = ImmGetContext(g_hwnd);
		if (imc) {
			ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);   /* half-typed text goes */
			ImmReleaseContext(g_hwnd, imc);
		}
		void *was = ImmAssociateContext(g_hwnd, 0);
		if (g_ime_on < 0 || was) g_himc = was;
		g_comp[0] = 0;
	}
	g_ime_on = on;
	g_spot[0] = g_spot[1] = g_spot[2] = -1;
}

void text_ime_spot(int x, int y, int h, int screen_w, int screen_h)
{
	int rc[4] = { 0 };
	void *imc;
	int *was = g_spot;
	if (!g_hwnd || !ime_ready() || g_ime_on != 1) return;
	if (x == was[0] && y == was[1] && h == was[2]) return;   /* each set is a message to the window: only on a change */
	if (!(imc = ImmGetContext(g_hwnd))) return;
	was[0] = x, was[1] = y, was[2] = h;
	if (GetClientRect(g_hwnd, rc) && screen_w > 0 && screen_h > 0 && rc[2] > 0 && rc[3] > 0) {
		x = x * rc[2] / screen_w;
		h = h * rc[3] / screen_h;
		y = y * rc[3] / screen_h;
	}
	COMPOSITIONFORM cf = { CFS_POINT, x, y, 0, 0, 0, 0 };
	CANDIDATEFORM cand = { 0, CFS_EXCLUDE, x, y + h, x, y, x + 1, y + h };
	ImmSetCompositionWindow(imc, &cf);
	ImmSetCandidateWindow(imc, &cand);
	ImmReleaseContext(g_hwnd, imc);
}

/* Every frame (live_menu.c, with chat_frame) */
void text_frame(void)
{
	void *w = ONgGameWindow;

	fonts_scan();
	if (w && !g_old_proc && !server_running()) {
		g_hwnd = w;
		g_old_proc = (WNDPROC)SetWindowLongW(w, GWL_WNDPROC, (long)window_proc);
		if (!g_old_proc) log_msg("text: can't read typed text (no window procedure)");
	}
	multibyte_detect();
	g_target = chat_typing() ? TEXT_TARGET_CHAT : mp_name_editing() ? TEXT_TARGET_NAME : ui_field_focused() ? TEXT_TARGET_FIELD : 0;
	if (!g_target) g_comp[0] = 0;
	ime_frame();
}

/* A module's message loop imports (PeekMessageA, GetMessageA, DispatchMessageA from user32): the
 * Unicode ones, all or none (a W message dispatched as an A one would garble its character). Only a
 * module that dispatches (Oni.exe; Daodan, AE's binkw32.dll, which runs the loop in its own
 * DDiPlatform_InputEvent_GetEvent); Bink's PeekMessageA stays. Returns the slots changed. */
static int ieq(const char *a, const char *b)
{
	while (*a && to_upper(*a) == to_upper(*b)) a++, b++;
	return to_upper(*a) == to_upper(*b);
}

static int imports_swap(u8 *base, const char *who)
{
	static const char *const was[3] = { "PeekMessageA", "GetMessageA", "DispatchMessageA" };
	static const char *const now[3] = { "PeekMessageW", "GetMessageW", "DispatchMessageW" };
	HANDLE user32 = GetModuleHandleA("user32.dll");
	void **slot[3] = { 0, 0, 0 }, *to[3];
	u8 *nt;
	u32 rva;

	if (!base || !user32 || base[0] != 'M' || base[1] != 'Z') return 0;
	nt = base + *(u32 *)(base + 0x3c);
	if (*(u32 *)nt != 0x4550 || !(rva = *(u32 *)(nt + 0x80))) return 0;   /* PE32: the import directory */
	for (u32 *d = (u32 *)(base + rva); d[3]; d += 5) {   /* OriginalFirstThunk, .., .., Name, FirstThunk */
		u32 *names = (u32 *)(base + (d[0] ? d[0] : d[4])), *iat = (u32 *)(base + d[4]);
		if (!ieq((const char *)(base + d[3]), "user32.dll")) continue;
		for (int i = 0; names[i]; i++) {
			if (names[i] & 0x80000000u) continue;   /* by ordinal */
			for (int k = 0; k < 3; k++)
				if (ieq((const char *)(base + names[i] + 2), was[k])) slot[k] = (void **)&iat[i];
		}
	}
	if (!slot[2] || (!slot[0] && !slot[1])) return 0;
	for (int k = 0; k < 3; k++) {
		void *a = GetProcAddress(user32, was[k]);
		to[k] = GetProcAddress(user32, now[k]);
		if (slot[k] && (!a || !to[k] || *slot[k] != a)) {
			log_msg("text: %s's %s import was changed by someone else: left as it is", who, was[k]);
			return -1;
		}
	}
	for (int k = 0; k < 3; k++)
		if (slot[k]) patch_write((u32)slot[k], &to[k], 4);
	log_msg("text: %s's message loop reads Unicode (%s%s%s)", who, slot[0] ? "PeekMessage " : "", slot[1] ? "GetMessage " : "",
	        "DispatchMessage");
	return 1;
}

void text_install(void)
{
	int ok = 1;
	map_ready();
	if (!patch_call(TScGlyphToCellCall, 0x42cb80, (u32)glyph_to_cell) || !patch_call(TScGlyphDrawCall, 0x42cc40, (u32)glyph_draw) ||
	    !patch_call(TScSegmentCopyCall, 0x426620, (u32)segment_copy) || !patch_call(WMcListTextCopyCall, 0x426620, (u32)list_text_copy)) {
		log_msg("text: Oni's text code isn't as expected: names and chat draw ASCII only");
		ok = 0;
	}
	{
		int oni = imports_swap((u8 *)GetModuleHandleA(0), "Oni.exe"), daodan = imports_swap((u8 *)GetModuleHandleA("binkw32.dll"), "binkw32.dll");
		if (oni < 0 || daodan < 0 || (!oni && !daodan)) {
			log_msg("text: typing stays in the Windows code page");
			ok = 0;
		}
	}
	if (ok) log_msg("text: names and chat take any of %d characters beyond ASCII", TEXT_MAP_COUNT);
}
