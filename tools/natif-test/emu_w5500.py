#!/usr/bin/env python3
# v327 : Pulse avec un adaptateur W5500 (ETH2SP1 dans le port serie 1, ETH2GC dans un port de carte) au lieu du Broadband
# Adapter : W5500 simule (trames SPI : adresse, controle, donnees ; socket 0 en MACRAW, tampons de 16 Ko qui font le tour).
# Verifie : demarrage (adresse Ethernet gardee, socket ouvert), GCMI -> GCMA, GCMR -> GCMD, le direct GCMS, l'anneau de
# reception qui fait le tour, l'ETH2GC dans le port B, et un canal pris par le jeu (on reessaie, sans abandonner).
# Usage : emu_w5500.py natres.bin natres.elf natres.h
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
NRO_MEM = int(re.search(r'#define NRO_MEM\s+(0x[0-9A-Fa-f]+)', hs).group(1), 16)
sym = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.run(['llvm-nm', ELF], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
GC_IP, PH_IP = bytes([192, 168, 1, 50]), bytes([192, 168, 1, 77])
PH_MAC = bytes([0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE])
class W5500:
    def __init__(s, ch, dev):
        s.ch, s.dev = ch, dev; s.blk = {b: bytearray(0x10000) for b in range(32)}; s.st = None; s.sent = []
        s.blk[0][0x09:0x0F] = bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03])      # (adresse mise par GC Station)
        s.blk[0][0x39] = 0x04; s.blk[0][0x2E] = 0x01; s.rx_wr = 0; s.resets = 0; s.opens = 0
    def r16(s, b, a): return s.blk[b][a] << 8 | s.blk[b][a + 1]
    def w16(s, b, a, v): s.blk[b][a] = (v >> 8) & 0xFF; s.blk[b][a + 1] = v & 0xFF
    def sync(s):
        rd = s.r16(1, 0x28); s.w16(1, 0x26, (s.rx_wr - rd) & 0xFFFF); s.w16(1, 0x2A, s.rx_wr & 0xFFFF)
        s.w16(1, 0x20, 0x4000 - ((s.r16(1, 0x24) - s.r16(1, 0x22)) & 0xFFFF))
    def put(s, v, n):
        b = [(v >> (24 - 8 * k)) & 0xFF for k in range(n)]
        if s.st is None:
            if n == 2 and b == [0, 0]: s.st = 'id'; return
            if n == 3: s.addr = b[0] << 8 | b[1]; s.bsb = b[2] >> 3; s.wr = bool(b[2] & 4); s.st = 'data'; return
            s.st = 'bad'; return
        if s.st != 'data' or not s.wr: return
        for x in b:
            m = 0x3FFF if s.bsb in (2, 3) else 0xFFFF
            a = s.addr & m; s.blk[s.bsb][a] = x
            if s.bsb == 0 and a == 0 and x & 0x80: s.reset()
            if s.bsb == 1 and a == 0x01: s.cmd(x)
            s.addr += 1
    def get(s, n):
        if s.st == 'id': return 0x03000000
        if s.st != 'data' or s.wr: return 0
        s.sync(); v = 0
        for k in range(n):
            m = 0x3FFF if s.bsb in (2, 3) else 0xFFFF
            v |= s.blk[s.bsb][s.addr & m] << (24 - 8 * k); s.addr += 1
        return v
    def reset(s):
        mac = bytes(s.blk[0][0x09:0x0F]); s.blk = {b: bytearray(0x10000) for b in range(32)}
        s.blk[0][0x39] = 0x04; s.blk[0][0x2E] = 0x01; s.rx_wr = 0; s.resets += 1      # (la vraie puce efface aussi SHAR)
    def cmd(s, c):
        s.blk[1][0x01] = 0
        if c == 0x01 and s.blk[1][0x00] & 0x0F == 0x04: s.blk[1][0x03] = 0x42; s.opens += 1
        if c == 0x10: s.blk[1][0x03] = 0x00
        if c == 0x20:
            rd, wr = s.r16(1, 0x22), s.r16(1, 0x24)
            s.sent.append(bytes(s.blk[2][(rd + i) & 0x3FFF] for i in range((wr - rd) & 0xFFFF))); s.w16(1, 0x22, wr)
    def deliver(s, f):
        if s.blk[1][0x03] != 0x42: return
        f = f + bytes(max(0, 60 - len(f))); d = struct.pack('>H', len(f) + 2) + f
        for x in d: s.blk[3][s.rx_wr & 0x3FFF] = x; s.rx_wr = (s.rx_wr + 1) & 0xFFFF
wz = W5500(int(os.environ.get('W_CH', '0')), int(os.environ.get('W_DEV', '2')))
exi = [[0] * 5 for _ in range(3)]
busy = {'ch': -1}
si = {}
def io_read(uc, off, size, data):
    if off >= 0x800:
        ch, reg = (off - 0x800) // 0x14, ((off - 0x800) % 0x14) // 4
        if ch < 3 and reg == 0 and ch == busy['ch']: return exi[ch][0] | 0x80     # (le jeu a choisi un autre peripherique)
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
    sel = ch == wz.ch and value & (0x80 << wz.dev)
    if reg == 0 and ch == wz.ch and not (value & (0x80 << wz.dev)): wz.st = None
    if reg == 3 and value & 1:
        n = ((value >> 4) & 3) + 1
        on = ch == wz.ch and exi[ch][0] & (0x80 << wz.dev)
        if (value >> 2) & 3 == 1:
            if on: wz.put(exi[ch][4], n)
        else: exi[ch][4] = wz.get(n) if on else 0
        exi[ch][3] = value & ~1
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
mu.mem_map(0x80000000, 0x01800000)
mu.mmio_map(0xCC006000, 0x1000, io_read, None, io_write, None)
mu.mem_map(0xCC005000, 0x1000)                          # (DSP : DMA audio)
mu.mem_map(0xCC002000, 0x1000)                          # (v307 : video)
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
wr32(H('opt'), NRO_MEM)
blk = 0x80700000
pad = [0] * len(PN)
def P(n, v): pad[PN.index(n)] = v
P('magic', 0x50504144); P('len', len(PN)); P('net.0', 0x504E4554); P('net.1', struct.unpack('>I', GC_IP)[0]); P('net.3', 51237)
P('net.9', 0xFFFFFF00); P('chmask', 0); P('xch', 0xFF)
for i in range(4): P('h%d' % i, 0x00808080); P('l%d' % i, 0x80800000)
words = [0] + pad
for i, w in enumerate(words): wr32(blk + i * 4, w)
wr32(H('chts'), blk)
padw = lambda n: rd32(blk + 4 + PN.index(n) * 4)
POLL = sym['pd_poll']
ok = True
def check(what, cond, extra=''):
    global ok
    print(('OK   ' if cond else 'FAUX ') + what + (' ' + extra if extra else ''))
    ok = ok and cond
def gcm(t, seq, a=0, b=0, c=0):
    d = b'GCM' + t.encode() + struct.pack('>IIII', seq, a, b, c)
    udp = struct.pack('>HHHH', 40001, 51237, 8 + len(d), 0) + d
    ip = struct.pack('>BBHHHBBH', 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0) + PH_IP + GC_IP
    return bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]) + PH_MAC + b'\x08\x00' + ip + udp
def udp_out():
    r = []
    for f in bba.sent:
        if f[12:14] != b'\x08\x00' or f[23] != 17: continue
        ln = struct.unpack('>H', f[38:40])[0]
        r.append((f, f[42:34 + ln]))
    bba.sent.clear()
    return r
def decode(d):
    a = struct.unpack('>I', d[8:12])[0]; mp = d[16:48]; o = 48; out = bytearray()
    for i in range(256):
        if mp[i >> 3] & (0x80 >> (i & 7)): out += d[o:o + 4]; o += 4
        else: out += bytes(4)
    return a, bytes(out)
mu.mem_write(0x80000000, b'GZLE01\x00\x01')
import random
random.seed(5)
ram = bytearray(4096)
for i in range(0, 4096, 4):
    if random.random() < 0.4: ram[i:i + 4] = random.randbytes(4)
mu.mem_write(0x80400000, bytes(ram))
mu.mem_write(0xCC005036, struct.pack('>H', 0x8123))
mu.mem_write(0xCC002000, struct.pack('>HH', 0x0F06 | 287 << 4 & 0x3FF0, 0x0105))
mu.mem_write(0xCC00201C, struct.pack('>I', 0x10000000 | (0x00F00000 >> 5)))
mu.mem_write(0xCC002024, struct.pack('>I', 0x10000000 | (0x00F00500 >> 5)))
mu.mem_write(0xCC002048, struct.pack('>H', 40 << 8 | 80))
def udp_out():
    r = []
    for f in wz.sent:
        if f[12:14] != b'\x08\x00' or f[23] != 17: continue
        ln = struct.unpack('>H', f[38:40])[0]
        r.append((f, f[42:34 + ln]))
    wz.sent.clear()
    return r
def gcms(seq, morceaux, periode=1):
    d = b'GCMS' + struct.pack('>IIII', seq, len(morceaux), periode, 0) + b''.join(struct.pack('>II', a, n) for a, n in morceaux)
    udp = struct.pack('>HHHH', 40001, 51237, 8 + len(d), 0) + d
    ip = struct.pack('>BBHHHBBH', 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0) + PH_IP + GC_IP
    return bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]) + PH_MAC + b'\x08\x00' + ip + udp
where = 'port %d, peripherique %d' % (wz.ch, wz.dev)
if os.environ.get('W_BUSY'):
    busy['ch'] = wz.ch; call(POLL); call(POLL)
    check('canal pris par le jeu : pas abandonne (on reessaiera)', padw('state') == 0, 'etat %d' % padw('state'))
    busy['ch'] = -1
call(POLL)
check('W5500 (%s) demarre' % where, padw('state') == 1 and wz.blk[1][0x03] == 0x42 and wz.resets == 1 and wz.opens == 1, 'etat %d sr %02X' % (padw('state'), wz.blk[1][0x03]))
check('adresse Ethernet gardee (celle de GC Station)', bytes(wz.blk[0][0x09:0x0F]) == bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]), bytes(wz.blk[0][0x09:0x0F]).hex())
check('socket 0 : MACRAW filtre, 16 Ko + 16 Ko, sans interruption', wz.blk[1][0x00] == 0x84 and wz.blk[1][0x1E] == 16 and wz.blk[1][0x1F] == 16 and wz.blk[0][0x18] == 0 and wz.blk[1][0x2C] == 0)
wz.deliver(gcm('I', 7)); call(POLL)
o = udp_out()
check('GCMI -> GCMA vers le telephone', len(o) == 1 and o[0][1][:4] == b'GCMA' and o[0][0][0:6] == PH_MAC and o[0][0][30:34] == PH_IP, str([x[1][:4] for x in o]))
wz.deliver(gcm('R', 8, 0x80400000, 4)); call(POLL)
o = udp_out()
check('GCMR -> 2 blocs GCMD justes', len(o) == 2 and all(decode(x[1]) == (0x80400000 + 1024 * i, bytes(ram[1024 * i:1024 * (i + 1)])) for i, x in enumerate(o)), '%d' % len(o))
mx = [(0x80400000, 100), (0x80400400, 1500), (0x80400100, 8)]
attendu = b''.join(bytes(mu.mem_read(a, n)) for a, n in mx)
wz.deliver(gcms(30, mx)); call(POLL)
o = udp_out()
check('GCMS : 2 paquets a l image, donnees justes', [x[1][:4] for x in o] == [b'GCMS'] * 2 and o[0][1][16:] + o[1][1][16:] == attendu, str([x[1][:4] for x in o]))
# l'anneau de reception fait le tour (16 Ko) : 300 demandes, une par image ; l'envoi aussi
n_ok = 0
for i in range(300):
    wz.deliver(gcm('I', 100 + i)); call(POLL)
    o = [x for x in udp_out() if x[1][:4] == b'GCMA']
    if len(o) == 1 and struct.unpack('>I', o[0][1][4:8])[0] == 100 + i: n_ok += 1
check('300 demandes de suite (tampons de 16 Ko qui font le tour) : 300 reponses justes', n_ok == 300 and wz.rx_wr > 16384 and wz.r16(1, 0x24) > 16384, '%d / 300, reception a %d, envoi a %d (au-dela de 16384 : les deux tampons ont fait le tour)' % (n_ok, wz.rx_wr, wz.r16(1, 0x24)))
wz.deliver(gcms(33, [])); call(POLL); udp_out()
print('TOUT OK' if ok else 'ECHEC')
