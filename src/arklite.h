/* Shared by Arklite's source files: minimal Win32, the log, patching and string helpers. */
#pragma once
#include "oni.h"

/* ---- Minimal Win32 --------------------------------------------------------------------- */

#define WINAPI __stdcall
typedef void *HANDLE;
typedef __builtin_va_list va_list;
#define va_start(ap, x) __builtin_va_start(ap, x)
#define va_end(ap)      __builtin_va_end(ap)

typedef struct {
	u32  dwFileAttributes;
	u32  ftCreationTime[2], ftLastAccessTime[2], ftLastWriteTime[2];
	u32  nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
	char cFileName[260];
	char cAlternateFileName[14];
} WIN32_FIND_DATAA;

#define INVALID_HANDLE_VALUE      ((HANDLE)-1)
#define INVALID_FILE_ATTRIBUTES   0xffffffffu
#define FILE_ATTRIBUTE_DIRECTORY  0x10
#define PAGE_EXECUTE_READWRITE    0x40
#define MAX_PATH                  260

__declspec(dllimport) int    WINAPI VirtualProtect(void *, u32, u32, u32 *);
__declspec(dllimport) int    WINAPI FlushInstructionCache(HANDLE, const void *, u32);
__declspec(dllimport) HANDLE WINAPI GetCurrentProcess(void);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char *, u32, u32, void *, u32, u32, HANDLE);
__declspec(dllimport) int    WINAPI WriteFile(HANDLE, const void *, u32, u32 *, void *);
__declspec(dllimport) HANDLE WINAPI FindFirstFileA(const char *, WIN32_FIND_DATAA *);
__declspec(dllimport) int    WINAPI FindNextFileA(HANDLE, WIN32_FIND_DATAA *);
__declspec(dllimport) int    WINAPI FindClose(HANDLE);
__declspec(dllimport) u32    WINAPI GetFileAttributesA(const char *);
__declspec(dllimport) u32    WINAPI GetFullPathNameA(const char *, u32, char *, char **);
__declspec(dllimport) u32    WINAPI GetPrivateProfileStringA(const char *, const char *, const char *, char *, u32, const char *);
__declspec(dllimport) u32    WINAPI GetEnvironmentVariableA(const char *, char *, u32);
__declspec(dllimport) u32    WINAPI GetPrivateProfileIntA(const char *, const char *, int, const char *);
__declspec(dllimport) int    WINAPI WritePrivateProfileStringA(const char *, const char *, const char *, const char *);
__declspec(dllimport) int    WINAPI wvsprintfA(char *, const char *, va_list);
__declspec(dllimport) HANDLE WINAPI GetModuleHandleA(const char *);
__declspec(dllimport) void * WINAPI AddVectoredExceptionHandler(u32, void *);

/* ---- common.c ---------------------------------------------------------------------------- */

/* Logs and dumps go to AE\logs\<name>_<date>_<time>.<ext>, the newest LOG_KEEP of each name kept */
#define LOG_DIR  "logs"
#define LOG_KEEP 3
HANDLE log_file_create(const char *name, char *path);   /* path: MAX_PATH, gets the file's name */
void log_open(const char *name);
const char *log_path(void);                   /* this session's log, e.g. logs\Arklite_2026-10-29_01-00-27.log */
void log_msg(const char *f, ...);              /* one line, CRLF appended */

/* Rewrite an instruction after checking its current bytes; logs and returns 0 on a mismatch. */
int patch_push_imm32(u32 addr, u32 expected, u32 value);
int patch_call(u32 addr, u32 expected, u32 target);
int patch_byte(u32 addr, u8 expected, u8 value);
/* Unchecked building blocks, for patches that verify many sites before writing any. */
int  code_matches(u32 addr, const u8 *expected, u32 n);
void patch_write(u32 addr, const void *bytes, u32 n);

/* Move a window right after another in its parent's child list (drawing order). */
void win_insert_after(WMtWindow *win, WMtWindow *after);

const char *ini_path(void);                    /* full path of AE\Arklite.ini */

unsigned str_len(const char *s);
int  str_cpy(char *d, unsigned size, const char *s);   /* always NUL-terminates, 0 if truncated */
char to_upper(char c);
int  str_ieq_n(const char *a, const char *b, unsigned n);
int  str_contains_i(const char *s, const char *sub);
void fmt(char *buf, const char *f, ...);       /* wvsprintfA: at most 1024 bytes */
const char *skip_space(const char *s);
int  parse_float(const char **s, float *out);  /* "-12.5": advances *s; 0 if no number */

/* ---- custom_game.c ----------------------------------------------------------------------- */

void custom_game_install(void);
const char *custom_game_level_ini(u32 level);   /* level.ini of the custom level in that slot, or 0 */
const char *custom_game_level_dir(u32 level);   /* that level's folder (ends with '\'), or 0 */
const char *custom_game_level_folder(u32 level); /* that folder's name in CustomLevels, or 0 */
int  custom_game_remount(u32 level);             /* list its files again and mount them (net.c's map download) */
const char *custom_game_level_name(u32 level);   /* the custom level's name (level.ini), or 0 */
int  custom_game_has_folder(const char *folder);  /* CustomLevels\<folder> holds a level */

/* ---- volume.c ----------------------------------------------------------------------------- */

void volume_install(void);
int  volume_channel_is_music(u8 *channel);   /* channel owned by a Music play (call under the sound lock) */

/* ---- more_options.c ----------------------------------------------------------------------- */

enum { PAUSE_MUSIC_KEEP, PAUSE_MUSIC_THEME, PAUSE_MUSIC_SILENCE };

void  more_options_install(void);
void  more_options_init(WMtWindow *dialog);                    /* after the Options dialog's init */
int   more_options_command(WMtWindow *dialog, u32 id, u32 notify);   /* 1 if it was ours */
int   more_options_cancel(WMtWindow *dialog);   /* Esc: 1 if it only left the More page */
void  more_options_end(WMtWindow *dialog);      /* the dialog is going (WMcMessage_Destroy) */
int   pause_music_mode(void);
u32   pause_background_alpha(void);   /* 0..255 */
float hud_scale(void);                /* 1.0 = original size */
int   show_fps(void);                 /* the FPS counter is on */

/* ---- hud_scale.c -------------------------------------------------------------------------- */

void hud_scale_install(void);
/* mp_hud.c owns the text sprite call (0x42deae) and the timer call (0x50061f) and hands the HUD's
 * own text to these, which scale it with the rest of the HUD */
u32  ONI_FC hud_text_sprite(const float *points, const void *uvs);   /* for 0x41a5e0 */
void ONI_FC hud_timer_display(void *center);                         /* for 0x4fd500 */

/* ---- hud_smooth.c ------------------------------------------------------------------------- */

void hud_smooth_frame(void);                   /* before the HUD draws at a scale other than 1 */
u8  *hud_smooth_swap(float scale, u32 *saved); /* the draw's texture, now our resampled copy, or 0 */
void hud_smooth_restore(u8 *texture, u32 saved);

/* ---- random_class.c----------------------------------------------------------------------- */

void random_class_install(void);

/* ---- particle_table.c --------------------------------------------------------------------- */

void particle_table_install(void);

/* ---- texture_clamp.c ---------------------------------------------------------------------- */

void texture_clamp_install(void);

/* ---- char_limit.c ------------------------------------------------------------------------- */

void char_limit_install(void);
u32  char_limit_gs_offset(u32 offset);   /* a stock game state offset, where it is now */

/* ---- lockstep.c -------------------------------------------------------------------------- */

void lockstep_install(void);   /* ARKLITE_RECORD / ARKLITE_REPLAY: input record and replay */
void lockstep_exit(void);      /* finish the recording or the replay report */

/* Arklite's own game state: variables the game's ticks change and depend on (a tick's RNG, the
 * players' score and respawns, item respawns). They go in their own section, .tstate, which is
 * all of Arklite that rollback.c snapshots and puts back; everything else Arklite keeps (the
 * network, the log, menus, recordings) must never be rewound. */
#define TICK_STATE __attribute__((section(".tstate")))

/* ---- rollback.c ------------------------------------------------------------------------ */
void rollback_install(void);     /* ARKLITE_MEMSURVEY: which memory the game's ticks write */
void rollback_frame(void);       /* lockstep.c: at every frame's update */
void rollback_tick_begin(void);  /* lockstep.c: around every tick */
void rollback_tick_end(void);
/* ARKLITE_ROLLBACK_TEST: runs the tick (body(action)) itself, twice from the same memory, and
 * returns 1, or 0 when the caller runs it */
int  rollback_test_tick(void *action, void (*body)(void *));
int  rollback_net_begin(void);            /* net.c: snapshots from the next tick on */
void rollback_net_end(void);
int  rollback_restore_tick(u32 tick);     /* back to the start of tick: 1, or 0 if not kept */
void rollback_impulses(int on);           /* one-shot sounds off while ticks run again */
u32  rollback_ambients_stopped(void);     /* ambient sounds stopped because their ticks were taken back */
u32  rollback_known_pages(void);
const char *rollback_check_path(void);   /* this run's Arklite-rollbackcheck file, "" if none */
int  rollback_check_begin(void);          /* after a rollback's re-run: keep its result */
int  rollback_check_end(const char *what); /* after the same ticks ran again: 1 same, 0 game state differs */
void rollback_check_stats(u32 *checks, u32 *bad);
void rollback_keep(const void *p, u32 n); /* memory a rewind never puts back (HUD textures) */
#define ROLLBACK_KEPT_TICKS 12            /* RB_SLOTS: how far back the game can go */
void lockstep_get_seeds(u32 *game, u32 *tick_local);   /* the game RNG and the tick-local stream */
void lockstep_set_seeds(u32 game, u32 tick_local);
int  lockstep_replay_spawn_seed(u32 *seed);   /* replaying: the recording's spawn seed (1), else 0 */
int lockstep_impulses_mute(int muted);    /* rollback re-runs ticks: only new one-shot sounds play (1), else off (0) */
/* net.c's recording of a networked game (same format, player 2's record per tick) */
int  lockstep_net_record_begin(u32 level, const char *path);
void lockstep_net_record_frame(u32 n, const u32 (*p1)[12], const u32 (*p2)[12]);
void lockstep_net_record_frame_end(void);
void lockstep_net_record_end(const char *why);

/* ---- net.c ------------------------------------------------------------------------------- */

void net_install(void);        /* ARKLITE_HOST / ARKLITE_JOIN: two PCs in lockstep over UDP */
void net_exit(void);
void net_main_menu(void);      /* the main menu opened: the network game this PC was in is over */
int  net_active(void);
int  net_synced(void);   /* in a session every PC runs the same (warm-up or match): multiplayer rules, players or not */
/* Custom Game picks what the next level is: alone, hosting, or joining */
#define NET_DEFAULT_PORT    27777
#define NET_SERVER_NAME_MAX 48
void net_menu_local(void);     /* Play (a mode set at startup stays) */
/* time_limit in minutes, score_limit in points (below 0: reached going down); 0: none */
void net_menu_host(u32 port, const char *name, u32 max_players, u32 time_limit, int score_limit);
#define NET_PLAYERS_MAX     4      /* the Host dialog's Max players: 2 to this */
#define NET_TIME_LIMIT_MAX  999
#define NET_SCORE_LIMIT_MAX 999
void net_menu_join(const char *address);   /* host[:port] */
/* Joining without the host's map: downloads its CustomLevels\<folder> from the host (after
 * net_menu_join, from the menu, in the loading popup); 1 when it's here. Else why not: */
int  net_map_fetch(const char *folder, u32 level);
const char *net_map_fetch_error(void);
/* What a server says about itself, for the Join list */
typedef struct {
	u32 token, ping_ms;
	int same_version;          /* it runs this Arklite's protocol (else nothing below is known) */
	u32 level, players, max_players, spectators, max_spectators, in_match, server_id;
	u32 from_addr;             /* where the reply came from (network order) */
	u16 from_port;
	char name[NET_SERVER_NAME_MAX], folder[64], map[48];
} NetServerInfo;
/* host[:port] looked up on a thread, one at a time: start (0 if one is still going), then poll
 * every frame: -1 not yet, 0 not found, 1 addr/port (network order) */
int  net_resolve_start(const char *address);
int  net_resolve_poll(u32 *addr, u16 *port);
void net_query_send(u32 token, u32 addr, u16 port);
void net_query_broadcast(u32 token);   /* this PC and the local network */
int  net_query_poll(NetServerInfo *out);   /* 1: a reply came, in *out */
#define NET_CHAR_IMAGE_SIZE (0x16a0 + 0x2838)   /* a character and its active data */
int  net_char_image(u32 index, u8 *img);   /* the character's state without drawing's fields; 0 if not in use */
u32  net_update(u32 count, void *actions, u32 *ticks_run);   /* the game update while it's on */
void net_tick_done(void);      /* after every tick */
enum { NETHUD_OFF, NETHUD_LOBBY, NETHUD_STARTING, NETHUD_MATCH };
typedef struct {
	int state;                 /* NETHUD_* */
	int local_player;          /* this PC's player: 0 the host (Konoko), 1.. the joiners, in joining order */
	int local_char;            /* players_char() of this PC's character (a joiner's lobby: 1) */
	int joiner;                /* this PC joined (else it hosts) */
	int ready, connected;      /* this PC pressed F3; the host is / is heard */
	int warmup;                /* the synced session is the warm-up, not the match */
	int match_on;              /* lobby: a match is on without this PC, it plays from the next one */
	u32 players, ready_count, needed;
	int countdown_ms;          /* -1: none */
	u32 problem;               /* NETBAD_*: what keeps the joiner out (joiner: of the host's game; host: a joiner) */
	int desynced;              /* a state hash differed in this match */
	u32 present;               /* the players in this lobby or match, a bit per player (gone ones not) */
	u32 gone, left;            /* match: players who stopped answering, and of them those who left (they stand) */
	int silent_ms, silent_who; /* match: nothing from that player's PC for this long (0: under 1.5 s) */
	int lost;                  /* joiner's match: 1 the host stopped answering, 2 it left */
	u32 time_limit;            /* the match's limits (the host's): minutes, 0 none */
	int score_limit;           /* 0: none; below 0: reached going down */
	int ping_ms[NET_PLAYERS_MAX];   /* each player's last round trip to the host (the host's row: a joiner's own), -1: not known */
	int over;                  /* match: a limit was reached, the game stands until everyone pressed fire */
	int over_ready;            /* this PC's player pressed fire */
	u32 over_players, over_ready_count;
	int over_left_ms;          /* until everyone goes back to the lobby anyway */
	int joining;               /* joiner: 1 catching up with the session in progress, 2 in sync, not in the game yet */
	int joining_pct;           /* joining 1: how far (0..100) */
	int fight_ms;              /* match (not warm-up) just began on this PC: "FIGHT!" this much longer */
	char punch_me[32];         /* joiner, the host silent: this PC's internet address, for the host's /punch ("" none) */
} NetHud;
/* What differs between the two PCs, so the host doesn't let the joiner in */
enum {
	NETBAD_PROTO = 1, NETBAD_DLL = 2, NETBAD_DAODAN = 4, NETBAD_DAODAN_INI = 8, NETBAD_LEVEL0 = 16,
	NETBAD_MAP = 32, NETBAD_PLAYERS = 64, NETBAD_FULL = 128,
};
#define NETBAD_LAST NETBAD_FULL
void net_hud(NetHud *h);
int  net_watching(void);         /* this PC's player watches: the player it shows, -2 free flight; -1 not watching */
u8  *net_swapped_konoko(void);   /* Konoko while the joiner's display runs with player 2 in gs+0xac, else 0 */
const char *net_problem_text(u32 bit);   /* what one NETBAD_* bit names */

/* ---- upnp.c ------------------------------------------------------------------------------ */

void upnp_start(u16 port);              /* host: ask the router to forward UDP port here (its own thread; [net] upnp=0: off) */
void upnp_stop(void);                   /* the forwarding taken back (waits up to 2 s for the router) */
const char *upnp_take_message(void);    /* the outcome, once, for the host's chat; 0: nothing new */

/* ---- stun.c ------------------------------------------------------------------------------ */

void stun_reset(void);                  /* a new socket: ask again at once */
void stun_frame(u32 sock, u32 now);     /* each frame the socket is open: asks every 25 s ([net] stun, stun2) */
int  stun_packet(const u8 *p, u32 n, const void *from);   /* 1: a STUN server's answer, taken */
int  stun_text(char *out);              /* this socket's internet address "a.b.c.d:port" (0, "": not known) */
int  stun_take_change(void);            /* 1 once after it became known or changed */

/* Hole punching (net.c): /ip and /punch */
int  net_my_address(char *out);         /* this PC's game (or Join list) socket's internet address; 0 if not known */
/* helper.c: the server list and introductions ([net] helper) */
void helper_set_proto(u16 proto);
int  helper_on(void);                  /* [net] helper names one */
void helper_frame(u32 sock, u32 now);   /* each frame the socket is open: its key */
int  helper_packet(u32 sock, const u8 *p, u32 n, const void *from);   /* 1: the helper's, taken */
int  helper_beat_due(u32 sock, u32 now);   /* host: then helper_beat */
void helper_beat(u32 sock, const NetServerInfo *game);
int  helper_take_news(void);           /* host: 1 listed, 0 refused, 2 silent, 3 hidden; -1 nothing new */
int  helper_take_intro(u32 *addr, u16 *port);   /* host: a player to knock toward */
void helper_list(u32 sock);            /* the Join list: ask for the list from the first page */
int  helper_list_next(NetServerInfo *out);   /* a game on it (from_addr/from_port its address) */
void helper_intro(u32 sock, u32 addr, u16 port);   /* introduce this socket to that host */
int  helper_intro_ack(u32 sock, u32 *asked_addr, u16 *asked_port, int *known, u32 *addr, u16 *port);
void helper_closed(u32 sock);
void net_helper_list(void);            /* the Join list's socket: helper_list */
void net_helper_intro(u32 addr, u16 port);   /* ...helper_intro */
int  net_helper_intro_ack(u32 *asked_addr, u16 *asked_port, int *known, u32 *addr, u16 *port);   /* ...its answers */
const char *net_punch(const char *address);   /* host: knock toward that player for a minute; what to tell the user */

/* ---- mp_menu.c --------------------------------------------------------------------------- */

/* Custom Game's dialogs: the New/Join popup, the Host settings and the Join list */
enum { MPMENU_CANCEL, MPMENU_NEW, MPMENU_JOIN };
void mp_menu_install(void);   /* text fields: keys and caret */
int  mp_menu_choose(void);
int  mp_menu_host(const char *map_name);   /* 1: Host (net.c is set up), 0: Cancel */
/* 1: Join (net.c is set up); the host's map: its CustomLevels folder ("" for a numbered level) and level */
int  mp_menu_join(char *folder, u32 folder_size, u32 *level);
void mp_menu_join_failed(const char *folder, const char *why);   /* the map download stopped: the Join list says why */

/* Building dialogs from code, shared with custom_game.c's level list */
#define WMcClass_EditField 7
#define WMcClass_ListBox   8
#define WMcClass_Label     0x14
#define WMrWindow_SetEnabled ((u8 (ONI_FC *)(WMtWindow *, u32))0x475580)
#define WMrWindow_GetBytes   ((u8 *(ONI_FC *)(WMtWindow *, u32))0x474dc0)   /* the class's data (volume.c: GetData) */
/* List box (class 8, proc 0x46c720); style 0x80000 keeps copies of the strings, 0x10000 has a scroll bar */
#define LBcMessage_DeleteString 0x46   /* p2 index */
#define LBcMessage_GetNumLines  0x48
#define LBcMessage_GetSelection 0x49   /* index, or -1 */
#define LBcMessage_ReplaceString 0x4d  /* p1 text, p2 index */
#define LBcMessage_Reset        0x4e
#define LBcMessage_SetSelection 0x53   /* p1 notify, p2 index (-1: none); scrolls it into view */
#define LBcMessage_Scroll       0x58   /* p1 4 << 16, p2 first line */
#define LBcStyle_Strings        0x80000
#define LBcStyle_ScrollBar      0x10000
/* Edit field (class 7, proc 0x46b9c0); style 0x10000 takes only digits, '.' and '-' */
#define EFcMessage_GetText      0x40   /* p1 buffer, p2 size */
#define EFcMessage_SetMaxLength 0x41   /* p1 characters */
#define EFcMessage_SetText      0x42   /* p1 text */
#define EFcStyle_Numbers        0x10000
#define EFcNotify_Changed       0xa    /* sent with the field's id after every key that changed it */
/* A new window like src (class unless cls != 0, flags, style unless style != ~0, font), placed after
 * anchor in the drawing order (earlier children draw on top) */
WMtWindow *ui_add(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *src, u32 cls, u32 style,
                  const char *text, u32 id, int x, int y, int w, int h);
void ui_set_frame(WMtWindow *w, int x, int y, int width, int height);
void ui_list_set_frame(WMtWindow *w, int x, int y, int width, int height);   /* a listbox: its row count follows */
void ui_set_text(WMtWindow *w, const char *text);
void ui_set_text_fit(WMtWindow *w, const char *text);   /* cut with "..." to the window's width */
/* Tahoma Bold 10 (the menus' font): width in pixels, and src cut with "..." to fit width */
int  ui_text_width(const char *s);
void ui_fit(char *dst, unsigned size, const char *src, int width);
/* A label (copy of font's font) with its text cut to w */
enum { UI_LEFT, UI_CENTER, UI_RIGHT };
#define UI_LABEL_STYLE 0x110000   /* the Options dialog's labels: vertically centered */
#define UI_ORANGE      0xffff7f00   /* the menus' text */
#define UI_BLUE        0xff689be4   /* the Options dialog's frame headers */
#define LBcStyle_OwnerDraw 0x100000 /* the list asks its dialog to draw each row's text (msg 0x36) */
#define LBcMessage_DrawRow 0x36
int  ui_list_draw_row(u32 p2, u32 color);   /* from the dialog proc on msg 0x36: that row's text in color */
void ui_list_draw_text(u32 p2, int x, int width, const char *text, u32 color);   /* text at x in the row, cut to width */
WMtWindow *ui_label(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *font, const char *text, int x, int y, int w, int h, int align);
void *ui_frame_hook(void (*fn)(void));   /* called every frame while a dialog is up; returns the old one */
void ui_frame_hook_call(void *old);       /* from fn: the hook it replaced */
/* Text fields with Arklite's editing (selection, caret, keys): begin when the dialog is made
 * (Enter = default_id while no field is edited), frame from its frame hook, end when it closes */
void ui_fields_begin(WMtWindow *dialog, u32 default_id);
void ui_fields_frame(void);
void ui_fields_end(void);
/* A field for numbers like 1.250 (digits and one '.', at most max characters). Each key that
 * changes it sends its dialog Command id | EFcNotify_Changed << 16; Enter, Tab, Escape or a click
 * elsewhere send id | UI_NOTIFY_CONFIRMED << 16, for the dialog to write its value back tidy. */
#define UI_NOTIFY_CONFIRMED 0x100   /* Arklite's own; Oni's notify codes are small */
WMtWindow *ui_field_decimal(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *font, u32 id, int x, int y, int w,
                            u32 max, const char *text);
int  ui_parse_thousandths(const char *s, int *out);   /* "1.25" -> 1250; 0 if it isn't a number */

/* ---- mp_board.c -------------------------------------------------------------------------- */

void mp_board_install(void);   /* the F1 screen: Scoreboard tab, the match's goal on Objective */
void mp_board_goal(char *out, u32 minutes, int score);   /* that goal, one sentence (192 bytes) */

/* ---- mp_hud.c ---------------------------------------------------------------------------- */

void mp_hud_install(void);     /* lobby text ("Press F3 to be ready"), countdown and score on screen */
enum { MP_LEFT, MP_CENTER, MP_RIGHT };
void mp_hud_text(const char *s, int x, int y, int align, u32 color);   /* only while the HUD draws */
void mp_hud_text_plain(const char *s, int x, int y, int align, u32 color);   /* not bold */
u32  mp_hud_style(u32 style);   /* the HUD text style from here on (0 plain, the HUD's bold); returns the one before */
int  mp_hud_line_height(void);
int  mp_hud_text_width(const char *s, int n);   /* the first n characters (n < 0: all), trailing spaces included */

/* ---- mp_names.c -------------------------------------------------------------------------- */

#define MP_NAME_MAX 24
void mp_names_install(void);   /* Arklite.ini [multiplayer] name, [players] p2_name */
const char *mp_name(int who);  /* 0: Konoko (the host), 1..: the other players */
const char *mp_name_local(void);          /* this PC's player's */
u32  mp_name_serial(void);                /* changes with this PC's name */
void mp_name_set(int who, const char *name);   /* net.c: another PC's player's ("" unknown) */
int  mp_name_change(const char *name);    /* this PC's player's new name: saved and announced; 0 unchanged */
void net_name_changed(const char *before, const char *after);   /* net.c: "Before is now known as After" */
int  mp_name_editing(void);               /* typed in the F2 menu */
void mp_name_edit_begin(void);
void mp_name_edit_end(int keep);          /* keep: saved in Arklite.ini */
const char *mp_name_edit_text(void);      /* what's typed so far (the name when not editing) */
void mp_names_draw(int width, int height);   /* from the HUD: the name of the player faced */
void mp_names_say(int who, const char *text);   /* chat.c: a message over that player's head for a while */

/* ---- chat.c ------------------------------------------------------------------------------- */

/* Chat in network games: Enter opens it, Enter sends, Esc closes (see chat.c) */
#define CHAT_MAX 128                  /* characters + 1 */
void mp_sound(const char *impulse);   /* a level 0 impulse by name, no position; outside a tick */
void chat_install(void);              /* Arklite.ini [chat] */
void menu_wheel_install(void);        /* menu_wheel.c: the mouse wheel in Oni's menus */
void screenshot_install(void);       /* screenshot.c: F12 saves the frame to screenshots\ */
void chat_frame(void);                /* every frame, after the update */
void chat_draw(int width, int height);   /* from the HUD */
int  chat_blocking(void);             /* open (or Enter/Esc that closed it still down): the game gets the mouse but no keyboard */
void chat_receive(int who, const char *text);   /* net.c: player who (another PC's) said it */
void chat_server(const char *text);   /* net.c: a server line ("Name entered the game") */
int  chat_wrap(const char *s, int width, int *starts, int max);   /* line starts of s wrapped to width (HUD text); returns the count */
int  net_chat_send(const char *text); /* net.c: to the other PCs; 0 if there's nobody */
int  net_chat_whisper(int to, const char *text);   /* net.c: to player to only (through the host); 0 if it can't go */
void chat_whisper_receive(int who, const char *text);   /* net.c: player who whispered it to this PC's player */
int  chat_muted(int who);             /* the F1 scoreboard muted that player's messages */
void chat_set_muted(int who, int on);

/* ---- char_menu.c ------------------------------------------------------------------------- */

/* Word 11 of an action record (never read by the tick): PLAYERS_CHOICE_TAG | (ONCC instance
 * number + 1) is the class that record's player comes back as, the tag alone the map's class */
#define PLAYERS_CHOICE_TAG 0x7fa00000u
#define PLAYERS_CHOICE_MASK 0xffe00000u
/* In the host's (Konoko's) word 11 of a network game: PLAYERS_PRESENT_FLAG | the players who
 * have a character, a bit per player at PLAYERS_PRESENT_SHIFT (players.c adds and removes them in
 * that tick; net.c sets it, see "Joining a game in progress") */
#define PLAYERS_PRESENT_FLAG 0x00100000u
#define PLAYERS_PRESENT_SHIFT 16
/* Bits 12-15 of anyone's word 11: an emote for that tick (PLAYERS_EMOTE_*, players.c); bit 11:
 * that player watches (/spectate, a state in every record, not a request); so the class number
 * is the low 11 bits */
#define PLAYERS_CLASS_MASK  0x000007ffu
#define PLAYERS_SPECTATE_FLAG 0x00000800u
#define PLAYERS_EMOTE_MASK  0x0000f000u
#define PLAYERS_EMOTE_SHIFT 12
enum { PLAYERS_EMOTE_TAUNT = 1, PLAYERS_EMOTE_TAUNT2, PLAYERS_EMOTE_SNEEZE, PLAYERS_EMOTE_IDLE /* .. +n-1: idle n */ };
#define PLAYERS_EMOTE_IDLES 3
void *players_emote_anim(u8 *c, int emote);   /* the animation it plays on that character, or 0 */
void players_emote_request(int emote);        /* chat.c: this PC's player, at its next record */
u32  players_emote_take(void);                /* net.c: the request's word 11 bits (once) */
/* Spectating (/spectate, /join): this PC's wish rides in every record (players_spectate_word);
 * every PC applies it in the same tick (players.c "Spectating") */
void players_spectate_want(int on);           /* chat.c: this PC's player watches (1) or plays (0) */
int  players_spectate_wanted(void);
u32  players_spectate_word(void);             /* net.c make_input: PLAYERS_SPECTATE_FLAG or 0 */
int  players_spectating(int who);             /* tick state: slot who watches */
u32  players_spectators(void);                /* a bit per slot */
int  players_hidden(u8 *c);                   /* not drawn: a watching host's parked Konoko */
void char_menu_install(void);
void char_menu_level0_changed(void);   /* class numbers changed: the roster is resolved again */
void respawn_fx_level0_changed(void);  /* its base classes come from the new level 0 */
void level0_swap_install(void);
char *level0_swap_ref(char *ref);      /* the file to open for a level file table entry */
int  level0_swap(const char *dir);     /* with no level loaded: level 0 from dir (level0_swap.c) */
int  level0_swaps(void);              /* swaps done in this Oni session */
int  char_menu_open(void);     /* the F2 menu is up: this PC's player gets no input */
void hud_textures_prepare(void);   /* net.c, before rollback starts: the HUD's textures made */
u8  *hud_solid(u16 argb1555, const char *name);   /* an 8x8 texture of one color (kept until Oni exits) */
void hud_rect(u8 *texture, int x, int y, int w, int h, u32 alpha);   /* stretched over the rectangle, from the HUD */
void char_menu_frame(void);    /* every frame, after the update: keys and mouse */
void char_menu_draw(int width, int height);   /* from the HUD */
u32  char_menu_record_word(void);             /* this PC's choice, for word 11 of its records */
void char_menu_tag(u32 count, void *actions); /* writes it into this PC's records of the frame */

/* ---- players.c --------------------------------------------------------------------------- */

void players_install(void);
const u32 *players_begin_frame(void);         /* player 2's record for this frame, or 0 */
void players_replay_frame(const u32 *record); /* replay: this frame's recorded one instead */
/* Players in a game: slot 0 Konoko, 1..PLAYERS_MAX-1 the next Player-flagged placements */
#define PLAYERS_MAX NET_PLAYERS_MAX
void players_net_frame(const u32 (*records)[6][12], u32 n);   /* networked: slot s's tick i gets records[s][i] (slot 0 unused) */
void players_set_count(u32 n);                /* players spawned at the next level load (2 by default) */
u32  players_count(void);
int  players_spawn_points(void);              /* the level's Player-flagged placements */
void players_set_spawn_seed(u32 seed);        /* the next level load's spawn picks (net.c: the host's, on every PC) */
u32  players_spawn_seed(void);                /* this level's (lockstep.c keeps it in a recording) */
void players_level_begin(void);               /* particle_table.c: a level loads, its spawns get a new seed */
u8  *players_char(int who);                   /* slot who's character, or 0 */
void players_note_hit(u8 *victim, u8 *attacker, u32 owner);   /* from TakeDamage, in a tick */
int  players_can_change(int who);            /* alive: an F2 choice applies at once */
int  players_score(int who);                  /* by slot */
int  players_enabled(void);                   /* Arklite.ini [players] p2 isn't 0 */
u32  players_respawn_ticks(void);             /* [multiplayer] respawn_delay, in ticks */
void players_set_respawn_ticks(u32 ticks);    /* the network host's, on the joiner */
void players_reset_scores(void);              /* and the class choices: the match starts */
void players_tick_record(const u32 *record);  /* in the tick: Konoko's record (its choice word) */
int  players_can_join(int who);               /* the map has a spawn placement for slot who */
/* A network match's limits (net.c): on from the match's start (0 ticks / score 0: none); a
 * negative score limit is reached going down (suicides). Checked in ticks, so rollback corrects it. */
void players_set_limits(int on, u32 ticks, int score);
u32  players_over_tick(void);                 /* tick + 1 in which a limit was reached, 0: not over */
int  players_time_left(void);                 /* ticks, -1: no time limit */
int  players_winner(u32 mask);                /* the best of mask's players (a bit per slot), -1 for a draw */
int  players_ranking(u32 mask, int *out);     /* mask's players, best first; returns how many */
int  players_killcam(u8 *c);                  /* c fell to the level's kill camera: this PC's camera stays */
int  multiplayer_active(void);                /* a player 2 is in the game: no pausing, no auto-aim */
int  multiplayer_map(void);                   /* two or more Player-flagged characters (spawn points) */

/* ---- obituary.c -------------------------------------------------------------------------- */

/* How a death was caused: the kill feed's icon (OBIT_SKULL 0, so a zeroed cause is a skull) */
enum { OBIT_SKULL, OBIT_MELEE, OBIT_SUPER, OBIT_BLAST, OBIT_FALL, OBIT_WEAPON };
void obituary_install(void);   /* the kill feed; super moves told apart from other melee hits */
int  obit_hit_cause(u8 *attacker, u32 owner, u8 **weapon);   /* in TakeDamage: OBIT_*, *weapon its ONWC */
u8  *obit_weapon_class(u8 *c);                 /* the ONWC of the weapon c holds, or 0 */
void obit_add(int killer, int victim, int kind, u8 *weapon);   /* in a tick; killer -1: nobody */
void obit_reset(void);
void obit_textures_prepare(void);              /* hud_textures_prepare: before rollback starts */
void obit_draw(int x, int y, int local);       /* from the HUD: lines from y down, left edge x */

/* ---- mp_rules.c -------------------------------------------------------------------------- */

void mp_rules_install(void);   /* multiplayer maps: free for all, damage x1.25, overhealing drains 1.25x faster */
void mp_rules_level(void);     /* a level's characters are about to be made: Ninjabot's weapon hand */

/* ---- regen.c ----------------------------------------------------------------------------- */

void regen_install(void);      /* players' Barabas regeneration: crouch, let go of crouch, Action */
/* A player's tick input on its way into the character (players.c): in itself, or copy (12 words)
 * changed while the character regenerates */
u32 *regen_input(u8 *chr, u32 *in, u32 *copy);

/* ---- items.c ----------------------------------------------------------------------------- */

void items_install(void);
void items_tick(void);   /* in a multiplayer game's tick: taken items come back after a while */
u32  items_default_ticks(void);             /* [multiplayer] item_respawn, in ticks */
void items_set_default_ticks(u32 ticks);    /* the network host's, on the joiner */
void items_player_respawned(const float *pos);   /* respawn_fx.c's particle, there */
void respawn_fx_install(void);                 /* keeps level 0's classes the effect is made from */
void respawn_fx_load(u32 level);               /* before a level's classes are set up */
const char *respawn_fx_root(void);             /* the particle class a respawn fires */
void items_stop_firing(void);               /* release every held trigger: the firing loop sounds end */

/* ---- live_menu.c ------------------------------------------------------------------------- */

void live_menu_install(void);
int  live_menu_open(void);     /* the pause menu is open and the game runs behind it */
void live_menu_request(void);  /* open it after this frame's update */
void live_menu_close(void);    /* end every live dialog (as Esc does), before a level reload */
/* WMrDialog_ModalBegin for every dialog (lockstep.c wraps the calls): in a multiplayer game a
 * dialog outside a tick runs with the game going on behind it, one inside a tick isn't shown */
int  lockstep_in_tick(void);   /* lockstep.c: inside a tick, on its thread */
u16  live_menu_modal(u32 id, WMtWindow *parent, WMtDialogProc proc, u32 param, u32 *result, int in_tick);

/* ---- vertex_light.c ---------------------------------------------------------------------- */

void bench_frame(void);           /* every frame: ARKLITE_BENCH timing (bench.c) */
void vertex_light_level_end(void);   /* from any level unload */
void vertex_light_install(void);     /* flickering lights' ambients play only while they fail */
void vertex_light_frame(void);   /* every frame: animated corners of a custom level (vertexlight.bin) */
void spin_particle_frame(void);  /* every frame: particles in a custom level's spinning groups (spin_particle.c) */

/* ---- crash_log.c -------------------------------------------------------------------------- */

void crash_log_install(void *instance);   /* log faults with registers and stack */
void crash_log_exit(void);                /* log the stack when the process exits */


/* ---- report.c: anonymous problem reports --------------------------------------------------- */
enum { REPORT_DESYNC = 1, REPORT_CRASH = 2, REPORT_DROP = 3 };
void report_install(void *instance);   /* [reports] send; sends a crash's log left by the last run */
void report_net(u32 reason, const char *session, int slot, u32 tick, const char *dump1, const char *dump2, const char *extra);
void report_crash(void);               /* the fault handler: this log goes at the next start */
const char *net_session_id(void);      /* net.c: the session's ID, "none" outside one */
int  net_local_slot(void);             /* net.c: this PC's player slot, -1 off the network */

/* ---- flare_depth.c: lens flare depth tests ---------------------------------------------------- */
void flare_depth_install(void);
void flare_depth_present(void);        /* screenshot.c, at every SwapBuffers */
int  screenshot_hooked(void);          /* screenshot.c: both SwapBuffers calls are hooked */
