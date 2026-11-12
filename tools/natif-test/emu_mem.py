#!/usr/bin/env python3
# v306 : chercheur de codes de l'appli (et le direct "GCMS") : le resident Pulse dans l'emulateur PowerPC (unicorn), avec un Broadband Adapter
# simule. Verifie : etat (GCMI -> GCMA, jeu), lecture de blocs (GCMR -> GCMD, mots non nuls), 2 blocs hors pause, ecriture
# (GCMW), pause et reprise (son coupe puis remis), refus sans l'option, annonce d'un succes (GCAC).
# Usage : emu_mem.py natres.bin natres.elf natres.h
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
        if l.startswith('unsigned char '):                  # (tableaux d'octets : comptes en mots, pour les positions)
            for x in l[14:].rstrip(';').split(','):
                dims = [int(d) for d in re.findall(r'\[(\d+)\]', x)]; t = 1
                for d in dims: t *= d
                names += ['%s_o%d' % (x.split('[')[0].strip(), i) for i in range((t + 3) // 4)]
            continue
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
        if value & 2 and (value >> 2) & 3 == 1:          # (v371 : DMA en ecriture : le direct, par la file d'envoi)
            data = bytes(uc.mem_read(0x80000000 | (exi[0][1] & 0x01FFFFE0), exi[0][2]))
            for k in range(0, len(data), 4): bba.put(int.from_bytes(data[k:k + 4], 'big'), 4)
            exi[0][3] = value & ~1; return
        n = ((value >> 4) & 3) + 1
        if (value >> 2) & 3 == 1: bba.put(exi[0][4], n)
        else: exi[0][4] = bba.get(n)
        exi[0][3] = value & ~1
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
call(POLL)
check('BBA demarre (sans manette du telephone)', padw('state') == 1, 'etat %d' % padw('state'))
bba.deliver(gcm('I', 7)); call(POLL)
o = udp_out()
check('GCMI -> GCMA vers le telephone', len(o) == 1 and o[0][1][:4] == b'GCMA' and o[0][0][0:6] == PH_MAC and o[0][0][30:34] == PH_IP
      and struct.unpack('>H', o[0][0][36:38])[0] == 40001, str([x[1][:4] for x in o]))
if o:
    d = o[0][1]
    check('GCMA : numero, pas en pause, jeu GZLE01', struct.unpack('>II', d[4:12]) == (7, 0) and d[12:18] == b'GZLE01', d[:20].hex())
    v = struct.unpack('>IIIII', d[36:56]) if len(d) >= 56 else (0,) * 5
    check('GCMA : registres video (TFBL, BFBL, PICCONF), 0 valeur bloquee, v419 : morceaux acceptes', len(d) == 60 and struct.unpack('>I', d[56:60])[0] in (96, 256) and v[1] & 0xFFFFFF == 0x00F00000 >> 5 and v[2] & 0xFFFFFF == 0x00F00500 >> 5
          and v[3] == 40 << 8 | 80 and v[4] == 0 and v[0] & 0xFFFF == 0x0105, '%d %s' % (len(d), ['%X' % x for x in v]))
bba.deliver(gcm('R', 8, 0x80400000, 4)); call(POLL)
o = udp_out()
check('GCMR hors pause : 2 blocs seulement', len(o) == 2 and all(x[1][:4] == b'GCMD' for x in o), '%d' % len(o))
good = all(decode(x[1]) == (0x80400000 + 1024 * i, bytes(ram[1024 * i:1024 * (i + 1)])) for i, x in enumerate(o))
check('GCMD : blocs justes (mots nuls omis)', good, '%d octets' % (len(o[0][1]) if o else 0))
# pause : lecture de 4 blocs, ecriture, reprise (tout dans la file avant l'image)
bba.deliver(gcm('P', 9, 1)); bba.deliver(gcm('R', 10, 0x80400000, 4)); bba.deliver(gcm('W', 11, 0x80400010, 2, 0xBEEF))
bba.deliver(gcm('W', 12, 0x817F0000 + 0xF000, 4, 1)); bba.deliver(gcm('P', 13, 0)); call(POLL)
o = udp_out()
k = [x[1][:4] for x in o]
check('pause : GCMA, 4 blocs, GCMA x2, reprise GCMA', k == [b'GCMA'] + [b'GCMD'] * 4 + [b'GCMA'] * 3, str(k))
if len(o) >= 8:
    check('GCMA de la pause : en pause', struct.unpack('>I', o[0][1][8:12])[0] == 1)
    check('GCMA de la reprise : plus en pause', struct.unpack('>I', o[7][1][8:12])[0] == 0)
    check('4 blocs justes en pause', all(decode(o[1 + i][1]) == (0x80400000 + 1024 * i, bytes(ram[1024 * i:1024 * (i + 1)])) for i in range(4)))
check('GCMW : 16 bits ecrits', bytes(mu.mem_read(0x80400010, 2)) == b'\xbe\xef')
check('GCMW dans le resident : refuse', rd32(0x817FF000) == struct.unpack('>I', bytes(mu.mem_read(0x817FF000, 4)))[0] and AT <= 0x817FF000 and bytes(mu.mem_read(0x817FF000, 4)) != b'\x00\x00\x00\x01')
check('son remis apres la pause', bytes(mu.mem_read(0xCC005036, 2)) == b'\x81\x23', bytes(mu.mem_read(0xCC005036, 2)).hex())
check('compteurs : 6 paquets GCM, 6 blocs', padw('mrx') == 7 and padw('mblk') == 6, 'mrx %d mblk %d' % (padw('mrx'), padw('mblk')))
# v307 : valeurs bloquees : 2 bloquees, une liberee, reecrites a chaque image (mfz_run)
bba.deliver(gcm('F', 20, 0x80400020, 1, 0x63)); bba.deliver(gcm('F', 21, 0x80400024, 4, 0x11223344)); call(POLL)
o = udp_out()
check('GCMF : 2 reponses, 2 valeurs bloquees', len(o) == 2 and struct.unpack('>I', o[1][1][52:56])[0] == 2, str([x[1][:4] for x in o]))
mu.mem_write(0x80400020, b'\x01'); mu.mem_write(0x80400024, b'\0\0\0\0'); call(sym['mfz_run'])
check('valeurs reecrites a l image', bytes(mu.mem_read(0x80400020, 1)) == b'\x63' and bytes(mu.mem_read(0x80400024, 4)) == bytes.fromhex('11223344'))
bba.deliver(gcm('F', 22, 0x80400020, 0, 0)); bba.deliver(gcm('F', 23, 0x817F0000, 4, 1)); call(POLL)
o = udp_out()
check('liberer, et refus dans Pulse : 1 valeur bloquee', len(o) == 2 and struct.unpack('>I', o[1][1][52:56])[0] == 1)
mu.mem_write(0x80400020, b'\x01'); call(sym['mfz_run'])
check('valeur liberee : plus reecrite', bytes(mu.mem_read(0x80400020, 1)) == b'\x01')
bba.deliver(gcm('F', 24, 0, 0, 0)); call(POLL); o = udp_out()
check('tout liberer', len(o) == 1 and struct.unpack('>I', o[0][1][52:56])[0] == 0)
# le direct de l'appli : abonnement a 3 morceaux (1608 octets : 2 paquets "GCMS" a chaque image)
def gcms(seq, morceaux, periode=1, opt=0):
    d = b'GCMS' + struct.pack('>IIII', seq, len(morceaux), periode, opt) + b''.join(struct.pack('>II', a, n) for a, n in morceaux)
    udp = struct.pack('>HHHH', 40001, 51237, 8 + len(d), 0) + d
    ip = struct.pack('>BBHHHBBH', 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0) + PH_IP + GC_IP
    return bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]) + PH_MAC + b'\x08\x00' + ip + udp
mx = [(0x80400000, 100), (0x80400400, 1500), (0x80400100, 8)]
attendu = b''.join(bytes(mu.mem_read(a, n)) for a, n in mx)
bba.deliver(gcms(30, mx)); call(POLL)
o = udp_out()
k = [x[1][:4] for x in o]
check('GCMS : abonnement, 2 paquets a l image', k == [b'GCMS'] * 2, str(k))
if len(o) == 2:
    h = [struct.unpack('>IIII', x[1][:16]) for x in o]
    data = o[0][1][16:] + o[1][1][16:]
    check('GCMS : morceaux, taille, donnees justes', h[0][2] == 2 and h[1][2] == (1 << 16 | 2) and h[0][3] == 1608 and data == attendu and len(o[0][1]) == 16 + 1400,
          '%s %d' % ([hex(x[2]) for x in h], len(data)))
call(POLL); o = udp_out()
check('GCMS : encore a l image suivante (sans redemander)', [x[1][:4] for x in o] == [b'GCMS'] * 2)
bba.deliver(gcms(31, [(0x81800000 - 8, 64), (0x80400000, 8)])); call(POLL); o = udp_out()
check('GCMS : morceau hors de la memoire du jeu : refuse (rien d envoye)', len(o) == 0, str([x[1][:4] for x in o]))
bba.deliver(gcms(32, [(0x80400000, 8)], 2))
wr32(H('vi_count'), 10); call(POLL); a1 = len(udp_out()); wr32(H('vi_count'), 11); call(POLL); a2 = len(udp_out())
check('GCMS : periode 2 : une image sur deux', a1 + a2 == 1, '%d %d' % (a1, a2))
# v328 : morceaux compacts (matrices 3 x 4 : la 1re telle quelle, puis 2 lignes de rotation x 16384 et la position x 4
# moins celle de la 1re, en 16 bits) ; images paires / impaires seulement
import math
mats = []
for j in range(3):
    c, s_ = math.cos(0.3 * j + 0.1), math.sin(0.3 * j + 0.1)
    mats.append([c, -s_, 0.0, 1234.5 + 10 * j, s_, c, 0.0, -876.25 - 3 * j, 0.0, 0.0, 1.0, 50.0 + 0.5 * j])
mu.mem_write(0x80410000, b''.join(struct.pack('>12f', *m) for m in mats))
bba.deliver(gcms(34, [(0x80400000, 8), (0x80410000, 0x80000000 | 144)])); wr32(H('vi_count'), 20); call(POLL); o = udp_out()
ok = len(o) == 1 and struct.unpack('>I', o[0][1][12:16])[0] == 8 + 48 + 2 * 18 and len(o[0][1]) == 16 + 8 + 84
err = 0.0
if ok:
    d = o[0][1][16 + 8:]
    f0 = struct.unpack('>12f', d[:48])
    ok = list(f0) == [struct.unpack('>f', struct.pack('>f', v))[0] for v in mats[0]]
    for j in (1, 2):
        v = struct.unpack('>9h', d[48 + 18 * (j - 1):48 + 18 * j])
        rot = [x / 16384 for x in v[:6]]; tr = [x / 4 for x in v[6:]]
        att = [mats[j][q] for q in (0, 1, 2, 4, 5, 6)]
        err = max([err] + [abs(rot[q] - att[q]) for q in range(6)] + [abs(tr[r] - (mats[j][4 * r + 3] - mats[0][4 * r + 3])) for r in range(3)])
check('GCMS v328 : compact (48 + 2 x 18 octets, valeurs justes a 0,25 pres)', ok and err <= 0.26, 'err %.4f, %s' % (err, [len(x[1]) for x in o]))
bba.deliver(gcms(35, [(0x80400000, 8), (0x80410000, 0x40000000 | 48), (0x80410030, 0x20000000 | 48)]))
wr32(H('vi_count'), 40); call(POLL); p0 = [struct.unpack('>I', x[1][12:16])[0] for x in udp_out()]
wr32(H('vi_count'), 41); call(POLL); p1 = [x[1][16 + 8:16 + 8 + 48] for x in udp_out()]
check('GCMS v328 : paires / impaires', p0 == [56] and p1 == [bytes(mu.mem_read(0x80410030, 48))], '%s %d' % (p0, len(p1)))
# (build 911 : 16 Ko au plus (avant 10 Ko) : sur 4 morceaux de 4,5 Ko, 3 gardes, 13 824 octets en 10 paquets)
bba.deliver(gcms(36, [(0x80400000, 4608), (0x80410000, 4608), (0x80420000, 4608), (0x80430000, 4608)])); wr32(H('vi_count'), 50); call(POLL); o = udp_out()
check('GCMS build 911 : 16 Ko au plus (3 morceaux de 4,5 Ko gardes)', len(o) == 10 and struct.unpack('>I', o[0][1][12:16])[0] == 13824, str(len(o)))
# v408 : copies de toute l'image (options bits 1-2) et images depuis le dernier envoi (bits 24-31 de la taille)
bba.deliver(gcms(37, [(0x80400000, 8)], 1, 2 << 1)); wr32(H('vi_count'), 60); call(POLL); udp_out()
wr32(H('vi_count'), 61); call(POLL); o = udp_out()
h = [struct.unpack('>IIII', x[1][:16]) for x in o]
check('GCMS v408 : 3 exemplaires de l image (2 en plus), identiques', len(o) == 3 and len(set(x[1] for x in o)) == 1, str(len(o)))
check('GCMS v408 : taille 8, 1 image depuis le dernier envoi', bool(h) and h[0][3] & 0xFFFFFF == 8 and h[0][3] >> 24 == 1, str([hex(x[3]) for x in h]))
wr32(H('vi_count'), 64); call(POLL); o = udp_out()
check('GCMS v408 : 3 images depuis le dernier envoi', bool(o) and struct.unpack('>I', o[0][1][12:16])[0] >> 24 == 3)
# v409 / v410 : anneau (option bit 3) : l'image N repart pendant N+2, N+7, N+15 et N+31 (cases a la taille de l'image)
RING = blk + 4 + len(PN) * 4
wr32(blk + 4 + PN.index('msr') * 4, len(PN) * 4)
mu.mem_write(RING, bytes(16384))
def anneau(morceaux, v0, nimg):
    bba.deliver(gcms(38 + v0, morceaux, 1, 8 | (1 << 1))); udp_out()
    vu = {}
    for v in range(v0, v0 + nimg):
        wr32(H('vi_count'), v); wr32(0x80400000, v * 7 + 1)     # (valeur differente a chaque image)
        call(POLL)
        for x in udp_out():
            img, pp, tt = struct.unpack('>III', x[1][4:16])
            vu.setdefault(v, []).append((img, x[1]))
    return vu
vu = anneau([(0x80400000, 8)], 1000, 40)
ok_r = True; det = ''
for v in range(1000, 1040):
    att = sorted([v, v] + [v - k for k in (2, 7, 15, 31) if v - k >= 1000])
    eu = sorted(i for i, _ in vu.get(v, []))
    if eu != att: ok_r = False; det = '%d : %s au lieu de %s' % (v, eu, att); break
    for i, pl in vu[v]:
        if struct.unpack('>I', pl[16:20])[0] != i * 7 + 1: ok_r = False; det = 'image %d renvoyee pendant %d : mauvaises donnees' % (i, v); break
check('GCMS v410 : anneau (8 octets) : N, N, N-2, N-7, N-15, N-31, donnees de chaque image justes', ok_r, det)
vu = anneau([(0x80400000, 1380)], 2000, 20)
eu = sorted(i for i, _ in vu[2019])
check('GCMS v416 : reserve sans compression (1380 octets : 11 images) : N-2 et N-7', eu == [2012, 2017, 2019, 2019], str(eu))
vu = anneau([(0x80400000, 2000)], 3000, 6)
check('GCMS v413 : image de 2 paquets : anneau aussi (8 cases : N-2)', sorted(i for i, _ in vu[3005]) == [3003, 3003] + [3005] * 4, str(sorted(i for i, _ in vu[3005])))
# v412 : bus pris par le jeu (carte memoire du canal 0) pendant 2 images : rien n'est envoye, mais les images sont gardees
# dans l'anneau et repartent 2 images plus tard (avant : perdues)
bba.deliver(gcms(60, [(0x80400000, 8)], 1, 8 | (1 << 1))); udp_out()
vu = {}
for v in range(4000, 4020):
    wr32(H('vi_count'), v); wr32(0x80400000, v * 7 + 1)
    occ = v in (4010, 4011)
    if occ: exi[0][0] = 0x80                                  # (le jeu a choisi la carte memoire : CS0)
    call(POLL)
    if occ: exi[0][0] = 0
    for x in udp_out():
        img = struct.unpack('>I', x[1][4:8])[0]
        vu.setdefault(v, []).append((img, struct.unpack('>I', x[1][16:20])[0]))
ok_b = not vu.get(4010) and not vu.get(4011) and (4010, 4010 * 7 + 1) in vu.get(4012, []) and (4011, 4011 * 7 + 1) in vu.get(4012, [])
check('GCMS v413 : bus pris : rien envoye, images gardees, renvoyees au prochain envoi', ok_b, str({k: vu.get(k) for k in (4010, 4011, 4012)}))
# v413 : image de 3 paquets (4096 octets, 3 cases), bus pris 3 images : toutes renvoyees (3 paquets chacune) au prochain envoi
for k in range(0, 4096, 4): wr32(0x80500000 + k, k * 3 + 5)
bba.deliver(gcms(61, [(0x80500000, 4096)], 1, 8 | (1 << 1) | 16)); udp_out()
vu = {}
for v in range(5000, 5012):
    wr32(H('vi_count'), v); wr32(0x80500000, v * 7 + 1)
    occ = v in (5005, 5006, 5007)
    if occ: exi[0][0] = 0x80
    call(POLL)
    if occ: exi[0][0] = 0
    for x in udp_out():
        img, pp = struct.unpack('>II', x[1][4:12])
        vu.setdefault(v, []).append((img, pp >> 16, pp >> 8 & 255, pp & 255))
r8 = vu.get(5008, [])
ok_c = all(not vu.get(v) for v in (5005, 5006, 5007)) and all(sorted(set(q for i, q, _, _ in r8 if i == v)) == [0, 1, 2] for v in (5005, 5006, 5007))
check('GCMS v413 : 4 Ko (3 paquets), bus pris 3 images : toutes renvoyees au prochain envoi', ok_c, str(sorted(set((i, q) for i, q, _, _ in r8))))
check('GCMS v413 : numero d abonnement dans les paquets (option bit 4)', bool(r8) and all(a == 61 and n == 3 for _, _, a, n in r8), str(r8[:3]))
# v416 : compression (option bit 5) : 2 736 octets, image de reference puis changements ; bus pris 8 images : toutes renvoyees,
# et chaque image refaite par le telephone (image de reference + changements, sommes) = la memoire du jeu a cette image
def gc_somme(raw):
    s1 = s2 = 0
    for b in raw: s1 = (s1 + b) & 0xFFFFFFFF; s2 = (s2 + s1) & 0xFFFFFFFF
    return s1, s2
def gc_decode(enc, refs):
    ri, n, s1, s2 = struct.unpack('>IIII', enc[:16])
    if ri not in refs and ri != -1: pass
    body = enc[16:]
    if len(body) == n and ri not in refs: raw = bytes(body)                       # (image de reference)
    else:
        if ri not in refs: return None, ri
        raw = bytearray(refs[ri]); o = 0; pos = 0
        while o < len(body):
            saut, nb = struct.unpack('>HH', body[o:o + 4]); o += 4; pos += saut
            raw[pos:pos + nb] = body[o:o + nb]; o += nb; pos += nb
        raw = bytes(raw)
    return (raw if gc_somme(raw) == (s1, s2) else None), ri
for k in range(0, 2736, 4): wr32(0x80600000 + k, k * 13 + 7)
bba.deliver(gcms(62, [(0x80600000, 2736)], 1, 8 | 16 | 32)); udp_out()
parts = {}; tailles = {}
vu = {}
for v in range(6000, 6040):
    wr32(H('vi_count'), v); wr32(0x80600000, v * 7 + 1); wr32(0x80600400, v * 3)
    occ = 6005 <= v <= 6012
    if occ: exi[0][0] = 0x80
    call(POLL)
    if occ: exi[0][0] = 0
    for x in udp_out():
        img, pp, tt = struct.unpack('>III', x[1][4:16])
        vu.setdefault(v, []).append(img)
        parts.setdefault(img, {})[pp >> 16] = x[1][16:]; tailles[img] = (tt & 0xFFFFFF, pp & 0x7F, bool(pp & 0x80))
refs = {}; ok_z = True; det = ''
for img in sorted(parts):
    tot, np_, code = tailles[img]
    if len(parts[img]) != np_: ok_z = False; det = 'image %d incomplete' % img; break
    enc = b''.join(parts[img][q] for q in range(np_))[:tot]
    if not code: ok_z = False; det = 'image %d pas codee' % img; break
    raw, ri = gc_decode(enc, refs)
    if raw is None: ok_z = False; det = 'image %d : pas refaite (reference %d)' % (img, ri); break
    if ri == img: refs[img] = raw
    want = bytearray(struct.pack('>I', 0) * 0) 
    if struct.unpack('>I', raw[0:4])[0] != img * 7 + 1 or struct.unpack('>I', raw[0x400:0x404])[0] != img * 3 or struct.unpack('>I', raw[8:12])[0] != 8 * 13 + 7:
        ok_z = False; det = 'image %d : mauvaises donnees' % img; break
check('GCMS v416 : compression : chaque image refaite juste (reference + changements, sommes)', ok_z and len(parts) == 40, det or str(len(parts)))
check('GCMS v416 : compression : bus pris 8 images : toutes renvoyees au prochain envoi', all(v not in vu for v in range(6005, 6013)) and all(i in vu.get(6013, []) for i in range(6005, 6013)), str(vu.get(6013)))
petites = [tailles[i][0] for i in sorted(tailles) if i not in refs]
check('GCMS v416 : compression : changements seulement (moins de 40 octets au lieu de 2 736)', bool(petites) and max(petites) < 40 and any(tailles[i][0] == 16 + 2736 for i in refs), str(petites[:5]) + ' ' + str(sorted(refs)))
# v417 : grosse liste compressee (12 000 octets en 3 morceaux de 4 608 au plus : Super Mario Sunshine, Paper Mario) : reference seulement dans
# ref, changements dans le journal ; bus pris 5 images ; chaque image refaite juste
for k in range(0, 12000, 4): wr32(0x80700000 + 0x10000 + k, k * 5 + 3)
bba.deliver(gcms(63, [(0x80710000, 4608), (0x80710000 + 4608, 4608), (0x80710000 + 9216, 2784)], 1, 8 | 16 | 32)); udp_out()
parts = {}; tailles = {}; vu = {}
for v in range(7000, 7070):
    wr32(H('vi_count'), v); wr32(0x80710000, v * 7 + 1); wr32(0x80710000 + 11000, v * 3)
    occ = 7040 <= v <= 7044
    if occ: exi[0][0] = 0x80
    call(POLL)
    if occ: exi[0][0] = 0
    for x in udp_out():
        img, pp, tt = struct.unpack('>III', x[1][4:16])
        vu.setdefault(v, []).append(img)
        parts.setdefault(img, {})[pp >> 16] = x[1][16:]; tailles[img] = (tt & 0xFFFFFF, pp & 0x7F, bool(pp & 0x80))
refs = {}; ok_g = True; det = ''
for img in sorted(parts):
    tot, np_, code = tailles[img]
    if len(parts[img]) != np_ or not code: ok_g = False; det = 'image %d incomplete ou pas codee' % img; break
    enc = b''.join(parts[img][q] for q in range(np_))[:tot]
    raw, ri = gc_decode(enc, refs)
    if raw is None or len(raw) != 12000: ok_g = False; det = 'image %d : pas refaite (reference %d)' % (img, ri); break
    if ri == img: refs[img] = raw
    if struct.unpack('>I', raw[0:4])[0] != img * 7 + 1 or struct.unpack('>I', raw[11000:11004])[0] != img * 3 or struct.unpack('>I', raw[8:12])[0] != 8 * 5 + 3:
        ok_g = False; det = 'image %d : mauvaises donnees' % img; break
check('GCMS v417 : liste de 12 000 octets compressee : toutes les images refaites justes, bus pris 5 images rattrape', ok_g and len(parts) == 70 and all(i in vu.get(7045, []) for i in range(7040, 7045)), det or '%d images, references %s' % (len(parts), sorted(refs)))
# v419 : 200 morceaux (liste dans la structure du telephone), en 2 paquets (option bit 6 : suite) ; reponse GCMA : 256
mx2 = [(0x80720000 + k * 64, 16) for k in range(200)]
for k in range(200): wr32(0x80720000 + k * 64, k * 11 + 1)
bba.deliver(gcms(64, mx2[:150], 1, 8 | 16)); bba.deliver(gcms(65, mx2[150:], 1, 8 | 16 | 64)); udp_out()
wr32(H('vi_count'), 7100); call(POLL)
o2 = udp_out(); ok_l = False; det = ''
for x in o2:
    if x[1][:4] == b'GCMS':
        img, pp, tt = struct.unpack('>III', x[1][4:16])
        ok_l = (tt & 0xFFFFFF) == 3200 and (pp >> 8 & 255) == 65 and all(struct.unpack('>I', x[1][16 + k * 16:20 + k * 16])[0] == k * 11 + 1 for k in range(min(80, (len(x[1]) - 16) // 16)))
        det = 'taille %d, abonnement %d' % (tt & 0xFFFFFF, pp >> 8 & 255); break
check('GCMS v419 : 200 morceaux en 2 paquets (suite de liste) : 3 200 octets, donnees justes', ok_l, det)
bba.deliver(gcmr(66, 'I')) if 'gcmr' in dir() else None
bba.deliver(gcms(33, [])); call(POLL); udp_out()
# succes obtenu : annonce
wr32(blk + 4 + PN.index('mach') * 4, 3)
call(POLL); call(POLL); call(POLL); call(POLL)
o = udp_out()
check('succes : GCAC envoye 3 fois', [x[1][:4] for x in o] == [b'GCAC'] * 3, str([x[1][:4] for x in o]))
# v312 : chercheur de codes seul : le BBA est cache au jeu (Mario Kart Double Dash le cherche, puis le garderait)
def jeu_lit_bba():
    exi[0][0] = 0x200; exi[0][4] = 0x04020200
    n0 = rd32(H('bba_n')); r = call(sym['exi_load'], 0xCC006810)
    exi[0][0] = 0
    return r, rd32(H('bba_n')) - n0
r, dn = jeu_lit_bba()
check('NRO_MEM seul : le jeu lit "aucun peripherique" au lieu du BBA', r == 0 and dn == 1, '%08X %d' % (r, dn))
wr32(H('opt'), 0)
r, dn = jeu_lit_bba()
check('sans option : le jeu voit le vrai BBA', r == 0x04020200 and dn == 0, '%08X %d' % (r, dn))
wr32(H('opt'), NRO_MEM)
# sans l'option : rien
wr32(H('opt'), NRO_PHONE)
bba.deliver(gcm('I', 14)); call(POLL)
check('sans NRO_MEM : pas de reponse', len(udp_out()) == 0)
print('TOUT OK' if ok else 'ECHEC')
