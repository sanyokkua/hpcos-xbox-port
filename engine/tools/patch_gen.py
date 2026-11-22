#!/usr/bin/env python3
"""Fixes regen.sh applies to the lifted code (python3 tools/patch_gen.py <gen dir>).

From MiddleDucky/HP-COS-Xbox-Recomp's port/tools/patch_gen.py (MIT): a function whose first instruction is a
conditional jump on its CALLER's flags is lifted with a fresh `_flags = 0`, so the branch is never taken.
This lifter already carries the caller's flags into three of HP-COS's four cases (they are labels inside the
caller here); sub_000C0001 is still a function of its own. Each fix finds its line by exact text and fails
loudly if it is gone; running twice changes nothing."""
import glob, sys

FIXES = [
    ("sub_000C0001",  # null check on [esi+0xB0]: the caller's `test eax, eax` at 0xBFFFF
     "    if (_flags /* je: equal / zero */) goto loc_000C000E;",
     "    if (eax == 0 /* je: flags of the caller's test eax, eax (0xBFFFF) - hand-fixed, the lifted _flags was always 0 */) goto loc_000C000E;"),
]

def main(gen):
    files = sorted(glob.glob(f"{gen}/recomp_*.c"))
    for func, old, new in FIXES:
        head = f"void {func}(void)\n"
        for p in files:
            s = open(p).read()
            i = s.find(head)
            if i < 0:
                continue
            j = s.find("\nvoid sub_", i + len(head))
            body = s[i:j if j > 0 else len(s)]
            if new in body:
                print(f"patch_gen: {func} already fixed"); break
            if old not in body:
                sys.exit(f"patch_gen: {func}: line not found (lifter changed?)")
            s = s[:i] + body.replace(old, new, 1) + s[i + len(body):]
            open(p, "w").write(s)
            print(f"patch_gen: {func} fixed"); break
        else:
            sys.exit(f"patch_gen: {func} not in {gen}")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "gen")
