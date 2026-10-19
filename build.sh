#!/bin/bash
# Builds build/Arklite.dll (the mod) and build/realbink.dll (its loader: Daodan loads that name)
# as 32-bit Windows DLLs with clang + lld; no MinGW or MSVC SDK needed.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
VERSION="$(cat VERSION)"
# Distro LLVM packages may only ship versioned tool names (llvm-dlltool-18); fall back to those.
DLLTOOL="${DLLTOOL:-$(command -v llvm-dlltool || ls /usr/bin/llvm-dlltool-* 2>/dev/null | sort -V | tail -1)}"
for lib in kernel32 user32; do
	"$DLLTOOL" -m i386 -k -d "src/$lib.def" -l "build/$lib.lib"
done
CC=(clang --target=i686-pc-windows-msvc -O2 -Wall -Wno-unused-parameter
	-ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe
	-nostdlib -fuse-ld=lld -shared "-DARKLITE_VERSION=\"$VERSION\"")
"${CC[@]}" -Wl,/entry:DllMain,/out:build/Arklite.dll \
	src/arklite.c src/common.c src/custom_game.c src/volume.c src/more_options.c src/hud_scale.c src/random_class.c src/char_limit.c src/lockstep.c src/live_menu.c src/players.c src/items.c src/mp_rules.c src/crash_log.c build/kernel32.lib build/user32.lib
"${CC[@]}" -Wl,/entry:DllMain,/def:src/realbink.def,/out:build/realbink.dll \
	src/loader.c build/kernel32.lib
rm -f build/Arklite.lib build/Arklite.exp build/realbink.lib build/realbink.exp a.lib
echo "built build/Arklite.dll and build/realbink.dll (Arklite $VERSION)"
