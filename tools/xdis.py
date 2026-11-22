import sys,struct
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from sigmatch import load,read
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
s=load(sys.argv[1]); va=int(sys.argv[2],16); n=int(sys.argv[3]) if len(sys.argv)>3 else 40
md=Cs(CS_ARCH_X86,CS_MODE_32)
for i,ins in enumerate(md.disasm(read(s,va,600),va)):
    if i>=n: break
    print(f'{ins.address:08x}  {ins.mnemonic} {ins.op_str}')
