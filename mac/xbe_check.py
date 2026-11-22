#!/usr/bin/env python3
"""Check that a default.xbe is the Harry Potter and the Chamber of Secrets USA build this port is made for.

Every section's raw size and the SHA-1 digest stored in its header must match the reference. Region and
media patches (extract-xiso applies one by default) change the certificate and a few .text bytes but leave the stored digests alone, so they still pass; the
script says whether .text is pristine (its data matches its stored digest) or patched.
The other sections' data must match their stored digests too.
Exit 0 = supported, 5 = Harry Potter, another build or region, 4 = another game, 2 = not an XBE.
    python3 -I mac/xbe_check.py <default.xbe>     (also imported by mac/import_disc.py: check(data))"""
import hashlib, struct, sys

TITLE_ID = 0x4541000E
REF = [  # (name, raw size, stored digest), from the USA default.xbe (SHA-1 f6cb092f..., title 0x4541000E)
    (".text", 1570612, "2755e9070565c96ec88b33ff5f8c4f848c7e2c31"),
    ("D3D", 64504, "d53e9a7b3bd258d50f26dfd0faff8670f77a7d59"),
    ("XGRPH", 8444, "33bea197fcdabd2960cd83234afe6d1f75c8f504"),
    ("DSOUND", 120972, "035a1870f7b66580e1b1291721b3bd35952d3e71"),
    ("XMV", 163228, "31ade51cce25c34b3d4a24169d26c25faa9f4048"),
    ("D3DX", 35912, "7d8b52615cdad4d11798624b6fe256a4f5e645d3"),
    ("XPP", 25268, "013887be12a08bb757866037efd6a1ce4a0157c1"),
    (".rdata", 92736, "9ef79540f828fe9bdc42f8837389788d9a5bf5b4"),
    (".data", 1106852, "6fdf407ece16adf83bff4673827ef29d1b03ebdc"),
    ("DOLBY", 29036, "c8c9142dccbd31bdca9a2d94e0acc47c2048d615"),
    ("$$XTINFO", 220, "a15429b415d3b93de443f6125d6338f244d92591"),
    ("$$XTIMAGE", 10240, "14390bbf7cc95e441a3fedc28402b06efd364ef4"),
    (".XTLID", 1320, "46e7922648a13cfed623f0ed130c7cbb43475cc9"),
]

def check(b):
    """(exit code, message) for the bytes of a default.xbe."""
    if b[:4] != b"XBEH":
        return 2, "not an XBE"
    try:
        base, = struct.unpack_from("<I", b, 0x104)
        cert, = struct.unpack_from("<I", b, 0x118)
        count, at = struct.unpack_from("<II", b, 0x11C)
        tid, = struct.unpack_from("<I", b, cert - base + 8)
    except struct.error:
        return 2, "not an XBE"
    if tid != TITLE_ID:
        return 4, f"title {tid:#x}: not Harry Potter Chamber of Secrets"
    if count != len(REF):
        return 5, f"Harry Potter Chamber of Secrets with {count} sections: another build or region (only the USA disc is supported)"
    text_pristine = True
    for i, (name, size, digest) in enumerate(REF):
        o = at - base + i * 56
        raw, rsz = struct.unpack_from("<II", b, o + 12)
        stored = b[o + 36:o + 56]
        if rsz != size or stored.hex() != digest:
            return 5, f"section {name} differs: another build or region (only the USA disc is supported)"
        same = hashlib.sha1(struct.pack("<I", rsz) + b[raw:raw + rsz]).digest() == stored
        if name == ".text":
            text_pristine = same
        elif not same:
            return 5, f"section {name}'s data does not match its digest: a modified executable"
    return 0, f"Harry Potter Chamber of Secrets USA ({'pristine' if text_pristine else 'media/region-patched'} .text)"


def main(path):
    code, msg = check(open(path, "rb").read())
    print(f"xbe_check: {'OK, ' if code == 0 else ''}{msg}")
    return code

if __name__ == "__main__":
    if len(sys.argv) != 2: sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
