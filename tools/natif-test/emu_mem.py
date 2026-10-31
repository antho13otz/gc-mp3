#!/usr/bin/env python3
# v306 : chercheur de codes de l'appli : le resident Pulse dans l'emulateur PowerPC (unicorn), avec un Broadband Adapter
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
    check('GCMA : registres video (TFBL, BFBL, PICCONF), 0 valeur bloquee', len(d) == 56 and v[1] & 0xFFFFFF == 0x00F00000 >> 5 and v[2] & 0xFFFFFF == 0x00F00500 >> 5
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
# succes obtenu : annonce
wr32(blk + 4 + PN.index('mach') * 4, 3)
call(POLL); call(POLL); call(POLL); call(POLL)
o = udp_out()
check('succes : GCAC envoye 3 fois', [x[1][:4] for x in o] == [b'GCAC'] * 3, str([x[1][:4] for x in o]))
# sans l'option : rien
wr32(H('opt'), NRO_PHONE)
bba.deliver(gcm('I', 14)); call(POLL)
check('sans NRO_MEM : pas de reponse', len(udp_out()) == 0)
print('TOUT OK' if ok else 'ECHEC')
