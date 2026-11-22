#!/usr/bin/env bash
# Regenerate the lifted C from the XBE: disasm -> func_id -> abi -> recomp.
#
#   tools/regen.sh
#
# Environment:
#   XBOXRECOMP_DIR  toolkit            (default: the vendored xboxrecomp/)
#   HPCOS_XBE         default.xbe        (default: <repo>/game/xbe/default.xbe)
#   HPCOS_GEN_DIR     output directory   (default: <repo>/engine/gen)
#   LIFT_ONLY=1     skip disasm/func_id/abi (enough after editing
#                   recomp_manual.c; seeds need the full run)
#
# Seeds (config/seed_functions.json) are entry points the static pass cannot
# see -- thread start routines and indirect-call targets found at runtime.
# Grow them with:  python3 -m tools.seed_from_log run.log default.xbe \
#                    --functions tools/disasm/output/functions.json \
#                    --seeds <this repo>/config/seed_functions.json
# and review every addition: a seed inside a function splits it.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TK="${XBOXRECOMP_DIR:-$HERE/xboxrecomp}"
XBE="${HPCOS_XBE:-$HERE/../game/extracted/default.xbe}"
GEN="${HPCOS_GEN_DIR:-$HERE/gen}"

cd "$TK"
if [ "${LIFT_ONLY:-0}" != "1" ]; then
    # tools.disasm needs the XBE parser's section analysis. Written next to the
    # disassembly output (gitignored) rather than into the game directory.
    ANALYSIS="$TK/tools/disasm/output/$(basename "${XBE%.*}")_analysis.json"
    mkdir -p "$(dirname "$ANALYSIS")"
    python3 -m tools.xbe_parser "$XBE" --json "$ANALYSIS" --quiet
    python3 -m tools.disasm "$XBE" --seed-functions "$HERE/config/seed_functions.json" \
        --analysis-json "$ANALYSIS"
    python3 -m tools.func_id "$XBE"
    python3 -m tools.abi_analysis "$XBE"
fi
mkdir -p "$GEN"
# recomp_types.h is written only when absent, so a stale copy would hide
# register-model changes in the toolkit. Nothing here edits it by hand.
rm -f "$GEN/recomp_types.h"
python3 -m tools.recomp "$XBE" --game-name "Harry Potter Chamber of Secrets" --all --split 250 \
    --gen-dir "$GEN" --exclude-manual "$HERE/src/recomp_manual.c" \
    --mmio-sections DSOUND,XPP
# fixes inside generated functions the lifter gets wrong (fails if one no longer applies)
python3 "$HERE/tools/patch_gen.py" "$GEN"
