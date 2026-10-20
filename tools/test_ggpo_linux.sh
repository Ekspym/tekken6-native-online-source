#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
gg="$root/third_party/ggpo/src/lib/ggpo"
out=${TMPDIR:-/tmp}/tekken6-ggpo-linux-smoke
rm -rf "$out"
mkdir -p "$out/obj"
mapfile -t sources < <(find "$gg" -maxdepth 2 -type f -name '*.cpp' ! -name 'platform_windows.cpp' | sort)
for source in "${sources[@]}"; do
  rel=${source#"$gg/"}
  obj="$out/obj/${rel//\//_}.o"
  clang++ -std=c++17 -O2 -fPIC \
    -I"$root/third_party/ggpo/src/include" -I"$gg" \
    -c "$source" -o "$obj"
done
ar rcs "$out/libggpo-linux.a" "$out"/obj/*.o
clang++ -std=c++17 -O2 \
  -I"$root/third_party/ggpo/src/include" -I"$gg" \
  "$root/tools/ggpo_linux_smoke.cpp" "$out/libggpo-linux.a" \
  -o "$out/ggpo_linux_smoke"
"$out/ggpo_linux_smoke"
printf 'GGPO_LINUX_COMPILE_UNITS=%d\n' "${#sources[@]}"
printf 'GGPO_LINUX_SMOKE_PASS=1\n'
