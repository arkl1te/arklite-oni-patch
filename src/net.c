/* Multiplayer step 3: two PCs over UDP, deterministic lockstep with input delay (no rollback).
 *
 *   ARKLITE_HOST=[port]       host: Konoko is this PC's player (port 27777 by default)
 *   ARKLITE_JOIN=host[:port]  join: player 2 (the second Player-flagged character) is this PC's
 *
 * Both PCs play the same map (ARKLITE_AUTOSTART=<folder>, or Custom Game). A level starts in the
 * lobby: each PC plays its own player alone (warm-up; the other player stands), and F3 toggles
 * "ready" (shown on screen by mp_hud.c). The joiner says HELLO (phase 0, its ready flag) every
 * 200 ms; the host answers LOBBY: how many players there are, how many are ready, the countdown.
 * When at least two players are there and 51% of them are ready, the host counts down 7 s (back
 * to the lobby if that stops being true); at 0 it sends LOBBY with "go" and both PCs reload the
 * level (ONrGame_StartLevel 0x4beb40, what Load Game does from the pause menu), so the match
 * starts from the same state on both: joining never interrupts the host's game, and nobody
 * needs the other's game state.
 *
 * After the reload each PC stops the clock (server time = game tick, so no tick runs) until the
 * other answers: the joiner sends HELLO (phase 1), the host answers START with its RNG seeds
 * (game RNG 0x5326d4 and lockstep.c's tick-local stream), which both put in place right before
 * the first tick. HELLO and START also carry a hash of the level-start state, the start tick,
 * the x87 control word and the game files; a difference is logged. The score (players.c) starts
 * at 0 with the match.
 *
 * Ticks are numbered from the session's start. Each frame, for every tick the clock wants
 * (server time - game tick, what Daodan's DDrActionBuffer_Get computes before it hands over the
 * frame's record), this PC makes its input for tick (ticks run + delay): the frame's record for
 * the first, the same buttons without aim for the rest (what a tick with no record does). Daodan
 * only returns a record when a tick is due and keeps merging input until then, so a record that
 * can't go out yet (this PC already `delay + 6` ticks ahead) is merged into the next one. Every
 * packet carries the inputs from the peer's acknowledged tick on (at most 24), so a lost packet
 * is resent by the next. A tick runs once both PCs' inputs for it are here: the update gets
 * Konoko's records as its action records (tick i, record i) and player 2's through
 * players_net_frame. When the peer has inputs further ahead than our delay, this PC is behind
 * and runs one tick more than its clock asks (at most 6 per frame, as stock).
 *
 * Records are cleaned the same way on both PCs: action bits 0-2 (escape, console, pausescreen;
 * 0x4faec0 opens the pause menu from bit 0 inside the tick, which would open it on every PC)
 * are cleared. Esc still opens the menu through utility input, which reads this PC's keys. The
 * invert-look setting (0x5ec70f) is per PC, so it's applied to this PC's aim before sending and
 * is off while the ticks run.
 *
 * Desync check: every 60 ticks each PC hashes the game state (both RNG streams and, for every
 * character in use, its flags, health, position, facing and velocity: no pointers, which differ
 * between PCs) and sends it with its inputs. Flag 0x4000 isn't hashed: the draw pass (0x4e1540)
 * sets it on the characters this PC drew. A different hash logs DESYNC with this PC's
 * character summary for that tick and writes every character's full image for that tick
 * (Arklite-desync-host.bin / -join.bin; tools/desync_diff.py compares the two), without the
 * fields each PC's drawing writes (flag 0x4000, +0x163c frames off screen, +0x1660 distance to
 * the camera, active +0x221c..+0x2225 detail level).
 *
 * Each PC sees its own player: on the joiner the camera (gs+0x80, mode +0 == 1 follows the
 * character at +0x54; updated by 0x4d5ca0, called at 0x4fb141 per tick and 0x4fb170 per frame)
 * and the sound listener (0x4e33e0/0x4e3460 position, 0x4e3640 facing, called with gs+0xac at
 * 0x4fb1f4, 0x4fb1fb, 0x4fb20f) follow player 2, and the whole game display (0x4fb480: HUD,
 * laser sight, crosshair) draws with player 2 in the player slot.
 *
 * Testing on one PC: Oni refuses a second instance (FindWindowA at 0x50f687, je at 0x50f68f),
 * so with networking on that check is skipped. The joiner's log is Arklite-join.log.
 *
 * If no packet arrives for 10 s during the game, the peer is lost: the game goes on with the
 * peer's player standing still. Arklite.ini [net]: port (27777), delay (4 ticks; the host's is
 * used on both PCs), host=1 or join=<ip>[:port] when the environment doesn't say, and record
 * (1: each PC records the game, every tick, to Arklite-net-host.akr / -join.akr, lockstep.c's
 * format; tools/akr_diff.py compares the two and names the first tick and field that differ). */
#include "arklite.h"

typedef unsigned long long u64;

#define ONgGameState            (*(u8 **)0x5ece7c)
#define GS_CAMERA(gs)           (*(u8 **)((gs) + 0x80))
#define GS_PLAYER(gs)           (*(u8 **)((gs) + 0xac))
#define GS_SYNC_BEHIND(gs)      (*(u32 *)((gs) + 0x44))
#define GS_TICK(gs)             (*(u32 *)((gs) + 0x144))
#define GS_SERVER_TIME(gs)      (*(u32 *)((gs) + 0x148))
#define GS_CHARACTERS(gs)       ((gs) + 0x1260)
#define ONgInvertLook           (*(u8 *)0x5ec70f)
#define ONgCharacterSlots       (*(u16 *)(0x4dac95 + 3))
#define ONrLevel_GetCurrent     ((u16 (ONI_FC *)(void))0x5054c0)
#define ONrGameState_Update     ((u32 (ONI_FC *)(u32, void *, u32 *))0x4fafb0)

#define CAM_TARGET(c)           (*(u8 **)((c) + 0x54))
#define ONrCamera_Update        ((void (ONI_FC *)(u32, u32, u32))0x4d5ca0)
#define ONrListener_Position    ((void (ONI_FC *)(u8 *, void *))0x4e3460)
#define ONrListener_PositionAlt ((void (ONI_FC *)(u8 *, void *))0x4e33e0)
#define ONrListener_Facing      ((void (ONI_FC *)(u8 *, void *))0x4e3640)
#define ONcSingleInstanceJe     0x50f68f
#define ONrCharacter_GetActive  ((u8 *(ONI_FC *)(u8 *))0x4f1180)

#define CHR_SIZE        0x16a0
#define CHR_INUSE       0x8000
#define CHR_DRAWN       0x4000   /* flags: drawn this frame (draw pass 0x4e1540), per PC */
#define ACTIVE_SIZE     0x2838
#define GS_ACTIVE_STOCK 0xb6268
#define IMG_SIZE        (CHR_SIZE + ACTIVE_SIZE)
#define IMG_CHARS       64        /* characters kept per state hash for the desync dump */

#define DEFAULT_PORT    27777
#define PROTO           3
#define MAGIC           0x544e4b41   /* "AKNT" */
#define RING            512          /* input slots, by tick */
#define MAX_SEND        24           /* inputs per packet */
#define MAX_AHEAD       6            /* inputs made past the delay before merging */
#define HASH_EVERY      60
#define HASH_SLOTS      8
#define MAX_CHARS       255
#define LOST_MS         10000
#define HELLO_MS        200
#define RESEND_MS       30
#define COUNTDOWN_MS    7000
#define VK_F3           0x72

void *memcpy(void *, const void *, unsigned);
void *memset(void *, int, unsigned);

/* ---- Winsock ------------------------------------------------------------------------------ */

typedef struct { u16 family, port; u32 addr; u8 zero[8]; } SOCKADDR_IN;
typedef struct { char *name; char **aliases; short type, length; char **addrs; } HOSTENT;

__declspec(dllimport) int  WINAPI WSAStartup(u16, void *);
__declspec(dllimport) int  WINAPI WSAGetLastError(void);
__declspec(dllimport) u32  WINAPI socket(int, int, int);
__declspec(dllimport) int  WINAPI bind(u32, const void *, int);
__declspec(dllimport) int  WINAPI sendto(u32, const void *, int, int, const void *, int);
__declspec(dllimport) int  WINAPI recvfrom(u32, void *, int, int, void *, int *);
__declspec(dllimport) int  WINAPI ioctlsocket(u32, u32, u32 *);
__declspec(dllimport) int  WINAPI closesocket(u32);
__declspec(dllimport) u32  WINAPI inet_addr(const char *);
__declspec(dllimport) u16  WINAPI htons(u16);
__declspec(dllimport) u16  WINAPI ntohs(u16);
__declspec(dllimport) HOSTENT * WINAPI gethostbyname(const char *);
__declspec(dllimport) u32  WINAPI GetTickCount(void);
__declspec(dllimport) int  WINAPI CloseHandle(HANDLE);
__declspec(dllimport) int  WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) u32  WINAPI GetPrivateProfileSectionA(const char *, char *, u32, const char *);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32  WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32  WINAPI GetCurrentProcessId(void);

#define INVALID_SOCKET  0xffffffffu
#define FIONBIO         0x8004667e

/* ---- Packets ------------------------------------------------------------------------------ */

enum { PKT_HELLO = 1, PKT_START, PKT_INPUT, PKT_BYE, PKT_LOBBY };

typedef struct { u32 magic; u16 proto; u8 type, pad; } PktHead;
#define NFILES 4
typedef struct {
	PktHead h;
	u32 level, start_tick, state_hash, fpu_cw, delay;
	char version[16];
	u32 files[NFILES];
	u32 phase;             /* 0: in the lobby, 1: waiting for the match to start */
	u32 ready;             /* the joiner pressed F3 */
} PktHello;
typedef struct {
	PktHead h;
	u32 level, players, ready, countdown_ms;   /* countdown_ms 0xffffffff: none */
	u32 go;                /* the match starts: reload the level */
	u32 joiner_ready;      /* what the host knows of the joiner's ready flag */
} PktLobby;
typedef struct {
	PktHead h;
	u32 level, start_tick, state_hash, fpu_cw, seed, local_seed, delay;
	char version[16];
	u32 files[NFILES];
} PktStart;
typedef struct {
	PktHead h;
	u32 first, count;      /* inputs for ticks first..first+count-1 follow */
	u32 ack;               /* every input of ours before this tick arrived */
	u32 hash_tick, hash;   /* our latest state hash (hash_tick 0: none yet) */
} PktInput;

/* ---- State -------------------------------------------------------------------------------- */

enum { NET_OFF, NET_HOST, NET_JOIN };
enum { S_IDLE, S_LOBBY, S_WAIT, S_RUN, S_LOST };

static int g_mode, g_state;
static u32 g_sock = INVALID_SOCKET;
static SOCKADDR_IN g_peer;
static int g_have_peer;
static u16 g_port = DEFAULT_PORT;
static u32 g_delay = 4;
static int g_record = 1;               /* [net] record: Arklite-net-host.akr / -join.akr */

static u32 g_level, g_last_tick;
static u32 g_ticks;                    /* ticks run this session */
static u32 g_gen;                      /* our inputs are made for ticks < g_gen */
static u32 g_remote;                   /* the peer's inputs are here for ticks < g_remote */
static u32 g_peer_ack;                 /* the peer has our inputs for ticks < g_peer_ack */
static u32 g_local_in[RING][12], g_remote_in[RING][12];
static u32 g_remote_at[RING];          /* tick + 1 of the input in the slot, 0: none */
static u32 g_pending[12], g_last_local[12];
static int g_has_pending;
static u32 g_sent_gen;

static u32 g_seed, g_local_seed;       /* the host's, put in place before the first tick */
static u32 g_start_hash, g_start_tick, g_start_fpu;
static u32 g_last_recv, g_last_send, g_last_hello;
static u32 g_frame_p1[6][12], g_frame_p2[6][12];

typedef struct { u32 index, flags; int health; float pos[3], facing, vel[3]; } CharSum;
static struct { u32 tick, hash, n; CharSum chars[MAX_CHARS]; } g_mine[HASH_SLOTS];
static struct { u32 tick, hash; } g_theirs[HASH_SLOTS];
static u32 g_checked[HASH_SLOTS];        /* the tick each slot was last compared at */
static u32 g_last_hash_tick, g_last_hash;
static u32 g_checks, g_bad_checks, g_first_bad;
/* Per hash slot, the full image of each character in use (character, then active data), for
 * the dump written at the first desync: Arklite-desync-host.bin / -join.bin */
typedef struct { u32 index, pad; u8 img[IMG_SIZE]; } CharImage;
static CharImage (*g_images)[IMG_CHARS];
static u32 g_nimages[HASH_SLOTS];
static u8 *g_cam_set;                  /* the character the joiner's camera was pointed at */
static u32 g_npackets_sent, g_npackets_recv, g_ninputs_recv, g_last_status, g_send_errors;

/* The lobby */
static int g_match_pending;            /* the level was reloaded for the match: start synced */
static int g_ready, g_f3_down;         /* this PC's player pressed F3 */
static int g_peer_seen, g_peer_ready;  /* host: a joiner is in the lobby, and its ready flag */
static u32 g_countdown_end;            /* host: GetTickCount at 0, 0: no countdown */
static u32 g_lobby_players = 1, g_lobby_ready, g_lobby_countdown = 0xffffffffu, g_lobby_rx;   /* joiner: the host's view */
static int g_host_heard;               /* joiner: a LOBBY arrived in this lobby */
static u32 g_last_lobby;

/* What both PCs must have the same of besides Oni.exe: Daodan patches Oni's code and has
 * gameplay settings, and level0 holds the characters and animations */
static const char *const g_file_names[NFILES] = {
	"binkw32.dll (Daodan)", "daodan.ini [gameplay] and [devmode]",
	"GameDataFolder\\level0_Final.dat", "GameDataFolder\\level0_Final.raw",
};
static u32 g_files[NFILES];

static u32 hash_words(u32 h, const u8 *p, u32 n)
{
	u32 i = 0;
	for (; i + 4 <= n; i += 4) h = (h ^ *(const u32 *)(p + i)) * 0x01000193u;
	for (; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
	return h;
}

static u32 hash_file(const char *path)
{
	static u8 *buf;
	HANDLE f = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 1, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
	u32 h = 0x811c9dc5u, got;

	if (f == INVALID_HANDLE_VALUE) return 0;
	if (!buf) buf = VirtualAlloc(0, 1 << 20, 0x3000, 4);
	while (buf && ReadFile(f, buf, 1 << 20, &got, 0) && got) h = hash_words(h, buf, got);
	CloseHandle(f);
	return h;
}

static void hash_files(void)
{
	static char section[8192];
	char ini[MAX_PATH];
	u32 n;

	g_files[0] = hash_file("binkw32.dll");
	if (!GetFullPathNameA("daodan.ini", sizeof ini, ini, 0)) str_cpy(ini, sizeof ini, "daodan.ini");
	g_files[1] = 0x811c9dc5u;
	n = GetPrivateProfileSectionA("gameplay", section, sizeof section, ini);
	g_files[1] = hash_words(g_files[1], (const u8 *)section, n);
	n = GetPrivateProfileSectionA("devmode", section, sizeof section, ini);
	g_files[1] = hash_words(g_files[1] ^ 1, (const u8 *)section, n);
	g_files[2] = hash_file("GameDataFolder\\level0_Final.dat");
	g_files[3] = hash_file("GameDataFolder\\level0_Final.raw");
	log_msg("net: Daodan %08x, its settings %08x, level0 %08x %08x", g_files[0], g_files[1], g_files[2], g_files[3]);
}

static const char *mode_name(void) { return g_mode == NET_HOST ? "host" : "join"; }

int net_active(void) { return g_mode != NET_OFF; }

/* ---- State hash --------------------------------------------------------------------------- */

static u32 fnv(u32 h, const void *p, u32 n)
{
	const u8 *b = p;
	while (n--) h = (h ^ *b++) * 0x01000193u;
	return h;
}

static u32 fpu_control_word(void)
{
	u16 cw;
	__asm__ volatile("fnstcw %0" : "=m"(cw));
	return cw;
}

static u32 summarize(CharSum *c, u32 *count)
{
	u8 *chr = GS_CHARACTERS(ONgGameState);
	u32 slots = ONgCharacterSlots, n = 0, seeds[2];
	u32 h = 0x811c9dc5u;

	if (slots > MAX_CHARS) slots = MAX_CHARS;
	lockstep_get_seeds(&seeds[0], &seeds[1]);
	h = fnv(h, seeds, sizeof seeds);
	for (u32 i = 0; i < slots; i++, chr += CHR_SIZE) {
		u32 flags = *(u32 *)(chr + 4);
		if (!(flags & CHR_INUSE)) continue;
		CharSum *s = &c[n++];
		s->index = i;
		s->flags = flags & ~CHR_DRAWN;
		s->health = *(int *)(chr + 0x38);
		memcpy(s->pos, chr + 0x148, 12);
		s->facing = *(float *)(chr + 0x16c);
		memcpy(s->vel, chr + 0x1c4, 12);
		h = fnv(h, s, sizeof *s);
	}
	*count = n;
	return h;
}

/* The character's state, without what each PC's own drawing writes into it */
static void image_of(u32 index, u8 *img)
{
	u8 *chr = GS_CHARACTERS(ONgGameState) + index * CHR_SIZE;
	u16 a = *(u16 *)(chr + 2);

	memcpy(img, chr, CHR_SIZE);
	if (a && (a & 0xff) < ONgCharacterSlots)   /* the high byte holds flags (0x8201: slot 1) */
		memcpy(img + CHR_SIZE, ONgGameState + char_limit_gs_offset(GS_ACTIVE_STOCK) + (a & 0xff) * ACTIVE_SIZE, ACTIVE_SIZE);
	else
		memset(img + CHR_SIZE, 0, ACTIVE_SIZE);
	*(u32 *)(img + 4) &= ~CHR_DRAWN;
	*(u32 *)(img + 0x163c) = *(u32 *)(img + 0x163c) != 0;   /* frames off screen */
	*(u32 *)(img + 0x1660) = 0;                             /* distance to the camera */
	memset(img + CHR_SIZE + 0x221c, 0, 10);                 /* detail level the drawing chose */
}

static void capture_images(u32 slot)
{
	u8 *chr = GS_CHARACTERS(ONgGameState);
	u32 slots = ONgCharacterSlots, n = 0;

	if (!g_images) return;
	if (slots > MAX_CHARS) slots = MAX_CHARS;
	for (u32 i = 0; i < slots && n < IMG_CHARS; i++, chr += CHR_SIZE) {
		if (!(*(u32 *)(chr + 4) & CHR_INUSE)) continue;
		g_images[slot][n].index = i;
		image_of(i, g_images[slot][n].img);
		n++;
	}
	g_nimages[slot] = n;
}

static void dump_images(u32 slot, u32 tick)
{
	const char *name = g_mode == NET_HOST ? "Arklite-desync-host.bin" : "Arklite-desync-join.bin";
	u32 head[4] = { 0x53444b41 /* "AKDS" */, 1, tick, g_nimages[slot] }, w;
	HANDLE f;

	if (!g_images) return;
	f = CreateFileA(name, 0x40000000, 1, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) {
		log_msg("net: can't write %s", name);
		return;
	}
	WriteFile(f, head, sizeof head, &w, 0);
	WriteFile(f, g_images[slot], g_nimages[slot] * sizeof(CharImage), &w, 0);
	CloseHandle(f);
	log_msg("net: wrote %s (%d characters at tick %d): send it with the other PC's", name, g_nimages[slot], tick);
}

static void log_summary(u32 slot)
{
	for (u32 i = 0; i < g_mine[slot].n; i++) {
		const CharSum *c = &g_mine[slot].chars[i];
		log_msg("net:   #%d %s flags %08x health %d pos %08x %08x %08x facing %08x vel %08x %08x %08x",
		        c->index, (const char *)GS_CHARACTERS(ONgGameState) + c->index * CHR_SIZE + 0x14,
		        c->flags, c->health, *(u32 *)&c->pos[0], *(u32 *)&c->pos[1], *(u32 *)&c->pos[2],
		        *(u32 *)&c->facing, *(u32 *)&c->vel[0], *(u32 *)&c->vel[1], *(u32 *)&c->vel[2]);
	}
}

static void compare_hash(u32 slot)
{
	u32 tick = g_mine[slot].tick;
	if (!tick || g_theirs[slot].tick != tick || g_checked[slot] == tick) return;
	g_checked[slot] = tick;
	g_checks++;
	if (g_theirs[slot].hash != g_mine[slot].hash) {
		if (!g_bad_checks++) {
			g_first_bad = tick;
			log_msg("net: DESYNC at tick %d: state hash %08x here, %08x on the %s; this PC's characters:",
			        tick, g_mine[slot].hash, g_theirs[slot].hash, g_mode == NET_HOST ? "joiner" : "host");
			log_summary(slot);
			dump_images(slot, tick);
		}
	}
}

/* lockstep.c's tick hook, after every tick */
void net_tick_done(void)
{
	if (g_state != S_RUN && g_state != S_LOST) return;
	g_ticks++;
	if (g_ticks % HASH_EVERY) return;
	u32 slot = (g_ticks / HASH_EVERY) % HASH_SLOTS;
	g_mine[slot].hash = summarize(g_mine[slot].chars, &g_mine[slot].n);
	capture_images(slot);
	g_mine[slot].tick = g_ticks;
	g_last_hash_tick = g_ticks;
	g_last_hash = g_mine[slot].hash;
	compare_hash(slot);
}

/* ---- Sending and receiving ---------------------------------------------------------------- */

static void head(PktHead *h, u8 type)
{
	h->magic = MAGIC;
	h->proto = PROTO;
	h->type = type;
	h->pad = 0;
}

static void send_to_peer(const void *p, u32 n)
{
	if (!g_have_peer) return;
	if (sendto(g_sock, p, n, 0, &g_peer, sizeof g_peer) < 0) {
		if (WSAGetLastError() == 10093) return;   /* WSANOTINITIALISED: the process is exiting */
		if (g_send_errors++ < 5) log_msg("net: send failed (error %d)", WSAGetLastError());
	} else {
		g_npackets_sent++;
	}
	g_last_send = GetTickCount();
}

static void send_hello(void)
{
	PktHello p;
	memset(&p, 0, sizeof p);
	head(&p.h, PKT_HELLO);
	p.level = g_level;
	p.start_tick = g_start_tick;
	p.state_hash = g_start_hash;
	p.fpu_cw = g_start_fpu;
	p.delay = g_delay;
	str_cpy(p.version, sizeof p.version, ARKLITE_VERSION);
	memcpy(p.files, g_files, sizeof p.files);
	p.phase = g_state != S_LOBBY;
	p.ready = g_ready;
	send_to_peer(&p, sizeof p);
	g_last_hello = GetTickCount();
}

static void send_start(void)
{
	PktStart p;
	memset(&p, 0, sizeof p);
	head(&p.h, PKT_START);
	p.level = g_level;
	p.start_tick = g_start_tick;
	p.state_hash = g_start_hash;
	p.fpu_cw = g_start_fpu;
	p.seed = g_seed;
	p.local_seed = g_local_seed;
	p.delay = g_delay;
	str_cpy(p.version, sizeof p.version, ARKLITE_VERSION);
	memcpy(p.files, g_files, sizeof p.files);
	send_to_peer(&p, sizeof p);
}

static u32 lobby_players(void) { return 1 + (g_peer_seen != 0); }
static u32 lobby_ready(void) { return (g_ready != 0) + (g_peer_seen && g_peer_ready); }

static void send_lobby(int go)
{
	PktLobby p;
	u32 now = GetTickCount();
	memset(&p, 0, sizeof p);
	head(&p.h, PKT_LOBBY);
	p.level = g_level;
	p.players = lobby_players();
	p.ready = lobby_ready();
	p.countdown_ms = g_countdown_end ? (g_countdown_end - now < COUNTDOWN_MS + 1000 ? g_countdown_end - now : 0) : 0xffffffffu;
	p.go = go;
	p.joiner_ready = g_peer_ready;
	send_to_peer(&p, sizeof p);
	g_last_lobby = now;
}

static void send_inputs(void)
{
	static u8 buf[sizeof(PktInput) + MAX_SEND * 0x30];
	PktInput *p = (PktInput *)buf;
	u32 from = g_peer_ack, n;

	if (from + RING - 8 < g_gen) from = g_gen - (RING - 8);   /* far behind: the ring moved on */
	n = g_gen - from;
	if (n > MAX_SEND) n = MAX_SEND;
	head(&p->h, PKT_INPUT);
	p->first = from;
	p->count = n;
	p->ack = g_remote;
	p->hash_tick = g_last_hash_tick;
	p->hash = g_last_hash;
	for (u32 i = 0; i < n; i++) memcpy(buf + sizeof *p + i * 0x30, g_local_in[(from + i) % RING], 0x30);
	send_to_peer(buf, sizeof *p + n * 0x30);
	g_sent_gen = g_gen;
}

static void send_bye(void)
{
	PktHead h;
	head(&h, PKT_BYE);
	send_to_peer(&h, sizeof h);
}

static void check_start(const char *who, u32 level, u32 start_tick, u32 state_hash, u32 fpu_cw, const char *version,
                        const u32 *files)
{
	for (u32 i = 0; i < NFILES; i++)
		if (files[i] != g_files[i])
			log_msg("net: %s differs on the %s (%08x there, %08x here): the games will come apart; use the same AE install on both PCs",
			        g_file_names[i], who, files[i], g_files[i]);
	char v[17];
	memcpy(v, version, 16);
	v[16] = 0;
	if (!str_ieq_n(v, ARKLITE_VERSION, 17))
		log_msg("net: the %s runs Arklite %s, this PC %s", who, v, ARKLITE_VERSION);
	if (start_tick != g_start_tick)
		log_msg("net: the level started at tick %d on the %s, %d here (start both the same way)", start_tick, who, g_start_tick);
	if (fpu_cw != g_start_fpu)
		log_msg("net: x87 control word %04x on the %s, %04x here (floats may round differently)", fpu_cw, who, g_start_fpu);
	if (state_hash != g_start_hash)
		log_msg("net: the level-start state differs from the %s's (%08x here, %08x there): not the same map, or started differently", who, g_start_hash, state_hash);
	else
		log_msg("net: level-start state matches the %s's (%08x)", who, state_hash);
}

static void check_files(const char *who, const char *version, const u32 *files)
{
	char v[17];
	for (u32 i = 0; i < NFILES; i++)
		if (files[i] != g_files[i])
			log_msg("net: %s differs on the %s (%08x there, %08x here): the games will come apart; use the same AE install on both PCs",
			        g_file_names[i], who, files[i], g_files[i]);
	memcpy(v, version, 16);
	v[16] = 0;
	if (!str_ieq_n(v, ARKLITE_VERSION, 17))
		log_msg("net: the %s runs Arklite %s, this PC %s", who, v, ARKLITE_VERSION);
}

static void start_match(void);

static void strip(u32 *rec)
{
	rec[0] &= ~7u;   /* escape, console, pausescreen: per PC */
}

static void on_input(const PktInput *p, u32 size)
{
	const u8 *data = (const u8 *)(p + 1);

	if (size < sizeof *p || p->count > MAX_SEND || size < sizeof *p + p->count * 0x30) return;
	if (p->ack > g_peer_ack && p->ack <= g_gen) g_peer_ack = p->ack;
	for (u32 i = 0; i < p->count; i++) {
		u32 t = p->first + i, slot = t % RING;
		if (t < g_remote || t >= g_ticks + RING - 8) continue;
		memcpy(g_remote_in[slot], data + i * 0x30, 0x30);
		strip(g_remote_in[slot]);
		g_remote_at[slot] = t + 1;
	}
	while (g_remote_at[g_remote % RING] == g_remote + 1) g_remote++;
	if (p->hash_tick && !(p->hash_tick % HASH_EVERY)) {
		u32 slot = (p->hash_tick / HASH_EVERY) % HASH_SLOTS;
		if (g_theirs[slot].tick != p->hash_tick) {
			g_theirs[slot].tick = p->hash_tick;
			g_theirs[slot].hash = p->hash;
			compare_hash(slot);
		}
	}
}

static void begin_run(void);
static int net_open(void);

static void receive(void)
{
	static u8 buf[2048];
	SOCKADDR_IN from;
	int len, n;

	for (;;) {
		len = sizeof from;
		n = recvfrom(g_sock, buf, sizeof buf, 0, &from, &len);
		if (n < 0) {
			if (WSAGetLastError() == 10054) continue;   /* WSAECONNRESET: an earlier send bounced */
			return;
		}
		const PktHead *h = (const PktHead *)buf;
		if ((u32)n < sizeof *h || h->magic != MAGIC) continue;
		g_npackets_recv++;
		if (h->proto != PROTO) {
			log_msg("net: packet of protocol %d from the peer, this is %d: update Arklite on both PCs", h->proto, PROTO);
			continue;
		}
		if (g_have_peer && (from.addr != g_peer.addr || from.port != g_peer.port)) {
			/* one joiner at a time: another only takes an empty lobby's place */
			if (g_mode == NET_JOIN || g_state != S_LOBBY || g_peer_seen) continue;
		}
		if (g_state == S_IDLE) continue;   /* not in a level yet: the peer will resend */

		if (h->type == PKT_HELLO && g_mode == NET_HOST && (u32)n >= sizeof(PktHello)) {
			const PktHello *p = (const PktHello *)buf;
			static u32 warned_level;
			if (p->level != g_level) {
				if (warned_level != p->level) log_msg("net: a joiner is on level %d, this is level %d", p->level, g_level);
				warned_level = p->level;
				continue;
			}
			if (!g_have_peer || g_state == S_WAIT || (g_state == S_LOBBY && !g_peer_seen)) {
				g_peer = from;
				g_have_peer = 1;
			}
			g_last_recv = GetTickCount();
			if (g_state == S_LOBBY) {
				if (p->phase) continue;   /* it reloaded already; it says phase 0 again soon or waits */
				if (!g_peer_seen) {
					const u8 *ip = (const u8 *)&from.addr;
					log_msg("net: %d.%d.%d.%d:%d joined the lobby", ip[0], ip[1], ip[2], ip[3], ntohs(from.port));
					check_files("joiner", p->version, p->files);
					g_peer_seen = 1;
				}
				if (g_peer_ready != (int)p->ready)
					log_msg("net: the joiner is %s", p->ready ? "ready" : "not ready");
				g_peer_ready = p->ready;
				send_lobby(0);
				continue;
			}
			if (!p->phase) {   /* still in the lobby: it missed "go" */
				if (g_state == S_WAIT) send_lobby(1);
				continue;
			}
			if (g_state == S_WAIT) {
				const u8 *ip = (const u8 *)&from.addr;
				log_msg("net: %d.%d.%d.%d:%d joined (input delay %d ticks there)", ip[0], ip[1], ip[2], ip[3], ntohs(from.port), p->delay);
				check_start("joiner", p->level, p->start_tick, p->state_hash, p->fpu_cw, p->version, p->files);
				lockstep_get_seeds(&g_seed, &g_local_seed);
				begin_run();
			}
			send_start();   /* again for every HELLO: the first START may be lost */
		} else if (h->type == PKT_LOBBY && g_mode == NET_JOIN && (u32)n >= sizeof(PktLobby)) {
			const PktLobby *p = (const PktLobby *)buf;
			g_last_recv = GetTickCount();
			if (g_state != S_LOBBY || p->level != g_level) continue;
			if (!g_host_heard) log_msg("net: in the host's lobby");
			g_host_heard = 1;
			g_lobby_players = p->players;
			g_lobby_ready = p->ready;
			g_lobby_countdown = p->countdown_ms;
			g_lobby_rx = g_last_recv;
			if (p->go) start_match();
		} else if (h->type == PKT_START && g_mode == NET_JOIN && (u32)n >= sizeof(PktStart)) {
			const PktStart *p = (const PktStart *)buf;
			g_last_recv = GetTickCount();
			if (g_state != S_WAIT) continue;
			if (p->level != g_level) {
				log_msg("net: the host is on level %d, this is level %d", p->level, g_level);
				continue;
			}
			check_start("host", p->level, p->start_tick, p->state_hash, p->fpu_cw, p->version, p->files);
			g_seed = p->seed;
			g_local_seed = p->local_seed;
			if (p->delay >= 1 && p->delay <= 30) g_delay = p->delay;   /* the host's, on both PCs */
			begin_run();
		} else if (h->type == PKT_INPUT && (g_state == S_RUN)) {
			g_last_recv = GetTickCount();
			if (!g_ninputs_recv++) log_msg("net: first input from the %s", g_mode == NET_HOST ? "joiner" : "host");
			on_input((const PktInput *)buf, n);
		} else if (h->type == PKT_BYE && g_state == S_RUN) {
			log_msg("net: the %s left; their player stands still", g_mode == NET_HOST ? "joiner" : "host");
			g_state = S_LOST;
		}
	}
}

/* ---- Session ------------------------------------------------------------------------------ */

static void session_begin(u32 level)
{
	CharSum *c = g_mine[0].chars;
	u32 n;

	g_level = level;
	g_state = g_match_pending ? S_WAIT : S_LOBBY;
	g_ticks = g_remote = g_peer_ack = g_sent_gen = 0;
	g_has_pending = 0;
	memset(g_local_in, 0, sizeof g_local_in);
	memset(g_remote_at, 0, sizeof g_remote_at);
	memset(g_last_local, 0, sizeof g_last_local);
	memset(g_mine, 0, sizeof g_mine);
	memset(g_theirs, 0, sizeof g_theirs);
	memset(g_checked, 0, sizeof g_checked);
	g_last_hash_tick = g_last_hash = 0;
	g_checks = g_bad_checks = g_first_bad = 0;
	g_npackets_sent = g_npackets_recv = g_ninputs_recv = g_send_errors = 0;
	g_last_status = GetTickCount();
	g_gen = g_delay;   /* ticks 0..delay-1: no input from either PC */
	g_start_tick = GS_TICK(ONgGameState);
	g_start_fpu = fpu_control_word();
	g_start_hash = summarize(c, &n);
	g_ready = 0;
	g_countdown_end = 0;
	g_lobby_players = 1;
	g_lobby_ready = 0;
	g_lobby_countdown = 0xffffffffu;
	g_host_heard = 0;
	if (g_state == S_LOBBY) {
		g_peer_seen = g_peer_ready = 0;
		log_msg("net: level %d loaded: lobby (F3: ready)%s", level, g_mode == NET_HOST ? ", waiting for a player to join" : "");
	} else {
		log_msg("net: level %d reloaded for the match (tick %d, state %08x, %d characters); %s", level, g_start_tick,
		        g_start_hash, n, g_mode == NET_HOST ? "waiting for the joiner" : "joining the host");
	}
}

static void begin_run(void)
{
	g_state = S_RUN;
	g_match_pending = 0;
	g_gen = g_delay;   /* ticks 0..delay-1: no input from either PC */
	g_last_recv = GetTickCount();
	players_reset_scores();
	log_msg("net: game on (%s), RNG %08x/%08x, input delay %d ticks", mode_name(), g_seed, g_local_seed, g_delay);
}

static void session_end(const char *why)
{
	if (g_state == S_IDLE) return;
	if (g_state == S_RUN) send_bye();
	lockstep_net_record_end(why);
	if (g_state == S_RUN || g_state == S_LOST)
		log_msg("net: level %d ended (%s) after %d ticks (%d s); state hashes: %d compared, %d differed%s",
		        g_level, why, g_ticks, g_ticks / 60, g_checks, g_bad_checks, g_bad_checks ? ", see DESYNC above" : "");
	else if (g_state == S_WAIT)
		log_msg("net: level %d ended (%s) before the match started", g_level, why);
	g_state = S_IDLE;
}

/* ---- Input -------------------------------------------------------------------------------- */

/* This frame's record, cleaned, waits for the next input made */
static void take_record(const u32 *rec)
{
	u32 r[12];
	float a, b;

	memcpy(r, rec, sizeof r);
	strip(r);
	if (ONgInvertLook) r[10] ^= 0x80000000u;   /* this PC's setting, applied before sending */
	if (!g_has_pending) {
		memcpy(g_pending, r, sizeof r);
		g_has_pending = 1;
		return;
	}
	for (int i = 0; i < 7; i++) g_pending[i] |= r[i];   /* buttons: a tap isn't lost */
	if (r[7] || r[8]) { g_pending[7] = r[7]; g_pending[8] = r[8]; }
	memcpy(&a, &g_pending[9], 4); memcpy(&b, &r[9], 4); a += b; memcpy(&g_pending[9], &a, 4);
	memcpy(&a, &g_pending[10], 4); memcpy(&b, &r[10], 4); a += b; memcpy(&g_pending[10], &a, 4);
	for (int i = 11; i < 12; i++) g_pending[i] = r[i];
}

static void make_input(void)
{
	u32 *in = g_local_in[g_gen % RING];

	if (live_menu_open()) {
		memset(in, 0, 0x30);                 /* the menu is up: our player stands */
		g_has_pending = 0;
	} else if (g_has_pending) {
		memcpy(in, g_pending, 0x30);
		g_has_pending = 0;
	} else {
		memcpy(in, g_last_local, 0x30);      /* the buttons stay held, no aim */
		in[7] = in[8] = in[9] = in[10] = 0;
	}
	memcpy(g_last_local, in, 0x30);
	g_gen++;
}

/* ---- The game update ---------------------------------------------------------------------- */

static u32 run_update(u32 n, u32 *ticks_run)
{
	u8 *gs = ONgGameState;
	u8 invert = ONgInvertLook;
	u32 ran = 0, err;

	GS_SERVER_TIME(gs) = GS_TICK(gs) + n;
	GS_SYNC_BEHIND(gs) = 0;
	ONgInvertLook = 0;
	err = ONrGameState_Update(n, g_frame_p1, &ran);
	ONgInvertLook = invert;
	if (ticks_run) *ticks_run = ran;
	g_last_tick = GS_TICK(gs);
	return err;
}

/* ---- The lobby ------------------------------------------------------------------------- */

/* F3, pressed while this Oni window has the focus (two Onis may share a PC) */
static int f3_pressed(void)
{
	u32 pid = 0;
	int down = (GetAsyncKeyState(VK_F3) & 0x8000) != 0, pressed;
	void *w = GetForegroundWindow();

	if (w) GetWindowThreadProcessId(w, &pid);
	if (pid != GetCurrentProcessId()) down = 0;
	pressed = down && !g_f3_down;
	g_f3_down = down;
	return pressed && !live_menu_open();
}

/* Reload the level for the match: both PCs do it, the host when its countdown ends, the joiner
 * when "go" arrives. The next frame begins a synced session (S_WAIT, then the START handshake). */
static void start_match(void)
{
	u32 level = g_level;

	log_msg("net: the match starts: reloading level %d", level);
	if (g_mode == NET_HOST)
		for (int i = 0; i < 3; i++) send_lobby(1);   /* and again for each HELLO that still says lobby */
	g_match_pending = 1;
	session_end("match starting");
	ONrGame_StartLevel(level);
}

static u32 lobby_frame(u32 count, void *actions, u32 *ticks_run, u32 now)
{
	u8 *gs = ONgGameState;
	u32 err, n, wanted;

	if (f3_pressed()) {
		g_ready = !g_ready;
		log_msg("net: %s", g_ready ? "ready" : "not ready");
		if (g_mode == NET_JOIN) send_hello();
		else if (g_peer_seen) send_lobby(0);
	}
	if (g_mode == NET_JOIN) {
		if (now - g_last_hello >= HELLO_MS) send_hello();
		if (g_host_heard && (int)(now - g_last_recv) > LOST_MS) {
			log_msg("net: nothing from the host for %d s", LOST_MS / 1000);
			g_host_heard = 0;
		}
	} else {
		if (g_peer_seen && (int)(now - g_last_recv) > LOST_MS) {
			log_msg("net: the joiner left the lobby");
			g_peer_seen = g_peer_ready = 0;
		}
		u32 players = lobby_players(), ready = lobby_ready();
		int enough = players >= 2 && ready * 100 >= players * 51;
		if (enough && !g_countdown_end) {
			g_countdown_end = now + COUNTDOWN_MS;
			if (!g_countdown_end) g_countdown_end = 1;
			log_msg("net: %d of %d players ready: the match starts in %d s", ready, players, COUNTDOWN_MS / 1000);
			send_lobby(0);
		} else if (!enough && g_countdown_end) {
			g_countdown_end = 0;
			log_msg("net: countdown stopped (%d of %d players ready)", ready, players);
			send_lobby(0);
		}
		if (g_countdown_end && (int)(now - g_countdown_end) >= 0) {
			start_match();
			if (ticks_run) *ticks_run = 0;
			return 0;
		}
		if (g_peer_seen && now - g_last_lobby >= HELLO_MS) send_lobby(0);
	}
	if (g_state != S_LOBBY) {   /* "go" arrived in receive(): the level is being reloaded */
		if (ticks_run) *ticks_run = 0;
		return 0;
	}

	/* warm-up: this PC's player plays alone, the other one stands */
	if (g_mode == NET_HOST) {
		players_net_frame(0, 0);
		err = ONrGameState_Update(count, actions, ticks_run);
	} else {
		wanted = GS_SERVER_TIME(gs) - GS_TICK(gs);
		n = wanted > 6 ? 6 : wanted;
		for (u32 k = 0; k < n; k++) {
			if (k == 0 && (count & 0xffff) && actions) {
				memcpy(g_frame_p2[0], actions, 0x30);
				strip(g_frame_p2[0]);
				memcpy(g_last_local, g_frame_p2[0], 0x30);
			} else {
				memcpy(g_frame_p2[k], g_last_local, 0x30);   /* the buttons stay held, no aim */
				g_frame_p2[k][7] = g_frame_p2[k][8] = g_frame_p2[k][9] = g_frame_p2[k][10] = 0;
			}
			if (live_menu_open()) memset(g_frame_p2[k], 0, 0x30);
		}
		GS_SYNC_BEHIND(gs) = 0;
		players_net_frame(g_frame_p2, n);
		err = ONrGameState_Update(0, actions, ticks_run);   /* Konoko: no record */
	}
	g_last_tick = GS_TICK(gs);
	return err;
}

/* What mp_hud.c shows */
void net_hud(NetHud *h)
{
	u32 now = GetTickCount();

	memset(h, 0, sizeof *h);
	h->state = g_mode == NET_OFF ? NETHUD_OFF : g_state == S_LOBBY ? NETHUD_LOBBY :
	           g_state == S_WAIT ? NETHUD_STARTING : g_state == S_RUN || g_state == S_LOST ? NETHUD_MATCH : NETHUD_OFF;
	h->local_player = g_mode == NET_JOIN ? 1 : 0;
	h->ready = g_ready;
	h->countdown_ms = -1;
	if (g_mode == NET_HOST) {
		h->connected = 1;
		h->players = lobby_players();
		h->ready_count = lobby_ready();
		if (g_countdown_end) h->countdown_ms = (int)(g_countdown_end - now) > 0 ? (int)(g_countdown_end - now) : 0;
	} else {
		h->connected = g_host_heard;
		h->players = g_lobby_players;
		h->ready_count = g_lobby_ready;
		if (g_lobby_countdown != 0xffffffffu) {
			int left = (int)g_lobby_countdown - (int)(now - g_lobby_rx);
			h->countdown_ms = left > 0 ? left : 0;
		}
	}
	h->needed = (h->players * 51 + 99) / 100;
	if (h->needed < 2) h->needed = 2;
}

/* lockstep.c's update hook, in place of the update, while networking is on */
u32 net_update(u32 count, void *actions, u32 *ticks_run)
{
	u8 *gs = ONgGameState;
	u32 level = ONrLevel_GetCurrent(), now = GetTickCount();

	if (g_sock == INVALID_SOCKET && !net_open()) {
		g_mode = NET_OFF;   /* from now on lockstep.c runs the update itself */
		players_net_frame(0, 0);
		return ONrGameState_Update(count, actions, ticks_run);
	}
	if (g_state != S_IDLE && (level != g_level || GS_TICK(gs) < g_last_tick))
		session_end(level != g_level ? "level left" : "level restarted");
	if (g_state == S_IDLE && level && gs) session_begin(level);
	if (g_state == S_IDLE) {
		players_net_frame(0, 0);
		return ONrGameState_Update(count, actions, ticks_run);
	}

	receive();
	now = GetTickCount();   /* after receive(), which stamps g_last_recv: now - g_last_recv mustn't wrap */
	u32 wanted = GS_SERVER_TIME(gs) - GS_TICK(gs);
	if (wanted > 6) wanted = 6;

	if (g_state == S_IDLE) {   /* "go" arrived: the level is being reloaded for the match */
		if (ticks_run) *ticks_run = 0;
		return 0;
	}
	if (g_state == S_LOBBY) return lobby_frame(count, actions, ticks_run, now);

	if (g_state == S_WAIT) {
		if (g_mode == NET_JOIN && now - g_last_hello >= HELLO_MS) send_hello();
		if (now - g_last_status >= 5000) {
			log_msg("net: still waiting for the %s (packets sent %d, received %d)", g_mode == NET_HOST ? "joiner" : "host",
			        g_npackets_sent, g_npackets_recv);
			g_last_status = now;
		}
		players_net_frame(0, 0);
		return run_update(0, ticks_run);   /* the clock stands until the game starts */
	}

	if (g_state == S_RUN && (int)(now - g_last_recv) > LOST_MS) {
		log_msg("net: nothing from the %s for %d s; their player stands still (packets sent %d, received %d, inputs received %d)",
		        g_mode == NET_HOST ? "joiner" : "host", LOST_MS / 1000, g_npackets_sent, g_npackets_recv, g_ninputs_recv);
		g_state = S_LOST;
	}

	if ((count & 0xffff) && actions) take_record((const u32 *)actions);
	for (u32 k = 0; k < wanted && g_gen < g_ticks + g_delay + MAX_AHEAD; k++) make_input();
	if (g_state == S_RUN && (g_gen != g_sent_gen || now - g_last_send >= RESEND_MS)) send_inputs();

	u32 remote_avail = g_state == S_LOST ? 6 : g_remote - g_ticks;
	u32 n = wanted;
	if (g_state == S_RUN && g_remote > g_ticks + g_delay + 1) n++;   /* the peer is ahead */
	if (n > g_gen - g_ticks) n = g_gen - g_ticks;
	if (n > remote_avail) n = remote_avail;
	if (n > 6) n = 6;
	if (n || g_state != S_RUN) {
		g_last_status = now;
	} else if (now - g_last_status >= 1000) {   /* stuck waiting: say on what */
		log_msg("net: waiting at tick %d: our inputs up to %d, the %s's up to %d; packets sent %d, received %d",
		        g_ticks, g_gen, g_mode == NET_HOST ? "joiner" : "host", g_remote, g_npackets_sent, g_npackets_recv);
		g_last_status = now;
	}

	for (u32 k = 0; k < n; k++) {
		u32 slot = (g_ticks + k) % RING;
		const u32 *mine = g_local_in[slot];
		static const u32 none[12];
		const u32 *theirs = g_state == S_LOST ? none : g_remote_in[slot];
		memcpy(g_frame_p1[k], g_mode == NET_HOST ? mine : theirs, 0x30);
		memcpy(g_frame_p2[k], g_mode == NET_HOST ? theirs : mine, 0x30);
	}
	if (n && !g_ticks) {
		lockstep_set_seeds(g_seed, g_local_seed);
		if (g_record) {
			const char *path = g_mode == NET_HOST ? "Arklite-net-host.akr" : "Arklite-net-join.akr";
			if (lockstep_net_record_begin(g_level, path)) log_msg("net: recording this game to %s", path);
		}
	}
	players_net_frame(g_frame_p2, n);
	lockstep_net_record_frame(n, g_frame_p1, g_frame_p2);
	u32 err = run_update(n, ticks_run);
	lockstep_net_record_frame_end();
	return err;
}

/* ---- Each PC's own view ------------------------------------------------------------------- */

/* The character this PC plays: player 2 on the joiner */
static u8 *view_character(u8 *konoko)
{
	u8 *p2;
	if (g_mode != NET_JOIN || !ONgGameState || konoko != GS_PLAYER(ONgGameState)) return konoko;
	p2 = players_p2();
	return p2 ? p2 : konoko;
}

static void ONI_FC camera_update(u32 ticks, u32 edx, u32 arg)
{
	u8 *gs = ONgGameState, *cam = gs ? GS_CAMERA(gs) : 0;
	if (cam && *(u32 *)cam == 1) {
		u8 *konoko = GS_PLAYER(gs), *view = view_character(konoko);
		u8 *target = CAM_TARGET(cam);
		if (view && view != konoko && (target == konoko || (target && target == g_cam_set)))
			CAM_TARGET(cam) = g_cam_set = view;
	}
	ONrCamera_Update(ticks, edx, arg);
}

/* The game display (0x4fb480: the draw hook while a level is loaded, and the main loop's call
 * at 0x4d47f3) runs once a frame outside the ticks and draws for "the player" in gs+0xac: the
 * draw pass (0x4e1540) draws only the player's laser sight (0x4e1900, called with gs+0xac at
 * 0x4e1836) and leaves its end in 0x5ecb58..0x5ecb64, the crosshair there (0x4f1fa0 from
 * 0x4fbaa0) uses the player's weapon, and the HUD (0x4fff50) the player's health and weapon.
 * On the joiner the display runs with player 2 in the player slot (gs+0xac, and its active
 * data in gs+0xb0 as SetPlayer 0x4fc8f0 sets it); Konoko goes back right after. Its first 8
 * bytes (sub esp,0x6c; mov eax,[0x5ece7c]) become a jump here and run in display_rest. */
#define ONcDisplay              0x4fb480

__attribute__((naked)) static void display_rest(void)
{
	__asm__ volatile(
		"subl $0x6c, %%esp\n\t"
		"movl 0x5ece7c, %%eax\n\t"
		"pushl $0x4fb488\n\t"
		"ret\n" ::);
}

static void display(void)
{
	u8 *gs = ONgGameState, *p1 = gs ? GS_PLAYER(gs) : 0, *view = view_character(p1);
	u32 b0;

	if (!view || view == p1) {
		display_rest();
		return;
	}
	b0 = *(u32 *)(gs + 0xb0);
	GS_PLAYER(gs) = view;
	*(u8 **)(gs + 0xb0) = ONrCharacter_GetActive(view);
	display_rest();
	GS_PLAYER(gs) = p1;
	*(u32 *)(gs + 0xb0) = b0;
}

static void ONI_FC listener_position(u8 *chr, void *out) { ONrListener_Position(view_character(chr), out); }
static void ONI_FC listener_position_alt(u8 *chr, void *out) { ONrListener_PositionAlt(view_character(chr), out); }
static void ONI_FC listener_facing(u8 *chr, void *out) { ONrListener_Facing(view_character(chr), out); }

static void view_install(void)
{
	if (patch_call(0x4fb141, (u32)ONrCamera_Update, (u32)camera_update) &&
	    patch_call(0x4fb170, (u32)ONrCamera_Update, (u32)camera_update))
		log_msg("net: the camera follows player 2");
	if (patch_call(0x4fb1f4, (u32)ONrListener_PositionAlt, (u32)listener_position_alt) &&
	    patch_call(0x4fb1fb, (u32)ONrListener_Position, (u32)listener_position) &&
	    patch_call(0x4fb20f, (u32)ONrListener_Facing, (u32)listener_facing))
		log_msg("net: sound is heard from player 2");
	{
		static const u8 entry[8] = { 0x83, 0xec, 0x6c, 0xa1, 0x7c, 0xce, 0x5e, 0x00 };
		u8 b[8] = { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
		u32 rel = (u32)display - (ONcDisplay + 5);
		if (code_matches(ONcDisplay, entry, sizeof entry)) {
			memcpy(b + 1, &rel, 4);
			patch_write(ONcDisplay, b, sizeof b);
			log_msg("net: the HUD, laser sight and crosshair show player 2");
		} else {
			log_msg("net: game display 0x%x changed, the HUD and crosshair show Konoko", ONcDisplay);
		}
	}
}

/* ---- Install ------------------------------------------------------------------------------ */

static int resolve(const char *spec, SOCKADDR_IN *out)
{
	char host[256];
	u32 port = DEFAULT_PORT, i = 0;

	while (spec[i] && spec[i] != ':' && i + 1 < sizeof host) { host[i] = spec[i]; i++; }
	host[i] = 0;
	if (spec[i] == ':') {
		port = 0;
		for (const char *s = spec + i + 1; *s >= '0' && *s <= '9'; s++) port = port * 10 + (*s - '0');
	}
	memset(out, 0, sizeof *out);
	out->family = 2;   /* AF_INET */
	out->port = htons((u16)port);
	out->addr = inet_addr(host);
	if (out->addr == 0xffffffffu) {
		HOSTENT *he = gethostbyname(host);
		if (!he || !he->addrs || !he->addrs[0]) return 0;
		memcpy(&out->addr, he->addrs[0], 4);
	}
	return 1;
}

static char g_join[300];

/* The socket, at the first game frame: Winsock shouldn't start inside DllMain */
static int net_open(void)
{
	u8 wsa[400];
	SOCKADDR_IN local;
	u32 on = 1;

	if (WSAStartup(0x0202, wsa)) {
		log_msg("net: Winsock didn't start, networking off");
		return 0;
	}
	g_sock = socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 17 /* IPPROTO_UDP */);
	if (g_sock == INVALID_SOCKET) {
		log_msg("net: no UDP socket (error %d), networking off", WSAGetLastError());
		return 0;
	}
	memset(&local, 0, sizeof local);
	local.family = 2;
	local.port = g_mode == NET_HOST ? htons(g_port) : 0;   /* the joiner: any free port */
	if (bind(g_sock, &local, sizeof local)) {
		log_msg("net: can't use UDP port %d (error %d; another host running?), networking off", g_port, WSAGetLastError());
		closesocket(g_sock);
		g_sock = INVALID_SOCKET;
		return 0;
	}
	ioctlsocket(g_sock, FIONBIO, &on);
	g_images = VirtualAlloc(0, HASH_SLOTS * IMG_CHARS * sizeof(CharImage), 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 4);
	if (g_mode == NET_JOIN) {
		if (!resolve(g_join, &g_peer)) {
			log_msg("net: can't find host %s, networking off", g_join);
			closesocket(g_sock);
			g_sock = INVALID_SOCKET;
			return 0;
		}
		g_have_peer = 1;
		const u8 *ip = (const u8 *)&g_peer.addr;
		log_msg("net: joining %d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], ntohs(g_peer.port));
	} else {
		log_msg("net: hosting on UDP port %d", g_port);
	}
	return 1;
}

void net_install(void)
{
	char host_env[32];
	u32 port;

	if (GetEnvironmentVariableA("ARKLITE_HOST", host_env, sizeof host_env)) g_mode = NET_HOST;
	else if (GetEnvironmentVariableA("ARKLITE_JOIN", g_join, sizeof g_join)) g_mode = NET_JOIN;
	else {   /* Arklite.ini [net] host=1 or join=<ip>[:port], for Windows shortcuts */
		host_env[0] = 0;
		if (GetPrivateProfileIntA("net", "host", 0, ini_path())) g_mode = NET_HOST;
		else if (GetPrivateProfileStringA("net", "join", "", g_join, sizeof g_join, ini_path()) && g_join[0])
			g_mode = NET_JOIN;
	}
	if (g_mode == NET_OFF) {
		log_msg("net: off (set ARKLITE_HOST or ARKLITE_JOIN, or host/join in Arklite.ini [net])");
		return;
	}

	port = GetPrivateProfileIntA("net", "port", DEFAULT_PORT, ini_path());
	g_delay = GetPrivateProfileIntA("net", "delay", 4, ini_path());
	g_record = GetPrivateProfileIntA("net", "record", 1, ini_path()) != 0;
	if (g_delay < 1) g_delay = 1;
	if (g_delay > 30) g_delay = 30;
	if (g_mode == NET_HOST) {   /* ARKLITE_HOST=1, or a port number */
		u32 v = 0;
		for (const char *s = host_env; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
		if (v > 1 && v < 65536) port = v;
	}
	if (port < 1 || port > 65535) port = DEFAULT_PORT;
	g_port = (u16)port;
	log_msg("net: %s, input delay %d ticks; the game waits at the level's start for the %s",
	        g_mode == NET_HOST ? "hosting" : "joining", g_delay, g_mode == NET_HOST ? "joiner" : "host");
	hash_files();
	if (g_mode == NET_JOIN) view_install();
	/* a second Oni on the same PC, for testing: skip "There is already an instance" */
	if (patch_byte(ONcSingleInstanceJe, 0x74, 0xeb)) log_msg("net: more than one Oni may run on this PC");
}

void net_exit(void)
{
	session_end("game closed");
	if (g_sock != INVALID_SOCKET) closesocket(g_sock);
	g_sock = INVALID_SOCKET;
}
