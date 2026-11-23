#!/bin/bash
# Dev play-test: the release build in a window, the log in this terminal (and in build/play/last.log).
# Own HOME in build/play (its player folder, saves and settings.ini stay there between runs; never your real
# ~/Library), the game from an APFS clone of game/extracted. Controller changes are logged (HPCOS_PAD_LOG=1).
#   tools/play_dev.sh            build (incremental) and play
#   HPCOS_NO_BUILD=1 tools/play_dev.sh
# The runtime's periodic logging is off (RECOMP_QUIET=1; RECOMP_QUIET=0 brings it back).
# 2D blits (CopyRects) are logged as [blit] lines, other 2D methods as [subch].
# Extra env passes through (HPCOS_FPS=60, RECOMP_PAD_SCRIPT=..., HPCOS_PEEK=..., ...).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/release/hpcos_recomp"
P="$ROOT/build/play"
if [[ "${HPCOS_NO_BUILD:-0}" != 1 ]]; then
  cmake --build "$ROOT/build/release" | grep -E "error|FAILED" && exit 1 || true
fi
mkdir -p "$P/home"
[[ -f "$P/game/default.xbe" ]] || cp -c -R "$ROOT/game/extracted" "$P/game"
cd "$P/home"
HOME="$P/home" NFSU2_GAME_DIR="$P/game" HPCOS_PAD_LOG="${HPCOS_PAD_LOG:-1}" HPCOS_SAVE_LOG="${HPCOS_SAVE_LOG:-1}" RECOMP_BLIT_LOG="${RECOMP_BLIT_LOG:-1}" RECOMP_SUBCH_TRACE="${RECOMP_SUBCH_TRACE:-80}" RECOMP_QUIET="${RECOMP_QUIET:-1}" "$BIN" 2>&1 | tee "$P/last.log"
