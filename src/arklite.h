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

void log_open(const char *path);
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
u32  rollback_known_pages(void);
#define ROLLBACK_KEPT_TICKS 12            /* RB_SLOTS: how far back the game can go */
void lockstep_get_seeds(u32 *game, u32 *tick_local);   /* the game RNG and the tick-local stream */
void lockstep_set_seeds(u32 game, u32 tick_local);
/* net.c's recording of a networked game (same format, player 2's record per tick) */
int  lockstep_net_record_begin(u32 level, const char *path);
void lockstep_net_record_frame(u32 n, const u32 (*p1)[12], const u32 (*p2)[12]);
void lockstep_net_record_frame_end(void);
void lockstep_net_record_end(const char *why);

/* ---- net.c ------------------------------------------------------------------------------- */

void net_install(void);        /* ARKLITE_HOST / ARKLITE_JOIN: two PCs in lockstep over UDP */
void net_exit(void);
int  net_active(void);
#define NET_CHAR_IMAGE_SIZE (0x16a0 + 0x2838)   /* a character and its active data */
int  net_char_image(u32 index, u8 *img);   /* the character's state without drawing's fields; 0 if not in use */
u32  net_update(u32 count, void *actions, u32 *ticks_run);   /* the game update while it's on */
void net_tick_done(void);      /* after every tick */
enum { NETHUD_OFF, NETHUD_LOBBY, NETHUD_STARTING, NETHUD_MATCH };
typedef struct {
	int state;                 /* NETHUD_* */
	int local_player;          /* 0 Konoko (host), 1 player 2 (joiner) */
	int ready, connected;      /* this PC pressed F3; the host is / is heard */
	u32 players, ready_count, needed;
	int countdown_ms;          /* -1: none */
	u32 problem;               /* NETBAD_*: what keeps the joiner out (joiner: of the host's game; host: a joiner) */
	int desynced;              /* a state hash differed in this match */
} NetHud;
/* What differs between the two PCs, so the host doesn't let the joiner in */
enum {
	NETBAD_PROTO = 1, NETBAD_DLL = 2, NETBAD_DAODAN = 4, NETBAD_DAODAN_INI = 8, NETBAD_LEVEL0 = 16,
	NETBAD_MAP = 32, NETBAD_PLAYERS = 64,
};
void net_hud(NetHud *h);
const char *net_problem_text(u32 bit);   /* what one NETBAD_* bit names */

/* ---- mp_hud.c ---------------------------------------------------------------------------- */

void mp_hud_install(void);     /* lobby text ("Press F3 to be ready"), countdown and score on screen */
enum { MP_LEFT, MP_CENTER, MP_RIGHT };
void mp_hud_text(const char *s, int x, int y, int align, u32 color);   /* only while the HUD draws */
int  mp_hud_line_height(void);

/* ---- mp_names.c -------------------------------------------------------------------------- */

#define MP_NAME_MAX 24
void mp_names_install(void);   /* Arklite.ini [multiplayer] name, [players] p2_name */
const char *mp_name(int who);  /* 0: Konoko (the host), 1: player 2 (the joiner) */
const char *mp_name_local(void);          /* this PC's player's */
u32  mp_name_serial(void);                /* changes with this PC's name */
void mp_name_set_peer(const char *name);  /* net.c: the other PC's player's */
int  mp_name_editing(void);               /* typed in the F2 menu */
void mp_name_edit_begin(void);
void mp_name_edit_end(int keep);          /* keep: saved in Arklite.ini */
const char *mp_name_edit_text(void);      /* what's typed so far (the name when not editing) */
void mp_names_draw(int width, int height);   /* from the HUD: the name of the player faced */

/* ---- char_menu.c ------------------------------------------------------------------------- */

/* Word 11 of an action record (never read by the tick): PLAYERS_CHOICE_TAG | (ONCC instance
 * number + 1) is the class that record's player comes back as, the tag alone the map's class */
#define PLAYERS_CHOICE_TAG 0x7fac0000u
#define PLAYERS_CHOICE_MASK 0xffff0000u
void char_menu_install(void);
int  char_menu_open(void);     /* the F2 menu is up: this PC's player gets no input */
void char_menu_frame(void);    /* every frame, after the update: keys and mouse */
void char_menu_draw(int width, int height);   /* from the HUD */
u32  char_menu_record_word(void);             /* this PC's choice, for word 11 of its records */
void char_menu_tag(u32 count, void *actions); /* writes it into this PC's records of the frame */

/* ---- players.c --------------------------------------------------------------------------- */

void players_install(void);
const u32 *players_begin_frame(void);         /* player 2's record for this frame, or 0 */
void players_replay_frame(const u32 *record); /* replay: this frame's recorded one instead */
void players_net_frame(const u32 (*records)[12], u32 n);   /* networked: tick i gets records[i] */
u8  *players_p2(void);                        /* player 2's character, or 0 */
u8  *players_char(int who);                   /* 0: Konoko, 1: player 2; or 0 */
void players_note_hit(u8 *victim, u8 *attacker);   /* from TakeDamage, in a tick */
int  players_can_change(int who);            /* alive: an F2 choice applies at once */
int  players_score(int who);                  /* 0: Konoko (player 1), 1: player 2 */
int  players_enabled(void);                   /* Arklite.ini [players] p2 isn't 0 */
u32  players_respawn_ticks(void);             /* [multiplayer] respawn_delay, in ticks */
void players_set_respawn_ticks(u32 ticks);    /* the network host's, on the joiner */
void players_reset_scores(void);              /* and the class choices: the match starts */
void players_tick_record(const u32 *record);  /* in the tick: Konoko's record (its choice word) */
int  multiplayer_active(void);                /* a player 2 is in the game: no pausing, no auto-aim */
int  multiplayer_map(void);                   /* two or more Player-flagged characters (spawn points) */

/* ---- mp_rules.c -------------------------------------------------------------------------- */

void mp_rules_install(void);   /* multiplayer maps: free for all, damage x1.25, overhealing drains 1.25x faster */

/* ---- items.c ----------------------------------------------------------------------------- */

void items_install(void);
void items_tick(void);   /* in a multiplayer game's tick: taken items come back after a while */
u32  items_default_ticks(void);             /* [multiplayer] item_respawn, in ticks */
void items_set_default_ticks(u32 ticks);    /* the network host's, on the joiner */

/* ---- live_menu.c ------------------------------------------------------------------------- */

void live_menu_install(void);
int  live_menu_open(void);     /* the pause menu is open and the game runs behind it */
void live_menu_request(void);  /* open it after this frame's update */
/* WMrDialog_ModalBegin for every dialog (lockstep.c wraps the calls): in a multiplayer game a
 * dialog outside a tick runs with the game going on behind it, one inside a tick isn't shown */
u16  live_menu_modal(u32 id, WMtWindow *parent, WMtDialogProc proc, u32 param, u32 *result, int in_tick);

/* ---- vertex_light.c ---------------------------------------------------------------------- */

void vertex_light_frame(void);   /* every frame: animated corners of a custom level (vertexlight.bin) */

/* ---- crash_log.c -------------------------------------------------------------------------- */

void crash_log_install(void *instance);   /* log faults with registers and stack */
void crash_log_exit(void);                /* log the stack when the process exits */
