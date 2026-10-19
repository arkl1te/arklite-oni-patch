/* Log, patching and string helpers shared by Arklite's patches. */
#include "arklite.h"

/* The MSVC target emits a reference to this whenever floats are used. */
int _fltused;

/* The compiler may emit calls to these even with -ffreestanding. */
void *memset(void *d, int c, unsigned n)
{
	volatile u8 *p = d;
	while (n--) *p++ = (u8)c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
	volatile u8 *p = d;
	const u8 *q = s;
	while (n--) *p++ = *q++;
	return d;
}

/* ---- Log (AE\Arklite.log, open for the whole session) ------------------------------------ */

static HANDLE g_log = INVALID_HANDLE_VALUE;

void log_open(const char *path)
{
	g_log = CreateFileA(path, 0x40000000 /* GENERIC_WRITE */, 1 /* FILE_SHARE_READ */, 0,
	                    2 /* CREATE_ALWAYS */, 0x80, 0);
}

void log_msg(const char *f, ...)
{
	char buf[1100];
	va_list ap;
	u32 written;

	if (g_log == INVALID_HANDLE_VALUE) return;
	va_start(ap, f);
	int n = wvsprintfA(buf, f, ap);
	va_end(ap);
	buf[n++] = '\r';
	buf[n++] = '\n';
	WriteFile(g_log, buf, n, &written, 0);
}

/* ---- Patching -------------------------------------------------------------------------- */

static void write_code(u8 *p, const void *bytes, u32 n)
{
	u32 old;
	VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old);
	memcpy(p, bytes, n);
	VirtualProtect(p, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), p, n);
}

int patch_push_imm32(u32 addr, u32 expected, u32 value)
{
	u8 *p = (u8 *)addr;

	if (p[0] != 0x68 || *(u32 *)(p + 1) != expected) {
		log_msg("patch 0x%x: unexpected bytes (%02x %08x), skipped", addr, p[0], *(u32 *)(p + 1));
		return 0;
	}
	write_code(p + 1, &value, 4);
	return 1;
}

int patch_call(u32 addr, u32 expected, u32 target)
{
	u8 *p = (u8 *)addr;
	u32 rel = target - (addr + 5);

	if (p[0] != 0xe8 || addr + 5 + *(u32 *)(p + 1) != expected) {
		log_msg("patch 0x%x: unexpected call, skipped", addr);
		return 0;
	}
	write_code(p + 1, &rel, 4);
	return 1;
}

int code_matches(u32 addr, const u8 *expected, u32 n)
{
	const u8 *p = (const u8 *)addr;
	for (u32 i = 0; i < n; i++)
		if (p[i] != expected[i]) return 0;
	return 1;
}

void patch_write(u32 addr, const void *bytes, u32 n)
{
	write_code((u8 *)addr, bytes, n);
}

int patch_byte(u32 addr, u8 expected, u8 value)
{
	u8 *p = (u8 *)addr;

	if (*p != expected) {
		log_msg("patch 0x%x: unexpected byte %02x, skipped", addr, *p);
		return 0;
	}
	write_code(p, &value, 1);
	return 1;
}

/* ---- Settings ----------------------------------------------------------------------------- */

const char *ini_path(void)
{
	static char path[MAX_PATH];

	if (!path[0] && !GetFullPathNameA("Arklite.ini", sizeof path, path, 0))
		str_cpy(path, sizeof path, "Arklite.ini");
	return path;
}

/* ---- Windows ------------------------------------------------------------------------------ */

void win_insert_after(WMtWindow *win, WMtWindow *after)
{
	/* Sibling links: +0 prev, +4 next; parent +0x10 first child. */
	WMtWindow **w = (WMtWindow **)win, **a = (WMtWindow **)after;
	WMtWindow *parent = *(WMtWindow **)((u8 *)win + 8);

	if (w[0]) ((WMtWindow **)w[0])[1] = w[1];
	else      *(WMtWindow **)((u8 *)parent + 0x10) = w[1];
	if (w[1]) ((WMtWindow **)w[1])[0] = w[0];

	w[0] = after;
	w[1] = a[1];
	if (a[1]) ((WMtWindow **)a[1])[0] = win;
	a[1] = win;
}

/* ---- Strings ----------------------------------------------------------------------------- */

unsigned str_len(const char *s)
{
	unsigned n = 0;
	while (s[n]) n++;
	return n;
}

int str_cpy(char *d, unsigned size, const char *s)
{
	unsigned i = 0;
	for (; s[i] && i + 1 < size; i++) d[i] = s[i];
	d[i] = 0;
	return s[i] == 0;
}

char to_upper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

int str_ieq_n(const char *a, const char *b, unsigned n)
{
	for (unsigned i = 0; i < n; i++) {
		if (to_upper(a[i]) != to_upper(b[i])) return 0;
		if (!a[i]) return 1;
	}
	return 1;
}

int str_contains_i(const char *s, const char *sub)
{
	unsigned n = str_len(sub);
	for (; *s; s++)
		if (str_ieq_n(s, sub, n)) return 1;
	return 0;
}

void fmt(char *buf, const char *f, ...)
{
	va_list ap;
	va_start(ap, f);
	wvsprintfA(buf, f, ap);
	va_end(ap);
}

const char *skip_space(const char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	return s;
}

int parse_float(const char **ps, float *out)
{
	const char *s = skip_space(*ps);
	float v = 0, scale = 1;
	int neg = 0, digits = 0;

	if (*s == '-' || *s == '+') neg = *s++ == '-';
	for (; *s >= '0' && *s <= '9'; s++, digits++) v = v * 10 + (*s - '0');
	if (*s == '.')
		for (s++; *s >= '0' && *s <= '9'; s++, digits++) v += (*s - '0') * (scale /= 10);
	if (!digits) return 0;
	*out = neg ? -v : v;
	*ps = s;
	return 1;
}
