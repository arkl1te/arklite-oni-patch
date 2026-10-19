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

/* ---- random_class.c----------------------------------------------------------------------- */

void random_class_install(void);

/* ---- char_limit.c ------------------------------------------------------------------------- */

void char_limit_install(void);
u32  char_limit_gs_offset(u32 offset);   /* a stock game state offset, where it is now */

/* ---- lockstep.c -------------------------------------------------------------------------- */

void lockstep_install(void);   /* ARKLITE_RECORD / ARKLITE_REPLAY: input record and replay */
void lockstep_exit(void);      /* finish the recording or the replay report */
void lockstep_get_seeds(u32 *game, u32 *tick_local);   /* the game RNG and the tick-local stream */
void lockstep_set_seeds(u32 game, u32 tick_local);

/* ---- net.c ------------------------------------------------------------------------------- */

void net_install(void);        /* ARKLITE_HOST / ARKLITE_JOIN: two PCs in lockstep over UDP */
void net_exit(void);
int  net_active(void);
u32  net_update(u32 count, void *actions, u32 *ticks_run);   /* the game update while it's on */
void net_tick_done(void);      /* after every tick */

/* ---- players.c --------------------------------------------------------------------------- */

void players_install(void);
const u32 *players_begin_frame(void);         /* player 2's record for this frame, or 0 */
void players_replay_frame(const u32 *record); /* replay: this frame's recorded one instead */
void players_net_frame(const u32 (*records)[12], u32 n);   /* networked: tick i gets records[i] */
u8  *players_p2(void);                        /* player 2's character, or 0 */
int  multiplayer_active(void);                /* a player 2 is in the game: no pausing, no auto-aim */
int  multiplayer_map(void);                   /* two or more Player-flagged characters (spawn points) */

/* ---- mp_rules.c -------------------------------------------------------------------------- */

void mp_rules_install(void);   /* multiplayer maps: damage x1.25, overhealing drains 1.25x faster */

/* ---- items.c ----------------------------------------------------------------------------- */

void items_install(void);
void items_tick(void);   /* in a multiplayer game's tick: taken items come back after a while */

/* ---- live_menu.c ------------------------------------------------------------------------- */

void live_menu_install(void);
int  live_menu_open(void);     /* the pause menu is open and the game runs behind it */
/* WMrDialog_ModalBegin for every dialog (lockstep.c wraps the calls): in a multiplayer game a
 * dialog outside a tick runs with the game going on behind it, one inside a tick isn't shown */
u16  live_menu_modal(u32 id, WMtWindow *parent, WMtDialogProc proc, u32 param, u32 *result, int in_tick);

/* ---- crash_log.c -------------------------------------------------------------------------- */

void crash_log_install(void *instance);   /* log faults with registers and stack */
void crash_log_exit(void);                /* log the stack when the process exits */
