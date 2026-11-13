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

/* ---- Log (AE\logs\Arklite_<date>_<time>.log, open for the whole session) ------------------ */

typedef struct { u16 year, month, weekday, day, hour, minute, second, ms; } SYSTEMTIME;
__declspec(dllimport) void WINAPI GetLocalTime(SYSTEMTIME *);
__declspec(dllimport) int  WINAPI CreateDirectoryA(const char *, void *);
__declspec(dllimport) int  WINAPI DeleteFileA(const char *);
__declspec(dllimport) int  WINAPI CloseHandle(HANDLE);

static HANDLE g_log = INVALID_HANDLE_VALUE;
static char g_log_path[MAX_PATH] = "Arklite.log";

/* "Arklite.log" -> logs\Arklite_2026-10-29_00-45-12.log. A crash or freeze used to be lost to
 * the next start rewriting the file; now the newest LOG_KEEP of each name are kept (the oldest
 * deleted to make room for this one). path gets the file's name, for the log. */
static int name_before(const char *a, const char *b)
{
	while (*a && *a == *b) a++, b++;
	return (u8)*a < (u8)*b;
}

HANDLE log_file_create(const char *name, char *path)
{
	char stem[100], pattern[MAX_PATH], oldest[MAX_PATH];
	const char *ext = "";
	WIN32_FIND_DATAA fd;
	SYSTEMTIME t;
	u32 n = str_len(name);

	if (n >= sizeof stem) return INVALID_HANDLE_VALUE;
	str_cpy(stem, sizeof stem, name);
	for (u32 i = n; i-- > 0 && stem[i] != '\\' && stem[i] != '/'; )
		if (stem[i] == '.') { ext = name + i; stem[i] = 0; break; }
	CreateDirectoryA(LOG_DIR, 0);
	fmt(pattern, LOG_DIR "\\%s_\?\?\?\?-\?\?-\?\?_\?\?-\?\?-\?\?%s", stem, ext);
	for (;;) {   /* the names sort by time: delete the first until there's room */
		u32 count = 0;
		HANDLE h = FindFirstFileA(pattern, &fd);
		if (h == INVALID_HANDLE_VALUE) break;
		do {
			if (!count++ || name_before(fd.cFileName, oldest)) str_cpy(oldest, sizeof oldest, fd.cFileName);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
		if (count < LOG_KEEP) break;
		fmt(path, LOG_DIR "\\%s", oldest);
		if (!DeleteFileA(path)) break;
	}
	GetLocalTime(&t);
	fmt(path, LOG_DIR "\\%s_%04d-%02d-%02d_%02d-%02d-%02d%s", stem, t.year, t.month, t.day, t.hour, t.minute, t.second, ext);
	return CreateFileA(path, 0x40000000 /* GENERIC_WRITE */, 1 /* FILE_SHARE_READ */, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
}

void log_open(const char *name)
{
	char path[MAX_PATH];
	if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
	g_log = log_file_create(name, path);
	if (g_log != INVALID_HANDLE_VALUE) str_cpy(g_log_path, sizeof g_log_path, path);
}

const char *log_path(void) { return g_log_path; }

void log_msg(const char *f, ...)
{
	char buf[1100];
	va_list ap;
	u32 written;

	if (g_log == INVALID_HANDLE_VALUE) return;
	va_start(ap, f);
	int n = wvsprintfA(buf, f, ap);
	va_end(ap);
	if (text_has_wide(buf)) {   /* names and chat beyond ASCII: UTF-8 in the file */
		char utf8[1100];
		n = text_to_utf8(utf8, sizeof utf8 - 2, buf);
		memcpy(buf, utf8, (unsigned)n);
	}
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
	unsigned i = 0, at = 0;
	for (; s[i] && i + 1 < size; i++) d[i] = s[i];
	if (!s[i]) {
		d[i] = 0;
		return 1;
	}
	/* cut short: never in the middle of a two-byte character (text.c), whose lead byte alone
	 * before the NUL would make Oni's text drawing read past it */
	while (at < i) {
		unsigned next = (u8)d[at] >= 0x80 && (u8)d[at] != 0xff ? at + 2 : at + 1;
		if (next > i) break;
		at = next;
	}
	d[at] = 0;
	return 0;
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
