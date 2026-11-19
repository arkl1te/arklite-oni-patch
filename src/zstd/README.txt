Zstandard 1.5.7 (https://github.com/facebook/zstd, tag v1.5.7, commit f8745da6ff1ad1e7bab384bd1f9d742439278e99),
used under its BSD licence (LICENSE.txt), for level0 patches (src/level0_patch.c).

zstd_ark.c is zstd's single-file library made by its own script, without threads, the dictionary
builder or legacy formats, and with Arklite's stand-ins for the C runtime (ark_zstd_deps.h, ark_inc/):
  cp zstd-ark-in.c ark_zstd_deps.h <zstd>/build/single_file_libs/
  cd <zstd>/build/single_file_libs
  python3 combine.py -r ../../lib -r . -x legacy/zstd_legacy.h -k zstd.h -k ark_zstd_deps.h -o zstd_ark.c zstd-ark-in.c
then '#include "../zstd.h"' changed to '#include "zstd.h"'. zstd.h and zstd_errors.h are lib/'s, unchanged.
