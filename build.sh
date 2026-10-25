#!/bin/bash
# Builds build/Arklite.dll (the mod) and build/realbink.dll (its loader: Daodan loads that name)
# as 32-bit Windows DLLs with clang + lld; no MinGW or MSVC SDK needed.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
VERSION="$(cat VERSION)"
# The build's commit goes into the version Arklite logs and the network handshake compares, so
# two PCs on different builds show it: 0.1.0+79b51e7, with * when src/ has uncommitted changes.
BUILD="$VERSION"
if commit="$(git rev-parse --short=7 HEAD 2>/dev/null)"; then
	BUILD="$VERSION+$commit"
	git diff --quiet HEAD -- src 2>/dev/null || BUILD="$BUILD*"
fi
# What the network handshake compares to tell whether two PCs run the same Arklite: a hash of the
# source (and this script's flags), not of the DLL, whose bytes differ with the compiler version
# and the build time (a local build and GitHub's of the same source must play together). CRs are
# dropped, so a Windows checkout with CRLF line endings hashes the same.
SRC_HASH="$(for f in $(printf '%s\n' src/*.c src/*.h src/*.def build.sh | LC_ALL=C sort); do
	echo "$f"; tr -d '\r' < "$f"; done | sha256sum | cut -c1-8)"
# Distro LLVM packages may only ship versioned tool names (llvm-dlltool-18); fall back to those.
DLLTOOL="${DLLTOOL:-$(command -v llvm-dlltool || ls /usr/bin/llvm-dlltool-* 2>/dev/null | sort -V | tail -1)}"
for lib in kernel32 user32 ws2_32; do
	"$DLLTOOL" -m i386 -k -d "src/$lib.def" -l "build/$lib.lib"
done
CC=(clang --target=i686-pc-windows-msvc -O2 -Wall -Wno-unused-parameter
	-ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe
	-nostdlib -fuse-ld=lld -shared "-DARKLITE_VERSION=\"$BUILD\"" "-DARKLITE_SRC_HASH=0x$SRC_HASH")
"${CC[@]}" -Wl,/entry:DllMain,/out:build/Arklite.dll \
	src/arklite.c src/common.c src/custom_game.c src/volume.c src/more_options.c src/hud_scale.c src/hud_smooth.c src/random_class.c src/texture_clamp.c src/char_limit.c src/lockstep.c src/live_menu.c src/players.c src/items.c src/mp_rules.c src/net.c src/mp_hud.c src/char_menu.c src/vertex_light.c src/crash_log.c build/kernel32.lib build/user32.lib build/ws2_32.lib
"${CC[@]}" -Wl,/entry:DllMain,/def:src/realbink.def,/out:build/realbink.dll \
	src/loader.c build/kernel32.lib
rm -f build/Arklite.lib build/Arklite.exp build/realbink.lib build/realbink.exp a.lib
echo "built build/Arklite.dll and build/realbink.dll (Arklite $BUILD, source $SRC_HASH)"
