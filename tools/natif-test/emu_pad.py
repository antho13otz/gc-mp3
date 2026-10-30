#!/usr/bin/env python3
# (prive) manettes du telephone : le resident Pulse dans l'emulateur PowerPC (unicorn), avec un Broadband Adapter
# et des registres SI simules. Verifie : ARP pour l'adresse de la console, paquet "GCPD" -> INBUF du port, SISR sans
# erreur avec donnees neuves, reponses "type" (0900) et "origine" de la bibliotheque des manettes, ports pas touches.
# Usage : emu_pad.py natres.bin natres.elf natres.h
import os, sys, struct, re, subprocess
os.environ['LIBUNICORN_PATH'] = os.environ.get('UNICORN_DIR', 'work/unicorn') + '/build'
sys.path.insert(0, os.environ.get('UNICORN_DIR', 'work/unicorn') + '/bindings/python')
from unicorn import *
from unicorn.ppc_const import *
BIN, ELF, HDR = sys.argv[1], sys.argv[2], sys.argv[3]
hs = open(HDR, encoding='latin-1').read()
AT = int(re.search(r'#define NATRES_AT\s+(0x[0-9A-Fa-f]+)', hs).group(1), 16)
def fields(name_start, name_end):
    b = hs[hs.index(name_start):hs.index(name_end)]
    names = []
    for l in b.split('\n'):
        l = l.split('/*')[0].strip()
        if not l.startswith('unsigned int ') and not l.startswith('natres_net_t '): continue
        if l.startswith('natres_net_t '): names += ['net.%d' % i for i in range(10)]; continue
        for x in l[13:].rstrip(';').split(','):
            m = re.match(r'(\w+)\[(\d+)\]', x.strip())
            names += ['%s%d' % (m.group(1), i) for i in range(int(m.group(2)))] if m else [x.strip()]
    return names
HN = fields('typedef struct {\n    unsigned int magic, version;', '} natres_hdr_t;')
PN = fields('typedef struct {\n    unsigned int magic;                                   /* "PPAD" */', '} natres_pad_t;')
NRO_PHONE = int(re.search(r'#define NRO_PHONE\s+(0x[0-9A-Fa-f]+)', hs).group(1), 16)
sym = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.run(['llvm-nm', ELF], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
GC_IP, PH_IP = bytes([192, 168, 1, 50]), bytes([192, 168, 1, 77])
PH_MAC = bytes([0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE])
class BBA:
    def __init__(s):
        s.mem = bytearray(0x10000); s.mem[0x20:0x26] = bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]); s.st = None; s.addr = 0; s.wr = False; s.tx = bytearray(); s.sent = []
    def rd16(s, a): return s.mem[a] | s.mem[a + 1] << 8
    def wr16(s, a, v): s.mem[a] = v & 0xFF; s.mem[a + 1] = (v >> 8) & 0xFF
    def put(s, v, n):
        b = [(v >> (24 - 8 * k)) & 0xFF for k in range(n)]
        if s.st is None:
            c = 0
            for x in b: c = c << 8 | x
            if n == 2 and c == 0: s.st = 'id'; return
            if n == 4 and c & 0x80000000: s.st = 'mx'; s.addr = (c >> 8) & 0xFFFF; s.wr = bool(c & 0x40000000); return
            s.st = 'exi'; return
        if s.st != 'mx' or not s.wr: return
        for x in b:
            if s.addr == 0x48: s.tx.append(x); continue
            s.mem[s.addr] = x
            if s.addr == 0x00 and x & 0x04: s.sent.append(bytes(s.tx)); s.tx = bytearray(); s.mem[0] &= ~0x06 & 0xFF
            s.addr += 1
    def get(s, n):
        if s.st == 'id': return 0x04020200
        if s.st != 'mx': return 0
        v = 0
        for k in range(n): v |= s.mem[(s.addr + k) & 0xFFFF] << (24 - 8 * k)
        s.addr += n
        return v
    def deliver(s, f):
        f = f + bytes(max(0, 60 - len(f)))
        bp, rh = s.rd16(0x0A) & 0xFFF, s.rd16(0x1A) & 0xFFF
        rwp = s.rd16(0x16) & 0xFFF
        a = rwp << 8; data = bytes(4) + f
        for x in data:
            if a >= (rh + 1) << 8: a = bp << 8
            s.mem[a] = x; a += 1
        nxt = (a + 255) >> 8
        if nxt > rh: nxt = bp
        s.mem[(rwp << 8):(rwp << 8) + 4] = struct.pack('<I', nxt | (4 + len(f)) << 12)
        s.wr16(0x16, nxt)
bba = BBA()
exi = [[0] * 5 for _ in range(3)]
si = {}
def io_read(uc, off, size, data):
    if off >= 0x800:
        ch, reg = (off - 0x800) // 0x14, ((off - 0x800) % 0x14) // 4
        return exi[ch][reg] if ch < 3 else 0
    if 0x400 <= off < 0x500: return si.get(off, 0)
    return 0
def io_write(uc, off, size, value, data):
    value &= 0xFFFFFFFF
    if 0x400 <= off < 0x500: si[off] = value; return
    if off < 0x800: return
    ch, reg = (off - 0x800) // 0x14, ((off - 0x800) % 0x14) // 4
    if ch >= 3: return
    exi[ch][reg] = value
    if ch == 0 and reg == 0 and not (value & 0x200): bba.st = None
    if ch == 0 and reg == 3 and value & 1:
        n = ((value >> 4) & 3) + 1
        if (value >> 2) & 3 == 1: bba.put(exi[0][4], n)
        else: exi[0][4] = bba.get(n)
        exi[0][3] = value & ~1
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
mu.mem_map(0x80000000, 0x01800000)
mu.mmio_map(0xCC006000, 0x1000, io_read, None, io_write, None)
mu.mem_map(0xCC406000 & ~0xFFF, 0x1000)                 # (adresse de secours de NRO_TRAP : pas utilisee ici)
mu.mem_write(AT, open(BIN, 'rb').read())
STOP = 0x80001000
mu.mem_write(STOP, struct.pack('>I', 0x48000000))
def rd32(a): return struct.unpack('>I', mu.mem_read(a, 4))[0]
def wr32(a, v): mu.mem_write(a, struct.pack('>I', v & 0xFFFFFFFF))
def call(fn, *args):
    for i, a in enumerate(args): mu.reg_write(UC_PPC_REG_3 + i, a)
    mu.reg_write(UC_PPC_REG_1, 0x81000000); mu.reg_write(UC_PPC_REG_LR, STOP)
    mu.emu_start(fn, STOP, timeout=20 * 1000000)
    return mu.reg_read(UC_PPC_REG_3)
H = lambda n: AT + HN.index(n) * 4
wr32(H('opt'), NRO_PHONE)
blk = 0x80700000
pad = [0] * len(PN)
def P(n, v): pad[PN.index(n)] = v
P('magic', 0x50504144); P('len', len(PN)); P('net.0', 0x504E4554); P('net.1', struct.unpack('>I', GC_IP)[0]); P('net.3', 51237)
P('net.9', 0xFFFFFF00); P('chmask', 0x2); P('xch', 0xFF)
for i in range(4): P('h%d' % i, 0x00808080); P('l%d' % i, 0x80800000)
words = [0] + pad
for i, w in enumerate(words): wr32(blk + i * 4, w)
wr32(H('chts'), blk)
padw = lambda n: rd32(blk + 4 + PN.index(n) * 4)
SL, SS, POLL = rd32(H('si_load')), rd32(H('si_store')), sym['pd_poll']
ok = True
def check(what, cond, extra=''):
    global ok
    print(('OK   ' if cond else 'FAUX ') + what + (' ' + extra if extra else ''))
    ok = ok and cond
# SISR : port 2 (canal 1) sans erreur, donnees neuves ; port 1 (canal 0) tel quel
si[0x438] = 0x08080808                                 # (NOREP partout : aucune manette branchee)
v = call(SL, 0xCC006438)
check('SISR : port 2 = donnees neuves, sans erreur ; port 1 inchange', v == 0x08200808, '%08X' % v)
# commande "type" (00) vers le port 2 : reponse 0900
call(SS, 0xCC006480, 0x00000000); call(SS, 0xCC006434, 0x80000000 | (1 << 1) | 1)
v = call(SL, 0xCC006480)
check('type du port 2 : manette standard', v == 0x09000000, '%08X' % v)
call(SS, 0xCC006480, 0x41000000); call(SS, 0xCC006434, (1 << 1) | 1)
v0, v1 = call(SL, 0xCC006480), call(SL, 0xCC006484)
check('origine du port 2 : sticks au centre', v0 == 0x00808080 and v1 == 0x80801F1F, '%08X %08X' % (v0, v1))
call(SS, 0xCC006480, 0x00000000); call(SS, 0xCC006434, (0 << 1) | 1); si[0x480] = 0x12345678
v = call(SL, 0xCC006480)
check('transfert vers le port 1 : pas touche', v == 0x12345678, '%08X' % v)
# BBA : demande ARP pour la console, puis un paquet de manette pour le port 2
call(POLL)
check('BBA demarre', padw('state') == 1, 'etat %d' % padw('state'))
arp = b'\xff' * 6 + PH_MAC + b'\x08\x06' + bytes([0, 1, 8, 0, 6, 4, 0, 1]) + PH_MAC + PH_IP + bytes(6) + GC_IP
bba.deliver(arp); call(POLL)
rep = [f for f in bba.sent if f[12:14] == b'\x08\x06' and f[20:22] == b'\x00\x02']
check('reponse ARP au telephone', len(rep) == 1 and rep[0][0:6] == PH_MAC and rep[0][28:32] == GC_IP)
def gcpd(ch, seq, h, l):
    d = b'GCPD' + bytes([1, ch, 0, 0]) + struct.pack('>III', seq, h, l)
    udp = struct.pack('>HHHH', 40000, 51237, 8 + len(d), 0) + d
    ip = struct.pack('>BBHHHBBH', 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0) + PH_IP + GC_IP
    return bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]) + PH_MAC + b'\x08\x00' + ip + udp
bba.deliver(gcpd(1, 1, 0x01000000 | 0x20 << 8 | 0xE0, 0x9070C010)); call(POLL)
h, l = call(SL, 0xCC006404 + 12), call(SL, 0xCC006408 + 12)
check('port 2 : A, stick (20, E0), stick C, gachettes', h == 0x018020E0 and l == 0x9070C010, '%08X %08X' % (h, l))
si[0x404] = 0x00808080 | 0x10000000; v = call(SL, 0xCC006404)
check('port 1 : la vraie manette', v == 0x10808080, '%08X' % v)
bba.deliver(gcpd(2, 5, 0x10000000, 0)); call(POLL)
check('paquet pour un port pas donne au telephone : refuse', padw('bad') == 1 and padw('rx') == 1, 'bad %d rx %d' % (padw('bad'), padw('rx')))
bba.deliver(gcpd(1, 1, 0x02000000, 0)); call(POLL)
check('meme numero : ignore', call(SL, 0xCC006410) == 0x018020E0)
bba.deliver(gcpd(1, 2, 0x02000000 | 0x8080, 0x80800000)); call(POLL)
check('paquet suivant : B', call(SL, 0xCC006410) == 0x02808080, '%08X' % call(SL, 0xCC006410))
print('TOUT OK' if ok else 'ECHEC')
