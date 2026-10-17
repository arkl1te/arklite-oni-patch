#!/bin/bash
# Builds build/Arklite.dll (32-bit Windows DLL) with clang + lld; no MinGW or MSVC SDK needed.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
for lib in kernel32 user32; do
	llvm-dlltool -m i386 -k -d "src/$lib.def" -l "build/$lib.lib"
done
clang --target=i686-pc-windows-msvc -O2 -Wall -Wno-unused-parameter \
	-ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe \
	-nostdlib -fuse-ld=lld -shared \
	-Wl,/entry:DllMain,/out:build/Arklite.dll \
	src/arklite.c src/common.c src/custom_game.c src/volume.c src/more_options.c src/hud_scale.c src/random_class.c src/char_limit.c build/kernel32.lib build/user32.lib
rm -f build/Arklite.lib build/Arklite.exp
echo "built build/Arklite.dll"
