#!/usr/bin/env bash
# Solanum - builds the app for this computer (tools/desktop): SDL2 in a window, the store answering from fixtures
# (tests/fixtures by default). For the interface; the PS5 build is `make`.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/build-desktop.sh            -> build/desktop/solanum
#   tools/build-desktop.sh preview    -> build/desktop-preview/solanum (off screen, scripted: NP_SCRIPT)
#
# Run it with NP_ROOT=<a data folder> (default ./solanum-data), NP_ASSETS=<repo>/assets and
# NP_FIXTURES=<repo>/tests/fixtures. A made-up console's apps go in <NP_ROOT>/console/data/homebrew.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-window}
out="$root/build/desktop"
preview=OFF
[[ $mode == preview ]] && { out="$root/build/desktop-preview"; preview=ON; }
paths=""
[[ $(uname -s) == Darwin ]] && command -v brew >/dev/null && paths="$(brew --prefix)"
generator=()
mkdir -p "$root/build"
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -S "$root/tools/desktop" -B "$out" "${generator[@]}" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH="$paths" -DNP_PREVIEW=$preview >"$out.log" 2>&1 ||
    { tail -40 "$out.log"; exit 1; }
cmake --build "$out" --target solanum --parallel "${BUILD_JOBS:-8}"
echo "==> [desktop] $out/solanum"
