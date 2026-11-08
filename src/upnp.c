/* UPnP port forwarding for a host (Arklite.ini [net] upnp, 1 by default).
 *
 * Players on the internet reach a host only if its router forwards the game's UDP port to it.
 * Most home routers do that on request (UPnP IGD: MiniUPnPd on AsusWRT, among others), so when
 * this PC hosts, a thread of its own asks:
 *   1. SSDP: "M-SEARCH" for an InternetGatewayDevice, to 239.255.255.250:1900 and to the default
 *      gateway (iphlpapi GetBestRoute) straight, three times over 3 s; the answers' LOCATION is the
 *      router's description (http://ip:port/rootDesc.xml).
 *   2. HTTP GET of the description: the WANIPConnection (or WANPPPConnection) service's type and
 *      control URL. The address this PC reached the router from (getsockname) is the one to
 *      forward to.
 *   3. SOAP AddPortMapping: UDP <port> on the router's internet side to this PC's <port>, for
 *      LEASE_S (renewed every LEASE_S / 2 while hosting); a router that only takes permanent
 *      mappings (error 725) gets lease 0. Error 718: the port goes to another PC already.
 *   4. GetExternalIPAddress: the address players on the internet join. A private or shared one
 *      (10/8, 172.16/12, 192.168/16, 100.64/10: the provider's NAT) means nobody outside can.
 * The outcome is a line in the host's chat (upnp_take_message, read by net.c on the game's thread)
 * and in the log. When hosting stops, DeletePortMapping (upnp_stop waits up to STOP_WAIT_MS). A
 * mapping left by a crash lapses with its lease. Nothing here touches the game: the thread only
 * talks to the router, with blocking sockets and timeouts. */
#include "arklite.h"

typedef struct { u16 family, port; u32 addr; u8 zero[8]; } SOCKADDR_IN;
typedef struct { char *name; char **aliases; short type, length; char **addrs; } HOSTENT;

__declspec(dllimport) int  WINAPI WSAStartup(u16, void *);
__declspec(dllimport) int  WINAPI WSAGetLastError(void);
__declspec(dllimport) u32  WINAPI socket(int, int, int);
__declspec(dllimport) int  WINAPI bind(u32, const void *, int);
__declspec(dllimport) int  WINAPI connect(u32, const void *, int);
__declspec(dllimport) int  WINAPI send(u32, const void *, int, int);
__declspec(dllimport) int  WINAPI recv(u32, void *, int, int);
__declspec(dllimport) int  WINAPI sendto(u32, const void *, int, int, const void *, int);
__declspec(dllimport) int  WINAPI recvfrom(u32, void *, int, int, void *, int *);
__declspec(dllimport) int  WINAPI getsockname(u32, void *, int *);
__declspec(dllimport) int  WINAPI setsockopt(u32, int, int, const void *, int);
__declspec(dllimport) int  WINAPI closesocket(u32);
__declspec(dllimport) u32  WINAPI inet_addr(const char *);
__declspec(dllimport) u16  WINAPI htons(u16);
__declspec(dllimport) HOSTENT * WINAPI gethostbyname(const char *);
__declspec(dllimport) u32  WINAPI GetTickCount(void);
__declspec(dllimport) void WINAPI Sleep(u32);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) int  WINAPI CloseHandle(HANDLE);
__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);

void *memcpy(void *, const void *, unsigned);
void *memset(void *, int, unsigned);

#define INVALID_SOCKET  0xffffffffu
#define SOL_SOCKET      0xffff
#define SO_SNDTIMEO     0x1005
#define SO_RCVTIMEO     0x1006
#define TIMEOUT_MS      3000
#define SSDP_TRIES      3
#define LEASE_S         3600
#define STOP_WAIT_MS    2000
#define MAX_ROUTERS     4
#define DESCRIPTION     "Arklite (Oni multiplayer)"

static HANDLE g_thread;
static volatile int g_running, g_stop;
static u16 g_port;
static char g_msg[CHAT_MAX];
static volatile int g_msg_ready;

/* The router's control point, from its description */
static u32 g_ctl_addr;                 /* network order */
static u16 g_ctl_port;                 /* host order */
static char g_ctl_path[256], g_service[96];
static u32 g_local;                    /* this PC's address towards the router, network order */
static int g_mapped;                   /* AddPortMapping took it */

static void say(const char *text)
{
	log_msg("upnp: %s", text);
	str_cpy(g_msg, sizeof g_msg, text);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	g_msg_ready = 1;
}

const char *upnp_take_message(void)
{
	static char out[CHAT_MAX];
	if (!g_msg_ready) return 0;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	str_cpy(out, sizeof out, g_msg);
	g_msg_ready = 0;
	return out;
}

static void ip_text(char *out, u32 a)
{
	const u8 *ip = (const u8 *)&a;
	fmt(out, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

/* An address players on the internet can't reach: private, or the provider's shared NAT */
static int private_ip(u32 a)
{
	const u8 *ip = (const u8 *)&a;
	return ip[0] == 10 || ip[0] == 127 || ip[0] == 0 || (ip[0] == 172 && (ip[1] & 0xf0) == 16) ||
	       (ip[0] == 192 && ip[1] == 168) || (ip[0] == 100 && (ip[1] & 0xc0) == 64) || (ip[0] == 169 && ip[1] == 254);
}

static const char *find(const char *s, const char *sub)
{
	unsigned n = str_len(sub);
	for (; *s; s++) {
		unsigned i = 0;
		while (i < n && s[i] == sub[i]) i++;
		if (i == n) return s;
	}
	return 0;
}

static const char *find_i(const char *s, const char *sub)
{
	unsigned n = str_len(sub);
	for (; *s; s++)
		if (str_ieq_n(s, sub, n)) return s;
	return 0;
}

/* The text of the first <tag>...</ after from, trimmed; 0 if there's none */
static int tag_text(const char *from, const char *tag, char *out, u32 size)
{
	char open[64];
	const char *a, *b;
	fmt(open, "<%s>", tag);
	if (!(a = find(from, open))) return 0;
	a += str_len(open);
	while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n') a++;
	if (!(b = find(a, "</"))) return 0;
	while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n')) b--;
	if ((u32)(b - a) >= size) return 0;
	memcpy(out, a, b - a);
	out[b - a] = 0;
	return 1;
}

static void cat(char *d, u32 size, const char *s)
{
	u32 n = str_len(d);
	str_cpy(d + n, size - n, s);
}

static void timeouts(u32 sock)
{
	u32 ms = TIMEOUT_MS;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ms, 4);
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &ms, 4);
}

/* http://host[:port]/path into its parts (host as an address); 0 if it isn't one */
static int parse_url(const char *url, u32 *addr, u16 *port, char *path, u32 path_size)
{
	char host[128];
	u32 n = 0, p = 80;
	if (!str_ieq_n(url, "http://", 7)) return 0;
	url += 7;
	while (*url && *url != ':' && *url != '/' && n + 1 < sizeof host) host[n++] = *url++;
	host[n] = 0;
	if (*url == ':') {
		p = 0;
		for (url++; *url >= '0' && *url <= '9'; url++) p = p * 10 + (*url - '0');
	}
	if (!n || !p || p > 65535) return 0;
	str_cpy(path, path_size, *url == '/' ? url : "/");
	*port = (u16)p;
	*addr = inet_addr(host);
	if (*addr == 0xffffffffu) {
		HOSTENT *h = gethostbyname(host);
		if (!h || !h->addrs || !h->addrs[0]) return 0;
		memcpy(addr, h->addrs[0], 4);
	}
	return 1;
}

/* One HTTP exchange (Connection: close); the whole answer, 0-terminated, in out. Its length, or 0 */
static u32 http(u32 addr, u16 port, const char *request, u32 len, char *out, u32 size)
{
	SOCKADDR_IN to, me;
	int mlen = sizeof me;
	u32 sock, got = 0;
	int n;

	sock = socket(2, 1 /* SOCK_STREAM */, 6 /* IPPROTO_TCP */);
	if (sock == INVALID_SOCKET) return 0;
	timeouts(sock);
	memset(&to, 0, sizeof to);
	to.family = 2;
	to.addr = addr;
	to.port = htons(port);
	if (connect(sock, &to, sizeof to)) {
		closesocket(sock);
		return 0;
	}
	memset(&me, 0, sizeof me);
	if (!getsockname(sock, &me, &mlen) && me.addr) g_local = me.addr;
	for (u32 sent = 0; sent < len; sent += (u32)n)
		if ((n = send(sock, request + sent, (int)(len - sent), 0)) <= 0) {
			closesocket(sock);
			return 0;
		}
	while (got + 1 < size && (n = recv(sock, out + got, (int)(size - 1 - got), 0)) > 0) got += (u32)n;
	out[got] = 0;
	closesocket(sock);
	return got;
}

static int http_status(const char *resp)
{
	int code = 0;
	const char *s = find(resp, " ");
	if (!str_ieq_n(resp, "HTTP/", 5) || !s) return 0;
	for (s++; *s >= '0' && *s <= '9'; s++) code = code * 10 + (*s - '0');
	return code;
}

/* A SOAP call to the router's WAN connection service: the HTTP status (200 fine), *upnp_error the
 * UPnP errorCode of a fault (500) */
static int soap(const char *action, const char *args, char *resp, u32 size, int *upnp_error)
{
	static char body[1400], req[2048];
	char line[600], ip[16];
	int code;

	*upnp_error = 0;
	body[0] = 0;
	cat(body, sizeof body, "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	                       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>");
	fmt(line, "<u:%s xmlns:u=\"%s\">", action, g_service);
	cat(body, sizeof body, line);
	cat(body, sizeof body, args);
	fmt(line, "</u:%s></s:Body></s:Envelope>\r\n", action);
	cat(body, sizeof body, line);
	ip_text(ip, g_ctl_addr);
	fmt(req, "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
	         "SOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
	    g_ctl_path, ip, g_ctl_port, g_service, action, str_len(body));
	cat(req, sizeof req, body);
	if (!http(g_ctl_addr, g_ctl_port, req, str_len(req), resp, size)) return 0;
	code = http_status(resp);
	if (code != 200) {
		char e[16];
		if (tag_text(resp, "errorCode", e, sizeof e))
			for (const char *c = e; *c >= '0' && *c <= '9'; c++) *upnp_error = *upnp_error * 10 + (*c - '0');
	}
	return code;
}

/* The description at url: its WAN connection service and control URL. 1 if it has one */
static int read_description(const char *url)
{
	static char resp[32768];
	char path[256], req[600], ip[16], ctl[256], base[256], type[96];
	const char *svc = 0;
	u32 addr, n = 0;
	u16 port;

	if (!parse_url(url, &addr, &port, path, sizeof path)) return 0;
	ip_text(ip, addr);
	fmt(req, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", path, ip, port);
	if (!http(addr, port, req, str_len(req), resp, sizeof resp) || http_status(resp) != 200) {
		log_msg("upnp: no description at %s", url);
		return 0;
	}
	static const char *const kinds[] = { "urn:schemas-upnp-org:service:WANIPConnection:",
	                                     "urn:schemas-upnp-org:service:WANPPPConnection:" };
	for (u32 i = 0; i < 2 && !svc; i++) svc = find(resp, kinds[i]);
	if (!svc) {
		log_msg("upnp: %s has no WAN connection service (not a router, or UPnP is off)", url);
		return 0;
	}
	while (svc[n] && svc[n] != '<' && svc[n] > ' ' && n + 1 < sizeof type) {   /* its whole type, with the version */
		type[n] = svc[n];
		n++;
	}
	type[n] = 0;
	if (!tag_text(svc, "controlURL", ctl, sizeof ctl)) {
		log_msg("upnp: %s names no control URL", url);
		return 0;
	}
	str_cpy(g_service, sizeof g_service, type);
	if (str_ieq_n(ctl, "http://", 7)) {
		if (!parse_url(ctl, &g_ctl_addr, &g_ctl_port, g_ctl_path, sizeof g_ctl_path)) return 0;
	} else {
		if (tag_text(resp, "URLBase", base, sizeof base))   /* relative to it; without one, to the description's */
			parse_url(base, &addr, &port, path, sizeof path);
		g_ctl_addr = addr;
		g_ctl_port = port;
		g_ctl_path[0] = 0;
		if (ctl[0] != '/') cat(g_ctl_path, sizeof g_ctl_path, "/");
		cat(g_ctl_path, sizeof g_ctl_path, ctl);
	}
	ip_text(ip, g_ctl_addr);
	log_msg("upnp: router %s:%d, %s at %s", ip, g_ctl_port, g_service, g_ctl_path);
	return 1;
}

/* The default gateway (iphlpapi GetBestRoute towards the internet), network order; 0 if unknown */
static u32 gateway(void)
{
	typedef u32 (WINAPI *BestRoute)(u32, u32, u32 *);
	HANDLE lib = LoadLibraryA("iphlpapi.dll");
	BestRoute best = lib ? (BestRoute)GetProcAddress(lib, "GetBestRoute") : 0;
	u32 row[14];
	memset(row, 0, sizeof row);
	if (!best || best(0x08080808u /* 8.8.8.8 */, 0, row)) return 0;
	return row[3];   /* dwForwardNextHop */
}

/* SSDP: the routers' description URLs; how many */
static int discover(char urls[MAX_ROUTERS][256])
{
	static const char *const targets[] = { "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
	                                       "urn:schemas-upnp-org:device:InternetGatewayDevice:2" };
	static char buf[2048];
	char msg[256];
	SOCKADDR_IN to, local, from;
	u32 sock, gw = gateway(), ms = 1000;
	int nurls = 0;

	sock = socket(2, 2, 17);
	if (sock == INVALID_SOCKET) return 0;
	memset(&local, 0, sizeof local);
	local.family = 2;
	bind(sock, &local, sizeof local);
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ms, 4);
	for (int round = 0; round < SSDP_TRIES && !g_stop && !nurls; round++) {
		u32 until;
		for (u32 t = 0; t < 2; t++) {
			fmt(msg, "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: %s\r\n\r\n",
			    targets[t]);
			memset(&to, 0, sizeof to);
			to.family = 2;
			to.port = htons(1900);
			to.addr = 0xfaffffefu;   /* 239.255.255.250 */
			sendto(sock, msg, str_len(msg), 0, &to, sizeof to);
			if (gw) {   /* straight to the gateway too: its answer comes from where it was asked */
				to.addr = gw;
				sendto(sock, msg, str_len(msg), 0, &to, sizeof to);
			}
		}
		until = GetTickCount() + 1000;
		while ((int)(until - GetTickCount()) > 0 && !g_stop) {
			int len = sizeof from, n = recvfrom(sock, buf, sizeof buf - 1, 0, &from, &len);
			const char *loc;
			char url[256];
			u32 k = 0;
			if (n <= 0) break;
			buf[n] = 0;
			if (!(loc = find_i(buf, "\nLOCATION:"))) continue;
			for (loc += 10; *loc == ' '; loc++) {}
			while (loc[k] && loc[k] != '\r' && loc[k] != '\n' && k + 1 < sizeof url) { url[k] = loc[k]; k++; }
			url[k] = 0;
			int seen = 0;
			for (int i = 0; i < nurls; i++) seen |= str_ieq_n(urls[i], url, 256);
			if (!seen && nurls < MAX_ROUTERS) str_cpy(urls[nurls++], 256, url);
		}
	}
	closesocket(sock);
	return nurls;
}

static int add_mapping(u32 lease, int *err)
{
	static char resp[8192];
	char args[700], ip[16];
	ip_text(ip, g_local);
	fmt(args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>"
	          "<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
	          "<NewPortMappingDescription>" DESCRIPTION "</NewPortMappingDescription><NewLeaseDuration>%d</NewLeaseDuration>",
	    g_port, g_port, ip, lease);
	return soap("AddPortMapping", args, resp, sizeof resp, err) == 200;
}

/* Who the router forwards the port to now (network order), 0 if nobody or unknown */
static u32 mapped_to(void)
{
	static char resp[8192];
	char args[300], client[32];
	int err;
	fmt(args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>", g_port);
	if (soap("GetSpecificPortMappingEntry", args, resp, sizeof resp, &err) != 200) return 0;
	if (!tag_text(resp, "NewInternalClient", client, sizeof client)) return 0;
	u32 a = inet_addr(client);
	return a == 0xffffffffu ? 0 : a;
}

static void delete_mapping(void)
{
	static char resp[8192];
	char args[300];
	int err, code;
	fmt(args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>", g_port);
	code = soap("DeletePortMapping", args, resp, sizeof resp, &err);
	if (code == 200) log_msg("upnp: the router no longer forwards UDP port %d", g_port);
	else log_msg("upnp: the router didn't remove the forwarding of UDP port %d (HTTP %d, error %d)", g_port, code, err);
}

/* The rest of the join address: ":port" unless it's the default */
static void port_text(char *out)
{
	if (g_port == NET_DEFAULT_PORT) out[0] = 0;
	else fmt(out, ":%d", g_port);
}

static u32 WINAPI upnp_thread(void *unused)
{
	static char urls[MAX_ROUTERS][256], resp[8192];
	char text[CHAT_MAX + 64], ip[16], ext[32], other[16], port[8];
	u32 lease = LEASE_S, renewed = 0;
	int n, err = 0, found = 0;
	u8 wsa[400];

	WSAStartup(0x0202, wsa);
	port_text(port);
	n = discover(urls);
	for (int i = 0; i < n && !found && !g_stop; i++) found = read_description(urls[i]);
	if (g_stop) goto out;
	if (!found) {
		fmt(text, "UPnP: no router answered. For players on the internet, forward UDP port %d to this PC by hand", g_port);
		say(text);
		goto out;
	}
	if (!add_mapping(lease, &err) && err == 725) {   /* OnlyPermanentLeasesSupported */
		lease = 0;
		add_mapping(lease, &err);
	}
	if (err) {
		u32 to = err == 718 ? mapped_to() : 0;   /* ConflictInMappingEntry */
		if (to && to != g_local) {
			ip_text(other, to);
			fmt(text, "UPnP: UDP port %d already goes to %s on your network, so players on the internet reach that PC", g_port,
			    other);
		} else {
			fmt(text, "UPnP: the router refused (error %d). For players on the internet, forward UDP port %d by hand", err,
			    g_port);
		}
		say(text);
		goto out;
	}
	g_mapped = 1;
	renewed = GetTickCount();
	ip_text(ip, g_local);
	log_msg("upnp: the router forwards UDP port %d to %s (lease %d s)", g_port, ip, lease);
	ext[0] = 0;
	if (soap("GetExternalIPAddress", "", resp, sizeof resp, &err) == 200) tag_text(resp, "NewExternalIPAddress", ext, sizeof ext);
	u32 ea = ext[0] ? inet_addr(ext) : 0xffffffffu;
	if (ea == 0xffffffffu || !ea)
		fmt(text, "UPnP: the router forwards UDP port %d to this PC; it didn't say its internet address", g_port);
	else if (private_ip(ea))
		fmt(text, "UPnP: no use, your provider shares the router's address %s: outside players need /punch", ext);
	else
		fmt(text, "UPnP: the router forwards UDP port %d here. Players on the internet join at %s%s", g_port, ext, port);
	say(text);

	while (!g_stop) {   /* renewed while hosting */
		Sleep(100);
		if (lease && GetTickCount() - renewed >= lease * 1000 / 2) {
			if (!add_mapping(lease, &err)) log_msg("upnp: renewing the forwarding failed (error %d)", err);
			renewed = GetTickCount();
		}
	}
	delete_mapping();
	g_mapped = 0;
out:
	g_running = 0;
	return 0;
}

void upnp_start(u16 port)
{
	if (!GetPrivateProfileIntA("net", "upnp", 1, ini_path())) {
		log_msg("upnp: off (Arklite.ini [net] upnp=0)");
		return;
	}
	if (g_running && port == g_port) return;   /* asked already for this port */
	upnp_stop();
	if (g_running) {
		log_msg("upnp: the last request is still under way: not asking for port %d", port);
		return;
	}
	g_port = port;
	g_stop = 0;
	g_mapped = 0;
	g_running = 1;
	if (g_thread) CloseHandle(g_thread);
	g_thread = CreateThread(0, 0, upnp_thread, 0, 0, 0);
	if (!g_thread) {
		g_running = 0;
		log_msg("upnp: no thread, not asking the router");
		return;
	}
	log_msg("upnp: asking the router to forward UDP port %d to this PC", port);
}

void upnp_stop(void)
{
	u32 began = GetTickCount();
	if (!g_running) return;
	g_stop = 1;
	while (g_running && GetTickCount() - began < STOP_WAIT_MS) Sleep(10);
	if (g_running) log_msg("upnp: the router didn't answer in time; the forwarding lapses with its lease");
}
