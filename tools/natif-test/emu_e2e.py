#!/usr/bin/env python3
# Test de bout en bout : la memoire preparee par GC Station (lancement natif) est chargee
# dans un emulateur PowerPC, avec une carte SD simulee (image FAT32) sur l'EXI et un
# vrai lecteur DVD simule (qui ne sait faire que "inquiry"). Le faux jeu doit lire l'ISO.
import os, sys, struct
os.environ['LIBUNICORN_PATH'] = os.environ.get('UNICORN_DIR', 'work/unicorn') + '/build'
sys.path.insert(0, os.environ.get('UNICORN_DIR', 'work/unicorn') + '/bindings/python')

from unicorn import *
from unicorn.ppc_const import *
from unicorn import ppc_const as _pc

BOOT, SDIMG, ISO = sys.argv[1], sys.argv[2], sys.argv[3]
disk = bytearray(open(SDIMG, 'rb').read())
iso = open(ISO, 'rb').read()
CHAN, DEV, SDHC = 2, 0, 1

class Card:
    def __init__(s): s.cmd = []; s.out = []; s.idle = True; s.app = False; s.acmd41 = 0; s.log = []; s.wr = None; s.wbuf = None; s.writes = []
    def deselect(s): s.cmd = []; s.out = []; s.wr = None; s.wbuf = None
    def xfer_in(s, b):
        if s.wr is not None:                        # ecriture (CMD24) : jeton FE, 512 octets, CRC
            if s.wbuf is None:
                if b == 0xFE: s.wbuf = bytearray()
                return
            s.wbuf.append(b)
            if len(s.wbuf) == 514:
                disk[s.wr * 512:(s.wr + 1) * 512] = s.wbuf[:512]; s.writes.append(s.wr)
                s.out = [0xFF, 0x05, 0x00, 0x00, 0xFF]; s.wr = None; s.wbuf = None
            return
        if s.out and b == 0xFF: return
        if not s.cmd and (b & 0xC0) != 0x40: return
        s.cmd.append(b)
        if len(s.cmd) == 6: s.exec_cmd(); s.cmd = []
    def xfer_out(s): return s.out.pop(0) if s.out else 0xFF
    def exec_cmd(s):
        c = s.cmd[0] & 0x3F; arg = int.from_bytes(bytes(s.cmd[1:5]), 'big'); crc = s.cmd[5]
        s.log.append((c, arg)); r = [0xFF]
        app, s.app = s.app, False
        if c == 0: s.idle = True; s.acmd41 = 0; s.out = r + [0x01 if crc == 0x95 else 0x09]; return
        if c == 8: s.out = r + [0x01, 0, 0, 0x01, arg & 0xFF]; return
        if c == 55: s.app = True; s.out = r + [0x01 if s.idle else 0]; return
        if c == 41 and app:
            s.acmd41 += 1
            if s.acmd41 >= 2: s.idle = False
            s.out = r + [0x01 if s.idle else 0]; return
        if c == 58: s.out = r + [0, 0xC0, 0xFF, 0x80, 0]; return
        if c == 24: s.out = r + [0]; s.wr = arg; return
        if c == 17:
            sec = arg
            s.out = r + [0, 0xFF, 0xFE] + list(disk[sec * 512:(sec + 1) * 512]) + [0, 0]; return
        s.out = r + [0x04]
card = Card()
exi = [[0] * 5 for _ in range(3)]
di_real = [0] * 10
kicks = []

si_inbuf = [0x80000000, 0x11900000, 0x80000000, 0x80000000]   # manette 1 : A + START + Z
si_sr = [0x00200000, 0]                               # (v58 : manette 1 avec RDST les 20 premieres fois : ne doit pas etre lue)
si_reads = []
resets = []
def pi_write(uc, off, size, value, data):
    if off == 0x24:
        resets.append(value & 0xFFFFFFFF)
        if value & 0xFFFFFFFF == 0: uc.emu_stop()       # redemarrage de la console
def pi_read(uc, off, size, data): return 0
def io_read(uc, off, size, data):
    if 0x400 <= off < 0x430 and (off - 0x400) % 12 == 4:
        si_reads.append((off, si_sr[1])); return si_inbuf[(off - 0x400) // 12]
    if 0x400 <= off < 0x430 and (off - 0x400) % 12 == 8: return 0
    if off == 0x438:                                    # SISR : RDST (donnees neuves pas encore prises)
        si_sr[1] += 1
        return si_sr[0] if si_sr[1] <= 20 else 0
    if off >= 0x800:
        o = off - 0x800; ch, reg = o // 0x14, (o % 0x14) // 4
        return exi[ch][reg] if ch < 3 else 0
    if off < 0x28: return di_real[off // 4]
    return 0
def io_write(uc, off, size, value, data):
    value &= 0xFFFFFFFF
    if off < 0x28:
        i = off // 4
        if i == 0:                                  # vrai DISR : 1 = acquitter, masques
            di_real[0] = (di_real[0] & ~(value & 0x54) & 0x54) | (value & 0x2A)
        else: di_real[i] = value
        if i == 7 and value & 1:                    # vrai lecteur : ne sait que "inquiry"
            kicks.append(di_real[2])
            di_real[7] = value & ~1
            di_real[0] |= 0x10 if di_real[2] >> 24 == 0x12 else 0x04
        return
    if off < 0x800: return
    o = off - 0x800; ch, reg = o // 0x14, (o % 0x14) // 4
    if ch >= 3: return
    exi[ch][reg] = value
    if reg == 0 and ch == CHAN and not (value & (0x80 << DEV)): card.deselect()
    if reg == 3 and (value & 1):
        n = ((value >> 4) & 3) + 1; mode = (value >> 2) & 3
        sel = ch == CHAN and (exi[ch][0] & (0x80 << DEV))
        if mode == 1:
            for k in range(n):
                if sel: card.xfer_in((exi[ch][4] >> (24 - 8 * k)) & 0xFF)
        elif mode == 0:
            v = 0
            for k in range(n): v |= (card.xfer_out() if sel else 0xFF) << (24 - 8 * k)
            exi[ch][4] = v
        exi[ch][3] = value & ~1

mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
mu.mem_map(0x80000000, 0x01800000)
mu.mmio_map(0xCC006000, 0x1000, io_read, None, io_write, None)
mu.mmio_map(0xCC003000, 0x1000, pi_read, None, pi_write, None)
b = open(BOOT, 'rb').read()
entry, n = struct.unpack_from('>II', b, 0); p = 8
for i in range(n):
    dst, sz = struct.unpack_from('>II', b, p); p += 8
    mu.mem_write(dst, b[p:p + sz]); p += sz
    print('copie %08X %d' % (dst, sz))
mu.mem_write(0x80000000, b[p:p + 0x20])
EXC = len(sys.argv) > 4 and sys.argv[4] == 'exc'          # v60 : faux plantage
if EXC: mu.mem_write(0x80001F40, struct.pack('>I', 1))
STOP = 0x80001000
mu.mem_write(STOP, struct.pack('>I', 0x48000000))
mu.reg_write(UC_PPC_REG_1, 0x81000000); mu.reg_write(UC_PPC_REG_LR, STOP)
mu.reg_write(UC_PPC_REG_31, 0xDEAD); mu.reg_write(UC_PPC_REG_30, 0xBEEF)
try:
    mu.emu_start(entry, STOP, timeout=300 * 1000000)
except UcError as e:
    print('ERREUR emulateur', e, 'pc=%08X lr=%08X' % (mu.reg_read(UC_PPC_REG_PC), mu.reg_read(UC_PPC_REG_LR))); sys.exit(1)
pc = mu.reg_read(UC_PPC_REG_PC)
print('fin pc=%08X r1=%08X r30=%X r31=%X' % (pc, mu.reg_read(UC_PPC_REG_1), mu.reg_read(UC_PPC_REG_30), mu.reg_read(UC_PPC_REG_31)))
ok = True
def chk(name, addr, want):
    global ok
    got = bytes(mu.mem_read(addr, len(want)))
    good = got == want
    ok &= good
    print(('OK   ' if good else 'FAUX ') + name)
chk('ID du disque (A8000040)', 0x80100000, iso[0:0x20])
chk('lecture non alignee 0x2000 @0x1004', 0x80101000, iso[0x1004:0x3004])
chk('grosse lecture 0x40000 @0x20000', 0x80200000, iso[0x20000:0x60000])
H = 0x817F0000
rd = lambda o: struct.unpack('>I', mu.mem_read(H + o, 4))[0]
print('resident : commandes=%d derniere=%08X err=%d sdhc=%d secteurs lus=%d' % (rd(0x4C), rd(0x50), rd(0x2C), rd(0x28), rd(0x38)))
print('vrai lecteur : %d commandes inquiry (%s)' % (len(kicks), ' '.join('%08X' % k for k in kicks)))
# v57 : A+Z+START -> redemarrage (la fin normale n'est donc pas atteinte)
rd32 = lambda a: struct.unpack('>I', mu.mem_read(a, 4))[0]
tbl = [rd32(0x80003040 + i * 4) for i in (20, 21, 24)]
hk = resets == [3, 0] and 0x817F0000 <= tbl[0] < 0x81800000 and 0x817F0000 <= tbl[2] < 0x81800000 and rd32(0x80001F00) == 3 and (EXC or 55 <= rd32(0x80001F04) <= 70)
print(('OK   ' if hk else 'FAUX ') + 'crochets + A+Z+START : reset %s, table SI %08X VI %08X, images %d, manettes %d' % (resets, tbl[0], tbl[2], rd32(0x80001F00), rd32(0x80001F04)))
ok &= hk
nolat = all(not (o == 0x40C and n <= 20) for o, n in si_reads) and (EXC or len(si_reads) > 0)
print(('OK   ' if nolat else 'FAUX ') + 'manettes : donnees neuves pas touchees (%d lectures SI)' % len(si_reads))
ok &= nolat
ok &= struct.unpack('>I', mu.mem_read(0x80001F24, 4))[0] == 0 and struct.unpack('>I', mu.mem_read(0x80001F20, 4))[0] == 0
ok &= rd(0x4C) == 4 and len(kicks) == 4 and all(k == 0x12000000 for k in kicks)
if card.writes:                                    # v54 : journal du jeu ecrit sur la carte
    js = card.writes[-1]; jb = disk[js * 512:js * 512 + 512]; open('journal.bin', 'wb').write(jb)
    u = lambda o: struct.unpack_from('>I', jb, o)[0]
    jok = len(set(card.writes)) == 1 and jb[:4] == b'GCSN' and u(0x4C) == 4 and u(0x90) == 3
    jok &= (u(0xB0) == 0x102 and u(0xB4) == 0x80001234 and u(0xB8) == 0x80005678 and u(0x98) == 0) if EXC else (u(0x98) == 1 and u(0xB0) == 0)
    if EXC: print('plantage note : %X a %08X LR %08X' % (u(0xB0), u(0xB4), u(0xB8)))
    print(('OK   ' if jok else 'FAUX ') + 'journal du jeu : %d ecritures, secteur %X, commandes=%d' % (len(card.writes), js, struct.unpack_from('>I', jb, 0x4C)[0]))
    ok &= jok
else:
    print('FAUX journal du jeu : rien ecrit'); ok = False
_m = {27: 0x1F10, 28: 0x1F14, 29: 0x1F18, 10: 0x1F1C, 30: 0x1F20, 31: 0x1F24}
r = lambda n: struct.unpack('>I', mu.mem_read(0x80000000 + _m[n], 4))[0]
v52 = r(29) == 0xE000 and r(28) == 0xE0000000 and r(10) == 0xCC006008 and r(27) == 0x1234
print(('OK   ' if v52 else 'FAUX ') + 'lhz / lwzu / sth (r29=%X r28=%X r10=%X r27=%X)' % (r(29), r(28), r(10), r(27)))
ok &= v52
print('TOUT OK' if ok else 'ECHEC')
