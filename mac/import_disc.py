#!/usr/bin/env python3
"""The installer's disc import: the player's own disc image into the game folder.

    python3 -I import_disc.py <disc image> <game dir>

Extracts the XDVDFS image (xiso.py) into <game dir>.part, checks default.xbe (xbe_check.py: the USA executable's section
digests; disc images differ in a few media-patched bytes, so no whole-file hash), then renames it to <game dir>. The port is
lifted from the USA executable; runtime overrides are at its addresses, so another one cannot run.
Exit 0 = done; 3 = not an Xbox disc; 4 = an Xbox game, not Harry Potter Chamber of Secrets; 5 = Harry Potter Chamber of Secrets, another edition.
Adapted from NFSU2Deadware's tools/dmg/import_disc.py (MIT, LeoDeadware).
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xiso  # noqa: E402
import xbe_check  # noqa: E402


def main():
    iso, dest = sys.argv[1], sys.argv[2]
    tmp = dest + ".part"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    print("import: %s -> %s" % (iso, dest), flush=True)
    try:
        xiso.extract(iso, tmp, log=lambda *a: print("import:", *a, flush=True))
    except ValueError:
        shutil.rmtree(tmp, ignore_errors=True)
        print("import: not an Xbox disc image", flush=True)
        return 3
    xbe = os.path.join(tmp, "default.xbe")
    if not os.path.isfile(xbe):
        shutil.rmtree(tmp, ignore_errors=True)
        print("import: no default.xbe: not an Xbox game", flush=True)
        return 3
    with open(xbe, "rb") as f:
        code, msg = xbe_check.check(f.read())
    if code:
        shutil.rmtree(tmp, ignore_errors=True)
        print("import: default.xbe: %s" % msg, flush=True)
        return 3 if code == 2 else code
    # The game's saves (UDATA) and title data (TDATA) live in this folder (the
    # kernel maps Partition1\ here): a new import keeps them.
    for keep in ("UDATA", "TDATA"):
        old = os.path.join(dest, keep)
        if os.path.isdir(old) and not os.path.exists(os.path.join(tmp, keep)):
            os.rename(old, os.path.join(tmp, keep))
    shutil.rmtree(dest, ignore_errors=True)
    os.rename(tmp, dest)
    print("import: done, default.xbe: %s" % msg, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
