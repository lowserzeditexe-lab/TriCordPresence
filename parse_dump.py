import struct

data = open("/app/crash.dmp", "rb").read()
print("total size:", len(data))
print("hex dump:")
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    hexs = " ".join(f"{b:02x}" for b in chunk)
    asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
    print(f"{i:04x}: {hexs:<48} {asc}")

print("\n=== Parse Luma3DS exception dump ===")
magic0, magic1 = struct.unpack_from("<II", data, 0)
print(f"magic: {magic0:08x} {magic1:08x}  (expect deadc0de deadcafe)")
if (magic0, magic1) != (0xdeadc0de, 0xdeadcafe):
    print("Unexpected magic; format may differ")
verMajor, verMinor, processor, core = struct.unpack_from("<HHHH", data, 8)
typ, flags, nbReg, codeSize, stackSize, addSize = struct.unpack_from("<IIIIII", data, 16)
print(f"version {verMajor}.{verMinor} processor={processor} core={core}")
print(f"type={typ} flags={flags} nbRegisters={nbReg} codeDumpSize={codeSize} stackDumpSize={stackSize} additionalDataSize={addSize}")

off = 40
regs = list(struct.unpack_from(f"<{nbReg}I", data, off))
off += nbReg*4
names = ["r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r11","r12","sp","lr","pc","cpsr","dfsr","ifsr","far","fpexc","fpinst","fpinst2"]
print("\nRegisters:")
for i,v in enumerate(regs):
    nm = names[i] if i < len(names) else f"reg{i}"
    print(f"  {nm:>7} = {v:08x}")

code = data[off:off+codeSize]; off += codeSize
stack = data[off:off+stackSize]; off += stackSize
add = data[off:off+addSize]
print(f"\ncode dump bytes: {len(code)}")
print(f"stack dump bytes: {len(stack)}")
print(f"additional data ({len(add)} bytes): {add!r}")

# exception type meaning (arm11)
types = {0:"FIQ", 1:"Undefined instruction", 2:"Prefetch abort", 3:"Data abort"}
print("\nException type:", types.get(typ, f"unknown({typ})"))
