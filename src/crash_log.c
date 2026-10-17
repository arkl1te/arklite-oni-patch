/* Crash log (AE\Arklite.log).
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
	log_stack(R(0xc4));
#undef R
	return 0;
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
