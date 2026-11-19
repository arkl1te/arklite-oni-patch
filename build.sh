#!/bin/bash
# Builds build/Arklite.dll (the mod) and build/realbink.dll (its loader: Daodan loads that name)
# as 32-bit Windows DLLs with clang + lld; no MinGW or MSVC SDK needed.
# ARKLITE_TEST=1 builds a test build into build/test/ instead (src/test_build.c: it lives in AE/arklite-test/
# beside the release, shows its commit on screen and talks to the dev helper); package.sh test zips it.
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
OUT=build
TEST=()
if [ "${ARKLITE_TEST:-}" = 1 ]; then
	[ -n "${commit:-}" ] || { echo "error: a test build needs git (its commit is its name)" >&2; exit 1; }
	OUT=build/test
	mkdir -p "$OUT"
	TEST=(-DARKLITE_TEST "-DARKLITE_COMMIT=\"$commit\"" "-DARKLITE_COMMIT_DATE=\"$(git show -s --format=%cs HEAD)\"")
fi
# What the network handshake compares to tell whether two PCs run the same Arklite: a hash of the
# source (and this script's flags), not of the DLL, whose bytes differ with the compiler version
# and the build time (a local build and GitHub's of the same source must play together). CRs are
# dropped, so a Windows checkout with CRLF line endings hashes the same.
SRC_HASH="$(for f in $(printf '%s\n' src/*.c src/*.h src/*.def src/zstd/*.c src/zstd/*.h src/zstd/ark_inc/*.h build.sh console.sh | LC_ALL=C sort); do
	echo "$f"; tr -d '\r' < "$f"; done | sha256sum | cut -c1-8)"
# Distro LLVM packages may only ship versioned tool names (llvm-dlltool-18); fall back to those.
DLLTOOL="${DLLTOOL:-$(command -v llvm-dlltool || ls /usr/bin/llvm-dlltool-* 2>/dev/null | sort -V | tail -1)}"
# server.c's Linux terminal console: console.sh, written to Oni/AE as Arklite-console.sh, as C bytes
# (CRs dropped like the hash above). Not #embed, which needs clang 19 and Ubuntu 24.04's (GitHub's
# builder) is 18.
tr -d '\r' < console.sh | od -An -v -tx1 | sed 's/\([0-9a-f][0-9a-f]\)/0x\1,/g' > build/console_sh.inc
for lib in kernel32 user32 ws2_32 gdi32; do
	"$DLLTOOL" -m i386 -k -d "src/$lib.def" -l "build/$lib.lib"
done
# Zstandard (src/zstd/README.txt: its single-file library, BSD licence) for level0 patches, built on its own
# (third-party code: its warnings aren't Arklite's) and only when it changed
if [ ! build/zstd_ark.o -nt src/zstd/zstd_ark.c ] || [ ! build/zstd_ark.o -nt build.sh ]; then
	clang --target=i686-pc-windows-msvc -O2 -w -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe \
		-Isrc/zstd/ark_inc -Isrc/zstd -c src/zstd/zstd_ark.c -o build/zstd_ark.o
fi
CC=(clang --target=i686-pc-windows-msvc -O2 -Wall -Wno-unused-parameter
	-ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe
	-nostdlib -fuse-ld=lld -shared -Ibuild "-DARKLITE_VERSION=\"$BUILD\"" "-DARKLITE_SRC_HASH=0x$SRC_HASH")
"${CC[@]}" "${TEST[@]}" -Wl,/entry:DllMain,/out:$OUT/Arklite.dll \
	src/arklite.c src/common.c src/custom_game.c src/level0_swap.c src/mp_menu.c src/volume.c src/more_options.c src/hud_scale.c src/hud_smooth.c src/random_class.c src/texture_clamp.c src/particle_table.c src/char_limit.c src/lockstep.c src/live_menu.c src/players.c src/items.c src/respawn_fx.c src/mp_rules.c src/obituary.c src/regen.c src/net.c src/mp_hud.c src/char_menu.c src/mp_names.c src/chat.c src/menu_wheel.c src/mp_board.c src/vertex_light.c src/spin_particle.c src/bench.c src/crash_log.c src/heap_check.c src/rollback.c src/upnp.c src/stun.c src/helper.c src/screenshot.c src/photo.c src/server.c src/report.c src/sysinfo.c src/lagometer.c src/bug.c src/flare_depth.c src/invis.c src/fx_menu.c src/motion_blur.c src/sprite_aspect.c src/mutators.c src/big_font.c src/text.c src/test_build.c src/level0_patch.c src/dialog.c src/weapon_check.c build/zstd_ark.o build/kernel32.lib build/user32.lib build/ws2_32.lib build/gdi32.lib
"${CC[@]}" -Wl,/entry:DllMain,/def:src/realbink.def,/out:$OUT/realbink.dll \
	src/loader.c build/kernel32.lib
rm -f "$OUT"/Arklite.lib "$OUT"/Arklite.exp "$OUT"/realbink.lib "$OUT"/realbink.exp a.lib
echo "built $OUT/Arklite.dll and $OUT/realbink.dll (Arklite $BUILD${TEST:+, test build}, source $SRC_HASH)"
