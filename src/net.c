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
 * Both PCs must run the same game: every HELLO and START carries a Compat record (hashes of
 * Arklite's source (build.sh: a local build and GitHub's of the same source match), Daodan, its gameplay settings, level0, and every file in the map's folder,
 * and whether player 2 is on). The host keeps a joiner whose record differs out of the lobby and
 * tells it why in LOBBY (refused); both PCs say on screen what differs. A packet of another
 * protocol (another Arklite) gets the same. The host's LOBBY also carries its gameplay settings
 * from Arklite.ini (respawn_delay, item_respawn) and the match's limits ([host] time_limit,
 * score_limit, from the Host dialog), which the joiner uses. A match ends at a limit (players.c);
 * see "The match's end" for the way back to the lobby (S_OVER, PKT_END).
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
 * are cleared. Word 11 carries this PC's F2 character choice (char_menu.c, read by players.c).
 * Esc still opens the menu through utility input, which reads this PC's keys. The
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

#define DEFAULT_PORT    NET_DEFAULT_PORT
#define PROTO           12
#define MAGIC           0x544e4b41   /* "AKNT" */
#define RING            512          /* input slots, by tick */
#define MAX_SEND        24           /* inputs per packet */
#define STALL_LOG_MS    30000        /* how often waits for the peer's inputs are logged */
#define HASH_EVERY      60
#define HASH_SLOTS      8
#define MAX_CHARS       255
#define LOST_MS         10000
#define HELLO_MS        200
#define NAME_MS         1000         /* the player's name goes out this often (and at once on a change) */
#define RESEND_MS       30
#define CHAT_RESEND_MS  250          /* a chat message nobody acknowledged goes again this often */
#define CHAT_GIVE_UP_MS 10000        /* ...for this long */
#define CHAT_QUEUE      16
#define COUNTDOWN_MS    7000
#define PROBLEM_MS      2000         /* a refusal stays on screen this long after the last one */
#define VK_F3           0x72
#define VK_LBUTTON      0x01
#define VK_F1           0x70
#define END_MS          30000        /* after a match ends: back to the lobby when everyone pressed fire, or after this */
#define END_SEND_MS     200
#define GS_HELP_REQUEST 0x138        /* u8: the main loop opens the F1 screen (0x5018c0) after the frame */
#define ACTION_HELP     4            /* record word 0: "pausescreen", F1 */
#define ACTION_FIRE_LO  0x60000000u  /* punch, kick */
#define ACTION_FIRE_HI  0x1c0u       /* fire1-3 */

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
__declspec(dllimport) int  WINAPI QueryPerformanceCounter(unsigned long long *);
__declspec(dllimport) int  WINAPI QueryPerformanceFrequency(unsigned long long *);
__declspec(dllimport) int  WINAPI CloseHandle(HANDLE);
__declspec(dllimport) int  WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) u32  WINAPI GetPrivateProfileSectionA(const char *, char *, u32, const char *);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32  WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32  WINAPI GetCurrentProcessId(void);
__declspec(dllimport) int  WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) int  WINAPI CreateDirectoryA(const char *, void *);
__declspec(dllimport) int  WINAPI MoveFileExA(const char *, const char *, u32);
__declspec(dllimport) int  WINAPI RemoveDirectoryA(const char *);
__declspec(dllimport) int  WINAPI DeleteFileA(const char *);
__declspec(dllimport) u32  WINAPI SetFilePointer(HANDLE, int, int *, u32);
__declspec(dllimport) void WINAPI Sleep(u32);
__declspec(dllimport) int  WINAPI PeekMessageA(void *, void *, u32, u32, u32);
__declspec(dllimport) int  WINAPI TranslateMessage(const void *);
__declspec(dllimport) int  WINAPI DispatchMessageA(const void *);
__declspec(dllimport) void WINAPI PostQuitMessage(int);
__declspec(dllimport) int  WINAPI setsockopt(u32, int, int, const void *, int);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
#ifndef ARKLITE_SRC_HASH
#define ARKLITE_SRC_HASH 0   /* build.sh: a hash of the source, the same for every build of it */
#endif

#define INVALID_SOCKET  0xffffffffu
#define FIONBIO         0x8004667e

/* ---- Packets ------------------------------------------------------------------------------ */

enum { PKT_HELLO = 1, PKT_START, PKT_INPUT, PKT_BYE, PKT_LOBBY, PKT_NAME,
       PKT_MAP_LIST_REQ, PKT_MAP_LIST, PKT_MAP_REQ, PKT_MAP_DATA, PKT_INFO_REQ, PKT_INFO,
       PKT_CHAT, PKT_CHAT_ACK, PKT_END };

typedef struct { u32 magic; u16 proto; u8 type, pad; } PktHead;
#define NFILES 4
/* What both PCs must have the same of (see g_file_names) */
typedef struct {
	char version[16];
	u32 files[NFILES];
	u32 dll;               /* Arklite's source (ARKLITE_SRC_HASH), not the DLL's bytes */
	u32 map;               /* the level's files: the custom level's whole folder */
	u32 players;           /* [players] p2 isn't 0 */
} Compat;
typedef struct {
	PktHead h;
	u32 level, start_tick, state_hash, fpu_cw, delay;
	Compat c;
	u32 phase;             /* 0: in the lobby, 1: waiting for the match to start */
	u32 ready;             /* the joiner pressed F3 */
} PktHello;
typedef struct {
	PktHead h;
	u32 level, players, ready, countdown_ms;   /* countdown_ms 0xffffffff: none */
	u32 go;                /* the match starts: reload the level */
	u32 joiner_ready;      /* what the host knows of the joiner's ready flag */
	u32 seed, local_seed;  /* with go: both RNG streams, set on both PCs before the reload */
	u32 refused;           /* NETBAD_*: the joiner can't play here, and why (then nothing else counts) */
	u32 respawn_ticks, item_ticks;   /* the host's Arklite.ini, used on both PCs */
	u32 map_offer;         /* with refused NETBAD_MAP: the host sends its map's files on request */
	u32 time_limit;        /* the match's limits, from the host's Host dialog: minutes (0: none) */
	int score_limit;       /* 0: none; below 0: reached going down (suicides) */
} PktLobby;
typedef struct {
	PktHead h;
	u32 level, start_tick, state_hash, fpu_cw, seed, local_seed, delay;
	Compat c;
} PktStart;
typedef struct {
	PktHead h;
	u32 first, count;      /* inputs for ticks first..first+count-1 follow */
	u32 ack;               /* every input of ours before this tick arrived */
	u32 hash_tick, hash;   /* our latest state hash (hash_tick 0: none yet) */
	int adv;               /* our ticks run minus the peer's inputs we have, as sent (time sync) */
	u32 stamp;             /* GetTickCount when sent */
	u32 echo, held;        /* the peer's latest stamp, and how many ms ago it arrived (ping) */
} PktInput;
typedef struct {
	PktHead h;
	char name[MP_NAME_MAX];   /* this PC's player's (mp_names.c); only shown, not game state */
} PktName;
/* Chat (chat.c): only shown, never game state. Messages are numbered in a stream (random per
 * Oni run); the receiver shows them in order and acknowledges the last one it showed, and the
 * sender sends every unacknowledged one again every CHAT_RESEND_MS. A stream the receiver hasn't
 * heard before starts at whatever number comes first (a new joiner, or the other PC restarted). */
typedef struct {
	PktHead h;
	u32 stream, seq;
	char text[CHAT_MAX];
} PktChat;
typedef struct {
	PktHead h;
	u32 stream, seq;       /* the sender's stream: every message up to seq was shown */
} PktChatAck;
/* The Join list asks every server it shows (mp_menu.c). The host answers whatever protocol the
 * question has, with its own in the head, so a joiner on another Arklite sees why it can't join */
typedef struct {
	PktHead h;
	u32 token, sent;       /* echoed back: which list entry asked, and when (the ping) */
} PktInfoReq;
typedef struct {
	PktHead h;
	u32 token, sent;
	u32 level, players, max_players, spectators, max_spectators;
	u32 in_match;          /* the match is on: a joiner waits in its lobby until the next one */
	u32 server_id;         /* random per hosting: the same server reached at two addresses */
	char name[NET_SERVER_NAME_MAX];   /* the server's, Host dialog */
	char folder[64];       /* the map's folder in CustomLevels, "" for a numbered level */
	char map[48];          /* the map's name */
} PktInfo;

/* After a match: each PC says whether its player pressed fire; the host says how many did, how
 * long is left, and finally "go": both PCs reload the level into the lobby */
typedef struct {
	PktHead h;
	u32 level;
	u32 ready;             /* the sender's player pressed fire */
	u32 go;                /* host: back to the lobby now */
	u32 players, ready_count, left_ms;   /* host: what it knows */
} PktEnd;

/* ---- State -------------------------------------------------------------------------------- */

enum { NET_OFF, NET_HOST, NET_JOIN };
enum { S_IDLE, S_LOBBY, S_WAIT, S_RUN, S_LOST, S_OVER };

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
static u32 g_stall_frames, g_stall_ticks, g_last_stall_log;   /* frames held back by the peer's inputs */
/* Time sync (rollback): each PC sends how far its ticks are past the peer's inputs it has (adv).
 * With the same latency both ways, (ours - theirs) / 2 is how many ticks this PC runs ahead of
 * the other in real time; the PC ahead leaves out one tick in every SYNC_EVERY until it isn't.
 * Never faster: the old catch-up ran one extra tick every frame, frames with no tick due
 * included, which at high frame rates ran the game about 3x as fast. */
#define SYNC_EVERY      8
static int g_peer_adv, g_peer_adv_known;
static u32 g_peer_stamp, g_peer_stamp_at;      /* the peer's newest PktInput stamp, when it came */
static u32 g_ping_sum, g_ping_n, g_ping_max;   /* round trips since the last 30 s log line */
static u32 g_sync_skip_at, g_sync_skipped;
/* Rollback (delay 0, the default): this PC's input runs at once, the peer's is predicted, and a
 * wrong guess puts the game back (rollback.c) and runs the ticks again */
#define ROLLING         (g_delay == 0)
#define RB_AHEAD        (ROLLBACK_KEPT_TICKS - 2)   /* ticks run past the peer's last real input, at most */
#define ONgSoundOn      (*(u8 *)0x5eb757)           /* SS2 "a sound driver is there": every sound call checks it */
static u32 g_verified;                 /* ticks < this ran with the peer's real inputs */
static u32 g_pred[RING][12];           /* the peer's input each tick last ran with */
static int g_rb_started;               /* the engine is on for this match */
static u8  g_sound_on;                 /* ONgSoundOn as the game had it */
static u32 g_rollbacks, g_resim_ticks, g_rb_failed, g_rb_deepest, g_rb_frames;
static u32 g_peer_ack;                 /* the peer has our inputs for ticks < g_peer_ack */
static u32 g_rb_us_sum, g_rb_us_max, g_rb_restore_us_max, g_rb_timed;   /* rollback cost since the last 30 s log line */

/* The performance counter's low half: differences over short spans only; us() turns them into
 * microseconds (32-bit math: -nostdlib has no 64-bit division) */
static u32 counter(void)
{
	unsigned long long now = 0;
	QueryPerformanceCounter(&now);
	return (u32)now;
}

static u32 us(u32 counts)
{
	static u32 per_us;
	if (!per_us) {
		unsigned long long f = 0;
		QueryPerformanceFrequency(&f);
		per_us = (f >> 32) ? 0xffffffffu / 1000000u : (u32)f / 1000000u;
		if (!per_us) per_us = 1;
	}
	return counts / per_us;
}
static u32 g_local_in[RING][12], g_remote_in[RING][12];
static u32 g_remote_at[RING];          /* tick + 1 of the input in the slot, 0: none */
static u32 g_pending[12], g_last_local[12];
static int g_has_pending;
static u32 g_sent_gen;

static u32 g_seed, g_local_seed;       /* the host's, put in place before the first tick */
static u32 g_start_hash, g_start_tick, g_start_fpu;
static u32 g_last_recv, g_last_send, g_last_hello;
static int g_lost_left;                /* S_LOST because the peer said goodbye, not silence */
static u32 g_frame_p1[6][12], g_frame_p2[6][12];

typedef struct { u32 index, flags; int health; float pos[3], facing, vel[3]; } CharSum;
static struct { u32 tick, hash, n, seeds[2]; CharSum chars[MAX_CHARS]; } g_mine[HASH_SLOTS];
static struct { u32 tick, hash; } g_theirs[HASH_SLOTS];
static u32 g_checked[HASH_SLOTS];        /* the tick each slot was last compared at */
static u32 g_published[HASH_SLOTS];      /* rollback: the tick each slot's hash went out for (verified) */
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
static u32 g_last_name, g_name_sent;   /* when our name last went out, and mp_name_serial() then */
static u32 g_match_seed, g_match_local_seed;   /* the RNGs the match's level loads with */
static u32 g_problem, g_problem_at;    /* NETBAD_* last seen (refused / refusing), and when */
/* The match's limits: the host's from Arklite.ini [host] (the Host dialog), the joiner's from LOBBY */
static u32 g_time_limit = 10;          /* minutes, 0: none */
static int g_score_limit = 20;         /* 0: none */
static int g_ping = -1;                /* ms, the last round trip measured (match only) */
/* After the match (S_OVER) */
static u32 g_over_at, g_last_end;
static u8 *g_look_who;                 /* whose camera turns after the match; 0: start again from the character */
static int g_end_ready, g_peer_end_ready, g_fire_down, g_over_lost;
static u32 g_end_players, g_end_ready_count, g_end_left_ms, g_end_rx;   /* joiner: the host's view */

/* A level reload that waits for the live dialogs to close (reload_level) */
static u32 g_reload;                   /* level + 1, 0: none */
static int g_reload_seeds;             /* load it with g_match_seed / g_match_local_seed */

/* The server, as the Host dialog (or Arklite.ini [host]) has it */
static char g_server_name[NET_SERVER_NAME_MAX] = "New Oni multiplayer game";
static u32 g_max_players = 8, g_max_spectators;
static u32 g_server_id;
static int g_startup_mode;             /* set by the environment or Arklite.ini [net], not the menu */
static int g_hashed, g_join_installed;

/* What both PCs must have the same of besides Oni.exe: Daodan patches Oni's code and has
 * gameplay settings, and level0 holds the characters and animations */
static const char *const g_file_names[NFILES] = {
	"binkw32.dll (Daodan)", "daodan.ini [gameplay] and [devmode]",
	"GameDataFolder\\level0_Final.dat", "GameDataFolder\\level0_Final.raw",
};
static u32 g_files[NFILES];
static u32 g_dll_hash, g_map_hash;

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
	/* shared for writing too: another Oni on this PC has these files open */
	HANDLE f = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 3, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
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
	g_dll_hash = ARKLITE_SRC_HASH;
	log_msg("net: Arklite source %08x, Daodan %08x, its settings %08x, level0 %08x %08x", g_dll_hash, g_files[0], g_files[1],
	        g_files[2], g_files[3]);
}

/* Every file below dir (which ends with '\\'), in whatever order the file system lists them
 * (Windows sorts, Wine on Linux doesn't): each file's hash, of its path below dir in upper case
 * and then its bytes, is added up */
static u32 hash_tree(const char *dir, const char *rel, int depth)
{
	WIN32_FIND_DATAA fd;
	char path[1024], sub[1024];
	u32 sum = 0;
	HANDLE f;

	if (depth > 8 || str_len(dir) + str_len(rel) + 2 >= MAX_PATH) return 0;
	fmt(path, "%s%s*", dir, rel);
	f = FindFirstFileA(path, &fd);
	if (f == INVALID_HANDLE_VALUE) return 0;
	do {
		if (fd.cFileName[0] == '.' && (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2]))) continue;
		fmt(sub, "%s%s", rel, fd.cFileName);
		if (str_len(dir) + str_len(sub) + 2 >= MAX_PATH) continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			fmt(path, "%s\\", sub);
			sum += hash_tree(dir, path, depth + 1);
			continue;
		}
		u32 h = 0x811c9dc5u;
		for (const char *s = sub; *s; s++) h = (h ^ (u8)to_upper(*s)) * 0x01000193u;
		fmt(path, "%s%s", dir, sub);
		sum += hash_words(h ^ hash_file(path), (const u8 *)&fd.nFileSizeLow, 4);
	} while (FindNextFileA(f, &fd));
	FindClose(f);
	return sum;
}

/* The files the level is made of: a custom level's whole folder (its .dat/.raw/.sep, level.ini
 * with the item respawn times, IGMD scripts), or an original level's files */
static u32 hash_map(u32 level)
{
	static const char *const ext[] = { "dat", "raw", "sep" };
	const char *dir = custom_game_level_dir(level);
	char path[64];
	u32 h = 0x811c9dc5u;

	if (dir) return hash_tree(dir, "", 0);
	for (u32 i = 0; i < 3; i++) {
		fmt(path, "GameDataFolder\\level%d_Final.%s", level, ext[i]);
		h = (h ^ hash_file(path)) * 0x01000193u;
	}
	return h;
}

static void compat_mine(Compat *c)
{
	memset(c, 0, sizeof *c);
	str_cpy(c->version, sizeof c->version, ARKLITE_VERSION);
	memcpy(c->files, g_files, sizeof c->files);
	c->dll = g_dll_hash;
	c->map = g_map_hash;
	c->players = players_enabled() != 0;
}

/* NETBAD_*: what differs between the peer's game and this PC's */
static u32 compat_problems(const Compat *c)
{
	u32 bad = 0;
	if (c->dll != g_dll_hash) bad |= NETBAD_DLL;
	if (c->files[0] != g_files[0]) bad |= NETBAD_DAODAN;
	if (c->files[1] != g_files[1]) bad |= NETBAD_DAODAN_INI;
	if (c->files[2] != g_files[2] || c->files[3] != g_files[3]) bad |= NETBAD_LEVEL0;
	if (c->map != g_map_hash) bad |= NETBAD_MAP;
	if (!c->players || !players_enabled()) bad |= NETBAD_PLAYERS;
	return bad;
}

const char *net_problem_text(u32 bit)
{
	switch (bit) {
	case NETBAD_PROTO:      return "Arklite's version";
	case NETBAD_DLL:        return "Arklite (not the same release or commit)";
	case NETBAD_DAODAN:     return "Daodan (binkw32.dll)";
	case NETBAD_DAODAN_INI: return "daodan.ini [gameplay] or [devmode]";
	case NETBAD_LEVEL0:     return "GameDataFolder\\level0_Final.dat or .raw";
	case NETBAD_MAP:        return "the map's files (its CustomLevels folder)";
	case NETBAD_PLAYERS:    return "Arklite.ini [players] p2 (0 turns multiplayer off)";
	}
	return "?";
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

/* seeds: 0 for the level-start check, which happens before the host's seeds are in place
 * (drawing may draw from the game RNG between the level load and the first update) */
static u32 summarize_with(CharSum *c, u32 *count, int seeds_too)
{
	u8 *chr = GS_CHARACTERS(ONgGameState);
	u32 slots = ONgCharacterSlots, n = 0, seeds[2];
	u32 h = 0x811c9dc5u;

	if (slots > MAX_CHARS) slots = MAX_CHARS;
	lockstep_get_seeds(&seeds[0], &seeds[1]);
	if (seeds_too) h = fnv(h, seeds, sizeof seeds);
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

static u32 summarize(CharSum *c, u32 *count) { return summarize_with(c, count, 1); }

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
	*(u32 *)(img + 0x166c) = 0;                             /* HUD hit arrows, faded for the viewer only */
}

/* rollback.c: character index's state as compared between PCs (image_of), 0 if not in use */
int net_char_image(u32 index, u8 *img)
{
	u8 *gs = ONgGameState;
	if (!gs || index >= ONgCharacterSlots || index >= MAX_CHARS) return 0;
	if (!(*(u32 *)(GS_CHARACTERS(gs) + index * CHR_SIZE + 4) & CHR_INUSE)) return 0;
	image_of(index, img);
	return 1;
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
	if (ROLLING && g_published[slot] != tick) return;   /* not run with the peer's real inputs yet */
	g_checked[slot] = tick;
	g_checks++;
	if (g_theirs[slot].hash != g_mine[slot].hash) {
		if (!g_bad_checks++) {
			g_first_bad = tick;
			log_msg("net: DESYNC at tick %d: state hash %08x here, %08x on the %s; this PC's characters:",
			        tick, g_mine[slot].hash, g_theirs[slot].hash, g_mode == NET_HOST ? "joiner" : "host");
			log_msg("net:   RNG seeds here %08x/%08x (the hash covers them; if every character matches, these differ)",
			        g_mine[slot].seeds[0], g_mine[slot].seeds[1]);
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
	lockstep_get_seeds(&g_mine[slot].seeds[0], &g_mine[slot].seeds[1]);
	capture_images(slot);
	g_mine[slot].tick = g_ticks;
	if (ROLLING && g_ticks > g_remote) return;   /* a guess: published once verified (publish_verified) */
	g_published[slot] = g_ticks;
	g_last_hash_tick = g_ticks;
	g_last_hash = g_mine[slot].hash;
	compare_hash(slot);
}

/* Rollback: the hashes of ticks that turned out to have been run with the right inputs */
static void publish_verified(void)
{
	for (u32 slot = 0; slot < HASH_SLOTS; slot++) {
		u32 tick = g_mine[slot].tick;
		if (!tick || g_published[slot] == tick || tick > g_verified) continue;
		g_published[slot] = tick;
		if (tick > g_last_hash_tick) {
			g_last_hash_tick = tick;
			g_last_hash = g_mine[slot].hash;
		}
		compare_hash(slot);
	}
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
	compat_mine(&p.c);
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
	compat_mine(&p.c);
	send_to_peer(&p, sizeof p);
}

static u32 lobby_players(void) { return 1 + (g_peer_seen != 0); }
static u32 lobby_ready(void) { return (g_ready != 0) + (g_peer_seen && g_peer_ready); }

static int map_can_serve(void);

/* refused: NETBAD_*, the joiner this goes to can't play here */
static void send_lobby_to(int go, u32 refused)
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
	p.seed = g_match_seed;
	p.local_seed = g_match_local_seed;
	p.refused = refused;
	p.map_offer = (refused & NETBAD_MAP) && map_can_serve();
	p.respawn_ticks = players_respawn_ticks();
	p.item_ticks = items_default_ticks();
	p.time_limit = g_time_limit;
	p.score_limit = g_score_limit;
	send_to_peer(&p, sizeof p);
	g_last_lobby = now;
}

static void send_lobby(int go) { send_lobby_to(go, 0); }

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
	p->adv = (int)g_ticks - (int)g_remote;
	p->stamp = GetTickCount();
	p->echo = g_peer_stamp;
	p->held = g_peer_stamp ? p->stamp - g_peer_stamp_at : 0;
	for (u32 i = 0; i < n; i++) memcpy(buf + sizeof *p + i * 0x30, g_local_in[(from + i) % RING], 0x30);
	send_to_peer(buf, sizeof *p + n * 0x30);
	g_sent_gen = g_gen;
}

static void send_name(void)
{
	PktName p;
	memset(&p, 0, sizeof p);
	head(&p.h, PKT_NAME);
	str_cpy(p.name, sizeof p.name, mp_name_local());
	send_to_peer(&p, sizeof p);
	g_last_name = GetTickCount();
	g_name_sent = mp_name_serial();
}

/* ---- Chat ---------------------------------------------------------------------------------- */

static struct { u32 seq, at; char text[CHAT_MAX]; } g_chat_out[CHAT_QUEUE];
static u32 g_chat_out_n, g_chat_seq, g_chat_stream, g_chat_sent_at;
static u32 g_chat_in_stream, g_chat_in_seq;

/* Someone to talk to: the joiner is in the lobby (host), the host answered (joiner), or a match */
static int chat_peer(void)
{
	return g_have_peer && (g_state == S_RUN || g_state == S_WAIT || g_state == S_OVER || g_peer_seen || g_host_heard);
}

static void chat_send_queued(void)
{
	PktChat p;
	u32 now = GetTickCount(), keep = 0;

	for (u32 i = 0; i < g_chat_out_n; i++)
		if (now - g_chat_out[i].at < CHAT_GIVE_UP_MS) g_chat_out[keep++] = g_chat_out[i];
	g_chat_out_n = keep;
	for (u32 i = 0; i < g_chat_out_n; i++) {
		memset(&p, 0, sizeof p);
		head(&p.h, PKT_CHAT);
		p.stream = g_chat_stream;
		p.seq = g_chat_out[i].seq;
		str_cpy(p.text, sizeof p.text, g_chat_out[i].text);
		send_to_peer(&p, sizeof p);
	}
	g_chat_sent_at = now;
}

int net_chat_send(const char *text)
{
	if (g_mode == NET_OFF || !chat_peer()) return 0;
	if (!g_chat_stream) g_chat_stream = (GetTickCount() * 0x9e3779b1u ^ (u32)(unsigned long)&g_chat_out) | 1;
	if (g_chat_out_n == CHAT_QUEUE) {   /* the oldest is lost */
		memcpy(g_chat_out, g_chat_out + 1, sizeof g_chat_out[0] * (CHAT_QUEUE - 1));
		g_chat_out_n--;
	}
	g_chat_out[g_chat_out_n].seq = ++g_chat_seq;
	g_chat_out[g_chat_out_n].at = GetTickCount();
	str_cpy(g_chat_out[g_chat_out_n].text, CHAT_MAX, text);
	g_chat_out_n++;
	chat_send_queued();
	return 1;
}

static void chat_received(const PktChat *p)
{
	PktChatAck a;
	char text[CHAT_MAX];

	if (p->stream != g_chat_in_stream) {
		g_chat_in_stream = p->stream;
		g_chat_in_seq = p->seq - 1;
	}
	if (p->seq == g_chat_in_seq + 1) {
		memcpy(text, p->text, sizeof text);
		text[sizeof text - 1] = 0;
		g_chat_in_seq = p->seq;
		chat_receive(text);
	}
	memset(&a, 0, sizeof a);
	head(&a.h, PKT_CHAT_ACK);
	a.stream = g_chat_in_stream;
	a.seq = g_chat_in_seq;
	send_to_peer(&a, sizeof a);
}

static void chat_acked(const PktChatAck *a)
{
	u32 keep = 0;
	if (a->stream != g_chat_stream) return;
	for (u32 i = 0; i < g_chat_out_n; i++)
		if ((int)(g_chat_out[i].seq - a->seq) > 0) g_chat_out[keep++] = g_chat_out[i];
	g_chat_out_n = keep;
}

static void send_bye(void)
{
	PktHead h;
	head(&h, PKT_BYE);
	for (int i = 0; i < 3; i++) send_to_peer(&h, sizeof h);   /* UDP: one may be lost, and there's no next time */
}

/* Logs what differs on the peer; returns it (NETBAD_*) */
static u32 check_files(const char *who, const Compat *c)
{
	u32 bad = compat_problems(c);
	char v[17];

	for (u32 i = 0; i < NFILES; i++)
		if (c->files[i] != g_files[i])
			log_msg("net: %s differs on the %s (%08x there, %08x here): the games will come apart; use the same AE install on both PCs",
			        g_file_names[i], who, c->files[i], g_files[i]);
	memcpy(v, c->version, 16);
	v[16] = 0;
	if (!str_ieq_n(v, ARKLITE_VERSION, 17))
		log_msg("net: the %s runs Arklite %s, this PC %s", who, v, ARKLITE_VERSION);
	if (bad & NETBAD_DLL)
		log_msg("net: Arklite differs on the %s (source %08x there, %08x here): use the same Arklite release or commit on both PCs",
		        who, c->dll, g_dll_hash);
	if (bad & NETBAD_MAP)
		log_msg("net: the map's files differ on the %s (%08x there, %08x here): copy the map's CustomLevels folder from one PC to the other",
		        who, c->map, g_map_hash);
	if (bad & NETBAD_PLAYERS)
		log_msg("net: player 2 is off on %s (Arklite.ini [players] p2=0)", players_enabled() ? who : "this PC");
	return bad;
}

static u32 check_start(const char *who, u32 start_tick, u32 state_hash, u32 fpu_cw, const Compat *c)
{
	u32 bad = check_files(who, c);
	if (start_tick != g_start_tick)
		log_msg("net: the level started at tick %d on the %s, %d here (start both the same way)", start_tick, who, g_start_tick);
	if (fpu_cw != g_start_fpu)
		log_msg("net: x87 control word %04x on the %s, %04x here (floats may round differently)", fpu_cw, who, g_start_fpu);
	if (state_hash != g_start_hash)
		log_msg("net: the level-start state differs from the %s's (%08x here, %08x there): not the same map, or started differently", who, g_start_hash, state_hash);
	else
		log_msg("net: level-start state matches the %s's (%08x)", who, state_hash);
	return bad;
}

/* The peer's game isn't this one (NETBAD_*): keep it out, and say why in the log (once while
 * it keeps trying) and on screen (net_hud) */
static void refuse(const char *who, u32 bad, const Compat *c)
{
	u32 now = GetTickCount();

	if (bad != g_problem || !g_problem_at || now - g_problem_at > LOST_MS) {
		log_msg("net: the %s's game isn't the same as this PC's, so they can't play together:", who);
		if (c) check_files(who, c);
		for (u32 bit = 1; bit <= NETBAD_PLAYERS; bit <<= 1)
			if (bad & bit) log_msg("net:   differs: %s", net_problem_text(bit));
	}
	g_problem = bad;
	g_problem_at = now ? now : 1;
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
	g_peer_adv = p->adv;
	g_peer_adv_known = 1;
	{
		u32 now = GetTickCount();
		if ((int)(p->stamp - g_peer_stamp) > 0 || !g_peer_stamp) {
			g_peer_stamp = p->stamp;
			g_peer_stamp_at = now;
		}
		if (p->echo) {
			u32 rtt = now - p->echo - p->held;
			if (rtt < 10000) {
				g_ping = (int)rtt;
				g_ping_sum += rtt;
				g_ping_n++;
				if (rtt > g_ping_max) g_ping_max = rtt;
			}
		}
	}
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

/* ---- Map download ------------------------------------------------------------------------- *
 * A joiner whose map's files differ from the host's (only NETBAD_MAP, on a custom level) gets
 * them from the host. The host's LOBBY refusal says it can send them (map_offer); the joiner
 * reloads the level (ONrGame_StartLevel), and its level load (0x5059d0) shows Oni's loading
 * popup (dialog 0x9b "Progress", window *0x5ed2f4: "Loading..." label, progress bar 100 at
 * 30,65 290x15, in a 350x150 box) and then opens the level's files (TMrLevel_Load 0x4217d0,
 * called at 0x505a7c with ECX the level). Arklite's call there first downloads the files:
 *   1. MAP_LIST_REQ / MAP_LIST: the host's list of the folder's files (path below the folder,
 *      size, hash of the bytes), MAP_LIST_PAGE at a time.
 *   2. The files missing here or with another size or hash are fetched in MAP_CHUNK pieces:
 *      MAP_REQ asks for up to MAP_REQ_MAX pieces, the host answers each with MAP_DATA. The joiner
 *      keeps `win` pieces asked for at once, asks again for one not here after DL_RTO_MS, halves
 *      the window when that happens and widens it while pieces keep coming.
 *   3. Each file goes to GameDataFolder\CustomLevels.download\<folder>\ and is checked against
 *      the host's hash; when all are here, the files they replace and the files the host doesn't
 *      have are moved to GameDataFolder\CustomLevels.backup\<folder>\ (outside CustomLevels, so
 *      Custom Game doesn't list them), the new ones into the folder, and the level is mounted
 *      again (its .dat names may have changed) before the engine opens it.
 * Under the progress bar two lines say "Downloading (left/total)", left = files still to
 * download, total = the files the map has, and "1.2MB of 3.4MB (1.23 MB/s)" for the file
 * coming in (B, KB or MB by that file's size; the speed's unit by the speed); the bar shows that
 * file. Esc cancels. A cancelled or failed download isn't tried again for that level until Oni
 * restarts: the lobby says the map differs, as before. Windows messages are handled meanwhile,
 * so the window doesn't hang. The host serves files only from the level it's on.
 * A joiner without the map's folder at all downloads it the same way from the menu, before the
 * level starts (net_map_fetch, from Join's Play), into CustomLevels\<folder>\. */
#define MAP_PATH        112
#define MAP_LIST_PAGE   12
#define MAP_MAX_FILES   512
#define MAP_CHUNK       1400
#define MAP_REQ_MAX     32           /* pieces per request */
#define DL_SLOTS        1024         /* request times, by piece */
#define DL_WINDOW_MIN   16
#define DL_WINDOW_MAX   768
#define DL_RTO_MS       400
#define DL_DRAW_MS      33
#define VK_ESCAPE       0x1b

typedef struct { char path[MAP_PATH]; u32 size, hash; } MapFile;
typedef struct { PktHead h; u32 level, first; } PktMapListReq;
typedef struct { PktHead h; u32 level, map, total, first, count; MapFile f[MAP_LIST_PAGE]; } PktMapList;
typedef struct { PktHead h; u32 level, file, chunk, count; } PktMapReq;
typedef struct { PktHead h; u32 level, file, chunk, size; } PktMapData;   /* size bytes follow */

#define ONgLoadingDialog        (*(WMtWindow **)0x5ed2f4)
#define LDcMessage_Progress     0x2e         /* the dialog passes it to the bar: p1 = percent */
#define M3rFrame_Begin          ((void (ONI_FC *)(u32))0x41ab00)
#define WMrDisplay              ((void (*)(void))0x476140)
#define M3rFrame_End            ((void (*)(void))0x41ad00)
#define TMrLevel_Load           ((u16 (ONI_FC *)(u32, u32))0x4217d0)
#define ONcLevelLoad_TMCall     0x505a7c

/* Every file below dir, walked as hash_tree does: its path below dir, size and hash. 0 when there
 * are too many or a path is too long to send */
static int list_tree(const char *dir, const char *rel, int depth, MapFile *out, u32 *n)
{
	WIN32_FIND_DATAA fd;
	char path[1024], sub[1024];
	HANDLE f;
	int ok = 1;

	if (depth > 8 || str_len(dir) + str_len(rel) + 2 >= MAX_PATH) return 0;
	fmt(path, "%s%s*", dir, rel);
	f = FindFirstFileA(path, &fd);
	if (f == INVALID_HANDLE_VALUE) return 1;
	do {
		if (fd.cFileName[0] == '.' && (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2]))) continue;
		fmt(sub, "%s%s", rel, fd.cFileName);
		if (str_len(dir) + str_len(sub) + 2 >= MAX_PATH) { ok = 0; continue; }
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			fmt(path, "%s\\", sub);
			ok &= list_tree(dir, path, depth + 1, out, n);
			continue;
		}
		if (*n == MAP_MAX_FILES || str_len(sub) >= MAP_PATH) { ok = 0; continue; }
		MapFile *m = &out[(*n)++];
		str_cpy(m->path, MAP_PATH, sub);
		m->size = fd.nFileSizeLow;
		fmt(path, "%s%s", dir, sub);
		m->hash = hash_file(path);
	} while (FindNextFileA(f, &fd));
	FindClose(f);
	return ok;
}

static void send_to(const void *p, u32 n, const SOCKADDR_IN *to)
{
	if (sendto(g_sock, p, n, 0, to, sizeof *to) >= 0) g_npackets_sent++;   /* a full buffer: asked again */
}

/* -- The host's side -- */

static MapFile *g_serve;
static u32 g_nserve, g_serve_level, g_serve_map;
static int g_serve_ok;
static HANDLE g_serve_file = INVALID_HANDLE_VALUE;
static u32 g_serve_index;

/* The level's files can be sent (a custom level, listed once per level and version) */
static int map_can_serve(void)
{
	const char *dir = custom_game_level_dir(g_level);

	if (!dir) return 0;
	if (!g_serve) g_serve = VirtualAlloc(0, MAP_MAX_FILES * sizeof(MapFile), 0x3000, 4);
	if (!g_serve) return 0;
	if (g_serve_level != g_level || g_serve_map != g_map_hash) {
		if (g_serve_file != INVALID_HANDLE_VALUE) CloseHandle(g_serve_file);
		g_serve_file = INVALID_HANDLE_VALUE;
		g_nserve = 0;
		g_serve_ok = list_tree(dir, "", 0, g_serve, &g_nserve);
		g_serve_level = g_level;
		g_serve_map = g_map_hash;
		log_msg("net: map download: %d files in %s%s", g_nserve, dir,
		        g_serve_ok ? "" : " (too many files or a path too long: joiners can't download it)");
	}
	return g_serve_ok;
}

static void map_serve(const u8 *buf, u32 n, const SOCKADDR_IN *to)
{
	const PktHead *h = (const PktHead *)buf;
	char path[1024];

	if (!map_can_serve()) return;
	if (h->type == PKT_MAP_LIST_REQ && n >= sizeof(PktMapListReq)) {
		const PktMapListReq *q = (const PktMapListReq *)buf;
		static u32 last_log;
		PktMapList p;
		if (q->level != g_level) return;
		if (!q->first && GetTickCount() - last_log > LOST_MS) {
			const u8 *ip = (const u8 *)&to->addr;
			log_msg("net: %d.%d.%d.%d:%d is downloading the map", ip[0], ip[1], ip[2], ip[3], ntohs(to->port));
			last_log = GetTickCount();
		}
		memset(&p, 0, sizeof p);
		head(&p.h, PKT_MAP_LIST);
		p.level = g_level;
		p.map = g_map_hash;
		p.total = g_nserve;
		p.first = q->first;
		for (u32 i = q->first; i < g_nserve && p.count < MAP_LIST_PAGE; i++) p.f[p.count++] = g_serve[i];
		send_to(&p, sizeof p, to);
	} else if (h->type == PKT_MAP_REQ && n >= sizeof(PktMapReq)) {
		static u8 data[MAP_REQ_MAX * MAP_CHUNK], out[sizeof(PktMapData) + MAP_CHUNK];
		const PktMapReq *q = (const PktMapReq *)buf;
		PktMapData *p = (PktMapData *)out;
		u32 off, len, got = 0;
		if (q->level != g_level || q->file >= g_nserve || !q->count || q->count > MAP_REQ_MAX) return;
		const MapFile *m = &g_serve[q->file];
		if (q->chunk >= (m->size + MAP_CHUNK - 1) / MAP_CHUNK) return;
		if (g_serve_file == INVALID_HANDLE_VALUE || g_serve_index != q->file) {
			if (g_serve_file != INVALID_HANDLE_VALUE) CloseHandle(g_serve_file);
			fmt(path, "%s%s", custom_game_level_dir(g_level), m->path);
			g_serve_file = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 3, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
			g_serve_index = q->file;
			if (g_serve_file == INVALID_HANDLE_VALUE) log_msg("net: map download: can't open %s", path);
		}
		if (g_serve_file == INVALID_HANDLE_VALUE) return;
		off = q->chunk * MAP_CHUNK;
		len = q->count * MAP_CHUNK;
		if (len > m->size - off) len = m->size - off;
		SetFilePointer(g_serve_file, (int)off, 0, 0 /* FILE_BEGIN */);
		if (!ReadFile(g_serve_file, data, len, &got, 0)) return;
		for (u32 k = 0; k * MAP_CHUNK < got; k++) {
			u32 size = got - k * MAP_CHUNK;
			if (size > MAP_CHUNK) size = MAP_CHUNK;
			head(&p->h, PKT_MAP_DATA);
			p->level = g_level;
			p->file = q->file;
			p->chunk = q->chunk + k;
			p->size = size;
			memcpy(out + sizeof *p, data + k * MAP_CHUNK, size);
			send_to(out, sizeof *p + size, to);
		}
	}
}

/* -- The joiner's side -- */

static u32 g_dl_level;                 /* the level whose load downloads the host's files first */
static u32 g_dl_declined;              /* its download failed or was cancelled: not again */
static WMtWindow *g_dl_lines[2];       /* the two lines under the loading popup's bar */
static u32 g_req_chunk[DL_SLOTS], g_req_time[DL_SLOTS];   /* piece + 1 asked for last, and when */

static void session_end(const char *why);
static u32 run_update(u32 n, u32 *ticks_run);

/* Every reload of the level net.c starts (the match, back to the lobby, the map download). It
 * runs from net_update, which a live dialog's frame calls too (live_menu.c live_frame): a load
 * there set the draw hook back to the plain display, so the dialog's loop went on drawing a game
 * nothing updated, a frozen picture until the process was killed (the Windows joiner, pause menu
 * open as the match ended without the host). With a live dialog up the dialogs are ended first
 * and the level loads on the next main-loop frame (net_update, before anything else); the
 * match's seeds are set just before the load, since the frames between draw from the RNGs. */
static void reload_level(u32 level, int seeds)
{
	if (live_menu_open()) {
		if (!g_reload) log_msg("net: closing the pause menu to reload level %d", level);
		g_reload = level + 1;
		g_reload_seeds = seeds;
		live_menu_close();
		return;
	}
	g_reload = 0;
	if (seeds) lockstep_set_seeds(g_match_seed, g_match_local_seed);
	ONrGame_StartLevel(level);
}

/* From the host's LOBBY refusal: reload the level, which downloads first (level_load) */
static int map_download_start(void)
{
	u32 level = g_level;

	if (g_dl_declined == level || !custom_game_level_dir(level)) return 0;
	log_msg("net: the host's map differs from this PC's: reloading level %d to download it", level);
	g_dl_level = level;
	session_end("downloading the host's map");
	reload_level(level, 0);
	return 1;
}

/* A path below the map's folder that stays in it */
static int safe_path(const char *s)
{
	if (!s[0] || s[0] == '\\') return 0;
	for (const char *c = s; *c; c++)
		if (*c == ':' || *c == '/' || (u8)*c < 0x20 || (c[0] == '.' && c[1] == '.')) return 0;
	return 1;
}

/* The folders of path (up to its last '\') */
static void make_dirs(char *path)
{
	for (char *c = path + 3; *c; c++) {
		if (*c != '\\') continue;
		*c = 0;
		CreateDirectoryA(path, 0);
		*c = '\\';
	}
}

static int move_file(const char *from, const char *to)
{
	char dir[1024];
	str_cpy(dir, sizeof dir, to);
	make_dirs(dir);
	if (MoveFileExA(from, to, 3 /* MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED */)) return 1;
	log_msg("net: map download: can't move %s to %s", from, to);
	return 0;
}

static int find_file(const MapFile *list, u32 n, const char *path)
{
	for (u32 i = 0; i < n; i++)
		if (str_ieq_n(list[i].path, path, MAP_PATH)) return (int)i;
	return -1;
}

/* One packet from the host, or 0 */
static u32 dl_receive(u8 *buf, u32 size)
{
	SOCKADDR_IN from;
	int len, n;

	for (;;) {
		len = sizeof from;
		n = recvfrom(g_sock, buf, size, 0, &from, &len);
		if (n < 0) {
			if (WSAGetLastError() == 10054) continue;
			return 0;
		}
		const PktHead *h = (const PktHead *)buf;
		if ((u32)n < sizeof *h || h->magic != MAGIC || h->proto != PROTO) continue;
		if (from.addr != g_peer.addr || from.port != g_peer.port) continue;
		g_npackets_recv++;
		return (u32)n;
	}
}

static int g_dl_quit;                  /* WM_QUIT came while downloading: posted again after */

/* Windows messages, and Esc (only while this Oni window has the focus) */
static int dl_cancelled(void)
{
	struct { void *hwnd; u32 msg, wp, lp, time, x, y; } m;
	u32 pid = 0;
	void *w;

	while (PeekMessageA(&m, 0, 0, 0, 1 /* PM_REMOVE */)) {
		if (m.msg == 0x12 /* WM_QUIT */) {
			g_dl_quit = (int)m.wp | 0x10000;
			return 1;
		}
		TranslateMessage(&m);
		DispatchMessageA(&m);
	}
	if (!(GetAsyncKeyState(VK_ESCAPE) & 0x8000)) return 0;
	w = GetForegroundWindow();
	if (w) GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId();
}

/* The two lines under the bar: copies of the "Loading..." label (the popup's first child with
 * id 0 that isn't its full-size outline), the bar's width */
static void dl_lines_create(void)
{
	WMtWindow *d = ONgLoadingDialog, *label = 0, *bar;

	g_dl_lines[0] = g_dl_lines[1] = 0;
	if (!d || !(bar = WMrDialog_GetItemByID(d, 100))) return;
	for (WMtWindow *w = WIN_FIRSTCHILD(d); w; w = WIN_NEXT(w))
		if (WIN_ID(w) == 0 && WIN_W(w) < WIN_W(d)) { label = w; break; }
	if (!label) return;
	for (int i = 0; i < 2; i++) {
		WMtWindow *w = WMrWindow_New(WIN_CLASS(label), "", WIN_FLAGS(label), WIN_STYLE(label), 0, WIN_X(bar),
		                             WIN_Y(bar) + WIN_H(bar) + 8 + i * 22, WIN_W(bar), 20, d, 0);
		if (!w) return;
		WMrWindow_SetFontInfo(w, WIN_FONTINFO(label));
		win_insert_after(w, label);   /* before the outline, which would dim it */
		g_dl_lines[i] = w;
	}
}

static void dl_draw(const char *a, const char *b, u32 percent)
{
	WMtWindow *d = ONgLoadingDialog;

	if (g_dl_lines[0]) WMrWindow_SetTitle(g_dl_lines[0], a, 0x100);
	if (g_dl_lines[1]) WMrWindow_SetTitle(g_dl_lines[1], b, 0x100);
	if (d) WMrMessage_Send(d, LDcMessage_Progress, percent, 0);
	M3rFrame_Begin(0);
	WMrDisplay();
	M3rFrame_End();
}

static void dl_lines_hide(void)
{
	for (int i = 0; i < 2; i++)
		if (g_dl_lines[i]) WMrWindow_SetVisible(g_dl_lines[i], 0);
	dl_draw("", "", 10);   /* where the load's bar was */
}

/* "12.3MB": the unit by basis (the file's size) */
static void size_text(char *out, u32 bytes, u32 basis)
{
	if (basis >= 1u << 20) fmt(out, "%u.%uMB", bytes >> 20, (bytes & 0xfffff) * 10 >> 20);
	else if (basis >= 1024) fmt(out, "%u.%uKB", bytes >> 10, (bytes & 0x3ff) * 10 >> 10);
	else fmt(out, "%uB", bytes);
}

static void speed_text(char *out, u32 bps)
{
	if (bps >= 1u << 20) fmt(out, "%u.%02u MB/s", bps >> 20, (bps & 0xfffff) * 100 >> 20);
	else if (bps >= 1024) fmt(out, "%u.%02u KB/s", bps >> 10, (bps & 0x3ff) * 100 >> 10);
	else fmt(out, "%u B/s", bps);
}

static const char *g_dl_why;            /* why the last download stopped */

/* Downloads the host's version of level's folder into dir (absolute, ends with '\\'; it may not
 * exist yet); 1 when it's in place */
static int map_download_to(u32 level, const char *dir, const char *folder)
{
	static u8 buf[2048];
	char stage[MAX_PATH], backup[MAX_PATH], path[1024], to[1024], a[128], b[128], s1[32], s2[32], s3[32];
	MapFile *theirs, *mine;
	u8 *need, *moved, *got = 0;
	u32 ntheirs = 0, nmine = 0, total = 0xffffffffu, their_map = 0, nneed = 0, nextra = 0, need_bytes = 0, biggest = 0;
	u32 now, last_heard, last_req = 0, last_draw = 0, bytes_all = 0, began;
	HANDLE out = INVALID_HANDLE_VALUE;
	const char *why = 0;
	int ok = 0, cur = -1;

	if (!g_have_peer) return 0;
	theirs = VirtualAlloc(0, 2 * MAP_MAX_FILES * (sizeof(MapFile) + 1), 0x3000, 4);
	if (!theirs) return 0;
	mine = theirs + MAP_MAX_FILES;
	need = (u8 *)(mine + MAP_MAX_FILES);
	moved = need + MAP_MAX_FILES;
	fmt(path, "GameDataFolder\\CustomLevels.download\\%s\\", folder);
	if (!GetFullPathNameA(path, sizeof stage, stage, 0)) str_cpy(stage, sizeof stage, path);
	fmt(path, "GameDataFolder\\CustomLevels.backup\\%s\\", folder);
	if (!GetFullPathNameA(path, sizeof backup, backup, 0)) str_cpy(backup, sizeof backup, path);
	dl_lines_create();
	log_msg("net: map download: asking the host for its list of %s's files", folder);

	/* 1. the host's list */
	last_heard = GetTickCount();
	while (total == 0xffffffffu || ntheirs < total) {
		u32 n;
		now = GetTickCount();
		if (dl_cancelled()) { why = "cancelled"; goto done; }
		if (now - last_heard > LOST_MS) { why = "the host stopped answering"; goto done; }
		if (!last_req || now - last_req >= DL_RTO_MS) {
			PktMapListReq q;
			head(&q.h, PKT_MAP_LIST_REQ);
			q.level = level;
			q.first = ntheirs;
			send_to_peer(&q, sizeof q);
			last_req = now ? now : 1;
		}
		while ((n = dl_receive(buf, sizeof buf))) {
			const PktMapList *p = (const PktMapList *)buf;
			if (p->h.type != PKT_MAP_LIST || n < sizeof *p || p->level != level || p->first != ntheirs ||
			    p->count > MAP_LIST_PAGE || p->total > MAP_MAX_FILES || p->first + p->count > p->total) continue;
			total = p->total;
			their_map = p->map;
			for (u32 i = 0; i < p->count; i++) {
				theirs[ntheirs] = p->f[i];
				theirs[ntheirs].path[MAP_PATH - 1] = 0;
				if (!safe_path(theirs[ntheirs].path)) { why = "the host sent a path outside the map's folder"; goto done; }
				ntheirs++;
			}
			last_heard = now;
			last_req = 0;   /* the next page at once */
		}
		if (now - last_draw >= DL_DRAW_MS) {
			dl_draw("Connecting to the host...", "", 10);
			last_draw = now;
		} else Sleep(1);
	}

	/* 2. what differs: files to fetch, and files the host doesn't have */
	if (!list_tree(dir, "", 0, mine, &nmine)) log_msg("net: map download: couldn't list every file in %s", dir);
	for (u32 i = 0; i < ntheirs; i++) {
		int k = find_file(mine, nmine, theirs[i].path);
		need[i] = k < 0 || mine[k].size != theirs[i].size || mine[k].hash != theirs[i].hash;
		if (!need[i]) continue;
		nneed++;
		need_bytes += theirs[i].size;
		if (theirs[i].size > biggest) biggest = theirs[i].size;
	}
	for (u32 i = 0; i < nmine; i++) nextra += find_file(theirs, ntheirs, mine[i].path) < 0;
	log_msg("net: map download: the host's map has %d files; %d to download (%u bytes), %d here that it doesn't have",
	        ntheirs, nneed, need_bytes, nextra);
	got = VirtualAlloc(0, biggest / MAP_CHUNK / 8 + 1, 0x3000, 4);
	if (!got) { why = "out of memory"; goto done; }

	/* 3. each file, into the download folder */
	began = GetTickCount();
	for (u32 i = 0, left = nneed; i < ntheirs; i++) {
		const MapFile *m = &theirs[i];
		u32 chunks = (m->size + MAP_CHUNK - 1) / MAP_CHUNK, lo = 0, win = 64, grown_at = 0, last_loss = 0, done_bytes = 0;
		if (!need[i]) continue;
		fmt(path, "%s%s", stage, m->path);
		make_dirs(path);
		out = CreateFileA(path, 0x40000000 /* GENERIC_WRITE */, 1, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
		if (out == INVALID_HANDLE_VALUE) { why = "can't write to GameDataFolder\\CustomLevels.download"; goto done; }
		cur = (int)i;
		memset(got, 0, chunks / 8 + 1);
		memset(g_req_chunk, 0, sizeof g_req_chunk);
		last_heard = last_draw = 0;
		last_heard = GetTickCount();
		while (lo < chunks) {
			u32 n, end, run = 0, run_start = 0;
			int lost = 0, heard = 0;
			now = GetTickCount();
			if (dl_cancelled()) { why = "cancelled"; goto done; }
			if (now - last_heard > LOST_MS) { why = "the host stopped answering"; goto done; }
			while ((n = dl_receive(buf, sizeof buf))) {
				const PktMapData *p = (const PktMapData *)buf;
				u32 w;
				if (p->h.type != PKT_MAP_DATA || n < sizeof *p || p->level != level || p->file != i || p->chunk >= chunks ||
				    (got[p->chunk >> 3] & (1 << (p->chunk & 7)))) continue;
				if (p->size != (p->chunk + 1 < chunks ? MAP_CHUNK : m->size - p->chunk * MAP_CHUNK) || n < sizeof *p + p->size) continue;
				SetFilePointer(out, (int)(p->chunk * MAP_CHUNK), 0, 0 /* FILE_BEGIN */);
				if (!WriteFile(out, buf + sizeof *p, p->size, &w, 0) || w != p->size) { why = "can't write the file (disk full?)"; goto done; }
				got[p->chunk >> 3] |= 1 << (p->chunk & 7);
				done_bytes += p->size;
				bytes_all += p->size;
				heard = 1;
			}
			if (heard) last_heard = now;
			while (lo < chunks && (got[lo >> 3] & (1 << (lo & 7)))) lo++;
			if (lo - grown_at >= win) {
				win = win + 32 > DL_WINDOW_MAX ? DL_WINDOW_MAX : win + 32;
				grown_at = lo;
			}
			/* ask for the pieces in the window not asked for yet, or not here DL_RTO_MS later */
			end = lo + win < chunks ? lo + win : chunks;
			for (u32 c = lo; c < end; c++) {
				u32 slot = c % DL_SLOTS;
				int due = !(got[c >> 3] & (1 << (c & 7))) &&
				          (g_req_chunk[slot] != c + 1 || now - g_req_time[slot] >= DL_RTO_MS);
				if (due) {
					if (g_req_chunk[slot] == c + 1) lost = 1;
					if (!run) run_start = c;
					run++;
					g_req_chunk[slot] = c + 1;
					g_req_time[slot] = now;
				}
				if (run && (!due || run == MAP_REQ_MAX || c + 1 == end)) {
					PktMapReq q;
					head(&q.h, PKT_MAP_REQ);
					q.level = level;
					q.file = i;
					q.chunk = run_start;
					q.count = run;
					send_to_peer(&q, sizeof q);
					run = 0;
				}
			}
			if (lost && now - last_loss >= DL_RTO_MS) {
				win = win / 2 < DL_WINDOW_MIN ? DL_WINDOW_MIN : win / 2;
				grown_at = lo;
				last_loss = now;
			}
			if (now - last_draw >= DL_DRAW_MS) {
				u32 ms = now - began ? now - began : 1;
				size_text(s1, done_bytes, m->size);
				size_text(s2, m->size, m->size);
				speed_text(s3, bytes_all / ms * 1000 + bytes_all % ms * 1000 / ms);
				fmt(a, "Downloading (%d/%d)", left, ntheirs);
				fmt(b, "%s of %s (%s)", s1, s2, s3);
				dl_draw(a, b, done_bytes / (m->size / 100 + 1) > 100 ? 100 : done_bytes / (m->size / 100 + 1));
				last_draw = now;
			} else if (!heard) {
				Sleep(1);
			}
		}
		CloseHandle(out);
		out = INVALID_HANDLE_VALUE;
		if (hash_file(path) != m->hash) { why = "a file arrived damaged"; goto done; }
		log_msg("net: map download: got %s (%u bytes)", m->path, m->size);
		left--;
	}
	cur = -1;

	/* 4. the old files out of the folder (to the backup), the new ones in */
	for (u32 i = 0; i < nmine; i++) {
		int k = find_file(theirs, ntheirs, mine[i].path);
		moved[i] = 0;
		if (k >= 0 && !need[k]) continue;
		fmt(path, "%s%s", dir, mine[i].path);
		fmt(to, "%s%s", backup, mine[i].path);
		if (!move_file(path, to)) {   /* put back what moved, and leave the map as it was */
			for (u32 j = 0; j < i; j++) {
				if (!moved[j]) continue;
				fmt(path, "%s%s", backup, mine[j].path);
				fmt(to, "%s%s", dir, mine[j].path);
				move_file(path, to);
			}
			why = "the map's files are in use";
			goto done;
		}
		moved[i] = 1;
	}
	for (u32 i = 0; i < ntheirs; i++) {
		if (!need[i]) continue;
		fmt(path, "%s%s", stage, theirs[i].path);
		fmt(to, "%s%s", dir, theirs[i].path);
		if (!move_file(path, to)) {
			log_msg("net: map download: the map's folder is half updated; its old files are in %s", backup);
			why = "can't move the downloaded files into the map's folder";
			goto done;
		}
	}
	log_msg("net: map download: done, %u bytes in %u ms%s%s; the host's map is %08x", bytes_all, GetTickCount() - began,
	        nmine ? "; old files in " : "", nmine ? backup : "", their_map);
	ok = 1;

done:
	if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
	g_dl_why = why ? why : "?";
	if (!ok) {
		log_msg("net: map download of level %d stopped: %s; this PC keeps its own version", level, g_dl_why);
		for (u32 i = 0; i < ntheirs && cur >= 0; i++) {   /* the downloaded files, left in the download folder */
			if (!need[i]) continue;
			fmt(path, "%s%s", stage, theirs[i].path);
			DeleteFileA(path);
			if ((int)i == cur) break;
		}
	}
	dl_lines_hide();
	if (got) VirtualFree(got, 0, 0x8000 /* MEM_RELEASE */);
	VirtualFree(theirs, 0, 0x8000);
	if (g_dl_quit) {
		PostQuitMessage(g_dl_quit & 0xffff);
		g_dl_quit = 0;
	}
	return ok;
}

/* The mounted level's folder, replaced by the host's version, mounted again */
static int map_download(u32 level)
{
	const char *dir = custom_game_level_dir(level), *folder = custom_game_level_folder(level);

	if (!dir || !folder || !map_download_to(level, dir, folder)) return 0;
	if (!custom_game_remount(level)) log_msg("net: map download: couldn't mount the new files");
	g_map_hash = hash_map(level);
	log_msg("net: map download: the map's files are now %08x", g_map_hash);
	g_problem = 0;
	return 1;
}

/* The level load's TMrLevel_Load call: the host's files first when they're wanted */
static u16 ONI_FC level_load(u32 level, u32 edx)
{
	u32 l = g_dl_level;

	if (l && (level & 0xff) == l) {
		g_dl_level = 0;
		if (!map_download(l)) g_dl_declined = l;
	}
	return TMrLevel_Load(level, edx);
}

static void begin_run(void);
static int net_open(void);

/* A joiner without the host's map at all (Join's Play, custom_game.c): it's downloaded from the
 * main menu, before the level starts, into GameDataFolder\CustomLevels\<folder>\, in Oni's
 * loading popup made the way the level load makes it (0x505a0b..0x505a39: the UI context saved
 * in *0x5ed2f0, psui_oniUI current, dialog 0x9b with proc 0x5068e0) and closed the way the level
 * load closes it (0x50687d: delete, context back), so the level load opens its own as usual.
 * A failure leaves no level to load, which Oni can't handle (TMrLevel_Load only logs "This level
 * is not valid" and nothing runs), so this one can't happen inside the level load. */
#define ONgLoadingContext       (*(void **)0x5ed2f0)
#define ONcLoadingDialogID      0x9b
#define ONrLoadingDialog_Proc   0x5068e0
#define ONcUIContextOni         "psui_oniUI"
#define WMrDialog_Create        ((u16 (ONI_FC *)(u32, WMtWindow *, u32, u32, WMtWindow **))0x468830)
#define WMrWindow_Delete        ((void (ONI_FC *)(WMtWindow *))0x474950)

int net_map_fetch(const char *folder, u32 level)
{
	char rel[MAX_PATH], dir[MAX_PATH];
	void *ui;
	int ok;

	g_dl_why = "can't reach the host";
	/* the host names the folder: one name, nothing that leads out of CustomLevels */
	if (!safe_path(folder) || str_len(folder) >= 64) {
		log_msg("net: the host's map folder \"%s\" isn't a folder name, not downloading it", folder);
		g_dl_why = "the host's map folder has a bad name";
		return 0;
	}
	for (const char *c = folder; *c; c++)
		if (*c == '\\') { g_dl_why = "the host's map folder has a bad name"; return 0; }
	if (g_mode != NET_JOIN || (g_sock == INVALID_SOCKET && !net_open())) return 0;
	fmt(rel, "GameDataFolder\\CustomLevels\\%s\\", folder);
	if (!GetFullPathNameA(rel, sizeof dir, dir, 0)) return 0;
	log_msg("net: this PC doesn't have the host's map %s (level %d): downloading it to %s", folder, level, dir);

	if (!ONgLoadingDialog) {
		ONgLoadingContext = ONiUI_GetContext();
		if ((ui = ONiUI_FindContext(ONcUIContextOni))) ONiUI_SetContext(ui);
		WMrDialog_Create(ONcLoadingDialogID, 0, ONrLoadingDialog_Proc, 0xffffffffu, &ONgLoadingDialog);
	}
	ok = map_download_to(level, dir, folder);
	if (ONgLoadingDialog) {
		WMrWindow_Delete(ONgLoadingDialog);
		ONgLoadingDialog = 0;
		ONiUI_SetContext(ONgLoadingContext);
	}
	if (!ok) {   /* a half-made folder isn't a map: what arrived stays in CustomLevels.download */
		RemoveDirectoryA(dir);
		return 0;
	}
	return 1;
}

const char *net_map_fetch_error(void)
{
	return g_dl_why ? g_dl_why : "?";
}

/* Players this game takes: the netcode has the host and one joiner */
#define NET_MAX_PLAYERS 2

static void answer_info(const PktInfoReq *q, const SOCKADDR_IN *to)
{
	PktInfo p;
	const char *folder = custom_game_level_folder(g_level), *map = custom_game_level_name(g_level);

	memset(&p, 0, sizeof p);
	head(&p.h, PKT_INFO);
	p.token = q->token;
	p.sent = q->sent;
	p.level = g_level;
	p.players = g_state == S_LOBBY ? lobby_players() : g_state == S_RUN || g_state == S_WAIT || g_state == S_OVER ? 1u + g_have_peer : 1;
	p.max_players = g_max_players < NET_MAX_PLAYERS ? g_max_players : NET_MAX_PLAYERS;
	p.spectators = 0;   /* no spectators yet */
	p.max_spectators = g_max_spectators;
	p.in_match = g_state != S_LOBBY;
	p.server_id = g_server_id;
	str_cpy(p.name, sizeof p.name, g_server_name);
	if (folder) str_cpy(p.folder, sizeof p.folder, folder);
	if (map) str_cpy(p.map, sizeof p.map, map);
	else fmt(p.map, "Level %d", g_level);
	send_to(&p, sizeof p, to);
}

/* ---- The match's end ------------------------------------------------------------------------ *
 * players.c marks the tick that reached the score or time limit (tick state, so a rollback that
 * changes the score changes it too). Once that tick ran with the peer's real inputs (rollback:
 * g_verified past it; with input delay every tick does; with the peer lost there's nothing to
 * wait for) the match is over: S_OVER, the game stands (the clock stops, as in S_WAIT) and
 * mp_hud.c shows the result. Each PC's player presses fire (the left mouse button: the clock is
 * stopped, so no action records come) and says so in PKT_END. The host sends "go" when everyone
 * did, or END_MS after its own end, and both PCs reload the level into the lobby (as a match
 * start does, but without the match): the joiner is back in the host's lobby and F3 starts the
 * next match. A joiner that never hears "go" goes back on its own 5 s later. Inputs keep going
 * out meanwhile, so a peer a few ticks behind can still finish the match's last ticks.
 *
 * While the game stands the mouse turns the camera as it turns the player in a tick, but only the
 * camera: the follow camera (gs+0x80 mode 1, its character at +0x54) sits behind the character's
 * look direction (active +0x21b0, from 0x4d8540: normalize(sin yaw, sin pitch, cos yaw), yaw the
 * facing +0x170 - +0x174 plus the aim's +0x21bc, pitch +0x21c0) and eases there every tick
 * (0x4d5ca0 with 1, 0, 0, as at 0x4fb141). over_look keeps its own yaw and pitch, turns them by the
 * aim in the frame's action records (+0x24 left/right, +0x28 up/down, 0x30 bytes each) as
 * 0x4f7b90 does (1/64 radian a unit, times half the weapon's +0x7c aim speed while one is held;
 * pitch within +-pi/2), puts them in the look direction and pitch for the camera's ticks, one per
 * 1/60 s, and puts the character's back. The records come from stock Oni's input thread on its
 * own, from Daodan's input (0x403be0 replaced) once its clock moved past the tick, with the mouse
 * moves it took in between, so this works the same with either. */

static int focused(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();
	if (w) GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId();
}

static int fire_down(void)
{
	return focused() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

static void send_end(int go)
{
	PktEnd p;
	u32 now = GetTickCount();
	memset(&p, 0, sizeof p);
	head(&p.h, PKT_END);
	p.level = g_level;
	p.ready = g_end_ready;
	p.go = go;
	p.players = 1 + !g_over_lost;
	p.ready_count = (g_end_ready != 0) + (!g_over_lost && g_peer_end_ready);
	p.left_ms = now - g_over_at < END_MS ? END_MS - (now - g_over_at) : 0;
	send_to_peer(&p, sizeof p);
	g_last_end = now;
}

static void back_to_lobby(const char *why)
{
	u32 level = g_level;
	log_msg("net: back to the lobby (%s): reloading level %d", why, level);
	g_over_lost = 1;   /* no goodbye: the other PC goes back too */
	g_match_pending = 0;
	session_end("match over");
	reload_level(level, 0);
}

static void end_received(const PktEnd *p)
{
	if (p->level != g_level) return;
	if (g_mode == NET_HOST) {
		if (g_peer_end_ready != (int)p->ready && p->ready) log_msg("net: the joiner pressed fire");
		g_peer_end_ready = p->ready;
		return;
	}
	g_end_players = p->players;
	g_end_ready_count = p->ready_count;
	g_end_left_ms = p->left_ms;
	g_end_rx = GetTickCount();
	if (p->go && (g_state == S_RUN || g_state == S_LOST || g_state == S_OVER)) back_to_lobby("the host said so");
}

/* The tick that ended the match ran with real inputs on this PC */
static int over_confirmed(void)
{
	u32 t = players_over_tick();
	if (!t) return 0;
	if (g_state == S_LOST || !ROLLING) return 1;
	return t - g_start_tick <= g_verified;
}

static void enter_over(u32 now)
{
	int w = players_winner();
	g_over_lost = g_state == S_LOST;
	g_state = S_OVER;
	g_over_at = now;
	g_last_end = 0;
	g_end_ready = 0;
	g_fire_down = fire_down();   /* fire held as the match ended doesn't count */
	g_end_players = 2;
	g_end_ready_count = 0;
	g_end_left_ms = END_MS;
	g_end_rx = now;
	g_look_who = 0;
	log_msg("net: the match is over after %d ticks: %s %d, %s %d; %s", g_ticks, mp_name(0), players_score(0), mp_name(1),
	        players_score(1), w < 0 ? "a draw" : w == 0 ? "the host won" : "the joiner won");
}

#define REC_AIM_LR(r)           (*(const float *)((r) + 0x24))
#define REC_AIM_UD(r)           (*(const float *)((r) + 0x28))
#define CHR_FACING(c)           (*(float *)((c) + 0x170))
#define CHR_FACING_TURN(c)      (*(float *)((c) + 0x174))
#define CHR_WEAPON(c)           (*(u8 **)((c) + 0x194))
#define WEAPON_AIM_SPEED(w)     (*(float *)(*(u8 **)((w) + 4) + 0x7c))   /* the weapon's class, +0x7c */
#define ACT_LOOK(a)             ((float *)((a) + 0x21b0))
#define ACT_AIM_YAW(a)          (*(float *)((a) + 0x21bc))
#define ACT_AIM_PITCH(a)        (*(float *)((a) + 0x21c0))
#define AIM_RAD                 0.015625f
#define HALF_PI                 1.5707964f
#define TWO_PI                  6.2831855f

static float g_look_yaw, g_look_pitch;
static u32 g_look_at, g_look_acc;      /* GetTickCount of the last frame; ms x 60 not yet a tick */

static float fsqrt(float x)
{
	float r;
	__asm__("fsqrt" : "=t"(r) : "0"(x));
	return r;
}

static void fsincos(float a, float *s, float *c)
{
	long double sv, cv;
	__asm__("fsincos" : "=t"(cv), "=u"(sv) : "0"((long double)a));
	*s = (float)sv;
	*c = (float)cv;
}

static void over_look(u32 count, const u8 *actions, u32 now)
{
	u8 *gs = ONgGameState, *cam = gs ? GS_CAMERA(gs) : 0, *who, *act;
	float lr = 0, ud = 0, sy, cy, sp, cp, l, look[3], pitch;
	u32 n;

	if (!cam || *(u32 *)cam != 1 || !(who = CAM_TARGET(cam)) || !(act = ONrCharacter_GetActive(who))) return;
	if (who != g_look_who) {
		g_look_who = who;
		g_look_yaw = CHR_FACING(who) - CHR_FACING_TURN(who) + ACT_AIM_YAW(act);
		g_look_pitch = ACT_AIM_PITCH(act);
		g_look_at = now;
		g_look_acc = 0;
	}

	if (actions && !live_menu_open() && !char_menu_open())
		for (u32 i = 0; i < (count & 0xffff); i++) {
			lr += REC_AIM_LR(actions + i * 0x30);
			ud += REC_AIM_UD(actions + i * 0x30);
		}
	if (CHR_WEAPON(who)) {
		float k = WEAPON_AIM_SPEED(CHR_WEAPON(who)) * 0.5f;
		lr *= k;
		ud *= k;
	}
	if (ONgInvertLook) ud = -ud;
	g_look_yaw -= lr * AIM_RAD;
	while (g_look_yaw < 0) g_look_yaw += TWO_PI;
	while (g_look_yaw >= TWO_PI) g_look_yaw -= TWO_PI;
	g_look_pitch += ud * AIM_RAD;
	if (g_look_pitch < -HALF_PI) g_look_pitch = -HALF_PI;
	if (g_look_pitch > HALF_PI) g_look_pitch = HALF_PI;

	g_look_acc += (now - g_look_at) * 60;
	g_look_at = now;
	n = g_look_acc / 1000;
	g_look_acc -= n * 1000;
	if (n > 6) n = 6;   /* as many as a frame runs */
	if (!n) return;

	fsincos(g_look_yaw, &sy, &cy);
	fsincos(g_look_pitch, &sp, &cp);
	l = fsqrt(sy * sy + sp * sp + cy * cy);
	memcpy(look, ACT_LOOK(act), sizeof look);
	pitch = ACT_AIM_PITCH(act);
	ACT_LOOK(act)[0] = sy / l;
	ACT_LOOK(act)[1] = sp / l;
	ACT_LOOK(act)[2] = cy / l;
	ACT_AIM_PITCH(act) = g_look_pitch;
	while (n--) ONrCamera_Update(1, 0, 0);
	memcpy(ACT_LOOK(act), look, sizeof look);
	ACT_AIM_PITCH(act) = pitch;
}

static u32 over_frame(u32 count, const u8 *actions, u32 now, u32 *ticks_run)
{
	static int help_down;
	int down = fire_down(), help = focused() && (GetAsyncKeyState(VK_F1) & 0x8000) != 0;

	/* F1 from the key itself: the records skip frames while the clock stands (and are stripped) */
	if (help && !help_down && !live_menu_open() && !chat_blocking()) ONgGameState[GS_HELP_REQUEST] = 1;
	help_down = help;
	if (down && !g_fire_down && !g_end_ready && !live_menu_open() && !char_menu_open() && !chat_blocking()) {
		g_end_ready = 1;
		log_msg("net: fire pressed: ready to go back to the lobby");
		if (g_have_peer) send_end(0);
	}
	g_fire_down = down;
	if (g_have_peer && now - g_last_end >= END_SEND_MS) send_end(0);
	if (now - g_last_send >= 100) send_inputs();   /* the peer may still need our last ones */
	if (g_mode == NET_HOST) {
		int quiet = (int)(now - g_last_recv) > LOST_MS;
		if ((g_end_ready && (g_peer_end_ready || g_over_lost || quiet)) || now - g_over_at >= END_MS) {
			for (int i = 0; i < 3; i++) send_end(1);
			back_to_lobby(now - g_over_at >= END_MS ? "time's up" : "everyone pressed fire");
			if (ticks_run) *ticks_run = 0;
			return 0;
		}
	} else if (now - g_over_at >= END_MS + 5000) {
		back_to_lobby("nothing from the host");
		if (ticks_run) *ticks_run = 0;
		return 0;
	}
	players_net_frame(0, 0);
	u32 err = run_update(0, ticks_run);   /* the game stands */
	over_look(count, actions, now);       /* but the camera turns */
	return err;
}

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
		if (h->type == PKT_INFO_REQ && g_mode == NET_HOST && (u32)n >= sizeof(PktInfoReq)) {
			answer_info((const PktInfoReq *)buf, &from);
			continue;
		}
		if (g_have_peer && (from.addr != g_peer.addr || from.port != g_peer.port)) {
			/* one joiner at a time: another only takes an empty lobby's place */
			if (g_mode == NET_JOIN || g_state != S_LOBBY || g_peer_seen) continue;
		}
		if (g_state == S_IDLE) continue;   /* not in a level yet: the peer will resend */
		if (h->proto != PROTO) {   /* another Arklite: its packets can't be read */
			if (!(g_problem & NETBAD_PROTO) || GetTickCount() - g_problem_at > LOST_MS)
				log_msg("net: packet of protocol %d from the %s, this is %d: use the same Arklite.dll on both PCs", h->proto,
				        g_mode == NET_HOST ? "joiner" : "host", PROTO);
			refuse(g_mode == NET_HOST ? "joiner" : "host", NETBAD_PROTO, 0);
			continue;
		}
		if (g_mode == NET_HOST && (h->type == PKT_MAP_LIST_REQ || h->type == PKT_MAP_REQ)) {
			map_serve(buf, n, &from);
			continue;
		}

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
				u32 bad = compat_problems(&p->c);
				if (bad) {   /* not the same game: it stays out of the lobby, and hears why */
					refuse("joiner", bad, &p->c);
					send_lobby_to(0, bad);
					continue;
				}
				if (!g_peer_seen) {
					const u8 *ip = (const u8 *)&from.addr;
					log_msg("net: %d.%d.%d.%d:%d joined the lobby", ip[0], ip[1], ip[2], ip[3], ntohs(from.port));
					check_files("joiner", &p->c);
					g_peer_seen = 1;
					g_problem = 0;
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
				u32 bad = compat_problems(&p->c);
				if (bad) {   /* changed since the lobby: no START, the match doesn't begin */
					refuse("joiner", bad, &p->c);
					continue;
				}
				const u8 *ip = (const u8 *)&from.addr;
				log_msg("net: %d.%d.%d.%d:%d joined (input delay %d ticks there)", ip[0], ip[1], ip[2], ip[3], ntohs(from.port), p->delay);
				check_start("joiner", p->start_tick, p->state_hash, p->fpu_cw, &p->c);
				lockstep_get_seeds(&g_seed, &g_local_seed);
				begin_run();
			}
			send_start();   /* again for every HELLO: the first START may be lost */
		} else if (h->type == PKT_LOBBY && g_mode == NET_JOIN && (u32)n >= sizeof(PktLobby)) {
			const PktLobby *p = (const PktLobby *)buf;
			g_last_recv = GetTickCount();
			if (g_state != S_LOBBY || p->level != g_level) continue;
			if (p->refused) {   /* the host keeps this PC out: nothing else in it counts */
				refuse("host", p->refused, 0);
				/* only the map differs and the host can send it: reload, downloading it first */
				if (p->refused == NETBAD_MAP && p->map_offer) map_download_start();
				continue;
			}
			if (!g_host_heard) log_msg("net: in the host's lobby");
			g_host_heard = 1;
			g_problem = 0;
			/* the host's gameplay settings: for the warm-up, and the match's level reload */
			players_set_respawn_ticks(p->respawn_ticks);
			items_set_default_ticks(p->item_ticks);
			g_time_limit = p->time_limit;
			g_score_limit = p->score_limit;
			g_lobby_players = p->players;
			g_lobby_ready = p->ready;
			g_lobby_countdown = p->countdown_ms;
			g_lobby_rx = g_last_recv;
			if (p->go) {
				g_match_seed = p->seed;
				g_match_local_seed = p->local_seed;
				start_match();
			}
		} else if (h->type == PKT_START && g_mode == NET_JOIN && (u32)n >= sizeof(PktStart)) {
			const PktStart *p = (const PktStart *)buf;
			g_last_recv = GetTickCount();
			if (g_state != S_WAIT) continue;
			if (p->level != g_level) {
				log_msg("net: the host is on level %d, this is level %d", p->level, g_level);
				continue;
			}
			u32 bad = compat_problems(&p->c);
			if (bad) {   /* changed since the lobby: the match doesn't begin */
				refuse("host", bad, &p->c);
				continue;
			}
			check_start("host", p->start_tick, p->state_hash, p->fpu_cw, &p->c);
			g_seed = p->seed;
			g_local_seed = p->local_seed;
			if (p->delay <= 30) g_delay = p->delay;   /* the host's, on both PCs (0: rollback) */
			begin_run();
		} else if (h->type == PKT_END && (u32)n >= sizeof(PktEnd)) {
			if (!g_have_peer || from.addr != g_peer.addr || from.port != g_peer.port) continue;
			end_received((const PktEnd *)buf);
		} else if (h->type == PKT_INPUT && (g_state == S_RUN || g_state == S_OVER)) {
			g_last_recv = GetTickCount();
			if (!g_ninputs_recv++) log_msg("net: first input from the %s", g_mode == NET_HOST ? "joiner" : "host");
			on_input((const PktInput *)buf, n);
		} else if (h->type == PKT_NAME && (u32)n >= sizeof(PktName)) {
			if (!g_have_peer || from.addr != g_peer.addr || from.port != g_peer.port) continue;
			const PktName *p = (const PktName *)buf;
			char name[MP_NAME_MAX];
			memcpy(name, p->name, sizeof name);
			name[sizeof name - 1] = 0;
			mp_name_set_peer(name);
		} else if ((h->type == PKT_CHAT && (u32)n >= sizeof(PktChat)) || (h->type == PKT_CHAT_ACK && (u32)n >= sizeof(PktChatAck))) {
			if (!g_have_peer || from.addr != g_peer.addr || from.port != g_peer.port) continue;
			if (h->type == PKT_CHAT) chat_received((const PktChat *)buf);
			else chat_acked((const PktChatAck *)buf);
		} else if (h->type == PKT_BYE && g_state == S_RUN) {
			log_msg("net: the %s left; their player stands still", g_mode == NET_HOST ? "joiner" : "host");
			g_lost_left = 1;
			g_state = S_LOST;
		} else if (h->type == PKT_BYE && g_state == S_OVER && !g_over_lost) {
			log_msg("net: the %s left after the match", g_mode == NET_HOST ? "joiner" : "host");
			g_over_lost = 1;
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
	{
		u32 map = hash_map(level);
		if (map != g_map_hash) log_msg("net: level %d's files: %08x", level, map);
		g_map_hash = map;
	}
	g_ticks = g_remote = g_peer_ack = g_sent_gen = 0;
	g_stall_frames = g_stall_ticks = 0;
	g_peer_adv = g_peer_adv_known = 0;
	g_peer_stamp = g_peer_stamp_at = g_ping_sum = g_ping_n = g_ping_max = 0;
	g_sync_skip_at = g_sync_skipped = 0;
	g_has_pending = 0;
	memset(g_local_in, 0, sizeof g_local_in);
	memset(g_remote_at, 0, sizeof g_remote_at);
	memset(g_last_local, 0, sizeof g_last_local);
	memset(g_mine, 0, sizeof g_mine);
	memset(g_theirs, 0, sizeof g_theirs);
	memset(g_checked, 0, sizeof g_checked);
	g_last_hash_tick = g_last_hash = 0;
	g_checks = g_bad_checks = g_first_bad = 0;
	memset(g_published, 0, sizeof g_published);
	memset(g_pred, 0, sizeof g_pred);
	g_verified = 0;
	g_rb_started = 0;
	g_rollbacks = g_resim_ticks = g_rb_failed = g_rb_deepest = g_rb_frames = 0;
	g_npackets_sent = g_npackets_recv = g_ninputs_recv = g_send_errors = 0;
	g_last_status = GetTickCount();
	g_gen = g_delay;   /* ticks 0..delay-1: no input from either PC */
	g_start_tick = GS_TICK(ONgGameState);
	g_start_fpu = fpu_control_word();
	g_start_hash = summarize_with(c, &n, 0);
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
	g_peer_end_ready = 0;
	g_ping = -1;
	players_reset_scores();
	players_set_limits(1, g_time_limit * 3600, g_score_limit);
	log_msg("net: game on (%s), RNG %08x/%08x, input delay %d ticks; limits: %d min, %d points", mode_name(), g_seed,
	        g_local_seed, g_delay, g_time_limit, g_score_limit);
}

static void session_end(const char *why)
{
	if (g_state == S_IDLE) return;
	if (g_state == S_RUN || (g_state == S_OVER && !g_over_lost)) send_bye();
	players_set_limits(0, 0, 0);
	lockstep_net_record_end(why);
	if (g_rb_started) {
		rollback_net_end();
		rollback_impulses(1);
		ONgSoundOn = g_sound_on;
		log_msg("net: rollback: %d rollbacks re-ran %d ticks (deepest %d), %d couldn't go back far enough",
		        g_rollbacks, g_resim_ticks, g_rb_deepest, g_rb_failed);
		g_rb_started = 0;
	}
	if (g_state == S_RUN || g_state == S_LOST || g_state == S_OVER)
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

	if (live_menu_open() || char_menu_open()) {
		memset(in, 0, 0x30);                 /* a menu is up: our player stands */
		g_has_pending = 0;
	} else if (g_has_pending) {
		memcpy(in, g_pending, 0x30);
		g_has_pending = 0;
	} else {
		memcpy(in, g_last_local, 0x30);      /* the buttons stay held, no aim */
		in[7] = in[8] = in[9] = in[10] = 0;
	}
	in[11] = char_menu_record_word();    /* the F2 choice (players.c) */
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
	return pressed && !live_menu_open() && !chat_blocking();
}

/* Reload the level for the match: both PCs do it, the host when its countdown ends, the joiner
 * when "go" arrives. The next frame begins a synced session (S_WAIT, then the START handshake).
 * Loading a level draws from the game RNG (a character's idle timer +0x1e4 comes from it at
 * creation, 0x4dc130), and the lobby ran each PC's RNG its own way, so both PCs load with the
 * seeds the host put in "go". */
static void start_match(void)
{
	u32 level = g_level;

	if (g_mode == NET_HOST) {
		lockstep_get_seeds(&g_match_seed, &g_match_local_seed);
		for (int i = 0; i < 3; i++) send_lobby(1);   /* and again for each HELLO that still says lobby */
	}
	log_msg("net: the match starts: reloading level %d (RNG %08x/%08x)", level, g_match_seed, g_match_local_seed);
	g_match_pending = 1;
	session_end("match starting");
	reload_level(level, 1);
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
			if (live_menu_open() || char_menu_open()) memset(g_frame_p2[k], 0, 0x30);
			g_frame_p2[k][11] = char_menu_record_word();
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
	           g_state == S_WAIT ? NETHUD_STARTING : g_state == S_RUN || g_state == S_LOST || g_state == S_OVER ? NETHUD_MATCH :
	           NETHUD_OFF;
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
	if (g_problem && g_problem_at && now - g_problem_at < PROBLEM_MS) h->problem = g_problem;
	h->desynced = (g_state == S_RUN || g_state == S_LOST) && g_bad_checks;
	if (g_state == S_RUN && (int)(now - g_last_recv) > 1500) h->silent_ms = (int)(now - g_last_recv);
	if (g_state == S_LOST) h->lost = g_lost_left ? 2 : 1;
	h->time_limit = g_time_limit;
	h->score_limit = g_score_limit;
	h->ping_ms = g_state == S_RUN || g_state == S_OVER ? g_ping : -1;
	h->peer_present = g_state == S_LOBBY ? (g_mode == NET_HOST ? g_peer_seen : g_host_heard) :
	                  g_state == S_WAIT || g_state == S_RUN || (g_state == S_OVER && !g_over_lost);
	if (g_state == S_OVER) {
		h->over = 1;
		h->over_ready = g_end_ready;
		if (g_mode == NET_HOST) {
			h->over_players = 1 + !g_over_lost;
			h->over_ready_count = (g_end_ready != 0) + (!g_over_lost && g_peer_end_ready);
			h->over_left_ms = now - g_over_at < END_MS ? (int)(END_MS - (now - g_over_at)) : 0;
		} else {
			h->over_players = g_end_players;
			h->over_ready_count = g_end_ready_count;
			h->over_left_ms = (int)g_end_left_ms - (int)(now - g_end_rx);
			if (h->over_left_ms < 0) h->over_left_ms = 0;
		}
	}
}

/* ---- Rollback ------------------------------------------------------------------------------ */

static int same_input(const u32 *a, const u32 *b)
{
	for (int i = 0; i < 12; i++)
		if (a[i] != b[i]) return 0;
	return 1;
}

/* The inputs of ticks g_ticks.. g_ticks+n-1 into the frame records: ours, and the peer's real one
 * or, past g_remote, a guess: its last real input with its buttons held and no stick or mouse
 * movement (as make_input repeats ours). The peer's input each tick runs with goes to g_pred. */
static void rb_fill_frames(u32 n)
{
	u32 guess[12];
	if (g_remote) memcpy(guess, g_remote_in[(g_remote - 1) % RING], sizeof guess);
	else memset(guess, 0, sizeof guess);
	guess[7] = guess[8] = guess[9] = guess[10] = 0;
	for (u32 k = 0; k < n; k++) {
		u32 t = g_ticks + k, slot = t % RING;
		const u32 *mine = g_local_in[slot], *theirs = t < g_remote ? g_remote_in[slot] : guess;
		memcpy(g_pred[slot], theirs, 0x30);
		memcpy(g_frame_p1[k], g_mode == NET_HOST ? mine : theirs, 0x30);
		memcpy(g_frame_p2[k], g_mode == NET_HOST ? theirs : mine, 0x30);
	}
}

/* Run the ticks again from g_ticks (just put back) up to to, silently */
static void rb_resimulate(u32 to)
{
	rollback_impulses(0);   /* one-shot sounds already played; ambients start again */
	while (g_ticks < to) {
		u32 n = to - g_ticks, before = g_ticks;
		if (n > 6) n = 6;
		rb_fill_frames(n);
		players_net_frame(g_frame_p2, n);
		run_update(n, 0);
		if (g_ticks == before) break;   /* the update ran nothing: don't spin */
	}
	rollback_impulses(1);
}

static u32 rolling_frame(u32 count, void *actions, u32 *ticks_run, u32 wanted, u32 now)
{
	/* the peer's inputs that came in: were the guesses right? */
	u32 upto = g_remote < g_ticks ? g_remote : g_ticks;
	for (u32 t = g_verified; t < upto; t++) {
		if (same_input(g_pred[t % RING], g_remote_in[t % RING])) continue;
		u32 to = g_ticks, t0 = counter();
		if (rollback_restore_tick(t)) {
			u32 t1 = us(counter() - t0);
			g_ticks = t;
			rb_resimulate(to);
			t0 = us(counter() - t0);
			g_rb_us_sum += t0;
			g_rb_timed++;
			if (t0 > g_rb_us_max) g_rb_us_max = t0;
			if (t1 > g_rb_restore_us_max) g_rb_restore_us_max = t1;
			g_rollbacks++;
			g_resim_ticks += to - t;
			if (to - t > g_rb_deepest) g_rb_deepest = to - t;
		} else if (!g_rb_failed++) {
			log_msg("net: rollback: tick %d isn't kept any more (now %d): this game will come apart", t, g_ticks);
		}
		break;
	}
	g_verified = g_remote < g_ticks ? g_remote : g_ticks;
	publish_verified();

	/* this frame's ticks: as many as the clock asks, one fewer now and then while ahead of the
	 * peer (time sync), and never further past the peer's last real input than the snapshots reach */
	u32 n = wanted;
	if (n > 6) n = 6;
	if (n && g_peer_adv_known && ((int)g_ticks - (int)g_remote - g_peer_adv) / 2 >= 2 &&
	    (int)(g_ticks - g_sync_skip_at) >= SYNC_EVERY) {
		n--;
		g_sync_skip_at = g_ticks;
		g_sync_skipped++;
	}
	if (g_ticks + n > g_remote + RB_AHEAD) {
		u32 can = g_remote + RB_AHEAD > g_ticks ? g_remote + RB_AHEAD - g_ticks : 0;
		g_stall_frames++;
		g_stall_ticks += n - can;
		n = can;
	}
	if ((count & 0xffff) && actions) take_record((const u32 *)actions);
	while (g_gen < g_ticks + n) make_input();
	if (g_gen != g_sent_gen || now - g_last_send >= RESEND_MS) send_inputs();
	if (now - g_last_stall_log >= STALL_LOG_MS) {
		if (g_stall_frames)
			log_msg("net: in the last %d s, %d frames waited for the %s's inputs (%d ticks in all)",
			        STALL_LOG_MS / 1000, g_stall_frames, g_mode == NET_HOST ? "joiner" : "host", g_stall_ticks);
		if (g_rb_started)
			log_msg("net: rollback so far: %d rollbacks, %d ticks run again (deepest %d), %d pages kept per tick, "
			        "%d ambient sounds stopped; time sync left out %d ticks, now %d ahead of the %s; ping %d ms (max %d); "
			        "a rollback took %d.%d ms on average, at most %d.%d ms (putting back at most %d.%d ms)",
			        g_rollbacks, g_resim_ticks, g_rb_deepest, rollback_known_pages(), rollback_ambients_stopped(),
			        g_sync_skipped, ((int)g_ticks - (int)g_remote - g_peer_adv) / 2, g_mode == NET_HOST ? "joiner" : "host",
			        g_ping_n ? g_ping_sum / g_ping_n : -1, g_ping_max,
			        g_rb_timed ? g_rb_us_sum / g_rb_timed / 1000 : 0, g_rb_timed ? g_rb_us_sum / g_rb_timed / 100 % 10 : 0,
			        g_rb_us_max / 1000, g_rb_us_max / 100 % 10, g_rb_restore_us_max / 1000, g_rb_restore_us_max / 100 % 10);
		g_ping_sum = g_ping_n = g_ping_max = 0;
		g_rb_us_sum = g_rb_us_max = g_rb_restore_us_max = g_rb_timed = 0;
		g_stall_frames = g_stall_ticks = 0;
		g_last_stall_log = now;
	}
	if (n) {
		g_last_status = now;
	} else if (now - g_last_status >= 1000) {
		log_msg("net: waiting at tick %d: the %s's inputs up to %d; packets sent %d, received %d",
		        g_ticks, g_mode == NET_HOST ? "joiner" : "host", g_remote, g_npackets_sent, g_npackets_recv);
		g_last_status = now;
	}

	rb_fill_frames(n);
	if (n && !g_rb_started) {   /* the match's first tick */
		lockstep_set_seeds(g_seed, g_local_seed);
		g_sound_on = ONgSoundOn;
		g_rb_started = rollback_net_begin();
		log_msg("net: rollback %s: no input delay, the %s's input guessed and corrected up to %d ticks back%s",
		        g_rb_started ? "on" : "FAILED", g_mode == NET_HOST ? "joiner" : "host", RB_AHEAD,
		        g_record ? " (recording is off with rollback)" : "");
	}
	players_net_frame(g_frame_p2, n);
	return run_update(n, ticks_run);
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
	if (g_reload) {   /* the session has ended: the game stands until the dialogs are gone */
		if (!live_menu_open()) {
			reload_level(g_reload - 1, g_reload_seeds);
			if (ticks_run) *ticks_run = 0;
			return 0;   /* as after any reload: the next frame begins the session */
		}
		live_menu_close();
		players_net_frame(0, 0);
		return run_update(0, ticks_run);
	}
	if (g_state != S_IDLE && (level != g_level || GS_TICK(gs) < g_last_tick))
		session_end(level != g_level ? "level left" : "level restarted");
	if (g_state == S_IDLE && level && gs) session_begin(level);
	if (g_state == S_IDLE) {
		players_net_frame(0, 0);
		return ONrGameState_Update(count, actions, ticks_run);
	}

	/* Esc: action bit 0 opens the pause menu inside the tick (0x4faec0), but it's kept out of the
	 * shared input (strip), so this PC opens its own menu when its record has Esc down */
	if ((count & 0xffff) && actions) {
		static int esc_down, help_down;
		int esc = (*(const u32 *)actions & 1) != 0, help = (*(const u32 *)actions & ACTION_HELP) != 0;
		if (esc && !esc_down && !live_menu_open() && !chat_blocking()) live_menu_request();   /* Esc closed the chat */
		esc_down = esc;
		/* F1 the same way: the tick's own request (0x4faf36) needs the bit, which is per PC */
		if (help && !help_down && !live_menu_open() && !char_menu_open() && !chat_blocking())
			gs[GS_HELP_REQUEST] = 1;
		help_down = help;
	}

	receive();
	now = GetTickCount();   /* after receive(), which stamps g_last_recv: now - g_last_recv mustn't wrap */
	if (g_have_peer && (g_name_sent != mp_name_serial() || now - g_last_name >= NAME_MS)) send_name();
	if (g_chat_out_n && now - g_chat_sent_at >= CHAT_RESEND_MS) chat_send_queued();
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
		g_lost_left = 0;
		g_state = S_LOST;
	}

	if ((g_state == S_RUN || g_state == S_LOST) && over_confirmed()) enter_over(now);
	if (g_state == S_OVER) return over_frame(count, (const u8 *)actions, now, ticks_run);

	if (g_state == S_RUN && ROLLING) return rolling_frame(count, actions, ticks_run, wanted, now);

	/* The ticks this frame runs, then this PC's inputs up to the delay past them: an input made
	 * now is always used g_delay ticks later. Making them ahead by wall-clock time instead let
	 * the queue grow (up to delay + 6 ticks) whenever the peer's inputs came late, and it never
	 * shrank back. A frame that makes no input merges its record into the next one. */
	u32 remote_avail = g_state == S_LOST ? 6 : g_remote - g_ticks;
	u32 n = wanted;
	if (n > remote_avail) {
		g_stall_frames++;
		g_stall_ticks += n - remote_avail;
		n = remote_avail;
	}
	if (n > 6) n = 6;
	if ((count & 0xffff) && actions) take_record((const u32 *)actions);
	while (g_gen < g_ticks + g_delay + n) make_input();
	if (n > g_gen - g_ticks) n = g_gen - g_ticks;
	if (g_state == S_RUN && (g_gen != g_sent_gen || now - g_last_send >= RESEND_MS)) send_inputs();
	if (now - g_last_stall_log >= STALL_LOG_MS) {
		if (g_stall_frames)
			log_msg("net: in the last %d s, %d frames waited for the %s's inputs (%d ticks late in all)",
			        STALL_LOG_MS / 1000, g_stall_frames, g_mode == NET_HOST ? "joiner" : "host", g_stall_ticks);
		g_stall_frames = g_stall_ticks = 0;
		g_last_stall_log = now;
	}
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
#define ONrHUD_Tick             ((void (*)(void))0x504780)

static u8 *g_swapped_p1;   /* Konoko while player 2 is in the player slot (display, HUD tick) */

u8 *net_swapped_konoko(void) { return g_swapped_p1; }
#define ONcTick_HUDCall         0x4fa9e4

/* The HUD's health ring and hypo bar animate toward values that 0x504780 takes, every tick, from
 * the character in gs+0xac (via 0x4fc8a0; into the trackers at 0x630fc0..), and it fades that
 * character's hit arrows (+0x166c, 4 bytes set by TakeDamage, read only by the HUD at
 * 0x500ddc). Neither is game state that matters, so on the joiner it runs with player 2 in the
 * player slot, like the display. */
static void hud_tick(void)
{
	u8 *gs = ONgGameState, *p1 = gs ? GS_PLAYER(gs) : 0, *view = view_character(p1);
	u32 b0;

	if (!view || view == p1) {
		ONrHUD_Tick();
		return;
	}
	b0 = *(u32 *)(gs + 0xb0);
	g_swapped_p1 = p1;
	GS_PLAYER(gs) = view;
	*(u8 **)(gs + 0xb0) = ONrCharacter_GetActive(view);
	ONrHUD_Tick();
	GS_PLAYER(gs) = p1;
	*(u32 *)(gs + 0xb0) = b0;
	g_swapped_p1 = 0;
}

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
	g_swapped_p1 = p1;
	GS_PLAYER(gs) = view;
	*(u8 **)(gs + 0xb0) = ONrCharacter_GetActive(view);
	display_rest();
	GS_PLAYER(gs) = p1;
	*(u32 *)(gs + 0xb0) = b0;
	g_swapped_p1 = 0;
}

/* ---- Sounds for "the player" ---------------------------------------------------------- *
 * Oni plays some sounds unpositioned (0x4fdac0: sound #id from the table at 0x631140) and only
 * for the player in gs+0xac, and keeps looping ones (0x4fdae0) for the player's low health,
 * overhealing and +0x1ba; all decided inside ticks, where gs+0xac is Konoko on every PC, so the
 * joiner heard the host's and never its own. On the joiner these spots look at player 2:
 *   0x51ca39  item pickup (sound 0xa), ItemAdd 0x51ca30: the player lookup there only decides
 *             the sound and the "new item" hint (0x50f560/0x50f580/0x5050a0, display only)
 *   0x4ddea9  "can't" sound 6 when reloading fails (character update 0x4ddba0), lookup + cmp
 *   0x4f755b  sound 6 in 0x4f6ad0: mov edx,[gs]; cmp esi,[edx+0xac] (12 bytes), only the sound
 *   0x4faa09  the tick's health sounds block: mov edi,[esi+0xac]; edi isn't used after it
 * And the hypo sound (4, at 0x4edb9c in item use 0x4eda40) played for every character that used
 * one: in a network game only this PC's own player's (and the AIs', as stock). */
#define ONrPlayer_Get           0x4fc8a0
#define ONrUISound_Play         0x4fdac0

static u8 *ONI_FC this_pc_player(void)
{
	u8 *gs = ONgGameState;
	return gs ? view_character(GS_PLAYER(gs)) : 0;
}

/* in place of 0x4fc8a0 (which only touches EAX) */
__attribute__((naked)) static void get_player_here(void)
{
	__asm__ volatile(
		"pushl %%ecx\n\t"
		"pushl %%edx\n\t"
		"call %c0\n\t"
		"popl %%edx\n\t"
		"popl %%ecx\n\t"
		"ret\n" :: "i"(this_pc_player));
}

/* in place of mov edx,[0x5ece7c]; cmp esi,[edx+0xac]: flags from cmp esi, this PC's player */
__attribute__((naked)) static void cmp_esi_player_here(void)
{
	__asm__ volatile(
		"pushl %%eax\n\t"
		"pushl %%ecx\n\t"
		"call %c0\n\t"
		"popl %%ecx\n\t"
		"movl 0x5ece7c, %%edx\n\t"
		"cmpl %%eax, %%esi\n\t"
		"popl %%eax\n\t"
		"ret\n" :: "i"(this_pc_player));
}

static u8 *ONI_FC health_sounds_player(u8 *konoko) { return view_character(konoko); }

/* in place of mov edi,[esi+0xac] */
__attribute__((naked)) static void load_edi_player_here(void)
{
	__asm__ volatile(
		"movl 0xac(%%esi), %%edi\n\t"
		"pushl %%eax\n\t"
		"pushl %%ecx\n\t"
		"pushl %%edx\n\t"
		"movl %%edi, %%ecx\n\t"
		"call %c0\n\t"
		"movl %%eax, %%edi\n\t"
		"popl %%edx\n\t"
		"popl %%ecx\n\t"
		"popl %%eax\n\t"
		"ret\n" :: "i"(health_sounds_player));
}

#define CHR_TYPE(c)             (*(u32 *)((c) + 0x1e8))   /* 0 player */

static u32 ONI_FC hypo_sound_wanted(u8 *c)
{
	return !c || CHR_TYPE(c) != 0 || c == this_pc_player();
}

/* in place of call 0x4fdac0 with the character in EBP (ECX sound, EDX 0) */
__attribute__((naked)) static void hypo_sound(void)
{
	__asm__ volatile(
		"pushl %%ecx\n\t"
		"pushl %%edx\n\t"
		"movl %%ebp, %%ecx\n\t"
		"call %c0\n\t"
		"popl %%edx\n\t"
		"popl %%ecx\n\t"
		"testl %%eax, %%eax\n\t"
		"jz 1f\n\t"
		"pushl $0x4fdac0\n\t"
		"ret\n"
		"1:\n\t"
		"ret\n" :: "i"(hypo_sound_wanted));
}

/* The joiner's spots (pass-through while hosting or off) */
static void sounds_join_install(void)
{
	static const u8 cmp_bytes[12] = { 0x8b, 0x15, 0x7c, 0xce, 0x5e, 0x00, 0x3b, 0xb2, 0xac, 0x00, 0x00, 0x00 };
	static const u8 edi_bytes[6] = { 0x8b, 0xbe, 0xac, 0x00, 0x00, 0x00 };
	u8 b[12];
	u32 rel;
	int ok = 1;

	ok &= patch_call(0x51ca39, ONrPlayer_Get, (u32)get_player_here);
	ok &= patch_call(0x4ddea9, ONrPlayer_Get, (u32)get_player_here);
	if (code_matches(0x4f755b, cmp_bytes, sizeof cmp_bytes)) {
		memset(b, 0x90, sizeof b);
		b[0] = 0xe8;
		rel = (u32)cmp_esi_player_here - (0x4f755b + 5);
		memcpy(b + 1, &rel, 4);
		patch_write(0x4f755b, b, sizeof b);
	} else ok = 0;
	if (code_matches(0x4faa09, edi_bytes, sizeof edi_bytes)) {
		b[0] = 0xe8;
		rel = (u32)load_edi_player_here - (0x4faa09 + 5);
		memcpy(b + 1, &rel, 4);
		b[5] = 0x90;
		patch_write(0x4faa09, b, 6);
	} else ok = 0;
	log_msg(ok ? "net: pickup, overheal and error sounds follow player 2"
	           : "net: some player sounds couldn't be moved to player 2 (game code changed)");
}

static void sounds_install(void)
{
	if (patch_call(0x4edb9c, ONrUISound_Play, (u32)hypo_sound)) log_msg("net: hypo sounds only for this PC's player");
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
			if (!patch_call(ONcTick_HUDCall, (u32)ONrHUD_Tick, (u32)hud_tick))
				log_msg("net: HUD tick call 0x%x changed, the health ring follows Konoko", ONcTick_HUDCall);
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
	{   /* room for a map download's bursts (Windows' default is 64 KB) */
		int rcv = 4 << 20, snd = 1 << 20;
		setsockopt(g_sock, 0xffff /* SOL_SOCKET */, 0x1002 /* SO_RCVBUF */, &rcv, 4);
		setsockopt(g_sock, 0xffff, 0x1001 /* SO_SNDBUF */, &snd, 4);
	}
	if (!g_images)
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

/* What the first networked level needs, once: the game files' hashes and the hypo sound */
static void net_prepare(void)
{
	if (g_hashed) return;
	g_hashed = 1;
	hash_files();
	sounds_install();
}

/* The joiner's view, sounds and map download. Every patch only changes anything while this PC
 * is joining (view_character, g_dl_level), so hosting or playing alone later is as before. */
static void join_install(void)
{
	if (g_join_installed) return;
	g_join_installed = 1;
	view_install();
	if (patch_call(ONcLevelLoad_TMCall, (u32)TMrLevel_Load, (u32)level_load))
		log_msg("net: a map that differs from the host's is downloaded while the level loads");
	sounds_join_install();
}

/* Out of the network game this PC was in (the main menu's Custom Game picks the next mode) */
static void net_leave(void)
{
	session_end("left from the menu");
	if (g_sock != INVALID_SOCKET) closesocket(g_sock);
	g_sock = INVALID_SOCKET;
	g_have_peer = g_peer_seen = g_peer_ready = 0;
	g_match_pending = 0;
	g_problem = 0;
	g_mode = NET_OFF;
}

void net_menu_local(void)
{
	if (g_mode == NET_OFF) return;
	if (g_startup_mode) {
		log_msg("net: Play: %s stays on (set by %s)", mode_name(), "ARKLITE_HOST/ARKLITE_JOIN or Arklite.ini [net]");
		return;
	}
	net_leave();
	log_msg("net: off, playing alone");
}

void net_menu_host(u32 port, const char *name, u32 max_players, u32 max_spectators, u32 time_limit, int score_limit)
{
	net_leave();
	g_startup_mode = 0;
	g_mode = NET_HOST;
	g_port = (u16)(port >= 1 && port <= 65535 ? port : DEFAULT_PORT);
	str_cpy(g_server_name, sizeof g_server_name, name && name[0] ? name : "New Oni multiplayer game");
	g_max_players = max_players;
	g_max_spectators = max_spectators;
	g_time_limit = time_limit;
	g_score_limit = score_limit;
	g_server_id = GetTickCount() * 2654435761u ^ (u32)GetCurrentProcessId();
	net_prepare();
	log_msg("net: hosting \"%s\" on UDP port %d, %d players (%d for now), %d spectators, %d min, %d points", g_server_name,
	        g_port, g_max_players, g_max_players < NET_MAX_PLAYERS ? g_max_players : NET_MAX_PLAYERS, g_max_spectators,
	        g_time_limit, g_score_limit);
}

void net_menu_join(const char *address)
{
	net_leave();
	g_startup_mode = 0;
	g_mode = NET_JOIN;
	str_cpy(g_join, sizeof g_join, address);
	net_prepare();
	join_install();
	log_msg("net: joining %s", g_join);
}

/* ---- Server info for the Join list ------------------------------------------------------- */

static u32 g_qsock = INVALID_SOCKET;

static int query_open(void)
{
	u8 wsa[400];
	SOCKADDR_IN local;
	u32 on = 1;

	if (g_qsock != INVALID_SOCKET) return 1;
	if (WSAStartup(0x0202, wsa)) return 0;
	g_qsock = socket(2, 2, 17);
	if (g_qsock == INVALID_SOCKET) return 0;
	memset(&local, 0, sizeof local);
	local.family = 2;
	if (bind(g_qsock, &local, sizeof local)) {
		closesocket(g_qsock);
		g_qsock = INVALID_SOCKET;
		return 0;
	}
	ioctlsocket(g_qsock, FIONBIO, &on);
	setsockopt(g_qsock, 0xffff /* SOL_SOCKET */, 0x20 /* SO_BROADCAST */, &on, 4);
	return 1;
}

/* Name lookups (gethostbyname waits for DNS, seconds for a name that doesn't exist) run on a
 * thread of their own, one at a time; the Join dialog asks and polls every frame */
static volatile int g_rs_state;        /* 0 idle, 1 asked, 2 answered */
static char g_rs_address[300];
static SOCKADDR_IN g_rs_result;
static int g_rs_ok;
static HANDLE g_rs_thread;

static u32 WINAPI resolver_thread(void *unused)
{
	u8 wsa[400];
	WSAStartup(0x0202, wsa);
	for (;;) {
		while (g_rs_state != 1) Sleep(5);
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		g_rs_ok = resolve(g_rs_address, &g_rs_result) && g_rs_result.port;
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		g_rs_state = 2;
	}
	return 0;
}

int net_resolve_start(const char *address)
{
	if (g_rs_state != 0 || !address[0]) return 0;
	if (!g_rs_thread) {
		g_rs_thread = CreateThread(0, 0, resolver_thread, 0, 0, 0);
		if (!g_rs_thread) return 0;
	}
	str_cpy(g_rs_address, sizeof g_rs_address, address);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	g_rs_state = 1;
	return 1;
}

int net_resolve_poll(u32 *addr, u16 *port)
{
	if (g_rs_state != 2) return -1;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	*addr = g_rs_result.addr;
	*port = g_rs_result.port;
	int ok = g_rs_ok;
	g_rs_state = 0;
	return ok;
}

void net_query_send(u32 token, u32 addr, u16 port)
{
	PktInfoReq q;
	SOCKADDR_IN to;

	if (!query_open()) return;
	memset(&q, 0, sizeof q);
	head(&q.h, PKT_INFO_REQ);
	q.token = token;
	q.sent = GetTickCount();
	memset(&to, 0, sizeof to);
	to.family = 2;
	to.addr = addr;
	to.port = port;
	sendto(g_qsock, &q, sizeof q, 0, &to, sizeof to);
}

/* Servers on this PC and the local network: the default port and Arklite.ini's */
void net_query_broadcast(u32 token)
{
	u32 ports[2] = { DEFAULT_PORT, GetPrivateProfileIntA("net", "port", DEFAULT_PORT, ini_path()) };
	for (int i = 0; i < 2; i++) {
		if (i && (ports[i] == ports[0] || ports[i] < 1 || ports[i] > 65535)) continue;
		net_query_send(token, 0xffffffffu, htons((u16)ports[i]));   /* 255.255.255.255 */
		net_query_send(token, 0x0100007fu, htons((u16)ports[i]));   /* 127.0.0.1 */
	}
}

int net_query_poll(NetServerInfo *out)
{
	static u8 buf[2048];
	SOCKADDR_IN from;
	int len, n;

	if (g_qsock == INVALID_SOCKET) return 0;
	for (;;) {
		len = sizeof from;
		n = recvfrom(g_qsock, buf, sizeof buf, 0, &from, &len);
		if (n < 0) {
			if (WSAGetLastError() == 10054) continue;   /* WSAECONNRESET: nobody on that port */
			return 0;
		}
		const PktInfo *p = (const PktInfo *)buf;
		if ((u32)n < sizeof p->h || p->h.magic != MAGIC || p->h.type != PKT_INFO) continue;
		memset(out, 0, sizeof *out);
		/* another protocol's reply may not be laid out like ours: only its token and time count */
		if ((u32)n < 3 * 4 + sizeof p->h) continue;
		out->token = p->token;
		out->ping_ms = GetTickCount() - p->sent;
		out->from_addr = from.addr;
		out->from_port = from.port;
		out->same_version = p->h.proto == PROTO && (u32)n >= sizeof *p;
		if (out->same_version) {
			out->level = p->level;
			out->players = p->players;
			out->max_players = p->max_players;
			out->spectators = p->spectators;
			out->max_spectators = p->max_spectators;
			out->in_match = p->in_match;
			out->server_id = p->server_id;
			memcpy(out->name, p->name, sizeof out->name);
			memcpy(out->folder, p->folder, sizeof out->folder);
			memcpy(out->map, p->map, sizeof out->map);
			out->name[sizeof out->name - 1] = out->folder[sizeof out->folder - 1] = out->map[sizeof out->map - 1] = 0;
		}
		return 1;
	}
}

void net_query_close(void)
{
	if (g_qsock != INVALID_SOCKET) closesocket(g_qsock);
	g_qsock = INVALID_SOCKET;
}

/* ---- Install ------------------------------------------------------------------------------ */

void net_install(void)
{
	char host_env[32];
	u32 port;

	/* The server's description, as the Host dialog saved it */
	GetPrivateProfileStringA("host", "name", g_server_name, g_server_name, sizeof g_server_name, ini_path());
	g_max_players = GetPrivateProfileIntA("host", "max_players", 8, ini_path());
	g_max_spectators = GetPrivateProfileIntA("host", "max_spectators", 0, ini_path());
	g_time_limit = GetPrivateProfileIntA("host", "time_limit", 10, ini_path());
	g_score_limit = (int)GetPrivateProfileIntA("host", "score_limit", 20, ini_path());
	if (g_time_limit > NET_TIME_LIMIT_MAX) g_time_limit = 10;   /* (a negative one reads as huge) */
	port = GetPrivateProfileIntA("net", "port", DEFAULT_PORT, ini_path());
	g_delay = GetPrivateProfileIntA("net", "delay", 0, ini_path());   /* 0: rollback, no input delay */
	g_record = GetPrivateProfileIntA("net", "record", 1, ini_path()) != 0;
	if (g_delay > 30) g_delay = 30;
	if (port < 1 || port > 65535) port = DEFAULT_PORT;
	g_port = (u16)port;

	if (GetEnvironmentVariableA("ARKLITE_HOST", host_env, sizeof host_env)) g_mode = NET_HOST;
	else if (GetEnvironmentVariableA("ARKLITE_JOIN", g_join, sizeof g_join)) g_mode = NET_JOIN;
	else {   /* Arklite.ini [net] host=1 or join=<ip>[:port], for Windows shortcuts */
		host_env[0] = 0;
		if (GetPrivateProfileIntA("net", "host", 0, ini_path())) g_mode = NET_HOST;
		else if (GetPrivateProfileStringA("net", "join", "", g_join, sizeof g_join, ini_path()) && g_join[0])
			g_mode = NET_JOIN;
	}
	if (g_mode == NET_OFF) {
		log_msg("net: off until Custom Game hosts or joins (or ARKLITE_HOST / ARKLITE_JOIN, or host/join in Arklite.ini [net])");
		return;
	}
	g_startup_mode = 1;
	g_server_id = GetTickCount() * 2654435761u ^ (u32)GetCurrentProcessId();

	if (g_mode == NET_HOST) {   /* ARKLITE_HOST=1, or a port number */
		u32 v = 0;
		for (const char *s = host_env; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
		if (v > 1 && v < 65536) g_port = (u16)v;
	}
	log_msg("net: %s, input delay %d ticks; the game waits at the level's start for the %s",
	        g_mode == NET_HOST ? "hosting" : "joining", g_delay, g_mode == NET_HOST ? "joiner" : "host");
	net_prepare();
	if (g_mode == NET_JOIN) join_install();
	/* a second Oni on the same PC, for testing: skip "There is already an instance". Daodan
	 * replaces Oni's platform setup (0x50f670) with DD_ONrPlatform_Initialize, which checks
	 * the same way (FindWindowA for class "ONI ", call eax; sub esp,8; test eax,eax; je at
	 * Daodan +0xc2c3); Oni's own je is patched too, for an Oni without Daodan. */
	{
		static const u8 daodan_check[] = { 0xff, 0xd0, 0x83, 0xec, 0x08, 0x85, 0xc0, 0x74, 0x22 };
		u32 daodan = (u32)GetModuleHandleA("binkw32.dll");
		int ok = patch_byte(ONcSingleInstanceJe, 0x74, 0xeb);
		if (daodan && code_matches(daodan + 0xc2bc, daodan_check, sizeof daodan_check)) {
			u8 jmp = 0xeb;
			patch_write(daodan + 0xc2c3, &jmp, 1);
		} else {
			ok = 0;
			log_msg("net: Daodan's instance check not found: a second Oni on this PC will refuse to start");
		}
		if (ok) log_msg("net: more than one Oni may run on this PC");
	}
}

/* Quitting a match goes to the main menu, where the update (and so session_end, which says
 * goodbye) doesn't run until the next level: the other PC saw silence and "connection lost",
 * and the goodbye at process exit came after Winsock was gone. The mode stays for the next level. */
void net_main_menu(void)
{
	session_end("left to the main menu");
}

void net_exit(void)
{
	session_end("game closed");
	net_query_close();
	if (g_sock != INVALID_SOCKET) closesocket(g_sock);
	g_sock = INVALID_SOCKET;
}
