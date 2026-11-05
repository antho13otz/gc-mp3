#!/usr/bin/env python3
# v331 : GBA du telephone : le resident Pulse dans l'emulateur PowerPC (unicorn), avec un Broadband Adapter et des
# registres SI simules. Un faux jeu fait le demarrage d'une GBA comme le SDK (format public : GBATEK, cle "Kawa" /
# "sedo", chiffrement et CRC du demarrage par le cable) : remise a zero, cle, taille, programme chiffre, CRC ; un faux
# telephone recoit les mots ("GCGB"), les accuse, dechiffre le programme et verifie qu'il est identique. Puis relais.
# Usage : emu_gba.py natres.bin natres.elf natres.h
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
P('net.9', 0xFFFFFF00); P('chmask', 0); P('gbmask', 0x2); P('xch', 0xFF)
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
KAWA, SEDO = 0x6177614B, 0x6F646573
def tr(cmd, data=b''):                                   # transfert du jeu vers le port 2 (canal 1) : rend les mots de reponse
    out = bytes([cmd]) + data
    out = out + bytes(8 - len(out))
    call(SS, 0xCC006480, struct.unpack('>I', out[0:4])[0]); call(SS, 0xCC006484, struct.unpack('>I', out[4:8])[0])
    call(SS, 0xCC006434, 0x80000000 | (len(data) + 1) << 16 | 5 << 8 | (1 << 1) | 1)
    return call(SL, 0xCC006480), call(SL, 0xCC006484)
def joystat(): return (tr(0x00)[0] >> 8) & 0xFF
def rd():                                                # 14 : JOY_TRANS (octet de poids faible d'abord), JOYSTAT
    a, b = tr(0x14)
    return struct.unpack('<I', struct.pack('>I', a))[0], b >> 24
def wr(w):                                               # 15 : JOY_RECV (octet de poids faible d'abord) ; rend JOYSTAT
    a, _ = tr(0x15, struct.pack('<I', w & 0xFFFFFFFF))
    return a >> 24
# SISR : port 2 sans erreur
si[0x438] = 0x08080808
v = call(SL, 0xCC006438)
check('SISR : port 2 (GBA) sans erreur ; port 1 inchange', v == 0x08000808, '%08X' % v)
# type avant toute remise a zero : GBA (0004)
a, _ = tr(0x00)
check('type du port 2 : GBA (0004)', a >> 16 == 0x0004, '%08X' % a)
a, _ = tr(0xFF)
check('remise a zero : 0004, JOYSTAT 0x18 (pret, un mot a lire)', a == 0x00041800, '%08X' % a)
t, js = rd()
key = t ^ SEDO
check('cle de session lue (JOYSTAT avec 0x08 puis efface)', js == 0x18 and joystat() == 0x10, 'JOYSTAT %02X puis %02X' % (js, joystat()))
check('cle = celle du resident', key == padw('gkey'), '%08X / %08X' % (key, padw('gkey')))
# programme de 0x1000 octets (en-tete 0xC0 + donnees), cle B (format inverse de celui de la GBA), chiffrement et CRC
import random
random.seed(7)
L = 0x1000
rom = bytearray(random.getrandbits(8) for _ in range(L)); rom[0xA0:0xAC] = b'TESTPULSEGBA'
field = (L - 0x200) >> 3
s = (field & 0x7F) | ((field >> 7) & 0x7F) << 8 | ((field >> 14) & 1) << 16 | 0x8000 | 0x80
for m in (KAWA, SEDO):
    w = s ^ m
    if bool(w & 0x200) == (m == SEDO): keyb = w; break
js = wr(keyb)
check('cle B : taille du programme (JOYSTAT 0x20)', padw('gst') == 2 and padw('glen') == L + 4 and joystat() == 0x20, 'etat %d taille %X JOYSTAT %02X' % (padw('gst'), padw('glen'), joystat()))
def crc_add(crc, d):
    for _ in range(32):
        crc = (crc >> 1) ^ 0xA1C1 if (crc ^ d) & 1 else crc >> 1
        d >>= 1
    return crc
plain = [struct.unpack_from('<I', rom, i)[0] for i in range(0, L, 4)]
k = key; crc = 0x15A0; cipher = []
for i, p in enumerate(plain):
    a = i * 4
    if a < 0xC0: cipher.append(p); continue
    crc = crc_add(crc, p)
    k = (k * KAWA + 1) & 0xFFFFFFFF
    cipher.append(p ^ k ^ ((-(0x02000000 + a)) & 0xFFFFFFFF) ^ 0x20796220)
k = (k * KAWA + 1) & 0xFFFFFFFF
cipher.append(((crc | L << 16) & 0xFFFFFFFF) ^ k ^ ((-(0x02000000 + L)) & 0xFFFFFFFF) ^ 0x20796220)
# faux telephone : il dit bonjour, puis accuse ce qu'il recoit
call(POLL)
check('BBA demarre', padw('state') == 1, 'etat %d' % padw('state'))
def gcgb(seq, ack, trans=0, opt=0):
    d = b'GCGB' + bytes([1, 1, 0, 0]) + struct.pack('>IIII', seq, ack, trans, opt)
    udp = struct.pack('>HHHH', 40001, 51237, 8 + len(d), 0) + d
    ip = struct.pack('>BBHHHBBH', 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0) + PH_IP + GC_IP
    return bytes([0x00, 0x09, 0xBF, 0x01, 0x02, 0x03]) + PH_MAC + b'\x08\x00' + ip + udp
seq = [1]
recus = {}
def phone(ack_all=True):
    for f in bba.sent:
        if len(f) > 42 + 40 and f[42:46] == b'GCGB' and f[36:38] == struct.pack('>H', 40001):
            d = f[42:]
            first, n = struct.unpack('>II', d[20:28])
            for i in range(n): recus[first + i] = struct.unpack('>I', d[40 + 4 * i:44 + 4 * i])[0]
    bba.sent.clear()
    nxt = 0
    while nxt in recus: nxt += 1
    seq[0] += 1
    bba.deliver(gcgb(seq[0], nxt if ack_all else 0)); call(POLL)
    return nxt
bba.deliver(gcgb(1, 0)); call(POLL)
check('bonjour du telephone : adresse retenue', padw('gip') == struct.unpack('>I', PH_IP)[0] and padw('gport') == 40001)
# envoi des mots : le jeu attend la bascule du bit 0x10 apres chaque mot (sinon : il interroge et le telephone accuse)
bloque = 0; attente_max = 0
for i, c in enumerate(cipher):
    avant = joystat()
    wr(c)
    n = 0
    while joystat() == avant and padw('gst') == 2:
        n += 1; bloque += 1 if n == 1 else 0
        phone()
        if n > 50: break
    attente_max = max(attente_max, n)
    if n > 50: break
check('tous les mots acceptes (bascule 0x10, le tampon plein a fait attendre le jeu %d fois)' % bloque, i == len(cipher) - 1 and bloque > 0 and attente_max <= 2, 'mot %d / %d, attente max %d' % (i + 1, len(cipher), attente_max))
check('programme recu : etat 3, JOYSTAT 0x08', padw('gst') == 3 and joystat() == 0x08 and padw('gpos') == L + 4, 'etat %d JOYSTAT %02X octets %X' % (padw('gst'), joystat(), padw('gpos')))
for _ in range(4): phone()
check('le telephone a tous les mots (cle B + programme + CRC)', len(recus) == len(cipher) + 1 and recus[0] == keyb, '%d / %d' % (len(recus), len(cipher) + 1))
# le telephone dechiffre (comme la GBA) et verifie le CRC
got = [recus[i + 1] for i in range(len(cipher))]
kk = key; dec = []; crc = 0x15A0
for i, c in enumerate(got):
    a = i * 4
    if a < 0xC0: dec.append(c); continue
    kk = (kk * KAWA + 1) & 0xFFFFFFFF
    p = c ^ kk ^ ((-(0x02000000 + a)) & 0xFFFFFFFF) ^ 0x20796220
    dec.append(p)
    if a < L: crc = crc_add(crc, p)
img = b''.join(struct.pack('<I', w) for w in dec[:L // 4])
check('programme dechiffre identique, CRC juste, titre lu', img == bytes(rom) and dec[L // 4] == (crc | L << 16) and img[0xA0:0xAC] == b'TESTPULSEGBA')
t, js = rd()
check('1re lecture apres le programme : programme lance (relais)', padw('gst') == 4, 'etat %d' % padw('gst'))
# relais : le telephone donne un mot, le jeu le lit ; le jeu ecrit, le telephone lit
seq[0] += 1; bba.deliver(gcgb(seq[0], padw('gw'), 0xCAFEBABE, 1 | 8 | 0x30 << 8)); call(POLL)
check('relais : JOYSTAT avec un mot a lire et les drapeaux du telephone', joystat() == 0x38, '%02X' % joystat())
t, js = rd()
check('relais : le jeu lit le mot du telephone', t == 0xCAFEBABE and js == 0x38 and joystat() == 0x30, '%08X %02X' % (t, js))
js = wr(0x12345678)
check('relais : le jeu ecrit, drapeau 0x02 jusqu\'a la lecture du telephone', js & 0x02 and joystat() & 0x02)
phone(); phone()                                         # (1er : accuse ; 2e : lit le paquet envoye ensuite)
check('relais : le mot ecrit arrive au telephone', recus.get(padw('gw') - 1) == 0x12345678)
seq[0] += 1; bba.deliver(gcgb(seq[0], padw('gw'), 0, 2)); call(POLL)
check('relais : lu par le telephone, drapeau 0x02 efface', not (joystat() & 0x02), '%02X' % joystat())
# v333 : JOY_TRANS numerote : un paquet repete ne le redonne pas au jeu ; le telephone apprend qu'il a ete lu
seq[0] += 1; bba.deliver(gcgb(seq[0], padw('gw'), 0x11223344, 1 | 5 << 16)); call(POLL)
t, js = rd()
check('relais : JOY_TRANS numero 5 lu par le jeu', t == 0x11223344 and js & 0x08 and not (joystat() & 0x08), '%08X %02X' % (t, js))
seq[0] += 1; bba.deliver(gcgb(seq[0], padw('gw'), 0x11223344, 1 | 5 << 16)); call(POLL)
check('relais : le meme numero repete (paquet en double) : pas redonne', not (joystat() & 0x08), '%02X' % joystat())
bba.sent.clear()
for _ in range(16): call(POLL)
lu5 = [f for f in bba.sent if len(f) >= 42 + 40 and f[42:46] == b'GCGB']
check('etat renvoye : dernier numero lu par le jeu = 5', bool(lu5) and lu5[-1][42 + 5] == 5, '%d paquets' % len(lu5))
a, _ = tr(0xFF)
check('remise a zero pendant le relais : nouveau demarrage (nouvelle cle)', padw('gst') == 1 and a == 0x00041800, 'etat %d %08X' % (padw('gst'), a))
# port 1 jamais touche
call(SS, 0xCC006480, 0x00000000); call(SS, 0xCC006434, (0 << 1) | 1); si[0x480] = 0x12345678
check('transfert vers le port 1 : pas touche', call(SL, 0xCC006480) == 0x12345678)
print('TOUT OK' if ok else 'ECHEC')
