#!/usr/bin/env python3
# Test du programme resident natres dans un emulateur PowerPC (unicorn),
# avec une carte SD simulee en mode SPI sur le bus EXI.
import os, sys, random, struct
os.environ['LIBUNICORN_PATH'] = os.environ.get('UNICORN_DIR', 'work/unicorn') + '/build'
sys.path.insert(0, os.environ.get('UNICORN_DIR', 'work/unicorn') + '/bindings/python')
from unicorn import *
from unicorn.ppc_const import *

BIN = sys.argv[1]
SDHC = int(sys.argv[2]) if len(sys.argv) > 2 else 1
CHAN = int(sys.argv[3]) if len(sys.argv) > 3 else 2
DEV = int(sys.argv[4]) if len(sys.argv) > 4 else 0
AT = 0x817F0000
random.seed(1)
disk = bytearray(random.getrandbits(8) for _ in range(4096 * 512))   # carte de 2 Mo

class Card:
    def __init__(s):
        s.cmd = []; s.out = []; s.idle = True; s.app = False; s.acmd41 = 0; s.log = []
    def deselect(s): s.cmd = []; s.out = []
    def xfer_in(s, b):
        if s.out and b == 0xFF: return
        if not s.cmd and (b & 0xC0) != 0x40: return
        s.cmd.append(b)
        if len(s.cmd) == 6: s.exec_cmd(); s.cmd = []
    def xfer_out(s):
        return s.out.pop(0) if s.out else 0xFF
    def exec_cmd(s):
        c = s.cmd[0] & 0x3F; arg = int.from_bytes(bytes(s.cmd[1:5]), 'big'); crc = s.cmd[5]
        s.log.append((c, arg))
        r = [0xFF, 0xFF]                       # (la carte met un peu de temps a repondre)
        app, s.app = s.app, False
        if c == 0:
            s.idle = True; s.out = r + [0x01 if crc == 0x95 else 0x09]; return
        if c == 8: s.out = r + [0x01, 0, 0, 0x01, arg & 0xFF]; return
        if c == 55: s.app = True; s.out = r + [0x01 if s.idle else 0]; return
        if c == 41 and app:
            s.acmd41 += 1
            if s.acmd41 >= 3: s.idle = False
            s.out = r + [0x01 if s.idle else 0]; return
        if c == 58: s.out = r + [0, 0xC0 if SDHC else 0x80, 0xFF, 0x80, 0]; return
        if c == 16: s.out = r + [0]; return
        if c == 17:
            if s.idle: s.out = r + [0x01]; return
            sec = arg if SDHC else arg // 512
            if not SDHC and arg % 512: s.out = r + [0x40]; return
            s.out = r + [0] + [0xFF] * 5 + [0xFE] + list(disk[sec * 512:(sec + 1) * 512]) + [0x12, 0x34]; return
        s.out = r + [0x04]

card = Card()
exi = [[0] * 5 for _ in range(3)]
def mmio_read(uc, off, size, data):
    ch, reg = off // 0x14, (off % 0x14) // 4
    return exi[ch][reg] if ch < 3 else 0
def mmio_write(uc, off, size, value, data):
    ch, reg = off // 0x14, (off % 0x14) // 4
    if ch >= 3: return
    exi[ch][reg] = value & 0xFFFFFFFF
    if reg == 0 and not (value & (0x80 << DEV)) and ch == CHAN: card.deselect()
    if reg == 3 and (value & 1):
        n = ((value >> 4) & 3) + 1; mode = (value >> 2) & 3
        sel = ch == CHAN and (exi[ch][0] & (0x80 << DEV))
        clk = ch == CHAN                       # horloge sans CS : la carte ne repond pas
        if mode == 1:
            for i in range(n):
                b = (exi[ch][4] >> (24 - 8 * i)) & 0xFF
                if sel: card.xfer_in(b)
        elif mode == 0:
            v = 0
            for i in range(n):
                b = card.xfer_out() if sel else 0xFF
                v |= b << (24 - 8 * i)
            v |= (exi[ch][4] & ((1 << (32 - 8 * n)) - 1)) if n < 4 else 0
            exi[ch][4] = v
        exi[ch][3] = value & ~1

mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
mu.mem_map(0x80000000, 0x01800000)
mu.mmio_map(0xCC006000, 0x1000, lambda uc, off, sz, d: mmio_read(uc, off - 0x800, sz, d) if off >= 0x800 else 0, None,
            lambda uc, off, sz, v, d: mmio_write(uc, off - 0x800, sz, v, d) if off >= 0x800 else None, None)
blob = open(BIN, 'rb').read()
mu.mem_write(AT, blob)
STOP = 0x80001000
mu.mem_write(STOP, struct.pack('>I', 0x48000000))   # b .
def rd32(a): return struct.unpack('>I', mu.mem_read(a, 4))[0]
def wr32(a, v): mu.mem_write(a, struct.pack('>I', v & 0xFFFFFFFF))
def call(fn, *args):
    for i, a in enumerate(args): mu.reg_write(UC_PPC_REG_3 + i, a)
    mu.reg_write(UC_PPC_REG_1, 0x81000000); mu.reg_write(UC_PPC_REG_LR, STOP)
    mu.emu_start(fn, STOP, timeout=60 * 1000000)
    v = mu.reg_read(UC_PPC_REG_3)
    return v - (1 << 32) if v & 0x80000000 else v

assert rd32(AT) == 0x4743534E, 'magic'
f_init, f_read, f_rsd = rd32(AT + 8), rd32(AT + 0xC), rd32(AT + 0x10)
wr32(AT + 0x14, CHAN); wr32(AT + 0x18, 0); wr32(AT + 0x1C, 5)   # (dev 0 annonce : le resident doit trouver dev 2 tout seul)
r = call(f_init)
print('init ->', r, 'sdhc', rd32(AT + 0x28), 'err', rd32(AT + 0x2C), 'stage', rd32(AT + 0x34), 'dev', rd32(AT + 0x18))
assert r == 0 and rd32(AT + 0x28) == SDHC
assert rd32(AT + 0x18) == DEV
# un secteur brut
buf = 0x80100000
assert call(f_rsd, 1234, buf) == 0 and bytes(mu.mem_read(buf, 512)) == bytes(disk[1234 * 512:1235 * 512]), 'secteur brut'
# ISO en 3 morceaux sur la carte
exts = [(0, 100, 50), (50, 3000, 7), (57, 600, 200)]
ext_ptr = rd32(AT + 0x24)
for i, (a, b, c) in enumerate(exts): mu.mem_write(ext_ptr + i * 12, struct.pack('>III', a, b, c))
wr32(AT + 0x20, len(exts))
iso = b''.join(bytes(disk[b * 512:(b + c) * 512]) for a, b, c in exts)
tests = [(0, 512), (0, 32), (100, 1000), (50 * 512 - 7, 20), (25600 - 3, 3600), (511, 2), (len(iso) - 700, 700), (12345, 64 * 1024)]
for off, ln in tests:
    mu.mem_write(buf, b'\xAA' * (ln + 8))
    r = call(f_read, buf + 1 if ln % 2 else buf, off, ln)
    got = bytes(mu.mem_read(buf + 1 if ln % 2 else buf, ln))
    assert r == 0 and got == iso[off:off + ln], ('lecture', off, ln, r, rd32(AT + 0x2C))
    print('ok lecture', hex(off), ln)
# hors de l'ISO -> erreur propre
assert call(f_read, buf, len(iso) + 10, 4) != 0 and rd32(AT + 0x2C) == 5
print('commandes:', sorted(set(c for c, a in card.log)), 'lectures SD:', rd32(AT + 0x38))
print('TOUT OK')
