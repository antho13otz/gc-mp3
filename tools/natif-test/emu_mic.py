#!/usr/bin/env python3
# v332 : micro du telephone : le resident Pulse dans l'emulateur PowerPC (unicorn), avec un Broadband Adapter et des
# registres EXI simules. Un faux jeu parle au slot B comme le pilote du micro de Mario Party 7 (identifiant EXI, FF, 40, 80,
# 20 + DMA ; une interruption EXI par paquet pret) ; un faux telephone envoie des echantillons ("GCVO") et lit l'etat
# renvoye. On verifie l'identifiant, l'etat, les interruptions, les paquets lus (octets identiques, dans l'ordre), le
# silence quand le telephone est en retard, et que le canal 0 n'est pas touche.
# Usage : emu_mic.py natres.bin natres.elf natres.h
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
        mc = re.match(r'unsigned char (\w+)((?:\[\d+\])+);', l)
        if mc:
            nb = 1
            for d in re.findall(r'\[(\d+)\]', mc.group(2)): nb *= int(d)
            names += ['%s_o%d' % (mc.group(1), i) for i in range(nb // 4)]; continue
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
# (build 915) horloge de Pulse (mftb) pilotee par le test : chaque "mftb rD" devient "lwz rD, 0x100(0)" ; le test ecrit
# le temps (tops de 1/40,5 MHz) a l'adresse 0x100 (clang l'ecrit "mfspr rD, 268"). Avant, l'horloge ne bougeait pas : la cadence des paquets (v353) et la
# reserve de son (v354) ne pouvaient pas etre essayees
img = bytearray(open(BIN, 'rb').read())
for o in range(0, len(img) - 3, 4):
    w = struct.unpack_from('>I', img, o)[0]
    if w & 0xFC1FFFFF in (0x7C0C42E6, 0x7C0C42A6): struct.pack_into('>I', img, o, 0x80000100 | (w & 0x03E00000))   # (mftb / mfspr 268)
mu.mem_map(0, 0x1000)
mu.mem_write(AT, bytes(img))
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
P('net.9', 0xFFFFFF00); P('chmask', 0); P('gbmask', 0); P('xch', 0xFF); P('micon', 1)
for i in range(4): P('h%d' % i, 0x00808080); P('l%d' % i, 0x80800000)
words = [0] + pad
for i, w in enumerate(words): wr32(blk + i * 4, w)
wr32(H('chts'), blk)
padw = lambda n: rd32(blk + 4 + PN.index(n) * 4)
XL, XS, POLL = rd32(H('exi_load')), rd32(H('exi_store')), sym['pd_poll']
ok = True
def check(what, cond, extra=''):
    global ok
    print(('OK   ' if cond else 'FAUX ') + what + (' ' + extra if extra else ''))
    ok = ok and cond
B = 0xCC006814                                           # registres du canal 1 (slot B)
MASKS = 0x1 | 0x4                                        # (EXIINTMASK, TCINTMASK : comme le SDK avec ses fonctions)
def sel(): call(XS, B, MASKS | 4 << 4 | 0x80)            # (peripherique 0, 16 MHz)
def desel(): call(XS, B, MASKS)
def imm_w(data, n): call(XS, B + 0x10, data); call(XS, B + 0x0C, (n - 1) << 4 | 1 << 2 | 1)
def imm_r(n): call(XS, B + 0x0C, (n - 1) << 4 | 1); return call(XL, B + 0x10)
def csr(): return call(XL, B)
def cmd(b, rd=0, wr=None):
    sel(); imm_w(b << 24, 1)
    if wr is not None: imm_w(wr << 16, 2)
    v = imm_r(rd) if rd else None
    desel(); return v
BUF = 0x80600000
def packet(n):                                           # interruption : etat, puis 20 et le paquet par DMA
    call(XS, B, MASKS | 2)                                # (le SDK acquitte l'interruption EXI)
    st = cmd(0x40, 2) >> 16
    mu.mem_write(BUF, b'\xEE' * n)
    sel(); imm_w(0x20 << 24, 1)
    call(XS, B + 0x04, BUF); call(XS, B + 0x08, n); call(XS, B + 0x0C, 2 | 1)
    tc = csr() & 8
    call(XS, B, MASKS | 8)                                # (fin du DMA acquittee)
    desel()
    return st, bytes(mu.mem_read(BUF, n)), tc
# identifiant (EXIGetID : 2 octets 00, puis 4 lus)
sel(); imm_w(0, 2); idv = imm_r(4); desel()
check('identifiant du slot B : micro (0x0A000000)', idv == 0x0A000000, '%08X' % idv)
check('slot B present (EXT)', csr() & 0x1000 != 0, '%08X' % csr())
cmd(0xFF)
check('remise a zero : etat 0', cmd(0x40, 2) >> 16 == 0)
cmd(0x80, wr=0xA800)                                     # en marche, paquets de 64 octets, 22050 Hz
check('etat ecrit puis relu (en marche, 64 octets, 22050 Hz)', cmd(0x40, 2) >> 16 == 0xA800 and padw('mstat') == 0xA800)
check('pas d\'interruption sans echantillons', not (csr() & 2))
call(POLL)
check('BBA demarre', padw('state') == 1, 'etat %d' % padw('state'))
def gcvo(seq, data):
    d = b'GCVO' + struct.pack('>II', seq, len(data)) + data
    udp = struct.pack('>HHHH', 40002, 51237, 8 + len(d), 0) + d
    ip = struct.pack('>BBHHHBBH', 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0) + PH_IP + GC_IP
    return bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]) + PH_MAC + b'\x08\x00' + ip + udp
# comportement : un paquet a la cadence de la frequence des la mise en marche (v353) ; v387 : sans son du telephone, un
# souffle tres faible (pas des zeros parfaits : Mario Party 7 jugeait le micro mort), et le son du telephone des qu'il
# arrive (pas de reserve a la mise en marche) ; un trou ensuite : du souffle et la reserve de ~93 ms (4096 octets a
# 22050 Hz) se refait (v354) ; plus de 4 reserves d'avance : on rattrape (latence bornee)
def souffle(b):
    ech = [struct.unpack('>h', b[i:i + 2])[0] for i in range(0, len(b) - 1, 2)]
    return len(b) % 2 == 0 and all(-255 <= e <= 255 for e in ech) and any(ech)
T = [1000]
def clock(dt=0): T[0] += dt; wr32(0x100, T[0])
clock()
PK = 64 * (40500000 // 22050)                            # (tops par paquet de 64 octets a 22050 Hz : ~2,9 ms)
cmd(0x80, wr=0x0000); cmd(0x80, wr=0xA800)              # (remise en marche : l'horloge du test compte des maintenant)
call(POLL)
check('mis en marche : 1er paquet annonce tout de suite (meme sans son)', csr() & 2 != 0, 'CSR %08X' % csr())
st, data, tc = packet(64)
check('rien du telephone : un paquet de souffle faible (pas des zeros)', souffle(data) and st == 0xA800 and tc != 0)
call(POLL)
check('pas de paquet avant son heure', not (csr() & 2), 'CSR %08X' % csr())
clock(PK); call(POLL)
check('paquet suivant a la cadence (64 octets a 22050 Hz)', csr() & 2 != 0, 'CSR %08X' % csr())
son = bytes((i * 7 + 3) & 0xFF for i in range(5000))
bba.deliver(gcvo(1, son[:1000])); call(POLL)
check('paquet du telephone recu (1000 octets dans l\'anneau)', padw('mnrx') == 1 and padw('mw') - padw('mr') == 1000, '%d %d' % (padw('mnrx'), padw('mw') - padw('mr')))
st, data, tc = packet(64)
check('mise en marche : le son du telephone tout de suite (pas de reserve)', data == son[:64] and padw('mw') - padw('mr') == 936, data[:8].hex())
for k in range(1, 5): bba.deliver(gcvo(1 + k, son[k * 1000:(k + 1) * 1000])); call(POLL)
lus = b''
for i in range(4):
    clock(PK); call(POLL)
    st, data, tc = packet(64); lus += data
check('la suite du son du telephone, octets identiques et dans l\'ordre', lus == son[64:320], lus[:8].hex())
reste = padw('mw') - padw('mr')
for i in range(reste // 64):
    clock(PK); call(POLL); packet(64)
clock(PK); call(POLL)
low0, ref0 = padw('mlow'), padw('mnrefill')
st, data, tc = packet(64)
check('telephone en retard : souffle, la reserve se refait', souffle(data[reste % 64:]) and padw('mlow') > low0 and padw('mnrefill') == ref0 + 1,
      'mlow %d refill %d' % (padw('mlow'), padw('mnrefill')))
for k in range(6): bba.deliver(gcvo(10 + k, bytes([0x55]) * 1400)); call(POLL)
clock(PK); call(POLL); packet(64)
check('trop d\'avance (8400 octets) : rattrape, il reste 2 reserves au plus', padw('mw') - padw('mr') <= 2 * 4096, '%d' % (padw('mw') - padw('mr')))
for _ in range(60):                                      # (paquets deja dus : lus d'abord, jusqu'a etre a l'heure)
    if not csr() & 2: break
    packet(64)
clock(150 * 40500000 // 1000); call(POLL)                # (150 ms sans lecture)
check('retard de plus de 100 ms : un seul paquet annonce (pas de rafale)', csr() & 2 != 0)
packet(64); call(POLL)
check('... et pas un 2e tout de suite', not (csr() & 2), 'CSR %08X' % csr())
# etat renvoye au telephone (toutes les 10 images)
bba.sent.clear()
for _ in range(10): call(POLL)
etat = [f for f in bba.sent if len(f) >= 42 + 40 and f[42:46] == b'GCVO' and f[36:38] == struct.pack('>H', 40002)]
ok_e = bool(etat) and struct.unpack('>I', etat[-1][50:54])[0] == 0xA800
check('etat renvoye au telephone ("GCVO" 2 : etat du micro)', ok_e, '%d paquets' % len(etat))
cmd(0x80, wr=0x0000)
check('arret : etat 0, plus d\'interruption', padw('mstat') == 0 and not (csr() & 2))
bba.deliver(gcvo(2, son)); call(POLL)
check('arrete : les echantillons n\'annoncent rien', not (csr() & 2))
# (v387b : l'appli 477 envoie son son micro eteint) remise en marche : les 1024 octets les plus recents, tout de suite
for k in range(5): bba.deliver(gcvo(30 + k, son[k * 1000:(k + 1) * 1000])); call(POLL)
cmd(0x80, wr=0xA800); clock(PK); call(POLL)
st, data, tc = packet(64)
check('mise en marche : les ~46 ms les plus recentes du telephone, tout de suite', data == son[-1024:-960], data[:8].hex())
cmd(0x80, wr=0x0000)
# canal 0 : vrai materiel, pas touche
exi[0][4] = 0x12345678
check('canal 0 (slot A) : vrai materiel', call(XL, 0xCC006810) == 0x12345678)
print('TOUT OK' if ok else 'ECHEC')
