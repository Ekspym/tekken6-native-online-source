#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=${TMPDIR:-/tmp}/t6-common-linux-smoke
clang++ -std=c++23 -O2 -pthread -I"$root/src" \
  "$root/tools/t6_common_linux_smoke.cpp" -ldl -o "$out"
"$out"
