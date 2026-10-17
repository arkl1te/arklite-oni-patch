/* Engine addresses and calling conventions for the original Oni.exe
 * (sha256 363cf09684ca6b729b62f22f90701d7ebf9a33778e55b191f5040bccdcdd013d).
 *
 * Bungie's code passes the first two arguments in ECX/EDX and the rest on the stack,
 * callee cleans up: that is exactly __fastcall. Names in brackets come from Daodan's
 * symbol table; the others are ours (see ../../custom-game/re/ and ../../docs/custom-game.md). */
#pragma once

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define ONI_FC __fastcall

/* ---- Window manager ---------------------------------------------------------------- */

typedef struct WMtWindow WMtWindow;
typedef u8 (ONI_FC *WMtDialogProc)(WMtWindow *dialog, u32 msg, u32 p1, u32 p2);

/* Window layout (from WMrWindow_New @ 0x475160) */
#define WIN_FLAGS(w)    (*(u32 *)((u8 *)(w) + 0x18))
#define WIN_STYLE(w)    (*(u32 *)((u8 *)(w) + 0x1c))
#define WIN_ID(w)       (*(u16 *)((u8 *)(w) + 0x20))
#define WIN_X(w)        (*(short *)((u8 *)(w) + 0x28))
#define WIN_Y(w)        (*(short *)((u8 *)(w) + 0x2a))
#define WIN_W(w)        (*(short *)((u8 *)(w) + 0x2c))
#define WIN_H(w)        (*(short *)((u8 *)(w) + 0x2e))
#define WIN_CLASS(w)    (**(u16 **)((u8 *)(w) + 0x14))
#define WIN_FONTINFO(w) ((void *)((u8 *)(w) + 0x148))
#define WIN_NEXT(w)       (*(WMtWindow **)((u8 *)(w) + 4))
#define WIN_FIRSTCHILD(w) (*(WMtWindow **)((u8 *)(w) + 0x10))
#define WIN_VISIBLE(w)    (WIN_FLAGS(w) & 1)

#define WMcClass_CheckBox 5
#define CBcStyle_Checked  0x10000

#define WMcMessage_Command    0x17
#define WMcMessage_InitDialog 0x3d
/* Posted to a dialog for Esc (key 0x1b) by the default dialog proc (0x468cd0), which then
 * turns it into Command(id 2, notify 1, no window) unless the dialog proc handles it. */
#define WMcMessage_Cancel     0x14
#define WMcNotify_Click       1
#define WMcNotify_DoubleClick 2
#define LBcMessage_AddString   0x45
#define LBcMessage_GetItemData 0x47
#define LBcMessage_SetItemData 0x51

#define WMcDialogID_MainMenu 0x96
#define WMcDialogID_LoadGame 0x97

/* [WMrWindow_New] cls, title, flags, style, id, x, y, w, h, parent, param */
#define WMrWindow_New ((WMtWindow *(ONI_FC *)(u32, const char *, u32, u32, u32, u32, u32, u32, u32, WMtWindow *, u32))0x475160)
/* [WMrWindow_SetLocation] */
#define WMrWindow_SetLocation ((u8 (ONI_FC *)(WMtWindow *, int, int))0x4756d0)
#define WMrWindow_SetFontInfo ((u8 (ONI_FC *)(WMtWindow *, const void *))0x475630)
#define WMrWindow_SetTitle    ((u8 (ONI_FC *)(WMtWindow *, const char *, u32))0x475990)
#define WMrWindow_SetFocus    ((void (ONI_FC *)(WMtWindow *))0x4755c0)
#define WMrWindow_SetVisible  ((u8 (ONI_FC *)(WMtWindow *, u32))0x475a10)
#define WMrSlider_GetValue    ((int (ONI_FC *)(WMtWindow *))0x4723a0)
#define WMrSlider_SetValue    ((void (ONI_FC *)(WMtWindow *, int))0x4723b0)
#define WMrSlider_SetRange    ((void (ONI_FC *)(WMtWindow *, int, int))0x472650)
/* Checkbox: msg 0x3b sets the check, 0x2d reads it. A click toggles it, then notifies the parent. */
#define WMrCheckBox_SetCheck  ((void (ONI_FC *)(WMtWindow *, u32))0x46b900)
#define WMrMessage_Send       ((u32 (ONI_FC *)(WMtWindow *, u32, u32, u32))0x4760d0)
#define WMrDialog_GetItemByID ((WMtWindow *(ONI_FC *)(WMtWindow *, u32))0x4689a0)
/* id, parent, proc, param, &result */
#define WMrDialog_ModalBegin  ((u16 (ONI_FC *)(u32, WMtWindow *, WMtDialogProc, u32, u32 *))0x4689d0)
#define WMrDialog_ModalEnd    ((void (ONI_FC *)(WMtWindow *, u32))0x468b90)

/* UI cursor/context save-restore used around every sub-dialog in the main menu */
#define ONiUI_GetContext  ((void *(ONI_FC *)(void))0x46aa60)
#define ONiUI_FindContext ((void *(ONI_FC *)(const char *))0x46aa80)
#define ONiUI_SetContext  ((void (ONI_FC *)(void *))0x46aa90)
#define ONcUIContextName  ((const char *)0x54c898)          /* "psui_oniUI" */

/* What the main menu does between closing and starting a level (see 0x4d2df1..0x4d2e0b) */
#define ONiMainMenu_FadeOut  ((void (ONI_FC *)(void))0x4d3100)
#define ONiMainMenu_Leave1   ((void (ONI_FC *)(void))0x477070)
#define ONiMainMenu_Leave2   ((void (ONI_FC *)(void))0x476140)
/* level | savepoint << 8 */
#define ONrGame_StartLevel   ((void (ONI_FC *)(u32))0x4beb40)

/* The main menu: ModalBegin(0x96, 0, proc, ...) pushes the proc at 0x4d2ca9 */
#define ONcMainMenu_Proc         0x4d2cd0
#define ONcMainMenu_ProcPushAddr 0x4d2ca9

/* ---- Template manager ---------------------------------------------------------------- */

/* [TMrInstance_GetTagCount] / [TMrInstance_GetDataPtr_ByNumber] */
#define TMrInstance_GetTagCount        ((u16 (ONI_FC *)(u32))0x4236f0)
#define TMrInstance_GetDataPtr_ByNumber ((u16 (ONI_FC *)(u32, u32, void **))0x423680)
#define TMcTag_ONLD 0x4f4e4c44   /* 'ONLD': u16 level at +0, name at +4 */
/* [TMrInstance_GetInstanceName] data pointer -> name without the tag ("striker_easy_1"), or 0 */
#define TMrInstance_GetInstanceName ((const char *(ONI_FC *)(void *))0x423d90)

#define UUrMemory_Block_New ((void *(ONI_FC *)(u32))0x424a50)

/* Level file table built by the startup scan (0x4209e0) and read by TMrLevel_Load (0x4217d0) */
typedef struct { char *ref; u32 file_index; } TMtLevelFileEntry;
#define TMgLevelValid      ((u8 *)0x56ecd8)                   /* u8[127]          */
#define TMgLevelFiles      ((TMtLevelFileEntry *)0x56ed58)
#define TMgLevelFileCount  (*(u16 *)0x57119a)
#define TMcLevelFiles_Max  ((0x570198 - 0x56ed58) / 8)        /* 648              */
#define TMgGameDataFolder  (*(char **)0x56ea88)

/* TMrLevel_OpenFile(ref, &file): maps one level file; called only from TMrLevel_Load at 0x4218c1.
 * The loaded file struct holds the instance count at +0x28 and the descriptors at +0x2c
 * (5 dwords: template, data, name, size, flags; flag 2 = no data). Each instance's data is
 * preceded by {instance id, level id}; the level id must equal the file's index, because the
 * engine resolves it by matching loaded files' index (+0x20) at 0x421734. */
#define TMrLevel_OpenFile         ((u16 (ONI_FC *)(char *, void **))0x422590)
#define TMcLevel_OpenFileCallAddr 0x4218c1
#define TMmFile_InstanceCount(f)  (*(u32 *)((u8 *)(f) + 0x28))
#define TMmFile_Descriptors(f)    (*(u32 **)((u8 *)(f) + 0x2c))

/* ---- Scripts ------------------------------------------------------------------------- */

/* sprintf(buf, "IGMD\\%s", levelName): push imm32 at these two addresses */
#define SLcIGMDFormat       0x557300
#define SLcIGMDPushAddr1    0x50fd1e
#define SLcIGMDPushAddr2    0x50ff86
