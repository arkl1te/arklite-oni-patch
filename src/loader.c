/* Arklite's loader, built as realbink.dll.
 *
 * The Daodan DLL (AE's binkw32.dll) loads realbink.dll by name and forwards Oni's Bink calls
 * to it. This DLL forwards them on to the original Bink, renamed bink_orig.dll (realbink.def),
 * and loads Arklite.dll, which holds everything else.
 *
 * A test build (test_build.c) runs instead while AE\arklite-test\active and
 * AE\arklite-test\Arklite.dll both exist: next to this DLL, whatever folder Oni started from.
 * If it won't load, the release runs, and arklite-test\loader.log says why. */
#include "arklite.h"

__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) u32    WINAPI GetModuleFileNameA(void *, char *, u32);
__declspec(dllimport) u32    WINAPI GetLastError(void);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);

static void note(const char *path, const char *msg, u32 n)
{
	u32 written;
	HANDLE log = CreateFileA(path, 0x40000000 /* GENERIC_WRITE */, 1, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (log == INVALID_HANDLE_VALUE) return;
	WriteFile(log, msg, n, &written, 0);
	CloseHandle(log);
}

/* this DLL's folder + name into out (MAX_PATH): 1, or 0 if it doesn't fit */
static int beside(void *instance, const char *name, char *out)
{
	u32 n = GetModuleFileNameA(instance, out, MAX_PATH), k = 0;
	if (!n || n >= MAX_PATH) return 0;
	while (n && out[n - 1] != '\\' && out[n - 1] != '/') n--;
	while (name[k]) k++;
	if (n + k + 1 > MAX_PATH) return 0;
	for (u32 i = 0; i <= k; i++) out[n + i] = name[i];
	return 1;
}

int WINAPI DllMain(void *instance, u32 reason, void *reserved)
{
	char active[MAX_PATH], dll[MAX_PATH];
	if (reason != 1) return 1;   /* DLL_PROCESS_ATTACH */
	if (beside(instance, TEST_DIR "\\active", active) && GetFileAttributesA(active) != 0xffffffffu &&
	    beside(instance, TEST_DIR "\\Arklite.dll", dll) && GetFileAttributesA(dll) != 0xffffffffu) {
		char log[MAX_PATH];
		u32 e;
		if (LoadLibraryA(dll)) return 1;
		e = GetLastError();
		if (beside(instance, TEST_DIR "\\loader.log", log)) {
			static const char msg[] = "arklite-test\\Arklite.dll failed to load (Windows error 0x00000000): the release ran instead\r\n";
			char text[sizeof msg];
			for (u32 i = 0; i < sizeof msg; i++) text[i] = msg[i];
			u32 at = 0;
			while (text[at] && !(text[at] == '0' && text[at + 1] == 'x')) at++;   /* the error's "0x" */
			for (int i = 0; text[at] && i < 8; i++) text[at + 2 + i] = "0123456789abcdef"[e >> (28 - 4 * i) & 15];
			note(log, text, sizeof msg - 1);
		}
	}
	if (!LoadLibraryA("Arklite.dll")) {
		static const char msg[] = "Arklite.dll not found or failed to load\r\n";
		note("Arklite.log", msg, sizeof msg - 1);
	}
	return 1;
}
