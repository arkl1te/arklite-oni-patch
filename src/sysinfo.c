/* Machine and setup dump: everything about this PC and this Oni that can make one player's game
 * differ from another's, written next to the run's log as <log>.profile.txt and sent with every
 * problem report (report.c), so a bug can be told apart as one player's, one setup's or everyone's.
 *
 * What (no addresses, no Windows user name, nothing that says who or where someone is; the player's
 * nickname from Arklite.ini is the name):
 *   - Arklite's build, the nickname, the session and slot when written;
 *   - Windows (product, version, build) or Wine (its version and the host system), the UI language;
 *   - the CPU, its logical processors, the memory;
 *   - every display adapter Windows knows (driver description, provider, version, date: a hybrid
 *     laptop lists both) and the one OpenGL really draws with (GL_VENDOR, GL_RENDERER, GL_VERSION);
 *   - the display mode, the monitors, Oni's window (client size, borders or not, covering its
 *     monitor = fullscreen), its DPI;
 *   - the process priority, the timer resolution;
 *   - Oni's files: MD5 of Oni.exe, Daodan (binkw32.dll), Arklite.dll and the other DLLs beside them,
 *     daodan.ini, persist.dat (hex) and Arklite.ini (addresses masked), AE's installed packages with
 *     their versions, the size and MD5 of level0;
 *   - every module loaded in Oni (name, size, PE time stamp, folder kind), overlays and hooks named
 *     (Steam, Discord, RivaTuner, ReShade, GeForce Experience, AMD's).
 * When: the first frame of a level, a new network session, and every 10 minutes of play. The parts
 * that need Oni's GL context and window are read on the main thread; the rest (registry, files,
 * hashes) on a thread of its own, which writes the file. */
#include "arklite.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) int    WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) u32    WINAPI GetFileSize(HANDLE, u32 *);
__declspec(dllimport) u32    WINAPI GetModuleFileNameA(void *, char *, u32);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);
__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) u32    WINAPI GetTickCount(void);
__declspec(dllimport) u16    WINAPI GetUserDefaultUILanguage(void);
__declspec(dllimport) HANDLE WINAPI GetCurrentProcess(void);
__declspec(dllimport) void   WINAPI GetLocalTime(void *);
__declspec(dllimport) u32    WINAPI GetWindowsDirectoryA(char *, u32);

typedef struct { u16 year, month, weekday, day, hour, minute, second, ms; } SYSTEMTIME_;
__declspec(dllimport) void * WINAPI WindowFromDC(void *);
__declspec(dllimport) int    WINAPI GetClientRect(void *, void *);

#define PROFILE_EVERY_MS  (10u * 60u * 1000u)
#define OUT_MAX           (256u << 10)

typedef struct { long l, t, r, b; } Rect;

/* the main thread's part (GL and the window), handed to the thread */
typedef struct {
	char why[64], session[40], gl_vendor[96], gl_renderer[160], gl_version[96];
	u32 gl_ext_len, gl_ext_hash;
	int slot, have_window, client_w, client_h, win_x, win_y, win_w, win_h, mon_w, mon_h, borderless, dpi;
	u32 style;
	u32 quality;   /* P3gQuality: 0 High, 1 Medium, 2 Low */
	char path[MAX_PATH];
} Snap;

static u32 g_last_ms, g_busy;
static char g_last_session[40];
static char g_profile_path[MAX_PATH];

/* ---- Output ---------------------------------------------------------------------------------- */

static char *g_out;
static u32 g_len;

static void put(const char *s)
{
	u32 n = str_len(s);
	if (g_len + n + 2 >= OUT_MAX) return;
	memcpy(g_out + g_len, s, n);
	g_len += n;
}

static void line(const char *s) { put(s); put("\r\n"); }

static const char *str_find(const char *s, const char *sub)
{
	u32 n = str_len(sub);
	for (; *s; s++) {
		u32 i = 0;
		while (i < n && s[i] == sub[i]) i++;
		if (i == n) return s;
	}
	return 0;
}

/* a line from fmt (wvsprintfA: no floats, at most 1024 characters) */
#define LINE(...) do { char b_[1100]; fmt(b_, __VA_ARGS__); line(b_); } while (0)

/* an IPv4 address (a.b.c.d) anywhere in s becomes [address] (ini values: servers, helper, stun) */
static void mask_addresses(char *s)
{
	for (char *p = s; *p; p++) {
		char *q = p;
		int dots = 0, digits = 0;
		if (p > s && ((p[-1] >= '0' && p[-1] <= '9') || p[-1] == '.')) continue;
		while ((*q >= '0' && *q <= '9') || *q == '.') {
			if (*q == '.') { dots++; if (!digits) break; digits = 0; } else digits++;
			q++;
		}
		if (dots == 3 && digits) {
			const char *rep = "[address]";
			u32 rl = str_len(rep), tail = str_len(q);
			if ((u32)(q - p) >= rl) {
				memcpy(p, rep, rl);
				memcpy(p + rl, q, tail + 1);
				p += rl - 1;
			}
		}
	}
}

/* ---- MD5 (RFC 1321) ------------------------------------------------------------------------- */

typedef struct { u32 a, b, c, d, len_lo, len_hi; u8 buf[64]; u32 n; } Md5;

static u32 rol(u32 x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5_block(Md5 *m, const u8 *p)
{
	static const u32 K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
	static const u8 R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
	u32 w[16], a = m->a, b = m->b, c = m->c, d = m->d;
	for (int i = 0; i < 16; i++) w[i] = (u32)p[i * 4] | (u32)p[i * 4 + 1] << 8 | (u32)p[i * 4 + 2] << 16 | (u32)p[i * 4 + 3] << 24;
	for (int i = 0; i < 64; i++) {
		u32 f, g;
		if (i < 16) { f = (b & c) | (~b & d); g = (u32)i; }
		else if (i < 32) { f = (d & b) | (~d & c); g = (5u * (u32)i + 1) & 15; }
		else if (i < 48) { f = b ^ c ^ d; g = (3u * (u32)i + 5) & 15; }
		else { f = c ^ (b | ~d); g = (7u * (u32)i) & 15; }
		u32 t = d;
		d = c;
		c = b;
		b = b + rol(a + f + K[i] + w[g], R[i]);
		a = t;
	}
	m->a += a; m->b += b; m->c += c; m->d += d;
}

static void md5_init(Md5 *m) { memset(m, 0, sizeof *m); m->a = 0x67452301; m->b = 0xefcdab89; m->c = 0x98badcfe; m->d = 0x10325476; }

static void md5_add(Md5 *m, const u8 *p, u32 n)
{
	u32 lo = m->len_lo;
	m->len_lo += n << 3;
	if (m->len_lo < lo) m->len_hi++;
	m->len_hi += n >> 29;
	while (n) {
		u32 k = 64 - m->n < n ? 64 - m->n : n;
		memcpy(m->buf + m->n, p, k);
		m->n += k; p += k; n -= k;
		if (m->n == 64) { md5_block(m, m->buf); m->n = 0; }
	}
}

static void md5_hex(Md5 *m, char *out)
{
	u8 pad = 0x80, z = 0, len[8];
	u32 lo = m->len_lo, hi = m->len_hi;
	for (int i = 0; i < 4; i++) { len[i] = (u8)(lo >> (8 * i)); len[4 + i] = (u8)(hi >> (8 * i)); }
	md5_add(m, &pad, 1);
	while (m->n != 56) md5_add(m, &z, 1);
	md5_add(m, len, 8);
	u32 v[4] = { m->a, m->b, m->c, m->d };
	static const char hx[] = "0123456789abcdef";
	for (int i = 0; i < 16; i++) {
		u8 byte = (u8)(v[i / 4] >> (8 * (i % 4)));
		out[i * 2] = hx[byte >> 4];
		out[i * 2 + 1] = hx[byte & 15];
	}
	out[32] = 0;
}

/* the MD5 and size of a file, 0 if it can't be read */
static int file_md5(const char *path, char *hex, u32 *size)
{
	HANDLE f = CreateFileA(path, 0x80000000, 1 | 2, 0, 3, 0x80, 0);
	static u8 buf[1u << 16];
	Md5 m;
	u32 got;
	if (f == INVALID_HANDLE_VALUE) return 0;
	*size = GetFileSize(f, 0);
	md5_init(&m);
	while (ReadFile(f, buf, sizeof buf, &got, 0) && got) md5_add(&m, buf, got);
	CloseHandle(f);
	md5_hex(&m, hex);
	return 1;
}

/* a whole small file into buf (NUL-terminated), its length; 0 if none */
static u32 file_text(const char *path, char *buf, u32 max)
{
	HANDLE f = CreateFileA(path, 0x80000000, 1 | 2, 0, 3, 0x80, 0);
	u32 got = 0;
	if (f == INVALID_HANDLE_VALUE) return 0;
	ReadFile(f, buf, max - 1, &got, 0);
	CloseHandle(f);
	buf[got] = 0;
	return got;
}

/* ---- Run-time imports (missing ones leave their lines out) ---------------------------------- */

static struct {
	long (WINAPI *RegOpenKeyExA)(void *, const char *, u32, u32, void **);
	long (WINAPI *RegQueryValueExA)(void *, const char *, u32 *, u32 *, u8 *, u32 *);
	long (WINAPI *RegEnumKeyExA)(void *, u32, char *, u32 *, u32 *, char *, u32 *, void *);
	long (WINAPI *RegCloseKey)(void *);
	int (WINAPI *EnumDisplayDevicesA)(const char *, u32, void *, u32);
	int (WINAPI *EnumDisplaySettingsA)(const char *, u32, void *);
	int (WINAPI *GetSystemMetrics)(int);
	long (WINAPI *GetWindowLongA)(void *, int);
	int (WINAPI *GetWindowRect)(void *, Rect *);
	void *(WINAPI *MonitorFromWindow)(void *, u32);
	int (WINAPI *GetMonitorInfoA)(void *, void *);
	u32 (WINAPI *GetDpiForWindow)(void *);
	void (WINAPI *GetSystemInfo)(void *);
	int (WINAPI *GlobalMemoryStatusEx)(void *);
	u32 (WINAPI *GetPriorityClass)(HANDLE);
	HANDLE (WINAPI *CreateToolhelp32Snapshot)(u32, u32);
	int (WINAPI *Module32First)(HANDLE, void *);
	int (WINAPI *Module32Next)(HANDLE, void *);
	long (WINAPI *RtlGetVersion)(void *);
	const char *(__cdecl *wine_get_version)(void);
	void (__cdecl *wine_get_host_version)(const char **, const char **);
	long (WINAPI *NtQueryTimerResolution)(u32 *, u32 *, u32 *);
	const u8 *(WINAPI *glGetString)(u32);
	void *(WINAPI *wglGetCurrentDC)(void);
	int ok;
} W;

static void imports(void)
{
	HANDLE adv, usr, k32, nt, gl;
	if (W.ok) return;
	W.ok = 1;
	adv = LoadLibraryA("advapi32.dll");
	usr = GetModuleHandleA("user32.dll");
	k32 = GetModuleHandleA("kernel32.dll");
	nt = GetModuleHandleA("ntdll.dll");
	gl = GetModuleHandleA("opengl32.dll");
	if (adv) {
		W.RegOpenKeyExA = GetProcAddress(adv, "RegOpenKeyExA");
		W.RegQueryValueExA = GetProcAddress(adv, "RegQueryValueExA");
		W.RegEnumKeyExA = GetProcAddress(adv, "RegEnumKeyExA");
		W.RegCloseKey = GetProcAddress(adv, "RegCloseKey");
	}
	if (usr) {
		W.EnumDisplayDevicesA = GetProcAddress(usr, "EnumDisplayDevicesA");
		W.EnumDisplaySettingsA = GetProcAddress(usr, "EnumDisplaySettingsA");
		W.GetSystemMetrics = GetProcAddress(usr, "GetSystemMetrics");
		W.GetWindowLongA = GetProcAddress(usr, "GetWindowLongA");
		W.GetWindowRect = GetProcAddress(usr, "GetWindowRect");
		W.MonitorFromWindow = GetProcAddress(usr, "MonitorFromWindow");
		W.GetMonitorInfoA = GetProcAddress(usr, "GetMonitorInfoA");
		W.GetDpiForWindow = GetProcAddress(usr, "GetDpiForWindow");
	}
	if (k32) {
		W.GetSystemInfo = GetProcAddress(k32, "GetSystemInfo");
		W.GlobalMemoryStatusEx = GetProcAddress(k32, "GlobalMemoryStatusEx");
		W.GetPriorityClass = GetProcAddress(k32, "GetPriorityClass");
		W.CreateToolhelp32Snapshot = GetProcAddress(k32, "CreateToolhelp32Snapshot");
		W.Module32First = GetProcAddress(k32, "Module32First");
		W.Module32Next = GetProcAddress(k32, "Module32Next");
	}
	if (nt) {
		W.RtlGetVersion = GetProcAddress(nt, "RtlGetVersion");
		W.wine_get_version = GetProcAddress(nt, "wine_get_version");
		W.wine_get_host_version = GetProcAddress(nt, "wine_get_host_version");
		W.NtQueryTimerResolution = GetProcAddress(nt, "NtQueryTimerResolution");
	}
	if (gl) {
		W.glGetString = GetProcAddress(gl, "glGetString");
		W.wglGetCurrentDC = GetProcAddress(gl, "wglGetCurrentDC");
	}
}

#define HKLM ((void *)0x80000002)

/* a registry string (or DWORD as decimal) into out; 0 if none */
static int reg_str(void *root, const char *key, const char *name, char *out, u32 size)
{
	void *k;
	u32 type = 0, n = size - 1;
	out[0] = 0;
	if (!W.RegOpenKeyExA || W.RegOpenKeyExA(root, key, 0, 0x20019 /* KEY_READ */, &k)) return 0;
	if (W.RegQueryValueExA(k, name, 0, &type, (u8 *)out, &n)) { W.RegCloseKey(k); out[0] = 0; return 0; }
	W.RegCloseKey(k);
	if (type == 4 /* REG_DWORD */ && n == 4) { u32 v; memcpy(&v, out, 4); fmt(out, "%u", v); return 1; }
	out[n < size ? n : size - 1] = 0;
	return out[0] != 0;
}

/* ---- The main thread's part ----------------------------------------------------------------- */

static void snap_main(Snap *s)
{
	void *dc, *w;
	imports();
	s->quality = *(u32 *)0x6a8704;
	if (W.glGetString) {
		const u8 *v = W.glGetString(0x1f00), *r = W.glGetString(0x1f01), *ver = W.glGetString(0x1f02), *e = W.glGetString(0x1f03);
		if (v) str_cpy(s->gl_vendor, sizeof s->gl_vendor, (const char *)v);
		if (r) str_cpy(s->gl_renderer, sizeof s->gl_renderer, (const char *)r);
		if (ver) str_cpy(s->gl_version, sizeof s->gl_version, (const char *)ver);
		if (e) {
			u32 h = 0x811c9dc5u;
			for (const u8 *c = e; *c; c++) { h = (h ^ *c) * 16777619u; s->gl_ext_len++; }
			s->gl_ext_hash = h;
		}
	}
	dc = W.wglGetCurrentDC ? W.wglGetCurrentDC() : 0;
	w = dc ? WindowFromDC(dc) : 0;
	if (w) {
		Rect c, r;
		struct { u32 cb; Rect mon, work; u32 flags; } mi;
		s->have_window = 1;
		if (GetClientRect(w, &c)) { s->client_w = (int)(c.r - c.l); s->client_h = (int)(c.b - c.t); }
		if (W.GetWindowRect && W.GetWindowRect(w, &r)) {
			s->win_x = (int)r.l; s->win_y = (int)r.t; s->win_w = (int)(r.r - r.l); s->win_h = (int)(r.b - r.t);
		}
		if (W.GetWindowLongA) s->style = (u32)W.GetWindowLongA(w, -16 /* GWL_STYLE */);
		s->borderless = !(s->style & 0x00C00000u /* WS_CAPTION */);
		mi.cb = sizeof mi;
		if (W.MonitorFromWindow && W.GetMonitorInfoA && W.GetMonitorInfoA(W.MonitorFromWindow(w, 2), &mi)) {
			s->mon_w = (int)(mi.mon.r - mi.mon.l);
			s->mon_h = (int)(mi.mon.b - mi.mon.t);
		}
		if (W.GetDpiForWindow) s->dpi = (int)W.GetDpiForWindow(w);
	}
	s->slot = net_local_slot();
	str_cpy(s->session, sizeof s->session, net_session_id());
}

/* ---- The thread: the rest, and the file ------------------------------------------------------ */

static const char *quality_name(u32 q) { return q == 0 ? "High" : q == 1 ? "Medium" : q == 2 ? "Low" : "?"; }

static const struct { const char *module, *what; } g_hooks[] = {
	{ "gameoverlayrenderer", "Steam overlay" }, { "discordhook", "Discord overlay" }, { "rtsshooks", "RivaTuner (RTSS)" },
	{ "reshade", "ReShade" }, { "nvspcap", "GeForce Experience capture" }, { "nvcamera", "NVIDIA Ansel" },
	{ "amdihk", "AMD hotkeys (Radeon Software)" }, { "atiuxpag", "AMD overlay" }, { "obs-", "OBS game capture" },
	{ "graphics-hook", "OBS game capture" }, { "fraps", "Fraps" }, { "bdcam", "Bandicam" }, { "overwolf", "Overwolf" },
	{ "medal", "Medal" }, { "xsplit", "XSplit" }, { "dinput8", "a DirectInput wrapper" }, { "dxwrapper", "dxwrapper" },
};

static int ends_with_i(const char *s, const char *e)
{
	u32 a = str_len(s), b = str_len(e);
	return a >= b && str_ieq_n(s + a - b, e, b);
}

static void section(const char *name) { put("\r\n["); put(name); line("]"); }

static void dump_os(void)
{
	char a[256], b[64], c[64], d[64];
	struct { u32 size, major, minor, build, platform; u16 sp[128]; } v;
	section("os");
	if (reg_str(HKLM, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ProductName", a, sizeof a)) {
		if (!reg_str(HKLM, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion", b, sizeof b))
			reg_str(HKLM, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ReleaseId", b, sizeof b);
		reg_str(HKLM, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuildNumber", c, sizeof c);
		reg_str(HKLM, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "UBR", d, sizeof d);
		LINE("windows: %s %s, build %s.%s", a, b, c, d);
	}
	memset(&v, 0, sizeof v);
	v.size = sizeof v;
	if (W.RtlGetVersion && !W.RtlGetVersion(&v)) LINE("kernel: %u.%u.%u", v.major, v.minor, v.build);
	if (W.wine_get_version) {
		const char *sys = 0, *rel = 0;
		if (W.wine_get_host_version) W.wine_get_host_version(&sys, &rel);
		LINE("wine: %s on %s %s", W.wine_get_version(), sys ? sys : "?", rel ? rel : "?");
	} else {
		line("wine: no");
	}
	LINE("ui language: %04x", GetUserDefaultUILanguage());
}

static void dump_cpu_memory(void)
{
	char a[160], mhz[32];
	struct { u16 arch, res; u32 page; void *min, *max; u32 mask, n, type, gran; u16 level, rev; } si;
	struct { u32 len, load; u32 phys_lo, phys_hi, avail_lo, avail_hi, pf_lo, pf_hi, apf_lo, apf_hi, v_lo, v_hi, av_lo, av_hi, x_lo, x_hi; } ms;
	section("cpu and memory");
	if (reg_str(HKLM, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", a, sizeof a)) {
		reg_str(HKLM, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "~MHz", mhz, sizeof mhz);
		LINE("cpu: %s (%s MHz)", a, mhz);
	}
	if (W.GetSystemInfo) { memset(&si, 0, sizeof si); W.GetSystemInfo(&si); LINE("logical processors: %u", si.n); }
	memset(&ms, 0, sizeof ms);
	ms.len = sizeof ms;
	if (W.GlobalMemoryStatusEx && W.GlobalMemoryStatusEx(&ms))
		LINE("memory: %u MB, %u MB free", (ms.phys_lo >> 20) | (ms.phys_hi << 12), (ms.avail_lo >> 20) | (ms.avail_hi << 12));
}

static void dump_gpus(const Snap *s)
{
	char key[200], desc[160], prov[96], ver[64], date[64];
	struct { u32 cb; char name[32], string[128]; u32 flags; char id[128], key[128]; } dd;
	section("graphics");
	for (u32 i = 0; i < 16; i++) {   /* every display adapter Windows has a driver for, active or not */
		fmt(key, "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\%04u", i);
		if (!reg_str(HKLM, key, "DriverDesc", desc, sizeof desc)) continue;
		reg_str(HKLM, key, "ProviderName", prov, sizeof prov);
		reg_str(HKLM, key, "DriverVersion", ver, sizeof ver);
		reg_str(HKLM, key, "DriverDate", date, sizeof date);
		LINE("adapter: %s (%s%sdriver %s, %s)", desc, prov, prov[0] ? ", " : "", ver, date);
	}
	for (u32 i = 0; W.EnumDisplayDevicesA && i < 16; i++) {
		memset(&dd, 0, sizeof dd);
		dd.cb = sizeof dd;
		if (!W.EnumDisplayDevicesA(0, i, &dd, 0)) break;
		if (dd.flags & 1 /* ATTACHED_TO_DESKTOP */)
			LINE("output %u: %s%s", i, dd.string, dd.flags & 4 /* PRIMARY */ ? " (primary)" : "");
	}
	LINE("opengl: %s | %s | %s", s->gl_vendor[0] ? s->gl_vendor : "?", s->gl_renderer[0] ? s->gl_renderer : "?",
	     s->gl_version[0] ? s->gl_version : "?");
	LINE("opengl extensions: %u characters, hash %08x", s->gl_ext_len, s->gl_ext_hash);
}

static void dump_display(const Snap *s)
{
	struct { char name[32]; u16 spec, drv, size, extra; u32 fields; u8 pad[16]; u16 color, duplex, yres, tt, collate;
	         char form[32]; u16 logpix; u32 bpp, w, h, flags, freq; u8 rest[64]; } dm;
	section("display");
	memset(&dm, 0, sizeof dm);
	dm.size = 156;
	if (W.EnumDisplaySettingsA && W.EnumDisplaySettingsA(0, 0xffffffffu /* ENUM_CURRENT_SETTINGS */, &dm))
		LINE("mode: %ux%u, %u Hz, %u bits", dm.w, dm.h, dm.freq, dm.bpp);
	if (W.GetSystemMetrics) LINE("monitors: %d", W.GetSystemMetrics(80 /* SM_CMONITORS */));
	if (s->have_window) {
		int full = s->borderless && s->win_w == s->mon_w && s->win_h == s->mon_h;
		LINE("oni window: client %dx%d, window %dx%d at %d,%d, monitor %dx%d, %s (style %08x)", s->client_w, s->client_h,
		     s->win_w, s->win_h, s->win_x, s->win_y, s->mon_w, s->mon_h,
		     full ? "fullscreen" : s->borderless ? "borderless window" : "window with borders", s->style);
		if (s->dpi) LINE("dpi: %d (%d%%)", s->dpi, s->dpi * 100 / 96);
	}
	LINE("particle quality: %s", quality_name(s->quality));
}

static void dump_process(void)
{
	u32 mn = 0, mx = 0, cur = 0, pc;
	section("process");
	pc = W.GetPriorityClass ? W.GetPriorityClass(GetCurrentProcess()) : 0;
	LINE("priority class: %x", pc);
	if (W.NtQueryTimerResolution && !W.NtQueryTimerResolution(&mn, &mx, &cur))
		LINE("timer resolution: now %u.%u ms (coarsest %u.%u, finest %u.%u)", cur / 10000, cur / 1000 % 10, mn / 10000, mn / 1000 % 10,
		     mx / 10000, mx / 1000 % 10);
}

/* Oni.exe's folder (with the last backslash) */
static void oni_dir(char *dir)
{
	u32 n = GetModuleFileNameA(0, dir, MAX_PATH);
	while (n && dir[n - 1] != '\\' && dir[n - 1] != '/') n--;
	dir[n] = 0;
}

static void dump_files(void)
{
	char dir[MAX_PATH], path[MAX_PATH + 64], hex[40];
	static const char *const names[] = { "Oni.exe", "binkw32.dll", "bink_orig.dll", "realbink.dll", "Arklite.dll", "xinput1_3.dll",
	                                     "opengl32.dll", "dinput8.dll", "dsound.dll", "d3d9.dll", "dxgi.dll", "winmm.dll" };
	u32 size;
	oni_dir(dir);
	section("oni files (beside Oni.exe)");
	for (u32 i = 0; i < sizeof names / sizeof names[0]; i++) {
		fmt(path, "%s%s", dir, names[i]);
		if (file_md5(path, hex, &size)) LINE("%s: %s, %u bytes", names[i], hex, size);
	}
	fmt(path, "%sGameDataFolder\\level0_Final.dat", dir);
	if (file_md5(path, hex, &size)) LINE("level0_Final.dat: %s, %u bytes", hex, size);
	fmt(path, "%sGameDataFolder\\level0_Final.raw", dir);
	if (file_md5(path, hex, &size)) LINE("level0_Final.raw: %s, %u bytes", hex, size);
}

/* "Key -> value" from an AE Mod_Info.cfg, the line starting with key */
static void cfg_value(const char *cfg, const char *key, char *out, u32 size)
{
	u32 kl = str_len(key);
	for (const char *l = cfg; *l; ) {
		if (str_ieq_n(l, key, kl) && (l[kl] == ' ' || l[kl] == '-')) {
			const char *v = l + kl;
			while (*v == ' ' || *v == '-' || *v == '>') v++;
			u32 k = 0;
			while (v[k] && v[k] != '\r' && v[k] != '\n' && k < size - 1) { out[k] = v[k]; k++; }
			out[k] = 0;
			return;
		}
		while (*l && *l != '\n') l++;
		if (*l) l++;
	}
}

/* AE: installed_mods.xml's package numbers, each with its version from AEInstaller\packages\<n...>\Mod_Info.cfg */
static void dump_packages(void)
{
	char dir[MAX_PATH], path[MAX_PATH + 96];
	static char xml[16384], cfg[4096];
	WIN32_FIND_DATAA fd;
	oni_dir(dir);
	section("ae packages");
	fmt(path, "%sGameDataFolder\\installed_mods.xml", dir);
	if (!file_text(path, xml, sizeof xml)) { line("installed_mods.xml: none"); return; }
	for (const char *p = xml; (p = str_find(p, "<int>")); ) {
		u32 num = 0;
		p += 5;
		while (*p >= '0' && *p <= '9') num = num * 10 + (u32)(*p++ - '0');
		char version[48] = "?", mod[96] = "";
		HANDLE h;
		fmt(path, "%sAEInstaller\\packages\\%05u*", dir, num);
		h = FindFirstFileA(path, &fd);
		if (h != INVALID_HANDLE_VALUE) {
			FindClose(h);
			fmt(path, "%sAEInstaller\\packages\\%s\\Mod_Info.cfg", dir, fd.cFileName);
			if (file_text(path, cfg, sizeof cfg)) {
				cfg_value(cfg, "ModVersion", version, sizeof version);
				cfg_value(cfg, "NameOfMod", mod, sizeof mod);
			}
			LINE("package %05u: %s, version %s", num, mod[0] ? mod : fd.cFileName + 5, version);
		} else {
			LINE("package %05u: not in AEInstaller\\packages", num);
		}
	}
}

static void dump_text_file(const char *title, const char *name, int mask)
{
	char dir[MAX_PATH], path[MAX_PATH + 64];
	static char buf[32768];
	oni_dir(dir);
	fmt(path, "%sAE\\%s", dir, name);
	if (!file_text(path, buf, sizeof buf)) { fmt(path, "%s%s", dir, name); if (!file_text(path, buf, sizeof buf)) return; }
	if (mask) mask_addresses(buf);
	section(title);
	put(buf);
	line("");
}

static void dump_persist(void)
{
	char dir[MAX_PATH], path[MAX_PATH + 64], hexline[100];
	static u8 buf[4096];
	static const char hx[] = "0123456789abcdef";
	u32 n;
	oni_dir(dir);
	fmt(path, "%sAE\\persist.dat", dir);
	if (!(n = file_text(path, (char *)buf, sizeof buf))) { fmt(path, "%spersist.dat", dir); if (!(n = file_text(path, (char *)buf, sizeof buf))) return; }
	section("persist.dat (hex)");
	for (u32 o = 0; o < n; o += 32) {
		u32 k = 0;
		for (u32 i = o; i < n && i < o + 32; i++) { hexline[k++] = hx[buf[i] >> 4]; hexline[k++] = hx[buf[i] & 15]; }
		hexline[k] = 0;
		line(hexline);
	}
}

static void dump_modules(void)
{
	struct { u32 size, mid, pid, gcnt, pcnt; u8 *base; u32 bytes; void *h; char name[256], path[260]; } me;
	char dir[MAX_PATH], sys[MAX_PATH];
	HANDLE snap;
	u32 n = 0;
	if (!W.CreateToolhelp32Snapshot || !W.Module32First) return;
	oni_dir(dir);
	GetWindowsDirectoryA(sys, sizeof sys);
	section("modules");
	snap = W.CreateToolhelp32Snapshot(8 /* TH32CS_SNAPMODULE */, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	me.size = sizeof me;
	for (int ok = W.Module32First(snap, &me); ok && n < 400; ok = W.Module32Next(snap, &me), n++) {
		u32 stamp = 0;
		const char *kind = str_ieq_n(me.path, dir, str_len(dir)) ? "oni folder" : str_ieq_n(me.path, sys, str_len(sys)) ? "windows" : "elsewhere";
		const char *hook = "";
		if (me.base && *(u16 *)me.base == 0x5a4d) stamp = *(u32 *)(me.base + *(u32 *)(me.base + 0x3c) + 8);
		for (u32 i = 0; i < sizeof g_hooks / sizeof g_hooks[0]; i++)
			if (str_contains_i(me.name, g_hooks[i].module)) hook = g_hooks[i].what;
		if (ends_with_i(me.name, ".exe") && !hook[0]) hook = "";
		LINE("%s, %u bytes, built %08x, %s%s%s", me.name, me.bytes, stamp, kind, hook[0] ? " -- " : "", hook);
	}
	CloseHandle(snap);
}

static u32 WINAPI profile_thread(void *arg)
{
	Snap *s = arg;
	SYSTEMTIME_ t;
	HANDLE f;
	u32 w;
	char name[64];
	g_out = VirtualAlloc(0, OUT_MAX, 0x3000, 4);
	if (!g_out) goto done;
	g_len = 0;
	GetLocalTime(&t);
	GetPrivateProfileStringA("multiplayer", "name", "", name, sizeof name, ini_path());
	line("Arklite machine and setup dump");
	section("arklite");
	LINE("version: %s, source %08x", ARKLITE_VERSION, (u32)ARKLITE_SRC_HASH);
	LINE("player: %s", name[0] ? name : "(no name)");
	LINE("written: %04u-%02u-%02u %02u:%02u:%02u (%s)", t.year, t.month, t.day, t.hour, t.minute, t.second, s->why);
	LINE("session: %s, slot %d", s->session[0] ? s->session : "none", s->slot);
	dump_os();
	dump_cpu_memory();
	dump_gpus(s);
	dump_display(s);
	dump_process();
	dump_files();
	dump_packages();
	dump_text_file("daodan.ini", "daodan.ini", 0);
	dump_text_file("Arklite.ini (addresses masked)", "Arklite.ini", 1);
	dump_persist();
	dump_modules();
	f = CreateFileA(s->path, 0x40000000, 1, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (f != INVALID_HANDLE_VALUE) {
		WriteFile(f, g_out, g_len, &w, 0);
		CloseHandle(f);
	}
	VirtualFree(g_out, 0, 0x8000);
	g_out = 0;
done:
	VirtualFree(s, 0, 0x8000);
	__sync_lock_release(&g_busy);
	return 0;
}

/* ---- What the rest of Arklite calls ---------------------------------------------------------- */

/* the dump's path for a log's path: <log without .log>.profile.txt */
void sysinfo_path_for_log(const char *log, char *out, u32 size)
{
	u32 n = str_len(log);
	str_cpy(out, size, log);
	if (n > 4 && str_ieq_n(log + n - 4, ".log", 4)) out[n - 4] = 0;
	n = str_len(out);
	if (n + 13 < size) str_cpy(out + n, size - n, ".profile.txt");
}

const char *sysinfo_path(void)
{
	if (!g_profile_path[0]) sysinfo_path_for_log(log_path(), g_profile_path, sizeof g_profile_path);
	return g_profile_path;
}

/* main thread, with Oni's GL context: write the dump now (on a thread) */
void sysinfo_write(const char *why)
{
	Snap *s;
	if (__sync_lock_test_and_set(&g_busy, 1)) return;   /* one at a time */
	s = VirtualAlloc(0, sizeof *s, 0x3000, 4);
	if (!s) { __sync_lock_release(&g_busy); return; }
	str_cpy(s->why, sizeof s->why, why);
	str_cpy(s->path, sizeof s->path, sysinfo_path());
	snap_main(s);
	g_last_ms = GetTickCount();
	{
		HANDLE t = CreateThread(0, 0x40000, profile_thread, s, 0, 0);
		if (t) CloseHandle(t);
		else { VirtualFree(s, 0, 0x8000); __sync_lock_release(&g_busy); }
	}
	if (!g_last_session[0]) log_msg("sysinfo: machine and setup dump %s (%s)", sysinfo_path(), why);
	str_cpy(g_last_session, sizeof g_last_session, str_ieq_n(net_session_id(), "none", 5) ? "" : net_session_id());
}

/* every frame of a level (lockstep.c's update): the first frame, a new network session, every 10 minutes */
void sysinfo_frame(void)
{
	static int done_first;
	const char *sess = net_session_id();
	if (str_ieq_n(sess, "none", 5)) sess = "";
	if (!done_first) { done_first = 1; sysinfo_write("first level frame"); return; }
	if (sess[0] && !str_ieq_n(sess, g_last_session, sizeof g_last_session)) { sysinfo_write("new network session"); return; }
	if (GetTickCount() - g_last_ms >= PROFILE_EVERY_MS) sysinfo_write("every 10 minutes");
}
