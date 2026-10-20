#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sdk="$root/third_party/rexglue-sdk"
required=(
  "$sdk/thirdparty/fmt/include/fmt/format.h"
  "$sdk/thirdparty/spdlog/include/spdlog/spdlog.h"
  "$sdk/thirdparty/simde/simde/simde-common.h"
  "$sdk/thirdparty/xxHash/xxhash.h"
  "$sdk/thirdparty/tomlplusplus/include/toml++/toml.hpp"
  "$sdk/thirdparty/imgui/imgui.h"
)
for path in "${required[@]}"; do
  if [[ ! -f "$path" ]]; then
    echo "missing selective ReXGlue dependency: $path" >&2
    exit 2
  fi
done
incs=(
  -I"$root/src"
  -I"$root/third_party/ggpo/src/include"
  -I"$root/third_party/ggpo/src/lib/ggpo"
  -I"$sdk/include"
  -I"$sdk/thirdparty/fmt/include"
  -I"$sdk/thirdparty/spdlog/include"
  -I"$sdk/thirdparty/simde"
  -I"$sdk/thirdparty/xxHash"
  -I"$sdk/thirdparty/tomlplusplus/include"
  -I"$sdk/thirdparty/utfcpp/source"
  -I"$sdk/thirdparty/imgui"
)
pass=0
while IFS= read -r source; do
  base=$(basename "$source")
  timeout 45s clang++ -std=c++23 -fsyntax-only \
    -DREX_HAS_VULKAN=1 -DREX_HAS_D3D12=0 \
    "${incs[@]}" "$source"
  printf 'PASS %s\n' "$base"
  ((++pass))
done < <(find "$root/src" -maxdepth 1 -type f -name 't6_*.cpp' | sort)
printf 'T6_LINUX_SYNTAX_PASS=%d\n' "$pass"
printf 'T6_LINUX_GENERATED_BOUNDARY=main.cpp,tekken6_app.cpp\n'
