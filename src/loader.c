/* Arklite's loader, built as realbink.dll.
 *
 * The Daodan DLL (AE's binkw32.dll) loads realbink.dll by name and forwards Oni's Bink calls
 * to it. This DLL forwards them on to the original Bink, renamed bink_orig.dll (realbink.def),
 * and loads Arklite.dll, which holds everything else. */
#include "arklite.h"

__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);

int WINAPI DllMain(void *instance, u32 reason, void *reserved)
{
	if (reason != 1) return 1;   /* DLL_PROCESS_ATTACH */
	if (!LoadLibraryA("Arklite.dll")) {
		static const char msg[] = "Arklite.dll not found or failed to load\r\n";
		u32 written;
		HANDLE log = CreateFileA("Arklite.log", 0x40000000 /* GENERIC_WRITE */, 1, 0,
		                         2 /* CREATE_ALWAYS */, 0x80, 0);
		if (log != INVALID_HANDLE_VALUE) WriteFile(log, msg, sizeof msg - 1, &written, 0);
	}
	return 1;
}
