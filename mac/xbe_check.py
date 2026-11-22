#!/usr/bin/env python3
"""Check that a default.xbe is the Harry Potter and the Chamber of Secrets USA build this port is made for.

Every section's raw size and the SHA-1 digest stored in its header must match the reference. Region and
media patches (extract-xiso applies one by default) change the certificate and a few .text bytes but leave the stored digests alone, so they still pass; the
script says whether .text is pristine (its data matches its stored digest) or patched (this disc's image is pristine).
The other sections' data must match their stored digests too.
Exit 0 = supported, 5 = Harry Potter, another build or region, 4 = another game, 2 = not an XBE.
    python3 -I mac/xbe_check.py <default.xbe>     (also imported by mac/import_disc.py: check(data))"""
import hashlib, struct, sys

TITLE_ID = 0x4541000E
REF = [  # (name, raw size, stored digest), from the USA default.xbe (SHA-1 dd881f2c..., title 0x4541000E, XDK 5028)
    (".text", 1177120, "ebec9744020f0fa472157c36332d3e31cebc69a7"),
    ("D3D", 61864, "7f83ad50bed273aa8b9c374e636f748371afa900"),
    ("D3DX", 1640, "18df09cde3772351968a06add85a78df9ed6c9eb"),
    ("XGRPH", 4644, "34f0f4d321de6ce8b4c0e4684ace6ab5efdb9ed4"),
    ("DSOUND", 40156, "d871b5cae1d2109a9e14a22bf10d9eee6f9dca04"),
    ("XMV", 163372, "a78198974882b7c03c16e5a86f110cc418eb8623"),
    ("XPP", 30728, "114f0a50679daacb802aa46804278f8f4b0c820e"),
    (".rdata", 68568, "e1c01dbd59f3ade046353d2c4851db74a55779e2"),
    (".data", 547088, "3c6717c73c9162973aade5fff1f8e5458b35d374"),
    ("DOLBY", 29036, "c8c9142dccbd31bdca9a2d94e0acc47c2048d615"),
    ("$$XTINFO", 356, "36f4853ed1990609d30ab47d31757fdc66b3a0d1"),
    ("$$XTIMAGE", 10240, "23ffba685b95ae64bda0417ceb7880a5da67c521"),
    ("$$XSIMAGE", 4096, "9fd81342aeb1f8f50e0aef3685ff0833631f636b"),
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
        return 5, f"Harry Potter Chamber of Secrets with {count} sections: another build or region (only the USA disc is supported; PAL is not yet)"
    text_pristine = True
    for i, (name, size, digest) in enumerate(REF):
        o = at - base + i * 56
        raw, rsz = struct.unpack_from("<II", b, o + 12)
        stored = b[o + 36:o + 56]
        if rsz != size or stored.hex() != digest:
            return 5, f"section {name} differs: another build or region (only the USA disc is supported; PAL is not yet)"
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
