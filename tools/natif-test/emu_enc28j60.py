#!/usr/bin/env python3
# v327 : Pulse avec un adaptateur W5500 (ETH2SP1 dans le port serie 1, ETH2GC dans un port de carte) au lieu du Broadband
# Adapter : W5500 simule (trames SPI : adresse, controle, donnees ; socket 0 en MACRAW, tampons de 16 Ko qui font le tour).
# Verifie : demarrage (adresse Ethernet gardee, socket ouvert), GCMI -> GCMA, GCMR -> GCMD, le direct GCMS, l'anneau de
# reception qui fait le tour, l'ETH2GC dans le port B, et un canal pris par le jeu (on reessaie, sans abandonner).
# v455 : copie pour l'ENC28J60 (emu_enc28j60.py) : puce simulee (commandes SPI, banques, MAC/MII, tampon de 8 Ko avec
# l'anneau de reception, PHY) et le Pulse ENC28J60 (PULSE_NET=4). Ci-dessous, l'en-tete d'origine (W5500).
# v455 : W_CHIP=6100 ou 6300 : la meme chose avec un W6100 / W6300 (registres ailleurs, verrous CHPLCKR / NETLCKR,
# identifiant EXI 0xFF800000 et trames de 4 octets pour le W6300) et le Pulse W6x00 (PULSE_NET=3).
# Usage : emu_w5500.py natres.bin natres.elf natres.h
import os, sys, struct, re, subprocess
os.environ['LIBUNICORN_PATH'] = os.environ.get('UNICORN_DIR', 'work/unicorn') + '/build'
sys.path.insert(0, os.environ.get('UNICORN_DIR', 'work/unicorn') + '/bindings/python')
from unicorn import *
from unicorn.ppc_const import *
BIN, ELF, HDR = sys.argv[1], sys.argv[2], sys.argv[3]
hs = open(HDR, encoding='latin-1').read()
AT = int(re.search(r'#define NATRES_AT(?:_FULL)?\s+(0x[0-9A-Fa-f]+)', hs).group(1), 16)
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
class ENC28J60:
    # registres : 4 banques de 32 (0x1B-0x1F communs) ; MAC / MII : banque 2 et banque 3 0x00-0x05, 0x0A (octet vide d'abord)
    def __init__(s, ch, dev):
        s.ch, s.dev = ch, dev; s.st = None; s.sent = []; s.resets = 0; s.phy = {}; s.reset0()
    def reset0(s):
        s.r = [bytearray(32) for _ in range(4)]; s.mem = bytearray(8192)
        s.r[0][0x00], s.r[0][0x01] = 0xFA, 0x05                       # ERDPT apres remise a zero
        s.r[0][0x1D] = 0x01                                             # ESTAT.CLKRDY
        s.r[0][0x1E] = 0x80                                             # ECON2.AUTOINC
        s.rx_wr = 0; s.cnt = 0
    def bank(s): return s.r[0][0x1F] & 3
    def reg(s, a): return (0, a) if a >= 0x1B else (s.bank(), a)
    def getr(s, a): b, a = s.reg(a); return s.r[0][a] if a >= 0x1B else s.r[b][a]
    def setr(s, a, v):
        b, a = s.reg(a)
        if a >= 0x1B:
            old = s.r[0][a]; s.r[0][a] = v
            if a == 0x1F and v & 0x08 and not old & 0x08: s.tx()
            if a == 0x1E and v & 0x40: s.r[0][0x1E] &= ~0x40; s.cnt = max(0, s.cnt - 1)
            return
        s.r[b][a] = v
        if b == 2 and a == 0x17: s.phy[s.r[2][0x14]] = s.r[2][0x16] | v << 8            # MIWRH : ecriture du PHY
    def mac(s, a): b, a = s.reg(a); return a < 0x1B and (b == 2 or (b == 3 and (a <= 5 or a == 0x0A)))
    def p16(s, b, a): return s.r[b][a] | s.r[b][a + 1] << 8
    def tx(s):
        st, nd = s.p16(0, 0x04), s.p16(0, 0x06)
        s.sent.append(bytes(s.mem[st + 1:nd + 1])); s.r[0][0x1F] &= ~0x08; s.r[0][0x1C] |= 0x08
    def put(s, v, n):
        b = [(v >> (24 - 8 * k)) & 0xFF for k in range(n)]
        if s.st is None:
            op = b[0]
            if op == 0xFF: s.reset0(); s.resets += 1; s.st = 'done'; return
            if n == 2 and b == [0, 0]: s.st = 'id'; return
            if op == 0x3A: s.st = 'rbm'; return
            if op == 0x7A: s.st = 'wbm'; b = b[1:]
            elif op >> 5 == 0: s.st = 'rcr'; s.a = op & 0x1F; s.dummy = s.mac(s.a); return
            elif op >> 5 in (2, 4, 5):
                s.st = ('wcr', 'bfs', 'bfc')[(2, 4, 5).index(op >> 5)]; s.a = op & 0x1F; b = b[1:]
            else: s.st = 'bad'; return
        for x in b:
            if s.st == 'wcr': s.setr(s.a, x)
            elif s.st == 'bfs': s.setr(s.a, s.getr(s.a) | x)
            elif s.st == 'bfc': s.setr(s.a, s.getr(s.a) & ~x & 0xFF)
            elif s.st == 'wbm':
                w = s.p16(0, 0x02); s.mem[w & 0x1FFF] = x; w = (w + 1) & 0x1FFF; s.r[0][0x02], s.r[0][0x03] = w & 0xFF, w >> 8
    def get(s, n):
        if s.st == 'id': return s.r[0][0x00] << 24 | s.r[0][0x01] << 16
        v = 0
        for k in range(n):
            x = 0
            if s.st == 'rcr':
                if s.dummy: s.dummy = False
                else:
                    if s.a == 0x19 and s.bank() == 1: s.r[1][0x19] = min(s.cnt, 255)
                    x = s.getr(s.a)
            elif s.st == 'rbm':
                r = s.p16(0, 0x00); x = s.mem[r]
                st, nd = s.p16(0, 0x08), s.p16(0, 0x0A)
                r = st if r == nd else (r + 1) & 0x1FFF; s.r[0][0x00], s.r[0][0x01] = r & 0xFF, r >> 8
            v |= x << (24 - 8 * k)
        return v
    def deliver(s, f):
        if not s.r[0][0x1F] & 0x04: return
        f = f + bytes(max(0, 60 - len(f))); st, nd = s.p16(0, 0x08), s.p16(0, 0x0A)
        size = 6 + len(f) + 4; nx = s.rx_wr + size; nx += nx & 1
        if nx > nd: nx = st + (nx - nd - 1)
        d = struct.pack('<HHH', nx, len(f) + 4, 0x0080) + f + bytes(4)
        for x in d:
            s.mem[s.rx_wr] = x; s.rx_wr = st if s.rx_wr == nd else s.rx_wr + 1
        s.rx_wr = nx; s.cnt += 1
wz = ENC28J60(int(os.environ.get('W_CH', '0')), int(os.environ.get('W_DEV', '2')))
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
# numero de la console (registres ECID 924-927) : l'emulateur n'en a pas ; les mfspr de Pulse deviennent des li de valeurs connues
ECID = [0x1111, 0x2222, 0x3333, 0x4444]
img = bytearray(open(BIN, 'rb').read()); nfix = 0
for o in range(0, len(img) - 3, 4):
    w = struct.unpack('>I', img[o:o + 4])[0]
    if w & 0xFC0007FE == (31 << 26 | 339 << 1):
        spr = ((w >> 11) & 31) << 5 | ((w >> 16) & 31)
        if 924 <= spr <= 927:
            rd = (w >> 21) & 31; img[o:o + 4] = struct.pack('>I', 0x38000000 | rd << 21 | ECID[spr - 924]); nfix += 1
mu.mem_write(AT, bytes(img))
assert nfix == 4, "mfspr ECID : %d trouves" % nfix
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
    return GC_MAC + PH_MAC + b'\x08\x00' + ip + udp
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
    return GC_MAC + PH_MAC + b'\x08\x00' + ip + udp
where = 'port %d, peripherique %d' % (wz.ch, wz.dev)
if os.environ.get('W_BUSY'):
    busy['ch'] = wz.ch; call(POLL); call(POLL)
    check('canal pris par le jeu : pas abandonne (on reessaiera)', padw('state') == 0, 'etat %d' % padw('state'))
    busy['ch'] = -1
call(POLL)
d = bytearray(19)
for k in range(16): d[k] = (ECID[k >> 2] >> (24 - 8 * (k & 3))) & 0xFF
d[16] ^= 0x04; d[17] ^= 0xA3
sm = wz.ch
for k in range(0, 18, 3): sm += d[k] << 16 | d[k + 1] << 8 | d[k + 2]; sm = (sm & 0xFFFFFF) + (sm >> 24)
MAC = bytes([0x00, 0x09, 0xBF, (sm >> 16) & 0xFF, (sm >> 8) & 0xFF, sm & 0xFF])
got = bytes([wz.r[3][0x04], wz.r[3][0x05], wz.r[3][0x02], wz.r[3][0x03], wz.r[3][0x00], wz.r[3][0x01]])
check('ENC28J60 (%s) demarre' % where, padw('state') == 1 and wz.resets == 1 and wz.r[0][0x1F] & 0x04, 'etat %d resets %d econ1 %02X' % (padw('state'), wz.resets, wz.r[0][0x1F]))
check('adresse Ethernet : celle de la pile de GC Station (numero de la console)', got == MAC, got.hex() + ' / ' + MAC.hex())
check('tampons : reception 0-0FFF, filtre (adresse, CRC, diffusion), MAC pret, PHY sans boucle locale', wz.p16(0, 0x08) == 0 and wz.p16(0, 0x0A) == 0x0FFF and wz.r[1][0x18] == 0xA1 and wz.r[2][0x00] & 1 and wz.r[2][0x02] == 0x32 and wz.phy.get(0x10) == 0x0100 and wz.r[0][0x1B] == 0)
GC_MAC = MAC
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
check('300 demandes de suite (anneau de reception de 4 Ko qui fait le tour) : 300 reponses justes', n_ok == 300 and wz.cnt == 0, '%d / 300, trames en attente %d' % (n_ok, wz.cnt))
wz.deliver(gcms(33, [])); call(POLL); udp_out()
print('TOUT OK' if ok else 'ECHEC')
