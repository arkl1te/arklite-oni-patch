/* Multiplayer step 3: two or more PCs over UDP (up to NET_PLAYERS_MAX players), deterministic
 * lockstep with input delay, or rollback (delay 0, the default).
 *
 *   ARKLITE_HOST=[port]       host: Konoko (player 1) is this PC's player (port 27777 by default)
 *   ARKLITE_JOIN=host[:port]  join: this PC plays player 2, 3, ... (the next Player-flagged characters)
 *
 * The host is the hub: each joiner talks only to the host (one address to reach, as for any
 * listen server), and the host passes every player's input on to every other PC. Links: on the
 * host, link s is the joiner playing player s; on a joiner, link 0 is the host.
 *
 * Every PC plays the same map (ARKLITE_AUTOSTART=<folder>, or Custom Game). A level starts in the
 * lobby, where each PC plays alone until someone's there to play with (a joiner plays player 2,
 * the others stand). Each joiner says HELLO (phase 0) every 200 ms; the host answers LOBBY: which
 * player that joiner is (in the order they came; one leaving moves the later ones up), how many
 * players there are, how many are ready, the countdown. The host takes up to Max players (the
 * Host dialog, NET_PLAYERS_MAX at most), and no more than the map has Player-flagged characters;
 * anyone past that hears NETBAD_FULL.
 *
 * The warm-up: WARM_WAIT_MS after a second player came, the host sends each joiner LOBBY with
 * "go" and warm (the player count and the joiner's player) and every PC reloads the level
 * (ONrGame_StartLevel 0x4beb40, what Load Game does from the pause menu) with that many players
 * (players_set_count), so they all start from the same state, as a match does: a warm-up is a
 * synced session like a match (S_WAIT, then S_RUN with g_warm), without limits, whose score the
 * match wipes. There F3 toggles "ready" (ready_frame, shown by mp_hud.c), HELLO has phase 2.
 * When at least two players are there and 51% of them are ready, the host counts down 7 s
 * (stopped if that stops being true); at 0, "go" without warm reloads every PC into the match.
 *
 * Joining a game in progress (rollback only, on a map with a spawn point for the new player):
 * nobody reloads. With rollback the host doesn't wait in its lobby for a second player: it starts
 * a warm-up alone at once (solo_session), so every player after it joins a running session.
 *   1. The host keeps the session's history (every player's final input of every tick, and its
 *      state hash every HASH_EVERY ticks). Someone new gets a place (Link.joining 1) and LOBBY
 *      with catchup: the seeds and player count the session's level loaded with, and the RNGs of
 *      its first tick.
 *   2. The joiner loads the level the same way (S_CATCHUP), fetches the history (PKT_HIST_REQ /
 *      PKT_HIST) and runs its ticks, muted and as fast as it can, checking the hashes.
 *   3. Within LIVE_GAP of the host's history it says so (HELLO phase 3, live_from): the host adds
 *      its player to the session (add_players: absent, no character, inputs made by the host as
 *      nothing pressed; every PC learns the new player count from PKT_INPUT) and sends it
 *      everyone's inputs from live_from as to any joiner (Link.joining 2). Its PC runs on with
 *      rollback, in sync, while its own inputs only tell the host how far it is (spect_gen).
 *   4. Once its inputs arrive in time, ours reach it and every other PC knows its player
 *      (Link.knows), the host takes its inputs from the next tick it hasn't made (joining_frame)
 *      and its own record says from that tick that the player has a character
 *      (PLAYERS_PRESENT_FLAG): players.c spawns it at that tick on every PC.
 * A player who leaves loses its character the same way (mark_gone: g_off_from), so its place can
 * go to someone new. If the replay comes out differently (catchup_check), the joiner says so and
 * waits in its lobby as before, and the old way lets it in (below).
 *
 * The old way, without rollback or after a failed replay, or after a match's end: someone who
 * comes waits in its own lobby (Link.waiting: the host knows it, but it's not in this session and
 * its slot is past g_nplayers). During a warm-up the host reloads everyone WARM_WAIT_MS later to
 * let it in (unless a countdown runs: the match takes it in); a match takes it in at its end. Every
 * "go" moves the players up into slots 1.. (host_go), and has a new session number, so a late
 * repeat is ignored.
 *
 * After the reload each PC stops the clock (server time = game tick, so no tick runs): the
 * joiners send HELLO (phase 1); once every one of them has (or after LOST_MS, without the ones
 * still loading: they're gone, see below), the host sends each START with its RNG seeds (game RNG
 * 0x5326d4 and lockstep.c's tick-local stream), which every PC puts in place right before the
 * first tick. HELLO and START also carry a hash of the level-start state, the start tick, the x87
 * control word and the game files; a difference is logged. The score (players.c) starts at 0
 * with the match.
 *
 * Every PC must run the same game: every HELLO and START carries a Compat record (hashes of
 * Arklite's source (build.sh: a local build and GitHub's of the same source match), Daodan, its
 * gameplay settings, level0, and every file in the map's folder, and whether player 2 is on).
 * The host keeps a joiner whose record differs out of the lobby and tells it why in LOBBY
 * (refused); both PCs say on screen what differs. A packet of another protocol (another
 * Arklite) gets the same. The host's LOBBY also carries its gameplay settings from Arklite.ini
 * (respawn_delay, item_respawn) and the match's limits ([host] time_limit, score_limit, from the
 * Host dialog), which the joiners use. A match ends at a limit (players.c); see "The match's end"
 * for the way back to the lobby (S_OVER, PKT_END).
 *
 * Ticks are numbered from the session's start. Each frame, for every tick the clock wants
 * (server time - game tick, what Daodan's DDrActionBuffer_Get computes before it hands over the
 * frame's record), this PC makes its input for tick (ticks run + delay): the frame's record for
 * the first, the same buttons without aim for the rest (what a tick with no record does). Daodan
 * only returns a record when a tick is due and keeps merging input until then, so a record that
 * can't go out yet is merged into the next one. Every PKT_INPUT carries, per player, the inputs
 * from the receiver's acknowledged tick on (MAX_SEND in all), so a lost packet is resent by the
 * next. A tick runs once every player's input for it is here (with rollback: is here or
 * guessed): the update gets Konoko's records as its action records (tick i, record i) and the
 * others' through players_net_frame.
 *
 * Records are cleaned the same way on every PC: action bits 0-2 (escape, console, pausescreen;
 * 0x4faec0 opens the pause menu from bit 0 inside the tick, which would open it on every PC)
 * are cleared. Word 11 carries this PC's F2 character choice (char_menu.c, read by players.c).
 * Esc still opens the menu through utility input, which reads this PC's keys. The
 * invert-look setting (0x5ec70f) is per PC, so it's applied to this PC's aim before sending and
 * is off while the ticks run.
 *
 * Desync check: every 60 ticks each PC hashes the game state (both RNG streams and, for every
 * character in use, its flags, health, position, facing and velocity: no pointers, which differ
 * between PCs) and sends it with its inputs; the host compares with every joiner's, each joiner
 * with the host's. Flag 0x4000 isn't hashed: the draw pass (0x4e1540) sets it on the characters
 * this PC drew. A different hash logs DESYNC with this PC's character summary for that tick and
 * writes every character's full image for that tick (Arklite-desync-host.bin / -join.bin, -join3
 * for player 3 and so on; tools/desync_diff.py compares two), without the fields each PC's
 * drawing writes (flag 0x4000, +0x163c frames off screen, +0x1660 distance to the camera, active
 * +0x221c..+0x2225 detail level).
 *
 * Each PC sees its own player: on a joiner the camera (gs+0x80, mode +0 == 1 follows the
 * character at +0x54; updated by 0x4d5ca0, called at 0x4fb141 per tick and 0x4fb170 per frame)
 * and the sound listener (0x4e33e0/0x4e3460 position, 0x4e3640 facing, called with gs+0xac at
 * 0x4fb1f4, 0x4fb1fb, 0x4fb20f) follow the joiner's player, and the whole game display (0x4fb480:
 * HUD, laser sight, crosshair) draws with that player in the player slot.
 *
 * Testing on one PC: Oni refuses a second instance (FindWindowA at 0x50f687, je at 0x50f68f),
 * so with networking on that check is skipped. The first joiner's log is Arklite-join.log; give
 * any more joiners on the same PC their own with ARKLITE_LOG=Arklite-join3.log and so on.
 *
 * Players who leave: a joiner that says goodbye, or sends nothing for 10 s during the match, is
 * gone (mark_gone). From the first tick the host has none of its inputs for, the host makes them
 * up as "nothing pressed" and sends them on like anyone's (fill_gone), so every PC still runs
 * the same ticks, and the host's record takes that player's character out of the game from its
 * next input (players.c); the rest play on. If the host goes, a joiner plays on
 * alone with everyone else standing. Arklite.ini [net]: port (27777), delay (0: rollback; the
 * host's is used on every PC), host=1 or join=<ip>[:port] when the environment doesn't say, and
 * record (1: with input delay and two players, each PC records the game, every tick, to
 * Arklite-net-host.akr / -join.akr, lockstep.c's format; tools/akr_diff.py compares the two and
 * names the first tick and field that differ). */
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
#define ONrCamera_EnterFollow   ((void (ONI_FC *)(void))0x4d73d0)   /* what cm_reset does */
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
#define PROTO           22
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
#define WARM_WAIT_MS    1500         /* a new player: this long for anyone coming along, then the reload */
#define RELOAD_EVERY_MS 60000        /* ...but one PC gets the warm-up reloaded for it once in this long (a PC that keeps
                                       failing to catch up, or a hostile one, would otherwise restart everyone's game) */
#define HIST_BYTES      (64u << 20)  /* the session's inputs kept for players joining it: 64 MB, about 97 minutes */
#define HIST_TICK       (NPL * 12)   /* u32 per tick: every player's input */
#define HIST_REQ_TICKS  600          /* a joiner asks for this many ticks at once... */
#define HIST_REQ_PKTS   64           /* ...which the host sends in at most this many packets */
#define HIST_PKT_BYTES  1300         /* inputs in one PKT_HIST */
#define HIST_PKT_TICKS  300          /* ticks in one PKT_HIST (so at most 6 state hashes) */
#define HIST_RETRY_MS   200          /* asked again from the first tick not here */
#define CATCHUP_US      25000        /* a catching-up frame runs ticks this long, then draws */
#define LIVE_GAP        60           /* caught up: within this many ticks of the host's inputs */
#define JOIN_GAP        20           /* comes in: its inputs and its acknowledgements this near the host's ticks */
#define JOIN_STEADY     30           /* ...for this many of the host's ticks in a row, after this many live ticks of its own */
#define JOIN_QUIET_MS   150          /* ...and nothing from it for longer than this (a PC that hitched isn't near) */
#define PROBLEM_MS      2000         /* a refusal stays on screen this long after the last one */
#define FIXING_MS       30000        /* host: a joiner refused for level0 or the map alone fixes it itself (cache swap,
                                       download, a reload each); the refusal shows only when it lasts longer */
#define VK_F3           0x72
#define VK_LBUTTON      0x01
#define VK_RBUTTON      0x02
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
__declspec(dllimport) int  WINAPI CreateHardLinkA(const char *, const char *, void *);
__declspec(dllimport) int  WINAPI CopyFileA(const char *, const char *, int);
__declspec(dllimport) u32  WINAPI GetLastError(void);
__declspec(dllimport) u32  WINAPI GetFileSize(HANDLE, u32 *);
__declspec(dllimport) int  WINAPI SetEndOfFile(HANDLE);
__declspec(dllimport) int  WINAPI RemoveDirectoryA(const char *);
__declspec(dllimport) int  WINAPI DeleteFileA(const char *);
__declspec(dllimport) u32  WINAPI SetFilePointer(HANDLE, int, int *, u32);
__declspec(dllimport) void WINAPI Sleep(u32);
__declspec(dllimport) int  WINAPI PeekMessageW(void *, void *, u32, u32, u32);
__declspec(dllimport) int  WINAPI TranslateMessage(const void *);
__declspec(dllimport) int  WINAPI DispatchMessageW(const void *);
__declspec(dllimport) void WINAPI PostQuitMessage(int);
__declspec(dllimport) int  WINAPI setsockopt(u32, int, int, const void *, int);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);
__declspec(dllimport) u32  WINAPI GetCurrentThreadId(void);
#ifndef ARKLITE_SRC_HASH
#define ARKLITE_SRC_HASH 0   /* build.sh: a hash of the source, the same for every build of it */
#endif

#define INVALID_SOCKET  0xffffffffu
#define FIONBIO         0x8004667e

/* ---- Packets ------------------------------------------------------------------------------ */

enum { PKT_HELLO = 1, PKT_START, PKT_INPUT, PKT_BYE, PKT_LOBBY, PKT_NAME,
       PKT_MAP_LIST_REQ, PKT_MAP_LIST, PKT_MAP_REQ, PKT_MAP_DATA, PKT_INFO_REQ, PKT_INFO,
       PKT_CHAT, PKT_CHAT_ACK, PKT_END, PKT_NAMES, PKT_HIST_REQ, PKT_HIST, PKT_COOKIE,
       PKT_L0_REQ, PKT_L0_DATA };

#define NPL             NET_SLOTS

/* key (protocol 19): the receiver's address proves the packet is from the PC playing there. The
 * host gives each address its cookie (cookie_of: a keyed hash of IP and port, with a secret made
 * at random when it opens its port); a PC can only learn the cookie of an address where it reads
 * packets. Every packet from a joiner or a map download carries it, and every packet the host sends
 * one carries it back, so a stranger who writes someone else's address on its packets (UDP lets
 * anyone do that) can't take a place, speak for a player or answer for the host. See "Strangers". */
typedef struct { u32 magic; u16 proto; u8 type, pad; u32 key; } PktHead;
#define NFILES 4
/* What every PC must have the same of (see g_file_names) */
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
	u32 phase;             /* 0: in the lobby, 1: waiting for the match to start, 2: in the warm-up, 3: joining it */
	u32 ready;             /* the joiner pressed F3 */
	u32 live_from;         /* phase 3: caught up, everyone's inputs from this tick on, please (0: still catching up) */
	u32 catchup_failed;    /* phase 0: the replay came out differently: let this PC in the old way */
} PktHello;
typedef struct {
	PktHead h;
	u32 level, players, ready, countdown_ms;   /* countdown_ms 0xffffffff: none */
	u32 go;                /* the match starts: reload the level */
	u32 joiner_ready;      /* what the host knows of this joiner's ready flag */
	u32 seed, local_seed;  /* with go: both RNG streams, set on every PC before the reload */
	u32 refused;           /* NETBAD_*: the joiner can't play here, and why (then nothing else counts) */
	u32 respawn_ticks, item_ticks;   /* the host's Arklite.ini, used on every PC */
	u32 map_offer;         /* with refused NETBAD_MAP: the host sends its map's files on request */
	u32 time_limit;        /* the match's limits, from the host's Host dialog: minutes (0: none) */
	int score_limit;       /* 0: none; below 0: reached going down (suicides) */
	u32 slot;              /* the player this joiner is: 1..players-1 */
	u32 capacity;          /* the most players this server takes on this map */
	u32 level0[2];         /* with refused NETBAD_LEVEL0: the host's hashes (level0_swap.c) */
	u32 session;           /* with go: this reload's number; a repeat of one already taken is ignored */
	u32 warm;              /* with go: the session is a warm-up */
	u32 in_match;          /* a match is on: this joiner plays when it ends */
	/* catchup: this joiner joins the session in progress (see "Joining a game in progress"): it
	 * loads the level as every PC did (load_players, seed/local_seed), runs from run_seed with the
	 * session's inputs (PKT_HIST) and comes in once it caught up. session, warm and slot as for go. */
	u32 catchup, load_players, run_seed, run_local_seed, start_tick, start_hash;
	u32 dedicated;         /* the host is a dedicated server (protocol 20): its player never plays (players.c) */
	u32 char_rules;        /* the host's mutators, NET_CHARS_* and NET_MUT_* (protocol 21; char_menu.c, mp_rules.c) */
	char note[64];         /* with refused NETBAD_KICKED: why, and for how long */
} PktLobby;
typedef struct {
	PktHead h;
	u32 level, start_tick, state_hash, fpu_cw, seed, local_seed, delay;
	Compat c;
	u32 players;           /* in the match: players 0..players-1 */
	u32 gone;              /* of them, those who never came (they stand) */
} PktStart;
/* Inputs. A joiner sends its own player's to the host; the host sends each joiner everyone
 * else's (its own and the other joiners' as they came in), so every PC gets every player's input
 * for every tick. Each stream is one player's inputs for ticks first..first+count-1 (0x30 bytes
 * each, after the Stream). */
typedef struct { u32 first; u16 slot, count; } Stream;
typedef struct {
	PktHead h;
	u32 ack[NPL];          /* the sender has player p's inputs for ticks < ack[p] */
	u32 hash_tick, hash;   /* our latest state hash (hash_tick 0: none yet) */
	int adv;               /* our ticks run minus the inputs we have of the receiver's player (time sync) */
	u32 stamp;             /* GetTickCount when sent */
	u32 echo, held;        /* the receiver's latest stamp, and how many ms ago it arrived (ping) */
	u32 gone, left;        /* host: the players who stopped answering / left; their inputs come from the host */
	u16 ping[NPL];         /* host: each joiner's last round trip in ms (0xffff: not known) */
	u32 nplayers;          /* host: the session's players (more come in during it) */
	u32 base[NPL];         /* host: a player added during the session has inputs from this tick (before: none pressed) */
	u32 absent;            /* host: players nobody plays (yet): no character, the host makes their inputs */
	u32 nstreams;
} PktInput;
typedef struct {
	PktHead h;
	char name[MP_NAME_MAX];   /* a joiner's player's (mp_names.c); only shown, not game state */
} PktName;
typedef struct {
	PktHead h;
	char name[NPL][MP_NAME_MAX];   /* host: every player's ("" none) */
} PktNames;
/* Chat (chat.c): only shown, never game state. Each link (host and one joiner) numbers its
 * messages in a stream (random per link); the receiver shows them in order and acknowledges the
 * last one it showed, and the sender sends every unacknowledged one again every CHAT_RESEND_MS. A
 * stream the receiver hasn't heard before starts at whatever number comes first (a new joiner, or
 * the other PC restarted). The host shows a joiner's message and passes it on to the other
 * joiners, saying whose it is (from). A whisper (chat.c's /w) names its one reader (to): the host
 * passes it on only to that joiner (protocol 17). */
typedef struct {
	PktHead h;
	u32 stream, seq;
	u32 from;              /* the player who wrote it, CHAT_SERVER: the host's line about who entered or left */
	u32 to;                /* the one player who reads it (a whisper), CHAT_ALL: everyone */
	char text[CHAT_MAX];
} PktChat;
#define CHAT_SERVER     0x100
#define CHAT_ALL        0xffffffffu
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
	u32 level, players, max_players, spectators, max_spectators;   /* max_spectators: always 0, no limit (kept: the helper's layout) */
	u32 in_match;          /* the match is on: a joiner waits in its lobby until the next one */
	u32 server_id;         /* random per hosting: the same server reached at two addresses */
	char name[NET_SERVER_NAME_MAX];   /* the server's, Host dialog */
	char folder[64];       /* the map's folder in CustomLevels, "" for a numbered level */
	char map[48];          /* the map's name */
	/* what a player downloads to join (protocol 21, the Join page's Current mods and Total download) */
	u32 l0[2];             /* the host's level0 pair (hash_file) */
	u32 l0_bytes;          /* a new player's level0 download (net_join_level0_bytes), or NET_INFO_MEASURING / _UNKNOWN */
	u32 map_hash, map_bytes, map_ok;   /* a custom map's folder (hash_map), its bytes; map_ok 0: it can't be sent */
	u32 nmods;             /* the host's installed AE mods (all of them; the first NET_INFO_MODS below) */
	u32 mods[NET_INFO_MODS][2];   /* package number, its package's bytes on disk */
} PktInfo;

/* Joining a session in progress: the session's inputs from its first tick. The joiner asks for
 * ticks from..from+count-1; the host answers with as many PKT_HIST as they take (up to what it has).
 * A PKT_HIST holds nhash {tick, state hash} pairs (the host's, for checking the replay), then
 * ticks first..first+count-1: per tick a byte with a bit per player whose input differs from the
 * tick before (the first tick: from nothing pressed), and for each such player a u16 with a bit per
 * changed word and those words. */
typedef struct {
	PktHead h;
	u32 session, from, count;
} PktHistReq;
typedef struct {
	PktHead h;
	u32 session, first, count;
	u32 avail;             /* the host has the inputs of ticks < avail */
	u32 nplayers;          /* its players now */
	u32 nhash, bytes;      /* then nhash x {tick, hash}, then bytes of inputs */
} PktHist;

/* After a match: each joiner says whether its player pressed fire; the host says how many did,
 * how long is left, and finally "go": every PC reloads the level into the lobby */
typedef struct {
	PktHead h;
	u32 level;
	u32 ready;             /* the sender's player pressed fire */
	u32 players, ready_count, left_ms;   /* host: what it knows */
} PktEnd;

/* ---- State -------------------------------------------------------------------------------- */

enum { NET_OFF, NET_HOST, NET_JOIN };
enum { S_IDLE, S_LOBBY, S_WAIT, S_RUN, S_LOST, S_OVER, S_CATCHUP };

/* The other PCs this one talks to. On the host, link s is the joiner playing player s (1..); on
 * a joiner, link 0 is the host. */
typedef struct {
	SOCKADDR_IN addr;
	int used;
	u32 last_recv, last_send;
	u32 sent_sig;          /* what the last PKT_INPUT carried (sig_for): a new input sends again */
	int ready;             /* host, lobby: the joiner pressed F3 */
	int end_ready;         /* host, after the match: the joiner's player pressed fire */
	int arrived;           /* host, S_WAIT: the joiner reloaded for the match */
	int waiting;           /* host: came during a warm-up or match, plays from the next reload */
	int rejoin;            /* host: a gone player, back in its lobby: plays from the next reload */
	int joining;           /* host: joins the session in progress: 1 catching up, 2 in sync (gets inputs), not in yet */
	int no_catchup;        /* host: its replay came out differently: it waits for the next reload */
	int reload_held;       /* host: it wants in again within RELOAD_EVERY_MS: said once that it waits */
	u32 spect_gen;         /* host, joining 2: it sent its own inputs up to here (none count before it's in) */
	u32 live_from;         /* host, joining 2: its first live tick */
	u32 near_since;        /* host, joining 2: our tick from which it has kept near (joining_frame; 0: it isn't) */
	int held_logged;       /* host, joining 2: "hitched, comes in once it keeps up" said */
	u32 knows;             /* host: the players whose inputs it acknowledged from their first tick (g_base) */
	char name[MP_NAME_MAX];   /* host: the joiner's player's */
	int entered;           /* host: "Name entered the game" was said (its first PKT_NAME) */
	u32 ack[NPL];          /* it has player p's inputs for ticks < ack[p] */
	int adv, adv_known;    /* time sync: its ticks past its inputs of our player, as it sent */
	u32 stamp, stamp_at;   /* its newest PktInput stamp, and when it came */
	int ping;              /* ms, the last round trip (-1: not known) */
	u32 ping_sum, ping_n, ping_max;   /* round trips since the last 30 s log line */
	u32 last_lobby, last_end;
	u32 last_names, names_at;   /* host: g_names_serial last sent, and when */
	struct { u32 tick, hash; } theirs[HASH_SLOTS];
	u32 checked[HASH_SLOTS];          /* the tick each slot was last compared at */
	struct { u32 seq, at, from, to; char text[CHAT_MAX]; } chat_out[CHAT_QUEUE];
	u32 chat_out_n, chat_seq, chat_stream, chat_sent_at, chat_in_stream, chat_in_seq;
} Link;

static int g_mode, g_state;
static u32 g_sock = INVALID_SOCKET;
static Link g_link[NPL];
static u16 g_port = DEFAULT_PORT;
static u32 g_delay = 4;
static int g_record = 1;               /* [net] record: Arklite-net-host.akr / -join.akr */

static u32 g_level, g_last_tick;
static char g_session[40];             /* the session's ID on every PC (level, starting seeds): problem reports */
static char g_dump_img[MAX_PATH], g_dump_ticks[MAX_PATH];   /* the desync dumps written, for report.c */
static u32 g_ticks;                    /* ticks run this session */
static u32 g_gen;                      /* our inputs are made for ticks < g_gen */
static u32 g_stall_frames, g_stall_ticks, g_last_stall_log;   /* frames held back by others' inputs */
/* Time sync (rollback): each PC sends how far its ticks are past the inputs it has of the
 * receiver's player (adv). With the same latency both ways, (ours - theirs) / 2 is how many
 * ticks this PC runs ahead of the other in real time; the PC ahead leaves out one tick in every
 * SYNC_EVERY until it isn't. A joiner keeps in time with the host; the host leaves out ticks only
 * while it's ahead of every joiner. Never faster: the old catch-up ran one extra tick every
 * frame, frames with no tick due included, which at high frame rates ran the game about 3x as
 * fast. */
#define SYNC_EVERY      8
static u32 g_sync_skip_at, g_sync_skipped;
/* Rollback (delay 0, the default): this PC's input runs at once, the others' are predicted, and
 * a wrong guess puts the game back (rollback.c) and runs the ticks again */
#define ROLLING         (g_delay == 0)
#define RB_AHEAD        (ROLLBACK_KEPT_TICKS - 2)   /* ticks run past the last real input of any player, at most */
#define ONgSoundOn      (*(u8 *)0x5eb757)           /* SS2 "a sound driver is there": every sound call checks it */
static u32 g_verified;                 /* ticks < this ran with every player's real inputs */
static int g_rb_started;               /* the engine is on for this match */
static u8  g_sound_on;                 /* ONgSoundOn as the game had it */
static u32 g_rollbacks, g_resim_ticks, g_rb_failed, g_rb_deepest, g_rb_frames;
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

/* Every player's inputs, by tick: this PC's own made here (g_gen), the others' as they came */
static u32 g_in[NPL][RING][12];
static u32 g_in_at[NPL][RING];         /* tick + 1 of the input in the slot, 0: none */
static u32 g_have[NPL];                /* others' inputs are here for ticks < g_have[p] */
static u32 g_pred[NPL][RING][12];      /* rollback: the input each other player's tick last ran with */
static u32 g_pending[12], g_last_local[12];
static int g_has_pending;

/* The match: players 0..g_nplayers-1, this PC's is g_slot (the host's 0) */
static u32 g_nplayers = 2;
static int g_slot;
static u32 g_gone, g_left;             /* players who stopped answering / said goodbye: they stand */
static u32 g_wait_since;               /* host, S_WAIT: when the reload for the match came */

/* Joining a session in progress (see "Joining a game in progress") */
static u32 g_absent;                   /* players nobody plays (yet): no character, inputs made by the host */
static u32 g_base[NPL];                /* a player added during the session: its inputs start at this tick */
static u32 g_on_from[NPL], g_off_from[NPL];   /* host: player p has a character in ticks on_from..off_from-1 */
static u32 g_load_players;             /* the players the session's level loaded with */
static int g_catchup;                  /* joiner: the session being loaded is joined in progress */
static int g_joining;                  /* joiner: 1 catching up (S_CATCHUP), 2 in sync, not in the game yet */
static u32 g_live_from;                /* joiner, joining 2: the tick it went over to the host's live inputs */
static u32 g_cu_start_tick, g_cu_start_hash;   /* joiner: the host's level start */
static u32 g_cu_failed;                /* joiner: the session whose replay came out differently */
static u8  g_cu_sound;                 /* joiner: ONgSoundOn before catching up (muted meanwhile) */
static u32 g_cu_began;                 /* joiner: when catching up began */
static u32 *g_hist;                    /* every player's input of every tick of the session, [tick][NPL][12] */
static u32 *g_hist_hash;               /* the host's state hash of every HASH_EVERY-th tick, 0: not known */
static u8  *g_hist_got;                /* joiner: the ticks that came */
static u32 g_hist_n;                   /* the ticks kept (joiner: here without a gap) */
static u32 g_hist_cap, g_hist_committed;
static int g_hist_ok;                  /* host: the history covers the whole session */
static u32 g_hist_avail, g_hist_nplayers;   /* joiner: the host's, as its last PKT_HIST said */
static u32 g_hist_req_at, g_hist_req_to;    /* joiner: the last request, and the tick it asked up to */

static u32 g_seed, g_local_seed;       /* the host's, put in place before the first tick */
static u32 g_start_hash, g_start_tick, g_start_fpu;
static u32 g_last_hello;
static int g_lost_left;                /* joiner S_LOST: the host said goodbye, not silence */
static u32 g_frames[NPL][6][12];       /* each player's records for the frame's ticks; [0] the update's own */

typedef struct { u32 index, flags; int health; float pos[3], facing, vel[3]; } CharSum;
static struct { u32 tick, hash, n, seeds[2]; CharSum chars[MAX_CHARS]; } g_mine[HASH_SLOTS];
static u32 g_published[HASH_SLOTS];      /* rollback: the tick each slot's hash went out for (verified) */
static u32 g_last_hash_tick, g_last_hash;
static u32 g_checks, g_bad_checks, g_first_bad;
/* Per hash slot, the full image of each character in use (character, then active data), for
 * the dump written at the first desync: Arklite-desync-host.bin / -join.bin */
typedef struct { u32 index, pad; u8 img[IMG_SIZE]; } CharImage;
static CharImage (*g_images)[IMG_CHARS];
static u32 g_nimages[HASH_SLOTS];
static u8 *g_cam_set;                  /* the character the joiner's camera was pointed at */
static u8 g_cam_held;                  /* this PC's camera is standing for players_killcam */
static u32 g_npackets_sent, g_npackets_recv, g_ninputs_recv, g_last_status, g_send_errors;

/* The lobby */
static int g_match_pending;            /* the level was reloaded for the match: start synced */
static int g_warm;                     /* this synced session (or the one being reloaded) is a warm-up */
static u32 g_fight_at;                 /* GetTickCount when this PC's match began (begin_run), 0: none */
#define FIGHT_MS 2000                  /* how long "FIGHT!" is on screen (mp_hud.c) */
static int g_lobby_match;              /* joiner: a match is on at the host, without this PC */
static int g_quiet_end;                /* every PC is reloading: no goodbye */
static u32 g_go_session, g_go_taken;   /* host: the last reload's number; joiner: the last one it followed */
static u32 g_last_join;                /* host: when a player last came */
static int g_ready, g_f3_down;         /* this PC's player pressed F3 */
static u32 g_countdown_end;            /* host: GetTickCount at 0, 0: no countdown */
static u32 g_lobby_players = 1, g_lobby_ready, g_lobby_countdown = 0xffffffffu, g_lobby_rx;   /* joiner: the host's view */
static int g_host_heard;               /* joiner: a LOBBY arrived in this lobby */
static u32 g_last_name, g_name_sent;   /* joiner: when our name last went out, and mp_name_serial() then */
static u32 g_names_serial = 1, g_names_local;   /* host: changes with any player's name */
static u32 g_match_seed, g_match_local_seed;   /* the RNGs the match's level loads with */
static u32 g_problem, g_problem_at;    /* NETBAD_* last seen (refused / refusing), and when */
static int g_problem_hidden;           /* g_problem is being fixed by itself: logged, not on screen */
static u32 g_fixing_since, g_fixing_last;   /* host: refusals the joiner may fix itself, first and last */
/* The match's limits: the host's from Arklite.ini [host] (the Host dialog), the joiner's from LOBBY */
static u32 g_time_limit = 10;          /* minutes, 0: none */
static int g_score_limit = 20;         /* 0: none */
/* The Host dialog's character checkboxes (NET_CHARS_*): every PC applies the host's, from LOBBY,
 * before any level of a session loads, so a choice of a character left out is the map's class on
 * every PC alike (players.c, char_menu_class_allowed) */
static u32 g_char_rules = NET_CHARS_ALL;
/* host: the message of the day (Arklite.ini [host] motd, the Host page), a server line to each
 * player who enters, after "Name entered the game"; "": none */
static char g_motd[NET_MOTD_MAX];
static void clean_text(char *s, u32 size);

static void motd_load(void)
{
	ini_text("host", "motd", "", g_motd, sizeof g_motd);
	clean_text(g_motd, sizeof g_motd);
}
static int g_peer_ping[NPL];           /* joiner: each player's round trip to the host, as the host said */
/* After the match (S_OVER) */
static u32 g_over_at;
static u8 *g_look_who;                 /* whose camera turns after the match; 0: start again from the character */
static int g_end_ready, g_fire_down, g_over_lost;
static u32 g_end_players, g_end_ready_count, g_end_left_ms, g_end_rx;   /* joiner: the host's view */

/* A level reload that waits for the live dialogs to close (reload_level) */
static u32 g_reload;                   /* level + 1, 0: none */
static const char *g_load_step;        /* why net.c loads the level next: the loading popup's first line (level_load) */
static int g_reload_seeds;             /* load it with g_match_seed / g_match_local_seed */

/* The server, as the Host dialog (or Arklite.ini [host]) has it */
static char g_server_name[NET_SERVER_NAME_MAX] = "New Oni multiplayer game";
static u32 g_max_players = NET_PLAYERS_MAX;
static u32 g_server_id;
static int g_startup_mode;             /* set by the environment or Arklite.ini [net], not the menu */
/* Dedicated server (server.c; "Dedicated server" below): the host's player always watches and
 * isn't one of the players; joiners learn it from LOBBY */
static int g_dedicated;                /* host: this is a dedicated server */
static int g_ded_host;                 /* joiner: the host said it's one */
static const char *g_lobby_note;       /* the next send_lobby_addr's note (a kick's) */
static char g_kick_note[64];           /* joiner: the server removed this PC: why ("" no) */
/* host: addresses its admin removed (kick: for some minutes, ban: until the server closes) */
#define BANS 16
static struct { u32 addr, until; int on; char note[64], name[MP_NAME_MAX]; } g_ban[BANS];   /* until: GetTickCount, 0: no end */

static const char *ban_note(u32 addr)
{
	u32 now = GetTickCount();
	for (int i = 0; i < BANS; i++) {
		if (!g_ban[i].on || g_ban[i].addr != addr) continue;
		if (g_ban[i].until && (int)(now - g_ban[i].until) >= 0) {
			g_ban[i].on = 0;
			continue;
		}
		return g_ban[i].note;
	}
	return 0;
}

const char *net_kick_note(void) { return g_kick_note; }
u32 net_server_port(void) { return g_port; }   /* server.c's Linux console listens on it (TCP) */
static int g_hashed, g_join_installed;

/* ---- Players and links ---------------------------------------------------------------------- */

static int gone(int p) { return (g_gone >> p & 1) != 0; }
static int absent(int p) { return (g_absent >> p & 1) != 0; }

/* Player p's inputs come over the network (or, on the host, are made up for one who's gone) */
static int remote(int p) { return p >= 0 && p < (int)g_nplayers && p != g_slot; }

/* What this PC has of player p's inputs: ticks < have_of(p) */
static u32 have_of(int p) { return p == g_slot ? g_gen : g_have[p]; }

/* The other players' inputs are all here for ticks < remote_min() */
static u32 remote_min(void)
{
	u32 m = 0xffffffffu;
	for (int p = 0; p < (int)g_nplayers; p++)
		if (remote(p) && g_have[p] < m) m = g_have[p];
	return m == 0xffffffffu ? g_gen : m;
}

/* A link to a PC still in this lobby or match (one catching up gets no inputs yet) */
static int live(int l)
{
	return l >= 0 && l < NPL && g_link[l].used &&
	       !(g_state != S_LOBBY && g_mode == NET_HOST && (gone(l) || g_link[l].waiting || g_link[l].joining == 1));
}

static int link_of(const SOCKADDR_IN *a)
{
	for (int l = 0; l < NPL; l++)
		if (g_link[l].used && g_link[l].addr.addr == a->addr && g_link[l].addr.port == a->port) return l;
	return -1;
}

/* host: joiners in the lobby (links 1.. are kept together) */
static u32 joiners(void)
{
	u32 n = 0;
	for (int l = 1; l < NPL; l++) n += g_link[l].used != 0;
	return n;
}

static u32 bits(u32 v)
{
	u32 n = 0;
	for (; v; v &= v - 1) n++;
	return n;
}

/* The players this PC's game has: the lobby's or the match's, without the gone ones */
static u32 present_mask(void)
{
	u32 n;
	if (g_state == S_LOBBY) n = g_mode == NET_HOST ? 1 + joiners() : g_host_heard ? g_lobby_players : 0;
	else n = g_nplayers;
	if (n > NPL) n = NPL;
	return ((1u << n) - 1) & ~(g_state == S_LOBBY ? 0 : g_gone | g_absent);
}

/* The most players this server takes on this level: Max players, and the map's spawn points */
static u32 capacity(void)
{
	int spawns = players_spawn_points();
	u32 n = g_max_players, ded = g_dedicated && g_mode == NET_HOST;
	if (spawns >= 2 && (u32)spawns < n) n = (u32)spawns;
	n += ded;   /* a dedicated server's own place (its player watches); the last player uses Konoko's placement */
	return n < 2 + ded ? 2 + ded : n > NPL ? NPL : n;
}

/* The character this PC's player drives: a joiner's lobby has only player 2 (each PC plays
 * alone there); the match's is the joiner's own */
static int local_char(void)
{
	if (g_mode != NET_JOIN) return 0;
	return g_state == S_LOBBY || g_state == S_IDLE ? 1 : g_slot;
}

static void link_reset(Link *k)
{
	SOCKADDR_IN a = k->addr;
	int used = k->used;
	memset(k, 0, sizeof *k);
	k->addr = a;
	k->used = used;
	k->ping = -1;
	k->chat_stream = (GetTickCount() * 0x9e3779b1u ^ (u32)(unsigned long)k ^ counter()) | 1;
}

/* host: a joiner comes into the lobby in the first free place */
static int add_link(const SOCKADDR_IN *a)
{
	for (int l = 1; l < NPL; l++) {
		if (g_link[l].used) continue;
		memset(&g_link[l], 0, sizeof g_link[l]);
		g_link[l].addr = *a;
		g_link[l].used = 1;
		link_reset(&g_link[l]);
		g_link[l].last_recv = g_last_join = GetTickCount();
		g_names_serial++;
		return l;
	}
	return -1;
}

static void remove_link(int l);
static void server_line(const char *name, const char *what);

/* host, lobby: joiner l said goodbye or stopped answering */
static void lobby_leave(int l)
{
	char name[MP_NAME_MAX];
	int said = g_link[l].entered;
	str_cpy(name, sizeof name, mp_name(l));   /* remove_link moves the names up */
	log_msg("net: %s (player %d) left the lobby", name, l + 1);
	remove_link(l);
	if (said) server_line(name, "left the game");
}

static void names_push(void)
{
	for (int l = 1; l < NPL; l++) mp_name_set(l, g_link[l].used ? g_link[l].name : "");
}

/* host: a joiner went. In the lobby the ones after it move up, so the lobby's players stay
 * 0..n-1; during a session a link is its player, so the place stays empty (host_go closes the gaps
 * at the next reload) */
static void remove_link(int l)
{
	if (g_state == S_LOBBY || g_state == S_IDLE) {
		for (; l + 1 < NPL; l++) g_link[l] = g_link[l + 1];
		memset(&g_link[NPL - 1], 0, sizeof g_link[0]);
	} else {
		memset(&g_link[l], 0, sizeof g_link[l]);
	}
	g_names_serial++;
	names_push();
}

/* host: the links moved up into 1.., in order (the next session numbers its players so) */
static void compact_links(void)
{
	int to = 1;
	for (int l = 1; l < NPL; l++) {
		if (!g_link[l].used) continue;
		if (l != to) {
			g_link[to] = g_link[l];
			memset(&g_link[l], 0, sizeof g_link[l]);
		}
		to++;
	}
	g_names_serial++;
	names_push();
}

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
	case NETBAD_FULL:       return "the server is full";
	case NETBAD_PROTO:      return "Arklite's version";
	case NETBAD_DLL:        return "Arklite (not the same release or commit)";
	case NETBAD_DAODAN:     return "Daodan (binkw32.dll)";
	case NETBAD_DAODAN_INI: return "daodan.ini [gameplay] or [devmode]";
	case NETBAD_LEVEL0:     return "GameDataFolder\\level0_Final.dat or .raw";
	case NETBAD_MAP:        return "the map's files (its CustomLevels folder)";
	case NETBAD_PLAYERS:    return "Arklite.ini [players] p2 (0 turns multiplayer off)";
	case NETBAD_KICKED:     return "the server removed this player";
	}
	return "?";
}

static const char *mode_name(void) { return g_mode == NET_HOST ? "host" : "join"; }

int net_active(void) { return g_mode != NET_OFF; }

/* The characters players may pick changed (whose: for the log): the F2 menu shows them again */
static void set_char_rules(u32 rules, const char *whose)
{
	if (rules == g_char_rules) return;
	g_char_rules = rules;
	log_msg("net: %s mutators %03x: Mutant Muro %s, Shinatama and Hasegawa %s, Mini Striker %s, Daodan %s, No Weapons %s, "
	        "Fast Screamer %s, Fast Plasma %s, Fast Ball %s, Bombs For Days %s", whose, rules,
	        rules & NET_CHARS_MUTANT_MURO ? "on" : "off", rules & NET_CHARS_SHINATAMA ? "on" : "off",
	        rules & NET_CHARS_MINI_STRIKER ? "on" : "off", rules & NET_MUT_DAODAN ? "on" : "off",
	        rules & NET_MUT_NO_WEAPONS ? "on" : "off", rules & NET_MUT_FAST_SCREAMER ? "on" : "off",
	        rules & NET_MUT_FAST_PLASMA ? "on" : "off", rules & NET_MUT_FAST_BALL ? "on" : "off",
	        rules & NET_MUT_BOMBS ? "on" : "off");
	char_menu_rules_changed();
}

/* report.c: the session's ID ("none" outside one) and this PC's player slot (-1 off the network) */
const char *net_session_id(void) { return g_session[0] ? g_session : "none"; }
int net_local_slot(void) { return g_mode == NET_OFF ? -1 : g_mode == NET_HOST ? 0 : g_slot; }

int net_synced(void)
{
	return g_mode != NET_OFF && (g_state == S_WAIT || g_state == S_RUN || g_state == S_LOST || g_state == S_OVER ||
	                             g_state == S_CATCHUP);
}

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
	char name[40], path[MAX_PATH];
	u32 head[4] = { 0x53444b41 /* "AKDS" */, 1, tick, g_nimages[slot] }, w;
	HANDLE f;

	if (!g_images) return;
	if (g_mode == NET_HOST) str_cpy(name, sizeof name, "Arklite-desync-host.bin");
	else if (g_slot <= 1) str_cpy(name, sizeof name, "Arklite-desync-join.bin");
	else fmt(name, "Arklite-desync-join%d.bin", g_slot + 1);   /* (more joiners on one PC) */
	f = log_file_create(name, path);
	if (f == INVALID_HANDLE_VALUE) {
		log_msg("net: can't write %s", path);
		return;
	}
	WriteFile(f, head, sizeof head, &w, 0);
	WriteFile(f, g_images[slot], g_nimages[slot] * sizeof(CharImage), &w, 0);
	CloseHandle(f);
	log_msg("net: wrote %s (%d characters at tick %d): send it with the other PC's", path, g_nimages[slot], tick);
	str_cpy(g_dump_img, sizeof g_dump_img, path);
}

/* The last TRING ticks one by one, for the desync dump's second file (Arklite-desyncticks-host.bin
 * / -join.bin; tools/desync_ticks.py compares two and names the first tick and field that
 * differ): after each tick, the inputs it ran with, both RNG seeds, Konoko's input state
 * (gs+0xb8) and each character in use (up to TRING_CHARS, as image_of). A rollback runs a tick
 * again and its entry is overwritten, so a verified tick's entry is its run with the real inputs.
 * The hash every HASH_EVERY ticks only says that the PCs came apart in those ticks; this says
 * where. */
#define TRING        256
#define TRING_CHARS  4
typedef struct {
	u32 tick, seeds[2], nplayers, guessed, nchars;
	u32 input[NPL][12];
	u32 gs_input[12];
	CharImage chars[TRING_CHARS];
} TickRec;
static TickRec *g_tring;

static void tring_capture(u32 ticks)
{
	TickRec *r;
	u8 *chr = GS_CHARACTERS(ONgGameState);
	u32 slots = ONgCharacterSlots, t = ticks - 1, s = t % RING;

	if (!g_tring || !ticks) return;
	r = &g_tring[ticks % TRING];
	r->tick = ticks;
	lockstep_get_seeds(&r->seeds[0], &r->seeds[1]);
	r->nplayers = g_nplayers;
	r->guessed = 0;
	for (int p = 0; p < NPL; p++) {
		if (p >= (int)g_nplayers) memset(r->input[p], 0, 0x30);
		else if (!remote(p) || t < g_have[p]) memcpy(r->input[p], g_in[p][s], 0x30);
		else {
			memcpy(r->input[p], g_pred[p][s], 0x30);
			r->guessed |= 1u << p;
		}
	}
	memcpy(r->gs_input, ONgGameState + 0xb8, 0x30);
	r->nchars = 0;
	if (slots > MAX_CHARS) slots = MAX_CHARS;
	for (u32 i = 0; i < slots && r->nchars < TRING_CHARS; i++, chr += CHR_SIZE) {
		if (!(*(u32 *)(chr + 4) & CHR_INUSE)) continue;
		r->chars[r->nchars].index = i;
		image_of(i, r->chars[r->nchars].img);
		r->nchars++;
	}
}

static void tring_dump(u32 tick)
{
	char name[48], path[MAX_PATH];
	u32 head[8] = { 0x54444b41 /* "AKDT" */, 1, tick, 0, NPL, TRING_CHARS, IMG_SIZE, sizeof(TickRec) }, w, n = 0;
	HANDLE f;

	if (!g_tring) return;
	for (u32 k = 0; k < TRING; k++)
		if (g_tring[k].tick && g_tring[k].tick <= tick && g_tring[k].tick + TRING > tick) n++;
	head[3] = n;
	if (g_mode == NET_HOST) str_cpy(name, sizeof name, "Arklite-desyncticks-host.bin");
	else if (g_slot <= 1) str_cpy(name, sizeof name, "Arklite-desyncticks-join.bin");
	else fmt(name, "Arklite-desyncticks-join%d.bin", g_slot + 1);
	f = log_file_create(name, path);
	if (f == INVALID_HANDLE_VALUE) {
		log_msg("net: can't write %s", path);
		return;
	}
	WriteFile(f, head, sizeof head, &w, 0);
	for (u32 t = tick + 1 - (tick < TRING ? tick : TRING); t <= tick; t++)   /* oldest first */
		if (t && g_tring[t % TRING].tick == t) WriteFile(f, &g_tring[t % TRING], sizeof(TickRec), &w, 0);
	CloseHandle(f);
	log_msg("net: wrote %s (%d ticks up to %d, one by one): send it with the other PC's", path, n, tick);
	str_cpy(g_dump_ticks, sizeof g_dump_ticks, path);
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

/* Compares hash slot `slot` with the one link l's PC sent for the same tick */
static void compare_hash(int l, u32 slot)
{
	Link *k = &g_link[l];
	u32 tick = g_mine[slot].tick;
	if (!tick || k->theirs[slot].tick != tick || k->checked[slot] == tick) return;
	if (ROLLING && g_published[slot] != tick) return;   /* not run with every player's real inputs yet */
	k->checked[slot] = tick;
	g_checks++;
	if (k->theirs[slot].hash != g_mine[slot].hash) {
		if (!g_bad_checks++) {
			g_first_bad = tick;
			log_msg("net: DESYNC at tick %d: state hash %08x here, %08x on %s's PC (player %d); this PC's characters:",
			        tick, g_mine[slot].hash, k->theirs[slot].hash, g_mode == NET_HOST ? mp_name(l) : "the host",
			        g_mode == NET_HOST ? l + 1 : 1);
			log_msg("net:   RNG seeds here %08x/%08x (the hash covers them; if every character matches, these differ)",
			        g_mine[slot].seeds[0], g_mine[slot].seeds[1]);
			log_summary(slot);
			g_dump_img[0] = g_dump_ticks[0] = 0;
			dump_images(slot, tick);
			tring_dump(tick);
			report_net(REPORT_DESYNC, g_session, net_local_slot(), tick, g_dump_img, g_dump_ticks, rollback_check_path());
		}
	}
}

static void compare_all(u32 slot)
{
	for (int l = 0; l < NPL; l++)
		if (live(l)) compare_hash(l, slot);
}

/* ---- The session's history (joining a game in progress) ------------------------------------ *
 * The host keeps every player's input of every tick of the session once it's final (every
 * player's real one is here: ticks < remote_min, and its own), and the state hash of every
 * HASH_EVERY-th tick once that tick ran with them. A joiner coming in later loads the level, gets
 * all of it (PKT_HIST_REQ / PKT_HIST) and runs the ticks again with it, checking each hash. The
 * memory is reserved once (HIST_BYTES) and committed as it fills; it's Arklite's own, so rollback
 * never puts it back. */

static void head(PktHead *h, u8 type);

static int hist_alloc(void)
{
	if (g_hist) return 1;
	g_hist_cap = HIST_BYTES / (HIST_TICK * 4);
	g_hist = VirtualAlloc(0, HIST_BYTES, 0x2000 /* MEM_RESERVE */, 4);
	g_hist_hash = VirtualAlloc(0, (g_hist_cap / HASH_EVERY + 1) * 4, 0x3000, 4);
	g_hist_got = VirtualAlloc(0, g_hist_cap, 0x3000, 4);
	if (!g_hist || !g_hist_hash || !g_hist_got) {
		log_msg("net: no memory for the session's inputs: players can join only at the next reload");
		if (g_hist) VirtualFree(g_hist, 0, 0x8000 /* MEM_RELEASE */);
		g_hist = 0;
		return 0;
	}
	g_hist_committed = 0;
	return 1;
}

/* Room for ticks < n */
static int hist_room(u32 n)
{
	u32 need;
	if (!g_hist || n > g_hist_cap) return 0;
	need = n * HIST_TICK * 4;
	if (need <= g_hist_committed) return 1;
	need = (need + (1u << 20) - 1) & ~((1u << 20) - 1);
	if (need > HIST_BYTES) need = HIST_BYTES;
	if (!VirtualAlloc(g_hist, need, 0x1000 /* MEM_COMMIT */, 4)) return 0;
	g_hist_committed = need;
	return 1;
}

static void hist_reset(void)
{
	g_hist_ok = hist_alloc();
	g_hist_n = g_hist_avail = g_hist_req_at = g_hist_req_to = 0;
	g_hist_nplayers = 0;
	if (!g_hist_ok) return;
	memset(g_hist_hash, 0, (g_hist_cap / HASH_EVERY + 1) * 4);
	memset(g_hist_got, 0, g_hist_cap);
}

static void hist_hash_put(u32 tick, u32 hash)
{
	if (g_mode == NET_HOST && g_hist_ok && tick < g_hist_cap) g_hist_hash[tick / HASH_EVERY] = hash;
}

/* host: the ticks whose inputs are all final now */
static void hist_append(void)
{
	u32 upto = remote_min();
	if (g_mode != NET_HOST || !g_hist_ok || g_state != S_RUN) return;
	if (g_gen < upto) upto = g_gen;
	while (g_hist_n < upto) {
		u32 t = g_hist_n, *row;
		if (t + RING - 8 < g_gen || !hist_room(t + 1)) {
			log_msg("net: the session's inputs from tick %d couldn't be kept: from now on players join at the next reload", t);
			g_hist_ok = 0;
			return;
		}
		row = g_hist + t * HIST_TICK;
		for (int p = 0; p < NPL; p++)
			if (p < (int)g_nplayers) memcpy(row + p * 12, g_in[p][t % RING], 0x30);
			else memset(row + p * 12, 0, 0x30);
		g_hist_n++;
	}
}

/* host: ticks first.. into out (room bytes, at most max ticks); returns the bytes, *count the ticks */
static u32 hist_encode(u32 first, u32 max, u8 *out, u32 room, u32 *count)
{
	static const u32 zero[HIST_TICK];
	u32 used = 0, n = 0;
	for (; n < max && first + n < g_hist_n; n++) {
		const u32 *row = g_hist + (first + n) * HIST_TICK, *prev = n ? row - HIST_TICK : zero;
		u8 one[1 + NPL * (2 + 0x30)];
		u32 k = 1;
		one[0] = 0;
		for (int p = 0; p < NPL; p++) {
			u16 words = 0;
			for (int w = 0; w < 12; w++)
				if (row[p * 12 + w] != prev[p * 12 + w]) words |= (u16)(1 << w);
			if (!words) continue;
			one[0] |= (u8)(1 << p);
			memcpy(one + k, &words, 2);
			k += 2;
			for (int w = 0; w < 12; w++)
				if (words >> w & 1) {
					memcpy(one + k, &row[p * 12 + w], 4);
					k += 4;
				}
		}
		if (used + k > room) break;
		memcpy(out + used, one, k);
		used += k;
	}
	*count = n;
	return used;
}

/* joiner: what hist_encode made; 0 if it doesn't read right */
static int hist_decode(u32 first, u32 count, const u8 *in, u32 bytes)
{
	u32 row[HIST_TICK], i = 0;
	memset(row, 0, sizeof row);
	for (u32 n = 0; n < count; n++) {
		u32 t = first + n;
		u8 players;
		if (i >= bytes) return 0;
		players = in[i++];
		for (int p = 0; p < NPL; p++) {
			u16 words;
			if (!(players >> p & 1)) continue;
			if (i + 2 > bytes) return 0;
			memcpy(&words, in + i, 2);
			i += 2;
			for (int w = 0; w < 12; w++)
				if (words >> w & 1) {
					if (i + 4 > bytes) return 0;
					memcpy(&row[p * 12 + w], in + i, 4);
					i += 4;
				}
		}
		if (!hist_room(t + 1)) return 0;
		if (!g_hist_got[t]) {
			memcpy(g_hist + t * HIST_TICK, row, sizeof row);
			g_hist_got[t] = 1;
		}
	}
	while (g_hist_n < g_hist_cap && g_hist_got[g_hist_n]) g_hist_n++;
	return 1;
}

static void send_link(int l, const void *p, u32 n);

/* host: a joining PC asked for ticks from.. */
static void hist_serve(int l, const PktHistReq *q)
{
	static u8 buf[sizeof(PktHist) + 6 * 8 + HIST_PKT_BYTES];
	PktHist *p = (PktHist *)buf;
	u32 from = q->from, end;

	if (!g_hist_ok || q->session != g_go_session || !g_link[l].joining || from > g_hist_n) return;
	end = from + (q->count < HIST_REQ_TICKS ? q->count : HIST_REQ_TICKS);
	if (end > g_hist_n) end = g_hist_n;
	for (u32 k = 0; k < HIST_REQ_PKTS; k++) {
		u32 *hashes = (u32 *)(p + 1), nh = 0, count = 0, max = end > from ? end - from : 0;
		u8 *data = (u8 *)(hashes + 12);
		if (!max && k) break;   /* (one packet in any case: it says how far the host is) */
		if (max > HIST_PKT_TICKS) max = HIST_PKT_TICKS;
		memset(p, 0, sizeof *p);
		head(&p->h, PKT_HIST);
		p->bytes = hist_encode(from, max, data, HIST_PKT_BYTES, &count);
		for (u32 t = (from + HASH_EVERY - 1) / HASH_EVERY * HASH_EVERY; t <= from + count && nh < 6; t += HASH_EVERY)
			if (t && g_hist_hash[t / HASH_EVERY]) {
				hashes[nh * 2] = t;
				hashes[nh * 2 + 1] = g_hist_hash[t / HASH_EVERY];
				nh++;
			}
		for (u32 i = 0; i < p->bytes; i++) ((u8 *)(hashes + nh * 2))[i] = data[i];   /* up against the hashes */
		p->session = g_go_session;
		p->first = from;
		p->count = count;
		p->avail = g_hist_n;
		p->nplayers = g_nplayers;
		p->nhash = nh;
		send_link(l, buf, sizeof *p + nh * 8 + p->bytes);
		if (!count) break;
		from += count;
	}
}

/* joiner: the host's answer */
static void hist_received(const PktHist *p, u32 n)
{
	const u32 *hashes = (const u32 *)(p + 1);

	if (g_state != S_CATCHUP || p->session != g_go_taken || !g_hist_ok) return;
	if (p->nhash > 6 || p->bytes > HIST_PKT_BYTES || sizeof *p + p->nhash * 8 + p->bytes > n) return;
	for (u32 i = 0; i < p->nhash; i++)
		if (hashes[i * 2] < g_hist_cap) g_hist_hash[hashes[i * 2] / HASH_EVERY] = hashes[i * 2 + 1];
	if (p->count && p->first < g_hist_cap && !hist_decode(p->first, p->count, (const u8 *)(hashes + p->nhash * 2), p->bytes))
		log_msg("net: a packet of the session's inputs (ticks %d-%d) didn't read right", p->first, p->first + p->count - 1);
	if (p->avail > g_hist_avail) g_hist_avail = p->avail;
	if (p->nplayers <= NPL) g_hist_nplayers = p->nplayers;
}

/* joiner: the next ticks, two requests in flight; again from the first missing one after a pause */
static void hist_request(u32 now)
{
	PktHistReq q;
	int again = now - g_hist_req_at >= HIST_RETRY_MS;
	u32 from;

	if (!again && g_hist_n + HIST_REQ_TICKS / 2 < g_hist_req_to) return;
	from = again ? g_hist_n : g_hist_req_to;
	if (!again && g_hist_avail && from >= g_hist_avail) return;   /* the host has no more yet */
	memset(&q, 0, sizeof q);
	head(&q.h, PKT_HIST_REQ);
	q.session = g_go_taken;
	q.from = from;
	q.count = HIST_REQ_TICKS;
	send_link(0, &q, sizeof q);
	g_hist_req_to = from + HIST_REQ_TICKS;
	g_hist_req_at = now;
}

static int g_cu_abort;                 /* joiner: the replay came out differently (found inside a tick) */

/* joiner, catching up: this tick's state against the host's */
static void catchup_check(u32 slot)
{
	u32 tick = g_mine[slot].tick, want = tick < g_hist_cap ? g_hist_hash[tick / HASH_EVERY] : 0;
	if (!want) return;
	g_checks++;
	if (want == g_mine[slot].hash || g_cu_abort) return;
	g_bad_checks++;
	log_msg("net: DESYNC while catching up: at tick %d the state hash is %08x here, %08x on the host; this PC's characters:",
	        tick, g_mine[slot].hash, want);
	log_msg("net:   RNG seeds here %08x/%08x", g_mine[slot].seeds[0], g_mine[slot].seeds[1]);
	log_summary(slot);
	g_dump_img[0] = g_dump_ticks[0] = 0;
	dump_images(slot, tick);
	tring_dump(tick);
	report_net(REPORT_DESYNC, g_session, net_local_slot(), tick, g_dump_img, g_dump_ticks, rollback_check_path());
	g_cu_abort = 1;
}

/* lockstep.c's tick hook, after every tick */
void net_tick_done(void)
{
	if (g_state != S_RUN && g_state != S_LOST && g_state != S_CATCHUP) return;
	g_ticks++;
	tring_capture(g_ticks);
	if (g_ticks % HASH_EVERY) return;
	u32 slot = (g_ticks / HASH_EVERY) % HASH_SLOTS;
	g_mine[slot].hash = summarize(g_mine[slot].chars, &g_mine[slot].n);
	lockstep_get_seeds(&g_mine[slot].seeds[0], &g_mine[slot].seeds[1]);
	capture_images(slot);
	g_mine[slot].tick = g_ticks;
	if (g_state == S_CATCHUP) {   /* the host's inputs: the hash the host had */
		catchup_check(slot);
		return;
	}
	if (ROLLING && g_ticks > remote_min()) return;   /* a guess: published once verified (publish_verified) */
	g_published[slot] = g_ticks;
	g_last_hash_tick = g_ticks;
	g_last_hash = g_mine[slot].hash;
	hist_hash_put(g_ticks, g_last_hash);
	compare_all(slot);
}

/* Rollback: the hashes of ticks that turned out to have been run with the right inputs */
static void publish_verified(void)
{
	for (u32 slot = 0; slot < HASH_SLOTS; slot++) {
		u32 tick = g_mine[slot].tick;
		if (!tick || g_published[slot] == tick || tick > g_verified) continue;
		g_published[slot] = tick;
		hist_hash_put(tick, g_mine[slot].hash);
		if (tick > g_last_hash_tick) {
			g_last_hash_tick = tick;
			g_last_hash = g_mine[slot].hash;
		}
		compare_all(slot);
	}
}

/* ---- Strangers ---------------------------------------------------------------------------- *
 * With the host's port forwarded, anyone on the internet can send packets to it, and UDP lets a
 * sender write any address as its own. So (protocol 19):
 *   - The host gives each address a cookie (cookie_of: SipHash-2-4 of the IP and port, keyed by
 *     128 random bits made when the port opens). A HELLO or map download request without the
 *     address's cookie in its head (key) gets only PKT_COOKIE back, a head with it, no bigger than
 *     the request (so a forged address can't turn the host into a flood of someone else). The PC
 *     really there puts it in the head of everything it sends after that. Only packets with the
 *     sender's cookie count at all: a place in the lobby, inputs, chat, goodbye, map files.
 *   - Everything the host sends a joiner carries that cookie too, and the joiner takes only packets
 *     from the host's address with it (a new cookie only while none came right for KEY_KEEP_MS:
 *     the host restarted), so nobody but the host can speak for the host.
 *   - The Join list's question (PKT_INFO_REQ) is padded to the size of the answer, for the same
 *     reason; an older Arklite's short question gets a short answer, which says "another version".
 *   - Each player's input is cleaned before it runs (strip): no NaN, infinity or absurd aim.
 * Everything else is ignored and counted; the log says how much every STRANGER_LOG_MS. */
#define KEY_KEEP_MS     2000
#define STRANGER_LOG_MS 30000

static u32 g_secret[4];                /* host: the cookies' key, made when the port opens */
static u32 g_key;                      /* joiner: the host's cookie for this PC's address, 0: none yet */
static u32 g_key_ok_at;                /* joiner: when a packet carrying it last came */
static int g_dl_rekeyed;               /* joiner, downloading: a cookie came, ask again at once */
static u32 g_strangers, g_strangers_at;

/* ---- Hole punching ------------------------------------------------------------------------ *
 * A host behind a NAT it can't open (the provider's shared address, CGNAT) can't be reached until
 * it sends to the joiner first, and the joiner's NAT the same the other way. So both knock: the
 * joiner's HELLO (or the Join list's PKT_INFO_REQ) goes to the host's internet address and port,
 * which the host learned from STUN (stun.c) and gave the joiner; the host types the joiner's
 * address, shown on the joiner's Join list or in its chat, after /punch, and sends it PKT_COOKIE
 * every PUNCH_EVERY_MS for PUNCH_MS (it becomes a link, or time's up). Each NAT then takes the
 * other's packets for answers. The joiner's key handshake runs as for anyone ("Strangers").
 * The Join list's socket (g_qsock) asked the server from its own NAT mapping: Play hands that very
 * socket to the game (g_adopt, net_open) so the hole punched for it stays the game's. */
#define PUNCH_MAX       8
#define PUNCH_MS        120000
#define PUNCH_EVERY_MS  200
#define HINT_AFTER_MS   4000         /* joiner: the host silent this long, the chat says how /punch helps */
#define HINT_HELPED_MS  12000        /* ...this long when the helper said it told the host (helper.c) */
#define INTRO_PUNCH_MS  15000        /* host: knocking toward a player the helper introduced */
#define INTRO_AFTER_MS  300          /* joiner: the host silent this long, the helper is asked to introduce it... */
#define INTRO_EVERY_MS  2000         /* ...and again this often while it stays silent */
static u32 g_qsock = INVALID_SOCKET;   /* the Join list's (see "Server info for the Join list") */
static u32 g_adopt = INVALID_SOCKET;   /* the Join list's socket, kept for the game's (Play) */
static struct { SOCKADDR_IN a; u32 until, last; int quiet; } g_punch[PUNCH_MAX];
static u32 g_intro_at;                 /* joiner: the last introduction asked for */
static int g_helper_knows;             /* joiner: the helper said it knows the host (1) or not (-1); 0 no answer */
static u32 g_opened_at;                /* when g_sock opened */
static char g_hint_addr[32];           /* joiner: its address when the chat last gave the /punch hint */
static int g_punch_said;               /* host: the /punch line was in the chat (helper off) */

static u64 rotl64(u64 x, int b) { return x << b | x >> (64 - b); }

/* SipHash-2-4 of one 8-byte message (only adds, xors and rotations: no 64-bit library calls) */
static u64 siphash8(const u32 *key, u64 m)
{
	u64 k0 = (u64)key[1] << 32 | key[0], k1 = (u64)key[3] << 32 | key[2];
	u64 v0 = k0 ^ 0x736f6d6570736575ull, v1 = k1 ^ 0x646f72616e646f6dull;
	u64 v2 = k0 ^ 0x6c7967656e657261ull, v3 = k1 ^ 0x7465646279746573ull;
	u64 b = (u64)8 << 56;
#define SIPROUND do { v0 += v1; v1 = rotl64(v1, 13); v1 ^= v0; v0 = rotl64(v0, 32); v2 += v3; v3 = rotl64(v3, 16); \
	v3 ^= v2; v0 += v3; v3 = rotl64(v3, 21); v3 ^= v0; v2 += v1; v1 = rotl64(v1, 17); v1 ^= v2; v2 = rotl64(v2, 32); } while (0)
	v3 ^= m; SIPROUND; SIPROUND; v0 ^= m;
	v3 ^= b; SIPROUND; SIPROUND; v0 ^= b;
	v2 ^= 0xff; SIPROUND; SIPROUND; SIPROUND; SIPROUND;
#undef SIPROUND
	return v0 ^ v1 ^ v2 ^ v3;
}

static u32 cookie_of(const SOCKADDR_IN *a)
{
	u32 c = (u32)siphash8(g_secret, (u64)a->addr << 16 | a->port);
	return c ? c : 1;
}

/* host: a new secret (Windows' RtlGenRandom; the clock, ids and stack as a fallback) */
static void make_secret(void)
{
	typedef unsigned char (WINAPI *GenRandom)(void *, u32);
	HANDLE adv = LoadLibraryA("advapi32.dll");
	GenRandom gen = adv ? (GenRandom)GetProcAddress(adv, "SystemFunction036") : 0;
	u32 local = 0;
	if (!gen || !gen(g_secret, sizeof g_secret)) {
		log_msg("net: no random numbers from Windows: the cookies' key comes from the clock");
		g_secret[0] ^= GetTickCount();
		g_secret[1] ^= GetCurrentProcessId() << 16 ^ GetCurrentThreadId();
		g_secret[2] ^= (u32)(unsigned long)&local;
	}
	g_secret[3] ^= counter();
}

/* Every packet this PC sends carries the key for the receiver: the host's cookie of that address,
 * a joiner's the one the host gave it */
static void put_key(const void *p, const SOCKADDR_IN *to)
{
	((PktHead *)p)->key = g_mode == NET_HOST ? cookie_of(to) : g_key;
}

static void stranger(const SOCKADDR_IN *from, const char *why)
{
	u32 now = GetTickCount();
	g_strangers++;
	if (g_strangers_at && now - g_strangers_at < STRANGER_LOG_MS) return;
	const u8 *ip = (const u8 *)&from->addr;
	log_msg("net: ignored %d packet%s from outside the game (the last from %d.%d.%d.%d:%d: %s)", g_strangers,
	        g_strangers == 1 ? "" : "s", ip[0], ip[1], ip[2], ip[3], ntohs(from->port), why);
	g_strangers = 0;
	g_strangers_at = now ? now : 1;
}

/* host: the cookie for that address, to a request that came without it */
static void send_cookie(const SOCKADDR_IN *to)
{
	PktHead h;
	memset(&h, 0, sizeof h);
	h.magic = MAGIC;
	h.proto = PROTO;
	h.type = PKT_COOKIE;
	put_key(&h, to);
	sendto(g_sock, &h, sizeof h, 0, to, sizeof *to);
}

/* joiner: the host's PKT_COOKIE. 1: it's this PC's key now */
static int joiner_cookie(u32 key)
{
	u32 now = GetTickCount();
	if (!key || key == g_key) return 0;
	if (g_key && now - g_key_ok_at < KEY_KEEP_MS) return 0;   /* the host speaks with the key it has: not the host's */
	log_msg(g_key ? "net: the host gave this PC a new key (it restarted?)" : "net: the host answered and gave this PC its key");
	g_key = key;
	g_key_ok_at = now;
	g_last_hello = 0;   /* say HELLO again at once, with it */
	return 1;
}

/* joiner: a packet from the host's address carries this PC's key (1), or isn't the host's (0) */
static int host_keyed(const PktHead *h)
{
	if (!g_key || h->key != g_key) return 0;
	g_key_ok_at = GetTickCount();
	return 1;
}

/* host: knock toward the addresses /punch named, until each is a player here or time's up */
static void punch_frame(u32 now)
{
	if (g_mode != NET_HOST || g_sock == INVALID_SOCKET) return;
	for (int i = 0; i < PUNCH_MAX; i++) {
		const u8 *ip = (const u8 *)&g_punch[i].a.addr;
		if (!g_punch[i].until) continue;
		if (link_of(&g_punch[i].a) >= 0) {
			log_msg("net: punched through to %d.%d.%d.%d:%d: it's in", ip[0], ip[1], ip[2], ip[3], ntohs(g_punch[i].a.port));
			g_punch[i].until = 0;
		} else if ((int)(now - g_punch[i].until) >= 0) {
			if (!g_punch[i].quiet)
				log_msg("net: /punch %d.%d.%d.%d:%d: no player came from there", ip[0], ip[1], ip[2], ip[3], ntohs(g_punch[i].a.port));
			g_punch[i].until = 0;
		} else if (now - g_punch[i].last >= PUNCH_EVERY_MS) {
			send_cookie(&g_punch[i].a);   /* (no bigger than any packet it lets in) */
			g_punch[i].last = now;
		}
	}
}

/* knock toward a for ms (an address already knocked at keeps the longer time); quiet: no log when
 * nobody comes (the helper's introductions: most are players only looking at the Join list) */
static void punch_add(const SOCKADDR_IN *a, u32 ms, int quiet)
{
	u32 slot = 0, now = GetTickCount(), until = now + ms;
	if (!until) until = 1;
	for (u32 i = 0; i < PUNCH_MAX; i++) {   /* that address again, or a free place, or the oldest */
		if (g_punch[i].until && g_punch[i].a.addr == a->addr && g_punch[i].a.port == a->port) {
			if ((int)(g_punch[i].until - until) > 0) return;
			slot = i;
			break;
		}
		if (!g_punch[i].until || g_punch[i].until < g_punch[slot].until) slot = i;
	}
	g_punch[slot].a = *a;
	g_punch[slot].until = until;
	g_punch[slot].last = 0;
	g_punch[slot].quiet = quiet;
}

const char *net_punch(const char *address)
{
	static char msg[CHAT_MAX];
	char host[32];
	u32 n = 0, port = 0;
	SOCKADDR_IN a;

	if (g_mode != NET_HOST || g_sock == INVALID_SOCKET) return "Only the host punches. Give the host your address (/ip).";
	while (address[n] && address[n] != ':' && address[n] != ' ' && n + 1 < sizeof host) host[n] = address[n], n++;
	host[n] = 0;
	if (address[n] == ':')
		for (const char *c = address + n + 1; *c >= '0' && *c <= '9'; c++) port = port * 10 + (*c - '0');
	memset(&a, 0, sizeof a);
	a.family = 2;
	a.addr = n ? inet_addr(host) : 0xffffffffu;
	if (a.addr == 0xffffffffu || !a.addr || !port || port > 65535) return "Usage: /punch 1.2.3.4:5678 (the address and port the player sees with /ip)";
	a.port = htons((u16)port);
	punch_add(&a, PUNCH_MS, 0);
	log_msg("net: /punch %s:%d: knocking for %d s", host, port, PUNCH_MS / 1000);
	fmt(msg, "Knocking toward that address for %d s: the player can join now.", PUNCH_MS / 1000);   /* (not repeated: streams) */
	return msg;
}

int net_my_address(char *out) { return stun_text(out); }

/* Text another PC sent (a name, a chat line): no control characters, and only whole characters
 * beyond ASCII (text.c: a broken one could make Oni's text drawing read past the end) */
static void clean_text(char *s, u32 size)
{
	if (!size) return;
	s[size - 1] = 0;
	for (u32 i = 0; i < size && s[i]; i++)
		if ((u8)s[i] < 0x20 || s[i] == 0x7f) s[i] = ' ';
	text_clean(s, size, s);
}

/* ---- Sending and receiving ---------------------------------------------------------------- */

static void head(PktHead *h, u8 type)
{
	h->magic = MAGIC;
	h->proto = PROTO;
	h->type = type;
	h->pad = 0;
	h->key = 0;   /* (put in by the send) */
}

static void send_addr(const void *p, u32 n, const SOCKADDR_IN *to)
{
	put_key(p, to);
	if (sendto(g_sock, p, n, 0, to, sizeof *to) < 0) {
		if (WSAGetLastError() == 10093) return;   /* WSANOTINITIALISED: the process is exiting */
		if (g_send_errors++ < 5) log_msg("net: send failed (error %d)", WSAGetLastError());
	} else {
		g_npackets_sent++;
	}
}

static void send_link(int l, const void *p, u32 n)
{
	if (l < 0 || l >= NPL || !g_link[l].used) return;
	send_addr(p, n, &g_link[l].addr);
	g_link[l].last_send = GetTickCount();
}

/* joiner: to the host */
static void send_to_host(const void *p, u32 n) { send_link(0, p, n); }

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
	p.phase = g_state == S_LOBBY ? 0 : g_state == S_CATCHUP || (g_joining == 2 && !g_ninputs_recv) ? 3 :
	          g_state == S_RUN && g_warm ? 2 : 1;
	p.ready = g_ready;
	if (p.phase == 3 && g_joining == 2) p.live_from = g_live_from;
	if (!p.phase && g_cu_failed && g_cu_failed == g_go_taken) p.catchup_failed = 1;
	send_to_host(&p, sizeof p);
	g_last_hello = GetTickCount();
}

static void send_start(int l)
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
	p.players = g_nplayers;
	p.gone = g_gone;
	send_link(l, &p, sizeof p);
}

/* The players there are, in this session or waiting for the next (not those still joining it) */
static u32 lobby_players(void)
{
	u32 n = 1;
	for (int l = 1; l < NPL; l++) n += g_link[l].used && !gone(l) && !g_link[l].joining;
	return n;
}

/* Player p (the host 0, a link 1..) watches (/spectate): only in a running session, where links
 * are their slots (players.c's tick state, the same on every PC); a link waiting to come in
 * doesn't have the slot yet */
static int watches(int p)
{
	if (p == 0 && g_dedicated && g_mode == NET_HOST) return 1;   /* the dedicated server's own: never plays */
	if (g_state == S_LOBBY || p >= (int)g_nplayers) return 0;
	if (p > 0 && (g_link[p].waiting || g_link[p].rejoin)) return 0;
	return players_spectating(p);
}

/* Who takes part in the ready-up: lobby_players without the spectators (they don't play, so
 * they can't be ready and don't count against the 51%). lobby_players still sizes the match:
 * a spectator keeps its place, and its records park it again in the match's first tick. */
static u32 ready_players(void)
{
	u32 n = !watches(0);
	for (int l = 1; l < NPL; l++) n += g_link[l].used && !gone(l) && !g_link[l].joining && !watches(l);
	return n;
}

static u32 lobby_ready(void)
{
	u32 n = g_ready != 0 && !watches(0);
	for (int l = 1; l < NPL; l++) n += g_link[l].used && !gone(l) && !g_link[l].joining && g_link[l].ready && !watches(l);
	return n;
}

/* The places taken: everyone there, joining or waiting included */
static u32 places_taken(void)
{
	u32 n = 1;
	for (int l = 1; l < NPL; l++) n += g_link[l].used && !gone(l);
	return n;
}

static int map_can_serve(void);

/* refused: NETBAD_*, the joiner at `to` can't play here; slot: the place it has (else 0) */
static void send_lobby_addr(const SOCKADDR_IN *to, int slot, int go, u32 refused)
{
	PktLobby p;
	u32 now = GetTickCount();
	memset(&p, 0, sizeof p);
	head(&p.h, PKT_LOBBY);
	p.level = g_level;
	p.players = ready_players();   /* (the joiner's "n of m players ready") */
	p.ready = lobby_ready();
	p.countdown_ms = g_countdown_end ? (g_countdown_end - now < COUNTDOWN_MS + 1000 ? g_countdown_end - now : 0) : 0xffffffffu;
	p.go = go;
	p.joiner_ready = slot > 0 && g_link[slot].ready;
	p.seed = g_match_seed;
	p.local_seed = g_match_local_seed;
	p.refused = refused;
	p.map_offer = (refused & NETBAD_MAP) && map_can_serve();
	p.respawn_ticks = players_respawn_ticks();
	p.item_ticks = items_default_ticks();
	p.time_limit = g_time_limit;
	p.score_limit = g_score_limit;
	p.slot = (u32)slot;
	p.capacity = capacity();
	p.level0[0] = g_files[2];
	p.level0[1] = g_files[3];
	p.session = g_go_session;
	p.warm = g_warm;
	p.in_match = g_state != S_LOBBY && !g_warm;
	p.dedicated = g_dedicated;
	p.char_rules = g_char_rules;
	if ((refused & NETBAD_KICKED) && g_lobby_note) str_cpy(p.note, sizeof p.note, g_lobby_note);
	if (go) p.players = g_nplayers;
	if (!go && !refused && slot > 0 && g_link[slot].joining && g_state == S_RUN) {   /* in now, as the session goes on */
		p.catchup = 1;
		p.in_match = 0;
		p.load_players = g_load_players;
		p.run_seed = g_seed;
		p.run_local_seed = g_local_seed;
		p.start_tick = g_start_tick;
		p.start_hash = g_start_hash;
	}
	send_addr(&p, sizeof p, to);
	if (slot > 0) g_link[slot].last_lobby = now;
}

/* Not to a removed player's leftover place (gone, kept until the session ends): its PC would read
 * the lobby as "let in" between the refusals its HELLOs get, and its "Can't join" line flashed */
static void send_lobby(int l, int go)
{
	if (gone(l) && ban_note(g_link[l].addr.addr)) return;
	send_lobby_addr(&g_link[l].addr, l, go, 0);
}

static void send_lobby_all(int go)
{
	for (int l = 1; l < NPL; l++)
		if (g_link[l].used) send_lobby(l, go);
}

/* What a PKT_INPUT to link l carries: if it changes, a new one goes at once */
static u32 sig_for(int l)
{
	u32 s = g_last_hash_tick * 31u;
	for (int p = 0; p < (int)g_nplayers; p++)
		if (p != l) s = s * 0x01000193u + have_of(p);
	return s;
}

/* Every input link l's PC hasn't acknowledged (at most MAX_SEND in all, shared between the
 * players): a joiner's own, or on the host every player's but the joiner's */
static void send_inputs(int l)
{
	static u8 buf[sizeof(PktInput) + NPL * sizeof(Stream) + MAX_SEND * 0x30];
	PktInput *p = (PktInput *)buf;
	Link *k = &g_link[l];
	u8 *out = buf + sizeof *p;
	int send[NPL], nsend = 0;
	u32 per;

	memset(p, 0, sizeof *p);
	head(&p->h, PKT_INPUT);
	for (int q = 0; q < NPL; q++) p->ack[q] = q < (int)g_nplayers ? have_of(q) : 0;
	p->hash_tick = g_last_hash_tick;
	p->hash = g_last_hash;
	p->adv = (int)g_ticks - (int)g_have[l];
	p->stamp = GetTickCount();
	p->echo = k->stamp;
	p->held = k->stamp ? p->stamp - k->stamp_at : 0;
	p->gone = g_gone;
	p->left = g_left;
	for (int q = 0; q < NPL; q++) p->ping[q] = q && g_link[q].used && g_link[q].ping >= 0 ? (u16)g_link[q].ping : 0xffff;
	p->nplayers = g_nplayers;
	memcpy(p->base, g_base, sizeof p->base);
	p->absent = g_absent;
	if (g_mode == NET_HOST && absent(l) && k->joining) {   /* its own inputs don't count yet: what came of them */
		p->ack[l] = k->spect_gen;
		p->adv = (int)g_ticks - (int)k->spect_gen;
	}

	for (int q = 0; q < (int)g_nplayers; q++) {
		if (g_mode == NET_JOIN ? q != g_slot : q == l) continue;
		if (k->ack[q] < have_of(q)) send[nsend++] = q;
	}
	per = nsend ? MAX_SEND / nsend : 0;
	for (int i = 0; i < nsend; i++) {
		int q = send[i];
		u32 from = k->ack[q], to = have_of(q), n;
		Stream *st = (Stream *)out;
		if (from + RING - 8 < to) from = to - (RING - 8);   /* far behind: the ring moved on */
		n = to - from;
		if (n > per) n = per;
		st->first = from;
		st->slot = (u16)q;
		st->count = (u16)n;
		out += sizeof *st;
		for (u32 j = 0; j < n; j++, out += 0x30) memcpy(out, g_in[q][(from + j) % RING], 0x30);
		p->nstreams++;
	}
	send_link(l, buf, (u32)(out - buf));
	k->sent_sig = sig_for(l);
}

/* Inputs to every PC that needs them: new ones at once, the rest every RESEND_MS */
static void send_inputs_due(u32 now)
{
	for (int l = 0; l < NPL; l++)
		if (live(l) && (g_link[l].sent_sig != sig_for(l) || now - g_link[l].last_send >= RESEND_MS)) send_inputs(l);
}

/* Names: a joiner sends its own to the host, the host every player's to every joiner */
static void send_names(u32 now)
{
	if (g_mode == NET_JOIN) {
		PktName p;
		if (!g_link[0].used || (g_name_sent == mp_name_serial() && now - g_last_name < NAME_MS)) return;
		memset(&p, 0, sizeof p);
		head(&p.h, PKT_NAME);
		str_cpy(p.name, sizeof p.name, mp_name_local());
		send_to_host(&p, sizeof p);
		g_last_name = now;
		g_name_sent = mp_name_serial();
		return;
	}
	if (g_names_local != mp_name_serial()) {
		g_names_local = mp_name_serial();
		g_names_serial++;
	}
	for (int l = 1; l < NPL; l++) {
		Link *k = &g_link[l];
		PktNames p;
		if (!live(l) || (k->last_names == g_names_serial && now - k->names_at < NAME_MS)) continue;
		memset(&p, 0, sizeof p);
		head(&p.h, PKT_NAMES);
		str_cpy(p.name[0], MP_NAME_MAX, mp_name_local());
		for (int q = 1; q < NPL; q++)
			if (g_link[q].used) str_cpy(p.name[q], MP_NAME_MAX, g_link[q].name);
		send_link(l, &p, sizeof p);
		k->last_names = g_names_serial;
		k->names_at = now;
	}
}

/* ---- Chat ---------------------------------------------------------------------------------- */

static void chat_send_queued(int l)
{
	Link *k = &g_link[l];
	PktChat p;
	u32 now = GetTickCount(), keep = 0;

	for (u32 i = 0; i < k->chat_out_n; i++)
		if (now - k->chat_out[i].at < CHAT_GIVE_UP_MS) k->chat_out[keep++] = k->chat_out[i];
	k->chat_out_n = keep;
	for (u32 i = 0; i < k->chat_out_n; i++) {
		memset(&p, 0, sizeof p);
		head(&p.h, PKT_CHAT);
		p.stream = k->chat_stream;
		p.seq = k->chat_out[i].seq;
		p.from = k->chat_out[i].from;
		p.to = k->chat_out[i].to;
		str_cpy(p.text, sizeof p.text, k->chat_out[i].text);
		send_link(l, &p, sizeof p);
	}
	k->chat_sent_at = now;
}

static void chat_queue(int l, u32 from, u32 to, const char *text)
{
	Link *k = &g_link[l];
	if (k->chat_out_n == CHAT_QUEUE) {   /* the oldest is lost */
		memcpy(k->chat_out, k->chat_out + 1, sizeof k->chat_out[0] * (CHAT_QUEUE - 1));
		k->chat_out_n--;
	}
	k->chat_out[k->chat_out_n].seq = ++k->chat_seq;
	k->chat_out[k->chat_out_n].at = GetTickCount();
	k->chat_out[k->chat_out_n].from = from;
	k->chat_out[k->chat_out_n].to = to;
	str_cpy(k->chat_out[k->chat_out_n].text, CHAT_MAX, text);
	k->chat_out_n++;
	chat_send_queued(l);
}

/* Someone to talk to: a joiner in the lobby (host), the host answered (joiner), or a match */
static int chat_link(int l)
{
	if (!live(l)) return 0;
	if (g_state == S_RUN || g_state == S_WAIT || g_state == S_OVER || g_state == S_CATCHUP) return 1;
	return g_mode == NET_HOST ? g_state == S_LOBBY : g_host_heard;
}

int net_chat_send(const char *text)
{
	int sent = 0;
	if (g_mode == NET_OFF) return 0;
	for (int l = 0; l < NPL; l++)
		if (chat_link(l)) {
			chat_queue(l, (u32)g_slot, CHAT_ALL, text);
			sent = 1;
		}
	return sent;
}

/* A whisper: the host sends it straight to that joiner, a joiner to the host, who reads it if it's
 * for the host or passes it on to that joiner */
int net_chat_whisper(int to, const char *text)
{
	int l = g_mode == NET_HOST ? to : 0;
	if (g_mode == NET_OFF || to < 0 || to >= NPL || to == g_slot || l < 0 || l >= NPL || !chat_link(l)) return 0;
	if (g_mode == NET_HOST && gone(l)) return 0;
	chat_queue(l, (u32)g_slot, (u32)to, text);
	return 1;
}

static void chat_received(int l, const PktChat *p)
{
	Link *k = &g_link[l];
	PktChatAck a;
	char text[CHAT_MAX];

	if (p->stream != k->chat_in_stream) {
		k->chat_in_stream = p->stream;
		k->chat_in_seq = p->seq - 1;
	}
	if (p->seq == k->chat_in_seq + 1) {
		int who = g_mode == NET_HOST ? l : (int)p->from;
		memcpy(text, p->text, sizeof text);
		text[sizeof text - 1] = 0;
		clean_text(text, sizeof text);
		k->chat_in_seq = p->seq;
		if (g_mode == NET_JOIN && p->from == CHAT_SERVER) {
			chat_server(text);
		} else if (p->to != CHAT_ALL) {   /* a whisper: this PC's player's, or (host) a joiner's to pass on */
			if (who < 0 || who >= NPL || who == g_slot) {
			} else if ((int)p->to == g_slot) {
				chat_whisper_receive(who, text);
			} else if (g_mode == NET_HOST && p->to < NPL && (int)p->to != l && chat_link((int)p->to) && !gone((int)p->to)) {
				chat_queue((int)p->to, (u32)who, p->to, text);
			}
		} else if (who >= 0 && who < NPL && who != g_slot) {
			chat_receive(who, text);
			if (g_mode == NET_HOST)   /* to the other joiners */
				for (int o = 1; o < NPL; o++)
					if (o != l && chat_link(o)) chat_queue(o, (u32)who, CHAT_ALL, text);
		}
	}
	memset(&a, 0, sizeof a);
	head(&a.h, PKT_CHAT_ACK);
	a.stream = k->chat_in_stream;
	a.seq = k->chat_in_seq;
	send_link(l, &a, sizeof a);
}

/* "Name entered the game" / "Name left the game": the host's own screen, and every joiner's
 * (but the one leaving). A joiner says it of the host itself when the host goes. */
static void server_line(const char *name, const char *what)
{
	char t[CHAT_MAX];
	fmt(t, "%s %s", name, what);
	chat_server(t);
	if (g_mode != NET_HOST) return;
	for (int o = 1; o < NPL; o++)
		if (chat_link(o) && !gone(o)) chat_queue(o, CHAT_SERVER, CHAT_ALL, t);
}

/* This PC's player's new name (mp_names.c): the host says so to everyone; a joiner's new name
 * reaches the host in its next PKT_NAME, and the host says it then. Out of a network game only
 * this screen hears it. */
void net_name_changed(const char *before, const char *after)
{
	char what[CHAT_MAX];
	if (g_mode == NET_JOIN && g_link[0].used) return;
	fmt(what, "is now known as %s", after);
	server_line(before, what);
}

static void chat_acked(int l, const PktChatAck *a)
{
	Link *k = &g_link[l];
	u32 keep = 0;
	if (a->stream != k->chat_stream) return;
	for (u32 i = 0; i < k->chat_out_n; i++)
		if ((int)(k->chat_out[i].seq - a->seq) > 0) k->chat_out[keep++] = k->chat_out[i];
	k->chat_out_n = keep;
}

static void chat_resend_due(u32 now)
{
	for (int l = 0; l < NPL; l++)
		if (g_link[l].chat_out_n && now - g_link[l].chat_sent_at >= CHAT_RESEND_MS) {
			if (live(l)) chat_send_queued(l);
			else g_link[l].chat_out_n = 0;
		}
}

static void send_bye_to(int l)
{
	PktHead h;
	head(&h, PKT_BYE);
	for (int i = 0; i < 3; i++) send_link(l, &h, sizeof h);   /* UDP: one may be lost, and there's no next time */
}

static void send_bye(void)
{
	for (int l = 0; l < NPL; l++)
		if (live(l)) send_bye_to(l);
}

/* Logs what differs on the peer; returns it (NETBAD_*) */
static u32 check_files(const char *who, const Compat *c)
{
	u32 bad = compat_problems(c);
	char v[17];

	for (u32 i = 0; i < NFILES; i++)
		if (c->files[i] != g_files[i])
			log_msg("net: %s differs on the %s (%08x there, %08x here): the games will come apart; use the same AE install on every PC",
			        g_file_names[i], who, c->files[i], g_files[i]);
	memcpy(v, c->version, 16);
	v[16] = 0;
	if (!str_ieq_n(v, ARKLITE_VERSION, 17))
		log_msg("net: the %s runs Arklite %s, this PC %s", who, v, ARKLITE_VERSION);
	if (bad & NETBAD_DLL)
		log_msg("net: Arklite differs on the %s (source %08x there, %08x here): use the same Arklite release or commit on every PC",
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

/* The peer's game isn't this one, or the server is full (NETBAD_*): keep it out, and say why in
 * the log (once while it keeps trying) and on screen (net_hud) */
static void refuse(const char *who, u32 bad, const Compat *c)
{
	u32 now = GetTickCount();

	if (bad != g_problem || !g_problem_at || now - g_problem_at > LOST_MS) {
		if (bad == NETBAD_FULL)
			log_msg("net: %s", g_mode == NET_HOST ? "a player can't join: the server is full" : "the server is full");
		else if (bad == NETBAD_KICKED)
			log_msg("net: %s", g_mode == NET_HOST ? "a removed player was turned away" : g_kick_note[0] ? g_kick_note : "the server removed this PC");
		else
			log_msg("net: the %s's game isn't the same as this PC's, so they can't play together:", who);
		if (c) check_files(who, c);
		for (u32 bit = 1; bit <= NETBAD_LAST; bit <<= 1)
			if ((bad & bit) && bit != NETBAD_FULL && bit != NETBAD_KICKED) log_msg("net:   differs: %s", net_problem_text(bit));
	}
	g_problem = bad;
	g_problem_at = now ? now : 1;
	g_problem_hidden = 0;
}

static void start_match(void);

static void strip(u32 *rec)
{
	rec[0] &= ~7u;   /* escape, console, pausescreen: per PC */
	/* words 2-10 are floats (analogs 0-8; word 11 is Arklite's, see char_menu.c): a NaN, an infinity
	 * or an aim of 16384 units (256 radians) and more in one tick isn't anyone's mouse, and the game
	 * would carry it in every character it touches: nothing pressed there instead */
	for (int w = 2; w <= 10; w++)
		if ((rec[w] >> 23 & 0xff) >= 127 + 14) rec[w] = 0;
}

/* host: player p's PC stopped answering or left. From the first tick the host has no input of
 * p's for, p's inputs are "nothing pressed", made up by the host (fill_gone) and sent to the
 * other joiners like everyone's, so every PC runs p's player the same way, and from the next
 * input the host makes, its record says p has no character any more (players.c takes it out of
 * the game on every PC at that tick). */
static void mark_gone(int p, int left, const char *why, int said)
{
	if (gone(p)) return;
	g_gone |= 1u << p;
	if (left) g_left |= 1u << p;
	g_link[p].chat_out_n = 0;
	if (g_off_from[p] > g_gen) g_off_from[p] = g_gen;
	log_msg("net: %s (player %d) %s; their player stands still from tick %d and is out of the game from tick %d", mp_name(p),
	        p + 1, why, g_have[p], g_gen);
	if (said) server_line(mp_name(p), "left the game");
}

/* host: the inputs of the gone and absent players, past the frame's ticks */
static void fill_gone(void)
{
	u32 to = g_ticks + g_delay + 12;
	for (int p = 1; p < (int)g_nplayers; p++) {
		if (!gone(p) && !absent(p)) continue;
		while (g_have[p] < to) {
			u32 t = g_have[p], slot = t % RING;
			memset(g_in[p][slot], 0, 0x30);
			g_in_at[p][slot] = t + 1;
			g_have[p]++;
		}
	}
}

/* Players p (from g_nplayers to n-1) are added to the session: their inputs exist from base[p]
 * (before it nothing pressed, as this PC ran them), and nobody plays them until the host's record
 * says so */
static void add_players(u32 n, const u32 *base)
{
	for (u32 q = g_nplayers; q < n; q++) {
		u32 b = base && base[q] <= g_ticks + RING ? base[q] : g_ticks;
		memset(g_in[q], 0, sizeof g_in[q]);
		memset(g_in_at[q], 0, sizeof g_in_at[q]);
		memset(g_pred[q], 0, sizeof g_pred[q]);
		g_base[q] = b;
		g_have[q] = b;
		for (int l = 0; l < NPL; l++) g_link[l].ack[q] = b;
		if ((int)q != g_slot) g_absent |= 1u << q;
		g_on_from[q] = g_off_from[q] = 0xffffffffu;
		log_msg("net: player %d added to the session at tick %d (inputs from tick %d)", q + 1, g_ticks, b);
	}
	g_nplayers = n;
}

static void on_input(int l, const PktInput *p, u32 size)
{
	Link *k = &g_link[l];
	const u8 *data = (const u8 *)(p + 1), *end = (const u8 *)p + size;
	u32 now = GetTickCount();

	if (size < sizeof *p || p->nstreams > NPL) return;
	for (int q = 0; q < NPL && q < (int)g_nplayers; q++) {
		if (p->ack[q] > k->ack[q] && p->ack[q] <= have_of(q)) k->ack[q] = p->ack[q];
		if (g_mode == NET_HOST && p->ack[q] >= g_base[q]) k->knows |= 1u << q;
	}
	k->adv = p->adv > RING ? RING : p->adv < -RING ? -RING : p->adv;
	k->adv_known = 1;
	if ((int)(p->stamp - k->stamp) > 0 || !k->stamp) {
		k->stamp = p->stamp;
		k->stamp_at = now;
	}
	if (p->echo) {
		u32 rtt = now - p->echo - p->held;
		if (rtt < 10000) {
			k->ping = (int)rtt;
			k->ping_sum += rtt;
			k->ping_n++;
			if (rtt > k->ping_max) k->ping_max = rtt;
		}
	}
	if (g_mode == NET_JOIN) {
		u32 news, all;
		if (p->nplayers > g_nplayers && p->nplayers <= NPL) add_players(p->nplayers, p->base);
		all = ((1u << g_nplayers) - 1) & ~(1u << g_slot);
		news = p->gone & ~g_gone & all;
		for (int q = 0; q < NPL; q++) {
			g_peer_ping[q] = p->ping[q] == 0xffff ? -1 : p->ping[q];
			if (news >> q & 1) log_msg("net: the host says %s (player %d) %s; their player is out of the game", mp_name(q), q + 1,
			                           p->left >> q & 1 ? "left" : "stopped answering");
		}
		g_gone = p->gone & all;   /* (one who comes back joins again: no longer gone) */
		g_left = p->left & g_gone;
		g_absent = p->absent & all;
	}
	for (u32 i = 0; i < p->nstreams; i++) {
		const Stream *st = (const Stream *)data;
		int q;
		if (data + sizeof *st > end) return;
		data += sizeof *st;
		if (st->count > MAX_SEND || data + st->count * 0x30 > end) return;
		q = st->slot;
		/* joining: its own inputs before it's in only tell how far it is */
		if (g_mode == NET_HOST && q == l && absent(q) && k->joining == 2 && st->first <= k->spect_gen &&
		    st->first + st->count > k->spect_gen)
			k->spect_gen = st->first + st->count;
		/* the host takes only the joiner's own player from it, and none of a gone or absent one's */
		if (remote(q) && (g_mode == NET_JOIN || (q == l && !gone(q) && !absent(q)))) {
			for (u32 j = 0; j < st->count; j++) {
				u32 t = st->first + j, slot = t % RING;
				if (t < g_have[q] || t >= g_ticks + RING - 8) continue;
				memcpy(g_in[q][slot], data + j * 0x30, 0x30);
				strip(g_in[q][slot]);
				g_in_at[q][slot] = t + 1;
			}
			while (g_in_at[q][g_have[q] % RING] == g_have[q] + 1) g_have[q]++;
		}
		data += st->count * 0x30;
	}
	if (p->hash_tick && !(p->hash_tick % HASH_EVERY)) {
		u32 slot = (p->hash_tick / HASH_EVERY) % HASH_SLOTS;
		if (k->theirs[slot].tick != p->hash_tick) {
			k->theirs[slot].tick = p->hash_tick;
			k->theirs[slot].hash = p->hash;
			compare_hash(l, slot);
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
 * level starts (net_map_fetch, from Join's Play), into CustomLevels\<folder>\.
 * MAP_LIST names the host's folder. Every custom map is level 31 here, so a host on another map
 * than the one asked about (restarted on it, or another Oni at its address) would have its files
 * put in the wrong folder: they're taken only for the folder asked about, and the joiner gets
 * the host's map in its own folder instead (map_fetch_into) and mounts that one. */
#define MAP_PATH        112
#define L0_SET          0x10000u     /* MAP_LIST_REQ / MAP_REQ "level" for the host's level0 pair (see "level0 download") */
#define L0_SIG          0x10001u     /* ...for the pair's block lists (see "level0 sync") */
#define MAP_LIST_PAGE   11           /* (with the folder name the list stays under the 1500-byte MTU) */
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
/* folder: the host's map folder in CustomLevels ("" for level0's lists): the joiner takes the
 * files only for the folder it asked about (see map_download_to) */
typedef struct { PktHead h; u32 level, map, total, first, count; char folder[64]; MapFile f[MAP_LIST_PAGE]; } PktMapList;
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
	put_key(p, to);
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

/* The pieces q asks for of a file of size bytes open in f, each in a MAP_DATA (level as asked) */
static void send_chunks(HANDLE f, u32 size, const PktMapReq *q, const SOCKADDR_IN *to)
{
	static u8 data[MAP_REQ_MAX * MAP_CHUNK], out[sizeof(PktMapData) + MAP_CHUNK];
	PktMapData *p = (PktMapData *)out;
	u32 off, len, got = 0;

	if (q->chunk >= (size + MAP_CHUNK - 1) / MAP_CHUNK) return;
	off = q->chunk * MAP_CHUNK;
	len = q->count * MAP_CHUNK;
	if (len > size - off) len = size - off;
	SetFilePointer(f, (int)off, 0, 0 /* FILE_BEGIN */);
	if (!ReadFile(f, data, len, &got, 0)) return;
	for (u32 k = 0; k * MAP_CHUNK < got; k++) {
		u32 n = got - k * MAP_CHUNK;
		if (n > MAP_CHUNK) n = MAP_CHUNK;
		head(&p->h, PKT_MAP_DATA);
		p->level = q->level;
		p->file = q->file;
		p->chunk = q->chunk + k;
		p->size = n;
		memcpy(out + sizeof *p, data + k * MAP_CHUNK, n);
		send_to(out, sizeof *p + n, to);
	}
}

/* ---- level0 download ------------------------------------------------------------------------ *
 * level0 is one big file pair every PC must have the same of (an AE install whose XML patches
 * didn't run has another, see docs "Fresh Linux install refused by the host"). A joiner refused
 * for level0 alone uses the host's pair from AE\cache\<dat hash>-<raw hash>\ (level0_swap.c); when
 * it isn't there, it gets it from the host the way a map comes (map_download_to, the "level"
 * L0_SET in the requests): the host lists its two files (the pair it runs: GameDataFolder's, or
 * the cache copy it switched to itself) with the hashes it compares, and sends them in pieces;
 * the joiner reloads the level, downloads them in the loading popup into the cache folder (about
 * 172 MB once, then kept), and the same load swaps level 0 to them. */
static char g_l0_active[64];           /* the cache folder of the level0 this PC switched to ("": GameDataFolder's) */
static HANDLE g_l0_file = INVALID_HANDLE_VALUE;
static u32 g_l0_file_index = 0xffffffffu;

static void l0_serve(const u8 *buf, u32 n, const SOCKADDR_IN *to)
{
	static const char *const names[2] = { "level0_Final.dat", "level0_Final.raw" };
	static MapFile files[2];
	static char dir[64];
	const PktHead *h = (const PktHead *)buf;
	char path[160];

	if (!files[0].size || !str_ieq_n(dir, g_l0_active[0] ? g_l0_active : "GameDataFolder", sizeof dir)) {
		str_cpy(dir, sizeof dir, g_l0_active[0] ? g_l0_active : "GameDataFolder");
		if (g_l0_file != INVALID_HANDLE_VALUE) CloseHandle(g_l0_file);
		g_l0_file = INVALID_HANDLE_VALUE;
		g_l0_file_index = 0xffffffffu;
		for (int i = 0; i < 2; i++) {
			WIN32_FIND_DATAA fd;
			HANDLE f;
			fmt(path, "%s\\%s", dir, names[i]);
			str_cpy(files[i].path, MAP_PATH, names[i]);
			files[i].hash = g_files[2 + i];   /* what the handshake compares */
			files[i].size = 0;
			if ((f = FindFirstFileA(path, &fd)) != INVALID_HANDLE_VALUE) {
				files[i].size = fd.nFileSizeLow;
				FindClose(f);
			}
		}
	}
	if (!files[0].size || !files[1].size) return;
	if (h->type == PKT_MAP_LIST_REQ && n >= sizeof(PktMapListReq)) {
		const PktMapListReq *q = (const PktMapListReq *)buf;
		static u32 last_log;
		PktMapList p;
		if (!q->first && GetTickCount() - last_log > LOST_MS) {
			const u8 *ip = (const u8 *)&to->addr;
			log_msg("net: %d.%d.%d.%d:%d is downloading this PC's level0 (%s)", ip[0], ip[1], ip[2], ip[3], ntohs(to->port), dir);
			last_log = GetTickCount();
		}
		memset(&p, 0, sizeof p);
		head(&p.h, PKT_MAP_LIST);
		p.level = L0_SET;
		p.total = 2;
		p.first = q->first;
		for (u32 i = q->first; i < 2 && p.count < MAP_LIST_PAGE; i++) p.f[p.count++] = files[i];
		send_to(&p, sizeof p, to);
	} else if (h->type == PKT_MAP_REQ && n >= sizeof(PktMapReq)) {
		const PktMapReq *q = (const PktMapReq *)buf;
		if (q->file >= 2 || !q->count || q->count > MAP_REQ_MAX) return;
		if (g_l0_file == INVALID_HANDLE_VALUE || g_l0_file_index != q->file) {
			if (g_l0_file != INVALID_HANDLE_VALUE) CloseHandle(g_l0_file);
			fmt(path, "%s\\%s", dir, names[q->file]);
			g_l0_file = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 3, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
			g_l0_file_index = q->file;
			if (g_l0_file == INVALID_HANDLE_VALUE) log_msg("net: level0 download: can't open %s", path);
		}
		if (g_l0_file != INVALID_HANDLE_VALUE) send_chunks(g_l0_file, files[q->file].size, q, to);
	}
}

/* ---- level0 sync: only the differences ---------------------------------------------------- *
 * Two level0 versions share most of their bytes (a mod adds or changes some instances; the .raw's
 * textures and sounds mostly stay, though at other offsets). So the joiner gets the host's pair
 * the zsync way: the host lists, for each file, a weak rolling checksum and a strong one of each
 * ZS block (L0_SIG: two "files" in memory, block lists, sent like a map's files); the joiner rolls
 * over its own level0 files (GameDataFolder's and every version in AE\\cache) looking for those
 * blocks at any offset, copies the ones it has, and asks for the rest as byte ranges (PKT_L0_REQ,
 * up to ZS_PAIRS ranges of at most MAP_CHUNK bytes; PKT_L0_DATA each). The whole files' hashes are
 * checked at the end; anything going wrong falls back to the full download (L0_SET).
 * Measured 2026-11-08 on a modded level0 (12.1 + 185.5 MB) against the stock one: 128-byte blocks
 * leave 4.1 MB of the .dat to download (its offsets into the .raw change everywhere, so bigger
 * blocks rarely match), 1 KB blocks 28.7 MB of the .raw, the lists 2.2 MB: about 35 MB, not 197. */
#define ZS_MAGIC        0x535a4b41u  /* "AKZS" */
#define ZS_PAIRS        32
typedef struct { u32 magic, block, size, nblocks; } ZsHead;   /* then nblocks x {weak, strong} */
typedef struct { u32 off; u16 len, pad; } ZsRange;
typedef struct { PktHead h; u32 file, count; ZsRange r[ZS_PAIRS]; } PktL0Req;
typedef struct { PktHead h; u32 file, off, size; } PktL0Data;   /* size bytes follow */
static const u32 g_zs_block[2] = { 128, 1024 };   /* .dat, .raw */
static const char *const g_l0_names[2] = { "level0_Final.dat", "level0_Final.raw" };

/* rsync's weak checksum of n bytes: (a & 0xffff) | b << 16, rolled one byte on by zs_roll */
static void zs_weak(const u8 *p, u32 n, u32 *a, u32 *b)
{
	u32 x = 0, y = 0;
	for (u32 i = 0; i < n; i++) { x += p[i]; y += x; }
	*a = x;
	*b = y;
}

static u32 zs_strong(const u8 *p, u32 n)
{
	u32 h = 0x811c9dc5u;
	for (u32 i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
	return h ? h : 1;
}

/* the host's block lists, made on a thread of their own (197 MB read and hashed) */
static struct {
	volatile int state;                /* 0 none, 1 making, 2 ready, -1 failed */
	char dir[64];                      /* of the pair they're for */
	u8 *list[2];
	u32 size[2], hash[2];
} g_zs;

/* The block list of the file at path, read through buf (1 MB); its size in *size and, when hash
 * isn't 0, the file's hash_file in *hash. 0 when it can't be read */
static u8 *zs_list_file(const char *path, u32 block, u8 *buf, u32 *size, u32 *hash)
{
	u32 got, total, k = 0, h = 0x811c9dc5u;
	HANDLE f = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 3, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
	ZsHead *z;

	if (f == INVALID_HANDLE_VALUE) return 0;
	total = GetFileSize(f, 0);
	*size = sizeof(ZsHead) + (total + block - 1) / block * 8;
	if (!(z = VirtualAlloc(0, *size, 0x3000, 4))) { CloseHandle(f); return 0; }
	z->magic = ZS_MAGIC;
	z->block = block;
	z->size = total;
	z->nblocks = (total + block - 1) / block;
	u32 *e = (u32 *)(z + 1);
	while (ReadFile(f, buf, 1 << 20, &got, 0) && got) {   /* (1 MB holds whole blocks) */
		if (hash) h = hash_words(h, buf, got);           /* (hash_file reads 1 MB at a time too) */
		for (u32 off = 0; off < got && k < z->nblocks; off += block, k++) {
			u32 n = got - off < block ? got - off : block, a, b;
			zs_weak(buf + off, n, &a, &b);
			e[2 * k] = (a & 0xffff) | b << 16;
			e[2 * k + 1] = zs_strong(buf + off, n);
		}
	}
	CloseHandle(f);
	if (k != z->nblocks) { VirtualFree(z, 0, 0x8000); return 0; }
	if (hash) *hash = h;
	return (u8 *)z;
}

static u32 WINAPI zs_make(void *unused)
{
	u8 *buf = VirtualAlloc(0, 1 << 20, 0x3000, 4);
	int ok = buf != 0;
	for (int i = 0; i < 2 && ok; i++) {
		char path[160];
		fmt(path, "%s\\%s", g_zs.dir, g_l0_names[i]);
		if (!(g_zs.list[i] = zs_list_file(path, g_zs_block[i], buf, &g_zs.size[i], 0))) { ok = 0; break; }
		g_zs.hash[i] = hash_words(0x811c9dc5u, g_zs.list[i], g_zs.size[i]);   /* = hash_file of it */
	}
	if (buf) VirtualFree(buf, 0, 0x8000);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	g_zs.state = ok ? 2 : -1;
	return 0;
}

/* 1: the block lists of the pair in dir are ready; else they're being made (asked again) */
static int zs_ready(const char *dir)
{
	if (g_zs.state == 1) return 0;
	if (g_zs.state != 0 && str_ieq_n(g_zs.dir, dir, sizeof g_zs.dir)) return g_zs.state == 2;
	for (int i = 0; i < 2; i++)
		if (g_zs.list[i]) VirtualFree(g_zs.list[i], 0, 0x8000), g_zs.list[i] = 0;
	str_cpy(g_zs.dir, sizeof g_zs.dir, dir);
	g_zs.state = 1;
	if (!CreateThread(0, 0, zs_make, 0, 0, 0)) g_zs.state = -1;
	else log_msg("net: level0 sync: making the block lists of %s for a joiner", dir);
	return 0;
}

/* L0_SIG: the two block lists, as files */
static void zs_serve(const u8 *buf, u32 n, const SOCKADDR_IN *to, const char *dir)
{
	const PktHead *h = (const PktHead *)buf;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	if (!zs_ready(dir)) return;   /* the joiner asks again (it waits up to LOST_MS) */
	if (h->type == PKT_MAP_LIST_REQ && n >= sizeof(PktMapListReq)) {
		const PktMapListReq *q = (const PktMapListReq *)buf;
		PktMapList p;
		memset(&p, 0, sizeof p);
		head(&p.h, PKT_MAP_LIST);
		p.level = L0_SIG;
		p.total = 2;
		p.first = q->first;
		for (u32 i = q->first; i < 2 && p.count < MAP_LIST_PAGE; i++, p.count++) {
			fmt(p.f[p.count].path, "%s.zs", g_l0_names[i]);
			p.f[p.count].size = g_zs.size[i];
			p.f[p.count].hash = g_zs.hash[i];
		}
		send_to(&p, sizeof p, to);
	} else if (h->type == PKT_MAP_REQ && n >= sizeof(PktMapReq)) {
		static u8 out[sizeof(PktMapData) + MAP_CHUNK];
		const PktMapReq *q = (const PktMapReq *)buf;
		PktMapData *p = (PktMapData *)out;
		if (q->file >= 2 || !q->count || q->count > MAP_REQ_MAX) return;
		for (u32 c = q->chunk; c < q->chunk + q->count && c * MAP_CHUNK < g_zs.size[q->file]; c++) {
			u32 left = g_zs.size[q->file] - c * MAP_CHUNK;
			head(&p->h, PKT_MAP_DATA);
			p->level = L0_SIG;
			p->file = q->file;
			p->chunk = c;
			p->size = left < MAP_CHUNK ? left : MAP_CHUNK;
			memcpy(out + sizeof *p, g_zs.list[q->file] + c * MAP_CHUNK, p->size);
			send_to(out, sizeof *p + p->size, to);
		}
	}
}

/* PKT_L0_REQ: byte ranges of the pair this PC runs */
static void zs_serve_ranges(const PktL0Req *q, const SOCKADDR_IN *to)
{
	static u8 out[sizeof(PktL0Data) + MAP_CHUNK];
	PktL0Data *p = (PktL0Data *)out;
	char path[160];
	const char *dir = g_l0_active[0] ? g_l0_active : "GameDataFolder";
	u32 size, got;

	if (q->file >= 2 || q->count > ZS_PAIRS) return;
	if (g_l0_file == INVALID_HANDLE_VALUE || g_l0_file_index != q->file) {
		if (g_l0_file != INVALID_HANDLE_VALUE) CloseHandle(g_l0_file);
		fmt(path, "%s\\%s", dir, g_l0_names[q->file]);
		g_l0_file = CreateFileA(path, 0x80000000, 3, 0, 3, 0x80, 0);
		g_l0_file_index = q->file;
	}
	if (g_l0_file == INVALID_HANDLE_VALUE) return;
	size = GetFileSize(g_l0_file, 0);
	for (u32 i = 0; i < q->count; i++) {
		const ZsRange *r = &q->r[i];
		if (!r->len || r->len > MAP_CHUNK || r->off >= size || r->len > size - r->off) continue;
		SetFilePointer(g_l0_file, (int)r->off, 0, 0);
		if (!ReadFile(g_l0_file, out + sizeof *p, r->len, &got, 0) || got != r->len) continue;
		head(&p->h, PKT_L0_DATA);
		p->file = q->file;
		p->off = r->off;
		p->size = r->len;
		send_to(out, sizeof *p + r->len, to);
	}
}

static void map_serve(const u8 *buf, u32 n, const SOCKADDR_IN *to)
{
	const PktHead *h = (const PktHead *)buf;
	char path[1024];

	if (n >= sizeof *h + 4 && *(const u32 *)(buf + sizeof *h) == L0_SET) {   /* (both requests start with the level) */
		l0_serve(buf, n, to);
		return;
	}
	if (n >= sizeof *h + 4 && *(const u32 *)(buf + sizeof *h) == L0_SIG) {
		zs_serve(buf, n, to, g_l0_active[0] ? g_l0_active : "GameDataFolder");
		return;
	}
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
		str_cpy(p.folder, sizeof p.folder, custom_game_level_folder(g_level));
		p.total = g_nserve;
		p.first = q->first;
		for (u32 i = q->first; i < g_nserve && p.count < MAP_LIST_PAGE; i++) p.f[p.count++] = g_serve[i];
		send_to(&p, sizeof p, to);
	} else if (h->type == PKT_MAP_REQ && n >= sizeof(PktMapReq)) {
		const PktMapReq *q = (const PktMapReq *)buf;
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
		send_chunks(g_serve_file, m->size, q, to);
	}
}

/* -- The joiner's side -- */

static u32 g_dl_level;                 /* the level whose load downloads the host's files first */
static u32 g_dl_declined;              /* its download failed or was cancelled: not again */
static WMtWindow *g_dl_lines[2];       /* the two lines under the loading popup's bar */
static WMtWindow *g_dl_lines_of;       /* the popup they were made in (0: none made since the last one closed) */
static u32 g_dl_lines_made;            /* pairs made so far (a new pair may sit where an old one was) */
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
	if (seeds) {
		lockstep_set_seeds(g_match_seed, g_match_local_seed);
		players_set_spawn_seed(g_match_seed);   /* every PC starts the players at the same points */
	}
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
	g_load_step = "Loading the host's map";
	reload_level(level, 0);
	return 1;
}

/* From the host's LOBBY refusal for level0 alone: when AE\cache has the host's pair, reload the
 * level, and its load swaps level 0 first (level0_swap.c). Tried once per host pair. */
static u32 g_l0_want[2], g_l0_tried[2], g_l0_pending, g_l0_fetch;
static u32 g_l0_installed[2];          /* GameDataFolder's pair, while this PC runs a host's (g_l0_active) */
static int g_l0_restart_said;
/* Swaps per Oni session: each keeps the old level 0 loaded (~172 MB of address space). A second
 * one used to draw everything white: the run-long dynamic file's pixel memory ran out under the
 * new level 0's fonts (level0_swap.c now enlarges it). After the last, Oni stays on that pair
 * until it restarts. */
#define L0_SWAPS_MAX    3   /* level0_swap.c's GHOSTS_MAX */
static char g_l0_dir[64];

/* This PC's own level0, back after a host's: not GameDataFolder's files again (the first level 0
 * stays loaded as a ghost with them open, and a second load of the same path drew every texture
 * white), but the same pair under cache\\<its hashes>\\, hard links to the installed files (a copy
 * where links don't work), which nothing ever writes to. 1: g_l0_dir names it */
static int l0_own_dir(void)
{
	static const char *const names[2] = { "level0_Final.dat", "level0_Final.raw" };
	char from[128], to[128];

	fmt(g_l0_dir, "cache\\%08x-%08x", g_l0_installed[0], g_l0_installed[1]);
	CreateDirectoryA("cache", 0);
	CreateDirectoryA(g_l0_dir, 0);
	for (int i = 0; i < 2; i++) {
		fmt(from, "GameDataFolder\\%s", names[i]);
		fmt(to, "%s\\%s", g_l0_dir, names[i]);
		if (hash_file(to) == g_l0_installed[i]) continue;
		DeleteFileA(to);
		if (!CreateHardLinkA(to, from, 0) && !CopyFileA(from, to, 0)) {
			log_msg("net: can't put this PC's own level0 in %s (error %d)", g_l0_dir, GetLastError());
			return 0;
		}
		if (hash_file(to) != g_l0_installed[i]) {
			log_msg("net: %s isn't this PC's own level0", to);
			return 0;
		}
	}
	return 1;
}

/* The host's level0 (want) for the next load: 2 back to this PC's own, 1 the host's pair is whole
 * in its cache folder (both: g_l0_dir and g_l0_want set, g_l0_pending is the caller's), 0 not here
 * (downloaded in the reload, g_l0_fetch), -1 not possible */
static int level0_swap_ready(const u32 *want)
{
	char path[128];

	fmt(g_l0_dir, "cache\\%08x-%08x", want[0], want[1]);
	g_l0_want[0] = want[0];
	g_l0_want[1] = want[1];
	if (g_l0_active[0] && want[0] == g_l0_installed[0] && want[1] == g_l0_installed[1])
		return l0_own_dir() ? 2 : -1;   /* this PC runs another host's level0, and this host has the installed one */
	fmt(path, "%s\\level0_Final.dat", g_l0_dir);
	if (hash_file(path) != want[0]) return 0;
	fmt(path, "%s\\level0_Final.raw", g_l0_dir);
	return hash_file(path) == want[1] ? 1 : 0;
}

static int level0_swap_start(const u32 *want)
{
	u32 level = g_level;
	int r;

	if (g_l0_tried[0] == want[0] && g_l0_tried[1] == want[1]) return 0;
	g_l0_tried[0] = want[0];
	g_l0_tried[1] = want[1];
	fmt(g_l0_dir, "cache\\%08x-%08x", want[0], want[1]);
	if (level0_swaps() >= L0_SWAPS_MAX) {
		log_msg("net: this host's level0 (%08x %08x) isn't the one this Oni switched to: restart Oni to join it", want[0], want[1]);
		return 0;
	}
	r = level0_swap_ready(want);
	if (r < 0) return 0;
	if (r == 2) {
		log_msg("net: the host has this PC's own level0: reloading level %d with it", level);
		g_l0_pending = 1;
		session_end("switching back to this PC's level0");
		g_load_step = "Switching back to your own level0";
		reload_level(level, 0);
		return 1;
	}
	if (r == 1) {
		log_msg("net: the host's level0 is in %s: reloading level %d with it", g_l0_dir, level);
		g_l0_pending = 1;
		session_end("switching to the host's level0");
		g_load_step = "Switching to the host's level0";
		reload_level(level, 0);
		return 1;
	}
	/* not here (or not whole): from the host, in the reload's loading popup (see "level0 download") */
	log_msg("net: the host's level0 (%08x %08x) isn't in %s: reloading level %d to download it", want[0], want[1], g_l0_dir, level);
	g_l0_fetch = 1;
	session_end("downloading the host's level0");
	g_load_step = "Getting the host's level0";
	reload_level(level, 0);
	return 1;
}

/* Join's Play: the Join list says which level0 the host runs (l0). When this PC has it whole in
 * its cache (or it's this PC's own again), the first load opens it at once, as the reload for the
 * host's refusal would have: one level load fewer for every Windows AE joiner of a Linux host
 * (2026-11-13). Not here: the host's refusal downloads it in a reload, as before; and a list
 * that was wrong only costs that reload, the handshake compares the files as always. */
void net_join_level0(const u32 *l0)
{
	int r;

	if (g_mode != NET_JOIN || (!l0[0] && !l0[1])) return;
	if (l0[0] == g_files[2] && l0[1] == g_files[3]) return;   /* on it already */
	if (level0_swaps() >= L0_SWAPS_MAX) return;   /* (the refusal says why) */
	r = level0_swap_ready(l0);
	if (r <= 0) return;
	g_l0_tried[0] = l0[0];
	g_l0_tried[1] = l0[1];
	g_l0_pending = 1;
	if (r == 2) log_msg("net: the host has this PC's own level0: the level loads with it");
	else log_msg("net: the host's level0 is in %s: the level loads with it", g_l0_dir);
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
		if (stun_packet(buf, (u32)n, &from) || helper_packet(g_sock, buf, (u32)n, &from)) continue;
		const PktHead *h = (const PktHead *)buf;
		if ((u32)n < sizeof *h || h->magic != MAGIC || h->proto != PROTO ||
		    from.addr != g_link[0].addr.addr || from.port != g_link[0].addr.port) {
			stranger(&from, "not the host");
			continue;
		}
		g_npackets_recv++;
		if (h->type == PKT_COOKIE) {   /* the first request had no key: ask again with it */
			if (joiner_cookie(h->key)) g_dl_rekeyed = 1;
			continue;
		}
		if (!host_keyed(h)) {
			stranger(&from, "the host's address without this PC's key");
			continue;
		}
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

	while (PeekMessageW(&m, 0, 0, 0, 1 /* PM_REMOVE */)) {   /* W, as Oni's own loop now (text.c) */
		if (m.msg == 0x12 /* WM_QUIT */) {
			g_dl_quit = (int)m.wp | 0x10000;
			return 1;
		}
		TranslateMessage(&m);
		DispatchMessageW(&m);
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

	if (d && g_dl_lines_of == d && g_dl_lines[0]) {   /* already in this popup (the join's lines, see level_load) */
		for (int i = 0; i < 2; i++)
			if (g_dl_lines[i]) WMrWindow_SetVisible(g_dl_lines[i], 1);
		return;
	}
	g_dl_lines[0] = g_dl_lines[1] = 0;
	g_dl_lines_of = d;
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
	g_dl_lines_made++;
}

/* The popup grows (never shrinks) to fit the lines' text: a line too long for it wrapped over the
 * next one. The box, its full-size outline, the bar and the lines widen together and the box
 * stays centered; only past the screen's width is a line cut ("..."). */
#define DL_PARENT(w)            (*(WMtWindow **)((u8 *)(w) + 8))
static void dl_fit(const char *a, const char *b)
{
	WMtWindow *d = ONgLoadingDialog, *bar, *parent;
	int need, grow, max_w;

	if (!d || !g_dl_lines[0]) return;
	need = ui_text_width(a) > ui_text_width(b) ? ui_text_width(a) : ui_text_width(b);
	need += 6;   /* (a little room: the font's last glyph overhangs its advance) */
	if (need <= WIN_W(g_dl_lines[0])) return;
	parent = DL_PARENT(d);
	max_w = parent ? WIN_W(parent) - 20 : 620;
	grow = need - WIN_W(g_dl_lines[0]);
	if (WIN_W(d) + grow > max_w) grow = max_w - WIN_W(d);
	if (grow <= 0) return;
	for (WMtWindow *w = WIN_FIRSTCHILD(d); w; w = WIN_NEXT(w))   /* the outline (the box's own size) */
		if (WIN_W(w) == WIN_W(d)) ui_set_frame(w, WIN_X(w), WIN_Y(w), WIN_W(w) + grow, WIN_H(w));
	if ((bar = WMrDialog_GetItemByID(d, 100))) ui_set_frame(bar, WIN_X(bar), WIN_Y(bar), WIN_W(bar) + grow, WIN_H(bar));
	for (int i = 0; i < 2; i++)
		if (g_dl_lines[i]) ui_set_frame(g_dl_lines[i], WIN_X(g_dl_lines[i]), WIN_Y(g_dl_lines[i]), WIN_W(g_dl_lines[i]) + grow, WIN_H(g_dl_lines[i]));
	ui_set_frame(d, WIN_X(d) - grow / 2, WIN_Y(d), WIN_W(d) + grow, WIN_H(d));
}

/* The lines' text and the bar, drawn by the next frame */
static void dl_set(const char *a, const char *b, u32 percent)
{
	WMtWindow *d = ONgLoadingDialog;

	char fit[160];
	dl_fit(a, b);
	if (g_dl_lines[0]) ui_fit(fit, sizeof fit, a, WIN_W(g_dl_lines[0])), WMrWindow_SetTitle(g_dl_lines[0], fit, 0x100);
	if (g_dl_lines[1]) ui_fit(fit, sizeof fit, b, WIN_W(g_dl_lines[1])), WMrWindow_SetTitle(g_dl_lines[1], fit, 0x100);
	if (d) WMrMessage_Send(d, LDcMessage_Progress, percent, 0);
}

static void dl_draw(const char *a, const char *b, u32 percent)
{
	dl_set(a, b, percent);
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
static char g_dl_host_folder[64];       /* ...when it was that the host is on another map: that map's folder */

/* A folder name in CustomLevels the host gave: one name, nothing that leads out of CustomLevels */
static int folder_name_ok(const char *folder)
{
	if (!safe_path(folder) || str_len(folder) >= 64) return 0;
	for (const char *c = folder; *c; c++)
		if (*c == '\\') return 0;
	return 1;
}

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
	const char *why = 0, *what = level == L0_SET ? "level0" : level == L0_SIG ? "level0 block list" : "map";
	int ok = 0, cur = -1;

	if (!g_link[0].used) return 0;
	g_dl_host_folder[0] = 0;
	theirs = VirtualAlloc(0, 2 * MAP_MAX_FILES * (sizeof(MapFile) + 1), 0x3000, 4);
	if (!theirs) return 0;
	mine = theirs + MAP_MAX_FILES;
	need = (u8 *)(mine + MAP_MAX_FILES);
	moved = need + MAP_MAX_FILES;
	/* (level0 never comes near GameDataFolder: it's downloaded and kept in AE\\cache) */
	fmt(path, level >= L0_SET ? "cache\\.download\\%s\\" : "GameDataFolder\\CustomLevels.download\\%s\\", folder);
	if (!GetFullPathNameA(path, sizeof stage, stage, 0)) str_cpy(stage, sizeof stage, path);
	fmt(path, level >= L0_SET ? "cache\\.old\\%s\\" : "GameDataFolder\\CustomLevels.backup\\%s\\", folder);
	if (!GetFullPathNameA(path, sizeof backup, backup, 0)) str_cpy(backup, sizeof backup, path);
	dl_lines_create();
	log_msg("net: %s download: asking the host for its list of %s's files", what, folder);

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
			send_to_host(&q, sizeof q);
			last_req = now ? now : 1;
		}
		while ((n = dl_receive(buf, sizeof buf))) {
			const PktMapList *p = (const PktMapList *)buf;
			if (p->h.type != PKT_MAP_LIST || n < sizeof *p || p->level != level || p->first != ntheirs ||
			    p->count > MAP_LIST_PAGE || p->total > MAP_MAX_FILES || p->first + p->count > p->total) continue;
			/* Every custom map is the same level number, so the list says whose files they are: a host
			 * that's on another map now (it restarted on one, or another Oni answers at its address)
			 * would otherwise put that map into this one's folder, and Custom Game would list two maps
			 * of one name (the Windows joiner, 2026-11-12: PowerPlant in CustomLevels\Condemned) */
			if (level < L0_SET && !str_ieq_n(p->folder, folder, sizeof p->folder)) {
				char theirs_name[64];
				str_cpy(theirs_name, sizeof theirs_name, p->folder);
				if (folder_name_ok(theirs_name)) str_cpy(g_dl_host_folder, sizeof g_dl_host_folder, theirs_name);
				log_msg("net: map download: the host is on its map \"%s\" now, not %s: nothing goes into %s", theirs_name, folder, dir);
				why = "the host is on another map now";
				goto done;
			}
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
		if (g_dl_rekeyed) {   /* the request that came back with the key: again, with it */
			g_dl_rekeyed = 0;
			last_req = 0;
			continue;
		}
		if (now - last_draw >= DL_DRAW_MS) {
			dl_draw("Connecting to the host...", "", 10);
			last_draw = now;
		} else Sleep(1);
	}

	/* 2. what differs: files to fetch, and files the host doesn't have */
	if (!list_tree(dir, "", 0, mine, &nmine)) log_msg("net: %s download: couldn't list every file in %s", what, dir);
	for (u32 i = 0; i < ntheirs; i++) {
		int k = find_file(mine, nmine, theirs[i].path);
		need[i] = k < 0 || mine[k].size != theirs[i].size || mine[k].hash != theirs[i].hash;
		if (!need[i]) continue;
		nneed++;
		need_bytes += theirs[i].size;
		if (theirs[i].size > biggest) biggest = theirs[i].size;
	}
	for (u32 i = 0; i < nmine; i++) nextra += find_file(theirs, ntheirs, mine[i].path) < 0;
	log_msg("net: %s download: the host has %d files; %d to download (%u bytes), %d here that it doesn't have", what,
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
			if (g_dl_rekeyed) {   /* (only if the host restarted meanwhile) every piece asked for again */
				g_dl_rekeyed = 0;
				memset(g_req_chunk, 0, sizeof g_req_chunk);
			}
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
					send_to_host(&q, sizeof q);
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
		log_msg("net: %s download: got %s (%u bytes)", what, m->path, m->size);
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
			log_msg("net: %s download: the map's folder is half updated; its old files are in %s", what, backup);
			why = "can't move the downloaded files into the map's folder";
			goto done;
		}
	}
	{
		char tail[32] = "";
		if (level < L0_SET) fmt(tail, "; the host's map is %08x", their_map);
		RemoveDirectoryA(stage);   /* (only when it's empty: a map's subfolders stay) */
		log_msg("net: %s download: done, %u bytes in %u ms%s%s%s", what, bytes_all, GetTickCount() - began,
		        nmine ? "; old files in " : "", nmine ? backup : "", tail);
	}
	ok = 1;

done:
	if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
	g_dl_why = why ? why : "?";
	if (!ok) {
		log_msg("net: %s download stopped: %s; this PC keeps its own version", what, g_dl_why);
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

/* The host's map into CustomLevels\<folder> (made when it isn't there). When the host turns out
 * to be on another map, that map's folder instead (only the files that differ, nothing when this
 * PC has it already), and folder is then that one's name. A folder this made stays only with a
 * map in it. */
static int map_fetch_into(u32 level, char *folder, u32 size)
{
	char dir[MAX_PATH];

	for (int pass = 0; pass < 2; pass++) {
		if (!custom_game_folder_path(folder, dir, sizeof dir)) { g_dl_why = "the map's folder path is too long"; return 0; }
		if (map_download_to(level, dir, folder)) return 1;
		RemoveDirectoryA(dir);   /* (only an empty one: one made here that nothing came into) */
		if (pass || !g_dl_host_folder[0]) return 0;
		log_msg("net: the host is on %s now, not %s: getting that map instead", g_dl_host_folder, folder);
		str_cpy(folder, size, g_dl_host_folder);
	}
	return 0;
}

/* The mounted level's folder, replaced by the host's version, mounted again; or the host's other
 * map, when it's on another one now, mounted in its place */
static int map_download(u32 level)
{
	const char *folder = custom_game_level_folder(level);
	char want[64];

	if (!custom_game_level_dir(level) || !folder) return 0;
	str_cpy(want, sizeof want, folder);
	if (!map_fetch_into(level, want, sizeof want)) return 0;
	if (!custom_game_mount_folder(level, want)) log_msg("net: map download: couldn't mount the new files");
	g_map_hash = hash_map(level);
	log_msg("net: map download: the map's files are now %08x", g_map_hash);
	g_problem = 0;
	return 1;
}

/* ---- level0 sync, the joiner's side (see "level0 sync") ------------------------------------ */
#define ZS_SCAN         (4u << 20)   /* bytes of a source file read at once */
#define ZS_PIECES_MAX   (256u << 10) /* ranges asked for, at most (more: the full download is as good) */

typedef struct { u32 off, len; } ZsPiece;

/* The list's weak sums in a hash table for zs_scan (the short last block is always downloaded);
 * have[] all 0. 0 when there's no memory (what was made is freed with zs_table_free) */
static int zs_table(const ZsHead *z, const u32 *e, int **headt, int **next, u8 **have, u32 *mask)
{
	u32 m;

	for (m = 1; m < z->nblocks && m < (1u << 20); m <<= 1) {}
	*mask = --m;
	*headt = VirtualAlloc(0, (m + 1) * 4, 0x3000, 4);
	*next = VirtualAlloc(0, z->nblocks * 4, 0x3000, 4);
	*have = VirtualAlloc(0, z->nblocks, 0x3000, 4);
	if (!*headt || !*next || !*have) return 0;
	memset(*headt, 0xff, (m + 1) * 4);
	for (u32 k = 0; k < z->nblocks; k++) {
		if ((k + 1) * z->block > z->size) { (*next)[k] = -1; continue; }
		(*next)[k] = (*headt)[e[2 * k] & m];
		(*headt)[e[2 * k] & m] = (int)k;
	}
	return 1;
}

static void zs_table_free(int **headt, int **next, u8 **have)
{
	if (*have) VirtualFree(*have, 0, 0x8000);
	if (*next) VirtualFree(*next, 0, 0x8000);
	if (*headt) VirtualFree(*headt, 0, 0x8000);
	*have = 0;
	*next = 0;
	*headt = 0;
}

/* The blocks not in have[] as the ranges the joiner asks for (consecutive blocks joined up to
 * MAP_CHUNK bytes), their bytes in *need. 0 when there would be more than ZS_PIECES_MAX: the
 * full download it is */
static int zs_pieces(const ZsHead *z, const u8 *have, ZsPiece *piece, u32 *npiece, u32 *need)
{
	*npiece = *need = 0;
	for (u32 k = 0; k < z->nblocks; k++) {
		if (have[k]) continue;
		u32 off = k * z->block, end = off + z->block > z->size ? z->size : off + z->block, n = *npiece;
		if (n && piece[n - 1].off + piece[n - 1].len == off && piece[n - 1].len + (end - off) <= MAP_CHUNK)
			piece[n - 1].len += end - off;
		else if (n < ZS_PIECES_MAX) {
			piece[n].off = off;
			piece[n].len = end - off;
			(*npiece)++;
		} else return 0;
		*need += end - off;
	}
	return 1;
}

/* Rolls over one of this PC's files, copying every block of the host's list it has (any offset)
 * into out (INVALID_HANDLE_VALUE: only marks them); have[] marks them. -1: cancelled (asked
 * every ZS_SCAN bytes) */
static int zs_scan(const char *path, const ZsHead *z, const u32 *e, int *headt, int *next, u32 mask,
                   u8 *have, u32 *nhave, HANDLE out, u8 *buf, int (*cancelled)(void))
{
	u32 B = z->block, avail = 0, p = 0, a = 0, b = 0, got, done = 0, last_pump = 0;
	int valid = 0;
	HANDLE f = CreateFileA(path, 0x80000000, 3, 0, 3, 0x80, 0);

	if (f == INVALID_HANDLE_VALUE) return 0;
	for (;;) {
		/* the bytes not looked at yet to the front, then more of the file after them */
		if (p) {
			memcpy(buf, buf + p, avail - p);   /* (forward copy: the regions may overlap, dst < src) */
			avail -= p;
			p = 0;
		}
		if (!ReadFile(f, buf + avail, ZS_SCAN + B - avail, &got, 0)) got = 0;
		avail += got;
		done += got;
		if (avail < B) break;
		if (done - last_pump >= ZS_SCAN) {
			last_pump = done;
			if (cancelled()) { CloseHandle(f); return -1; }
		}
		while (p + B <= avail) {
			int matched = 0;
			if (!valid) { zs_weak(buf + p, B, &a, &b); valid = 1; }
			u32 w = (a & 0xffff) | b << 16, strong = 0;
			/* blocks found leave their chain, so a long run of one content (zeros) isn't walked again */
			for (int *link = (int *)&headt[w & mask], k; (k = *link) >= 0;) {
				if (have[k]) { *link = next[k]; continue; }
				if (e[2 * k] != w || (strong ? strong : (strong = zs_strong(buf + p, B))) != e[2 * k + 1]) { link = &next[k]; continue; }
				u32 wr;
				if (out != INVALID_HANDLE_VALUE) {
					SetFilePointer(out, (int)(k * B), 0, 0);
					WriteFile(out, buf + p, B, &wr, 0);
				}
				have[k] = 1;
				(*nhave)++;
				matched = 1;
				*link = next[k];
			}
			if (*nhave == z->nblocks) break;
			if (matched) {   /* the next block likely follows on */
				p += B;
				valid = 0;
				continue;
			}
			if (p + B == avail) break;   /* the next byte isn't read yet */
			{
				u32 o = buf[p], n = buf[p + B];
				a += n - o;
				b += a - B * o;
			}
			p++;
		}
		if (*nhave == z->nblocks || !got) break;
	}
	CloseHandle(f);
	return 0;
}

/* The host's pair by its differences into dir (cache\<dat>-<raw>, relative), 1 when it's there.
 * 0: try the full download; -1: cancelled */
static int level0_sync(const char *dir)
{
	static u8 rbuf[2048];
	char folder[48], sigdir[MAX_PATH], rel[96], stage[MAX_PATH], path[MAX_PATH], to[MAX_PATH], a[128], b2[128], s1[32], s2[32], s3[32];
	u8 *list = 0, *have = 0, *buf = 0, *pgot = 0;
	int *headt = 0, *next = 0, result = 0;
	ZsPiece *piece = 0;
	HANDLE out = INVALID_HANDLE_VALUE;
	u32 began = GetTickCount(), fetched = 0, to_fetch_all = 0, full = 0;

	fmt(folder, "level0-%08x-%08x", g_l0_want[0], g_l0_want[1]);
	fmt(rel, "cache\\.sig\\%s\\", folder);
	if (!GetFullPathNameA(rel, sizeof sigdir, sigdir, 0)) return 0;
	{   /* 1. the block lists, like a map's files */
		char sigfolder[64];
		fmt(sigfolder, "%s-lists", folder);
		if (!map_download_to(L0_SIG, sigdir, sigfolder)) return g_dl_why && str_ieq_n(g_dl_why, "cancelled", 10) ? -1 : 0;
	}
	fmt(rel, "cache\\.download\\%s\\", folder);
	if (!GetFullPathNameA(rel, sizeof stage, stage, 0)) return 0;
	buf = VirtualAlloc(0, ZS_SCAN + 4096, 0x3000, 4);
	piece = VirtualAlloc(0, ZS_PIECES_MAX * sizeof *piece, 0x3000, 4);
	if (!buf || !piece) goto done;
	dl_lines_create();

	for (int fi = 0; fi < 2; fi++) {
		HANDLE lf;
		u32 lsize, got, nhave = 0, mask, npiece = 0, need_bytes = 0;
		const ZsHead *z;
		const u32 *e;

		/* the list */
		fmt(path, "%s%s.zs", sigdir, g_l0_names[fi]);
		lf = CreateFileA(path, 0x80000000, 3, 0, 3, 0x80, 0);
		if (lf == INVALID_HANDLE_VALUE) goto done;
		lsize = GetFileSize(lf, 0);
		list = VirtualAlloc(0, lsize + 4, 0x3000, 4);
		if (!list || !ReadFile(lf, list, lsize, &got, 0) || got != lsize) { CloseHandle(lf); goto done; }
		CloseHandle(lf);
		z = (const ZsHead *)list;
		e = (const u32 *)(z + 1);
		if (lsize < sizeof *z || z->magic != ZS_MAGIC || z->block != g_zs_block[fi] || !z->size ||
		    z->nblocks != (z->size + z->block - 1) / z->block || lsize != sizeof *z + z->nblocks * 8) {
			log_msg("net: level0 sync: the host's block list of %s is damaged", g_l0_names[fi]);
			goto done;
		}
		/* its weak sums in a hash table */
		if (!zs_table(z, e, &headt, &next, &have, &mask)) goto done;

		/* 2. what this PC has: its own file, then every version in the cache */
		fmt(path, "%s%s", stage, g_l0_names[fi]);
		make_dirs(path);
		out = CreateFileA(path, 0xc0000000 /* GENERIC_READ | GENERIC_WRITE */, 1, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
		if (out == INVALID_HANDLE_VALUE) goto done;
		dl_draw("Looking for the host's level0 in your files...", "", 10);
		fmt(path, "GameDataFolder\\%s", g_l0_names[fi]);
		if (zs_scan(path, z, e, headt, next, mask, have, &nhave, out, buf, dl_cancelled) < 0) { result = -1; goto done; }
		{
			WIN32_FIND_DATAA fd;
			HANDLE fh = FindFirstFileA("cache\\*", &fd);
			if (fh != INVALID_HANDLE_VALUE) {
				do {
					if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.' || nhave == z->nblocks) continue;
					fmt(path, "cache\\%s", fd.cFileName);
					if (str_ieq_n(path, dir, sizeof path)) continue;
					fmt(path, "cache\\%s\\%s", fd.cFileName, g_l0_names[fi]);
					if (zs_scan(path, z, e, headt, next, mask, have, &nhave, out, buf, dl_cancelled) < 0) { FindClose(fh); result = -1; goto done; }
				} while (FindNextFileA(fh, &fd));
				FindClose(fh);
			}
		}
		/* 3. the rest from the host, as ranges of at most MAP_CHUNK bytes */
		if (!zs_pieces(z, have, piece, &npiece, &need_bytes)) {
			log_msg("net: level0 sync: too much of %s differs, the full download it is", g_l0_names[fi]);
			goto done;
		}
		log_msg("net: level0 sync: %s: %u of %u blocks of %u bytes found in this PC's files; %u bytes to download in %u pieces",
		        g_l0_names[fi], nhave, z->nblocks, z->block, need_bytes, npiece);
		to_fetch_all += need_bytes;
		full += z->size;
		pgot = VirtualAlloc(0, npiece + 1, 0x3000, 4);
		if (!pgot) goto done;
		{
			u32 lo = 0, win = 64, grown_at = 0, last_loss = 0, last_draw = 0, last_heard = GetTickCount(), done_bytes = 0, left = npiece;
			u32 *asked = VirtualAlloc(0, (npiece + 1) * 4, 0x3000, 4);
			if (!asked) goto done;
			while (left) {
				u32 now = GetTickCount(), n;
				int heard = 0, lost = 0;
				if (dl_cancelled()) { VirtualFree(asked, 0, 0x8000); result = -1; goto done; }
				if (now - last_heard > LOST_MS) { VirtualFree(asked, 0, 0x8000); log_msg("net: level0 sync: the host stopped answering"); goto done; }
				while ((n = dl_receive(rbuf, sizeof rbuf))) {
					const PktL0Data *d = (const PktL0Data *)rbuf;
					u32 lo2 = 0, hi = npiece, w;
					if (d->h.type != PKT_L0_DATA || n < sizeof *d || d->file != (u32)fi || n < sizeof *d + d->size) continue;
					while (lo2 < hi) { u32 m = (lo2 + hi) / 2; if (piece[m].off < d->off) lo2 = m + 1; else hi = m; }
					if (lo2 >= npiece || piece[lo2].off != d->off || piece[lo2].len != d->size || pgot[lo2]) continue;
					SetFilePointer(out, (int)d->off, 0, 0);
					if (!WriteFile(out, rbuf + sizeof *d, d->size, &w, 0) || w != d->size) { VirtualFree(asked, 0, 0x8000); goto done; }
					pgot[lo2] = 1;
					left--;
					done_bytes += d->size;
					fetched += d->size;
					heard = 1;
				}
				if (heard) last_heard = now;
				if (g_dl_rekeyed) { g_dl_rekeyed = 0; memset(asked, 0, (npiece + 1) * 4); }
				while (lo < npiece && pgot[lo]) lo++;
				if (lo - grown_at >= win) { win = win + 32 > DL_WINDOW_MAX ? DL_WINDOW_MAX : win + 32; grown_at = lo; }
				{
					PktL0Req q;
					u32 end = lo + win < npiece ? lo + win : npiece;
					memset(&q, 0, sizeof q);
					for (u32 c = lo; c < end; c++) {
						if (pgot[c] || (asked[c] && now - asked[c] < DL_RTO_MS)) continue;
						if (asked[c]) lost = 1;
						asked[c] = now ? now : 1;
						q.r[q.count].off = piece[c].off;
						q.r[q.count].len = (u16)piece[c].len;
						if (++q.count == ZS_PAIRS) {
							head(&q.h, PKT_L0_REQ);
							q.file = (u32)fi;
							send_to_host(&q, sizeof q);
							q.count = 0;
						}
					}
					if (q.count) {
						head(&q.h, PKT_L0_REQ);
						q.file = (u32)fi;
						send_to_host(&q, sizeof q);
					}
				}
				if (lost && now - last_loss >= DL_RTO_MS) { win = win / 2 < DL_WINDOW_MIN ? DL_WINDOW_MIN : win / 2; grown_at = lo; last_loss = now; }
				if (now - last_draw >= DL_DRAW_MS) {
					u32 ms = now - began ? now - began : 1;
					size_text(s1, done_bytes, need_bytes);
					size_text(s2, need_bytes, need_bytes);
					speed_text(s3, fetched / ms * 1000 + fetched % ms * 1000 / ms);
					fmt(a, "Downloading the host's level0 (%d/2): only what differs", fi + 1);
					fmt(b2, "%s of %s (%s)", s1, s2, s3);
					dl_draw(a, b2, need_bytes ? done_bytes / (need_bytes / 100 + 1) : 100);
					last_draw = now;
				} else if (!heard) Sleep(1);
			}
			VirtualFree(asked, 0, 0x8000);
		}
		/* 4. the file as the host has it, or nothing */
		SetFilePointer(out, (int)z->size, 0, 0);
		SetEndOfFile(out);
		CloseHandle(out);
		out = INVALID_HANDLE_VALUE;
		fmt(path, "%s%s", stage, g_l0_names[fi]);
		if (hash_file(path) != g_l0_want[fi]) {
			log_msg("net: level0 sync: the rebuilt %s isn't the host's; the full download it is", g_l0_names[fi]);
			goto done;
		}
		VirtualFree(pgot, 0, 0x8000); pgot = 0;
		zs_table_free(&headt, &next, &have);
		VirtualFree(list, 0, 0x8000); list = 0;
	}
	/* 5. into the cache folder */
	for (int fi = 0; fi < 2; fi++) {
		fmt(path, "%s%s", stage, g_l0_names[fi]);
		fmt(rel, "%s\\%s", dir, g_l0_names[fi]);
		if (!GetFullPathNameA(rel, sizeof to, to, 0) || !move_file(path, to)) goto done;
	}
	RemoveDirectoryA(stage);
	for (int fi = 0; fi < 2; fi++) {   /* the block lists did their job */
		fmt(path, "%s%s.zs", sigdir, g_l0_names[fi]);
		DeleteFileA(path);
	}
	RemoveDirectoryA(sigdir);
	log_msg("net: level0 sync: done in %u ms: %u bytes downloaded (the whole pair is %u)", GetTickCount() - began, to_fetch_all, full);
	result = 1;

done:
	if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
	if (pgot) VirtualFree(pgot, 0, 0x8000);
	zs_table_free(&headt, &next, &have);
	if (list) VirtualFree(list, 0, 0x8000);
	if (buf) VirtualFree(buf, 0, 0x8000);
	if (piece) VirtualFree(piece, 0, 0x8000);
	if (result <= 0) {   /* what was rebuilt goes */
		for (int fi = 0; fi < 2; fi++) {
			fmt(path, "%s%s", stage, g_l0_names[fi]);
			DeleteFileA(path);
		}
		RemoveDirectoryA(stage);
	}
	dl_lines_hide();
	return result;
}

/* ---- A new player's download (the Host Game screen) ----------------------------------------- *
 * What a player with no mods (AE's stock level0, no custom maps, nothing in AE\cache) downloads the
 * first time they join this PC's game, in bytes of the files (packet headers not counted):
 *   - a custom level: its whole folder, what map_can_serve lists (Join's Play fetches it first,
 *     see "Map download"); an original level: nothing (it can't be sent);
 *   - level0: nothing when this PC's pair is a stock AE one (g_l0_stock); otherwise what level0
 *     sync would send, measured the joiner's way on a thread: this PC's block lists (sent first),
 *     rolled over the joiner's stock level0, plus the ranges a joiner would ask for. Where a file
 *     has more than ZS_PIECES_MAX ranges the joiner gives up and downloads the whole pair after
 *     what it already fetched, and so does this count. The stock level0 is a stock pair in
 *     AE\cache when there is one, else AEInstaller\vanilla (the level0 every AE install keeps),
 *     which counts a little more: on 2026-11-09 a 197.6 MB mod measured 39.9 MB against vanilla
 *     where the real sync from a stock AE level0 had sent 35.0 MB; on a 499 MB mod the two bases
 *     were within 1% (zsim: 11.58 vs 11.53 MB of the .dat, 328.4 vs 325.9 MB of the .raw).
 * The level0 part takes seconds (both pairs read), so it's kept in Arklite.ini [host] join_level0
 * as "<GameDataFolder pair's sizes and times>|<base folder> <bytes>". */
static const u32 g_l0_stock[][2] = {
	{ 0xf40ec301u, 0x6091538bu },   /* AE core, XML patches in (Linux, 2026-11-01) */
	{ 0xe19686a1u, 0xae11addfu },   /* AE core without its XML patches (an install whose XmlTools didn't run) */
};

static struct {
	volatile int state;                 /* 0 none, 1 measuring, 2 done, -1 failed */
	char key[160];                      /* the pair the state is for and the base (jc_key) */
	char base[64];                      /* the stock level0 measured against */
	u32 bytes, lists, full, hash[2];
	int stock, reported, have_hash;
} g_jc;

static int jc_never(void) { return 0; }

static u32 WINAPI jc_measure(void *unused)
{
	u8 *buf = VirtualAlloc(0, ZS_SCAN + 4096, 0x3000, 4), *list[2] = { 0, 0 }, *have = 0;
	ZsPiece *piece = VirtualAlloc(0, ZS_PIECES_MAX * sizeof *piece, 0x3000, 4);
	int *headt = 0, *next = 0, ok = buf && piece;
	u32 lsize[2], total = 0, full = 0;
	char path[MAX_PATH];

	for (int fi = 0; fi < 2 && ok; fi++) {
		fmt(path, "GameDataFolder\\%s", g_l0_names[fi]);
		ok = (list[fi] = zs_list_file(path, g_zs_block[fi], buf, &lsize[fi], &g_jc.hash[fi])) != 0;
	}
	g_jc.stock = 0;
	for (u32 i = 0; ok && i < sizeof g_l0_stock / sizeof g_l0_stock[0]; i++)
		if (g_jc.hash[0] == g_l0_stock[i][0] && g_jc.hash[1] == g_l0_stock[i][1]) g_jc.stock = 1;
	if (ok && !g_jc.stock) {
		g_jc.lists = total = lsize[0] + lsize[1];
		g_jc.full = full = ((const ZsHead *)list[0])->size + ((const ZsHead *)list[1])->size;
		for (int fi = 0; fi < 2 && ok; fi++) {
			const ZsHead *z = (const ZsHead *)list[fi];
			const u32 *e = (const u32 *)(z + 1);
			u32 mask, nhave = 0, npiece, need;
			if (!(ok = zs_table(z, e, &headt, &next, &have, &mask))) break;
			fmt(path, "%s\\%s", g_jc.base, g_l0_names[fi]);
			zs_scan(path, z, e, headt, next, mask, have, &nhave, INVALID_HANDLE_VALUE, buf, jc_never);
			if (!zs_pieces(z, have, piece, &npiece, &need)) {
				total += full;   /* the whole pair after what was fetched */
				break;
			}
			total += need;
			zs_table_free(&headt, &next, &have);
		}
	}
	zs_table_free(&headt, &next, &have);
	for (int fi = 0; fi < 2; fi++)
		if (list[fi]) VirtualFree(list[fi], 0, 0x8000);
	if (piece) VirtualFree(piece, 0, 0x8000);
	if (buf) VirtualFree(buf, 0, 0x8000);
	g_jc.bytes = total;
	g_jc.have_hash = ok;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	g_jc.state = ok ? 2 : -1;
	return 0;
}

/* GameDataFolder's pair by sizes and times and the base it's measured against (a stock pair in
 * the cache, else AEInstaller\\vanilla), as the Arklite.ini entry names them; 0 if one is missing */
static int jc_key(char *key, char *base)
{
	u32 v[6];
	str_cpy(base, 64, "AEInstaller\\vanilla");
	for (u32 i = 0; i < sizeof g_l0_stock / sizeof g_l0_stock[0]; i++) {
		char dat[96], raw[96];
		fmt(dat, "cache\\%08x-%08x\\%s", g_l0_stock[i][0], g_l0_stock[i][1], g_l0_names[0]);
		fmt(raw, "cache\\%08x-%08x\\%s", g_l0_stock[i][0], g_l0_stock[i][1], g_l0_names[1]);
		if (GetFileAttributesA(dat) != INVALID_FILE_ATTRIBUTES && GetFileAttributesA(raw) != INVALID_FILE_ATTRIBUTES) {
			fmt(base, "cache\\%08x-%08x", g_l0_stock[i][0], g_l0_stock[i][1]);
			break;
		}
	}
	for (int fi = 0; fi < 2; fi++) {
		WIN32_FIND_DATAA fd;
		char path[64];
		HANDLE f;
		fmt(path, "GameDataFolder\\%s", g_l0_names[fi]);
		if ((f = FindFirstFileA(path, &fd)) == INVALID_HANDLE_VALUE) return 0;
		FindClose(f);
		v[3 * fi] = fd.nFileSizeLow;
		v[3 * fi + 1] = fd.ftLastWriteTime[0];
		v[3 * fi + 2] = fd.ftLastWriteTime[1];
	}
	fmt(key, "%08x%08x%08x-%08x%08x%08x|%s", v[0], v[1], v[2], v[3], v[4], v[5], base);
	return 1;
}

/* The level0 part of a new player's download (see above): 1 and *bytes, 0 while it's measured
 * (asked again every frame), -1 when it can't be */
int net_join_level0_bytes(u32 *bytes)
{
	char key[sizeof g_jc.key], base[sizeof g_jc.base], saved[sizeof g_jc.key + 48];
	u32 n;

	if (g_jc.state == 1) return 0;
	if (!jc_key(key, base)) return -1;
	if (g_jc.state != 0 && str_ieq_n(g_jc.key, key, sizeof key)) {
		if (g_jc.state < 0) return -1;
		if (!g_jc.reported) {   /* just measured: logged and kept from this thread */
			g_jc.reported = 1;
			if (g_jc.stock) log_msg("net: a new player's download: level0 %08x %08x is a stock AE one, nothing to download",
			                        g_jc.hash[0], g_jc.hash[1]);
			else log_msg("net: a new player's download: level0 %08x %08x, measured against %s: %u bytes "
			             "(block lists %u; the whole pair is %u)", g_jc.hash[0], g_jc.hash[1], g_jc.base, g_jc.bytes, g_jc.lists, g_jc.full);
			fmt(saved, "%s %u %08x %08x", key, g_jc.bytes, g_jc.hash[0], g_jc.hash[1]);
			WritePrivateProfileStringA("host", "join_level0", saved, ini_path());
		}
		*bytes = g_jc.bytes;
		return 1;
	}
	str_cpy(g_jc.key, sizeof g_jc.key, key);
	GetPrivateProfileStringA("host", "join_level0", "", saved, sizeof saved, ini_path());
	n = str_len(key);
	if (str_ieq_n(saved, key, n) && saved[n] == ' ' && saved[n + 1] >= '0' && saved[n + 1] <= '9') {
		const char *s = saved + n + 1;
		g_jc.bytes = 0;
		for (; *s >= '0' && *s <= '9'; s++) g_jc.bytes = g_jc.bytes * 10 + (u32)(*s - '0');
		/* the pair's hashes after it (an older Arklite saved none: measured again) */
		g_jc.have_hash = 1;
		for (int fi = 0; fi < 2; fi++) {
			u32 h = 0, d = 0;
			if (*s == ' ') s++;
			for (; d < 8 && ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f')); s++, d++) h = h << 4 | (u32)(*s <= '9' ? *s - '0' : *s - 'a' + 10);
			if (d != 8) g_jc.have_hash = 0;
			g_jc.hash[fi] = h;
		}
		if (g_jc.have_hash) {
			g_jc.reported = 1;
			g_jc.state = 2;
			*bytes = g_jc.bytes;
			return 1;
		}
	}
	str_cpy(g_jc.base, sizeof g_jc.base, base);
	g_jc.reported = 0;
	g_jc.state = 1;
	if (!CreateThread(0, 0, jc_measure, 0, 0, 0)) {
		g_jc.state = -1;
		return -1;
	}
	log_msg("net: a new player's download: measuring level0 against %s", base);
	return 0;
}

/* The map part: a custom level's folder (dir ends with '\\'), 0 for an original level. 0 returned
 * when joiners can't download it (too many files, or a path too long) */
int net_join_map_bytes(const char *dir, u32 *bytes)
{
	static MapFile *files;
	u32 n = 0;
	int ok;

	*bytes = 0;
	if (!dir) return 1;   /* an original level: nothing to send */
	if (!files) files = VirtualAlloc(0, MAP_MAX_FILES * sizeof(MapFile), 0x3000, 4);
	if (!files) return 0;
	ok = list_tree(dir, "", 0, files, &n);
	for (u32 i = 0; i < n; i++) *bytes = *bytes + files[i].size < *bytes ? 0xffffffffu : *bytes + files[i].size;
	return ok;
}

/* ---- What this PC downloads to join a server (the Join page) ---- *
 * From the server's info answer (PKT_INFO, protocol 21):
 *   - level0: nothing when the host's pair is this PC's installed one (measured as the Host page
 *     does, kept in [host] join_level0 with the hashes) or is already in AE\cache\<its hashes>;
 *     else the host's own measure of a new player's download (against stock AE level0), an
 *     estimate: the real sync starts from this PC's level0 and cache, which may hold more of it;
 *   - the map: nothing for an original level or when CustomLevels\<folder> hashes the same as
 *     the host's (hash_map's way); else the host's folder's bytes. */
#define MAP_HASH_CACHE 16
static struct { char folder[64]; u32 hash; int have; } g_mh[MAP_HASH_CACHE];
static u32 g_nmh;

void net_join_download_forget(void) { g_nmh = 0; }

static int folder_hash(const char *folder, u32 *hash)
{
	char dir[MAX_PATH];
	u32 i;

	for (i = 0; i < g_nmh && i < MAP_HASH_CACHE; i++)
		if (str_len(g_mh[i].folder) == str_len(folder) && str_ieq_n(g_mh[i].folder, folder, sizeof g_mh[i].folder)) {
			*hash = g_mh[i].hash;
			return g_mh[i].have;
		}
	i = g_nmh++ % MAP_HASH_CACHE;
	str_cpy(g_mh[i].folder, sizeof g_mh[i].folder, folder);
	g_mh[i].have = custom_game_folder_dir(folder, dir, sizeof dir);
	g_mh[i].hash = g_mh[i].have ? hash_tree(dir, "", 0) : 0;
	*hash = g_mh[i].hash;
	return g_mh[i].have;
}

int net_join_download(const NetServerInfo *s, u32 *bytes)
{
	u32 own, total = 0, h;
	int r, estimate = 0;
	char dat[96], raw[96];

	*bytes = 0;
	if (!s->has_download) return NETDL_UNKNOWN;
	if (s->folder[0]) {
		if (!s->map_ok) return NETDL_IMPOSSIBLE;
		if (!folder_hash(s->folder, &h) || h != s->map_hash) total = s->map_bytes;
	}
	r = net_join_level0_bytes(&own);   /* this PC's own pair's hashes come with it */
	if (r == 0) return NETDL_MEASURING;
	if (!(r > 0 && g_jc.have_hash && g_jc.hash[0] == s->l0[0] && g_jc.hash[1] == s->l0[1])) {
		fmt(dat, "cache\\%08x-%08x\\%s", s->l0[0], s->l0[1], g_l0_names[0]);
		fmt(raw, "cache\\%08x-%08x\\%s", s->l0[0], s->l0[1], g_l0_names[1]);
		if (GetFileAttributesA(dat) == INVALID_FILE_ATTRIBUTES || GetFileAttributesA(raw) == INVALID_FILE_ATTRIBUTES) {
			if (s->l0_bytes == NET_INFO_MEASURING) return NETDL_MEASURING;
			if (s->l0_bytes == NET_INFO_UNKNOWN) return NETDL_UNKNOWN;
			total = total + s->l0_bytes < total ? 0xffffffffu : total + s->l0_bytes;
			estimate = s->l0_bytes != 0;
		}
	}
	*bytes = total;
	return estimate ? NETDL_ABOUT : NETDL_EXACT;
}

/* The level load's TMrLevel_Load call: the host's files first when they're wanted */
/* ---- What the loading popup says while a network game loads ----------------------------------- *
 * A joiner loads the level up to three times before it plays (the Join list's load, again with the
 * host's level0, again for the warm-up or the match), each in Oni's loading popup: a 350x150 box,
 * its bar at 10% for the whole file reading (the long part: ONrLevel_Load moves it only between
 * its steps, 0x5059d0), on a black screen. On the Windows joiner (2560x1440) that read as a black
 * screen with no progress until the game came on (user's report, 2026-11-13). Now the popup's two
 * lines under the bar (the map download's, dl_lines_create) say which step it is ("Switching to the
 * host's level0") and which file is being read ("level14_Final.dat (3 of 13)"), and the bar moves
 * from 10% to 20% file by file (custom_game.c's open hook calls net_load_file). Windows messages
 * are handled at each of those frames, so Windows doesn't take the window for hung. Only for loads
 * net.c starts and a joiner's first one; the game's own loads are as Oni shows them. */
static const char *g_load_a;           /* this load's first line; 0: not shown */
static char g_load_map[64];
static u32 g_load_files;               /* files opened so far in this load */
static int g_load_count_on, g_load_quit;
static WMtWindow *g_cover_dialog;      /* the popup the joiner's cover made between loads (see below) */
static u32 g_cover_loads;              /* level loads so far (the cover's steps) */
static int g_cover_pending;            /* Join's Play or ARKLITE_JOIN: the cover starts with the next level load */
static void cover_begin(void);

static void load_pump(void)
{
	struct { void *hwnd; u32 msg, wp, lp, time, x, y; } m;

	if (g_load_quit) return;
	while (PeekMessageW(&m, 0, 0, 0, 1 /* PM_REMOVE */)) {
		if (m.msg == 0x12 /* WM_QUIT */) {   /* for Oni's own loop, after the load */
			PostQuitMessage((int)m.wp);
			g_load_quit = 1;
			return;
		}
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
}

static void load_show(const char *b, u32 percent)
{
	if (!g_load_a || !ONgLoadingDialog) return;
	dl_lines_create();   /* (the same two lines again: a download hides them when it's done) */
	if (!g_dl_lines[0]) return;
	load_pump();
	dl_draw(g_load_a, b, percent);
}

void net_load_file(const char *ref, u32 count)
{
	const char *name = ref;
	char b[96];

	if (!g_load_count_on || !g_load_a) return;
	for (const char *p = ref; *p; p++)
		if (*p == '\\' || *p == '/') name = p + 1;
	g_load_files++;
	if (count < g_load_files) count = g_load_files;
	fmt(b, "%s (%u of %u)", name, g_load_files, count);
	load_show(b, 10 + 10 * (g_load_files - 1) / count);
}

static u16 ONI_FC level_load(u32 level, u32 edx)
{
	u32 l = g_dl_level;
	u16 r;

	g_load_a = 0;
	g_load_files = 0;
	g_load_count_on = g_load_quit = 0;
	g_cover_loads++;
	if (g_cover_pending && g_mode == NET_JOIN) cover_begin();
	g_cover_pending = 0;
	/* a new popup (ONrLevel_Load's, 0x505a0b); or the one a joiner's cover kept up between its
	 * loads, which ONrLevel_Load uses as it is (0x505a09), its lines already in it */
	if (!g_cover_dialog || ONgLoadingDialog != g_cover_dialog) g_dl_lines_of = 0;
	if (ONgLoadingDialog && (g_load_step || g_mode == NET_JOIN)) {
		const char *name = custom_game_level_name(level & 0xff);
		g_load_a = g_load_step ? g_load_step : "Joining the game";
		if (name) str_cpy(g_load_map, sizeof g_load_map, name);
		else fmt(g_load_map, "Level %u", level & 0xff);
		load_show(g_load_map, 10);
	}
	g_load_step = 0;

	if (g_l0_fetch) {
		char rel[80], dir[MAX_PATH], folder[40], path[128];
		g_l0_fetch = 0;
		fmt(rel, "%s\\", g_l0_dir);
		fmt(folder, "level0-%08x-%08x", g_l0_want[0], g_l0_want[1]);
		int synced = level0_sync(g_l0_dir);   /* only the differences; else the whole pair */
		if (synced > 0 || (synced == 0 && GetFullPathNameA(rel, sizeof dir, dir, 0) && map_download_to(L0_SET, dir, folder))) {
			fmt(path, "%s\\level0_Final.dat", g_l0_dir);
			u32 d = hash_file(path);
			fmt(path, "%s\\level0_Final.raw", g_l0_dir);
			if (d == g_l0_want[0] && hash_file(path) == g_l0_want[1]) g_l0_pending = 1;
			else log_msg("net: the downloaded level0 in %s isn't the host's: not switching to it", g_l0_dir);
		} else {
			log_msg("net: no level0 from the host (%s): this PC can't join it", g_dl_why ? g_dl_why : "?");
		}
	}
	if (!g_l0_pending && g_l0_active[0] && g_mode != NET_JOIN && !g_l0_restart_said) {
		/* a host's level0 is only for playing on that host: hosting, or playing alone, is on this
		 * PC's own again (the installed files were never touched) */
		g_l0_want[0] = g_l0_installed[0];
		g_l0_want[1] = g_l0_installed[1];
		if (level0_swaps() >= L0_SWAPS_MAX) {
			g_l0_restart_said = 1;
			log_msg("net: not joining any more, but this Oni already switched level0 once: it stays on the host's until Oni restarts");
		} else if (l0_own_dir()) {
			g_l0_pending = 1;
			log_msg("net: not joining any more: back to this PC's own level0 (%s)", g_l0_dir);
		} else {
			g_l0_installed[0] = g_l0_installed[1] = 0;   /* (not tried again) */
			g_l0_active[0] = 0;
			log_msg("net: staying on the host's level0 until Oni restarts");
		}
	}
	if (g_l0_pending) {
		int back = g_l0_active[0] && g_l0_want[0] == g_l0_installed[0] && g_l0_want[1] == g_l0_installed[1];
		g_l0_pending = 0;
		if (!g_l0_active[0]) {
			g_l0_installed[0] = g_files[2];
			g_l0_installed[1] = g_files[3];
		}
		load_show(back ? "Opening your own level0" : "Opening the host's level0", 10);
		if (level0_swap(g_l0_dir)) {
			g_files[2] = g_l0_want[0];
			g_files[3] = g_l0_want[1];
			if (back) {
				g_l0_active[0] = 0;
				g_l0_tried[0] = g_l0_tried[1] = 0;   /* a host with another level0 may be switched to again */
				log_msg("net: on this PC's own level0 %08x %08x again", g_files[2], g_files[3]);
			} else {
				str_cpy(g_l0_active, sizeof g_l0_active, g_l0_dir);   /* as a host, this is the pair it sends */
				log_msg("net: now on the host's level0 %08x %08x (from %s) while playing there", g_files[2], g_files[3], g_l0_dir);
			}
		}
	}
	if (l && (level & 0xff) == l) {
		g_dl_level = 0;
		if (!map_download(l)) g_dl_declined = l;
	}
	load_show(g_load_map, 10);
	g_load_count_on = 1;
	r = TMrLevel_Load(level, edx);
	g_load_count_on = 0;
	if (g_load_a && g_dl_lines[1] && ONgLoadingDialog)   /* shown at ONrLevel_Load's next step (20%) */
		WMrWindow_SetTitle(g_dl_lines[1], "Setting up the level", 0x100);
	g_load_a = 0;
	g_dl_lines_of = 0;   /* (the popup closes when ONrLevel_Load is done) */
	g_cover_dialog = 0;  /* (the cover's too: ONrLevel_Load deletes whichever it showed, 0x50687d) */
	return r;
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

int net_map_fetch(char *folder, u32 size, u32 level)
{
	void *ui;
	int ok;

	g_dl_why = "can't reach the host";
	if (!folder_name_ok(folder)) {
		log_msg("net: the host's map folder \"%s\" isn't a folder name, not downloading it", folder);
		g_dl_why = "the host's map folder has a bad name";
		return 0;
	}
	if (g_mode != NET_JOIN || (g_sock == INVALID_SOCKET && !net_open())) return 0;
	log_msg("net: this PC doesn't have the host's map %s (level %d): downloading it", folder, level);

	if (!ONgLoadingDialog) {
		ONgLoadingContext = ONiUI_GetContext();
		if ((ui = ONiUI_FindContext(ONcUIContextOni))) ONiUI_SetContext(ui);
		WMrDialog_Create(ONcLoadingDialogID, 0, ONrLoadingDialog_Proc, 0xffffffffu, &ONgLoadingDialog);
	}
	ok = map_fetch_into(level, folder, size);   /* (a half-made folder isn't a map: what arrived stays in CustomLevels.download) */
	g_dl_lines_of = 0;
	if (ONgLoadingDialog) {
		WMrWindow_Delete(ONgLoadingDialog);
		ONgLoadingDialog = 0;
		ONiUI_SetContext(ONgLoadingContext);
	}
	return ok;
}

const char *net_map_fetch_error(void)
{
	return g_dl_why ? g_dl_why : "?";
}

/* ---- One loading popup while joining ------------------------------------------------------- *
 * A joiner loads the level two or three times before it plays: the Join list's load, a reload
 * with the host's level0 when its own differs (every Windows AE joiner of a Linux host: AE's XML
 * patches), and the reload for the session it joins (the warm-up or the match, then catching up).
 * Between them the lobby ran for a moment: a few frames of the level ("Press F3 to be ready /
 * Connecting to the host...") and the level script's music, started again by every load. The
 * user's recording from the Windows PC (2026-11-13, 60 fps): Play at 4.65 s, the level at 5.02,
 * a popup, the level at 5.47, a popup, the catch-up at 5.80, in the game at 6.7; music started
 * three times (script_debug.txt). Under Wine the same, shorter.
 *
 * Now from Play (or ARKLITE_JOIN) until this PC is in the game, the screen is one loading popup:
 * between loads the game's display (0x4fb480, net.c display) draws nothing and Oni's own loading
 * popup stays up, made the way ONrLevel_Load makes it (net_map_fetch's recipe); ONrLevel_Load
 * uses a popup that's there instead of making one (0x505a09) and deletes it at its end, so the
 * next frame makes another. Its lines say the step and the catch-up's percent. Every channel is
 * silent meanwhile (volume_hush): the loads start the music and the level's sounds as always,
 * heard once it's over. Drawing isn't game state (a dedicated server draws nothing at all), and
 * the frames and ticks run as before. It ends:
 *   - in the game (S_RUN, not joining), or the session over (S_LOST, S_OVER);
 *   - in the host's lobby, COVER_LOBBY_MS after its first word with no reload or catch-up coming;
 *   - a refusal on the lobby screen (not one being fixed by a reload: g_problem_hidden);
 *   - the pause menu (Esc: the way out stays), leaving the network game or the level;
 *   - COVER_WAIT_MS on one step with nothing happening (no word from the host, say) but the
 *     catch-up, whose percent moves; the lobby screen then says what it always said. */
#define COVER_LOBBY_MS 1000
#define COVER_WAIT_MS  15000
static int g_cover;
static u32 g_cover_began, g_cover_step_at, g_cover_heard_at, g_cover_seen_loads, g_cover_first_load;
static int g_cover_state;

/* How far a joiner's catch-up is, 0..100 */
static int joining_pct(void)
{
	int pct = !g_hist_avail ? 0 : g_hist_avail < 40000000u ? (int)(g_ticks * 100 / g_hist_avail) :
	          (int)(g_ticks / (g_hist_avail / 100));   /* (no 64-bit division: -nostdlib) */
	return pct > 100 ? 100 : pct;
}

static void cover_begin(void)
{
	if (g_cover) return;
	g_cover = 1;
	g_cover_began = g_cover_step_at = GetTickCount();
	g_cover_heard_at = 0;
	g_cover_state = -1;
	g_cover_seen_loads = g_cover_loads;
	g_cover_first_load = g_cover_loads - 1;   /* (from level_load, which counted this one) */
	volume_hush(1);
}

static void cover_end(const char *why)
{
	if (!g_cover) return;
	g_cover = 0;
	if (g_cover_dialog && ONgLoadingDialog == g_cover_dialog) {   /* between loads: closed as ONrLevel_Load closes it */
		WMrWindow_Delete(g_cover_dialog);
		ONgLoadingDialog = 0;
		ONiUI_SetContext(ONgLoadingContext);
		g_dl_lines[0] = g_dl_lines[1] = 0;
		g_dl_lines_of = 0;
	}
	g_cover_dialog = 0;
	volume_hush(0);
	log_msg("net: joining: one loading popup for %u ms over %u level loads, until %s", GetTickCount() - g_cover_began,
	        g_cover_loads - g_cover_first_load, why);
}

static const char *cover_done(u32 now)
{
	if (g_mode != NET_JOIN) return "this PC stopped joining";
	if (live_menu_open()) return "the pause menu opened";
	if (g_state == S_RUN && !g_joining) return "it was in the game";
	if (g_state == S_LOST || g_state == S_OVER) return "the session ended";
	if (g_problem && !g_problem_hidden) return "a refusal to show";
	if (g_state == S_LOBBY && g_cover_heard_at && now - g_cover_heard_at >= COVER_LOBBY_MS) return "it was in the host's lobby";
	if (g_state != S_CATCHUP && now - g_cover_step_at >= COVER_WAIT_MS) return "nothing happened for a while";
	return 0;
}

/* The popup up (made as ONrLevel_Load makes it, when there's none) with this step's lines: 1 when it
 * is the cover's, ready to be drawn */
static int cover_popup(void)
{
	u32 pct = 100;
	const char *a, *b = g_load_map;
	char line[64];

	if (!ONgLoadingDialog) {
		void *ui;
		ONgLoadingContext = ONiUI_GetContext();
		if ((ui = ONiUI_FindContext(ONcUIContextOni))) ONiUI_SetContext(ui);
		WMrDialog_Create(ONcLoadingDialogID, 0, ONrLoadingDialog_Proc, 0xffffffffu, &ONgLoadingDialog);
		if (!ONgLoadingDialog) {
			ONiUI_SetContext(ONgLoadingContext);
			cover_end("no loading popup could be made");
			return 0;
		}
		g_cover_dialog = ONgLoadingDialog;
	}
	if (ONgLoadingDialog != g_cover_dialog) return 0;   /* (someone else's popup: drawn as it is, over the game) */
	dl_lines_create();
	if (g_state == S_CATCHUP || (g_state == S_RUN && g_joining == 2)) {
		a = g_warm ? "Joining the warm-up" : "Joining the match";
		if (g_state == S_CATCHUP) {
			pct = (u32)joining_pct();
			fmt(line, "Catching up with the host (%u%%)", pct);
			b = line;
		} else {
			b = "Coming into the game";
		}
	} else if (g_state == S_WAIT) {
		a = g_warm ? "Starting the warm-up" : "Starting the match";
		b = "Waiting for the other players";
	} else if (g_state == S_LOBBY && !g_host_heard) {
		a = "Connecting to the host...";
	} else {
		a = "Joining the game";
	}
	{	/* only when it changes: the popup is Oni's memory, and rollback runs from "Coming into the game" on */
		static u32 set_in;
		static char set[200];
		char now_set[200];
		int same = 1;
		fmt(now_set, "%s|%s|%u", a, b, pct);
		for (int i = 0; same && (set[i] || now_set[i]); i++) same = set[i] == now_set[i];
		if (set_in != g_dl_lines_made || !same) {
			dl_set(a, b, pct);
			set_in = g_dl_lines_made;
			str_cpy(set, sizeof set, now_set);
		}
	}
	return 1;
}

/* The game display's frame (net.c display) while joining: 1 when it's the popup's, the game not
 * drawn; the main loop's UI draw (0x476140) then draws the popup */
static int cover_frame(void)
{
	u32 now = GetTickCount();
	const char *why;

	if (!g_cover) return 0;
	if (g_cover_state != (int)g_state || g_cover_seen_loads != g_cover_loads) {   /* a new step */
		g_cover_state = (int)g_state;
		g_cover_seen_loads = g_cover_loads;
		g_cover_step_at = now;
		g_cover_heard_at = 0;
	}
	if (g_state == S_LOBBY && g_host_heard && !g_cover_heard_at) g_cover_heard_at = now | 1;
	if ((why = cover_done(now))) {
		cover_end(why);
		return 0;
	}
	return cover_popup();
}

/* The level's start (0x4f4d40, which ONrLevel_Load calls at 0x5068be just after closing its popup)
 * draws one frame black, the fade it then lifts (0x4f515e: 0x4f5220 with alpha 1). Between a
 * joiner's loads that was a black flash between two popups (Windows recording, 2026-11-13: 2
 * frames; the probe: one black frame at every load's end): under the cover, the popup instead. */
#define ONcLevelStart_FadeCall  0x4f515e
#define ONrFade_Frame           0x4f5220   /* (ecx: splash or 0, float alpha), ret 4 */
static void ONI_FC level_fade_frame(void *splash, u32 edx, float alpha)
{
	if (g_cover && g_mode == NET_JOIN && cover_popup()) {
		M3rFrame_Begin(0);
		WMrDisplay();
		M3rFrame_End();
		return;
	}
	((void (ONI_FC *)(void *, u32, float))ONrFade_Frame)(splash, edx, alpha);
}

/* What the Join list and the server list show of this game: the players (a dedicated server's own
 * isn't one), the most it takes, and how many of them watch */
static void info_counts(u32 *players, u32 *max, u32 *spect)
{
	u32 mine = g_dedicated ? ~1u : ~0u;
	*players = g_state == S_IDLE ? !g_dedicated : bits(present_mask() & mine);
	*max = capacity() - (g_dedicated != 0);
	*spect = bits(players_spectators() & present_mask() & mine);   /* players watching (/spectate) */
}

static void answer_info(const u8 *buf, u32 n, const SOCKADDR_IN *to)
{
	const PktInfoReq *q = (const PktInfoReq *)buf;
	PktInfo p;
	const char *folder = custom_game_level_folder(g_level), *map = custom_game_level_name(g_level);

	if (q->h.proto != PROTO) {   /* an older Arklite's (8-byte head): its token and time back, as short */
		u32 r[5];
		memset(r, 0, sizeof r);
		r[0] = MAGIC;
		r[1] = PROTO | PKT_INFO << 16;
		memcpy(&r[2], buf + 8, 8);
		sendto(g_sock, r, sizeof r, 0, to, sizeof *to);
		return;
	}
	memset(&p, 0, sizeof p);
	head(&p.h, PKT_INFO);
	p.token = q->token;
	p.sent = q->sent;
	p.level = g_level;
	info_counts(&p.players, &p.max_players, &p.spectators);
	p.in_match = g_state != S_LOBBY && !g_warm;
	p.server_id = g_server_id;
	str_cpy(p.name, sizeof p.name, g_server_name);
	if (folder) str_cpy(p.folder, sizeof p.folder, folder);
	if (map) str_cpy(p.map, sizeof p.map, map);
	else fmt(p.map, "Level %d", g_level);
	{
		u32 b;
		int r = net_join_level0_bytes(&b);   /* measured once, on a thread (the Host page's line) */
		p.l0_bytes = r > 0 ? b : r == 0 ? NET_INFO_MEASURING : NET_INFO_UNKNOWN;
	}
	p.l0[0] = g_files[2];
	p.l0[1] = g_files[3];
	p.map_ok = 1;
	if (folder) {
		p.map_ok = map_can_serve() != 0;
		p.map_hash = g_map_hash;
		for (u32 i = 0; i < g_nserve; i++) p.map_bytes = p.map_bytes + g_serve[i].size < p.map_bytes ? 0xffffffffu : p.map_bytes + g_serve[i].size;
	}
	p.nmods = ui_installed_mods(p.mods, NET_INFO_MODS);
	send_addr(&p, sizeof p, to);
}

/* host: what the server list shows of this game (helper.c's BEAT), as PKT_INFO says it */
static void helper_frame_host(u32 now)
{
	NetServerInfo g;
	const char *folder = custom_game_level_folder(g_level), *map = custom_game_level_name(g_level);
	u32 addr;
	u16 port;
	int news;

	while (helper_take_intro(&addr, &port)) {   /* a player wants in: knock toward it (see "Hole punching") */
		SOCKADDR_IN a;
		memset(&a, 0, sizeof a);
		a.family = 2;
		a.addr = addr;
		a.port = port;
		if (link_of(&a) >= 0) continue;
		{   /* the log says it once per address (players browsing ask again every few seconds) */
			static u32 said[8], nsaid;
			u32 k, key = a.addr ^ (u32)a.port << 16 ^ a.port;
			for (k = 0; k < nsaid && k < 8 && said[k] != key; k++) {}
			if (k == nsaid || k == 8) {
				const u8 *ip = (const u8 *)&a.addr;
				said[nsaid++ % 8] = key;
				log_msg("helper: a player at %d.%d.%d.%d:%d is looking at this game: knocking toward it", ip[0], ip[1], ip[2], ip[3], ntohs(a.port));
			}
		}
		punch_add(&a, INTRO_PUNCH_MS, 1);
	}
	if ((news = helper_take_news()) >= 0)
		chat_server(news == 1 ? "Your game is on the server list: players outside your network find it in Join." :
		            news == 3 ? "Your game isn't on the server list ([host] listed=0), but players can still reach it by address." :
		            news == 0 ? "The server list is full: players outside your network need /punch (see /help)." :
		                        "The server list doesn't answer: players outside your network need /punch (see /help).");
	if (!helper_beat_due(g_sock, now)) return;
	memset(&g, 0, sizeof g);
	g.level = g_level;
	info_counts(&g.players, &g.max_players, &g.spectators);
	g.in_match = g_state != S_LOBBY && !g_warm;
	g.server_id = g_server_id;
	str_cpy(g.name, sizeof g.name, g_server_name);
	if (folder) str_cpy(g.folder, sizeof g.folder, folder);
	if (map) str_cpy(g.map, sizeof g.map, map);
	else fmt(g.map, "Level %d", g_level);
	helper_beat(g_sock, &g);
}

/* joiner: the host hasn't answered: the helper tells it this PC's address (it knocks back), and
 * says where the host really is (a CGNAT host's port isn't the one it hosts on) */
static void helper_frame_join(u32 now)
{
	u32 asked, addr;
	u16 asked_port, port;
	int known;

	while (helper_intro_ack(g_sock, &asked, &asked_port, &known, &addr, &port)) {
		if (asked != g_link[0].addr.addr || asked_port != g_link[0].addr.port) continue;
		if (g_helper_knows != (known ? 1 : -1))
			log_msg(known ? "net: the helper knows the host and told it this PC's address" : "net: the helper doesn't know that host");
		g_helper_knows = known ? 1 : -1;
		if (known && !g_key && (addr != g_link[0].addr.addr || port != g_link[0].addr.port)) {
			const u8 *ip = (const u8 *)&addr;
			log_msg("net: the host is at %d.%d.%d.%d:%d on the internet (the helper says)", ip[0], ip[1], ip[2], ip[3], ntohs(port));
			g_link[0].addr.addr = addr;
			g_link[0].addr.port = port;
			g_intro_at = 0;   /* introduce to that address too */
		}
	}
	if (g_key || now - g_opened_at < INTRO_AFTER_MS || (g_intro_at && now - g_intro_at < INTRO_EVERY_MS)) return;
	g_intro_at = now ? now : 1;
	helper_intro(g_sock, g_link[0].addr.addr, g_link[0].addr.port);
}

/* ---- The match's end ------------------------------------------------------------------------ *
 * players.c marks the tick that reached the score or time limit (tick state, so a rollback that
 * changes the score changes it too). Once that tick ran with every player's real inputs
 * (rollback: g_verified past it; with input delay every tick does; with the host lost there's
 * nothing to wait for) the match is over: S_OVER, the game stands (the clock stops, as in
 * S_WAIT) and mp_hud.c shows the result. Each PC's player presses fire (the left mouse button:
 * the clock is stopped, so no action records come) and the joiners say so in PKT_END. When every
 * player still here did, or END_MS after its own end, the host sends "go" for a warm-up (host_go,
 * with whoever waited for the match's end), and F3 there starts the next match. Without any joiner
 * left it goes back to its lobby. A joiner that never hears "go" goes back to its lobby on its own
 * 5 s later. Inputs keep going out meanwhile, so a PC a few ticks behind can still finish the
 * match's last ticks.
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

/* host: the players still here after the match, and how many of them pressed fire */
static u32 end_players(void) { return bits(present_mask() & (g_dedicated ? ~1u : ~0u)); }

static u32 end_ready_count(void)
{
	u32 n = g_end_ready != 0 && !g_dedicated;   /* a dedicated server's own doesn't play */
	for (int l = 1; l < NPL; l++) n += live(l) && g_link[l].end_ready;
	return n;
}

static void send_end(int l)
{
	PktEnd p;
	u32 now = GetTickCount();
	memset(&p, 0, sizeof p);
	head(&p.h, PKT_END);
	p.level = g_level;
	p.ready = g_end_ready;
	p.players = end_players();
	p.ready_count = end_ready_count();
	p.left_ms = now - g_over_at < END_MS ? END_MS - (now - g_over_at) : 0;
	send_link(l, &p, sizeof p);
	g_link[l].last_end = now;
}

static void send_end_all(void)
{
	for (int l = 0; l < NPL; l++)
		if (live(l)) send_end(l);
}

static void back_to_lobby(const char *why)
{
	u32 level = g_level;
	log_msg("net: back to the lobby (%s): reloading level %d", why, level);
	g_over_lost = 1;   /* no goodbye: the other PCs go back too */
	g_match_pending = 0;
	session_end("match over");
	/* Out of the session: if the host lets this PC in again, it's into the same session number
	 * (a warm-up goes on), which must not read as a repeat. A failed catch-up keeps it: HELLO
	 * says catchup_failed for that session. */
	if (g_mode == NET_JOIN && g_go_taken != g_cu_failed) g_go_taken = 0;
	g_load_step = "Back to the lobby";
	reload_level(level, 0);
}

static void end_received(int l, const PktEnd *p)
{
	if (p->level != g_level) return;
	if (g_mode == NET_HOST) {
		if (g_link[l].end_ready != (int)p->ready && p->ready) log_msg("net: %s (player %d) pressed fire", mp_name(l), l + 1);
		g_link[l].end_ready = p->ready;
		return;
	}
	g_end_players = p->players;
	g_end_ready_count = p->ready_count;
	g_end_left_ms = p->left_ms;
	g_end_rx = GetTickCount();
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
	u32 mask = present_mask() | (1u << g_slot);
	int w = players_winner(mask), order[NPL], n = players_ranking(mask, order);
	char line[160] = "";

	g_over_lost = g_state == S_LOST;
	g_state = S_OVER;
	g_over_at = now;
	for (int l = 0; l < NPL; l++) {
		g_link[l].last_end = 0;
		g_link[l].end_ready = 0;
	}
	g_end_ready = 0;
	g_fire_down = fire_down();   /* fire held as the match ended doesn't count */
	g_end_players = bits(mask);
	g_end_ready_count = 0;
	g_end_left_ms = END_MS;
	g_end_rx = now;
	g_look_who = 0;
	items_stop_firing();   /* no more ticks: a trigger held now would loop its sound until the reload */
	mp_sound("objective_new");
	for (int i = 0; i < n; i++) {
		char one[48];
		fmt(one, "%s%s %d", i ? ", " : "", mp_name(order[i]), players_score(order[i]));
		if (str_len(line) + str_len(one) < sizeof line) fmt(line + str_len(line), "%s", one);
	}
	log_msg("net: the match is over after %d ticks: %s; %s", g_ticks, line,
	        w < 0 ? "a draw" : w == g_slot ? "this PC's player won" : "another player won");
	if (g_dedicated && g_mode == NET_HOST)
		server_print("The match is over: %s. %s%s", line[0] ? line : "nobody played", w < 0 ? "A draw" : "Winner: ", w < 0 ? "" : mp_name(w));
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

static void ONI_FC camera_update(u32 ticks, u32 edx, u32 arg);

/* S_WAIT, from the reload until every PC is in: no ticks, so the camera never ran for the new
 * level and the host (whose camera nothing else moves) saw black until it moved. It runs as at
 * the match's end; a joiner's is pointed at its own player first (camera_update, no tick). */
static void wait_camera(u32 count, const void *actions, u32 now)
{
	camera_update(0, 0, 0);
	over_look(count, (const u8 *)actions, now);
}

static void host_check_silence(u32 now);
static void host_go(int warm);

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
		send_end_all();
	}
	g_fire_down = down;
	for (int l = 0; l < NPL; l++) {
		if (!live(l)) continue;
		if (now - g_link[l].last_end >= END_SEND_MS) send_end(l);
		if (now - g_link[l].last_send >= 100) send_inputs(l);   /* the others may still need our last ones */
	}
	if (g_mode == NET_HOST) {
		host_check_silence(now);
		if (((g_end_ready || g_dedicated) && end_ready_count() >= end_players()) || now - g_over_at >= END_MS) {
			log_msg("net: the match's end is over: %s", now - g_over_at >= END_MS ? "time's up" : "everyone pressed fire");
			host_go(1);
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

static void host_begin(void);

/* host: player l can join the session in progress (see "Joining a game in progress") */
static int catchup_possible(int l)
{
	return g_mode == NET_HOST && g_state == S_RUN && ROLLING && g_hist_ok && l > 0 && l < NPL && l < (int)capacity() &&
	       players_can_join(l) && !g_link[l].no_catchup;
}

/* host: player l starts catching up: it hears so in LOBBY (catchup), loads the level and asks for
 * the session's inputs. Its player has no character and nothing pressed until it's in. */
static void start_joining(int l)
{
	Link *k = &g_link[l];
	k->joining = 1;
	k->waiting = k->rejoin = 0;
	k->ready = 0;
	k->spect_gen = 0;
	if (l < (int)g_nplayers) {   /* its place is in the session already (it was gone, or no one's) */
		g_gone &= ~(1u << l);
		g_left &= ~(1u << l);
		g_absent |= 1u << l;
	}
	log_msg("net: %s (player %d) joins the %s in progress: catching up from its tick 0 (the session is at tick %d)",
	        mp_name(l), l + 1, g_warm ? "warm-up" : "match", g_ticks);
}

/* host: a place for a PC that wants in: a free one the map has a spawn point for, or during a
 * session one whose player is gone; -1: full */
static int free_place(const SOCKADDR_IN *from)
{
	u32 cap = capacity();
	int l;
	if (places_taken() >= cap) return -1;
	for (l = 1; l < (int)cap; l++)
		if (!g_link[l].used) break;
	if (l >= (int)cap && g_state == S_RUN) {
		for (l = 1; l < (int)cap; l++)
			if (g_link[l].used && gone(l) && !g_link[l].rejoin) break;
		if (l >= (int)cap) return -1;
		log_msg("net: %s's place (player %d, gone) goes to a new player", mp_name(l), l + 1);
		g_link[l].used = 0;
		g_gone &= ~(1u << l);   /* no one plays it until the new player is in */
		g_left &= ~(1u << l);
		g_absent |= 1u << l;
	}
	if (l >= (int)cap) return -1;
	memset(&g_link[l], 0, sizeof g_link[l]);
	g_link[l].addr = *from;
	g_link[l].used = 1;
	link_reset(&g_link[l]);
	g_link[l].last_recv = g_last_join = GetTickCount();
	g_names_serial++;
	names_push();
	return l;
}

/* host: joining player l caught up to tick from: it gets everyone's inputs from there, and its
 * player is added to the session (no one plays it yet: absent) */
static void joining_live(int l, u32 from)
{
	Link *k = &g_link[l];
	if (k->joining != 1 || !from || from > g_hist_n) return;
	if ((u32)l >= g_nplayers) add_players((u32)l + 1, 0);
	g_absent |= 1u << l;
	k->joining = 2;
	for (int q = 0; q < NPL; q++) k->ack[q] = q < (int)g_nplayers && from < have_of(q) ? from : have_of(q);
	memset(k->theirs, 0, sizeof k->theirs);
	memset(k->checked, 0, sizeof k->checked);
	k->adv = k->adv_known = 0;
	k->stamp = k->stamp_at = 0;
	k->sent_sig = 0;
	k->spect_gen = k->live_from = from;
	k->near_since = 0;
	k->held_logged = 0;
	log_msg("net: %s (player %d) caught up to tick %d (the session is at %d): in sync, coming in", mp_name(l), l + 1, from,
	        g_ticks);
}

/* host: the joining players that are in sync come in. Each needs: inputs from its PC near this
 * PC's ticks, ours reaching it, and every other PC knowing its player (they ran its absent ticks
 * as nothing pressed). It comes in at the first tick whose input isn't made yet: its own inputs
 * count from there, and the host's record says it has a character from there.
 * Near means steadily: JOIN_STEADY live ticks of its own first, then JOIN_STEADY of ours in a row
 * with its inputs within JOIN_GAP and packets coming. Its first live frame does slow one-time
 * work (rollback's start; the Windows joiner of 2026-11-06 went quiet ~1.4 s there): taken in
 * at its first input, everyone's game stood still waiting for it (the host 64 frames). Now a
 * PC that hitches only comes in later. */
static void joining_frame(void)
{
	u32 now = GetTickCount();
	for (int l = 1; l < NPL && l < (int)g_nplayers; l++) {
		Link *k = &g_link[l];
		int known = 1;
		if (!k->used || k->joining != 2 || !k->adv_known) continue;
		if (k->spect_gen < k->live_from + JOIN_STEADY || k->spect_gen + JOIN_GAP < g_ticks || k->ack[0] + JOIN_GAP < g_gen ||
		    (int)(now - k->last_recv) > JOIN_QUIET_MS) {
			if (k->near_since && !k->held_logged++)
				log_msg("net: %s (player %d) fell behind at tick %d (its inputs up to %d, %d ms since its last packet): it "
				        "comes in once it keeps up", mp_name(l), l + 1, g_ticks, k->spect_gen, (int)(now - k->last_recv));
			k->near_since = 0;
			continue;
		}
		if (!k->near_since) k->near_since = g_ticks ? g_ticks : 1;
		if (g_ticks < k->near_since + JOIN_STEADY) continue;
		for (int o = 1; o < NPL; o++)
			if (o != l && live(o) && !g_link[o].joining && o < (int)g_nplayers && !(g_link[o].knows >> l & 1)) known = 0;
		if (!known) continue;
		fill_gone();
		u32 t = g_have[l];
		if (t < g_gen) t = g_gen;
		while (g_have[l] < k->spect_gen || g_have[l] < t) {   /* nothing pressed up to where it comes in */
			u32 slot = g_have[l] % RING;
			memset(g_in[l][slot], 0, 0x30);
			g_in_at[l][slot] = g_have[l] + 1;
			g_have[l]++;
		}
		t = g_have[l];
		g_absent &= ~(1u << l);
		g_on_from[l] = t;
		g_off_from[l] = 0xffffffffu;
		k->joining = 0;
		k->ready = 0;
		log_msg("net: %s (player %d) comes into the game at tick %d (now %d), with %d players", mp_name(l), l + 1, t, g_ticks,
		        bits(present_mask()));
	}
}

/* host: the replay of player l's PC came out differently: it comes in the old way, at the next
 * reload (a warm-up starts again for it; a match takes it at its end) */
static void catchup_failed(int l)
{
	Link *k = &g_link[l];
	if (k->no_catchup) return;
	log_msg("net: %s (player %d) couldn't catch up (its replay came out differently): it comes in at the next reload",
	        mp_name(l), l + 1);
	k->no_catchup = 1;
	k->joining = 0;
	k->waiting = 1;
	g_absent |= l < (int)g_nplayers ? 1u << l : 0;
	g_last_join = GetTickCount();
}

/* The host: from a joiner (link l, -1 for an address it doesn't know yet) */
static void host_hello(int l, const PktHello *p, const SOCKADDR_IN *from)
{
	static u32 warned_level;
	u32 bad;

	if (p->level != g_level) {
		if (warned_level != p->level) log_msg("net: a joiner is on level %d, this is level %d", p->level, g_level);
		warned_level = p->level;
		return;
	}
	if (l >= 0) g_link[l].last_recv = GetTickCount();
	/* removed by the server's admin: it hears why, nothing else. By IP address (a new port is a
	 * restart away), but only for someone new or the removed link itself: players already in the
	 * game behind the same address (one router, CGNAT, this PC) stay */
	if ((l < 0 || gone(l)) && ban_note(from->addr)) {
		g_lobby_note = ban_note(from->addr);
		send_lobby_addr(from, 0, 0, NETBAD_KICKED);
		g_lobby_note = 0;
		if (l > 0 && (g_state == S_LOBBY || g_link[l].waiting || g_link[l].joining)) remove_link(l);
		return;
	}
	if (l > 0 && !p->phase && p->catchup_failed && g_link[l].joining) catchup_failed(l);
	if (g_state == S_LOBBY || (!p->phase && (l < 0 || g_link[l].waiting || g_link[l].joining == 1))) {
		if (p->phase) return;   /* it reloaded already; it says phase 0 again soon or waits */
		bad = compat_problems(&p->c);
		if (bad) {   /* not the same game: it stays out of the lobby, and hears why */
			u32 now = GetTickCount();
			refuse("joiner", bad, &p->c);
			if (!(bad & ~(NETBAD_LEVEL0 | NETBAD_MAP))) {   /* things the joiner may fix by itself */
				if (!g_fixing_since || now - g_fixing_last > FIXING_MS) g_fixing_since = now;
				g_fixing_last = now;
				g_problem_hidden = now - g_fixing_since < FIXING_MS;
			}
			send_lobby_addr(from, l > 0 ? l : 0, 0, bad);
			if (l > 0) remove_link(l);
			return;
		}
		if (l < 0) {
			const u8 *ip = (const u8 *)&from->addr;
			if ((l = g_state == S_LOBBY ? (places_taken() >= capacity() ? -1 : add_link(from)) : free_place(from)) < 0) {
				refuse("joiner", NETBAD_FULL, 0);
				send_lobby_addr(from, 0, 0, NETBAD_FULL);
				return;
			}
			log_msg("net: %d.%d.%d.%d:%d joined the lobby as player %d (%d of %d)", ip[0], ip[1], ip[2], ip[3], ntohs(from->port),
			        l + 1, places_taken(), capacity());
			check_files("joiner", &p->c);
			g_problem = 0;
			if (g_state != S_LOBBY) {
				if (catchup_possible(l)) start_joining(l);
				else g_link[l].waiting = 1;
			}
		}
		if (g_link[l].ready != (int)p->ready)
			log_msg("net: %s (player %d) is %s", mp_name(l), l + 1, p->ready ? "ready" : "not ready");
		g_link[l].ready = p->ready;
		send_lobby(l, 0);
		return;
	}
	if (l < 1) return;
	if (!p->phase && (gone(l) || (g_state == S_RUN && g_warm) || g_link[l].joining == 2)) {   /* it's back in its lobby */
		if (!gone(l) && !g_link[l].joining) mark_gone(l, 0, "went back to its lobby", 0);
		if (catchup_possible(l)) {   /* it joins the session again, as it goes on */
			g_link[l].entered = 0;   /* "entered the game" again with its name */
			start_joining(l);
			send_lobby(l, 0);
			return;
		}
		if (!g_link[l].rejoin) g_last_join = GetTickCount();   /* the next reload takes it */
		g_link[l].rejoin = 1;
		g_link[l].joining = 0;
		return;
	}
	if (gone(l) || !p->phase) return;   /* S_WAIT sends "go" again by itself */
	if (p->phase == 3) {   /* joining: still catching up, or caught up to live_from */
		if (g_link[l].joining == 1 && p->live_from) joining_live(l, p->live_from);
		return;
	}
	if (g_link[l].joining) return;
	if (p->phase == 2) {   /* its warm-up: the ready flag */
		if (g_state != S_RUN || !g_warm) return;
		if (g_link[l].ready != (int)p->ready)
			log_msg("net: %s (player %d) is %s", mp_name(l), l + 1, p->ready ? "ready" : "not ready");
		g_link[l].ready = p->ready;
		return;
	}
	if (g_state == S_WAIT) {
		bad = compat_problems(&p->c);
		if (bad) {   /* changed since the lobby: no START, it doesn't play */
			refuse("joiner", bad, &p->c);
			return;
		}
		if (!g_link[l].arrived) {
			const u8 *ip = (const u8 *)&from->addr;
			g_link[l].arrived = 1;
			log_msg("net: %d.%d.%d.%d:%d (player %d) is in (input delay %d ticks there)", ip[0], ip[1], ip[2], ip[3],
			        ntohs(from->port), l + 1, p->delay);
			check_start("joiner", p->start_tick, p->state_hash, p->fpu_cw, &p->c);
		}
		for (int k = 1; k < (int)g_nplayers; k++)
			if (!gone(k) && !g_link[k].arrived) return;   /* the others are still loading */
		host_begin();
		return;
	}
	send_start(l);   /* again for every HELLO: the first START may be lost */
}

static void joiner_lobby(const PktLobby *p)
{
	int go = p->go && p->session != g_go_taken;   /* a repeat of a reload already done means nothing */

	g_link[0].last_recv = GetTickCount();
	if (p->refused & NETBAD_KICKED) {   /* the server's admin removed this PC (see "Dedicated server") */
		memcpy(g_kick_note, p->note, sizeof g_kick_note);
		g_kick_note[sizeof g_kick_note - 1] = 0;
		clean_text(g_kick_note, sizeof g_kick_note);
		if (!g_kick_note[0]) str_cpy(g_kick_note, sizeof g_kick_note, "The server removed you.");
		if (g_state != S_LOBBY && g_state != S_IDLE) {
			log_msg("net: removed by the server: %s", g_kick_note);
			chat_server(g_kick_note);
			back_to_lobby("removed by the server");
			return;
		}
		refuse("host", NETBAD_KICKED, 0);
		return;
	}
	if (!p->refused && (p->dedicated != 0) != g_ded_host) {   /* before any level of its sessions loads (players.c) */
		g_ded_host = p->dedicated != 0;
		players_set_dedicated(g_ded_host);
		log_msg("net: the host %s a dedicated server", g_ded_host ? "is" : "isn't");
	}
	if (!p->refused) set_char_rules(p->char_rules & NET_MUTATORS_ALL, "the host's");
	if (g_state == S_WAIT || p->level != g_level) return;
	if (g_state != S_LOBBY && !go) {   /* in a synced session only the ready state and "go" count */
		if (g_warm && !p->refused) {
			g_host_heard = 1;
			g_lobby_players = p->players;
			g_lobby_ready = p->ready;
			g_lobby_countdown = p->countdown_ms;
			g_lobby_rx = g_link[0].last_recv;
		}
		return;
	}
	if (p->refused) {   /* the host keeps this PC out: nothing else in it counts */
		refuse("host", p->refused, 0);
		/* only the map differs and the host can send it: reload, downloading it first */
		/* level0 first: the map's refusal comes again after that reload. While a fix is under way
		 * the refusal is only logged: the reload checks again. */
		if (p->refused & NETBAD_LEVEL0) g_problem_hidden = level0_swap_start(p->level0);
		else if (p->refused == NETBAD_MAP && p->map_offer) g_problem_hidden = map_download_start();
		return;
	}
	if (!g_host_heard) log_msg("net: in the host's lobby as player %d", p->slot + 1);
	else if ((int)p->slot != g_slot) log_msg("net: now player %d in the host's lobby", p->slot + 1);
	g_host_heard = 1;
	g_problem = 0;
	if (p->slot >= 1 && p->slot < NPL) g_slot = (int)p->slot;
	/* the host's gameplay settings: for the warm-up, and the match's level reload */
	players_set_respawn_ticks(p->respawn_ticks);
	items_set_default_ticks(p->item_ticks);
	g_time_limit = p->time_limit;
	g_score_limit = p->score_limit;
	g_lobby_players = p->players;
	g_lobby_ready = p->ready;
	g_lobby_countdown = p->countdown_ms;
	g_lobby_rx = g_link[0].last_recv;
	g_lobby_match = p->in_match != 0;
	if (go) {
		if (p->players < 2 || p->players > NPL || (int)p->slot >= (int)p->players || p->slot < 1) {
			log_msg("net: the host's match has %d players, this PC as player %d: not playable here", p->players, p->slot + 1);
			return;
		}
		g_nplayers = p->players;
		g_match_seed = p->seed;
		g_match_local_seed = p->local_seed;
		g_go_taken = p->session;
		g_warm = p->warm != 0;
		g_catchup = 0;
		start_match();
	} else if (p->catchup && g_state == S_LOBBY && p->session != g_go_taken && p->session != g_cu_failed) {
		/* the session goes on without a reload: load it as everyone did, and catch up */
		if (p->load_players < 1 || p->load_players > NPL || p->slot < 1 || p->slot >= NPL) {
			log_msg("net: the host's session loaded with %d players, this PC as player %d: not playable here", p->load_players,
			        p->slot + 1);
			return;
		}
		g_nplayers = p->load_players;
		g_match_seed = p->seed;
		g_match_local_seed = p->local_seed;
		g_seed = p->run_seed;
		g_local_seed = p->run_local_seed;
		g_cu_start_tick = p->start_tick;
		g_cu_start_hash = p->start_hash;
		g_go_taken = p->session;
		g_warm = p->warm != 0;
		g_delay = 0;   /* (the host joins players in only with rollback) */
		g_catchup = 1;
		g_lobby_match = 0;
		log_msg("net: joining the host's %s in progress as player %d", g_warm ? "warm-up" : "match", g_slot + 1);
		start_match();
	}
}

static void joiner_start(const PktStart *p)
{
	u32 bad;
	g_link[0].last_recv = GetTickCount();
	if (g_state != S_WAIT) return;
	if (p->level != g_level) {
		log_msg("net: the host is on level %d, this is level %d", p->level, g_level);
		return;
	}
	bad = compat_problems(&p->c);
	if (bad) {   /* changed since the lobby: the match doesn't begin */
		refuse("host", bad, &p->c);
		return;
	}
	if (p->players != g_nplayers) {
		log_msg("net: the host's match has %d players, this PC loaded it for %d: not starting", p->players, g_nplayers);
		return;
	}
	check_start("host", p->start_tick, p->state_hash, p->fpu_cw, &p->c);
	g_seed = p->seed;
	g_local_seed = p->local_seed;
	g_gone = p->gone & ~(1u << g_slot);
	if (p->delay <= 30) g_delay = p->delay;   /* the host's, on every PC (0: rollback) */
	begin_run();
}

static void receive(void)
{
	static u8 buf[2048];
	SOCKADDR_IN from;
	int len, n, l;

	for (;;) {
		len = sizeof from;
		n = recvfrom(g_sock, buf, sizeof buf, 0, &from, &len);
		if (n < 0) {
			if (WSAGetLastError() == 10054) continue;   /* WSAECONNRESET: an earlier send bounced */
			return;
		}
		if (stun_packet(buf, (u32)n, &from)) continue;   /* this PC's internet address (stun.c) */
		if (helper_packet(g_sock, buf, (u32)n, &from)) continue;   /* the server list and introductions (helper.c) */
		const PktHead *h = (const PktHead *)buf;
		if ((u32)n < 8 || h->magic != MAGIC) {
			stranger(&from, "not Arklite's");
			continue;
		}
		g_npackets_recv++;
		if (h->type == PKT_INFO_REQ && g_mode == NET_HOST) {   /* the Join list: anyone may ask */
			if (h->proto == PROTO ? (u32)n >= sizeof(PktInfo) : (u32)n >= 16) answer_info(buf, (u32)n, &from);
			continue;
		}
		l = link_of(&from);
		if (g_mode == NET_JOIN && l != 0) {   /* only the host talks to a joiner */
			stranger(&from, "not the host");
			continue;
		}
		if (g_state == S_IDLE) continue;   /* not in a level yet: the others will resend */
		if (h->proto != PROTO) {   /* another Arklite: its packets can't be read */
			if (g_mode == NET_HOST && l < 0 && h->type != PKT_HELLO) {
				stranger(&from, "another protocol");
				continue;
			}
			if (!(g_problem & NETBAD_PROTO) || GetTickCount() - g_problem_at > LOST_MS)
				log_msg("net: packet of protocol %d from the %s, this is %d: use the same Arklite.dll on every PC", h->proto,
				        g_mode == NET_HOST ? "joiner" : "host", PROTO);
			refuse(g_mode == NET_HOST ? "joiner" : "host", NETBAD_PROTO, 0);
			continue;
		}
		if ((u32)n < sizeof *h) {
			stranger(&from, "too short");
			continue;
		}
		if (g_mode == NET_HOST && h->key != cookie_of(&from)) {   /* see "Strangers" */
			/* a would-be joiner's request, or a player's packet whose key went wrong (its PC took a
			 * forged PKT_COOKIE): the right one back, no bigger than what came */
			if (((h->type == PKT_HELLO || h->type == PKT_MAP_LIST_REQ || h->type == PKT_MAP_REQ || h->type == PKT_L0_REQ) &&
			     (u32)n >= sizeof *h + 8) ||
			    l >= 0)
				send_cookie(&from);
			else
				stranger(&from, h->key ? "wrong key" : "no key");
			continue;
		}
		if (g_mode == NET_JOIN) {
			if (h->type == PKT_COOKIE) {
				joiner_cookie(h->key);
				continue;
			}
			if (!host_keyed(h)) {
				stranger(&from, "the host's address without this PC's key");
				continue;
			}
		}
		if (g_mode == NET_HOST && h->type == PKT_L0_REQ && (u32)n >= sizeof(PktL0Req)) {   /* see "level0 sync" */
			zs_serve_ranges((const PktL0Req *)buf, &from);
			continue;
		}
		if (g_mode == NET_HOST && (h->type == PKT_MAP_LIST_REQ || h->type == PKT_MAP_REQ)) {
			map_serve(buf, n, &from);
			continue;
		}
		if (h->type == PKT_HELLO && g_mode == NET_HOST && (u32)n >= sizeof(PktHello)) {
			host_hello(l, (const PktHello *)buf, &from);
			continue;
		}
		if (l < 0) {   /* everything else only from a PC in this lobby or match */
			stranger(&from, "not in the game");
			continue;
		}
		if (g_mode == NET_HOST && g_state != S_LOBBY && gone(l)) continue;
		if (h->type == PKT_HIST_REQ && g_mode == NET_HOST && (u32)n >= sizeof(PktHistReq)) {
			hist_serve(l, (const PktHistReq *)buf);
		} else if (h->type == PKT_HIST && g_mode == NET_JOIN && (u32)n >= sizeof(PktHist)) {
			hist_received((const PktHist *)buf, (u32)n);
		} else if (h->type == PKT_LOBBY && g_mode == NET_JOIN && (u32)n >= sizeof(PktLobby)) {
			joiner_lobby((const PktLobby *)buf);
		} else if (h->type == PKT_START && g_mode == NET_JOIN && (u32)n >= sizeof(PktStart)) {
			joiner_start((const PktStart *)buf);
		} else if (h->type == PKT_END && (u32)n >= sizeof(PktEnd)) {
			end_received(l, (const PktEnd *)buf);
		} else if (h->type == PKT_INPUT && (g_state == S_RUN || g_state == S_OVER)) {
			g_link[l].last_recv = GetTickCount();
			if (!g_ninputs_recv++) log_msg("net: first input from %s", g_mode == NET_HOST ? "a joiner" : "the host");
			on_input(l, (const PktInput *)buf, n);
		} else if (h->type == PKT_NAME && g_mode == NET_HOST && (u32)n >= sizeof(PktName)) {
			const PktName *p = (const PktName *)buf;
			char name[MP_NAME_MAX];
			memcpy(name, p->name, sizeof name);
			name[sizeof name - 1] = 0;
			clean_text(name, sizeof name);
			if (!str_ieq_n(name, g_link[l].name, MP_NAME_MAX) || str_len(name) != str_len(g_link[l].name)) {
				if (g_link[l].entered && *g_link[l].name && *name) {   /* a new name (F2 or /name) */
					char what[CHAT_MAX];
					fmt(what, "is now known as %s", name);
					server_line(g_link[l].name, what);
				}
				str_cpy(g_link[l].name, MP_NAME_MAX, name);
				mp_name_set(l, name);
				g_names_serial++;
			}
			if (!g_link[l].entered) {   /* its name comes right after it joined */
				g_link[l].entered = 1;
				server_line(mp_name(l), "entered the game");
				if (g_motd[0] && chat_link(l)) {   /* the message of the day, to that player alone */
					char t[CHAT_MAX];
					log_msg("net: message of the day to player %d", l + 1);
					fmt(t, "Server MOTD: %s", g_motd);
					chat_queue(l, CHAT_SERVER, CHAT_ALL, t);   /* (a server line goes to l's queue only) */
				}
			}
		} else if (h->type == PKT_NAMES && g_mode == NET_JOIN && (u32)n >= sizeof(PktNames)) {
			const PktNames *p = (const PktNames *)buf;
			for (int q = 0; q < NPL; q++) {
				char name[MP_NAME_MAX];
				if (q == g_slot) continue;
				memcpy(name, p->name[q], sizeof name);
				name[sizeof name - 1] = 0;
				clean_text(name, sizeof name);
				mp_name_set(q, name);
			}
		} else if ((h->type == PKT_CHAT && (u32)n >= sizeof(PktChat)) || (h->type == PKT_CHAT_ACK && (u32)n >= sizeof(PktChatAck))) {
			if (h->type == PKT_CHAT) chat_received(l, (const PktChat *)buf);
			else chat_acked(l, (const PktChatAck *)buf);
		} else if (h->type == PKT_BYE && g_mode == NET_HOST) {
			if (g_state == S_LOBBY || g_link[l].waiting || g_link[l].joining) {
				lobby_leave(l);
			} else {
				mark_gone(l, 1, g_state == S_OVER ? "left after the match" : "left", 1);
			}
		} else if (h->type == PKT_BYE && g_state == S_RUN) {
			log_msg("net: the host left; the other players stand still");
			if (g_ded_host) chat_server("The server closed.");
			else server_line(mp_name(0), "left the game");
			g_lost_left = 1;
			g_state = S_LOST;
		} else if (h->type == PKT_BYE && g_state == S_OVER && !g_over_lost) {
			log_msg("net: the host left after the match");
			if (g_ded_host) chat_server("The server closed.");
			else server_line(mp_name(0), "left the game");
			g_over_lost = 1;
		} else if (h->type == PKT_BYE && g_state == S_LOBBY && g_host_heard) {
			log_msg("net: the host left the lobby");
			if (g_ded_host) chat_server("The server closed.");
			else server_line(mp_name(0), "left the game");
			g_host_heard = 0;
		}
	}
}

/* ---- Session ------------------------------------------------------------------------------ */

static void catchup_begin(void);

static void session_begin(u32 level)
{
	CharSum *c = g_mine[0].chars;
	u32 n;

	g_level = level;
	g_state = g_match_pending ? S_WAIT : S_LOBBY;
	g_look_who = 0;   /* S_WAIT's camera starts again from the character */
	{
		u32 map = hash_map(level);
		if (map != g_map_hash) log_msg("net: level %d's files: %08x", level, map);
		g_map_hash = map;
	}
	g_ticks = 0;
	g_stall_frames = g_stall_ticks = 0;
	g_sync_skip_at = g_sync_skipped = 0;
	g_has_pending = 0;
	memset(g_in, 0, sizeof g_in);
	memset(g_in_at, 0, sizeof g_in_at);
	memset(g_have, 0, sizeof g_have);
	memset(g_last_local, 0, sizeof g_last_local);
	memset(g_mine, 0, sizeof g_mine);
	if (g_tring)
		for (u32 k = 0; k < TRING; k++) g_tring[k].tick = 0;
	memset(g_peer_ping, 0xff, sizeof g_peer_ping);
	g_last_hash_tick = g_last_hash = 0;
	g_checks = g_bad_checks = g_first_bad = 0;
	memset(g_published, 0, sizeof g_published);
	memset(g_pred, 0, sizeof g_pred);
	g_absent = 0;
	memset(g_base, 0, sizeof g_base);
	for (int p = 0; p < NPL; p++) {
		g_on_from[p] = p < (int)g_nplayers ? 0 : 0xffffffffu;
		g_off_from[p] = 0xffffffffu;
	}
	g_joining = 0;
	g_cu_abort = 0;
	g_verified = 0;
	g_rb_started = 0;
	g_rollbacks = g_resim_ticks = g_rb_failed = g_rb_deepest = g_rb_frames = 0;
	g_npackets_sent = g_npackets_recv = g_ninputs_recv = g_send_errors = 0;
	g_last_status = g_wait_since = GetTickCount();
	g_gen = g_delay;   /* ticks 0..delay-1: no input from any PC */
	g_start_tick = GS_TICK(ONgGameState);
	g_start_fpu = fpu_control_word();
	g_start_hash = summarize_with(c, &n, 0);
	if (!g_match_pending) {   /* F3 pressed before a reload into a warm-up still counts */
		g_ready = 0;
		g_warm = 0;
	}
	g_lobby_match = 0;
	g_countdown_end = 0;
	g_lobby_players = 1;
	g_lobby_ready = 0;
	g_lobby_countdown = 0xffffffffu;
	g_host_heard = 0;
	for (int l = 0; l < NPL; l++) {   /* each link starts this level from nothing */
		Link *k = &g_link[l];
		memset(k->ack, 0, sizeof k->ack);
		memset(k->theirs, 0, sizeof k->theirs);
		memset(k->checked, 0, sizeof k->checked);
		k->adv = k->adv_known = 0;
		k->stamp = k->stamp_at = k->ping_sum = k->ping_n = k->ping_max = 0;
		k->ping = -1;
		k->sent_sig = 0;
		k->arrived = k->ready = k->end_ready = 0;
		k->knows = 0;
	}
	if (g_state == S_LOBBY) g_catchup = 0;
	if (g_state == S_LOBBY) {
		g_gone = g_left = 0;
		if (g_mode == NET_HOST) {   /* the joiners come in again, in the order they say hello */
			for (int l = 1; l < NPL; l++) memset(&g_link[l], 0, sizeof g_link[l]);
			names_push();
			g_slot = 0;
		}
		log_msg("net: level %d loaded: lobby (F3: ready)%s", level, g_mode == NET_HOST ? ", waiting for players to join" : "");
	} else {
		log_msg("net: level %d reloaded for the %s (tick %d, state %08x, %d characters, %d players, this PC's is player %d); %s",
		        level, g_warm ? "warm-up" : "match", g_start_tick, g_start_hash, n, g_nplayers, g_slot + 1,
		        g_mode == NET_HOST ? "waiting for the joiners" : g_catchup ? "catching up with the host" : "joining the host");
		if (g_catchup && g_mode == NET_JOIN) catchup_begin();
	}
}

static void begin_run(void)
{
	g_state = S_RUN;
	g_match_pending = 0;
	g_load_players = g_nplayers;
	if (g_mode == NET_HOST && ROLLING) hist_reset();
	else g_hist_ok = 0;
	if (!g_warm) g_ready = 0;
	g_gen = g_delay;   /* ticks 0..delay-1: no input from any PC */
	for (int l = 0; l < NPL; l++) {
		g_link[l].last_recv = GetTickCount();
		g_link[l].end_ready = 0;
		g_link[l].ping = -1;
	}
	players_reset_scores();
	if (g_warm) {
		players_set_limits(0, 0, 0);
	} else {
		players_set_limits(1, g_time_limit * 3600, g_score_limit);
		mp_sound("objective_new");
		g_fight_at = GetTickCount() | 1;
	}
	fmt(g_session, "L%d-%08x%08x", g_level, g_seed, g_local_seed);
	log_msg("net: %s on (%s, player %d of %d), RNG %08x/%08x, input delay %d ticks; limits: %d min, %d points",
	        g_warm ? "warm-up" : "game", mode_name(), g_slot + 1, g_nplayers, g_seed, g_local_seed, g_delay, g_time_limit, g_score_limit);
}

/* ---- Joining a game in progress: the joiner ------------------------------------------------- *
 * The level just loaded as on every PC at the session's start. S_CATCHUP: the session's inputs
 * come from the host (hist_request), and each frame runs as many of their ticks as CATCHUP_US
 * allows, silently (ONgSoundOn off: which sounds play is never game state), then draws. Every
 * HASH_EVERY ticks the state is checked against the host's hash; a difference ends it (the host
 * then lets this PC in at the next reload, as before). Within LIVE_GAP of the host's last final
 * tick, catchup_live: from here on the inputs come as in any session (PKT_INPUT, rollback on),
 * from the tick reached; HELLO phase 3 says which. This PC is in sync but not in the game yet
 * (joining 2): its player has no character, so its inputs change nothing, until the host's record
 * says otherwise (joining_frame on the host). */

static void catchup_begin(void)
{
	u32 now = GetTickCount();

	if (g_start_tick != g_cu_start_tick || g_start_hash != g_cu_start_hash)
		log_msg("net: the level started differently here (tick %d, state %08x) than on the host (tick %d, state %08x): the "
		        "replay will come apart", g_start_tick, g_start_hash, g_cu_start_tick, g_cu_start_hash);
	else
		log_msg("net: level-start state matches the host's (%08x)", g_start_hash);
	g_state = S_CATCHUP;
	g_match_pending = 0;
	g_catchup = 0;
	g_joining = 1;
	g_ready = 0;
	g_cu_abort = 0;
	g_cu_began = now;
	hist_reset();
	if (!g_hist_ok) {
		back_to_lobby("no memory to catch up with");
		return;
	}
	for (int l = 0; l < NPL; l++) {
		g_link[l].last_recv = now;
		g_link[l].ping = -1;
	}
	players_reset_scores();
	if (g_warm) players_set_limits(0, 0, 0);
	else players_set_limits(1, g_time_limit * 3600, g_score_limit);
	g_cu_sound = ONgSoundOn;
	ONgSoundOn = 0;
	/* here, not in the first live frame (rolling_frame): that frame's time is time the others may
	 * wait for this PC (on Windows the roster and its textures are part of a ~1.4 s first frame) */
	hud_textures_prepare();
	fmt(g_session, "L%d-%08x%08x", g_level, g_seed, g_local_seed);
	log_msg("net: catching up with the host's %s (player %d), RNG %08x/%08x", g_warm ? "warm-up" : "match", g_slot + 1, g_seed,
	        g_local_seed);
}

/* Caught up: the session's live inputs from tick g_ticks on */
static void catchup_live(u32 now)
{
	u32 n = g_hist_nplayers > (u32)g_slot + 1 ? g_hist_nplayers : (u32)g_slot + 1;

	ONgSoundOn = g_cu_sound;
	g_state = S_RUN;
	g_joining = 2;
	g_live_from = g_ticks;
	g_nplayers = n > NPL ? NPL : n;
	for (int p = 0; p < NPL; p++) {
		memset(g_in[p], 0, sizeof g_in[p]);
		memset(g_in_at[p], 0, sizeof g_in_at[p]);
		memset(g_pred[p], 0, sizeof g_pred[p]);
		if (g_ticks)   /* the last input: what the guesses repeat until the live ones come */
			memcpy(g_in[p][(g_ticks - 1) % RING], g_hist + (g_ticks - 1) * HIST_TICK + p * 12, 0x30);
		g_have[p] = g_ticks;
		for (int l = 0; l < NPL; l++) g_link[l].ack[p] = g_ticks;
	}
	g_absent = 0;
	g_gen = g_verified = g_ticks;
	g_has_pending = 0;
	memset(g_last_local, 0, sizeof g_last_local);
	g_ninputs_recv = 0;
	g_link[0].last_recv = g_last_status = now;
	g_link[0].adv = g_link[0].adv_known = 0;
	g_rb_started = 0;   /* rollback starts at the next tick */
	log_msg("net: caught up to tick %d in %d s (the host had %d): in sync, waiting to come into the game", g_ticks,
	        (now - g_cu_began) / 1000, g_hist_avail);
	send_hello();
}

static u32 catchup_frame(u32 count, void *actions, u32 *ticks_run, u32 now)
{
	u32 t0 = counter(), total = 0, err = 0, ran;

	if ((int)(now - g_link[0].last_recv) > LOST_MS) {
		back_to_lobby("nothing from the host while catching up");
		if (ticks_run) *ticks_run = 0;
		return 0;
	}
	if (now - g_last_hello >= HELLO_MS) send_hello();
	hist_request(now);
	while (g_ticks < g_hist_n && us(counter() - t0) < CATCHUP_US) {
		u32 n = g_hist_n - g_ticks, before = g_ticks;
		if (n > 6) n = 6;
		memset(g_frames, 0, sizeof g_frames);
		for (u32 k = 0; k < n; k++)
			for (int p = 0; p < NPL; p++) memcpy(g_frames[p][k], g_hist + (g_ticks + k) * HIST_TICK + p * 12, 0x30);
		if (!g_ticks) lockstep_set_seeds(g_seed, g_local_seed);
		players_net_frame(g_frames, n);
		err = run_update(n, &ran);
		total += ran;
		if (g_ticks == before || g_cu_abort) break;
	}
	if (g_cu_abort) {   /* the replay came out differently: in the old way, at the next reload */
		g_cu_failed = g_go_taken;
		g_state = S_LOBBY;   /* (so HELLO says phase 0, with catchup_failed) */
		for (int i = 0; i < 3; i++) send_hello();
		g_state = S_CATCHUP;
		g_quiet_end = 1;   /* no goodbye: the host keeps this PC's place for the reload */
		back_to_lobby("the replay came out differently from the host's game");
		g_quiet_end = 0;
		if (ticks_run) *ticks_run = 0;
		return 0;
	}
	if (!total) {   /* waiting for the host's inputs: the game stands */
		players_net_frame(0, 0);
		err = run_update(0, ticks_run);
		wait_camera(count, actions, now);
		total = 0;
	}
	if (ticks_run) *ticks_run = total;
	if (now - g_last_status >= 5000) {
		log_msg("net: catching up: tick %d, %d of the host's %d here", g_ticks, g_hist_n, g_hist_avail);
		g_last_status = now;
	}
	if (g_ticks == g_hist_n && g_hist_avail && g_hist_n + LIVE_GAP >= g_hist_avail) catchup_live(now);
	return err;
}

/* host: every joiner of the match is in (or gone): the match starts on every PC */
static void host_begin(void)
{
	lockstep_get_seeds(&g_seed, &g_local_seed);
	begin_run();
	fill_gone();
	for (int l = 1; l < (int)g_nplayers; l++)
		if (!gone(l)) send_start(l);
}

static void session_end(const char *why)
{
	if (g_state == S_IDLE) return;
	if (!g_quiet_end && (g_state == S_RUN || g_state == S_CATCHUP || (g_state == S_OVER && !g_over_lost))) send_bye();
	if (g_state == S_CATCHUP) {
		ONgSoundOn = g_cu_sound;
		log_msg("net: stopped catching up (%s) at tick %d of %d", why, g_ticks, g_hist_avail);
	}
	g_joining = 0;
	players_set_limits(0, 0, 0);
	players_set_count(2);   /* a lobby: each PC plays alone (start_match sets the match's) */
	lockstep_net_record_end(why);
	if (g_rb_started) {
		rollback_net_end();
		rollback_impulses(1);
		ONgSoundOn = g_sound_on;
		u32 checks, checks_bad;
		rollback_check_stats(&checks, &checks_bad);
		log_msg("net: rollback: %d rollbacks re-ran %d ticks (deepest %d), %d couldn't go back far enough; %d checked by "
		        "running them twice, %d of those came out differently%s", g_rollbacks, g_resim_ticks, g_rb_deepest, g_rb_failed,
		        checks, checks_bad, checks_bad ? " (see Arklite-rollbackcheck in logs)" : "");
		g_rb_started = 0;
	}
	if (g_state == S_RUN || g_state == S_LOST || g_state == S_OVER)
		log_msg("net: level %d ended (%s) after %d ticks (%d s); state hashes: %d compared, %d differed%s",
		        g_level, why, g_ticks, g_ticks / 60, g_checks, g_bad_checks, g_bad_checks ? ", see DESYNC above" : "");
	else if (g_state == S_WAIT)
		log_msg("net: level %d ended (%s) before the match started", g_level, why);
	g_state = S_IDLE;
}

/* host: a joiner of the match that's been silent for LOST_MS is gone */
static void host_check_silence(u32 now)
{
	for (int l = NPL - 1; l >= 1; l--)
		if (g_link[l].used && (g_link[l].waiting || g_link[l].joining) && (int)(now - g_link[l].last_recv) > LOST_MS)
			lobby_leave(l);
	for (int l = 1; l < (int)g_nplayers; l++)
		if (g_link[l].used && !gone(l) && (int)(now - g_link[l].last_recv) > LOST_MS) {
			mark_gone(l, 0, "stopped answering", 1);
			report_net(REPORT_DROP, g_session, net_local_slot(), g_ticks, 0, 0, 0);
		}
}

/* ---- Input -------------------------------------------------------------------------------- */

static void fly_input(const u32 *r);

/* This frame's record, cleaned, waits for the next input made */
static void take_record(const u32 *rec)
{
	u32 r[12];
	float a, b;

	memcpy(r, rec, sizeof r);
	strip(r);
	if (ONgInvertLook) r[10] ^= 0x80000000u;   /* this PC's setting, applied before sending */
	fly_input(r);   /* a watcher's free flight (only read while flying) */
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

static int me_watching(void);   /* "Watching", below */

static void make_input(void)
{
	u32 *in = g_in[g_slot][g_gen % RING];

	if (live_menu_open() || char_menu_open() || g_dedicated) {
		memset(in, 0, 0x30);                 /* a menu is up (or a dedicated server's): our player stands */
		g_has_pending = 0;
	} else if (g_has_pending) {
		memcpy(in, g_pending, 0x30);
		g_has_pending = 0;
	} else {
		memcpy(in, g_last_local, 0x30);      /* the buttons stay held, no aim */
		in[7] = in[8] = in[9] = in[10] = 0;
	}
	/* a watcher's keys and mouse move only its own camera (free flight reads them in take_record,
	 * clicks come from the mouse itself): the others get none of them, so their guess of this
	 * record (the last one, buttons held, no mouse) is always right and a watcher never makes
	 * them roll back */
	if (me_watching()) memset(in, 0, 11 * 4);
	/* the F2 choice, a chat emote and whether this PC's player watches (players.c) */
	in[11] = char_menu_record_word() | players_emote_take() | players_spectate_word();
	if (g_mode == NET_HOST) {             /* and who has a character (players.c, "Joining and leaving") */
		u32 present = 1;
		for (int p = 1; p < NPL; p++)
			if (g_on_from[p] <= g_gen && g_gen < g_off_from[p]) present |= 1u << p;
		in[11] |= PLAYERS_PRESENT_FLAG | (present >> 1) << PLAYERS_PRESENT_SHIFT;   /* (player 0 is always there) */
	}
	memcpy(g_last_local, in, 0x30);
	g_in_at[g_slot][g_gen % RING] = g_gen + 1;
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
	err = ONrGameState_Update(n, g_frames[0], &ran);
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

/* Reload the level for the match: every PC does it, the host when its countdown ends, the
 * joiners when "go" arrives. The next frame begins a synced session (S_WAIT, then the START
 * handshake). The level loads with as many players as the match has (players_set_count), and
 * loading draws from the game RNG (a character's idle timer +0x1e4 comes from it at creation,
 * 0x4dc130), and the lobby ran each PC's RNG its own way, so every PC loads with the seeds the
 * host put in "go". */
static void start_match(void)
{
	u32 level = g_level;

	if (g_mode == NET_HOST) {
		g_nplayers = lobby_players();
		g_slot = 0;
		lockstep_get_seeds(&g_match_seed, &g_match_local_seed);
		g_match_seed ^= GetTickCount() * 0x9e3779b1u;   /* the spawn points too: new each match */
		g_go_session = g_go_session ? g_go_session + 1 : (GetTickCount() | 1);
		for (int i = 0; i < 3; i++) send_lobby_all(1);   /* and again for each HELLO that still says lobby */
	}
	log_msg("net: the %s starts: reloading level %d for %d players, this PC's is player %d (RNG %08x/%08x)",
	        g_warm ? "warm-up" : "match", level, g_nplayers, g_slot + 1, g_match_seed, g_match_local_seed);
	if (g_dedicated && g_mode == NET_HOST) server_print(g_warm ? "Warm-up (%d players)." : "The match starts (%d players).", g_nplayers - 1);
	g_match_pending = 1;
	g_quiet_end = 1;
	session_end(g_warm ? "warm-up starting" : "match starting");
	g_quiet_end = 0;
	players_set_count(g_nplayers);
	g_load_step = !g_warm ? "Starting the match" : g_mode == NET_JOIN ? "Joining the warm-up" : "Starting the warm-up";
	reload_level(level, 1);
}

/* host: everyone reloads into a warm-up (warm) or the match. The gone players go, the waiting
 * come in, and players are numbered 1.. again */
/* A session the host can play alone, so the players who come join it as it goes on: with
 * rollback (joining needs its history) on a map with spawn points for them */
static int solo_session(void) { return ROLLING && players_spawn_points() >= 2; }

static void host_go(int warm)
{
	for (int l = NPL - 1; l >= 1; l--)
		if (g_link[l].used && gone(l) && !g_link[l].rejoin) remove_link(l);
	for (int l = 1; l < NPL; l++) g_link[l].waiting = g_link[l].rejoin = g_link[l].joining = g_link[l].no_catchup = 0;
	compact_links();
	g_gone = g_left = g_absent = 0;
	if (!joiners() && !(warm && solo_session())) {
		back_to_lobby("no player left");
		return;
	}
	g_warm = warm;
	start_match();
}

/* A joiner the running session doesn't have wants in */
/* host: when a warm-up last reloaded to let a PC at each IP address in (by address, not by link:
 * one that leaves and comes back is a new link). See RELOAD_EVERY_MS. */
#define RELOAD_IPS 8
static struct { u32 addr, at; } g_reload_ip[RELOAD_IPS];

static u32 *reload_ip_at(u32 addr, int make)
{
	int oldest = 0;
	for (int i = 0; i < RELOAD_IPS; i++) {
		if (g_reload_ip[i].at && g_reload_ip[i].addr == addr) return &g_reload_ip[i].at;
		if (g_reload_ip[i].at < g_reload_ip[oldest].at) oldest = i;
	}
	if (!make) return 0;
	g_reload_ip[oldest].addr = addr;
	return &g_reload_ip[oldest].at;
}

static int newcomers(void)
{
	u32 now = GetTickCount();
	for (int l = 1; l < NPL; l++) {
		Link *k = &g_link[l];
		u32 *at;
		if (!k->used || !(k->waiting || k->rejoin)) continue;
		if ((at = reload_ip_at(k->addr.addr, 0)) && now - *at < RELOAD_EVERY_MS) {
			if (!k->reload_held++)
				log_msg("net: %s (player %d) wants in again %d s after the last reload for its address: it waits for the next one",
				        mp_name(l), l + 1, (now - *at) / 1000);
			continue;
		}
		return 1;
	}
	return 0;
}

/* The lobby's and the warm-up's frame: F3, HELLO or LOBBY, and on the host what starts next.
 * 1: the level is being reloaded */
static int ready_frame(u32 now)
{
	int f3 = f3_pressed();

	/* a spectator doesn't play: no F3, and /spectate takes a "ready" back (in a session: the lobby
	 * before one has no ready-up that counts, and mp_hud.c shows the F3 line there as ever) */
	if (players_spectate_wanted() && g_state != S_LOBBY) {
		f3 = 0;
		if (g_ready) {
			g_ready = 0;
			log_msg("net: not ready (spectating)");
			if (g_mode == NET_JOIN) send_hello();
			else send_lobby_all(0);
		}
	}
	if (f3) {
		g_ready = !g_ready;
		log_msg("net: %s", g_ready ? "ready" : "not ready");
		if (g_mode == NET_JOIN) send_hello();
		else send_lobby_all(0);
	}
	if (g_mode == NET_JOIN) {
		char a[32], t[CHAT_MAX];
		if (now - g_last_hello >= HELLO_MS) send_hello();
		/* the host never answered: maybe it's behind a NAT that only opens with /punch */
		if (g_state == S_LOBBY && !g_host_heard && !g_key && !g_problem && now - g_opened_at >= (g_helper_knows > 0 ? HINT_HELPED_MS : HINT_AFTER_MS) &&
		    stun_text(a) &&
		    !str_ieq_n(a, g_hint_addr, sizeof g_hint_addr)) {
			str_cpy(g_hint_addr, sizeof g_hint_addr, a);
			str_cpy(t, sizeof t, "The host doesn't answer. If it's outside your network, it can let you in with /punch and your /ip");
			chat_server(t);
		}
		if (g_state == S_LOBBY && g_host_heard && (int)(now - g_link[0].last_recv) > LOST_MS) {
			log_msg("net: nothing from the host for %d s", LOST_MS / 1000);
			if (g_ded_host) chat_server("The server stopped answering.");
			else server_line(mp_name(0), "left the game");
			g_host_heard = 0;
		}
		return 0;
	}
	if (g_state == S_LOBBY)
		for (int l = NPL - 1; l >= 1; l--)
			if (g_link[l].used && (int)(now - g_link[l].last_recv) > LOST_MS) lobby_leave(l);
	u32 players = ready_players(), ready = lobby_ready();
	int enough = g_state != S_LOBBY && players >= 2 && ready * 100 >= players * 51;
	if (enough && !g_countdown_end) {
		g_countdown_end = now + COUNTDOWN_MS;
		if (!g_countdown_end) g_countdown_end = 1;
		log_msg("net: %d of %d players ready: the match starts in %d s", ready, players, COUNTDOWN_MS / 1000);
		send_lobby_all(0);
	} else if (!enough && g_countdown_end) {
		g_countdown_end = 0;
		log_msg("net: countdown stopped (%d of %d players ready)", ready, players);
		send_lobby_all(0);
	}
	if (g_countdown_end && (int)(now - g_countdown_end) >= 0) {
		host_go(0);
		return 1;
	}
	if ((g_state == S_LOBBY ? (solo_session() ? lobby_players() >= 1 : players >= 2) : newcomers()) && !g_countdown_end &&
	    now - g_last_join >= WARM_WAIT_MS) {
		log_msg("net: %d player%s: the warm-up %s%s", players, players == 1 ? "" : "s",
		        g_state == S_LOBBY ? "starts" : "starts again for the new player",
		        g_state == S_LOBBY && solo_session() ? " (players who come join it as it goes on)" : "");
		if (g_state != S_LOBBY)
			for (int l = 1; l < NPL; l++)
				if (g_link[l].used && (g_link[l].waiting || g_link[l].rejoin)) {
					*reload_ip_at(g_link[l].addr.addr, 1) = now ? now : 1;
					g_link[l].reload_held = 0;
				}
		host_go(1);
		return 1;
	}
	for (int l = 1; l < NPL; l++)
		if (g_link[l].used && now - g_link[l].last_lobby >= HELLO_MS) send_lobby(l, 0);
	return 0;
}

static u32 lobby_frame(u32 count, void *actions, u32 *ticks_run, u32 now)
{
	u8 *gs = ONgGameState;
	u32 err, n, wanted;

	if (ready_frame(now) || g_state != S_LOBBY) {   /* or "go" came in receive(): the level is being reloaded */
		if (ticks_run) *ticks_run = 0;
		return 0;
	}

	/* alone: this PC's player plays, the others stand (a joiner's is player 2 here) */
	if (g_mode == NET_HOST) {
		players_net_frame(0, 0);
		if ((count & 0xffff) && actions) ((u32 *)actions)[11] |= players_emote_take();   /* Konoko's first record */
		err = ONrGameState_Update(count, actions, ticks_run);
	} else {
		wanted = GS_SERVER_TIME(gs) - GS_TICK(gs);
		n = wanted > 6 ? 6 : wanted;
		memset(g_frames, 0, sizeof g_frames);
		for (u32 k = 0; k < n; k++) {
			u32 *f = g_frames[1][k];
			if (k == 0 && (count & 0xffff) && actions) {
				memcpy(f, actions, 0x30);
				strip(f);
				memcpy(g_last_local, f, 0x30);
			} else {
				memcpy(f, g_last_local, 0x30);   /* the buttons stay held, no aim */
				f[7] = f[8] = f[9] = f[10] = 0;
			}
			if (live_menu_open() || char_menu_open()) memset(f, 0, 0x30);
			f[11] = char_menu_record_word() | (k == 0 ? players_emote_take() : 0);
		}
		GS_SYNC_BEHIND(gs) = 0;
		players_net_frame(g_frames, n);
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
	for (int p = 0; p < NPL; p++) h->ping_ms[p] = -1;
	h->state = g_mode == NET_OFF ? NETHUD_OFF : g_state == S_LOBBY ? NETHUD_LOBBY :
	           g_state == S_WAIT || g_state == S_CATCHUP ? NETHUD_STARTING :
	           g_state == S_RUN || g_state == S_LOST || g_state == S_OVER ? NETHUD_MATCH : NETHUD_OFF;
	if (g_mode == NET_JOIN && (g_state == S_CATCHUP || (g_state == S_RUN && g_joining == 2))) {
		h->joining = g_state == S_CATCHUP ? 1 : 2;
		h->joining_pct = joining_pct();
	}
	h->local_player = g_mode == NET_JOIN ? g_slot : 0;
	h->local_char = local_char();
	h->joiner = g_mode == NET_JOIN;
	h->ready = g_ready;
	h->warmup = g_warm && (g_state == S_WAIT || g_state == S_RUN || g_state == S_CATCHUP);
	h->match_on = g_state == S_LOBBY && g_lobby_match;
	h->countdown_ms = -1;
	if (g_mode == NET_OFF) return;
	h->present = present_mask() | (1u << h->local_player);
	if (g_ded_host) h->present &= ~1u;   /* the dedicated server's own player isn't one */
	/* the host never answered: this PC's address for its /punch (see "Hole punching") */
	if (g_mode == NET_JOIN && g_state == S_LOBBY && !g_host_heard && !g_problem && now - g_opened_at >= HINT_AFTER_MS)
		stun_text(h->punch_me);
	if (g_mode == NET_HOST) {
		h->connected = 1;
		h->players = ready_players();
		h->ready_count = lobby_ready();
		if (g_countdown_end) h->countdown_ms = (int)(g_countdown_end - now) > 0 ? (int)(g_countdown_end - now) : 0;
		for (int l = 1; l < NPL; l++)
			if (g_link[l].used) h->ping_ms[l] = g_link[l].ping;
	} else {
		h->connected = g_host_heard || g_state != S_LOBBY;
		h->players = g_lobby_players;
		h->ready_count = g_lobby_ready;
		if (g_lobby_countdown != 0xffffffffu) {
			int left = (int)g_lobby_countdown - (int)(now - g_lobby_rx);
			h->countdown_ms = left > 0 ? left : 0;
		}
		for (int p = 1; p < NPL; p++) h->ping_ms[p] = g_peer_ping[p];
		h->ping_ms[0] = g_link[0].ping;   /* the host's row: this PC's own round trip to it */
		h->ping_ms[g_slot] = -1;
	}
	h->needed = (h->players * 51 + 99) / 100;
	if (h->needed < 2) h->needed = 2;
	if (g_problem && !g_problem_hidden && g_problem_at && now - g_problem_at < PROBLEM_MS) h->problem = g_problem;
	h->desynced = (g_state == S_RUN || g_state == S_LOST) && g_bad_checks;
	if (g_state != S_LOBBY) {
		h->gone = g_gone;
		h->left = g_left;
	}
	if (g_state == S_RUN) {
		for (int l = 0; l < NPL; l++) {
			int quiet = (int)(now - g_link[l].last_recv);
			if (live(l) && (g_mode == NET_JOIN || (l < (int)g_nplayers && !g_link[l].joining)) && quiet > 1500 &&
			    quiet > h->silent_ms) {
				h->silent_ms = quiet;
				h->silent_who = l;
			}
		}
	}
	if (g_state == S_LOST) h->lost = g_lost_left ? 2 : 1;
	if (g_fight_at && g_state == S_RUN && !g_warm && now - g_fight_at < FIGHT_MS) h->fight_ms = (int)(FIGHT_MS - (now - g_fight_at));
	else g_fight_at = 0;
	h->time_limit = g_time_limit;
	h->score_limit = g_score_limit;
	if (g_state != S_RUN && g_state != S_OVER)
		for (int p = 0; p < NPL; p++) h->ping_ms[p] = -1;
	if (g_state == S_OVER) {
		h->over = 1;
		h->over_ready = g_end_ready;
		if (g_mode == NET_HOST) {
			h->over_players = end_players();
			h->over_ready_count = end_ready_count();
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

/* The inputs of ticks g_ticks.. g_ticks+n-1 into the frame records: ours, and each other
 * player's real one or, past what's here of it, a guess: its last real input with its buttons
 * held and no stick or mouse movement (as make_input repeats ours). The input each other player's
 * tick runs with goes to g_pred. */
static void rb_fill_frames(u32 n)
{
	static u32 guess[NPL][12];
	for (int p = 0; p < (int)g_nplayers; p++) {
		if (!remote(p)) continue;
		if (g_have[p]) memcpy(guess[p], g_in[p][(g_have[p] - 1) % RING], sizeof guess[p]);
		else memset(guess[p], 0, sizeof guess[p]);
		guess[p][7] = guess[p][8] = guess[p][9] = guess[p][10] = 0;
		guess[p][11] &= ~PLAYERS_EMOTE_MASK;   /* an emote is one tick's */
	}
	memset(g_frames, 0, sizeof g_frames);
	for (u32 k = 0; k < n; k++) {
		u32 t = g_ticks + k, slot = t % RING;
		for (int p = 0; p < (int)g_nplayers; p++) {
			const u32 *in = !remote(p) || t < g_have[p] ? g_in[p][slot] : guess[p];
			if (remote(p)) memcpy(g_pred[p][slot], in, 0x30);
			memcpy(g_frames[p][k], in, 0x30);
		}
	}
}

/* Run the ticks again from g_ticks (just put back) up to to, silently */
static void rb_resimulate(u32 to)
{
	rollback_impulses(0);   /* one-shot sounds play only if the first run didn't play them; ambients start again */
	while (g_ticks < to) {
		u32 n = to - g_ticks, before = g_ticks;
		if (n > 6) n = 6;
		rb_fill_frames(n);
		players_net_frame(g_frames, n);
		run_update(n, 0);
		if (g_ticks == before) break;   /* the update ran nothing: don't spin */
	}
	rollback_impulses(1);
}

/* rollback.c's check of a rollback: the same ticks put back and run once more with the same
 * inputs, the two runs compared (anything that differs is state a rollback leaves behind) */
#define RB_CHECK_FIRST 40   /* every rollback of a match's first 40 */
#define RB_CHECK_EVERY 16   /* then one in 16 */

static void rb_check(u32 t, u32 to, int player)
{
	char what[120];
	if (g_ticks != to || !rollback_check_begin()) return;
	if (!rollback_restore_tick(t)) return;
	g_ticks = t;
	rb_resimulate(to);
	if (g_ticks != to) return;   /* the update ran fewer ticks: nothing to compare */
	fmt(what, "rollback %d, player %d's input at tick %d guessed wrong, ticks %d-%d run again", g_rollbacks,
	    player + 1, t, t, to - 1);
	rollback_check_end(what);
}

/* Time sync: how many ticks this PC runs ahead of the others in real time (a joiner: of the
 * host; the host: of the joiner furthest ahead), 0 when not known */
static int ahead(void)
{
	int best = 0x7fffffff, any = 0;
	for (int l = 0; l < NPL; l++) {
		Link *k = &g_link[l];
		if (!live(l) || !k->adv_known || (g_mode == NET_HOST && (l >= (int)g_nplayers || gone(l) || k->joining))) continue;
		int a = ((int)g_ticks - (int)g_have[l] - k->adv) / 2;
		if (a < best) best = a;
		any = 1;
	}
	return any ? best : 0;
}

static const char *others(void) { return g_mode == NET_HOST ? "joiners" : "others"; }

/* The 30 s log line's ping part: each link's average and worst */
static void ping_log(char *out, u32 size)
{
	out[0] = 0;
	for (int l = 0; l < NPL; l++) {
		Link *k = &g_link[l];
		char one[64];
		if (!live(l)) continue;
		fmt(one, "%splayer %d %d ms (max %d)", out[0] ? ", " : "", g_mode == NET_HOST ? l + 1 : 1,
		    k->ping_n ? (int)(k->ping_sum / k->ping_n) : -1, k->ping_max);
		if (str_len(out) + str_len(one) < size) fmt(out + str_len(out), "%s", one);
		k->ping_sum = k->ping_n = k->ping_max = 0;
	}
}

static u32 rolling_frame(u32 count, void *actions, u32 *ticks_run, u32 wanted, u32 now)
{
	/* the inputs that came in: were the guesses right? From the first wrong one on, again */
	u32 bad = 0xffffffffu;
	int bad_player = 0;
	for (int p = 0; p < (int)g_nplayers; p++) {
		if (!remote(p)) continue;
		u32 upto = g_have[p] < g_ticks ? g_have[p] : g_ticks;
		for (u32 t = g_verified; t < upto && t < bad; t++)
			if (!same_input(g_pred[p][t % RING], g_in[p][t % RING])) {
				bad = t;
				bad_player = p;
				break;
			}
	}
	if (bad != 0xffffffffu) {
		u32 t = bad, to = g_ticks, t0 = counter();
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
			if (g_rollbacks <= RB_CHECK_FIRST || !(g_rollbacks % RB_CHECK_EVERY)) rb_check(t, to, bad_player);
		} else if (!g_rb_failed++) {
			log_msg("net: rollback: tick %d isn't kept any more (now %d): this game will come apart", t, g_ticks);
		}
	}
	{
		u32 m = remote_min();
		g_verified = m < g_ticks ? m : g_ticks;
	}
	publish_verified();

	/* this frame's ticks: as many as the clock asks, one fewer now and then while ahead of the
	 * others (time sync), and never further past the last real input of any player than the
	 * snapshots reach */
	u32 n = wanted, avail = remote_min();
	if (g_joining == 2 && g_ticks + 2 < avail) n = avail - g_ticks;   /* just caught up: through the inputs that came */
	if (n > 6) n = 6;
	if (n && ahead() >= 2 && (int)(g_ticks - g_sync_skip_at) >= SYNC_EVERY) {
		n--;
		g_sync_skip_at = g_ticks;
		g_sync_skipped++;
	}
	if (g_ticks + n > avail + RB_AHEAD) {
		u32 can = avail + RB_AHEAD > g_ticks ? avail + RB_AHEAD - g_ticks : 0;
		g_stall_frames++;
		g_stall_ticks += n - can;
		n = can;
	}
	if ((count & 0xffff) && actions) take_record((const u32 *)actions);
	while (g_gen < g_ticks + n) make_input();
	if (g_mode == NET_HOST) {
		joining_frame();
		hist_append();
	}
	send_inputs_due(now);
	if (now - g_last_stall_log >= STALL_LOG_MS) {
		char pings[200];
		ping_log(pings, sizeof pings);
		if (g_stall_frames)
			log_msg("net: in the last %d s, %d frames waited for the %s' inputs (%d ticks in all)",
			        STALL_LOG_MS / 1000, g_stall_frames, others(), g_stall_ticks);
		if (g_rb_started)
			log_msg("net: rollback so far: %d rollbacks, %d ticks run again (deepest %d), %d pages kept per tick, "
			        "%d ambient sounds stopped; time sync left out %d ticks, now %d ahead of the %s; ping %s; "
			        "a rollback took %d.%d ms on average, at most %d.%d ms (putting back at most %d.%d ms)",
			        g_rollbacks, g_resim_ticks, g_rb_deepest, rollback_known_pages(), rollback_ambients_stopped(),
			        g_sync_skipped, ahead(), others(), pings[0] ? pings : "-",
			        g_rb_timed ? g_rb_us_sum / g_rb_timed / 1000 : 0, g_rb_timed ? g_rb_us_sum / g_rb_timed / 100 % 10 : 0,
			        g_rb_us_max / 1000, g_rb_us_max / 100 % 10, g_rb_restore_us_max / 1000, g_rb_restore_us_max / 100 % 10);
		g_rb_us_sum = g_rb_us_max = g_rb_restore_us_max = g_rb_timed = 0;
		g_stall_frames = g_stall_ticks = 0;
		g_last_stall_log = now;
	}
	if (n) {
		g_last_status = now;
	} else if (now - g_last_status >= 1000) {
		char line[160] = "";
		for (int p = 0; p < (int)g_nplayers; p++) {
			char one[40];
			if (!remote(p)) continue;
			fmt(one, "%splayer %d's up to %d", line[0] ? ", " : "", p + 1, g_have[p]);
			if (str_len(line) + str_len(one) < sizeof line) fmt(line + str_len(line), "%s", one);
		}
		log_msg("net: waiting at tick %d: inputs %s; packets sent %d, received %d", g_ticks, line, g_npackets_sent,
		        g_npackets_recv);
		g_last_status = now;
	}

	rb_fill_frames(n);
	u32 first_t0 = 0, tex_us = 0;
	if (n && !g_rb_started) {   /* the match's first tick (or a joining PC's first live one) */
		first_t0 = counter();
		if (!g_ticks) lockstep_set_seeds(g_seed, g_local_seed);
		g_sound_on = ONgSoundOn;
		hud_textures_prepare();   /* a rewind mustn't see them made */
		tex_us = us(counter() - first_t0);
		g_rb_started = rollback_net_begin();
		log_msg("net: rollback %s: no input delay, the %s' inputs guessed and corrected up to %d ticks back%s",
		        g_rb_started ? "on" : "FAILED", others(), RB_AHEAD, g_record ? " (recording is off with rollback)" : "");
	}
	players_net_frame(g_frames, n);
	u32 err = run_update(n, ticks_run);
	if (first_t0)
		log_msg("net: rollback's first frame took %d ms (%d ticks; the HUD's textures %d ms)", us(counter() - first_t0) / 1000, n,
		        tex_us / 1000);
	return err;
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
	stun_frame(g_sock, now);
	helper_frame(g_sock, now);
	if (g_mode == NET_HOST) helper_frame_host(now);
	else helper_frame_join(now);
	punch_frame(now);
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
	if (g_mode == NET_HOST) {   /* what the router said to upnp.c's request, in the host's chat */
		const char *m = upnp_take_message();
		char a[32], t[CHAT_MAX];
		if (m) chat_server(m);
		/* No address on screen unless asked (/ip): players stream their games. With the helper its
		 * own line says how players get in; without it, how /punch helps. */
		if (stun_take_change() && stun_text(a) && !helper_on() && !g_punch_said) {   /* see "Hole punching" */
			g_punch_said = 1;
			str_cpy(t, sizeof t, "Players outside your network may need /punch to get in (see /help)");
			chat_server(t);
		}
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
	now = GetTickCount();   /* after receive(), which stamps last_recv: now - last_recv mustn't wrap */
	send_names(now);
	chat_resend_due(now);
	u32 wanted = GS_SERVER_TIME(gs) - GS_TICK(gs);
	if (wanted > 6) wanted = 6;

	if (g_state == S_IDLE) {   /* "go" arrived: the level is being reloaded for the match */
		if (ticks_run) *ticks_run = 0;
		return 0;
	}
	if (g_state == S_LOBBY) return lobby_frame(count, actions, ticks_run, now);
	if (g_state == S_CATCHUP) return catchup_frame(count, actions, ticks_run, now);
	if (g_joining == 2) {   /* in sync: until the host's inputs come, it says from where; then until it's in */
		if (!g_ninputs_recv && now - g_last_hello >= HELLO_MS) send_hello();
		if (players_char(g_slot)) {
			g_joining = 0;
			log_msg("net: in the game as player %d (tick %d)", g_slot + 1, g_ticks);
		}
	}

	if (g_state == S_WAIT) {
		if (g_mode == NET_JOIN && now - g_last_hello >= HELLO_MS) send_hello();
		if (g_mode == NET_JOIN && now - g_wait_since >= 3 * LOST_MS) {   /* the host gave up on this PC */
			back_to_lobby("nothing from the host");
			if (ticks_run) *ticks_run = 0;
			return 0;
		}
		if (g_mode == NET_HOST)   /* until each is in: whatever state "go" missed it in, it sends no phase 1 */
			for (int l = 1; l < (int)g_nplayers; l++)
				if (g_link[l].used && !gone(l) && !g_link[l].arrived && now - g_link[l].last_lobby >= HELLO_MS) send_lobby(l, 1);
		if (g_mode == NET_HOST && g_nplayers == 1) {   /* alone: no one to wait for */
			host_begin();
		} else if (g_mode == NET_HOST && now - g_wait_since >= LOST_MS) {   /* the ones still loading don't play */
			int any = 0;
			for (int l = 1; l < (int)g_nplayers; l++) {
				if (!g_link[l].arrived) mark_gone(l, 0, "didn't load the match in time", 0);   /* still in the lobby */
				any |= !gone(l);
			}
			if (!any) {
				back_to_lobby("no joiner came to the match");
				if (ticks_run) *ticks_run = 0;
				return 0;
			}
			host_begin();
		} else if (now - g_last_status >= 5000) {
			log_msg("net: still waiting for the %s (packets sent %d, received %d)", g_mode == NET_HOST ? "joiners" : "host",
			        g_npackets_sent, g_npackets_recv);
			g_last_status = now;
		}
		if (g_state == S_WAIT) {
			players_net_frame(0, 0);
			u32 err = run_update(0, ticks_run);   /* the clock stands until the game starts */
			wait_camera(count, actions, now);
			return err;
		}
	}

	if (g_mode == NET_HOST && (g_state == S_RUN || g_state == S_OVER)) host_check_silence(now);
	if (g_state == S_RUN && g_warm && (ready_frame(now) || g_state != S_RUN)) {
		if (ticks_run) *ticks_run = 0;
		return 0;
	}
	if (g_mode == NET_HOST) fill_gone();
	if (g_mode == NET_JOIN && g_state == S_RUN && (int)(now - g_link[0].last_recv) > LOST_MS) {
		log_msg("net: nothing from the host for %d s; the other players stand still (packets sent %d, received %d, inputs received %d)",
		        LOST_MS / 1000, g_npackets_sent, g_npackets_recv, g_ninputs_recv);
		if (g_ded_host) chat_server("The server stopped answering.");
		else server_line(mp_name(0), "left the game");
		g_lost_left = 0;
		g_state = S_LOST;
		report_net(REPORT_DROP, g_session, net_local_slot(), g_ticks, 0, 0, 0);
	}

	if (g_state == S_LOST && g_warm) {   /* nothing to finish: find the host again from the lobby */
		back_to_lobby("the host is gone");
		if (ticks_run) *ticks_run = 0;
		return 0;
	}
	if ((g_state == S_RUN || g_state == S_LOST) && over_confirmed()) enter_over(now);
	if (g_state == S_OVER) return over_frame(count, (const u8 *)actions, now, ticks_run);

	if (g_state == S_RUN && ROLLING) return rolling_frame(count, actions, ticks_run, wanted, now);

	/* The ticks this frame runs, then this PC's inputs up to the delay past them: an input made
	 * now is always used g_delay ticks later. Making them ahead by wall-clock time instead let
	 * the queue grow (up to delay + 6 ticks) whenever the others' inputs came late, and it never
	 * shrank back. A frame that makes no input merges its record into the next one. */
	u32 remote_avail = g_state == S_LOST ? 6 : remote_min() - g_ticks;
	u32 n = wanted;
	if (n > remote_avail) {
		g_stall_frames++;
		g_stall_ticks += n - remote_avail;
		n = remote_avail;
	}
	if (n > 6) n = 6;
	if ((count & 0xffff) && actions) take_record((const u32 *)actions);
	while (g_gen < g_ticks + g_delay + n) make_input();
	if (g_mode == NET_HOST) fill_gone();
	if (n > g_gen - g_ticks) n = g_gen - g_ticks;
	if (g_state == S_RUN) send_inputs_due(now);
	if (now - g_last_stall_log >= STALL_LOG_MS) {
		if (g_stall_frames)
			log_msg("net: in the last %d s, %d frames waited for the %s' inputs (%d ticks late in all)",
			        STALL_LOG_MS / 1000, g_stall_frames, others(), g_stall_ticks);
		g_stall_frames = g_stall_ticks = 0;
		g_last_stall_log = now;
	}
	if (n || g_state != S_RUN) {
		g_last_status = now;
	} else if (now - g_last_status >= 1000) {   /* stuck waiting: say on what */
		log_msg("net: waiting at tick %d: our inputs up to %d, the %s' up to %d; packets sent %d, received %d",
		        g_ticks, g_gen, others(), remote_min(), g_npackets_sent, g_npackets_recv);
		g_last_status = now;
	}

	memset(g_frames, 0, sizeof g_frames);
	for (u32 k = 0; k < n; k++) {
		u32 slot = (g_ticks + k) % RING;
		for (int p = 0; p < (int)g_nplayers; p++)
			if (p == g_slot || g_state != S_LOST) memcpy(g_frames[p][k], g_in[p][slot], 0x30);
	}
	if (n && !g_ticks) {
		lockstep_set_seeds(g_seed, g_local_seed);
		if (g_record && g_nplayers == 2) {
			const char *path = g_mode == NET_HOST ? "Arklite-net-host.akr" : "Arklite-net-join.akr";
			if (lockstep_net_record_begin(g_level, path)) log_msg("net: recording this game to %s", path);
		} else if (g_record) {
			log_msg("net: not recording this game: recordings hold two players, this match has %d", g_nplayers);
		}
	}
	players_net_frame(g_frames, n);
	lockstep_net_record_frame(n, g_frames[0], g_frames[1]);
	u32 err = run_update(n, ticks_run);
	lockstep_net_record_frame_end();
	return err;
}

/* ---- Each PC's own view ------------------------------------------------------------------- */

/* ---- Watching (/spectate; players.c "Spectating") ------------------------------------------ *
 * A watcher starts in free flight: Arklite flies the camera itself (Oni's manual camera mode 3, so
 * no camera mode moves it, and the camera's view set with 0x4d6070 as every mode does) from this
 * PC's own input (take_record: movement keys, mouse look, jump up, crouch down, walk slow), and the
 * listener is the camera. Left click watches the next player, right click the one before, and
 * past the last (or first) one it's free flight again: a watched player is shown as a joiner's
 * own is (view_character: camera, listener, HUD, laser sight, crosshair); the camera cuts to them.
 * Only this PC's view: nothing here is game state. */
static int g_watch = -1;     /* the player this PC watches, -1: free flight */
static int g_watch_step;     /* a click asked for the next (1) or previous (-1) player */
static int g_was_watching;

static int me_watching(void)
{
	return g_mode != NET_OFF && ONgGameState && players_spectating(g_mode == NET_JOIN ? g_slot : 0);
}

/* Who can be watched: a player with a character (alive or waiting to respawn) who isn't watching */
static int watchable(int who)
{
	int me = g_mode == NET_JOIN ? g_slot : 0;
	return who >= 0 && who < NPL && who != me && !players_spectating(who) && players_char(who);
}

static int g_watch_was;   /* watching last time watched() looked: a new watch starts in free flight */

static u8 *watched(void)
{
	int step = g_watch_step;

	g_watch_step = 0;
	if (!me_watching()) {
		g_watch_was = 0;
		return 0;
	}
	if (!g_watch_was) g_watch = -1;
	g_watch_was = 1;
	if (g_watch >= 0 && !watchable(g_watch)) g_watch = -1;   /* they left or watch too: free flight */
	if (step) {   /* free flight (-1) and the players in turn */
		int at = g_watch;
		for (int k = 0; k <= NPL; k++) {
			at += step;
			if (at >= NPL) at = -1;
			if (at < -1) at = NPL - 1;
			if (at == -1 || watchable(at)) break;
		}
		g_watch = at;
	}
	return g_watch >= 0 ? players_char(g_watch) : 0;
}

static int flying(void) { return me_watching() && (g_watch < 0 || !watchable(g_watch)); }

int net_watching(void) { return !me_watching() ? -1 : flying() ? -2 : g_watch; }

/* Free flight: this PC's input since the camera last moved (take_record) */
#define CAM_MODE(c)             (*(u32 *)(c))
#define CAM_FOLLOW              1
#define CAM_MANUAL              3
#define CAM_LOC(c)              ((float *)((c) + 0x58))
#define CAM_VIEW(c)             ((float *)((c) + 0x64))
#define CAM_UP(c)               ((float *)((c) + 0x70))
#define ONrCamera_EnterManual   ((void (ONI_FC *)(void))0x4d7560)
#define ONrCamera_Activate      ((void (ONI_FC *)(u8 *))0x4d6070)   /* ECX the camera: its view goes to the renderer */
/* Quake 3's spectator (bg_pmove.c PM_FlyMove + PM_Friction + PM_Accelerate + PM_SlideMove), in
 * Oni units: its 400 units/s speed and 100 stop speed scaled by an Oni body against a Quake one
 * (about 0.4), flight acceleration 8, spectator friction 5 */
#define FLY_SPEED               160.0f
#define FLY_STOP_SPEED          40.0f
#define FLY_ACCEL               8.0f
#define FLY_FRICTION            5.0f
#define FLY_RADIUS              3.0f       /* the camera's sphere against the level */
#define FLY_LOOK                (1.0f / 128.0f)   /* radians a mouse unit (see fly_frame) */
#define FLY_SKIP                0x48800    /* quad flags a character's collision skips (call at 0x4ebddf) */
#define FLY_BUMPS               4
#define GS_COLLISION(gs)        (*(u8 **)(*(u8 **)((gs) + 0x150) + 0x40))
/* Level sphere sweep (lockstep.c "Collision query log"): ECX the level's collision, EDX the sphere
 * (x, y, z, radius), [the move], [quad flags to skip], [most results]; ret 0xc; al: hit. Results:
 * count *0x6a906c (u16), 0x48 bytes each from 0x6a9080: +0x0c the quad's plane (normal x, y, z
 * and d, facing out), +0x1c the contact point */
#define AKrCollision_Sphere     ((u8 (ONI_FC *)(void *, const float *, const float *, u32, u32))0x441cb0)
#define AKgResultCount          (*(u16 *)0x6a906c)
#define AKgResults              ((const u8 *)0x6a9080)
/* record buttons (bits of words 0-1): forward 21, backward 22, stepleft 25, stepright 26,
 * jump 27, crouch 28, walk 32 */
#define FLY_HELD(b, bit)        ((b) >> (bit) & 1)

__declspec(dllimport) int WINAPI QueryPerformanceCounter(u64 *);
__declspec(dllimport) int WINAPI QueryPerformanceFrequency(u64 *);

static float g_fly_yaw, g_fly_pitch, g_fly_dx, g_fly_dy, g_fly_vel[3];
static u64 g_fly_buttons, g_fly_last, g_fly_freq;
static int g_fly_on;          /* the camera is ours (manual mode) */

static float fsin(float x) { float r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static float fcos(float x) { float r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
static float fatan2(float y, float x) { float r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void fly_input(const u32 *r)
{
	float a, b;
	memcpy(&a, &r[9], 4);
	memcpy(&b, &r[10], 4);
	g_fly_dx += a;
	g_fly_dy += b;
	g_fly_buttons = (u64)r[0] | (u64)r[1] << 32;
}

static void fly_view(u8 *cam)
{
	float cp = fcos(g_fly_pitch), sp = fsin(g_fly_pitch), sy = fsin(g_fly_yaw), cy = fcos(g_fly_yaw);
	float *v = CAM_VIEW(cam), *u = CAM_UP(cam);
	v[0] = cp * sy; v[1] = sp; v[2] = cp * cy;
	u[0] = -sp * sy; u[1] = cp; u[2] = -sp * cy;
}

/* A hit's push-out direction: from its contact point to the sphere's center (the sphere's own
 * normal there, right for edges and corners too), or the quad's plane when the center is on the
 * point. Returns the distance, or -1 for a quad seen from behind: as in Quake, a surface only
 * blocks from its front (the side the level is played on), so a camera that starts inside a
 * wall (Oni's follow camera goes into walls and shows through them) flies out of it. */
static float fly_normal(const u8 *hit, const float *c, float *n)
{
	const float *pl = (const float *)(hit + 0x0c), *pt = (const float *)(hit + 0x1c);
	float d[3] = { c[0] - pt[0], c[1] - pt[1], c[2] - pt[2] }, len = fsqrt(dot3(d, d));

	if (dot3(pl, c) + pl[3] < -0.01f) return -1;
	if (len > 0.01f && dot3(d, pl) > 0) {
		for (int k = 0; k < 3; k++) n[k] = d[k] / len;
		return len;
	}
	for (int k = 0; k < 3; k++) n[k] = pl[k];
	return dot3(pl, c) + pl[3];
}

/* Out of anything the sphere at c touches */
static void fly_unstick(u8 *coll, float *c)
{
	static const float none[3] = { 0, 0, 0 };
	float sphere[4] = { c[0], c[1], c[2], FLY_RADIUS }, n[3], d;
	u32 count;

	if (!AKrCollision_Sphere(coll, sphere, none, FLY_SKIP, 0xff)) return;
	count = AKgResultCount;
	for (u32 i = 0; i < count && i < 0xff; i++) {
		d = fly_normal(AKgResults + i * 0x48, c, n);
		if (d >= 0 && d < FLY_RADIUS)
			for (int k = 0; k < 3; k++) c[k] += n[k] * (FLY_RADIUS + 0.01f - d);
	}
}

/* PM_SlideMove: move by vel * dt; on a hit, clip the move and the velocity along what it hit
 * (Quake's overclip, so it slides off) and try again, up to FLY_BUMPS times */
static void fly_slide(u8 *coll, float *c, float *vel, float dt)
{
	float move[3] = { vel[0] * dt, vel[1] * dt, vel[2] * dt };

	if (!coll) {
		for (int k = 0; k < 3; k++) c[k] += move[k];
		return;
	}
	fly_unstick(coll, c);
	for (int bump = 0; bump < FLY_BUMPS; bump++) {
		float sphere[4] = { c[0], c[1], c[2], FLY_RADIUS };
		u32 n;
		int clipped = 0;
		if (dot3(move, move) < 1e-8f) return;
		if (!AKrCollision_Sphere(coll, sphere, move, FLY_SKIP, 0xff)) {
			for (int k = 0; k < 3; k++) c[k] += move[k];
			return;
		}
		n = AKgResultCount;
		for (u32 i = 0; i < n && i < 0xff; i++) {
			float pl[3], into, vinto;
			if (fly_normal(AKgResults + i * 0x48, c, pl) < 0) continue;   /* from behind: no block */
			into = dot3(move, pl);
			vinto = dot3(vel, pl);
			if (into < 0) {
				for (int k = 0; k < 3; k++) move[k] -= pl[k] * into * 1.001f;
				clipped = 1;
			}
			if (vinto < 0)
				for (int k = 0; k < 3; k++) vel[k] -= pl[k] * vinto * 1.001f;
		}
		if (!clipped) {   /* nothing it goes into (only back faces, or ones it moves away from) */
			for (int k = 0; k < 3; k++) c[k] += move[k];
			return;
		}
	}
}

/* photo.c's camera: walls stop and slide it as they do a watcher's */
void net_fly_move(float *pos, float *vel, float dt)
{
	fly_slide(ONgGameState && *(u8 **)(ONgGameState + 0x150) ? GS_COLLISION(ONgGameState) : 0, pos, vel, dt);
}

/* Once a frame while flying: turn and move the camera, then hand its view to the renderer */
static void fly_frame(u8 *cam)
{
	u64 now;
	float dt, take, wish[3], wishspeed, speed, *v, *l;
	int ok = focused() && !live_menu_open() && !char_menu_open() && !chat_blocking();
	u64 b = ok ? g_fly_buttons : 0;

	if (!g_fly_freq) QueryPerformanceFrequency(&g_fly_freq);
	QueryPerformanceCounter(&now);
	if (!g_fly_on) {   /* from where the camera is, looking where it looks */
		v = CAM_VIEW(cam);
		g_fly_yaw = fatan2(v[0], v[2]);
		g_fly_pitch = fatan2(v[1], fsqrt(v[0] * v[0] + v[2] * v[2]));
		ONrCamera_EnterManual();
		g_fly_on = 1;
		g_fly_last = now;
		g_fly_dx = g_fly_dy = 0;
		g_fly_vel[0] = g_fly_vel[1] = g_fly_vel[2] = 0;
		log_msg("net: watching in free flight (look %d/1000 radian a mouse unit)", (int)(FLY_LOOK * 1000 + 0.5f));
	}
	dt = (float)(long long)(now - g_fly_last) / (float)(long long)g_fly_freq;
	g_fly_last = now;
	if (dt > 0.1f) dt = 0.1f;
	if (dt < 0) dt = 0;

	/* look: the mouse comes once a tick (60 Hz), so each tick's movement is spread over the
	 * following 1/60 s instead of jumping on the frames that get it; 1/60 radian a unit, as Oni's
	 * manual camera (signs checked in game: the record's up/down is positive for the mouse going
	 * up; Invert Look already applied by take_record). 1/128 radian a unit: aiming a pistol
	 * (Oni.exe 0x4f7be1: mouse * 1/64 (0x52b3b4) * the weapon's MouseSensitivity * 0.5, w1_tap's 1;
	 * other guns are slower, bare hands 1/64); it was 1/60, too fast to watch with (user, 2026-11-12) */
	take = dt * 60.0f;
	if (take > 1) take = 1;
	if (ok) {
		g_fly_yaw -= g_fly_dx * take * FLY_LOOK;
		g_fly_pitch += g_fly_dy * take * FLY_LOOK;
		if (g_fly_pitch > 1.55f) g_fly_pitch = 1.55f;
		if (g_fly_pitch < -1.55f) g_fly_pitch = -1.55f;
		g_fly_dx -= g_fly_dx * take;
		g_fly_dy -= g_fly_dy * take;
	} else {
		g_fly_dx = g_fly_dy = 0;
	}
	fly_view(cam);
	v = CAM_VIEW(cam);
	l = CAM_LOC(cam);

	/* PM_Friction (spectator) */
	speed = fsqrt(dot3(g_fly_vel, g_fly_vel));
	if (speed < 0.1f) {
		g_fly_vel[0] = g_fly_vel[1] = g_fly_vel[2] = 0;
	} else {
		float control = speed < FLY_STOP_SPEED ? FLY_STOP_SPEED : speed, ns = speed - control * FLY_FRICTION * dt;
		if (ns < 0) ns = 0;
		for (int k = 0; k < 3; k++) g_fly_vel[k] *= ns / speed;
	}
	/* PM_FlyMove's wish: forward along the view, sideways level, jump/crouch straight up/down */
	{
		float fwd = (float)FLY_HELD(b, 21) - (float)FLY_HELD(b, 22), side = (float)FLY_HELD(b, 26) - (float)FLY_HELD(b, 25),
		      up = (float)FLY_HELD(b, 27) - (float)FLY_HELD(b, 28);
		float rx = -v[2], rz = v[0], n = fsqrt(rx * rx + rz * rz);   /* right of the view (Oni: right of +z is -x) */
		if (n > 0.0001f) rx /= n, rz /= n;
		wish[0] = v[0] * fwd + rx * side;
		wish[1] = v[1] * fwd + up;
		wish[2] = v[2] * fwd + rz * side;
		wishspeed = fsqrt(dot3(wish, wish));
		if (wishspeed > 0.0001f) {
			float add, acc;
			for (int k = 0; k < 3; k++) wish[k] /= wishspeed;
			wishspeed = FLY_SPEED * (FLY_HELD(b, 32) ? 0.5f : 1.0f);   /* (as pm_cmdscale: a diagonal is no faster) */
			/* PM_Accelerate */
			add = wishspeed - dot3(g_fly_vel, wish);
			acc = FLY_ACCEL * dt * wishspeed;
			if (add > 0) {
				if (acc > add) acc = add;
				for (int k = 0; k < 3; k++) g_fly_vel[k] += acc * wish[k];
			}
		}
	}
	fly_slide(ONgGameState && *(u8 **)(ONgGameState + 0x150) ? GS_COLLISION(ONgGameState) : 0, l, g_fly_vel, dt);
	ONrCamera_Activate(cam);
}

static void watch_keys(void)
{
	static int ldown, rdown;
	int ok = me_watching() && focused() && !live_menu_open() && !char_menu_open() && !chat_blocking();
	int l = ok && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, r = ok && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	if (l && !ldown) g_watch_step = 1;
	else if (r && !rdown) g_watch_step = -1;
	ldown = l;
	rdown = r;
}

/* The character this PC shows: the player it watches, else a joiner's own player */
static u8 *view_character(u8 *konoko)
{
	u8 *mine;
	if (g_mode == NET_OFF || !ONgGameState || konoko != GS_PLAYER(ONgGameState)) return konoko;
	if ((mine = watched())) return mine;
	if (g_mode != NET_JOIN) return konoko;
	mine = players_char(local_char());
	return mine ? mine : konoko;
}

/* The character draw pass's visibility test (0x4e27d0, ECX the character; called at 0x4e16a4):
 * a watching host's parked Konoko is never drawn, on any PC, nor the player photo mode's "Hide
 * player" hides */
#define ONrCharacter_OnScreen   ((u8 (ONI_FC *)(u8 *))0x4e27d0)
#define ONcDraw_OnScreenCall    0x4e16a4
static u8 ONI_FC character_on_screen(u8 *c) { return players_hidden(c) || photo_hides(c) ? 0 : ONrCharacter_OnScreen(c); }

static void ONI_FC camera_update(u32 ticks, u32 edx, u32 arg)
{
	u8 *gs = ONgGameState, *cam = gs ? GS_CAMERA(gs) : 0;
	if (photo_owns_camera()) return;   /* photo.c moves it itself (single player only) */
	watch_keys();
	if (cam && flying()) {
		view_character(GS_PLAYER(gs));   /* (takes a click) */
		if (flying()) {
			if (!ticks) fly_frame(cam);   /* the frame's call (0x4fb170), not each tick's */
			return;
		}
	}
	if (cam && g_fly_on) {   /* out of free flight: Oni's follow camera again, a cut */
		g_fly_on = 0;
		if (CAM_MODE(cam) == CAM_MANUAL) ONrCamera_EnterFollow();
		if (CAM_TARGET(cam) != GS_PLAYER(gs)) g_cam_set = CAM_TARGET(cam);
	}
	if (gs && players_killcam(view_character(GS_PLAYER(gs)))) { g_cam_held = 1; return; }
	if (cam && *(u32 *)cam == 1) {
		u8 *konoko = GS_PLAYER(gs), *view = view_character(konoko);
		u8 *target = CAM_TARGET(cam);
		int watching = me_watching();
		if (view && view != target && (target == konoko || (target && target == g_cam_set)) &&
		    (view != konoko || watching || g_was_watching)) {
			CAM_TARGET(cam) = g_cam_set = view;
			/* watching someone else, or back to playing: a cut, not a glide across the map */
			if (watching || g_was_watching) ONrCamera_EnterFollow();
		}
		g_was_watching = watching;
		/* back from the kill camera: a cut to the new spawn, not a glide across the map */
		if (g_cam_held && CAM_TARGET(cam)) ONrCamera_EnterFollow();
	}
	g_cam_held = 0;
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

	if (cover_frame()) return;   /* a joiner between its loads: the loading popup, not the game */
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
	log_msg(ok ? "net: pickup, overheal and error sounds follow this PC's player"
	           : "net: some player sounds couldn't be moved to this PC's player (game code changed)");
}

static void sounds_install(void)
{
	if (patch_call(0x4edb9c, ONrUISound_Play, (u32)hypo_sound)) log_msg("net: hypo sounds only for this PC's player");
}

/* free flight: the listener is the camera (position, and its view made level for the facing) */
static int fly_listener(void *out, int facing)
{
	u8 *cam = ONgGameState ? GS_CAMERA(ONgGameState) : 0;
	float *o = out, *v;
	if (!cam || !g_fly_on || !flying()) return 0;
	if (!facing) {
		memcpy(o, CAM_LOC(cam), 12);
		return 1;
	}
	v = CAM_VIEW(cam);
	float n = fsqrt(v[0] * v[0] + v[2] * v[2]);
	o[0] = n > 0.0001f ? v[0] / n : 0;
	o[1] = 0;
	o[2] = n > 0.0001f ? v[2] / n : 1;
	return 1;
}

static void ONI_FC listener_position(u8 *chr, void *out) { if (!fly_listener(out, 0)) ONrListener_Position(view_character(chr), out); }
static void ONI_FC listener_position_alt(u8 *chr, void *out) { if (!fly_listener(out, 0)) ONrListener_PositionAlt(view_character(chr), out); }
static void ONI_FC listener_facing(u8 *chr, void *out) { if (!fly_listener(out, 1)) ONrListener_Facing(view_character(chr), out); }

static void view_install(void)
{
	static int done;
	if (done) return;
	done = 1;
	if (patch_call(0x4fb1f4, (u32)ONrListener_PositionAlt, (u32)listener_position_alt) &&
	    patch_call(0x4fb1fb, (u32)ONrListener_Position, (u32)listener_position) &&
	    patch_call(0x4fb20f, (u32)ONrListener_Facing, (u32)listener_facing))
		log_msg("net: sound is heard from this PC's player");
	{
		static const u8 entry[8] = { 0x83, 0xec, 0x6c, 0xa1, 0x7c, 0xce, 0x5e, 0x00 };
		u8 b[8] = { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
		u32 rel = (u32)display - (ONcDisplay + 5);
		if (code_matches(ONcDisplay, entry, sizeof entry)) {
			memcpy(b + 1, &rel, 4);
			patch_write(ONcDisplay, b, sizeof b);
			log_msg("net: the HUD, laser sight and crosshair show this PC's player");
			if (!patch_call(ONcTick_HUDCall, (u32)ONrHUD_Tick, (u32)hud_tick))
				log_msg("net: HUD tick call 0x%x changed, the health ring follows Konoko", ONcTick_HUDCall);
		} else {
			log_msg("net: game display 0x%x changed, the HUD and crosshair show Konoko", ONcDisplay);
		}
	}
}

/* ---- Dedicated server ----------------------------------------------------------------------- *
 * A host that doesn't play (the Host dialog's Dedicate, or ARKLITE_DEDICATED=<map folder>[:port]):
 * server.c gives it a console, skips drawing, hides the game window and turns the sound off.
 *   - Its player (Konoko, slot 0) always watches (players_spectate_want, so every record says so and
 *     players.c parks her on every PC) and is never one of the players: not in the ready-up
 *     (watches), the match's end (end_players), the server list's counts (info_counts) or the
 *     joiners' lists (LOBBY dedicated -> net_hud). Its records carry nothing else (make_input).
 *   - It keeps slot 0 (NPL is NET_PLAYERS_MAX + 1 for it), so it still takes NET_PLAYERS_MAX players;
 *     capacity() adds its place, and players.c makes the player past the map's placements from Konoko's (players_set_dedicated, which every PC must
 *     know before a session's level loads: LOBBY carries it, protocol 20).
 *   - The admin's commands (server.c) come here: say (a server line, CHAT_SERVER, to everyone),
 *     players, kick (out, then refused with NETBAD_KICKED and a note for some minutes; by IP
 *     address), ban (the same until the server closes), start (the match now), warmup, limits,
 *     name, max players, quit (goodbye to everyone, then Oni's own quit flag). */

static void lobby_goodbye(void);

#define ONgExitRequested (*(u8 *)0x630ffd)   /* the main loop ends (what Quit sets) */

static void dedicated_begin(void)
{
	g_dedicated = 1;
	players_spectate_want(1);
	players_set_dedicated(1);
	server_start();
}

int net_dedicated_host(void) { return g_dedicated && g_mode == NET_HOST; }
int net_dedicated(void) { return net_dedicated_host() || (g_mode == NET_JOIN && g_ded_host); }

static const char *state_text(void)
{
	return g_state == S_LOBBY ? "waiting" : g_state == S_WAIT ? "starting" : g_state == S_OVER ? "match over" :
	       g_state == S_RUN || g_state == S_LOST ? (g_warm ? "warm-up" : "match") : "loading";
}

int net_server_players(NetPlayerRow *rows, int max)
{
	int n = 0;
	for (int l = 1; l < NPL && n < max; l++) {
		NetPlayerRow *r = &rows[n];
		const u8 *ip = (const u8 *)&g_link[l].addr.addr;
		if (!g_link[l].used) continue;
		memset(r, 0, sizeof *r);
		r->used = 1;
		r->slot = l;
		r->gone = gone(l);
		r->joining = g_link[l].joining || g_link[l].waiting || g_link[l].rejoin;
		r->watching = !r->gone && !r->joining && watches(l);
		r->playing = !r->gone && !r->joining && !r->watching;
		r->ping = g_link[l].ping;
		r->score = players_score(l);
		str_cpy(r->name, sizeof r->name, mp_name(l)[0] ? mp_name(l) : "(no name yet)");
		fmt(r->addr, "%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], ntohs(g_link[l].addr.port));
		n++;
	}
	return n;
}

int net_server_say(const char *text)
{
	char t[CHAT_MAX + 16];
	int any = 0;
	fmt(t, "Server: %s", text);
	t[CHAT_MAX - 1] = 0;
	clean_text(t, CHAT_MAX);
	chat_server(t);   /* the console has it through chat.c */
	if (g_mode != NET_HOST) return 0;
	for (int o = 1; o < NPL; o++)
		if (chat_link(o) && !gone(o)) {
			chat_queue(o, CHAT_SERVER, CHAT_ALL, t);
			any = 1;
		}
	return any;
}

int net_server_find(const char *who)
{
	int found = -1, count = 0, n = str_len(who);
	if (n == 1 && who[0] >= '2' && who[0] <= '0' + NPL) {   /* a player number, as the console lists them */
		int l = who[0] - '1';
		return g_link[l].used ? l : -1;
	}
	for (int l = 1; l < NPL; l++) {   /* the whole name, else a name that starts with it */
		if (!g_link[l].used) continue;
		if (str_len(mp_name(l)) == (unsigned)n && str_ieq_n(mp_name(l), who, n)) return l;
		if (n && str_ieq_n(mp_name(l), who, n)) found = l, count++;
	}
	return count > 1 ? -2 : found;
}

const char *net_server_kick(int l, u32 minutes, const char *reason)
{
	static char out[160];
	char name[MP_NAME_MAX], note[64];
	int b = 0, i;
	u32 now = GetTickCount();

	if (g_mode != NET_HOST || l < 1 || l >= NPL || !g_link[l].used) return "No such player.";
	str_cpy(name, sizeof name, mp_name(l));
	if (minutes) fmt(note, "Kicked for %d min%s%s", minutes, reason && reason[0] ? ": " : ".", reason ? reason : "");
	else fmt(note, "Banned from this server%s%s", reason && reason[0] ? ": " : ".", reason ? reason : "");
	note[sizeof note - 1] = 0;
	for (i = 0; i < BANS; i++) {   /* the same address again, else a free entry, else the oldest kick */
		if (g_ban[i].on && g_ban[i].addr == g_link[l].addr.addr) break;
		if (!g_ban[i].on) b = i;
	}
	if (i < BANS) b = i;
	g_ban[b].on = 1;
	g_ban[b].addr = g_link[l].addr.addr;
	g_ban[b].until = minutes ? (now + minutes * 60000u) | 1 : 0;
	str_cpy(g_ban[b].note, sizeof g_ban[b].note, note);
	str_cpy(g_ban[b].name, sizeof g_ban[b].name, name);
	/* it hears why at once (a lost packet: again with every HELLO it sends) */
	g_lobby_note = note;
	for (i = 0; i < 3; i++) send_lobby_addr(&g_link[l].addr, l, 0, NETBAD_KICKED);
	g_lobby_note = 0;
	if (g_state == S_LOBBY || g_state == S_IDLE || g_link[l].waiting || g_link[l].joining) remove_link(l);
	else mark_gone(l, 1, minutes ? "was kicked" : "was banned", 0);
	server_line(name, minutes ? "was kicked" : "was banned");
	log_msg("net: %s (player %d) removed: %s", name, l + 1, note);
	fmt(out, "%s: %s", name, note);
	return out;
}

/* The removed players still out, in g_ban order: what 'bans' numbers 1, 2, ... */
static int bans_active(int *idx)
{
	int n = 0;
	for (int i = 0; i < BANS; i++)
		if (g_ban[i].on && ban_note(g_ban[i].addr)) idx[n++] = i;
	return n;
}

/* "unban [who]": who is the number 'bans' gives, the name the player had, its address, or all;
 * nothing when only one is out. Its PC, still asking to join, comes in at its next HELLO. */
const char *net_server_unban(const char *who)
{
	static char out[160];
	int idx[BANS], n = bans_active(idx), pick = -1, count = 0;
	u32 len = str_len(who);

	if (!n) return "Nobody is kicked or banned.";
	if (!len) {
		if (n > 1) return "More than one is out: unban <number from 'bans', name, address or all>.";
		pick = 0;
	} else if (str_ieq_n(who, "all", 4)) {
		for (int k = 0; k < n; k++) g_ban[idx[k]].on = 0;
		log_msg("net: unbanned everyone (%d)", n);
		fmt(out, "%d player%s can come back.", n, n == 1 ? "" : "s");
		return out;
	} else if (len <= 2 && who[0] >= '1' && who[0] <= '9' && (len == 1 || (who[1] >= '0' && who[1] <= '9'))) {
		int k = len == 1 ? who[0] - '1' : (who[0] - '0') * 10 + who[1] - '1';
		if (k >= n) return "No such number: 'bans' lists them.";
		pick = k;
	} else {
		for (int k = 0; k < n && pick < 0; k++) {   /* the address, else the whole name, else a name it starts */
			const u8 *ip = (const u8 *)&g_ban[idx[k]].addr;
			char a[20];
			fmt(a, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
			if (str_len(a) == len && str_ieq_n(a, who, len)) pick = k;
			else if (str_len(g_ban[idx[k]].name) == len && str_ieq_n(g_ban[idx[k]].name, who, len)) pick = k;
		}
		if (pick < 0)
			for (int k = 0; k < n; k++)
				if (str_ieq_n(g_ban[idx[k]].name, who, len)) pick = k, count++;
		if (count > 1) return "More than one removed name starts with that: give more of it, or the number from 'bans'.";
		if (pick < 0) return "Nobody removed matches that ('bans' lists them).";
	}
	{
		int i = idx[pick];
		const u8 *ip = (const u8 *)&g_ban[i].addr;
		g_ban[i].on = 0;
		fmt(out, "%s (%d.%d.%d.%d) can come back.", g_ban[i].name[0] ? g_ban[i].name : "The player", ip[0], ip[1], ip[2], ip[3]);
		log_msg("net: unbanned %s", out);
	}
	return out;
}

const char *net_server_bans(char *out, u32 size)
{
	int idx[BANS], n = bans_active(idx);
	u32 now = GetTickCount(), used = 0;
	out[0] = 0;
	for (int k = 0; k < n; k++) {
		int i = idx[k];
		const u8 *ip = (const u8 *)&g_ban[i].addr;
		char one[200], left[32];
		if (g_ban[i].until) fmt(left, "%d min left", (int)(g_ban[i].until - now) / 60000 + 1);
		else str_cpy(left, sizeof left, "until the server closes");
		fmt(one, "%s%d. %s, %d.%d.%d.%d (%s): %s", used ? "\n" : "", k + 1, g_ban[i].name[0] ? g_ban[i].name : "?",
		    ip[0], ip[1], ip[2], ip[3], left, g_ban[i].note);
		if (used + str_len(one) + 1 >= size) break;
		str_cpy(out + used, size - used, one);
		used += str_len(one);
	}
	return out[0] ? out : "Nobody is kicked or banned.";
}

const char *net_server_start(void)
{
	if (g_mode != NET_HOST) return "Not hosting.";
	if (g_state != S_RUN || !g_warm) return "Only from the warm-up.";
	if (!ready_players()) return "Nobody is playing.";
	server_print("Starting the match.");
	host_go(0);
	return 0;
}

const char *net_server_warmup(void)
{
	if (g_mode != NET_HOST) return "Not hosting.";
	if (g_state == S_RUN && g_warm) return "The warm-up is on already.";
	if (g_state != S_RUN && g_state != S_OVER && g_state != S_LOST) return "Not now: no match is on.";
	server_print("Back to a warm-up.");
	host_go(1);
	return 0;
}

const char *net_server_status(char *out, u32 size)
{
	const char *map = custom_game_level_name(g_level), *folder = custom_game_level_folder(g_level);
	char limits[64], tl[24];
	u32 players, max, spect;
	int left = g_state == S_RUN && !g_warm ? players_time_left() : -1;

	info_counts(&players, &max, &spect);
	fmt(limits, "time limit %d min, score limit %d", g_time_limit, g_score_limit);
	if (left >= 0) fmt(tl, ", %d:%02d left", left / 3600, left / 60 % 60);
	else tl[0] = 0;
	fmt(out, "\"%s\" on UDP port %d: %s (%s), %s%s; %d of %d players, %d watching; %s",
	    g_server_name, g_port, map ? map : "no map", folder ? folder : "-", state_text(), tl, players, max, spect, limits);
	out[size - 1] = 0;
	return out;
}

void net_server_set_limits(int minutes, int score, int have_minutes, int have_score)
{
	char v[16];
	if (have_minutes) {
		g_time_limit = (u32)minutes;
		fmt(v, "%d", minutes);
		WritePrivateProfileStringA("host", "time_limit", v, ini_path());
	}
	if (have_score) {
		g_score_limit = score;
		fmt(v, "%d", score);
		WritePrivateProfileStringA("host", "score_limit", v, ini_path());
	}
	if (g_mode == NET_HOST && g_state != S_IDLE) send_lobby_all(0);   /* the joiners' screens say them */
}

void net_server_set_name(const char *name)
{
	str_cpy(g_server_name, sizeof g_server_name, name);
	ini_text_write("host", "name", g_server_name);
}

void net_server_set_max_players(u32 n)
{
	char v[16];
	g_max_players = n;
	fmt(v, "%d", n);
	WritePrivateProfileStringA("host", "max_players", v, ini_path());
}

void net_server_quit(void)
{
	log_msg("net: the dedicated server closes");
	lobby_goodbye();
	session_end("the server closed");
	ONgExitRequested = 1;
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
	if (g_mode == NET_JOIN && g_adopt != INVALID_SOCKET) {   /* see "Hole punching" */
		g_sock = g_adopt;
		g_adopt = INVALID_SOCKET;
		log_msg("net: the game goes on with the Join list's socket (its way through the NATs, if punched, stays open)");
	} else {
		g_sock = socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 17 /* IPPROTO_UDP */);
		if (g_sock == INVALID_SOCKET) {
			log_msg("net: no UDP socket (error %d), networking off", WSAGetLastError());
			return 0;
		}
		stun_reset();
		memset(&local, 0, sizeof local);
		local.family = 2;
		local.port = g_mode == NET_HOST ? htons(g_port) : 0;   /* the joiner: any free port */
		if (bind(g_sock, &local, sizeof local)) {
			log_msg("net: can't use UDP port %d (error %d; another host running?), networking off", g_port, WSAGetLastError());
			closesocket(g_sock);
			g_sock = INVALID_SOCKET;
			return 0;
		}
	}
	ioctlsocket(g_sock, FIONBIO, &on);
	g_key = 0;   /* joiner: the host gives this address its key (see "Strangers") */
	g_opened_at = GetTickCount();
	g_hint_addr[0] = 0;
	g_punch_said = 0;
	g_intro_at = 0;
	g_helper_knows = 0;
	if (g_mode == NET_HOST) make_secret();
	{   /* room for a map download's bursts (Windows' default is 64 KB) */
		int rcv = 4 << 20, snd = 1 << 20;
		setsockopt(g_sock, 0xffff /* SOL_SOCKET */, 0x1002 /* SO_RCVBUF */, &rcv, 4);
		setsockopt(g_sock, 0xffff, 0x1001 /* SO_SNDBUF */, &snd, 4);
	}
	if (!g_images)
		g_images = VirtualAlloc(0, HASH_SLOTS * IMG_CHARS * sizeof(CharImage), 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 4);
	if (!g_tring) g_tring = VirtualAlloc(0, TRING * sizeof(TickRec), 0x3000, 4);
	if (g_mode == NET_JOIN) {
		memset(&g_link[0], 0, sizeof g_link[0]);
		g_slot = 1;   /* until the host's lobby says which player this PC is */
		if (!resolve(g_join, &g_link[0].addr)) {
			log_msg("net: can't find host %s, networking off", g_join);
			closesocket(g_sock);
			g_sock = INVALID_SOCKET;
			return 0;
		}
		g_link[0].used = 1;
		link_reset(&g_link[0]);
		const u8 *ip = (const u8 *)&g_link[0].addr.addr;
		log_msg("net: joining %d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], ntohs(g_link[0].addr.port));
	} else {
		g_slot = 0;
		log_msg("net: hosting on UDP port %d", g_port);
		upnp_start(g_port);   /* players on the internet: the router forwards the port here (upnp.c) */
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
	if (!patch_call(ONcLevelStart_FadeCall, ONrFade_Frame, (u32)level_fade_frame))
		log_msg("net: the level start's black frame at 0x%x isn't the expected call: a joiner may see it flash", ONcLevelStart_FadeCall);
	sounds_join_install();
}

/* Leaving a lobby: the others hear it at once instead of after LOST_MS of silence (a match's
 * goodbye is session_end's) */
static void lobby_goodbye(void)
{
	if (g_state == S_LOBBY && g_sock != INVALID_SOCKET) send_bye();
}

/* Out of the network game this PC was in (the main menu's Custom Game picks the next mode) */
static void net_leave(void)
{
	g_cover_pending = 0;
	g_l0_pending = g_l0_fetch = 0;   /* (a switch Join's Play set up for a load that never came) */
	cover_end("this PC left from the menu");
	lobby_goodbye();
	session_end("left from the menu");
	upnp_stop();
	if (g_sock != INVALID_SOCKET) helper_closed(g_sock), closesocket(g_sock);
	g_sock = INVALID_SOCKET;
	if (g_adopt != INVALID_SOCKET) closesocket(g_adopt);
	g_adopt = INVALID_SOCKET;
	memset(g_punch, 0, sizeof g_punch);
	memset(g_link, 0, sizeof g_link);
	names_push();
	g_match_pending = 0;
	g_problem = 0;
	/* A new connection follows the host's sessions from nothing: joining the same host again, its
	 * warm-up still has the session number this PC followed before, and the host's LOBBY telling
	 * this PC to catch up with it was taken for a late repeat and ignored (the rejoin never got in) */
	g_go_taken = g_cu_failed = 0;
	players_spectate_want(0);   /* a new game starts playing */
	g_ded_host = 0;
	g_kick_note[0] = 0;
	players_set_dedicated(g_dedicated && g_mode == NET_HOST);   /* (a dedicated server never leaves: quit closes Oni) */
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

u32 net_char_rules(void) { return g_mode == NET_OFF ? NET_CHARS_ALL : g_char_rules & NET_CHARS_ALL; }
int net_mutator(u32 bit) { return g_mode != NET_OFF && (g_char_rules & bit); }
int net_mutator_daodan(void) { return net_mutator(NET_MUT_DAODAN); }

u32 net_host_char_rules_saved(void)
{
	return (GetPrivateProfileIntA("host", "mutant_muro", 0, ini_path()) ? NET_CHARS_MUTANT_MURO : 0) |
	       (GetPrivateProfileIntA("host", "shinatama_hasegawa", 0, ini_path()) ? NET_CHARS_SHINATAMA : 0) |
	       (GetPrivateProfileIntA("host", "mini_striker", 0, ini_path()) ? NET_CHARS_MINI_STRIKER : 0) |
	       (GetPrivateProfileIntA("host", "daodan", 0, ini_path()) ? NET_MUT_DAODAN : 0) |
	       (GetPrivateProfileIntA("host", "no_weapons", 0, ini_path()) ? NET_MUT_NO_WEAPONS : 0) |
	       (GetPrivateProfileIntA("host", "fast_screamer", 0, ini_path()) ? NET_MUT_FAST_SCREAMER : 0) |
	       (GetPrivateProfileIntA("host", "fast_plasma", 0, ini_path()) ? NET_MUT_FAST_PLASMA : 0) |
	       (GetPrivateProfileIntA("host", "bombs", 0, ini_path()) ? NET_MUT_BOMBS : 0) |
	       (GetPrivateProfileIntA("host", "fast_ball", 0, ini_path()) ? NET_MUT_FAST_BALL : 0);
}

void net_menu_host(u32 port, const char *name, u32 max_players, u32 time_limit, int score_limit, u32 char_rules)
{
	net_leave();
	g_startup_mode = 0;
	g_mode = NET_HOST;
	view_install();   /* a watching host's view (the rest only changes anything on a joiner) */
	g_port = (u16)(port >= 1 && port <= 65535 ? port : DEFAULT_PORT);
	str_cpy(g_server_name, sizeof g_server_name, name && name[0] ? name : "New Oni multiplayer game");
	g_max_players = max_players >= 2 && max_players <= NET_PLAYERS_MAX ? max_players : NET_PLAYERS_MAX;
	g_time_limit = time_limit;
	g_score_limit = score_limit;
	set_char_rules(char_rules & NET_MUTATORS_ALL, "the Host dialog's");
	motd_load();
	g_server_id = GetTickCount() * 2654435761u ^ (u32)GetCurrentProcessId();
	net_prepare();
	log_msg("net: hosting \"%s\" on UDP port %d, %d players, %d min, %d points; mutators %02x", g_server_name, g_port,
	        g_max_players, g_time_limit, g_score_limit, g_char_rules);
}

/* The Host dialog's Dedicate: hosting as a dedicated server, which stays one until Oni closes */
void net_menu_dedicated(u32 port, const char *name, u32 max_players, u32 time_limit, int score_limit, u32 char_rules)
{
	net_menu_host(port, name, max_players, time_limit, score_limit, char_rules);
	dedicated_begin();
}

void net_menu_join(const char *address)
{
	net_leave();
	g_startup_mode = 0;
	g_mode = NET_JOIN;
	set_char_rules(NET_CHARS_ALL, "joining: until the host's come,");
	if (g_qsock != INVALID_SOCKET) {   /* the game goes on with it (see "Hole punching") */
		g_adopt = g_qsock;
		g_qsock = INVALID_SOCKET;
	}
	str_cpy(g_join, sizeof g_join, address);
	net_prepare();
	join_install();
	g_cover_pending = 1;   /* from the level load it starts (a start that fails leaves the menu as it is) */
	log_msg("net: joining %s", g_join);
}

/* ---- Server info for the Join list ------------------------------------------------------- */

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
	union { PktInfoReq q; u8 pad[sizeof(PktInfo)]; } u;   /* as big as the answer (see "Strangers") */
	SOCKADDR_IN to;

	if (!query_open()) return;
	memset(&u, 0, sizeof u);
	head(&u.q.h, PKT_INFO_REQ);
	u.q.token = token;
	u.q.sent = GetTickCount();
	memset(&to, 0, sizeof to);
	to.family = 2;
	to.addr = addr;
	to.port = port;
	sendto(g_qsock, &u, sizeof u, 0, &to, sizeof to);
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
	stun_frame(g_qsock, GetTickCount());   /* the address a host behind CGNAT punches toward (see "Hole punching") */
	helper_frame(g_qsock, GetTickCount());
	for (;;) {
		len = sizeof from;
		n = recvfrom(g_qsock, buf, sizeof buf, 0, &from, &len);
		if (n < 0) {
			if (WSAGetLastError() == 10054) continue;   /* WSAECONNRESET: nobody on that port */
			return 0;
		}
		if (stun_packet(buf, (u32)n, &from) || helper_packet(g_qsock, buf, (u32)n, &from)) continue;
		const PktInfo *p = (const PktInfo *)buf;
		if ((u32)n < sizeof p->h || p->h.magic != MAGIC || p->h.type != PKT_INFO) continue;
		memset(out, 0, sizeof *out);
		/* another protocol's reply may not be laid out like ours: only its token and time count
		 * (before protocol 19 the head was 8 bytes, the token and time right after it) */
		if (p->h.proto < 19) {
			if ((u32)n < 16) continue;
			out->token = ((const u32 *)buf)[2];
			out->ping_ms = GetTickCount() - ((const u32 *)buf)[3];
		} else {
			if ((u32)n < 3 * 4 + sizeof p->h) continue;
			out->token = p->token;
			out->ping_ms = GetTickCount() - p->sent;
		}
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
			clean_text(out->name, sizeof out->name);
			out->has_download = 1;
			out->l0[0] = p->l0[0];
			out->l0[1] = p->l0[1];
			out->l0_bytes = p->l0_bytes;
			out->map_hash = p->map_hash;
			out->map_bytes = p->map_bytes;
			out->map_ok = p->map_ok;
			out->nmods = p->nmods;
			memcpy(out->mods, p->mods, sizeof out->mods);
		}
		return 1;
	}
}

/* The Join list: the helper's server list, and introductions to hosts that don't answer (helper.c) */
void net_helper_list(void)
{
	if (query_open()) helper_list(g_qsock);
}

void net_helper_intro(u32 addr, u16 port)
{
	if (query_open()) helper_intro(g_qsock, addr, port);
}

int net_helper_intro_ack(u32 *asked_addr, u16 *asked_port, int *known, u32 *addr, u16 *port)
{
	return g_qsock != INVALID_SOCKET && helper_intro_ack(g_qsock, asked_addr, asked_port, known, addr, port);
}

void net_query_close(void)
{
	if (g_qsock != INVALID_SOCKET) helper_closed(g_qsock), closesocket(g_qsock);
	g_qsock = INVALID_SOCKET;
}

/* ---- Install ------------------------------------------------------------------------------ */

void net_install(void)
{
	char host_env[32], ded_env[96] = "";
	u32 port;

	/* Always on: the kill camera (players_killcam) is every PC's and a local game's too; the rest
	 * of camera_update only changes anything on a joiner */
	if (patch_call(0x4fb141, (u32)ONrCamera_Update, (u32)camera_update) &&
	    patch_call(0x4fb170, (u32)ONrCamera_Update, (u32)camera_update))
		log_msg("net: the camera follows this PC's player");
	/* also always on: photo mode hides the player with it */
	if (!patch_call(ONcDraw_OnScreenCall, (u32)ONrCharacter_OnScreen, (u32)character_on_screen))
		log_msg("net: draw pass call 0x%x changed, a watching host's character stays drawn", ONcDraw_OnScreenCall);

	helper_set_proto(PROTO);   /* the server list shows only games of this protocol */

	/* The server's description, as the Host dialog saved it */
	ini_text("host", "name", g_server_name, g_server_name, sizeof g_server_name);
	g_max_players = GetPrivateProfileIntA("host", "max_players", NET_PLAYERS_MAX, ini_path());
	if (g_max_players < 2 || g_max_players > NET_PLAYERS_MAX) g_max_players = NET_PLAYERS_MAX;   /* (an older Arklite allowed 64) */
	g_time_limit = GetPrivateProfileIntA("host", "time_limit", 10, ini_path());
	g_score_limit = (int)GetPrivateProfileIntA("host", "score_limit", 20, ini_path());
	g_char_rules = net_host_char_rules_saved();   /* a host started from the ini or ARKLITE_DEDICATED (a joiner's come from LOBBY) */
	motd_load();
	if (g_time_limit > NET_TIME_LIMIT_MAX) g_time_limit = 10;   /* (a negative one reads as huge) */
	port = GetPrivateProfileIntA("net", "port", DEFAULT_PORT, ini_path());
	g_delay = GetPrivateProfileIntA("net", "delay", 0, ini_path());   /* 0: rollback, no input delay */
	g_record = GetPrivateProfileIntA("net", "record", 1, ini_path()) != 0;
	if (g_delay > 30) g_delay = 30;
	if (port < 1 || port > 65535) port = DEFAULT_PORT;
	g_port = (u16)port;

	if (GetEnvironmentVariableA("ARKLITE_DEDICATED", ded_env, sizeof ded_env)) {   /* <map folder>[:port] */
		const char *colon = ded_env;
		while (*colon && *colon != ':') colon++;
		g_mode = NET_HOST;
		host_env[0] = 0;
		if (*colon) str_cpy(host_env, sizeof host_env, colon + 1);   /* the port, read as ARKLITE_HOST's below */
	} else if (GetEnvironmentVariableA("ARKLITE_HOST", host_env, sizeof host_env)) g_mode = NET_HOST;
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
	if (g_mode == NET_JOIN) join_install(), g_cover_pending = 1;
	else view_install();   /* a watching host's view */
	if (g_mode == NET_HOST && ded_env[0]) dedicated_begin();
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
	cover_end("this PC left to the main menu");
	g_cover_pending = g_mode == NET_JOIN;   /* (the mode stays: the next level joins again) */
	lobby_goodbye();
	session_end("left to the main menu");
}

void net_exit(void)
{
	lobby_goodbye();
	session_end("game closed");
	upnp_stop();
	net_query_close();
	if (g_sock != INVALID_SOCKET) closesocket(g_sock);
	g_sock = INVALID_SOCKET;
}
