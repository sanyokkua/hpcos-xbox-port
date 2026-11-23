#!/usr/bin/env python3
"""Differential test of the lifted code (dev tool; .venv: unicorn).

tools/difftest.py <dir>/<n>_pre.bin [<entry va, default 0x53710>]

The game wrote <n>_pre.bin / <n>_post.bin around one call of a lifted function (HPCOS_DIFF_DIR,
HPCOS_DIFF_AT; see recomp_manual.c). This runs the ORIGINAL x86 bytes from the "pre" state in
Unicorn until the function returns, then compares registers and guest memory with "post" (what
the lifted C produced). A difference that is not float rounding is a lifting bug candidate.
Kernel calls and MMIO accesses are reported; the run stops at the first kernel call."""
import struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.x86_const import *

HDR = struct.Struct('<4sI8IiI8d4I')


def load(path):
    with open(path, 'rb') as f:
        h = HDR.unpack(f.read(HDR.size))
        main_lo, main_size, cbase, csize = h[20:24]
        main = f.read(main_size)
        contig = f.read(csize)
    return dict(regs=h[2:10], fp_top=h[10], fp_cw=h[11], fp=h[12:20],
                main_lo=main_lo, main=main, cbase=cbase, contig=contig)


def main():
    pre_path = sys.argv[1]
    entry = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x53710
    post_path = pre_path.replace('_pre.bin', '_post.bin')
    pre, post = load(pre_path), load(post_path)
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    lo = pre['main_lo']
    uc.mem_map(lo, (len(pre['main']) + 0xFFF) & ~0xFFF)
    uc.mem_write(lo, pre['main'])
    uc.mem_map(pre['cbase'], len(pre['contig']))
    uc.mem_write(pre['cbase'], pre['contig'])
    eax, ecx, edx, ebx, esp, ebp, esi, edi = pre['regs']
    for r, v in zip((UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                     UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI), pre['regs']):
        uc.reg_write(r, v)
    uc.reg_write(UC_X86_REG_FPCW, pre['fp_cw'] or 0x027F)
    ret = struct.unpack_from('<I', pre['main'], esp - lo)[0]
    events = []
    n = [0]

    def on_unmapped(uc, access, addr, size, value, ud):
        events.append('unmapped access %#x (size %d, eip %#x)' % (addr, size, uc.reg_read(UC_X86_REG_EIP)))
        return False

    def on_code(uc, addr, size, ud):
        n[0] += 1
        if n[0] > 200_000_000:
            events.append('instruction limit'); uc.emu_stop()

    import os
    watch = [int(x, 16) for x in os.environ.get('DIFF_WATCH', '').split(',') if x]
    if watch:
        from unicorn import UC_HOOK_MEM_WRITE
        def on_write(uc, access, addr, size, value, ud):
            for w in watch:
                if addr <= w < addr + size:
                    v = struct.unpack('<f', struct.pack('<I', value & 0xFFFFFFFF))[0] if size == 4 else value
                    print('  write %#x = %#x (%g) at eip %#x' % (w, value & 0xFFFFFFFF, v,
                                                              uc.reg_read(UC_X86_REG_EIP)))
        uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    # functions the port replaces or wraps (engine/src/*.c): the emulator runs their original
    # bytes, so a difference after one of them may be the override's doing, not a lifting bug
    import glob, re
    over = set()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'engine', 'src')
    for fn in glob.glob(os.path.join(root, '*.c')):
        over |= {int(a, 16) for a in re.findall(r'\nvoid sub_([0-9A-F]{8})\(void\)', open(fn).read())}
    over.discard(entry)
    seen = {}

    def on_block(uc, addr, size, ud):
        if addr in over:
            seen[addr] = seen.get(addr, 0) + 1
        if trace is not None:
            trace.append(addr)
    trace = [] if os.environ.get('DIFF_TRACE') else None
    from unicorn import UC_HOOK_BLOCK
    uc.hook_add(UC_HOOK_BLOCK, on_block)
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)
    uc.hook_add(UC_HOOK_CODE, on_code, begin=1, end=0)
    try:
        uc.emu_start(entry, ret)
    except UcError as e:
        events.append('stopped: %s at eip %#x' % (e, uc.reg_read(UC_X86_REG_EIP)))
    print('entry %#x ret %#x: %d instructions; %s' % (entry, ret, n[0], '; '.join(events) or 'returned'))
    for a, c in sorted(seen.items()):
        print('  entered overridden sub_%08X %d times' % (a, c))
    if trace is not None:
        with open(os.environ['DIFF_TRACE'], 'w') as f:
            f.write('\n'.join('%08x' % a for a in trace))
    names = ('eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi')
    regs = (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
            UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI)
    for nm, r, v in zip(names, regs, post['regs']):
        u = uc.reg_read(r)
        if nm in ('eax', 'esp', 'ebx', 'esi', 'edi', 'ebp') and u != v:
            print('  reg %s: x86 %#x lifted %#x' % (nm, u, v))
    final_esp = uc.reg_read(UC_X86_REG_ESP)
    for name, base, before, after in (('main', lo, pre['main'], post['main']),
                                      ('contig', pre['cbase'], pre['contig'], post['contig'])):
        emu = bytes(uc.mem_read(base, len(before)))
        diffs = []
        # dword compare where either side changed
        for off in range(0, len(before) - 3, 4):
            a = emu[off:off + 4]; b = after[off:off + 4]
            if a != b:
                va = base + off
                if final_esp - 0x4000 <= va < final_esp:       # dead stack below the final esp
                    continue
                fa, fb = struct.unpack('<f', a)[0], struct.unpack('<f', b)[0]
                ia, ib = struct.unpack('<I', a)[0], struct.unpack('<I', b)[0]
                close = (fa == fa and fb == fb and abs(fa - fb) <= 1e-3 * max(1.0, abs(fa), abs(fb)))
                diffs.append((va, ia, ib, fa, fb, close, before[off:off + 4] != a, before[off:off + 4] != b))
        real = [d for d in diffs if not d[5]]
        print('  %s: %d dwords differ, %d beyond float rounding' % (name, len(diffs), len(real)))
        for va, ia, ib, fa, fb, close, emu_wrote, lift_wrote in real[:60]:
            print('    %#010x x86 %08x (%g) lifted %08x (%g)  written by %s' % (
                va, ia, fa, ib, fb, '+'.join(w for w, f in (('x86', emu_wrote), ('lifted', lift_wrote)) if f) or '?'))


if __name__ == '__main__':
    main()
