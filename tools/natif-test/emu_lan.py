#!/usr/bin/env python3
# v359 : Pulse LAN (jeu en reseau local par Internet) : le resident Pulse LAN dans l'emulateur PowerPC (unicorn), avec un
# Broadband Adapter simule et un "jeu" qui s'en sert comme la bibliotheque reseau (transactions EXI : commande, puis
# donnees en DMA ou immediates). Verifie : demande ARP pour la box, trame du jeu emballee vers l'ami (Ethernet de la
# box, IPv4, UDP 51240, "GCLR1" + partie, la trame telle quelle) et pas envoyee telle quelle, trame de l'ami deballee
# sur place dans l'anneau de reception (longueur corrigee), reponse ARP a la box, signe de vie ignore par le jeu,
# nombre d'octets de la file d'envoi (TXFIFOCNT) vu par le jeu, autres transactions du jeu intactes.
# Usage : emu_lan.py natres_lan.bin natres_lan.elf natres.h
import os, sys, struct, re, subprocess, hashlib
os.environ['LIBUNICORN_PATH'] = os.environ.get('UNICORN_DIR', 'work/unicorn') + '/build'
sys.path.insert(0, os.environ.get('UNICORN_DIR', 'work/unicorn') + '/bindings/python')
from unicorn import *
from unicorn.ppc_const import *
BIN, ELF, HDR = sys.argv[1], sys.argv[2], sys.argv[3]
hs = open(HDR, encoding='latin-1').read()
AT = int(re.search(r'#define NATRES_AT\s+(0x[0-9A-Fa-f]+)', hs).group(1), 16)
b = hs[hs.index('typedef struct {\n    unsigned int magic, version;'):hs.index('} natres_hdr_t;')]
HN = []
for l in b.split('\n'):
    l = l.split('/*')[0].strip()
    if not l.startswith('unsigned int ') and not l.startswith('natres_net_t '): continue
    if l.startswith('natres_net_t '): HN += ['net.%d' % i for i in range(10)]; continue
    for x in l[13:].rstrip(';').split(','):
        m = re.match(r'(\w+)\[(\d+)\]', x.strip())
        HN += ['%s%d' % (m.group(1), i) for i in range(int(m.group(2)))] if m else [x.strip()]
sym = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.run(['llvm-nm', ELF], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
GC_IP, GW_IP, PEER_IP = bytes([192, 168, 1, 50]), bytes([192, 168, 1, 1]), bytes([203, 0, 113, 9])
GC_MAC, GW_MAC, AMI_MAC = bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]), bytes([0x02, 0x11, 0x22, 0x33, 0x44, 0x55]), bytes([0x00, 0x09, 0xBF, 0x0A, 0x0B, 0x0C])
CODE = b'partie-essai'
ROOM = hashlib.sha256(CODE).digest()[:4]
class BBA:
    def __init__(s):
        s.mem = bytearray(0x10000); s.mem[0x20:0x26] = GC_MAC; s.st = None; s.addr = 0; s.wr = False; s.tx = bytearray(); s.sent = []
        s.wr16(0x0A, 1); s.wr16(0x1A, 0x0F); s.wr16(0x16, 1); s.wr16(0x18, 1)          # (reglages du jeu : anneau pages 1..F)
    def rd16(s, a): return s.mem[a] | s.mem[a + 1] << 8
    def wr16(s, a, v): s.mem[a] = v & 0xFF; s.mem[a + 1] = (v >> 8) & 0xFF
    def put_bytes(s, bs):
        if s.st is None:
            c = 0
            for x in bs: c = c << 8 | x
            if len(bs) == 2 and c == 0: s.st = 'id'; return
            if len(bs) == 4 and c & 0x80000000: s.st = 'mx'; s.addr = (c >> 8) & 0xFFFF; s.wr = bool(c & 0x40000000); return
            s.st = 'exi'; return
        if s.st != 'mx' or not s.wr: return
        for x in bs:
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
        return rwp
    def ring(s, p, n):                                  # (n octets du paquet de la page p, apres le descripteur)
        bp, rh = s.rd16(0x0A) & 0xFFF, s.rd16(0x1A) & 0xFFF
        a, o = (p << 8) + 4, bytearray()
        for _ in range(n):
            if a >= (rh + 1) << 8: a = bp << 8
            o.append(s.mem[a]); a += 1
        return bytes(o)
bba = BBA()
exi = [[0] * 5 for _ in range(3)]
mu = None
def io_read(uc, off, size, data):
    if off >= 0x800:
        ch, reg = (off - 0x800) // 0x14, ((off - 0x800) % 0x14) // 4
        return exi[ch][reg] if ch < 3 else 0
    return 0
def io_write(uc, off, size, value, data):
    value &= 0xFFFFFFFF
    if off < 0x800: return
    ch, reg = (off - 0x800) // 0x14, ((off - 0x800) % 0x14) // 4
    if ch >= 3: return
    exi[ch][reg] = value
    if ch == 0 and reg == 0 and not (value & 0x200): bba.st = None
    if ch == 0 and reg == 3 and value & 1:
        rw = (value >> 2) & 3
        if value & 2:                                    # DMA
            mar, ln = exi[0][1] & 0x01FFFFFF, exi[0][2]
            if rw == 1: bba.put_bytes(bytes(uc.mem_read(0x80000000 | mar, ln)))
            else: uc.mem_write(0x80000000 | mar, bytes((bba.get(1) >> 24) & 0xFF for _ in range(ln)))
        else:
            n = ((value >> 4) & 3) + 1
            if rw == 1: bba.put_bytes(bytes((exi[0][4] >> (24 - 8 * k)) & 0xFF for k in range(n)))
            else: exi[0][4] = bba.get(n)
        exi[0][3] = value & ~1
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
mu.mem_map(0x80000000, 0x01800000)
mu.mem_map(0xC0000000, 0x01800000)                      # (memoire sans cache : copie a part, tenue a jour plus bas)
mu.mmio_map(0xCC006000, 0x1000, io_read, None, io_write, None)
mu.mem_map(0xCC406000 & ~0xFFF, 0x1000)
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
XL, XS = rd32(H('exi_load')), rd32(H('exi_store'))
LC = sym['lan_cfg']
for i, v in enumerate([0x504C414E, struct.unpack('>I', GC_IP)[0], 0xFFFFFF00, struct.unpack('>I', GW_IP)[0], struct.unpack('>I', PEER_IP)[0], 51240, struct.unpack('>I', ROOM)[0]]):
    wr32(LC + 4 * i, v)
lan = lambda i: rd32(LC + 4 * i)
ok = True
def check(what, cond, extra=''):
    global ok
    print(('OK   ' if cond else 'FAUX ') + what + (' ' + extra if extra else ''))
    ok = ok and cond
# ---- le "jeu" : transactions EXI, comme la bibliotheque reseau ----
def sel(): call(XS, 0xCC006800, 0x200 | 5 << 4)
def desel(): call(XS, 0xCC006800, 0)
def imm_w(v, n): call(XS, 0xCC006810, v); call(XS, 0xCC00680C, (n - 1) << 4 | 1 << 2 | 1)
def imm_r(n): call(XS, 0xCC00680C, (n - 1) << 4 | 0 << 2 | 1); return call(XL, 0xCC006810)
def dma_w(addr, data):
    mu.mem_write(addr, data); mu.mem_write(0xC0000000 | (addr & 0x01FFFFFF), data)
    call(XS, 0xCC006804, addr & 0x01FFFFFF); call(XS, 0xCC006808, len(data)); call(XS, 0xCC00680C, 2 | 1 << 2 | 1)
def mx_w8(a, v): sel(); imm_w(0xC0000000 | a << 8, 4); imm_w(v << 24, 1); desel()
def mx_r8(a): sel(); imm_w(0x80000000 | a << 8, 4); v = imm_r(1) >> 24; desel(); return v
def game_send(frame):
    sel(); imm_w(0xC0000000 | 0x48 << 8, 4); dma_w(0x80500000, frame[:len(frame) & ~31]);
    for x in frame[len(frame) & ~31:]: imm_w(x << 24, 1)
    desel()
    sel(); imm_w(0xC0000000 | 0x3E << 8, 4); imm_r(1); desel()   # (rien : juste pour la suite)
    mx_w8(0x00, 0x0C)                                    # NCRA : SR | ST1
# 1) premiere transaction du jeu : Pulse demande ou est la box (ARP)
n0 = len(bba.sent)
v = mx_r8(0x09)
arp = bba.sent[n0] if len(bba.sent) > n0 else b''
check('demande ARP pour la box', len(arp) >= 42 and arp[12:14] == b'\x08\x06' and arp[38:42] == GW_IP and arp[28:32] == GC_IP, arp[:42].hex())
# 2) la box repond : adresse retenue
rep = GC_MAC + GW_MAC + b'\x08\x06' + struct.pack('>HHBBH', 1, 0x0800, 6, 4, 2) + GW_MAC + GW_IP + GC_MAC + GC_IP
bba.deliver(rep); mx_r8(0x09)
# 3) une trame du jeu : emballee vers l'ami
frame = b'\xff' * 6 + GC_MAC + b'\x08\x00' + bytes(range(100))
n0 = len(bba.sent)
game_send(frame)
out = bba.sent[n0] if len(bba.sent) > n0 else b''
check('trame du jeu emballee vers l\'ami', len(out) == 51 + len(frame) and out[:6] == GW_MAC and out[6:12] == GC_MAC and out[26:30] == GC_IP and out[30:34] == PEER_IP
      and struct.unpack('>H', out[36:38])[0] == 51240 and out[42:47] == b'GCLR1' and out[47:51] == ROOM and out[51:] == frame, '%d octets' % len(out))
ip = out[14:34]
s = sum(struct.unpack('>10H', ip)); s = (s & 0xFFFF) + (s >> 16); s = (s & 0xFFFF) + (s >> 16)
check('en-tete IPv4 juste (longueur, somme)', struct.unpack('>H', ip[2:4])[0] == 20 + 8 + 9 + len(frame) and s == 0xFFFF)
check('pas envoyee telle quelle', len(bba.sent) == n0 + 1 and lan(7) == 1, 'envois %d, compte %d' % (len(bba.sent) - n0, lan(7)))
# 4) une trame de l'ami : deballee sur place
inner = AMI_MAC + GC_MAC + b'\x08\x00' + bytes((i * 7) & 255 for i in range(80))
outer = GC_MAC + GW_MAC + b'\x08\x00' + struct.pack('>BBHHHBBH4s4s', 0x45, 0, 20 + 8 + 9 + len(inner), 1, 0, 64, 17, 0, PEER_IP, GC_IP) + struct.pack('>HHHH', 51240, 51240, 8 + 9 + len(inner), 0) + b'GCLR1' + ROOM + inner
pg = bba.deliver(outer)
mx_r8(0x09)
d = struct.unpack('<I', bytes(bba.mem[pg << 8:(pg << 8) + 4]))[0]
check('trame de l\'ami deballee (le jeu la lit telle quelle)', (d >> 12) & 0xFFF == len(inner) + 4 and bba.ring(pg, len(inner)) == inner, 'longueur %d' % (((d >> 12) & 0xFFF) - 4))
check('compte des trames recues', lan(8) == 1)
# 5) signe de vie de l'ami : rien pour le jeu (type inconnu)
ka = GC_MAC + GW_MAC + b'\x08\x00' + struct.pack('>BBHHHBBH4s4s', 0x45, 0, 37, 2, 0, 64, 17, 0, PEER_IP, GC_IP) + struct.pack('>HHHH', 51240, 51240, 17, 0) + b'GCLR1' + ROOM
pg = bba.deliver(ka); mx_r8(0x09)
check('signe de vie ignore par le jeu', bba.ring(pg, 14)[12:14] == b'\x88\xb5')
# 6) une partie differente : pas touchee
other = outer[:47] + b'XXXX' + inner
pg = bba.deliver(other); mx_r8(0x09)
check('autre partie : pas touchee', bba.ring(pg, len(other)) == other)
# 7) la box demande ou est la console : reponse
req = b'\xff' * 6 + GW_MAC + b'\x08\x06' + struct.pack('>HHBBH', 1, 0x0800, 6, 4, 1) + GW_MAC + GW_IP + bytes(6) + GC_IP
n0 = len(bba.sent); bba.deliver(req); mx_r8(0x09)
r = bba.sent[n0] if len(bba.sent) > n0 else b''
check('reponse ARP a la box', len(r) >= 42 and r[:6] == GW_MAC and r[20:22] == b'\x00\x02' and r[22:28] == GC_MAC and r[28:32] == GC_IP)
# 8) le jeu lit TXFIFOCNT pendant qu'il remplit la file : le nombre d'octets de sa trame
sel(); imm_w(0xC0000000 | 0x48 << 8, 4); dma_w(0x80500000, bytes(64)); desel()
sel(); imm_w(0x80000000 | 0x3E << 8, 4); v = imm_r(2); desel()
check('TXFIFOCNT vu par le jeu', (v >> 24) == 64 and ((v >> 16) & 0xFF) == 0, '%08X' % v)
mx_w8(0x00, 0x0C)
# 9) autres transactions du jeu : intactes
mx_w8(0x08, 0x5A)
check('registre ecrit par le jeu (IMR) : intact', bba.mem[0x08] == 0x5A)
print('TOUT OK' if ok else 'ECHEC')
sys.exit(0 if ok else 1)
