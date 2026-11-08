/* The Arklite helper: a server list and introductions on a public machine (arklite/helper/).
 *
 * A host behind its provider's shared address (CGNAT) can't be reached by anyone it didn't send to
 * first (see net.c "Hole punching"). The helper is reachable by everyone, so:
 *   - The host sends it BEAT every BEAT_MS from the game's socket: what its game is. That keeps the
 *     host's NAT open toward the helper, and the helper lists the game under the address and port
 *     the packets came from: the host's internet mapping, the one players send to.
 *   - The Join list asks LIST (from its own socket, which becomes the game's on Play) and shows them.
 *   - A player asks INTRO_REQ with the host's address; the helper sends the host INTRO with the
 *     player's, and the host knocks toward it (net.c), opening its NAT for that player: /punch
 *     without anyone typing. INTRO_ACK gives the player the host's real port (a CGNAT host's public
 *     port is random; a player who typed only the IP gets it).
 * Every request carries the key the helper gave its address (COOKIE), so the helper never answers
 * an address that didn't ask. Packets from anywhere but the helper's address, or whose key isn't
 * the one the helper gave this socket, are dropped.
 *
 * [net] helper (default DEFAULT_HELPER; empty: none). [host] listed=0 keeps a game off the list
 * (introductions still work). The name is looked up on a thread of its own, once.
 *
 * Each socket has its own NAT mapping and so its own key: the state is kept per socket (the Join
 * list's and the game's may both be open while a host's pause menu shows the Join list). */
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

void *memcpy(void *, const void *, unsigned);
void *memset(void *, int, unsigned);

#define DEFAULT_HELPER  "51.20.130.46:27780"
#define HMAGIC          0x50484b41u   /* "AKHP" */
#define HVERSION        1
enum { H_COOKIE = 1, H_BEAT, H_BEAT_ACK, H_LIST_REQ, H_LIST, H_INTRO_REQ, H_INTRO_ACK, H_INTRO };
#define BEAT_MS         15000        /* host: what its game is, this often (also keeps its NAT open toward the helper) */
#define BEAT_RETRY_MS   2000         /* ...until the helper answers */
#define SILENT_MS       10000        /* no answer this long: the helper is down (said once) */
#define KEY_ASK_MS      1000         /* no key yet: asked again this soon */
#define LIST_PAGE       6
#define LIST_MAX        64
#define ACKS_MAX        16
#define CONNS           2
#define INTROS_MAX      8

#pragma pack(push, 1)
typedef struct { u32 magic; u8 version, type; u16 proto; u32 key; } HHead;
typedef struct { u32 server_id, flags, level, players, max_players, spectators, max_spectators, in_match;
                 char name[48], folder[64], map[48]; } HGame;
typedef struct { u32 addr; u16 port, pad; } HAddr;
typedef struct { u32 addr; u16 port, proto; u32 server_id, level, players, max_players, spectators, max_spectators, in_match;
                 char name[48], folder[64], map[48]; } HEntry;
typedef struct { HHead h; HGame g; } PktBeat;
typedef struct { HHead h; u32 listed; HAddr a; } PktBeatAck;
typedef struct { HHead h; u32 page; } PktListReq;
typedef struct { HHead h; u32 total, page, count; HEntry e[LIST_PAGE]; } PktList;
typedef struct { HHead h; HAddr a; } PktIntroReq;
typedef struct { HHead h; u32 status; HAddr asked, a; } PktIntroAck;
typedef struct { HHead h; HAddr a; } PktIntro;
#pragma pack(pop)

typedef char check_entry[sizeof(HEntry) == 196 ? 1 : -1];
typedef char check_game[sizeof(HGame) == 192 ? 1 : -1];

static char g_name[128];
static SOCKADDR_IN g_helper;
static volatile int g_resolved;        /* 0 not yet, 1 looking up, 2 done (g_helper.addr 0: none) */
static int g_on = -1, g_listed = 1;
static u16 g_proto;

typedef struct {
	u32 sock;                          /* 0xffffffff: unused */
	u32 key, key_asked, used_at;
	u32 beat_at, beat_ack_at, first_beat;
	int listed;                        /* the helper's last BEAT_ACK: 1 on the list, 0 refused (full) */
	int told_silent;
	u32 list_page, list_total;         /* LIST pages being asked for */
	int list_logged;                   /* the count the log last said (-1: none yet) */
	int list_wanted;
	HAddr intro[INTROS_MAX];           /* INTRO_REQs waiting for the key */
	u32 nintro;
} Conn;
static Conn g_conn[CONNS] = { { 0xffffffffu }, { 0xffffffffu } };

/* what came, until taken */
static NetServerInfo g_list[LIST_MAX];
static u32 g_list_n, g_list_taken, g_list_serial;
static struct { u32 sock, asked_addr; u16 asked_port; int status; u32 addr; u16 port; } g_acks[ACKS_MAX];
static u32 g_acks_n;                   /* (per socket: the Join list's and a joining game's both ask) */
static SOCKADDR_IN g_intros[8];
static u32 g_intros_n;
static int g_listed_news = -1;         /* host: 1 listed, 0 refused, 2 silent: said once by net.c */

static u32 WINAPI resolver(void *unused)
{
	u8 wsa[400];
	char host[128];
	u32 n = 0, port = 27780;
	WSAStartup(0x0202, wsa);
	while (g_name[n] && g_name[n] != ':' && n + 1 < sizeof host) host[n] = g_name[n], n++;
	host[n] = 0;
	if (g_name[n] == ':') {
		port = 0;
		for (const char *c = g_name + n + 1; *c >= '0' && *c <= '9'; c++) port = port * 10 + (*c - '0');
	}
	memset(&g_helper, 0, sizeof g_helper);
	if (n && port && port < 65536) {
		g_helper.family = 2;
		g_helper.port = htons((u16)port);
		g_helper.addr = inet_addr(host);
		if (g_helper.addr == 0xffffffffu) {
			HOSTENT *h = gethostbyname(host);
			g_helper.addr = 0;
			if (h && h->addrs && h->addrs[0]) memcpy(&g_helper.addr, h->addrs[0], 4);
		}
	}
	if (!g_helper.addr) log_msg("helper: can't find the helper %s: no server list or introductions", g_name);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	g_resolved = 2;
	return 0;
}

static int ready(void)
{
	if (g_on < 0) {
		GetPrivateProfileStringA("net", "helper", DEFAULT_HELPER, g_name, sizeof g_name, ini_path());
		g_listed = GetPrivateProfileIntA("host", "listed", 1, ini_path()) != 0;
		g_on = g_name[0] != 0;
		if (!g_on) log_msg("helper: off (Arklite.ini [net] helper is empty): no server list or introductions");
	}
	if (!g_on) return 0;
	if (!g_resolved) {
		g_resolved = 1;
		if (!CreateThread(0, 0, resolver, 0, 0, 0)) g_resolved = 2;
		return 0;
	}
	if (g_resolved != 2) return 0;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	return g_helper.addr != 0;
}

/* 1: [net] helper names one (found or still being looked up) */
int helper_on(void)
{
	ready();
	return g_on > 0 && (g_resolved != 2 || g_helper.addr);
}

static Conn *conn(u32 sock, int make)
{
	Conn *old = &g_conn[0];
	for (int i = 0; i < CONNS; i++)
		if (g_conn[i].sock == sock) return &g_conn[i];
	if (!make) return 0;
	for (int i = 1; i < CONNS; i++)
		if (g_conn[i].used_at < old->used_at) old = &g_conn[i];
	memset(old, 0, sizeof *old);
	old->sock = sock;
	old->used_at = GetTickCount();
	old->listed = -1;
	old->list_logged = -1;
	return old;
}

void helper_set_proto(u16 proto) { g_proto = proto; }

static void send_h(Conn *c, void *p, u32 n, u8 type)
{
	HHead *h = p;
	h->magic = HMAGIC;
	h->version = HVERSION;
	h->type = type;
	h->proto = g_proto;
	h->key = c->key;
	sendto(c->sock, p, (int)n, 0, &g_helper, sizeof g_helper);
}

/* no key yet: any request brings it (a 12-byte COOKIE back) */
static int keyed(Conn *c, u32 now)
{
	if (c->key) return 1;
	if (!c->key_asked || now - c->key_asked >= KEY_ASK_MS) {
		PktListReq q;
		memset(&q, 0, sizeof q);
		send_h(c, &q, sizeof q, H_LIST_REQ);
		c->key_asked = now ? now : 1;
	}
	return 0;
}

static void send_intro_reqs(Conn *c)
{
	for (u32 i = 0; i < c->nintro; i++) {
		PktIntroReq q;
		memset(&q, 0, sizeof q);
		q.a = c->intro[i];
		send_h(c, &q, sizeof q, H_INTRO_REQ);
	}
	c->nintro = 0;
}

static void send_list_req(Conn *c)
{
	PktListReq q;
	memset(&q, 0, sizeof q);
	q.page = c->list_page;
	send_h(c, &q, sizeof q, H_LIST_REQ);
	c->list_wanted = 0;
}

/* Each frame the socket is open */
void helper_frame(u32 sock, u32 now)
{
	Conn *c;
	if (!ready()) return;
	c = conn(sock, 1);
	c->used_at = now;
	if (!keyed(c, now)) return;
	if (c->nintro) send_intro_reqs(c);
	if (c->list_wanted) send_list_req(c);
}

/* host: a BEAT is due (then helper_beat) */
int helper_beat_due(u32 sock, u32 now)
{
	Conn *c;
	if (!ready() || !(c = conn(sock, 0)) || !c->key) return 0;
	if (!c->first_beat) c->first_beat = now ? now : 1;
	if (!c->told_silent && !c->beat_ack_at && now - c->first_beat >= SILENT_MS) {
		c->told_silent = 1;
		g_listed_news = 2;
		log_msg("helper: %s doesn't answer: the game isn't on the server list", g_name);
	}
	return !c->beat_at || now - c->beat_at >= (c->beat_ack_at && c->listed >= 0 ? BEAT_MS : BEAT_RETRY_MS);
}

void helper_beat(u32 sock, const NetServerInfo *g)
{
	Conn *c = conn(sock, 0);
	PktBeat p;
	if (!c) return;
	memset(&p, 0, sizeof p);
	p.g.server_id = g->server_id;
	p.g.flags = g_listed ? 0 : 1;
	p.g.level = g->level;
	p.g.players = g->players;
	p.g.max_players = g->max_players;
	p.g.spectators = g->spectators;
	p.g.max_spectators = g->max_spectators;
	p.g.in_match = g->in_match;
	str_cpy(p.g.name, sizeof p.g.name, g->name);
	str_cpy(p.g.folder, sizeof p.g.folder, g->folder);
	str_cpy(p.g.map, sizeof p.g.map, g->map);
	send_h(c, &p, sizeof p, H_BEAT);
	c->beat_at = GetTickCount();
	if (!c->beat_at) c->beat_at = 1;
}

/* host: 1 on the list, 0 refused, 2 the helper is silent, 3 introductions only ([host] listed=0); once */
int helper_take_news(void)
{
	int n = g_listed_news;
	g_listed_news = -1;
	if (n == 1 && !g_listed) n = 3;
	return n;
}

/* The Join list: the games on the list, from the first page */
void helper_list(u32 sock)
{
	Conn *c;
	if (!ready()) return;
	c = conn(sock, 1);
	c->list_page = 0;
	c->list_wanted = 1;
	g_list_n = g_list_taken = 0;
	g_list_serial++;
	helper_frame(sock, GetTickCount());
}

/* the next game the list brought (out->from_addr/from_port its address); 0: none */
int helper_list_next(NetServerInfo *out)
{
	if (g_list_taken >= g_list_n) return 0;
	*out = g_list[g_list_taken++];
	return 1;
}

/* Introduce this socket to the host at addr:port (network order) */
void helper_intro(u32 sock, u32 addr, u16 port)
{
	Conn *c;
	if (!ready()) return;
	c = conn(sock, 1);
	for (u32 i = 0; i < c->nintro; i++)
		if (c->intro[i].addr == addr && c->intro[i].port == port) return;
	if (c->nintro >= INTROS_MAX) return;
	c->intro[c->nintro].addr = addr;
	c->intro[c->nintro].port = port;
	c->intro[c->nintro].pad = 0;
	c->nintro++;
	helper_frame(sock, GetTickCount());
}

/* An INTRO_ACK: the host asked about, whether the helper knows it, and its real address; 0: none */
int helper_intro_ack(u32 sock, u32 *asked_addr, u16 *asked_port, int *known, u32 *addr, u16 *port)
{
	for (u32 k = 0; k < g_acks_n; k++) {
		if (g_acks[k].sock != sock) continue;
		*asked_addr = g_acks[k].asked_addr;
		*asked_port = g_acks[k].asked_port;
		*known = g_acks[k].status;
		*addr = g_acks[k].addr;
		*port = g_acks[k].port;
		g_acks_n--;
		for (u32 i = k; i < g_acks_n; i++) g_acks[i] = g_acks[i + 1];
		return 1;
	}
	return 0;
}

/* host: a player the helper introduced (knock toward it); 0: none */
int helper_take_intro(u32 *addr, u16 *port)
{
	if (!g_intros_n) return 0;
	g_intros_n--;
	*addr = g_intros[g_intros_n].addr;
	*port = g_intros[g_intros_n].port;
	return 1;
}

/* A packet on sock: 1 if it was the helper's (taken here) */
int helper_packet(u32 sock, const u8 *p, u32 n, const void *from_)
{
	const SOCKADDR_IN *from = from_;
	const HHead *h = (const HHead *)p;
	Conn *c;

	if (g_resolved != 2 || !g_helper.addr || from->addr != g_helper.addr || from->port != g_helper.port) return 0;
	if (n < sizeof *h || h->magic != HMAGIC || h->version != HVERSION || !(c = conn(sock, 0))) return 1;
	if (h->type == H_COOKIE) {
		if (h->key && h->key != c->key) {
			if (c->key) log_msg("helper: a new key (the helper restarted?)");
			c->key = h->key;
			c->beat_at = 0;   /* a host's BEAT goes again at once, with it */
		}
		return 1;
	}
	if (!c->key || h->key != c->key) return 1;   /* not the helper's answer to this socket */
	if (h->type == H_BEAT_ACK && n >= sizeof(PktBeatAck)) {
		const PktBeatAck *a = (const PktBeatAck *)p;
		int was = c->listed;
		c->beat_ack_at = GetTickCount();
		c->listed = a->listed != 0;
		if (c->listed != was) {
			const u8 *ip = (const u8 *)&a->a.addr;
			log_msg(c->listed ? "helper: the game is on the server list as %d.%d.%d.%d:%d%s" : "helper: the server list is full (%d.%d.%d.%d:%d)%s",
			        ip[0], ip[1], ip[2], ip[3], ntohs(a->a.port), g_listed ? "" : " (hidden: [host] listed=0)");
			g_listed_news = c->listed;
		}
		c->told_silent = 0;
	} else if (h->type == H_LIST && n >= 12 + sizeof(HHead)) {
		const PktList *l = (const PktList *)p;
		u32 count = l->count;
		if (l->page != c->list_page) return 1;   /* an old answer */
		if (count > LIST_PAGE || n < sizeof(HHead) + 12 + count * sizeof(HEntry)) return 1;
		for (u32 i = 0; i < count && g_list_n < LIST_MAX; i++) {
			const HEntry *e = &l->e[i];
			NetServerInfo *o = &g_list[g_list_n++];
			memset(o, 0, sizeof *o);
			o->same_version = 1;
			o->from_addr = e->addr;
			o->from_port = e->port;
			o->server_id = e->server_id;
			o->level = e->level;
			o->players = e->players;
			o->max_players = e->max_players;
			o->spectators = e->spectators;
			o->max_spectators = e->max_spectators;
			o->in_match = e->in_match;
			memcpy(o->name, e->name, sizeof o->name);
			memcpy(o->folder, e->folder, sizeof o->folder);
			memcpy(o->map, e->map, sizeof o->map);
			o->name[sizeof o->name - 1] = o->folder[sizeof o->folder - 1] = o->map[sizeof o->map - 1] = 0;
			for (char *s = o->name; *s; s++)
				if ((u8)*s < 0x20 || *s == 0x7f) *s = ' ';
		}
		if (!l->page && (int)l->total != c->list_logged) {
			log_msg("helper: the server list has %d game%s of this version", l->total, l->total == 1 ? "" : "s");
			c->list_logged = (int)l->total;
		}
		c->list_total = l->total;
		if ((c->list_page + 1) * LIST_PAGE < l->total && g_list_n < LIST_MAX) {   /* the next page */
			c->list_page++;
			send_list_req(c);
		}
	} else if (h->type == H_INTRO_ACK && n >= sizeof(PktIntroAck) && g_acks_n < ACKS_MAX) {
		const PktIntroAck *a = (const PktIntroAck *)p;
		g_acks[g_acks_n].sock = sock;
		g_acks[g_acks_n].asked_addr = a->asked.addr;
		g_acks[g_acks_n].asked_port = a->asked.port;
		g_acks[g_acks_n].status = a->status != 0;
		g_acks[g_acks_n].addr = a->a.addr;
		g_acks[g_acks_n].port = a->a.port;
		g_acks_n++;
	} else if (h->type == H_INTRO && n >= sizeof(PktIntro) && g_intros_n < sizeof g_intros / sizeof g_intros[0]) {
		const PktIntro *a = (const PktIntro *)p;
		memset(&g_intros[g_intros_n], 0, sizeof g_intros[0]);
		g_intros[g_intros_n].family = 2;
		g_intros[g_intros_n].addr = a->a.addr;
		g_intros[g_intros_n].port = a->a.port;
		g_intros_n++;
	}
	return 1;
}

/* the socket closed: its key and state go */
void helper_closed(u32 sock)
{
	Conn *c = conn(sock, 0);
	if (c) memset(c, 0, sizeof *c), c->sock = 0xffffffffu;
	for (u32 k = 0; k < g_acks_n;)
		if (g_acks[k].sock == sock) {
			g_acks_n--;
			for (u32 i = k; i < g_acks_n; i++) g_acks[i] = g_acks[i + 1];
		} else
			k++;
}
