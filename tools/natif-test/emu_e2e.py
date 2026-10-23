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
ai = [0] * 8                                          # v65 : registres AI (0xCC006C00)
dspr = {}                                             # v65 : registres DSP / DMA du son (0xCC005000, 16 bits)
bad0 = []                                             # v65 : la vraie carte du slot A ne doit jamais etre choisie
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
    if 0xC00 <= off < 0xC20: return ai[(off - 0xC00) // 4]
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
    if 0xC00 <= off < 0xC20: ai[(off - 0xC00) // 4] = value; return
    if off < 0x800: return
    o = off - 0x800; ch, reg = o // 0x14, (o % 0x14) // 4
    if ch >= 3: return
    if ch == 0 and reg == 0 and value & 0x80: bad0.append(('CSR', value))
    if ch == 0 and reg == 3 and value & 1 and exi[0][0] & 0x80: bad0.append(('CR', value))
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
mu.mmio_map(0xCC000000, 0x1000, pi_read, None, lambda *a: None, None)      # (CP : ecritures ignorees)
def dsp_write(uc, off, size, value, data):
    if size == 2: dspr[off] = value & 0xFFFF
    elif size == 4: dspr[off] = (value >> 16) & 0xFFFF; dspr[off + 2] = value & 0xFFFF
def dsp_read(uc, off, size, data):
    return dspr.get(off, 0) if size == 2 else (dspr.get(off, 0) << 16) | dspr.get(off + 2, 0)
mu.mmio_map(0xCC005000, 0x1000, dsp_read, None, dsp_write, None)          # (DSP / DMA du son)
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
print('vrai lecteur : %d commandes inquiry' % len(kicks))
# v57 : A+Z+START -> redemarrage (la fin normale n'est donc pas atteinte)
rd32 = lambda a: struct.unpack('>I', mu.mem_read(a, 4))[0]
tbl = [rd32(0x80003040 + i * 4) for i in (20, 21, 24)]
ret = rd32(0x80001F50) == 0x42424242 and pc == STOP      # v64 : retour a GC Station (.dol recharge, pas de redemarrage)
print(('OK   ' if ret else 'FAUX ') + 'retour a GC Station : faux .dol recharge depuis la carte et lance (%08X, pc %08X)' % (rd32(0x80001F50), pc))
ok &= ret
hk = resets == [] and 0x817F0000 <= tbl[0] < 0x81800000 and 0x817F0000 <= tbl[2] < 0x81800000 and rd32(0x80001F00) == 3 and (EXC or 55 <= rd32(0x80001F04) <= 70)
print(('OK   ' if hk else 'FAUX ') + 'crochets + A+Z+START / plantage : redemarrages %s, table SI %08X VI %08X, images %d, manettes %d' % (resets, tbl[0], tbl[2], rd32(0x80001F00), rd32(0x80001F04)))
ok &= hk
nolat = all(not (o == 0x40C and n <= 20) for o, n in si_reads) and (EXC or len(si_reads) > 0)
print(('OK   ' if nolat else 'FAUX ') + 'manettes : donnees neuves pas touchees (%d lectures SI)' % len(si_reads))
ok &= nolat
ok &= struct.unpack('>I', mu.mem_read(0x80001F24, 4))[0] == 0 and struct.unpack('>I', mu.mem_read(0x80001F20, 4))[0] == 0
ok &= rd(0x4C) == 6 and len(kicks) >= 6 + 31 and all(k == 0x12000000 for k in kicks)   # (v61 : + 31 reveils pour les morceaux ; v65 : + E1, E2)
# v65 : musique du disque (ADPCM du disque decode et ajoute au tampon du son)
def adp(bits, q, h):
    hist = 0
    if q >> 4 == 1: hist = h[0] * 0x3c
    elif q >> 4 == 2: hist = h[0] * 0x73 - h[1] * 0x34
    elif q >> 4 == 3: hist = h[0] * 0x62 - h[1] * 0x37
    hist = max(-0x200000, min(0x1fffff, (hist + 0x20) >> 6))
    b = (bits << 12) & 0xFFFF
    b = b - 0x10000 if b & 0x8000 else b
    cur = ((b >> (q & 0xf)) << 6) + hist
    h[1] = h[0]; h[0] = cur
    return max(-0x8000, min(0x7fff, cur >> 6))
pcm = []; hl = [0, 0]; hr = [0, 0]
for blk in range(6):
    a = iso[0x10000 + blk * 32:0x10000 + blk * 32 + 32]
    for i in range(28): pcm.append((adp(a[i + 4] & 0xF, a[0], hl), adp(a[i + 4] >> 4, a[1], hr)))
want = b''.join(struct.pack('>hh', max(-0x8000, min(0x7fff, (l * 255) >> 8)), max(-0x8000, min(0x7fff, (r * 255) >> 8))) for l, r in pcm[:160])
got = bytes(mu.mem_read(0x80300000, 0x280))
dtk = got == want and rd32(0x80001F28) == 1 and rd32(0x80001F08) == 1
print(('OK   ' if dtk else 'FAUX ') + 'musique du disque : 160 echantillons ajoutes au son (%d identiques), etat E2 = %d, fonction son du jeu %d' % (sum(1 for i in range(0, 0x280, 4) if got[i:i + 4] == want[i:i + 4]), rd32(0x80001F28), rd32(0x80001F08)))
ok &= dtk
# v65 : carte memoire virtuelle : ID, ecritures (DMA) et relecture, contenu dans le fichier sur la carte SD
cn, ct = rd(0xF8), rd(0xFC)
ext = [struct.unpack('>III', mu.mem_read(ct + i * 12, 12)) for i in range(cn)]
def csd(sec):
    for i0, sd, c in ext:
        if i0 <= sec < i0 + c: return sd + sec - i0
    return None
pat = b''.join(struct.pack('>I', 0xA5000000 | (i * 4)) for i in range(32))
orig = lambda o, n: bytes(((x >> 9) * 7 + x) & 0xFF for x in range(o, o + n))
exp = pat + orig(0x2080, 0x180)
exp2 = pat + orig(0x110080, 0x180)
r1 = bytes(mu.mem_read(0x80410000, 0x200)); r2 = bytes(mu.mem_read(0x80410200, 0x200))
s1, s2 = csd(0x2000 >> 9), csd(0x110000 >> 9)
f1 = bytes(disk[s1 * 512:s1 * 512 + 512]) if s1 else b''; f2 = bytes(disk[s2 * 512:s2 * 512 + 512]) if s2 else b''
mc = rd32(0x80001F2C) == 0x10 and rd32(0x80001F30) & 0x1000 and r1 == exp and r2 == exp2 and f1 == exp and f2 == exp2 and not bad0 and exi[1][4] == 0x5A5A5A5A and cn == 2 and rd(0x48) & 4
print(('OK   ' if mc else 'FAUX ') + 'carte memoire virtuelle : ID %X, CSR %X, relu %s/%s, fichier %s/%s, %d morceaux, canal 1 reel %X, vraie carte touchee %s' % (
    rd32(0x80001F2C), rd32(0x80001F30), r1 == exp, r2 == exp2, f1 == exp, f2 == exp2, cn, exi[1][4], bad0[:3]))
ok &= bool(mc)
csecs = {csd(0x2000 >> 9), csd(0x110000 >> 9)}
if card.writes:                                    # v54 : journal du jeu ecrit sur la carte
    js = card.writes[-1]; jb = disk[js * 512:js * 512 + 512]; open('journal.bin', 'wb').write(jb)
    u = lambda o: struct.unpack_from('>I', jb, o)[0]
    jok = set(card.writes) - csecs == ({js, js + 1} if EXC else {js}) and jb[:4] == b'GCSN' and u(0x4C) == 6 and u(0x90) == 3
    if EXC:                                        # v79 : details du plantage dans le secteur suivant
        cb = disk[(js + 1) * 512:(js + 2) * 512]; open('journal.bin', 'ab').write(cb)
        cu = lambda o: struct.unpack_from('>I', cb, o)[0]
        cok = cb[:4] == b'GCSC' and cu(0x0C) == 0x80001234 and cu(0x14) == 0x80005678 and cu(0x30 + 4) == 0x80001A00 and cu(0x30 + 16) == 0x01234567
        cok &= cu(0x2C) == 2 and cu(0xD0) == 0x80007777 and cu(0xD4) == 0x80008888 and cb[0x110:0x120] == b'm_Do_machine.cpp'
        cok &= all(cu(0xB0 + i * 4) == rd32(0x80000000 + o) for i, o in enumerate((0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0xF0, 0xF4)))
        print(('OK   ' if cok else 'FAUX ') + 'details du plantage : PC %08X LR %08X r1 %08X r4 %08X, %d appels %08X %08X, texte r3 %r' % (
            cu(0x0C), cu(0x14), cu(0x34), cu(0x40), cu(0x2C), cu(0xD0), cu(0xD4), bytes(cb[0x110:0x120])))
        jok &= cok
    print('journal : musique %d blocs, %d tampons ; carte %d commandes (derniere %02X), %d ecritures, %d interruptions' % (u(0x108), u(0x10C), u(0x110), u(0x118), u(0x114), u(0x11C)))
    print('echantillons : %d, code %08X LR %08X' % (u(0x130), u(0x138), u(0x134)))
    jok &= u(0x130) > 0 and u(0x134) == 0x80003100 and 0x80003100 < u(0x138) < 0x80004000
    jok &= u(0x108) == 6 and u(0x10C) == 1 and u(0x110) == 5 and u(0x118) == 0x52 and u(0x114) == 2
    print('lectures en fond : %d (%d morceaux)' % (u(0xD0), u(0xD4)))
    jok &= u(0xD0) == 1 and u(0xD4) == 32                # (0x40000 octets en morceaux de 8 Ko)
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
