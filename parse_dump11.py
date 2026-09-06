import struct, subprocess, os

DMP = "/app/crash11.dmp"
ELF = "/app/tricord_src/tricord-presence/sysmodule/tricord_presenced.elf"
NM = "/opt/dkp_root/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm"

data = open(DMP, "rb").read()
print("total size:", len(data))

magic0, magic1 = struct.unpack_from("<II", data, 0)
assert (magic0, magic1) == (0xdeadc0de, 0xdeadcafe), "bad magic"
verMajor, verMinor, processor, core = struct.unpack_from("<HHHH", data, 8)
excType, totalSize, regDumpSize, codeDumpSize, stackDumpSize, addDataSize = struct.unpack_from("<IIIIII", data, 16)
print(f"version {verMajor}.{verMinor} processor={processor} core={core}")
types = {0:"FIQ", 1:"Undefined instruction", 2:"Prefetch abort", 3:"Data abort"}
print(f"exceptionType={excType} ({types.get(excType,'?')})")
print(f"regDumpSize={regDumpSize} codeDumpSize={codeDumpSize} stackDumpSize={stackDumpSize} addDataSize={addDataSize}")

off = 40
nbReg = regDumpSize // 4
regs = list(struct.unpack_from(f"<{nbReg}I", data, off)); off += regDumpSize
names = ["r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r11","r12","sp","lr","pc","cpsr","dfsr","ifsr","far","fpexc","fpinst","fpinst2"]
reg = {}
print("\nRegisters:")
for i,v in enumerate(regs):
    nm = names[i] if i < len(names) else f"reg{i}"
    reg[nm]=v
    print(f"  {nm:>8} = {v:08x}")

code = data[off:off+codeDumpSize]; off += codeDumpSize
stack = data[off:off+stackDumpSize]; off += stackDumpSize
add = data[off:off+addDataSize]
print(f"\nadditional data: {add!r}")
# process name (8) + title id (u64) typical
if len(add) >= 16:
    name = add[:8].split(b'\x00')[0].decode('latin1')
    tid = struct.unpack_from("<Q", add, 8)[0]
    print(f"process name: {name!r}  title id: {tid:016X}")

# symbol map
syms=[]
out = subprocess.check_output([NM, "-n", ELF]).decode()
for line in out.splitlines():
    p=line.split()
    if len(p)>=3 and p[1] in ("t","T"):
        try:
            a=int(p[0],16)
        except ValueError:
            continue
        syms.append((a,p[2]))
syms.sort()
def find(addr):
    prev=None
    for a,n in syms:
        if a<=addr: prev=(a,n)
        else: break
    return prev
print("\nFault locations:")
for label in ("pc","lr"):
    if label in reg:
        r=find(reg[label])
        if r: print(f"  {label.upper()}={reg[label]:#010x} -> {r[1]} (+{reg[label]-r[0]:#x})")
        else: print(f"  {label.upper()}={reg[label]:#010x} -> (no symbol)")

# decode stack return addresses that look like code (.text 0x100000-0x161000)
print("\nStack words in .text range (possible return addresses):")
sp = reg.get("sp",0)
for i in range(0, len(stack), 4):
    w = struct.unpack_from("<I", stack, i)[0]
    if 0x00100000 <= w < 0x00161000:
        r=find(w)
        loc = f"{r[1]} (+{w-r[0]:#x})" if r else "?"
        print(f"  [sp+{i:#x}]={w:#010x} -> {loc}")
