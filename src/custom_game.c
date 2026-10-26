/* Custom Game: a main-menu entry that lists and starts custom levels, alone or over the network.
 *
 * Custom levels live in GameDataFolder\CustomLevels\<folder>\levelN_<suffix>.dat. The
 * engine's startup scan never sees that folder, so level numbers can't collide. When a
 * level is picked, its files are added to the engine's level file table under a free
 * level slot and the level is started from that slot.
 *
 * The button opens a popup (mp_menu.c): New lists the levels with Play (alone), Host (the Host
 * settings dialog, then the level as a network host; a level needs two or more spawns, that is
 * Player-flagged characters, read from its files) and Back. Join lists bookmarked servers
 * (mp_menu.c) and starts the host's map in the host's level slot. */
#include "arklite.h"

__declspec(dllimport) int  WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) u32  WINAPI SetFilePointer(HANDLE, int, int *, u32);
__declspec(dllimport) int  WINAPI CloseHandle(HANDLE);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int  WINAPI VirtualFree(void *, u32, u32);
void *memset(void *, int, unsigned);

/* ---- Custom level discovery ------------------------------------------------------------ */

#define MAX_CUSTOM      128
#define MAX_LEVEL_FILES 8
#define ITEM_CUSTOM     0x200     /* listbox item data: ITEM_CUSTOM + index; < 127 = level number */
#define ID_CUSTOM_GAME  105

typedef struct {
	char folder[64];
	char name[96];
	char files[MAX_LEVEL_FILES][64];
	u32  hash[MAX_LEVEL_FILES];
	int  nfiles;
	u32  source;   /* original level it was made from (level.ini source=, or the file's number) */
	int  spawns;   /* Player-flagged characters (spawn points), -1: not counted yet */
} CustomLevel;

static CustomLevel g_custom[MAX_CUSTOM];
static int  g_ncustom;
static char g_dataDir[MAX_PATH];               /* absolute, ends with '\' */
static char g_igmdFormat[256] = "IGMD\\%s";
static int  g_slot = -1;
static char g_mountedIni[MAX_PATH];           /* level.ini of the level mounted in g_slot */
static char g_mountedDir[MAX_PATH];           /* its folder, ends with '\' */
static char g_mountedFolder[64];              /* its folder's name in CustomLevels */
static char g_mountedName[96];                /* its name (level.ini) */
static int  g_forceSlot = -1;                 /* joining: the host's slot, so both load the same level number */
/* Debug: ARKLITE_AUTOSTART=<folder> opens Custom Game and plays that folder, as if clicked. */
static char g_autostart[64];
static int  g_autostartPending;
/* ARKLITE_SAVEPOINT=<n> (with AUTOSTART): start that level at save point n. */
static u32  g_autostartSavePoint;
static u32  g_startSavePoint;

/* persist.dat save games in memory (ONrPersist, 0x50f320): [40 levels][10 save points],
 * 0x204 bytes each, byte 0x40 bit 0 = present. */
#define PERSIST_SAVES  ((u8 *)0x5ed3a0)
#define PERSIST_ENTRY  0x204

static u8 *persist_entry(u32 level, u32 sp) { return PERSIST_SAVES + (level * 10 + sp) * PERSIST_ENTRY; }

static int persist_valid(u32 level, u32 sp)
{
	return level < 40 && sp < 10 && (persist_entry(level, sp)[0x40] & 1);
}

static const u8 g_original[] = { 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };

static int is_original(u32 level)
{
	for (unsigned i = 0; i < sizeof g_original; i++)
		if (g_original[i] == level) return 1;
	return 0;
}

/* Mirrors the engine's parser (0x420590): "level<N>_<suffix>.dat". The suffix hash is 0
 * for "Final", otherwise sum((toupper(c) - 0x40) * i), i = 1, 2, ... */
static int parse_level_file(const char *leaf, u32 *level, u32 *hash)
{
	const char *p, *dot = 0;
	u32 n = 0, h = 0, i = 1;

	if (!str_ieq_n(leaf, "level", 5)) return 0;
	p = leaf + 5;
	if (*p < '0' || *p > '9') return 0;
	while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
	if (*p++ != '_' || n > 127) return 0;
	for (const char *q = p; *q; q++)
		if (*q == '.') dot = q;
	if (!dot) return 0;
	if (dot - p == 5 && p[0] == 'F' && p[1] == 'i' && p[2] == 'n' && p[3] == 'a' && p[4] == 'l')
		h = 0;
	else
		for (; p < dot; p++, i++) h += (to_upper(*p) - 0x40) * i;
	*level = n;
	*hash = h;
	return 1;
}

static void scan_custom_levels(void)
{
	char pattern[MAX_PATH], path[MAX_PATH];
	WIN32_FIND_DATAA fd, ff;
	HANDLE hd, hf;

	g_ncustom = 0;
	fmt(pattern, "%sCustomLevels\\*", g_dataDir);
	hd = FindFirstFileA(pattern, &fd);
	if (hd == INVALID_HANDLE_VALUE) {
		log_msg("no CustomLevels folder (%s)", pattern);
		return;
	}
	do {
		CustomLevel *cl = &g_custom[g_ncustom];

		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
		if (!str_cpy(cl->folder, sizeof cl->folder, fd.cFileName)) continue;
		cl->nfiles = 0;
		cl->source = 0;
		cl->spawns = -1;

		fmt(path, "%sCustomLevels\\%s\\level*.dat", g_dataDir, cl->folder);
		hf = FindFirstFileA(path, &ff);
		if (hf != INVALID_HANDLE_VALUE) {
			do {
				u32 level, hash;
				if (cl->nfiles == MAX_LEVEL_FILES || str_contains_i(ff.cFileName, "_tools")) continue;
				if (!parse_level_file(ff.cFileName, &level, &hash)) continue;
				if (!str_cpy(cl->files[cl->nfiles], sizeof cl->files[0], ff.cFileName)) continue;
				if (cl->nfiles == 0) cl->source = level;
				cl->hash[cl->nfiles++] = hash;
			} while (FindNextFileA(hf, &ff));
			FindClose(hf);
		}
		if (cl->nfiles == 0) {
			log_msg("CustomLevels\\%s: no level*.dat, skipped", cl->folder);
			continue;
		}

		fmt(path, "%sCustomLevels\\%s\\level.ini", g_dataDir, cl->folder);
		GetPrivateProfileStringA("level", "name", cl->folder, cl->name, sizeof cl->name, path);
		{
			char src[16];
			u32 n = 0;
			GetPrivateProfileStringA("level", "source", "", src, sizeof src, path);
			for (const char *c = src; *c >= '0' && *c <= '9'; c++) n = n * 10 + (*c - '0');
			if (n > 0 && n < 127) cl->source = n;
		}
		log_msg("found \"%s\" in CustomLevels\\%s (%d file(s))", cl->name, cl->folder, cl->nfiles);
		g_ncustom++;
	} while (g_ncustom < MAX_CUSTOM && FindNextFileA(hd, &fd));
	FindClose(hd);
}

/* ---- Mounting ---------------------------------------------------------------------------- */

static int slot_in_use(u32 s)
{
	if (TMgLevelValid[s]) return 1;
	for (u32 i = 0; i < TMgLevelFileCount; i++)
		if ((TMgLevelFiles[i].file_index >> 25) == s) return 1;
	return 0;
}

/* The slot's level files go to slot 127 (never loaded) and the level number is free again */
static void release_slot(u32 s)
{
	for (u32 i = 0; i < TMgLevelFileCount; i++)
		if ((TMgLevelFiles[i].file_index >> 25) == s) TMgLevelFiles[i].file_index = (127u << 25) | 1;
	TMgLevelValid[s] = 0;
}

static int choose_slot(void)
{
	int want = g_forceSlot;

	g_forceSlot = -1;
	if (want >= 20 && want <= 31 && want != g_slot) {
		if (slot_in_use(want)) {
			log_msg("mount: the host's slot %d is in use here, using %d", want, g_slot);
		} else {
			if (g_slot >= 0) release_slot(g_slot);
			g_slot = want;
		}
	}
	/* Below 32 (the wiki reports glitches above 31) and below 40 (persist.dat save slots). */
	if (g_slot < 0)
		for (int s = 31; s >= 20; s--)
			if (!slot_in_use(s)) { g_slot = s; break; }
	return g_slot;
}

static int mount_custom_level(const CustomLevel *cl)
{
	int reuse[MAX_LEVEL_FILES], nreuse = 0;
	u32 s = choose_slot();
	char path[MAX_PATH];

	if ((int)s < 0) {
		log_msg("mount: no free level slot");
		return 0;
	}

	/* Entries left by a previous mount in this slot are reused. */
	for (u32 i = 0; i < TMgLevelFileCount && nreuse < MAX_LEVEL_FILES; i++)
		if ((TMgLevelFiles[i].file_index >> 25) == s) reuse[nreuse++] = i;

	for (int f = 0; f < cl->nfiles; f++) {
		TMtLevelFileEntry *e;

		fmt(path, "%sCustomLevels\\%s\\%s", g_dataDir, cl->folder, cl->files[f]);
		if (str_len(path) >= 0xff) {
			log_msg("mount: path too long: %s", path);
			return 0;
		}
		if (f < nreuse) {
			e = &TMgLevelFiles[reuse[f]];
		} else {
			if (TMgLevelFileCount >= TMcLevelFiles_Max) {
				log_msg("mount: level file table full");
				return 0;
			}
			e = &TMgLevelFiles[TMgLevelFileCount];
			e->ref = UUrMemory_Block_New(0x100);
			if (!e->ref) return 0;
			TMgLevelFileCount++;
		}
		str_cpy(e->ref, 0x100, path);
		e->file_index = (((cl->hash[f] & 0xffffff) | (s << 24)) << 1) | 1;
		log_msg("mount: slot %d <- %s (index 0x%08x)", s, path, e->file_index);
	}
	/* Leftover entries from a bigger previous level: park them on slot 127, never loaded. */
	for (int f = cl->nfiles; f < nreuse; f++)
		TMgLevelFiles[reuse[f]].file_index = (127u << 25) | 1;

	TMgLevelValid[s] = 1;
	fmt(g_mountedIni, "%sCustomLevels\\%s\\level.ini", g_dataDir, cl->folder);
	fmt(g_mountedDir, "%sCustomLevels\\%s\\", g_dataDir, cl->folder);
	str_cpy(g_mountedFolder, sizeof g_mountedFolder, cl->folder);
	str_cpy(g_mountedName, sizeof g_mountedName, cl->name);

	fmt(path, "%sCustomLevels\\%s\\IGMD", g_dataDir, cl->folder);
	if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
		fmt(g_igmdFormat, "CustomLevels\\%s\\IGMD\\%%s", cl->folder);
	else
		str_cpy(g_igmdFormat, sizeof g_igmdFormat, "IGMD\\%s");
	log_msg("mount: scripts from %s", g_igmdFormat);
	return 1;
}

/* A mounted file was built for another level number, so its instance headers carry that
 * number's file index. Rewrite them to the slot's index before the engine resolves them. */
static u16 ONI_FC open_level_file_hook(char *ref, void **out)
{
	u16 error = TMrLevel_OpenFile(ref, out);

	if (error || g_slot < 0 || !*out) return error;
	for (u32 i = 0; i < TMgLevelFileCount; i++) {
		if (TMgLevelFiles[i].ref != ref || (TMgLevelFiles[i].file_index >> 25) != (u32)g_slot) continue;

		u32 index = TMgLevelFiles[i].file_index, n = TMmFile_InstanceCount(*out), fixed = 0;
		u32 *desc = TMmFile_Descriptors(*out);
		for (u32 k = 0; k < n; k++, desc += 5) {
			if ((desc[4] & 2) || !desc[1]) continue;
			u32 *level_id = (u32 *)(desc[1] - 4);
			if ((*level_id & 1) && *level_id != index) { *level_id = index; fixed++; }
		}
		log_msg("open: %s: %u instances, %u level ids set to 0x%08x", ref, n, fixed, index);
		break;
	}
	return error;
}

const char *custom_game_level_ini(u32 level)
{
	return g_slot >= 0 && level == (u32)g_slot && g_mountedIni[0] ? g_mountedIni : 0;
}

const char *custom_game_level_dir(u32 level)
{
	return custom_game_level_ini(level) ? g_mountedDir : 0;
}

const char *custom_game_level_folder(u32 level)
{
	return custom_game_level_ini(level) ? g_mountedFolder : 0;
}

const char *custom_game_level_name(u32 level)
{
	return custom_game_level_ini(level) ? g_mountedName : 0;
}

static int find_folder(const char *folder)
{
	for (int i = 0; i < g_ncustom; i++)
		if (str_ieq_n(g_custom[i].folder, folder, sizeof g_custom[i].folder)) return i;
	return -1;
}

int custom_game_has_folder(const char *folder)
{
	scan_custom_levels();
	return find_folder(folder) >= 0;
}

/* The mounted level's folder changed on disk (net.c downloaded the host's version): its files
 * are listed again and mounted in the same slot, before the engine opens them */
int custom_game_remount(u32 level)
{
	char folder[64];

	if (!custom_game_level_ini(level)) return 0;
	str_cpy(folder, sizeof folder, g_mountedFolder);
	scan_custom_levels();
	for (int i = 0; i < g_ncustom; i++)
		if (str_ieq_n(g_custom[i].folder, folder, sizeof folder)) return mount_custom_level(&g_custom[i]);
	log_msg("remount: CustomLevels\\%s has no level files any more", folder);
	return 0;
}

/* ---- Player spawns ------------------------------------------------------------------------ */

/* Player-flagged CHAR objects in a level file, without loading it. The objects of each type are
 * in a BINA instance named "CJBO<type>" (BINACJBOCharacter for CHAR), whose data in the .dat is
 * {size, offset in the .raw}. There: "OBJC" tag, size, version, then one object after another
 * ({u32 size; tag "CHAR" (stored "RAHC"); id; flags; position; rotation; CHAR's flags with bit 0
 * IsPlayer ...}) until a size of 0. */
static int read_at(HANDLE f, u32 offset, void *buf, u32 n)
{
	u32 got;
	return SetFilePointer(f, (int)offset, 0, 0) == offset && ReadFile(f, buf, n, &got, 0) && got == n;
}

static int count_spawns_file(const char *dat)
{
	HANDLE f = CreateFileA(dat, 0x80000000 /* GENERIC_READ */, 3, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
	u32 head[16], bina[2] = { 0, 0 }, ninst, data_at, names_at, names_size;
	u8 *buf = 0;
	char raw[MAX_PATH];
	int found = 0, spawns = 0;

	if (f == INVALID_HANDLE_VALUE) return 0;
	if (!read_at(f, 0, head, sizeof head)) goto done;
	ninst = head[5];
	data_at = head[8];
	names_at = head[10];
	names_size = head[11];
	if (ninst > 0x100000 || names_size > 0x4000000) goto done;
	buf = VirtualAlloc(0, ninst * 20 + names_size + 1, 0x3000, 4);
	if (!buf || !read_at(f, 0x40, buf, ninst * 20) || !read_at(f, names_at, buf + ninst * 20, names_size)) goto done;
	buf[ninst * 20 + names_size] = 0;
	for (u32 i = 0; i < ninst && !found; i++) {
		const u32 *d = (const u32 *)(buf + i * 20);   /* tag, data offset, name offset, size, flags */
		if (d[0] != 0x42494e41 /* BINA */ || (d[4] & 1) || d[2] >= names_size) continue;
		const char *name = (const char *)buf + ninst * 20 + d[2];
		if (str_len(name) == 17 && str_ieq_n(name, "BINACJBOCharacter", 17))
			found = read_at(f, data_at + d[1], bina, sizeof bina);
	}
done:
	if (buf) VirtualFree(buf, 0, 0x8000 /* MEM_RELEASE */);
	CloseHandle(f);
	if (!found || bina[0] < 12 || bina[0] > 0x1000000) return 0;

	/* levelN_X.dat -> levelN_X.raw */
	if (!str_cpy(raw, sizeof raw, dat) || str_len(raw) < 4) return 0;
	str_cpy(raw + str_len(raw) - 3, 4, "raw");
	f = CreateFileA(raw, 0x80000000, 3, 0, 3, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return 0;
	buf = VirtualAlloc(0, bina[0], 0x3000, 4);
	if (buf && read_at(f, bina[1], buf, bina[0])) {
		for (u32 at = 12; at + 0x2c <= bina[0];) {
			u32 size = *(u32 *)(buf + at);
			if (!size) break;
			if (*(u32 *)(buf + at + 4) == 0x43484152 /* CHAR */ && (*(u32 *)(buf + at + 0x28) & 1)) spawns++;
			at += 4 + size;
		}
	}
	if (buf) VirtualFree(buf, 0, 0x8000);
	CloseHandle(f);
	return spawns;
}

static int custom_spawns(CustomLevel *cl)
{
	char path[MAX_PATH];

	if (cl->spawns >= 0) return cl->spawns;
	cl->spawns = 0;
	for (int f = 0; f < cl->nfiles; f++) {
		fmt(path, "%sCustomLevels\\%s\\%s", g_dataDir, cl->folder, cl->files[f]);
		if (str_len(path) > 4 && str_ieq_n(path + str_len(path) - 4, ".dat", 4)) cl->spawns += count_spawns_file(path);
	}
	log_msg("CustomLevels\\%s: %d player spawn(s)", cl->folder, cl->spawns);
	return cl->spawns;
}

/* A numbered level installed the classic way: its files in the engine's table */
static int level_spawns(u32 level)
{
	int n = 0;
	for (u32 i = 0; i < TMgLevelFileCount; i++) {
		const char *ref = TMgLevelFiles[i].ref;
		u32 len = ref ? str_len(ref) : 0;
		if ((TMgLevelFiles[i].file_index >> 25) == level && len > 4 && str_ieq_n(ref + len - 4, ".dat", 4))
			n += count_spawns_file(ref);
	}
	return n;
}

/* ---- New: the level list (the Load Game dialog 0x97, its list as it is) ---------------------- */

#define SEL_HOST        0x10000   /* with the list's item data: the level is to be hosted */
#define MAX_ROWS        (MAX_CUSTOM + 127)
#define ROW_TEXT_W      345       /* the list's 375 px less its scroll bar and margins */
enum { ID_PLAY = 1, ID_BACK = 2, ID_HOST = 3, ID_LIST = 100 };

static WMtWindow *g_lb, *g_status, *g_host;
static int  g_rowSpawns[MAX_ROWS];
static char g_rowName[MAX_ROWS][96];
static int  g_nrows, g_shownRow = -2;
static void *g_listHookOld;

static void add_row(const char *name, u32 data, int spawns)
{
	char fit[128];
	u32 index;

	if (g_nrows >= MAX_ROWS) return;
	/* As in the Load Game list (0x46e7e0): SetItemData takes p1 = data, p2 = index. */
	ui_fit(fit, sizeof fit, name, ROW_TEXT_W);
	index = WMrMessage_Send(g_lb, LBcMessage_AddString, (u32)fit, 0);
	WMrMessage_Send(g_lb, LBcMessage_SetItemData, data, index);
	g_rowSpawns[g_nrows] = spawns;
	str_cpy(g_rowName[g_nrows], sizeof g_rowName[0], name);
	g_nrows++;
}

static int selected_row(void)
{
	int row = g_lb ? (int)WMrMessage_Send(g_lb, LBcMessage_GetSelection, 0, 0) : -1;
	return row >= 0 && row < g_nrows ? row : -1;
}

/* The selected level's spawn count decides whether Host can be clicked */
static void show_selection(void)
{
	int row = selected_row(), spawns;
	char text[128];

	if (row == g_shownRow) return;
	g_shownRow = row;
	spawns = row >= 0 ? g_rowSpawns[row] : -1;
	if (g_host) WMrWindow_SetEnabled(g_host, spawns >= 2);
	text[0] = 0;
	if (spawns >= 0 && spawns < 2)
		fmt(text, "This map has %d player spawn%s; hosting needs 2.", spawns, spawns == 1 ? "" : "s");
	ui_set_text_fit(g_status, text);
}

static void list_frame(void)
{
	ui_frame_hook_call(g_listHookOld);
	show_selection();
}

static void fill_level_list(WMtWindow *dialog)
{
	WMtWindow *play = WMrDialog_GetItemByID(dialog, ID_PLAY);
	WMtWindow *back = WMrDialog_GetItemByID(dialog, ID_BACK);
	u8 listed[127] = { 0 };
	char label[128];

	g_lb = WMrDialog_GetItemByID(dialog, ID_LIST);
	g_status = g_host = 0;
	g_nrows = 0;
	g_shownRow = -2;
	if (!g_lb || !play || !back) {
		log_msg("custom game: the Load Game dialog's list or buttons are missing");
		g_lb = 0;
		return;
	}

	/* New windows go right after the list in the drawing order, before the outline frame,
	 * whose translucent fill would dim them. Buttons stay 130x40 (stock Load at 120, Cancel at 410). */
	ui_label(dialog, g_lb, play, "New Game", 5, 10, 110, 20, UI_LEFT);
	ui_set_frame(g_lb, 140, 30, 375, 340);
	WIN_STYLE(g_lb) |= LBcStyle_OwnerDraw;   /* rows drawn by custom_game_proc: multiplayer maps in blue */
	g_status = ui_label(dialog, g_lb, play, "", 140, 375, 375, 20, UI_CENTER);
	ui_set_text(play, "Play");
	ui_set_text(back, "Back");
	g_host = ui_add(dialog, play, back, 0, ~0u, "Host", ID_HOST, 265, WIN_Y(play), WIN_W(play), WIN_H(play));

	scan_custom_levels();
	for (int i = 0; i < g_ncustom; i++)
		add_row(g_custom[i].name, ITEM_CUSTOM + i, custom_spawns(&g_custom[i]));

	/* Non-original levels installed the classic way (numbered files in GameDataFolder). */
	u16 nld = TMrInstance_GetTagCount(TMcTag_ONLD);
	for (u32 k = 0; k < nld; k++) {
		u8 *onld;
		if (TMrInstance_GetDataPtr_ByNumber(TMcTag_ONLD, k, (void **)&onld) != 0) break;
		u32 level = *(u16 *)onld;
		if (level >= 127 || listed[level] || !TMgLevelValid[level] || is_original(level) || (int)level == g_slot) continue;
		listed[level] = 1;
		fmt(label, "%s (level %d)", (char *)(onld + 4), level);
		add_row(label, level, level_spawns(level));
	}
	for (u32 level = 1; level < 127; level++) {
		if (listed[level] || !TMgLevelValid[level] || is_original(level) || (int)level == g_slot) continue;
		fmt(label, "Level %d", level);
		add_row(label, level, level_spawns(level));
	}

	if (g_nrows == 0) add_row("(no custom levels found)", 0, -1);
	WMrWindow_SetFocus(g_lb);
	show_selection();

	if (g_autostart[0]) {
		for (int i = 0; i < g_ncustom; i++)
			if (str_ieq_n(g_custom[i].folder, g_autostart, sizeof g_autostart)) {
				log_msg("autostart: %s (save point %d)", g_custom[i].folder, (int)g_autostartSavePoint);
				g_startSavePoint = g_autostartSavePoint;
				WMrDialog_ModalEnd(dialog, ITEM_CUSTOM + i);
			}
		g_autostart[0] = 0;
	}
}

static u8 ONI_FC custom_game_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	u32 id = p1 & 0xffff, code = p1 >> 16;

	if (msg == WMcMessage_InitDialog) {
		fill_level_list(dialog);
		return 1;
	}
	if (msg == LBcMessage_DrawRow && id == ID_LIST && p2) {
		u32 row = *(u32 *)(p2 + 0xc);
		return ui_list_draw_row(p2, row < (u32)g_nrows && g_rowSpawns[row] >= 2 ? UI_BLUE : UI_ORANGE);
	}
	if (msg != WMcMessage_Command) return 0;

	if ((id == ID_PLAY && code == WMcNotify_Click) || (id == ID_LIST && code == WMcNotify_DoubleClick) ||
	    (id == ID_HOST && code == WMcNotify_Click)) {
		int row = selected_row();
		u32 data = row >= 0 ? WMrMessage_Send(g_lb, LBcMessage_GetItemData, 0, (u32)row) & 0xffff : 0xffff;
		log_msg("custom game: %s item data 0x%x", id == ID_HOST ? "host" : "play", data);
		if (data == 0 || data == 0xffff) return 1;
		if (id != ID_HOST) {
			WMrDialog_ModalEnd(dialog, data);
		} else if (g_rowSpawns[row] < 2) {
			g_shownRow = -2;   /* say why again */
			show_selection();
		} else if (mp_menu_host(g_rowName[row])) {
			WMrDialog_ModalEnd(dialog, data | SEL_HOST);
		}
	} else if (id == ID_BACK && code == WMcNotify_Click) {
		WMrDialog_ModalEnd(dialog, 0);
	}
	return 1;
}

/* The level list: the item data of the level to start (| SEL_HOST), 0 for Back */
static u32 run_level_list(void)
{
	u32 selection = 0;

	g_listHookOld = ui_frame_hook(list_frame);
	WMrDialog_ModalBegin(WMcDialogID_LoadGame, 0, custom_game_proc, 0, &selection);
	ui_frame_hook(g_listHookOld);
	return selection;
}

/* ---- Main menu ------------------------------------------------------------------------------- */

static void add_custom_game_button(WMtWindow *dialog)
{
	WMtWindow *load = WMrDialog_GetItemByID(dialog, 101);
	WMtWindow *first = WMrDialog_GetItemByID(dialog, 100);
	WMtWindow *button;
	int y0, old_step, new_step, new_y0;

	if (WMrDialog_GetItemByID(dialog, ID_CUSTOM_GAME)) return;
	if (!load || !first) {
		log_msg("main menu: New Game/Load Game buttons not found");
		return;
	}

	/* Squeeze the button rows (60px apart in the stock menu) to make room for one more row
	 * right below Load Game. Buttons sharing a row (Options/Resume) stay together. */
	y0 = WIN_Y(first);
	old_step = WIN_Y(load) - y0;
	if (old_step <= 0) old_step = 60;
	new_step = old_step * 5 / 6;
	new_y0 = y0 - (old_step - new_step) / 2;
	for (u32 id = 100; id <= 104; id++) {
		WMtWindow *w = WMrDialog_GetItemByID(dialog, id);
		if (!w) continue;
		int row = (WIN_Y(w) - y0 + old_step / 2) / old_step;
		if (row >= 2) row++;
		WMrWindow_SetLocation(w, WIN_X(w), new_y0 + row * new_step);
	}

	button = WMrWindow_New(WIN_CLASS(load), "Custom Game", WIN_FLAGS(load), WIN_STYLE(load), ID_CUSTOM_GAME,
	                       WIN_X(load), new_y0 + 2 * new_step, WIN_W(load), WIN_H(load), dialog, 0);
	if (!button) {
		log_msg("main menu: WMrWindow_New failed");
		return;
	}
	WMrWindow_SetFontInfo(button, WIN_FONTINFO(load));
	win_insert_after(button, load);
	log_msg("main menu: added Custom Game button (class %d flags 0x%x style 0x%x)",
	        WIN_CLASS(load), WIN_FLAGS(load), WIN_STYLE(load));
}

/* Save points of a custom level live in its slot. A level copied from an original one has
 * none yet, so the original level's saved game for that save point is used (the player
 * must have reached it there). Returns the save point to start at (0 = beginning). */
static u32 prepare_save_point(const CustomLevel *cl, u32 slot, u32 sp)
{
	if (sp == 0 || sp >= 10) return 0;
	if (persist_valid(slot, sp)) return sp;
	if (cl->source && persist_valid(cl->source, sp)) {
		u8 *from = persist_entry(cl->source, sp), *to = persist_entry(slot, sp);
		for (u32 i = 0; i < PERSIST_ENTRY; i++) to[i] = from[i];
		log_msg("save point %d: using level %d's saved game", (int)sp, (int)cl->source);
		return sp;
	}
	/* Level 1's save point 0 is Combat Training; save point 1 is the Warehouse start, which
	 * Catwalk uses as "beginning". Its script places Konoko itself, and restore_game
	 * (0x4fd8b0) just logs "invalid save" when there's nothing to restore. */
	if (cl->source == 1 && sp == 1) {
		log_msg("save point 1: no saved game, starting level 1 after Combat Training");
		return sp;
	}
	log_msg("save point %d: no saved game in slot %d or level %d; starting from the beginning",
	        (int)sp, (int)slot, (int)cl->source);
	return 0;
}

static void start_selection(WMtWindow *main_menu, u32 selection)
{
	u32 level, sp = 0;

	selection &= 0xffff;
	if (selection >= ITEM_CUSTOM) {
		const CustomLevel *cl = &g_custom[selection - ITEM_CUSTOM];
		if (selection - ITEM_CUSTOM >= (u32)g_ncustom || !mount_custom_level(cl))
			return;
		level = g_slot;
		sp = prepare_save_point(cl, level, g_startSavePoint);
		g_startSavePoint = 0;
	} else {
		str_cpy(g_igmdFormat, sizeof g_igmdFormat, "IGMD\\%s");
		level = selection;
	}
	log_msg("starting level %d at save point %d", level, (int)sp);

	/* Same sequence the main menu uses for New Game / Load Game. */
	ONiMainMenu_FadeOut();
	WMrDialog_ModalEnd(main_menu, 0);
	ONiMainMenu_Leave1();
	ONiMainMenu_Leave2();
	ONrGame_StartLevel(level | sp << 8);
}

/* Custom Game: the New/Join popup until a level is picked or it's cancelled. Returns the item
 * data of the level to start (0: none), with net.c set to play alone, host or join */
static u32 custom_game_menu(void)
{
	char folder[64];
	u32 level, selection;

	if (g_autostart[0]) return run_level_list();   /* the test path: networking as the environment says */
	for (;;) {
		int choice = mp_menu_choose();
		if (choice == MPMENU_NEW) {
			selection = run_level_list();
			if (!selection) continue;   /* Back */
			if (!(selection & SEL_HOST)) net_menu_local();
			return selection;
		}
		if (choice != MPMENU_JOIN) return 0;
		if (!mp_menu_join(folder, sizeof folder, &level)) continue;
		if (!folder[0]) return level;   /* a numbered level: both have it in the same slot */
		scan_custom_levels();
		int i = find_folder(folder);
		if (i < 0) return 0;   /* mp_menu_join checked it's here */
		g_forceSlot = (int)level;
		return ITEM_CUSTOM + i;
	}
}

static u8 ONI_FC main_menu_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	WMtDialogProc original = (WMtDialogProc)ONcMainMenu_Proc;

	if (g_autostartPending && msg != WMcMessage_InitDialog && msg != WMcMessage_Command &&
	    WMrDialog_GetItemByID(dialog, ID_CUSTOM_GAME)) {
		g_autostartPending = 0;
		return main_menu_proc(dialog, WMcMessage_Command, ID_CUSTOM_GAME | (WMcNotify_Click << 16), 0);
	}

	if (msg == WMcMessage_Command && (p1 & 0xffff) == ID_CUSTOM_GAME) {
		if ((p1 >> 16) == WMcNotify_Click) {
			void *saved = ONiUI_GetContext();
			void *ui = ONiUI_FindContext(ONcUIContextName);
			if (ui) ONiUI_SetContext(ui);
			u32 selection = custom_game_menu();
			ONiUI_SetContext(saved);
			if (selection) start_selection(dialog, selection);
		}
		return 1;
	}

	u8 handled = original(dialog, msg, p1, p2);
	if (msg == WMcMessage_InitDialog) add_custom_game_button(dialog);
	return handled;
}

/* ---- Install ------------------------------------------------------------------------------- */

void custom_game_install(void)
{
	if (!GetFullPathNameA("GameDataFolder\\", sizeof g_dataDir, g_dataDir, 0)) g_dataDir[0] = 0;
	log_msg("custom game: data folder %s", g_dataDir);
	if (GetEnvironmentVariableA("ARKLITE_AUTOSTART", g_autostart, sizeof g_autostart) - 1 < sizeof g_autostart - 1)
		g_autostartPending = 1;
	else
		g_autostart[0] = 0;
	{
		char sp[8];
		if (GetEnvironmentVariableA("ARKLITE_SAVEPOINT", sp, sizeof sp) - 1 < sizeof sp - 1 &&
		    sp[0] >= '0' && sp[0] <= '9')
			g_autostartSavePoint = (u32)(sp[0] - '0');
	}

	patch_push_imm32(ONcMainMenu_ProcPushAddr, ONcMainMenu_Proc, (u32)main_menu_proc);
	mp_menu_install();
	patch_push_imm32(SLcIGMDPushAddr1, SLcIGMDFormat, (u32)g_igmdFormat);
	patch_push_imm32(SLcIGMDPushAddr2, SLcIGMDFormat, (u32)g_igmdFormat);
	patch_call(TMcLevel_OpenFileCallAddr, (u32)TMrLevel_OpenFile, (u32)open_level_file_hook);
	log_msg("custom game: patches applied");
}
