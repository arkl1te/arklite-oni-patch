/* Tahoma sizes Oni doesn't have, made at runtime: Arklite's text (mp_hud.c) grows with the screen
 * in real font sizes, never by stretching a smaller size's pictures.
 *
 * Oni's one family, TSFFTahoma, has 7, 9, 10, 12 and 14 point, each plain (0), bold (1) and italic
 * (2). A font (TSFT, OniFoxed BFW_TextSystem.h) is:
 *   +0x6 size (u16), +0x8 style (u32), +0xc ascender, +0xe descender, +0x10 leading, +0x12
 *   leading width (i16 each), +0x14 256 glyph arrays (TSGA: 256 glyphs of 0x14 bytes: code,
 *   advance, width, height, origin x, origin y (u16/i16), offset of its bitmap in longs (u32),
 *   cell (u32, runtime)), +0x414 the count of longs, +0x418 the longs: one byte a pixel, 255 empty,
 *   0 full ink.
 * Every stock glyph is as wide as its advance and as tall as ascender + descender, origin (0,
 * ascender): a box the pen walks over. Bungie's are Windows Tahoma at 96 dpi (14 point: a 19 px em,
 * ascender 19, descender 4, 'A' 13 px), so the sizes here are made the same way: GDI draws each
 * character of Windows-1252 (Oni's text is bytes) in Tahoma or Tahoma Bold, grayscale antialiased,
 * at a -(points * 96 / 72) px em. Windows has Tahoma, and Wine ships its own; when the face found
 * isn't Tahoma, nothing is made and the text keeps Oni's sizes (log line "big font").
 *
 * A family picks a font by exact size and style (0x42ed50 -> GetBestFont: the size asked, then
 * smaller ones, plain if the style is missing). mp_hud.c puts a copy of the context's family
 * with the made fonts appended on the context while it draws (big_font_family) and puts the
 * original back after, so nothing else ever sees them.
 *
 * Drawing (0x42dac0, the context's string drawer): each glyph is a sprite from a cell of the
 * font's texture, found through the font's template private data (TMrTemplate_PrivateData_GetDataPtr
 * 0x4242c0 with the font private data descriptor *0x6b5444, called at 0x42dae4 and 0x42dc43, the
 * only two reads): {cell width, cell height, cells, cells a row, rows, u ratio, v ratio, the
 * texture (ARGB4444, the cache's format), the cells (0x18 bytes: glyph, last use, u0 v0 u1 v1)}.
 * A glyph without a cell (+0x10 zero) takes the first empty cell or the least recently used one
 * and is drawn into it from its bitmap (0x42cb80/0x42cc40) before the sprite. Our fonts aren't
 * template instances, so those two calls come here first and get our own private data. Every
 * glyph gets its cell and its pixels when the font is made, so nothing is drawn into the texture
 * while playing (each such draw re-uploads the texture); only a font whose glyphs don't fit 1024 x
 * 1024 leaves the rest to Oni's own path. Pixels are written as Oni's drawer writes them into a
 * cleared cell: every 4-bit channel (coverage * 15) >> 8 (0x42cc40, case 0).
 *
 * Each font has SPARE empty cells more than its own 256 glyphs need, for the characters beyond
 * ASCII text.c gives it (glyph arrays 0x80..0xfe, drawn into a cell when first used).
 *
 * The textures are Oni TXMPs (8x8, the dynamic file kept until exit, like char_menu.c's) with
 * pixels of our own memory. A rewind must never see one made, so big_font_prepare makes them all
 * before a networked match starts (char_menu.c hud_textures_prepare), keeps them out of rollback's
 * restores, and a size change later reuses them. */
#include "arklite.h"

__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

/* ---- GDI ----------------------------------------------------------------------------------- */

typedef struct {
	u32 biSize; int biWidth, biHeight; u16 biPlanes, biBitCount;
	u32 biCompression, biSizeImage; int biXPelsPerMeter, biYPelsPerMeter; u32 biClrUsed, biClrImportant;
	u32 bmiColors[1];
} BITMAPINFO;

typedef struct {
	int tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth,
	    tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
	u16 tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
	u8  tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
	u8  pad[3];
} TEXTMETRICW;

typedef struct { int cx, cy; } SIZE;

__declspec(dllimport) HANDLE WINAPI CreateCompatibleDC(HANDLE);
__declspec(dllimport) int    WINAPI DeleteDC(HANDLE);
__declspec(dllimport) HANDLE WINAPI CreateDIBSection(HANDLE, const BITMAPINFO *, u32, void **, HANDLE, u32);
__declspec(dllimport) HANDLE WINAPI SelectObject(HANDLE, HANDLE);
__declspec(dllimport) int    WINAPI DeleteObject(HANDLE);
__declspec(dllimport) HANDLE WINAPI CreateFontW(int, int, int, int, int, u32, u32, u32, u32, u32, u32, u32, u32, const u16 *);
__declspec(dllimport) u32    WINAPI SetTextColor(HANDLE, u32);
__declspec(dllimport) int    WINAPI SetBkMode(HANDLE, int);
__declspec(dllimport) int    WINAPI TextOutW(HANDLE, int, int, const u16 *, int);
__declspec(dllimport) int    WINAPI GetTextMetricsW(HANDLE, TEXTMETRICW *);
__declspec(dllimport) int    WINAPI GetTextExtentPoint32W(HANDLE, const u16 *, int, SIZE *);
__declspec(dllimport) int    WINAPI GetTextFaceW(HANDLE, int, u16 *);
__declspec(dllimport) int    WINAPI GdiFlush(void);

#define FW_NORMAL           400
#define FW_BOLD             700
#define DEFAULT_CHARSET     1
#define OUT_TT_ONLY_PRECIS  7
#define ANTIALIASED_QUALITY 4
#define FF_SWISS_VARIABLE   0x22
#define TRANSPARENT         1

/* ---- Oni ----------------------------------------------------------------------------------- */

#define TMrTemplate_PrivateData_GetDataPtr ((void *(ONI_FC *)(void *, void *))0x4242c0)
#define TScPrivateData_Call1 0x42dae4
#define TScPrivateData_Call2 0x42dc43
#define M3rTextureMap_New ((u16 (ONI_FC *)(u32, u32, u32, u32, u32, const char *, u8 **))0x41eb00)
#define PIXEL_ARGB4444    0
#define TXMP_W(t)         (*(u16 *)((t) + 0x84))
#define TXMP_H(t)         (*(u16 *)((t) + 0x86))
#define TXMP_TEXELS(t)    (*(u8 **)((t) + 0x94))
#define TXMP_DIRTY(t)     (*(u8 *)((t) + 0xa0))   /* uploaded again when next drawn */

#define FONT_SIZE(f)      (*(u16 *)((f) + 0x6))
#define FONT_STYLE(f)     (*(u32 *)((f) + 0x8))
#define FONT_ASCENDER(f)  (*(short *)((f) + 0xc))
#define FONT_DESCENDER(f) (*(short *)((f) + 0xe))
#define FONT_LEADING(f)   (*(short *)((f) + 0x10))
#define FONT_INSET(f)     (*(short *)((f) + 0x12))
#define FONT_ARRAYS(f)    ((u8 **)((f) + 0x14))
#define FONT_NLONGS(f)    (*(u32 *)((f) + 0x414))
#define FONT_HEAD         0x418                   /* the longs follow */
#define FAMILY_COUNT(y)   (*(u32 *)((y) + 0x14))
#define FAMILY_FONTS(y)   ((u8 **)((y) + 0x18))
#define INSTANCE_HEADER   8                       /* before every instance: its index and file */

typedef struct {
	u8   *glyph;
	u32  time;
	float u0, v0, u1, v1;
} Cell;   /* 0x18 bytes, as Oni's */

typedef struct {
	u32   cell_w, cell_h, cells, cols, rows;
	float u_ratio, v_ratio;
	u8    *texture;
	Cell  *cell;
} Private;   /* TStFont_PrivateData */

#define SLOTS     3        /* the size's plain and bold, the big size's bold ("FIGHT!") */
#define SPARE     256      /* empty cells for characters beyond ASCII (text.c), drawn when first used */
#define MAX_SIDE  1024     /* Arklite's upload limit (texture_clamp.c) */

typedef struct {
	int     points, style;   /* points 0: empty */
	int     failed_points, failed_style;   /* not tried again every frame */
	u8      *block;          /* header, font, longs, glyph array, cells */
	u8      *font;
	u8      *texels;
	u8      *texture;        /* the TXMP, made once */
	u8      *oni_texels;     /* ...its own 8x8 pixels, while it has no font */
	Private priv;
} Slot;

static Slot g_slot[SLOTS];
static u8  *g_family, *g_family_of;   /* our copy, and the family it copies */
static int  g_family_points[SLOTS], g_family_styles[SLOTS];
static int  g_failed;                 /* no Tahoma: Oni's sizes only */

static int round_i(float v) { return (int)(v + 0.5f); }

/* ---- The two private data reads ------------------------------------------------------------ */

static void *ONI_FC font_private(void *descriptor, void *instance)
{
	for (int i = 0; i < SLOTS; i++)
		if (g_slot[i].points && instance == g_slot[i].font) return &g_slot[i].priv;
	return TMrTemplate_PrivateData_GetDataPtr(descriptor, instance);
}

/* ---- Making a font ------------------------------------------------------------------------- */

/* Windows-1252 0x80..0x9f; 0 where 1252 has no character */
static const u16 g_cp1252[32] = {
	0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017d, 0,
	0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0, 0x017e, 0x0178,
};

static u16 to_unicode(int code)
{
	if (code < 32 || code == 127) return 0;
	if (code >= 0x80 && code < 0xa0) return g_cp1252[code - 0x80];
	return (u16)code;
}

static int is_tahoma(HANDLE dc)
{
	static const u16 want[] = { 'T', 'a', 'h', 'o', 'm', 'a', 0 };
	u16 face[32] = { 0 };
	int i;

	if (!GetTextFaceW(dc, 32, face)) return 0;
	for (i = 0; want[i]; i++)
		if (face[i] != want[i]) return 0;
	return !face[i];
}

static HANDLE tahoma(int points, int style)
{
	static const u16 name[] = { 'T', 'a', 'h', 'o', 'm', 'a', 0 };
	return CreateFontW(-round_i(points * 96.0f / 72.0f), 0, 0, 0, style == 1 ? FW_BOLD : FW_NORMAL, style == 2, 0, 0,
	                   DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, 0, ANTIALIASED_QUALITY, FF_SWISS_VARIABLE, name);
}

/* The slot's font gone: its texture back on its own 8x8 pixels before ours are freed */
static void release(Slot *s)
{
	if (s->texture && s->texels && TXMP_TEXELS(s->texture) == s->texels) {
		TXMP_W(s->texture) = 8;
		TXMP_H(s->texture) = 8;
		TXMP_TEXELS(s->texture) = s->oni_texels;
		TXMP_DIRTY(s->texture) = 1;
	}
	if (s->font) text_font_remove(s->font);
	if (s->block) VirtualFree(s->block, 0, 0x8000);
	if (s->texels) VirtualFree(s->texels, 0, 0x8000);
	s->block = s->font = s->texels = 0;
	s->points = 0;
	memset(&s->priv, 0, sizeof s->priv);
}

static int pow2(int v) { int p = 8; while (p < v) p <<= 1; return p; }

/* Makes the slot's TXMP (once): 8x8 of Oni's, its pixels replaced by ours when a font is made */
static int texture_make(Slot *s, int index)
{
	char name[32];

	if (s->texture) return 1;
	fmt(name, "arklite_font_%d", index);
	if (M3rTextureMap_New(8, 8, PIXEL_ARGB4444, 1, 0, name, &s->texture) || !s->texture) {
		log_msg("big font: can't make texture %s", name);
		s->texture = 0;
		return 0;
	}
	rollback_keep(s->texture - 8, 8 + 0xa4);
	s->oni_texels = TXMP_TEXELS(s->texture);
	return 1;
}

/* Once: GDI's Tahoma 14 against Bungie's (advances of ASCII, ascender, descender), to the log */
static void check_14(const u8 *oni14, int style)
{
	static int done;
	HANDLE dc, font, old;
	TEXTMETRICW tm;
	const u8 *array;
	int differ = 0;

	if (done || !oni14 || !(array = FONT_ARRAYS(oni14)[0])) return;
	done = 1;
	if (!(dc = CreateCompatibleDC(0))) return;
	if ((font = tahoma(14, style))) {
		old = SelectObject(dc, font);
		GetTextMetricsW(dc, &tm);
		for (int code = 32; code < 127; code++) {
			u16 wc = (u16)code;
			SIZE size = { 0, 0 };
			GetTextExtentPoint32W(dc, &wc, 1, &size);
			differ += *(const u16 *)(array + code * 0x14 + 2) != (u16)size.cx;
		}
		log_msg("big font: Tahoma 14 %s from GDI against Oni's: %d of 95 advances differ, ascender %d/%d, descender %d/%d",
		        style == 1 ? "bold" : "plain", differ, tm.tmAscent, FONT_ASCENDER(oni14), tm.tmDescent, FONT_DESCENDER(oni14));
		SelectObject(dc, old);
		DeleteObject(font);
	}
	DeleteDC(dc);
}

/* Builds Tahoma points in style into slot s; header: an instance header to copy (one of the
 * family's) */
static int make_font(Slot *s, int index, int points, int style, const u8 *header)
{
	HANDLE dc = 0, font = 0, bitmap = 0, old_font = 0, old_bitmap = 0;
	TEXTMETRICW tm;
	u8 adv[256] = { 0 };
	u8 *bits = 0;
	int ok = 0, asc, desc, ch, cw = 1, dib_w, n = 0, side_w = 0, side_h = 0, cols = 0, rows = 0;
	u32 longs = 0;

	release(s);
	if (!texture_make(s, index)) return 0;
	dc = CreateCompatibleDC(0);
	font = dc ? tahoma(points, style) : 0;
	if (!font) { log_msg("big font: GDI can't make Tahoma %d", points); goto done; }
	old_font = SelectObject(dc, font);
	if (!is_tahoma(dc)) { log_msg("big font: no Tahoma here, the text keeps Oni's sizes"); g_failed = 1; goto done; }
	GetTextMetricsW(dc, &tm);
	asc = tm.tmAscent;
	desc = tm.tmDescent;
	ch = asc + desc;
	for (int code = 0; code < 256; code++) {
		u16 wc = to_unicode(code);
		SIZE size = { 0, 0 };
		if (!wc || !GetTextExtentPoint32W(dc, &wc, 1, &size) || size.cx <= 0) continue;
		adv[code] = (u8)(size.cx < 255 ? size.cx : 255);
		if (adv[code] > cw) cw = adv[code];
		longs += ((u32)adv[code] * (u32)ch + 3) / 4;
		n++;
	}
	if (!n || ch <= 0 || ch > MAX_SIDE || cw > MAX_SIDE) goto done;

	/* the texture: the smallest of 256, 512, 1024 wide that holds every glyph and SPARE more, else
	 * 1024 x 1024 */
	for (side_w = 256; side_w <= MAX_SIDE; side_w <<= 1) {
		cols = side_w / cw;
		if (!cols) continue;
		rows = (n + SPARE + cols - 1) / cols;
		side_h = pow2(rows * ch);
		if (side_h <= side_w) break;
	}
	if (side_w > MAX_SIDE) {
		side_w = side_h = MAX_SIDE;
		cols = side_w / cw;
		rows = side_h / ch;
	}

	/* one block: instance header, font and its longs, the glyph array, the cells */
	u32 font_bytes = INSTANCE_HEADER + FONT_HEAD + longs * 4, array_at = (font_bytes + 15) & ~15u;
	u32 cells_at = array_at + 256 * 0x14, total = cells_at + (u32)(cols * rows) * sizeof(Cell);
	s->block = VirtualAlloc(0, total, 0x3000, 4);
	s->texels = VirtualAlloc(0, (u32)(side_w * side_h * 2), 0x3000, 4);
	if (!s->block || !s->texels) { log_msg("big font: no memory for Tahoma %d", points); goto done; }
	memcpy(s->block, header, INSTANCE_HEADER);
	s->font = s->block + INSTANCE_HEADER;
	FONT_SIZE(s->font) = (u16)points;
	FONT_STYLE(s->font) = (u32)style;
	FONT_ASCENDER(s->font) = (short)asc;
	FONT_DESCENDER(s->font) = (short)desc;
	FONT_LEADING(s->font) = (short)round_i(points * 3 / 14.0f);   /* 14 point: 3 and 4 */
	FONT_INSET(s->font) = (short)round_i(points * 4 / 14.0f);
	FONT_ARRAYS(s->font)[0] = s->block + array_at;
	FONT_NLONGS(s->font) = longs;
	s->priv.cell_w = (u32)cw;
	s->priv.cell_h = (u32)ch;
	s->priv.cols = (u32)cols;
	s->priv.rows = (u32)rows;
	s->priv.cells = (u32)(cols * rows);
	s->priv.u_ratio = 1.0f / (float)side_w;
	s->priv.v_ratio = 1.0f / (float)side_h;
	s->priv.texture = s->texture;
	s->priv.cell = (Cell *)(s->block + cells_at);

	/* a DIB to draw each character in: white on black, top-down */
	BITMAPINFO bmi = { 0 };
	dib_w = cw + 8;
	bmi.biSize = 40;
	bmi.biWidth = dib_w;
	bmi.biHeight = -ch;
	bmi.biPlanes = 1;
	bmi.biBitCount = 32;
	bitmap = CreateDIBSection(dc, &bmi, 0, (void **)&bits, 0, 0);
	if (!bitmap || !bits) { log_msg("big font: GDI can't make a bitmap"); goto done; }
	old_bitmap = SelectObject(dc, bitmap);
	SetTextColor(dc, 0xffffff);
	SetBkMode(dc, TRANSPARENT);

	/* glyphs in the order they get cells: ASCII, then the rest */
	u8 *array = FONT_ARRAYS(s->font)[0], *data = s->font + FONT_HEAD;
	u32 offset = 0;
	int next_cell = 0;
	for (int pass = 0; pass < 2; pass++)
		for (int code = 0; code < 256; code++) {
			u8 *g = array + code * 0x14;
			u16 wc = to_unicode(code);
			int w = adv[code];

			if ((code >= 32 && code < 127) != !pass) continue;
			*(u16 *)(g + 0) = (u16)code;
			if (!w) continue;   /* nothing to draw: advance 0, no cell, never drawn by our text */
			*(u16 *)(g + 2) = (u16)w;
			*(short *)(g + 4) = (short)w;
			*(short *)(g + 6) = (short)ch;
			*(short *)(g + 8) = 0;
			*(short *)(g + 10) = (short)asc;
			*(u32 *)(g + 12) = offset;
			memset(bits, 0, (u32)(dib_w * ch * 4));
			GdiFlush();
			TextOutW(dc, 0, 0, &wc, 1);
			GdiFlush();
			u8 *px = data + offset * 4;
			for (int y = 0; y < ch; y++)
				for (int x = 0; x < w; x++) {
					const u8 *p = bits + (y * dib_w + x) * 4;
					u8 c = p[0] > p[1] ? p[0] : p[1];
					if (p[2] > c) c = p[2];
					px[y * w + x] = (u8)(255 - c);
				}
			offset += ((u32)w * (u32)ch + 3) / 4;
			if (next_cell < (int)s->priv.cells) {   /* its cell, drawn as 0x42cc40 would */
				Cell *cell = &s->priv.cell[next_cell];
				int cx = cw * (next_cell % cols), cy = ch * (next_cell / cols);
				for (int y = 0; y < ch; y++)
					for (int x = 0; x < w; x++) {
						u32 v = ((255u - px[y * w + x]) * 15u) >> 8;
						((u16 *)s->texels)[(cy + y) * side_w + cx + x] = (u16)(v << 12 | v << 8 | v << 4 | v);
					}
				cell->glyph = g;
				cell->time = 0;
				cell->u0 = (float)cx * s->priv.u_ratio;
				cell->v0 = (float)cy * s->priv.v_ratio;
				cell->u1 = (float)(cx + w) * s->priv.u_ratio;
				cell->v1 = (float)(cy + ch) * s->priv.v_ratio;
				*(u32 *)(g + 16) = (u32)cell;
				next_cell++;
			}
		}
	TXMP_W(s->texture) = (u16)side_w;
	TXMP_H(s->texture) = (u16)side_h;
	TXMP_TEXELS(s->texture) = s->texels;
	TXMP_DIRTY(s->texture) = 1;
	s->points = points;
	s->style = style;
	ok = 1;
	text_font_add(s->font);   /* names and chat beyond ASCII: their glyphs (text.c) */
	log_msg("big font: Tahoma %d %s made: ascender %d, descender %d, %d glyphs, %d cells of %dx%d in %dx%d",
	        points, style == 1 ? "bold" : style == 2 ? "italic" : "plain", asc, desc, n, cols * rows, cw, ch, side_w, side_h);
done:
	if (old_bitmap) SelectObject(dc, old_bitmap);
	if (bitmap) DeleteObject(bitmap);
	if (old_font) SelectObject(dc, old_font);
	if (font) DeleteObject(font);
	if (dc) DeleteDC(dc);
	if (!ok) {
		release(s);   /* the texture still had its own pixels: they're set last, on success */
		s->failed_points = points;
		s->failed_style = style;
	}
	return ok;
}

/* ---- The family ---------------------------------------------------------------------------- */

static u8 *family_font(u8 *family, int points, int style)
{
	for (u32 i = 0; i < FAMILY_COUNT(family); i++) {
		u8 *f = FAMILY_FONTS(family)[i];
		if (f && FONT_SIZE(f) == points && (int)FONT_STYLE(f) == style) return f;
	}
	return 0;
}

void *big_font_family(void *family_in, int points, int big_points)
{
	u8 *family = family_in, *header = 0;
	int want_points[SLOTS] = { points, points, big_points }, want_style[SLOTS] = { 0, 1, 1 };
	int changed = 0, ours = 0;

	if (!family || g_failed || FAMILY_COUNT(family) == 0 || FAMILY_COUNT(family) > 256) return family;
	for (u32 i = 0; i < FAMILY_COUNT(family) && !header; i++)
		if (FAMILY_FONTS(family)[i]) header = FAMILY_FONTS(family)[i] - INSTANCE_HEADER;
	if (!header) return family;
	for (int i = 0; i < SLOTS; i++) {
		Slot *s = &g_slot[i];
		int p = want_points[i];
		if (p <= 0 || family_font(family, p, want_style[i])) {   /* Oni has it */
			want_points[i] = 0;
			continue;
		}
		if (s->points != p || s->style != want_style[i]) {
			if (s->failed_points == p && s->failed_style == want_style[i]) {
				want_points[i] = 0;
				continue;
			}
			check_14(family_font(family, 14, want_style[i]), want_style[i]);
			if (!make_font(s, i, p, want_style[i], header)) {
				want_points[i] = 0;
				if (g_failed) return family;
			}
			changed = 1;
		}
		ours += want_points[i] != 0;
	}
	if (!ours) return family;
	for (int i = 0; i < SLOTS; i++)
		if (g_family_points[i] != want_points[i] || g_family_styles[i] != want_style[i]) changed = 1;
	if (g_family && g_family_of == family && !changed) return g_family;

	/* the copy: the family's instance header and fields, its fonts, then ours */
	u32 count = FAMILY_COUNT(family), size = INSTANCE_HEADER + 0x18 + (count + SLOTS) * 4;
	if (g_family) VirtualFree(g_family - INSTANCE_HEADER, 0, 0x8000);
	g_family = 0;
	u8 *block = VirtualAlloc(0, size, 0x3000, 4);
	if (!block) return family;
	memcpy(block, family - INSTANCE_HEADER, INSTANCE_HEADER + 0x18 + count * 4);
	g_family = block + INSTANCE_HEADER;
	g_family_of = family;
	for (int i = 0; i < SLOTS; i++) {
		g_family_points[i] = want_points[i];
		g_family_styles[i] = want_style[i];
		if (want_points[i] && g_slot[i].points == want_points[i]) FAMILY_FONTS(g_family)[count++] = g_slot[i].font;
	}
	FAMILY_COUNT(g_family) = count;
	return g_family;
}

/* Before a networked match: every texture made now, so none is made while rollback may rewind */
void big_font_prepare(void)
{
	for (int i = 0; i < SLOTS; i++) texture_make(&g_slot[i], i);
}

void big_font_install(void)
{
	if (patch_call(TScPrivateData_Call1, 0x4242c0, (u32)font_private) &&
	    patch_call(TScPrivateData_Call2, 0x4242c0, (u32)font_private))
		log_msg("big font: Tahoma sizes Oni lacks can be made");
	else
		g_failed = 1;
}
