/* Crash log (in Arklite's log, AE\logs\Arklite_<date>_<time>.log).
 *
 * Wine prints nothing when Oni dies while loading a level, so faults and the exit path are
 * logged: registers plus every stack value that points into Oni.exe, Daodan or Arklite. */
#include "arklite.h"

typedef struct { u32 base, end; const char *name; } Module;
static Module g_mods[3];

static void add_module(int i, void *h, const char *name)
{
	u8 *b = h;
	if (!b) return;
	g_mods[i].base = (u32)b;
	g_mods[i].end = (u32)b + *(u32 *)(b + *(u32 *)(b + 0x3c) + 0x50);   /* SizeOfImage */
	g_mods[i].name = name;
}

static const Module *module_of(u32 a)
{
	for (int i = 0; i < 3; i++)
		if (g_mods[i].name && a >= g_mods[i].base && a < g_mods[i].end) return &g_mods[i];
	return 0;
}

static void log_stack(u32 esp)
{
	u32 top;
	int n = 0;
	__asm__ volatile("movl %%fs:4, %0" : "=r"(top));   /* TEB StackBase */
	for (u32 *p = (u32 *)(esp & ~3u); (u32)p < top && n < 40; p++) {
		const Module *m = module_of(*p);
		if (!m) continue;
		log_msg("  [esp+%04x] %s+%x (%08x)", (u32)p - esp, m->name, *p - m->base, *p);
		n++;
	}
}

static int g_faults;
static u32 g_last_at, g_last_ms;

__declspec(dllimport) int WINAPI TerminateProcess(HANDLE, u32);
__declspec(dllimport) u32 WINAPI GetTickCount(void);

static long WINAPI fault_handler(u32 **info)
{
	u32 *rec = info[0];       /* EXCEPTION_RECORD */
	u8 *ctx = (u8 *)info[1];  /* i386 CONTEXT */
	u32 code = rec[0];
	const Module *m = module_of(rec[3]);
#define R(off) (*(u32 *)(ctx + (off)))

	if (code < 0xc0000000u || g_faults >= 8) return 0;   /* EXCEPTION_CONTINUE_SEARCH */
	g_faults++;
	log_msg("FAULT %08x at %08x (%s+%x), %s %08x", code, rec[3], m ? m->name : "?",
	        m ? rec[3] - m->base : 0, rec[5] ? "write" : "read", rec[4] > 1 ? rec[6] : 0);
	log_msg("  eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x",
	        R(0xb0), R(0xa4), R(0xac), R(0xa8), R(0xa0), R(0x9c), R(0xb4), R(0xc4));
	{   /* what heap_check.c's journal knows of the small blocks the registers point into */
		static const char *names[] = { "eax", "ebx", "ecx", "edx", "esi", "edi" };
		static const u16 offs[] = { 0xb0, 0xa4, 0xac, 0xa8, 0xa0, 0x9c };
		for (int i = 0; i < 6; i++) heap_check_fault(names[i], R(offs[i]));
	}
	log_stack(R(0xc4));
#undef R
	if (g_faults == 1) report_crash();   /* its log goes with the next start's problem report */
	/* the same access violation again at once: the crash's own handling ran into it (a broken
	 * heap, its lock held), and the Linux host froze there with the game on screen instead of
	 * closing. Closing now lets the others see this player gone */
	if (code == 0xc0000005u && rec[3] == g_last_at && GetTickCount() - g_last_ms < 2000) {
		log_msg("the same fault came back while handling it: closing Oni");
		TerminateProcess(GetCurrentProcess(), code);
	}
	g_last_at = rec[3];
	g_last_ms = GetTickCount();
	return 0;
}

/* The DLLs loaded in Oni that aren't Windows' own (Oni.exe, Daodan, Arklite, and anything else a
 * program put in: overlays, recorders, hooks, which can change the floats' control words), by
 * name, ", " between: 1 if the list changed since the last call */
__declspec(dllimport) u32 WINAPI GetWindowsDirectoryA(char *, u32);

int modules_changed(char *out, u32 size)
{
	static u32 last;
	char win[MAX_PATH];
	u32 wn = GetWindowsDirectoryA(win, sizeof win), h = 0x811c9dc5u, n = 0, count = 0;
	u8 *peb, *head, *e;
	__asm__ volatile("movl %%fs:0x30, %0" : "=r"(peb));
	head = *(u8 **)(peb + 0x0c) + 0x0c;   /* PEB_LDR_DATA.InLoadOrderModuleList */
	out[0] = 0;
	for (e = *(u8 **)head; e && e != head && count < 512; e = *(u8 **)e, count++) {
		const u16 *full = *(const u16 **)(e + 0x28), *base = *(const u16 **)(e + 0x30);
		u32 flen = *(const u16 *)(e + 0x24) / 2, blen = *(const u16 *)(e + 0x2c) / 2, i;
		if (!base || !blen) continue;
		if (full && wn && wn < sizeof win && flen > wn) {   /* in the Windows folder (system32, syswow64...) */
			for (i = 0; i < wn; i++) {
				char a = (char)full[i], b = win[i];
				if (a >= 'A' && a <= 'Z') a += 32;
				if (b >= 'A' && b <= 'Z') b += 32;
				if (a != b && !((a == '/' || a == '\\') && (b == '/' || b == '\\'))) break;
			}
			if (i == wn && (full[i] == '\\' || full[i] == '/')) continue;
		}
		if (n + blen + 3 >= size) {
			if (n + 4 < size) str_cpy(out + n, size - n, ", ...");
			n = size - 1;
			break;
		}
		if (n) { out[n++] = ','; out[n++] = ' '; }
		for (i = 0; i < blen; i++) {
			char c = base[i] < 0x80 ? (char)base[i] : '?';
			out[n++] = c;
			h = (h ^ (u8)(c | 0x20)) * 0x01000193u;
		}
		out[n] = 0;
	}
	if (h == last) return 0;
	last = h;
	return 1;
}

void crash_log_install(void *instance)
{
	add_module(0, GetModuleHandleA(0), "Oni.exe");
	add_module(1, GetModuleHandleA("binkw32.dll"), "daodan");
	add_module(2, instance, "Arklite");
	AddVectoredExceptionHandler(1, fault_handler);
}

void crash_log_exit(void)
{
	u32 esp;
	__asm__ volatile("movl %%esp, %0" : "=r"(esp));
	log_msg("process exiting; stack:");
	log_stack(esp);
}
