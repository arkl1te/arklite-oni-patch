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
int  rollback_check_begin(void);          /* after a rollback's re-run: keep its result */
int  rollback_check_end(const char *what); /* after the same ticks ran again: 1 same, 0 game state differs */
void rollback_check_stats(u32 *checks, u32 *bad);
void rollback_keep(const void *p, u32 n); /* memory a rewind never puts back (HUD textures) */
#define ROLLBACK_KEPT_TICKS 12            /* RB_SLOTS: how far back the game can go */
void lockstep_get_seeds(u32 *game, u32 *tick_local);   /* the game RNG and the tick-local stream */
void lockstep_set_seeds(u32 game, u32 tick_local);
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
/* Custom Game picks what the next level is: alone, hosting, or joining */
#define NET_DEFAULT_PORT    27777
#define NET_SERVER_NAME_MAX 48
void net_menu_local(void);     /* Play (a mode set at startup stays) */
/* time_limit in minutes, score_limit in points (below 0: reached going down); 0: none */
void net_menu_host(u32 port, const char *name, u32 max_players, u32 max_spectators, u32 time_limit, int score_limit);
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
} NetHud;
/* What differs between the two PCs, so the host doesn't let the joiner in */
enum {
	NETBAD_PROTO = 1, NETBAD_DLL = 2, NETBAD_DAODAN = 4, NETBAD_DAODAN_INI = 8, NETBAD_LEVEL0 = 16,
	NETBAD_MAP = 32, NETBAD_PLAYERS = 64, NETBAD_FULL = 128,
};
#define NETBAD_LAST NETBAD_FULL
void net_hud(NetHud *h);
u8  *net_swapped_konoko(void);   /* Konoko while the joiner's display runs with player 2 in gs+0xac, else 0 */
const char *net_problem_text(u32 bit);   /* what one NETBAD_* bit names */

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

/* ---- mp_board.c -------------------------------------------------------------------------- */

void mp_board_install(void);   /* the F1 screen: Scoreboard tab, the match's goal on Objective */
void mp_board_goal(char *out, u32 minutes, int score);   /* that goal, one sentence (192 bytes) */

/* ---- mp_hud.c ---------------------------------------------------------------------------- */

void mp_hud_install(void);     /* lobby text ("Press F3 to be ready"), countdown and score on screen */
enum { MP_LEFT, MP_CENTER, MP_RIGHT };
void mp_hud_text(const char *s, int x, int y, int align, u32 color);   /* only while the HUD draws */
int  mp_hud_line_height(void);
int  mp_hud_text_width(const char *s, int n);   /* the first n characters (n < 0: all), trailing spaces included */

/* ---- mp_names.c -------------------------------------------------------------------------- */

#define MP_NAME_MAX 24
void mp_names_install(void);   /* Arklite.ini [multiplayer] name, [players] p2_name */
const char *mp_name(int who);  /* 0: Konoko (the host), 1..: the other players */
const char *mp_name_local(void);          /* this PC's player's */
u32  mp_name_serial(void);                /* changes with this PC's name */
void mp_name_set(int who, const char *name);   /* net.c: another PC's player's ("" unknown) */
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
void chat_frame(void);                /* every frame, after the update */
void chat_draw(int width, int height);   /* from the HUD */
int  chat_blocking(void);             /* open (or Enter/Esc that closed it still down): the game gets the mouse but no keyboard */
void chat_receive(int who, const char *text);   /* net.c: player who (another PC's) said it */
void chat_server(const char *text);   /* net.c: a server line ("Name entered the game") */
int  chat_wrap(const char *s, int width, int *starts, int max);   /* line starts of s wrapped to width (HUD text); returns the count */
int  net_chat_send(const char *text); /* net.c: to the other PCs; 0 if there's nobody */
int  chat_muted(int who);             /* the F1 scoreboard muted that player's messages */
void chat_set_muted(int who, int on);

/* ---- char_menu.c ------------------------------------------------------------------------- */

/* Word 11 of an action record (never read by the tick): PLAYERS_CHOICE_TAG | (ONCC instance
 * number + 1) is the class that record's player comes back as, the tag alone the map's class */
#define PLAYERS_CHOICE_TAG 0x7fac0000u
#define PLAYERS_CHOICE_MASK 0xffff0000u
void char_menu_install(void);
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
u8  *players_char(int who);                   /* slot who's character, or 0 */
void players_note_hit(u8 *victim, u8 *attacker, u32 owner);   /* from TakeDamage, in a tick */
int  players_can_change(int who);            /* alive: an F2 choice applies at once */
int  players_score(int who);                  /* by slot */
int  players_enabled(void);                   /* Arklite.ini [players] p2 isn't 0 */
u32  players_respawn_ticks(void);             /* [multiplayer] respawn_delay, in ticks */
void players_set_respawn_ticks(u32 ticks);    /* the network host's, on the joiner */
void players_reset_scores(void);              /* and the class choices: the match starts */
void players_tick_record(const u32 *record);  /* in the tick: Konoko's record (its choice word) */
/* A network match's limits (net.c): on from the match's start (0 ticks / score 0: none); a
 * negative score limit is reached going down (suicides). Checked in ticks, so rollback corrects it. */
void players_set_limits(int on, u32 ticks, int score);
u32  players_over_tick(void);                 /* tick + 1 in which a limit was reached, 0: not over */
int  players_time_left(void);                 /* ticks, -1: no time limit */
int  players_winner(u32 mask);                /* the best of mask's players (a bit per slot), -1 for a draw */
int  players_ranking(u32 mask, int *out);     /* mask's players, best first; returns how many */
int  multiplayer_active(void);                /* a player 2 is in the game: no pausing, no auto-aim */
int  multiplayer_map(void);                   /* two or more Player-flagged characters (spawn points) */

/* ---- mp_rules.c -------------------------------------------------------------------------- */

void mp_rules_install(void);   /* multiplayer maps: free for all, damage x1.25, overhealing drains 1.25x faster */

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
u16  live_menu_modal(u32 id, WMtWindow *parent, WMtDialogProc proc, u32 param, u32 *result, int in_tick);

/* ---- vertex_light.c ---------------------------------------------------------------------- */

void bench_frame(void);           /* every frame: ARKLITE_BENCH timing (bench.c) */
void vertex_light_frame(void);   /* every frame: animated corners of a custom level (vertexlight.bin) */

/* ---- crash_log.c -------------------------------------------------------------------------- */

void crash_log_install(void *instance);   /* log faults with registers and stack */
void crash_log_exit(void);                /* log the stack when the process exits */
