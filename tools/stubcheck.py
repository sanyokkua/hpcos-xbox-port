#!/usr/bin/env python3
"""Are any of the lifter's "not detected" stubs real code? tools/stubcheck.py  (.venv: capstone)

engine/gen/recomp_stubs_unresolved.c gives every direct call or jump target the disassembler did not
detect as a function an empty body (`g_esp += 4`, a bare ret). For bytes the lifter decoded out of
alignment that is harmless: nothing reaches them. For a real instruction start -- typically a function's
shared exit (`pop esi; pop ebp; pop edi; ret`) that another path jumps to -- it is a silent bug: the
pops never happen, the caller gets its registers and stack back wrong (CoS: the bean pick-up at 0x721C0
tail-jumped to 0x72510, the entity update's loop registers became pointers, and the game froze or
crashed at the next door). Each stub is classed:
  BOUNDARY  an instruction start inside a detected function: real code, seed it (tools/addseed.py)
  code      outside every function but in .text: real code, seed it
  mid-insn  inside an instruction of a detected function: a misdecode, harmless
  data      outside .text: harmless
Exits 1 when any stub is real code. Seeding one can split a function and turn its internal jumps into
new stubs: relift (tools/relift.sh) and run this again until it passes."""
import bisect, glob, json, os, re, sys
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(R, "tools"))
from sigmatch import load, read
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

TEXT = (0x11000, 0x130620)
secs = load(os.path.join(R, "game/extracted/default.xbe"))
fs = json.load(open(os.path.join(R, "engine/xboxrecomp/tools/disasm/output/functions.json")))
rng = sorted((int(f["start"], 16), int(f["end"], 16)) for f in fs)
starts = [r[0] for r in rng]
md = Cs(CS_ARCH_X86, CS_MODE_32)
refs = {}
for path in glob.glob(os.path.join(R, "engine/gen/recomp_0*.c")):
    cur = None
    for line in open(path, errors="ignore"):
        m = re.match(r"^void (sub_[0-9A-F]{8})(_gen)?\(void\)", line)
        if m:
            cur = m.group(1)
        for t in re.findall(r"\b(sub_[0-9A-F]{8})\(\)", line):
            refs.setdefault(t, set()).add(cur or "?")
stubs = re.findall(r"0x([0-9A-F]+): not detected", open(os.path.join(R, "engine/gen/recomp_stubs_unresolved.c")).read())
real = 0
for a in (int(x, 16) for x in stubs):
    i = bisect.bisect_right(starts, a) - 1
    if i >= 0 and rng[i][0] <= a < rng[i][1]:
        s = rng[i][0]
        kind = "BOUNDARY" if a in {x.address for x in md.disasm(read(secs, s, a - s + 16) or b"", s)} else "mid-insn"
    else:
        kind = "code" if TEXT[0] <= a < TEXT[1] else "data"
    real += kind in ("BOUNDARY", "code")
    ins = [f"{x.mnemonic} {x.op_str}" for x, _ in zip(md.disasm(read(secs, a, 16) or b"", a), range(2))]
    print(f"{a:#010x} {kind:8} from {','.join(sorted(refs.get('sub_%08X' % a, [])))} | {' ; '.join(ins)}")
print(f"stubcheck: {len(stubs)} stubs, {real} real code" + (" -- seed them" if real else ""))
sys.exit(1 if real else 0)
