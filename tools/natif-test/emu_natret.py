#!/usr/bin/env python3
# v187 : test du retour a GC Station en quittant une appli (natret, en 0x80001800) dans l'emulateur PowerPC :
# carte SD simulee (celle de emu_natres.py), faux .dol de GC Station en 3 morceaux sur la carte, saut en 0x80001800
# comme le fait libogc (exit) : le .dol doit etre recharge a sa place et lance.
import os, sys, struct, random
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'emu_natres.py')).read()
BIN = sys.argv[1]; SDHC_A = sys.argv[2] if len(sys.argv) > 2 else '1'; CHAN_A = sys.argv[3] if len(sys.argv) > 3 else '2'
sys.argv = ['emu_natres.py', BIN, SDHC_A, CHAN_A, '0']
exec(src[:src.index("mu = Uc(")])                      # (carte SD et bus EXI simules)
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
mu.mem_map(0x80000000, 0x01800000)
mu.mmio_map(0xCC000000, 0x10000, lambda uc, off, sz, d: mmio_read(uc, off - 0x6800, sz, d) if 0x6800 <= off < 0x6840 else 0, None,
            lambda uc, off, sz, v, d: mmio_write(uc, off - 0x6800, sz, v, d) if 0x6800 <= off < 0x6840 else hw.append((off, v)), None)
hw = []
def rd32(a): return struct.unpack('>I', mu.mem_read(a, 4))[0]
# faux .dol : texte en 0x80003100 (r3 = 0x42424242 ; b .), donnees en 0x80400000 (60 Ko), point d'entree 0x80003100
text = struct.pack('>IIII', 0x3C604242, 0x60634242, 0x48000000, 0) + bytes(random.getrandbits(8) for _ in range(3000))
data = bytes(random.getrandbits(8) for _ in range(60000))
hdr = bytearray(0x100)
struct.pack_into('>I', hdr, 0, 0x100); struct.pack_into('>I', hdr, 0x48, 0x80003100); struct.pack_into('>I', hdr, 0x90, len(text))
struct.pack_into('>I', hdr, 0x1C, 0x100 + len(text)); struct.pack_into('>I', hdr, 0x64, 0x80400000); struct.pack_into('>I', hdr, 0xAC, len(data))
struct.pack_into('>I', hdr, 0xE0, 0x80003100)
dol = bytes(hdr) + text + data
nsec = (len(dol) + 511) // 512
exts = [(0, 700, 3), (3, 2000, 40), (43, 900, nsec - 43)]   # (3 morceaux, dans le desordre sur la carte)
for fs, sd, cnt in exts:
    for k in range(cnt):
        chunk = dol[(fs + k) * 512:(fs + k + 1) * 512].ljust(512, b'\0')
        disk[(sd + k) * 512:(sd + k + 1) * 512] = chunk
blob = bytearray(open(BIN, 'rb').read())
assert blob[4:12] == b'STUBHAXX' and blob[12:16] == b'GCRT', 'signature'
mu.mem_write(0x80001800, bytes(blob))
mu.mem_write(0x80001800 + len(blob), bytes(0x80002C00 - 0x80001800 - len(blob)))
def w32(a, v): mu.mem_write(a, struct.pack('>I', v))
w32(0x80001810, CHAN); w32(0x80001814, 0); w32(0x80001818, 4); w32(0x8000181C, len(exts)); w32(0x80001820, len(dol))
for i, (fs, sd, cnt) in enumerate(exts): mu.mem_write(0x80001830 + i * 12, struct.pack('>III', fs, sd, cnt))
mu.mem_write(0x80003100, b'\xAA' * len(text)); mu.mem_write(0x80400000, b'\x55' * len(data))   # (ce que l'appli avait laisse)
mu.reg_write(UC_PPC_REG_MSR, mu.reg_read(UC_PPC_REG_MSR) | 0x8000)
mu.emu_start(0x80001800, 0x80003108, timeout=120 * 1000000)
pc = mu.reg_read(UC_PPC_REG_PC); r3 = mu.reg_read(UC_PPC_REG_3)
ok = pc == 0x80003108 and r3 == 0x42424242
ok &= bytes(mu.mem_read(0x80003100, len(text))) == text and bytes(mu.mem_read(0x80400000, len(data))) == data
ok &= not (mu.reg_read(UC_PPC_REG_MSR) & 0x8000)
print('etape %d err %d sdhc %d ; pc %08X r3 %08X ; commandes %s' % (rd32(0x80001828), rd32(0x80001824), rd32(0x8000182C), pc, r3, sorted(set(c for c, a in card.log))))
print('TOUT OK' if ok else 'ECHEC')
