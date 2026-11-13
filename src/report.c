/* Anonymous problem reports: when a network game goes out of sync, a player drops or Oni crashes,
 * Arklite sends what the developer needs to find out why to the Arklite helper (TCP, the
 * helper's port): this PC's log and, for a desync, the dumps net.c wrote (Arklite-desync-*.bin,
 * Arklite-desyncticks-*.bin) and the rollback check (Arklite-rollbackcheck_*.txt). Every PC of a
 * session tags its report with the same session ID (the level and the session's starting seeds,
 * net_session_id), so the host's and each joiner's reports of one desync end up side by side.
 *
 * Anonymous, made so on this PC before anything is sent (anonymize):
 *   - internet addresses (a.b.c.d, with any :port) become [address];
 *   - players' names become "player N" (N from net.c, from the log's own "names:" lines, or
 *     "[name]" when the player isn't known); the server's name becomes [server name];
 *   - chat messages are left out (the line stays, its text doesn't);
 *   - paths: the game's folder (Oni.exe's) becomes <Oni>\, Arklite.dll's folder <Arklite>\, and
 *     anything else absolute (a drive, \\server, /home...) is cut to [path]\ and the last part.
 * The dumps hold game memory only (the characters, inputs, RNG), no names or addresses. The helper
 * keeps no sender address. Arklite.ini [reports] send=0 turns reports off.
 *
 * When: the first DESYNC of a session, a player who stopped answering or the host lost (once a
 * session each), and a crash: the fault handler can't send, so it leaves logs\report-pending.txt
 * and the next start of Oni sends that log. Sending runs on its own thread; a failure is logged
 * and dropped (no retries, nothing piles up).
 *
 * Wire format (little-endian), one report per TCP connection: "AKRP", version 1, body length,
 * then the body: Arklite's version (32 bytes), its source hash, the reason, this PC's player slot,
 * the tick, the session ID (40 bytes), the number of files; per file its name (64 bytes, no
 * folder), raw and packed size, and the packed bytes (an LZ4 block). The helper answers "OK"
 * (4 bytes) or "NO". */
#include "arklite.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

typedef struct { u16 family, port; u32 addr; u8 zero[8]; } RSOCKADDR_IN;
typedef struct { char *name; char **aliases; short type, length; char **addrs; } RHOSTENT;
__declspec(dllimport) int  WINAPI WSAStartup(u16, void *);
__declspec(dllimport) u32  WINAPI socket(int, int, int);
__declspec(dllimport) int  WINAPI connect(u32, const void *, int);
__declspec(dllimport) int  WINAPI send(u32, const void *, int, int);
__declspec(dllimport) int  WINAPI recv(u32, void *, int, int);
__declspec(dllimport) int  WINAPI closesocket(u32);
__declspec(dllimport) int  WINAPI setsockopt(u32, int, int, const void *, int);
__declspec(dllimport) u32  WINAPI inet_addr(const char *);
__declspec(dllimport) u16  WINAPI htons(u16);
__declspec(dllimport) RHOSTENT * WINAPI gethostbyname(const char *);
__declspec(dllimport) int  WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) u32  WINAPI GetFileSize(HANDLE, u32 *);
__declspec(dllimport) int  WINAPI CloseHandle(HANDLE);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int  WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) u32  WINAPI GetModuleFileNameA(void *, char *, u32);
__declspec(dllimport) int  WINAPI DeleteFileA(const char *);

#define REPORT_MAGIC    0x50524b41u   /* "AKRP" */
#define REPORT_VERSION  1
#define MAX_FILES       6
#define FILE_MAX        (64u << 20)   /* bigger files aren't sent */
#define SEND_MAX        (12u << 20)   /* the packed report, at most */
#define DEFAULT_HELPER  "51.20.130.46:27780"
#define PENDING         LOG_DIR "\\report-pending.txt"
#define NAMES           8

static int g_on;
static char g_helper[96];
static struct { char dir[MAX_PATH]; const char *label; } g_prefix[2];   /* with the last backslash; longer first */
static u32 g_sent_mask;                                 /* reasons sent this session */
static char g_sent_session[40];

typedef struct {
	u32 reason, slot, tick;
	char session[40];
	int nfiles;
	char files[MAX_FILES][MAX_PATH];
	char names[NAMES][40];   /* nickname of player i+1 ("" unknown) */
	char local[40];          /* this PC's player's nickname */
	int local_slot;
	char lnames[24][40];     /* nicknames the log itself names (players who left or renamed too) */
	int lslot[24], nl;       /* their slots, -1 unknown */
} Job;

/* ---- LZ4 block ------------------------------------------------------------------------------ */

static u32 rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }

static u8 *lz4_len(u8 *o, u32 n)
{
	while (n >= 255) { *o++ = 255; n -= 255; }
	*o++ = (u8)n;
	return o;
}

/* LZ4 block format; dst holds at least n + n / 255 + 16. table: 1 << 16 u32s */
static u32 lz4_pack(const u8 *src, u32 n, u8 *dst, u32 *table)
{
	u8 *o = dst;
	u32 ip = 0, anchor = 0, mflimit = n > 12 ? n - 12 : 0, mlimit = n > 5 ? n - 5 : 0;

	memset(table, 0, 4u << 16);
	while (ip < mflimit) {
		u32 h = (rd32(src + ip) * 2654435761u) >> 16, ref = table[h];
		table[h] = ip;
		if (ref >= ip || ip - ref > 65535 || rd32(src + ref) != rd32(src + ip)) {
			ip++;
			continue;
		}
		u32 len = 4;
		while (ip + len < mlimit && src[ref + len] == src[ip + len]) len++;
		u32 lit = ip - anchor, ml = len - 4;
		*o++ = (u8)((lit < 15 ? lit : 15) << 4 | (ml < 15 ? ml : 15));
		if (lit >= 15) o = lz4_len(o, lit - 15);
		memcpy(o, src + anchor, lit);
		o += lit;
		*o++ = (u8)(ip - ref);
		*o++ = (u8)((ip - ref) >> 8);
		if (ml >= 15) o = lz4_len(o, ml - 15);
		ip += len;
		anchor = ip;
	}
	u32 lit = n - anchor;
	*o++ = (u8)((lit < 15 ? lit : 15) << 4);
	if (lit >= 15) o = lz4_len(o, lit - 15);
	memcpy(o, src + anchor, lit);
	o += lit;
	return (u32)(o - dst);
}

/* ---- Anonymizing a log ------------------------------------------------------------------------ */

static int alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int digit(char c) { return c >= '0' && c <= '9'; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int starts(const char *s, const char *p)
{
	while (*p) if (*s++ != *p++) return 0;
	return 1;
}

static const char *find_str(const char *s, const char *p)
{
	for (; *s; s++) if (starts(s, p)) return s;
	return 0;
}

/* "a.b.c.d" (1-3 digits each) at s: its length with any ":port", else 0 */
static u32 ip_at(const char *s)
{
	u32 i = 0;
	for (int part = 0; part < 4; part++) {
		u32 d = 0;
		while (digit(s[i]) && d < 4) { i++; d++; }
		if (!d || d > 3) return 0;
		if (part < 3) {
			if (s[i] != '.') return 0;
			i++;
		}
	}
	if (digit(s[i]) || (s[i] == '.' && digit(s[i + 1]))) return 0;
	if (s[i] == ':' && digit(s[i + 1])) {
		i++;
		while (digit(s[i])) i++;
	}
	return i;
}

/* The start of an absolute path at s (a drive, \\server, or a Unix home or root folder) */
static int abs_path_at(const char *s, const char *line)
{
	if (s > line && (alnum(s[-1]) || s[-1] == '\\' || s[-1] == '/')) return 0;
	if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':' && (s[2] == '\\' || s[2] == '/'))
		return 1;
	if (s[0] == '\\' && s[1] == '\\' && alnum(s[2])) return 1;
	static const char *const unix_roots[] = { "/home/", "/Users/", "/root/", "/mnt/", "/media/", "/run/", "/tmp/", "/var/", "/opt/", "/usr/" };
	for (u32 i = 0; i < sizeof unix_roots / sizeof unix_roots[0]; i++)
		if (starts(s, unix_roots[i])) return 1;
	return 0;
}

static int prefix_i(const char *s, const char *p)
{
	for (; *p; s++, p++) {
		char a = lower(*s), b = lower(*p);
		if (a == '/') a = '\\';
		if (b == '/') b = '\\';
		if (a != b) return 0;
	}
	return 1;
}

typedef struct { char *o; u32 left; } Out;

static void put(Out *w, const char *s, u32 n)
{
	if (n > w->left) n = w->left;
	memcpy(w->o, s, n);
	w->o += n;
	w->left -= n;
}

static void puts_(Out *w, const char *s) { put(w, s, str_len(s)); }

/* A name of names[] (or the local one) whole at s: its replacement, and its length in *n */
static const char *name_at(const Job *j, const char *s, const char *line, u32 *n, char *buf)
{
	if (s > line && alnum(s[-1])) return 0;
	for (int i = -1; i < NAMES + j->nl; i++) {
		const char *name = i < 0 ? j->local : i < NAMES ? j->names[i] : j->lnames[i - NAMES];
		u32 len = str_len(name);
		if (!len || !starts(s, name) || alnum(s[len])) continue;
		*n = len;
		int slot = i < 0 ? j->local_slot : i < NAMES ? i : j->lslot[i - NAMES];
		if (slot < 0) return "[name]";
		fmt(buf, "player %d", slot + 1);
		return buf;
	}
	return 0;
}

/* Learns names from the log's own lines: "names: player N is X", "net: X (player N) ..." */
static void learn_names(Job *j, const char *line)
{
	const char *p;
	char name[40];
	u32 n = 0;
	int slot = -1;

	if (starts(line, "names: player ") && digit(line[14])) {
		slot = line[14] - '1';
		p = find_str(line, " is ");
		if (!p) return;
		p += 4;
		while (p[n] && p[n] != '\r' && p[n] != '\n' && n < sizeof name - 1) { name[n] = p[n]; n++; }
	} else if (starts(line, "names: this PC's player is ")) {
		p = line + 27;
		if (starts(p, "now ")) p += 4;
		while (p[n] && p[n] != '\r' && p[n] != '\n' && n < sizeof name - 1) { name[n] = p[n]; n++; }
		name[n] = 0;
		{
			const char *cut = find_str(name, " (saved in Arklite.ini)");
			if (cut) name[cut - name] = 0, n = (u32)(cut - name);
		}
		slot = j->local_slot;
	} else if (starts(line, "net: ") && (p = find_str(line, " (player ")) && digit(p[9]) && p[10] == ')') {
		slot = p[9] - '1';
		n = (u32)(p - (line + 5));
		if (n >= sizeof name) return;
		memcpy(name, line + 5, n);
	} else {
		return;
	}
	name[n] = 0;
	if (!n || j->nl >= 24) return;
	for (u32 i = 0; i < n; i++) if (name[i] == '"' || name[i] == ':') return;
	if (starts(name, "Player ") || starts(name, "player ")) return;   /* the placeholders: no one's name */
	for (int i = 0; i < j->nl; i++) if (str_len(j->lnames[i]) == n && starts(j->lnames[i], name)) return;
	str_cpy(j->lnames[j->nl], sizeof j->lnames[j->nl], name);
	j->lslot[j->nl++] = slot;
}

/* One line (without its CRLF) into w, anonymized */
static void anonymize_line(const Job *j, const char *line, u32 len, Out *w)
{
	char l[1200], buf[24];
	u32 n = len < sizeof l - 1 ? len : sizeof l - 1;
	memcpy(l, line, n);
	l[n] = 0;

	/* chat: the messages players typed stay out (Arklite's own chat notices stay) */
	if (starts(l, "chat: (server) ")) {
		if (find_str(l, "Server: ")) { puts_(w, "chat: (server) [message left out]"); return; }
	} else if (starts(l, "chat: ")) {
		if (find_str(l + 6, ": ") || find_str(l, " whispers")) { puts_(w, "chat: [message left out]"); return; }
	}
	/* the server's name */
	if (starts(l, "net: hosting \"")) {
		const char *end = find_str(l + 14, "\" on ");
		if (end) {
			puts_(w, "net: hosting [server name]");
			const char *rest = end + 1;   /* " on UDP port ...": through the rules below */
			anonymize_line(j, rest, str_len(rest), w);
			return;
		}
	}
	for (const char *s = l; *s; ) {
		u32 k;
		const char *rep;
		if ((k = ip_at(s)) && (s == l || (!alnum(s[-1]) && s[-1] != '.'))) {
			puts_(w, "[address]");
			s += k;
			continue;
		}
		{
			int hit = 0;
			for (int i = 0; i < 2 && !hit; i++)
				if (g_prefix[i].dir[0] && prefix_i(s, g_prefix[i].dir)) {
					puts_(w, g_prefix[i].label);
					s += str_len(g_prefix[i].dir);
					hit = 1;
				}
			if (hit) continue;
		}
		if (abs_path_at(s, l)) {   /* up to the line's last separator: what's after it is the file */
			const char *last = 0;
			for (const char *t = s; *t; t++) if (*t == '\\' || *t == '/') last = t;
			puts_(w, "[path]\\");
			s = last ? last + 1 : s + str_len(s);
			continue;
		}
		if ((rep = name_at(j, s, l, &k, buf))) {
			puts_(w, rep);
			s += k;
			continue;
		}
		put(w, s, 1);
		s++;
	}
}

/* The whole text: returns its new length in out (size cap) */
static u32 anonymize(Job *j, const char *in, u32 n, char *out, u32 cap)
{
	Out w = { out, cap };
	/* the log's own names first: they hold players who left, too */
	for (u32 i = 0; i < n; ) {
		u32 e = i;
		while (e < n && in[e] != '\n') e++;
		char l[200];
		u32 m = e - i < sizeof l - 1 ? e - i : sizeof l - 1;
		memcpy(l, in + i, m);
		l[m] = 0;
		learn_names(j, l);
		i = e + 1;
	}
	for (u32 i = 0; i < n; ) {
		u32 e = i, end;
		while (e < n && in[e] != '\n') e++;
		end = e;
		if (end > i && in[end - 1] == '\r') end--;
		anonymize_line(j, in + i, end - i, &w);
		put(&w, "\r\n", 2);
		i = e + 1;
	}
	return cap - w.left;
}

/* ---- Sending ------------------------------------------------------------------------------- */

static int send_all(u32 s, const void *p, u32 n)
{
	const u8 *b = p;
	while (n) {
		int k = send(s, (const char *)b, n > 65536 ? 65536 : (int)n, 0);
		if (k <= 0) return 0;
		b += k;
		n -= (u32)k;
	}
	return 1;
}

static u32 resolve(u16 *port)
{
	char host[96];
	u32 i = 0, p = 27780, a;
	str_cpy(host, sizeof host, g_helper);
	while (host[i] && host[i] != ':') i++;
	if (host[i] == ':') {
		p = 0;
		for (const char *c = host + i + 1; digit(*c); c++) p = p * 10 + (u32)(*c - '0');
		host[i] = 0;
	}
	*port = htons((u16)(p && p < 65536 ? p : 27780));
	a = inet_addr(host);
	if (a == 0xffffffffu) {
		RHOSTENT *h = gethostbyname(host);
		a = h && h->addrs && h->addrs[0] ? *(u32 *)h->addrs[0] : 0;
	}
	return a;
}

static int text_file(const char *path)
{
	u32 n = str_len(path);
	return n > 4 && (str_ieq_n(path + n - 4, ".log", 4) || str_ieq_n(path + n - 4, ".txt", 4));
}

static const char *base_name(const char *p)
{
	const char *b = p;
	for (; *p; p++) if (*p == '\\' || *p == '/') b = p + 1;
	return b;
}

static const char *reason_name(u32 r)
{
	return r == REPORT_DESYNC ? "desync" : r == REPORT_CRASH ? "crash" : r == REPORT_DROP ? "drop" : "?";
}

static u32 WINAPI report_thread(void *arg)
{
	Job *j = arg;
	u8 *body = VirtualAlloc(0, SEND_MAX + 4096, 0x3000, 4), *raw = 0, *txt = 0;
	u32 *table = VirtualAlloc(0, 4u << 16, 0x3000, 4), len = 0, files = 0, packed_all = 0, raw_all = 0;
	u8 wsa[512];

	if (!body || !table) goto done;
	memset(body, 0, 0x60);
	str_cpy((char *)body, 32, ARKLITE_VERSION);
	u32 head[4] = { ARKLITE_SRC_HASH, j->reason, j->slot, j->tick };
	memcpy(body + 32, head, 16);
	str_cpy((char *)body + 48, 40, j->session);
	len = 32 + 16 + 40 + 4;
	for (int f = 0; f < j->nfiles; f++) {
		HANDLE h = CreateFileA(j->files[f], 0x80000000 /* GENERIC_READ */, 3 /* share read+write */, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
		u32 n, got = 0;
		if (h == INVALID_HANDLE_VALUE) continue;
		n = GetFileSize(h, 0);
		if (n == 0xffffffffu || n > FILE_MAX) { CloseHandle(h); continue; }
		raw = VirtualAlloc(0, n + 16, 0x3000, 4);
		if (raw && !ReadFile(h, raw, n, &got, 0)) got = 0;
		CloseHandle(h);
		if (!raw) continue;
		u8 *src = raw;
		u32 sn = got;
		if (text_file(j->files[f])) {   /* anonymized; may grow a little ([address], player N) */
			u32 cap = got * 2 + 4096;
			txt = VirtualAlloc(0, cap, 0x3000, 4);
			if (!txt) goto next;
			sn = anonymize(j, (const char *)raw, got, (char *)txt, cap);
			src = txt;
		}
		u32 bound = sn + sn / 255 + 16, packed;
		u8 *pk = VirtualAlloc(0, bound, 0x3000, 4);
		if (!pk) goto next;
		packed = lz4_pack(src, sn, pk, table);
		if (len + 72 + packed > SEND_MAX) {   /* doesn't fit: left out */
			log_msg("report: %s left out (%d KB packed: the report would be too big)", base_name(j->files[f]), packed / 1024);
			VirtualFree(pk, 0, 0x8000);
			goto next;
		}
		u8 *fh = body + len;
		memset(fh, 0, 72);
		str_cpy((char *)fh, 64, base_name(j->files[f]));
		memcpy(fh + 72, pk, packed);
		VirtualFree(pk, 0, 0x8000);
		memcpy(fh + 64, &sn, 4);
		memcpy(fh + 68, &packed, 4);
		len += 72 + packed;
		files++;
		packed_all += packed;
		raw_all += sn;
	next:
		if (txt) VirtualFree(txt, 0, 0x8000);
		if (raw) VirtualFree(raw, 0, 0x8000);
		txt = raw = 0;
	}
	memcpy(body + 32 + 16 + 40, &files, 4);
	if (!files) {
		log_msg("report: nothing to send (%s)", reason_name(j->reason));
		goto done;
	}
	{
		RSOCKADDR_IN a;
		u16 port;
		u32 s, timeout = 20000, hdr[3] = { REPORT_MAGIC, REPORT_VERSION, len };
		char ok[4] = { 0 };
		WSAStartup(0x0202, wsa);
		memset(&a, 0, sizeof a);
		a.family = 2;
		a.addr = resolve(&port);
		a.port = port;
		if (!a.addr) {
			log_msg("report: the helper %s can't be found: the %s report isn't sent", g_helper, reason_name(j->reason));
			goto done;
		}
		s = socket(2, 1 /* SOCK_STREAM */, 6);
		if (s == 0xffffffffu) goto done;
		setsockopt(s, 0xffff, 0x1005 /* SO_SNDTIMEO */, &timeout, 4);
		setsockopt(s, 0xffff, 0x1006 /* SO_RCVTIMEO */, &timeout, 4);
		if (connect(s, &a, sizeof a) || !send_all(s, hdr, sizeof hdr) || !send_all(s, body, len) ||
		    recv(s, ok, 4, 0) < 2 || ok[0] != 'O' || ok[1] != 'K')
			log_msg("report: the %s report (%d files, %d KB) couldn't be sent", reason_name(j->reason), files, len / 1024);
		else
			log_msg("report: sent an anonymous %s report: %d file(s), %d KB packed from %d KB", reason_name(j->reason), files,
			        (packed_all + 1023) / 1024, (raw_all + 1023) / 1024);
		closesocket(s);
	}
done:
	if (body) VirtualFree(body, 0, 0x8000);
	if (table) VirtualFree(table, 0, 0x8000);
	VirtualFree(j, 0, 0x8000);
	return 0;
}

/* ---- What the rest of Arklite calls ---------------------------------------------------------- */

static Job *job_new(u32 reason)
{
	Job *j;
	if (!g_on) return 0;
	j = VirtualAlloc(0, sizeof *j, 0x3000, 4);
	if (!j) return 0;
	j->reason = reason;
	j->local_slot = -1;
	for (int i = 0; i < NAMES && i < PLAYERS_MAX; i++) {
		const char *n = mp_name(i);
		if (n) text_to_utf8(j->names[i], sizeof j->names[i], n);   /* as the log has them (common.c log_msg) */
	}
	if (mp_name_local()) text_to_utf8(j->local, sizeof j->local, mp_name_local());
	return j;
}

static void job_add(Job *j, const char *path)
{
	if (path && path[0] && j->nfiles < MAX_FILES) str_cpy(j->files[j->nfiles++], MAX_PATH, path);
}

static void job_go(Job *j)
{
	HANDLE t = CreateThread(0, 0x40000, report_thread, j, 0, 0);
	if (t) CloseHandle(t);
	else VirtualFree(j, 0, 0x8000);
}

/* net.c: a reason's report for this session (once each), with the dumps it wrote */
void report_net(u32 reason, const char *session, int slot, u32 tick, const char *dump1, const char *dump2, const char *extra)
{
	Job *j;
	if (!g_on) return;
	if (!str_ieq_n(session, g_sent_session, sizeof g_sent_session)) {
		str_cpy(g_sent_session, sizeof g_sent_session, session);
		g_sent_mask = 0;
	}
	if (g_sent_mask & (1u << reason)) return;
	g_sent_mask |= 1u << reason;
	if (!(j = job_new(reason))) return;
	j->slot = (u32)slot;
	j->tick = tick;
	j->local_slot = slot;
	str_cpy(j->session, sizeof j->session, session);
	job_add(j, log_path());
	job_add(j, dump1);
	job_add(j, dump2);
	job_add(j, extra);
	job_go(j);
}

/* crash_log.c's fault handler: this log is sent at the next start (no threads or sockets now) */
void report_crash(void)
{
	char line[MAX_PATH + 64];
	u32 w;
	HANDLE f;
	if (!g_on) return;
	f = CreateFileA(PENDING, 0x40000000, 0, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return;
	fmt(line, "%s\r\n%s\r\n%d\r\n", log_path(), net_session_id(), net_local_slot());
	WriteFile(f, line, str_len(line), &w, 0);
	CloseHandle(f);
}

static void send_pending(void)
{
	char text[MAX_PATH + 64], *lines[3] = { 0 };
	u32 got = 0, k = 0;
	HANDLE f = CreateFileA(PENDING, 0x80000000, 1, 0, 3, 0x80, 0);
	Job *j;
	if (f == INVALID_HANDLE_VALUE) return;
	ReadFile(f, text, sizeof text - 1, &got, 0);
	CloseHandle(f);
	DeleteFileA(PENDING);
	text[got] = 0;
	lines[k++] = text;
	for (char *c = text; *c && k < 3; c++)
		if (*c == '\n') { *c = 0; if (c > text && c[-1] == '\r') c[-1] = 0; lines[k++] = c + 1; }
	if (k < 3 || !(j = job_new(REPORT_CRASH))) return;
	for (char *c = lines[2]; *c; c++) if (*c == '\r' || *c == '\n') *c = 0;
	{
		int v = 0, neg = lines[2][0] == '-';
		for (const char *c = lines[2] + neg; digit(*c); c++) v = v * 10 + (*c - '0');
		j->local_slot = neg ? -1 : v;
		j->slot = (u32)j->local_slot;
	}
	str_cpy(j->session, sizeof j->session, lines[1]);
	job_add(j, lines[0]);
	log_msg("report: the last run crashed: sending its log (%s)", lines[0]);
	job_go(j);
}

void report_install(void *instance)
{
	char path[MAX_PATH];
	u32 n;

	g_on = GetPrivateProfileIntA("reports", "send", 1, ini_path()) != 0;
	GetPrivateProfileStringA("net", "helper", DEFAULT_HELPER, g_helper, sizeof g_helper, ini_path());
	if (!g_helper[0]) g_on = 0;
	for (int i = 0; i < 2; i++) {   /* Oni.exe's folder and Arklite.dll's */
		n = GetModuleFileNameA(i ? instance : 0, path, sizeof path);
		while (n && path[n - 1] != '\\' && path[n - 1] != '/') n--;
		path[n] = 0;
		str_cpy(g_prefix[i].dir, sizeof g_prefix[i].dir, path);
		g_prefix[i].label = i ? "<Arklite>\\" : "<Oni>\\";
	}
	if (str_len(g_prefix[0].dir) < str_len(g_prefix[1].dir)) {   /* the longer first: one may hold the other */
		char t[MAX_PATH];
		const char *l = g_prefix[0].label;
		str_cpy(t, sizeof t, g_prefix[0].dir);
		str_cpy(g_prefix[0].dir, sizeof g_prefix[0].dir, g_prefix[1].dir);
		str_cpy(g_prefix[1].dir, sizeof g_prefix[1].dir, t);
		g_prefix[0].label = g_prefix[1].label;
		g_prefix[1].label = l;
	}
	if (!g_on) {
		log_msg("report: problem reports off (Arklite.ini [reports] send=0, or no helper)");
		return;
	}
	log_msg("report: anonymous problem reports on (desyncs, dropped players, crashes; [reports] send=0 turns them off)");
	send_pending();
}
