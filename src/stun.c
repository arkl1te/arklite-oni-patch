/* This PC's internet address as the internet sees it (STUN, RFC 5389), for hole punching.
 *
 * Behind a router, and more so behind a provider's shared address (CGNAT), nobody outside can
 * send to this PC until it sends to them first: each side's NAT lets in only answers to what went
 * out. Hole punching uses that: both PCs send to each other's *public* address, and each NAT then
 * takes the other's packets for answers. Each PC needs its public address and port, as its NAT
 * maps the game's socket, so the question goes out on that very socket (net.c's g_sock): a STUN
 * Binding Request to a public STUN server ([net] stun, Google's by default; stun2 a second one
 * asked if the first doesn't answer; empty: none), whose answer names the address and port the
 * packet came from. Asked again every STUN_EVERY_MS: that also keeps the NAT's mapping open, and
 * a changed address is announced again (net.c).
 *
 * The user's line (2026-11-07): three STUN servers all saw one socket as the same public port
 * (endpoint-independent mapping), the kind of NAT hole punching works through.
 *
 * Name lookups block, so they run on a thread of their own (once per server); the questions go
 * out and the answers come in on the game's thread (stun_frame, and stun_packet from net.c's
 * receive loop). */
#include "arklite.h"

typedef struct { u16 family, port; u32 addr; u8 zero[8]; } SOCKADDR_IN;
typedef struct { char *name; char **aliases; short type, length; char **addrs; } HOSTENT;

__declspec(dllimport) int  WINAPI WSAStartup(u16, void *);
__declspec(dllimport) int  WINAPI sendto(u32, const void *, int, int, const void *, int);
__declspec(dllimport) u32  WINAPI inet_addr(const char *);
__declspec(dllimport) u16  WINAPI htons(u16);
__declspec(dllimport) u16  WINAPI ntohs(u16);
__declspec(dllimport) HOSTENT * WINAPI gethostbyname(const char *);
__declspec(dllimport) u32  WINAPI GetTickCount(void);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) int  WINAPI CloseHandle(HANDLE);
__declspec(dllimport) int  WINAPI QueryPerformanceCounter(unsigned long long *);

void *memcpy(void *, const void *, unsigned);
void *memset(void *, int, unsigned);

#define STUN_COOKIE     0x2112a442u
#define STUN_EVERY_MS   25000        /* asked again this often (keeps the mapping open too) */
#define STUN_RETRY_MS   1000         /* no answer yet: again this soon (the other server every second time) */
#define STUN_GIVE_UP    10           /* ...this many times, then only every STUN_EVERY_MS */
#define DEFAULT_STUN    "stun.l.google.com:19302"
#define DEFAULT_STUN2   "stun.cloudflare.com:3478"

static char g_names[2][128];
static SOCKADDR_IN g_server[2];
static volatile int g_resolved;        /* 0 not yet, 1 looking up, 2 done */
static HANDLE g_thread;
static u8 g_tid[12];                   /* the question waiting for its answer */
static u32 g_asked_at, g_tries, g_answer_at;
static u32 g_addr;                     /* network order, 0: not known */
static u16 g_port;                     /* network order */
static int g_changed;                  /* a new address: stun_take_change reports it once */
static int g_on = -1;
static u32 g_sock = 0xffffffffu;       /* the socket asked about: another one has its own mapping */

static int same(const u8 *a, const u8 *b, u32 n)
{
	for (u32 i = 0; i < n; i++)
		if (a[i] != b[i]) return 0;
	return 1;
}

static int resolve(const char *s, SOCKADDR_IN *out)
{
	char host[128];
	u32 n = 0, port = 3478;
	while (s[n] && s[n] != ':' && n + 1 < sizeof host) host[n] = s[n], n++;
	host[n] = 0;
	if (s[n] == ':') {
		port = 0;
		for (const char *c = s + n + 1; *c >= '0' && *c <= '9'; c++) port = port * 10 + (*c - '0');
	}
	if (!n || !port || port > 65535) return 0;
	memset(out, 0, sizeof *out);
	out->family = 2;
	out->port = htons((u16)port);
	out->addr = inet_addr(host);
	if (out->addr == 0xffffffffu) {
		HOSTENT *h = gethostbyname(host);
		if (!h || !h->addrs || !h->addrs[0]) return 0;
		memcpy(&out->addr, h->addrs[0], 4);
	}
	return 1;
}

static u32 WINAPI resolver(void *unused)
{
	u8 wsa[400];
	WSAStartup(0x0202, wsa);
	for (int i = 0; i < 2; i++)
		if (g_names[i][0] && !resolve(g_names[i], &g_server[i])) {
			log_msg("stun: can't find the STUN server %s", g_names[i]);
			memset(&g_server[i], 0, sizeof g_server[i]);
		}
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	g_resolved = 2;
	return 0;
}

static int enabled(void)
{
	if (g_on < 0) {
		GetPrivateProfileStringA("net", "stun", DEFAULT_STUN, g_names[0], sizeof g_names[0], ini_path());
		GetPrivateProfileStringA("net", "stun2", DEFAULT_STUN2, g_names[1], sizeof g_names[1], ini_path());
		g_on = g_names[0][0] || g_names[1][0];
		if (!g_on) log_msg("stun: off (Arklite.ini [net] stun and stun2 are empty): this PC won't know its internet address");
	}
	return g_on;
}

/* A new socket (or the same handle reused): its mapping is a new one, ask at once */
void stun_reset(void)
{
	g_addr = 0;
	g_port = 0;
	g_asked_at = g_tries = g_answer_at = 0;
	g_changed = 0;
}

static void ask(u32 sock, u32 now)
{
	u8 q[20];
	unsigned long long r = 0;
	int i = g_tries % 2;
	if (!g_server[i].addr) i ^= 1;
	if (!g_server[i].addr) return;
	QueryPerformanceCounter(&r);
	for (int k = 0; k < 12; k++) g_tid[k] = (u8)(r >> (k % 8 * 8)) ^ (u8)(now >> (k % 4 * 8)) ^ (u8)(k * 0x9d);
	q[0] = 0, q[1] = 1;                       /* Binding Request */
	q[2] = 0, q[3] = 0;                       /* no attributes */
	q[4] = 0x21, q[5] = 0x12, q[6] = 0xa4, q[7] = 0x42;
	memcpy(q + 8, g_tid, 12);
	sendto(sock, q, sizeof q, 0, &g_server[i], sizeof g_server[i]);
	g_asked_at = now ? now : 1;
	g_tries++;
}

/* Each frame the socket is open (the game's, or the Join list's that becomes the game's) */
void stun_frame(u32 sock, u32 now)
{
	if (!enabled()) return;
	if (sock != g_sock) {   /* another socket: another mapping */
		stun_reset();
		g_sock = sock;
	}
	if (!g_resolved) {
		g_resolved = 1;
		if (!(g_thread = CreateThread(0, 0, resolver, 0, 0, 0))) g_resolved = 2;
		return;
	}
	if (g_resolved != 2) return;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	u32 wait = g_answer_at ? STUN_EVERY_MS : g_tries < STUN_GIVE_UP ? STUN_RETRY_MS : STUN_EVERY_MS;
	if (g_asked_at && now - g_asked_at < wait) return;
	if (g_answer_at && now - g_answer_at < STUN_EVERY_MS) return;
	if (g_tries == STUN_GIVE_UP && !g_answer_at) log_msg("stun: no STUN server answers: this PC's internet address isn't known");
	ask(sock, now);
}

/* A packet on the game's socket: 1 if it was a STUN server's answer (taken here) */
int stun_packet(const u8 *p, u32 n, const void *from_)
{
	const SOCKADDR_IN *from = from_;
	u32 len, i;
	int ours = 0;
	for (int k = 0; k < 2; k++)
		if (g_server[k].addr && from->addr == g_server[k].addr && from->port == g_server[k].port) ours = 1;
	if (!ours || n < 20 || p[4] != 0x21 || p[5] != 0x12 || p[6] != 0xa4 || p[7] != 0x42) return 0;
	if (p[0] != 0x01 || p[1] != 0x01 || !same(p + 8, g_tid, 12)) return 1;   /* not the answer to our question */
	len = (u32)p[2] << 8 | p[3];
	if (20 + len > n) return 1;
	for (i = 20; i + 4 <= 20 + len;) {
		u32 type = (u32)p[i] << 8 | p[i + 1], alen = (u32)p[i + 2] << 8 | p[i + 3];
		const u8 *v = p + i + 4;
		if (i + 4 + alen > 20 + len) break;
		if ((type == 0x0020 || type == 0x0001) && alen >= 8 && v[1] == 0x01) {   /* (XOR-)MAPPED-ADDRESS, IPv4 */
			u8 a[4], pt[2];
			memcpy(pt, v + 2, 2);
			memcpy(a, v + 4, 4);
			if (type == 0x0020) {
				pt[0] ^= 0x21, pt[1] ^= 0x12;
				a[0] ^= 0x21, a[1] ^= 0x12, a[2] ^= 0xa4, a[3] ^= 0x42;
			}
			u32 addr;
			u16 port;
			memcpy(&addr, a, 4);
			memcpy(&port, pt, 2);
			if (addr != g_addr || port != g_port) {
				char t[32];
				g_addr = addr;
				g_port = port;
				g_changed = 1;
				stun_text(t);
				log_msg("stun: the internet sees this PC's game socket as %s", t);
			}
			g_answer_at = GetTickCount();
			if (!g_answer_at) g_answer_at = 1;
			g_tries = 0;
			return 1;
		}
		i += 4 + ((alen + 3) & ~3u);
	}
	return 1;
}

/* "a.b.c.d:port", or "" if not known */
int stun_text(char *out)
{
	const u8 *ip = (const u8 *)&g_addr;
	if (!g_addr) {
		out[0] = 0;
		return 0;
	}
	fmt(out, "%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], ntohs(g_port));
	return 1;
}

/* 1 once after the address became known or changed */
int stun_take_change(void)
{
	int c = g_changed;
	g_changed = 0;
	return c;
}
