#!/bin/bash
# Scripted dev run: tools/run_test.sh <name> <seconds> [shot times "10,20,30"] -- extra env passes through.
# Own HOME and an APFS clone of the game in build/t/<name> (never the play-test folder); log in build/t/<name>/run.log,
# shots in build/t/<name>/shot_<t>.png (the overlay's screenshot, menu closed).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
N="$1"; SECS="$2"; SHOTS="${3:-}"
T="$ROOT/build/t/$N"; mkdir -p "$T/home"
[[ -f "$T/game/default.xbe" ]] || cp -c -R "$ROOT/game/extracted" "$T/game"
S=""; for t in ${SHOTS//,/ }; do S="$S${S:+,}${t}:shot"; done
cd "$T/home"
HOME="$T/home" NFSU2_GAME_DIR="$T/game" NFSU2_SMOKE_SECONDS="$SECS" NFSU2_OVERLAY_SHOT="$T/shot" \
  NFSU2_OVERLAY_SCRIPT="${NFSU2_OVERLAY_SCRIPT:-$S}" "$ROOT/build/release/hpcos_recomp" > "$T/run.log" 2>&1
echo "rc=$? log=$T/run.log"
