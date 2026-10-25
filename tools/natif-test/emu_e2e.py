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

def crc16(b):                                          # v94 : CRC des blocs (CCITT), verifie a 32 MHz
    c = 0
    for x in b:
        c ^= x << 8
        for _ in range(8): c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c
BAD32 = os.environ.get('EMU_BAD32') == '1'             # (32 MHz qui abime les donnees : le resident doit repasser a 16 MHz)
NODMA = os.environ.get('EMU_NODMA') == '1'             # (adaptateur sans DMA : le resident doit le voir)
fast = [False]
def blk(sec):
    d = bytes(disk[sec * 512:(sec + 1) * 512]); c = crc16(d)
    if BAD32 and fast[0]: d = bytes([d[0] ^ 1]) + d[1:]
    return list(d) + [c >> 8, c & 0xFF]
class Card:
    def __init__(s): s.cmd = []; s.out = []; s.idle = True; s.app = False; s.acmd41 = 0; s.log = []; s.wr = None; s.wbuf = None; s.writes = []; s.multi = None
    def deselect(s): s.cmd = []; s.out = []; s.wr = None; s.wbuf = None; s.multi = None
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
    def xfer_out(s):
        if not s.out and s.multi is not None:          # v88 : CMD18 : les secteurs suivants, jusqu'a CMD12
            sec = s.multi; s.multi += 1
            s.out = [0xFF, 0xFE] + blk(sec)
        return s.out.pop(0) if s.out else 0xFF
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
        if c == 18: s.multi = arg; s.out = r + [0]; return
        if c == 12: s.multi = None; s.out = [0xFF, 0x00, 0x00, 0x00, 0xFF]; return   # (octet de bourrage, R1, occupe, pret)
        if c == 17:
            sec = arg
            s.out = r + [0, 0xFF, 0xFE] + blk(sec); return
        s.out = r + [0x04]
card = Card()
exi = [[0] * 5 for _ in range(3)]
ai = [0] * 8                                          # v65 : registres AI (0xCC006C00)
dspr = {}                                             # v65 : registres DSP / DMA du son (0xCC005000, 16 bits)
bad0 = []                                             # v65 : la vraie carte du slot A ne doit jamais etre choisie
di_real = [0] * 10
kicks = []

si_inbuf = [0x80000000, 0x11900000, 0x80000000, 0x80000000]   # manette 1 : A + START + Z
if os.environ.get('EMU_SISTALE'):                     # v99 : ports sans manette pas interroges par le jeu : 0 (pas le bit d'erreur)
    si_inbuf = [0, 0x11900000, 0, 0]
si_sr = [0x00200000, 0]                               # (v58 : manette 1 avec RDST les 20 premieres fois : ne doit pas etre lue)
si_inl = [0, 0x20E0C010, 0, 0]                          # v122 : stick C (20, E0), gachettes L C0, R 10 (manette 2)
si_out = [0, 0, 0, 0]; si_poll = []                    # v122 : OUTBUF (mode des manettes), ecritures de SIPOLL
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
    if 0x400 <= off < 0x430 and (off - 0x400) % 12 == 8: return si_inl[(off - 0x400) // 12]
    if 0x400 <= off < 0x430 and (off - 0x400) % 12 == 0: return si_out[(off - 0x400) // 12]
    if off == 0x438:                                    # SISR : RDST (donnees neuves pas encore prises)
        si_sr[1] += 1
        return si_sr[0] if si_sr[1] <= 20 or si_sr[1] % 3 == 0 else 0   # (v93 : ensuite, donnees neuves une fois sur 3, comme un vrai jeu)
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
    if 0x400 <= off < 0x430 and (off - 0x400) % 12 == 0: si_out[(off - 0x400) // 12] = value; return
    if off == 0x430: si_poll.append(value); return
    if off < 0x800: return
    o = off - 0x800; ch, reg = o // 0x14, (o % 0x14) // 4
    if ch >= 3: return
    if ch == 0 and reg == 0 and value & 0x80: bad0.append(('CSR', value))
    if ch == 0 and reg == 3 and value & 1 and exi[0][0] & 0x80: bad0.append(('CR', value))
    if MODE == 'cardb' and ch == 1 and reg == 0 and value & 0x80: bad0.append(('CSR1', value))   # (v122 : vraie carte du slot B jamais choisie)
    exi[ch][reg] = value
    if reg == 0 and ch == CHAN and not (value & (0x80 << DEV)): card.deselect()
    if reg == 3 and (value & 1):
        n = ((value >> 4) & 3) + 1; mode = (value >> 2) & 3
        sel = ch == CHAN and (exi[ch][0] & (0x80 << DEV))
        fast[0] = ((exi[ch][0] >> 4) & 7) >= 5
        if value & 2:                          # v94 : DMA (lecture)
            ln = exi[ch][2]; data = bytes((card.xfer_out() if sel else 0xFF) ^ (0x5A if NODMA else 0) for _ in range(ln))
            uc.mem_write(0x80000000 | exi[ch][1], data); exi[ch][3] = value & ~1; return
        if mode == 1:
            for k in range(n):
                if sel: card.xfer_in((exi[ch][4] >> (24 - 8 * k)) & 0xFF)
        elif mode == 0:
            v = 0
            for k in range(n): v |= (card.xfer_out() if sel else 0xFF) << (24 - 8 * k)
            exi[ch][4] = v
        exi[ch][3] = value & ~1

MODE = sys.argv[4] if len(sys.argv) > 4 else ''
TRAP = MODE == 'trap'                                 # v139 : acces pris par exception (option du jeu)
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
if TRAP:                                              # (memoire aussi en adresses physiques : les exceptions demarrent sans traduction)
    import ctypes
    ram = ctypes.create_string_buffer(0x01800000)
    mu.mem_map_ptr(0x80000000, 0x01800000, UC_PROT_ALL, ctypes.addressof(ram))
    mu.mem_map_ptr(0x00000000, 0x01800000, UC_PROT_ALL, ctypes.addressof(ram))
    mu.mmio_map(0xCC406000, 0x1000, io_read, None, io_write, None)   # (2e adresse de la page : celle du resident)
else: mu.mem_map(0x80000000, 0x01800000)
mu.mmio_map(0xCC006000, 0x1000, io_read, None, io_write, None)
mu.mmio_map(0xCC003000, 0x1000, pi_read, None, pi_write, None)
mu.mmio_map(0xCC000000, 0x1000, pi_read, None, lambda *a: None, None)      # (CP : ecritures ignorees)
def dsp_write(uc, off, size, value, data):
    if size == 2: dspr[off] = value & 0xFFFF
    elif size == 4: dspr[off] = (value >> 16) & 0xFFFF; dspr[off + 2] = value & 0xFFFF
def dsp_read(uc, off, size, data):
    return dspr.get(off, 0) if size == 2 else (dspr.get(off, 0) << 16) | dspr.get(off + 2, 0)
mu.mmio_map(0xCC005000, 0x1000, dsp_read, None, dsp_write, None)          # (DSP / DMA du son)
vir = {0x02: 0x0001, 0x6C: 0, 0x6E: 1}                # v99 : ecran (VI) : DCR actif NTSC, 27 MHz, cable numerique
mu.mmio_map(0xCC002000, 0x1000, lambda uc, off, size, data: vir.get(off, 0) if size == 2 else (vir.get(off, 0) << 16) | vir.get(off + 2, 0), None, lambda *a: None, None)
b = open(BOOT, 'rb').read()
entry, n = struct.unpack_from('>II', b, 0); p = 8
for i in range(n):
    dst, sz = struct.unpack_from('>II', b, p); p += 8
    mu.mem_write(dst, b[p:p + sz]); p += sz
    print('copie %08X %d' % (dst, sz))
mu.mem_write(0x80000000, b[p:p + 0x20])
EXC = len(sys.argv) > 4 and sys.argv[4] == 'exc'          # v60 : faux plantage
if EXC: mu.mem_write(0x80001F40, struct.pack('>I', 1))
EXEC = len(sys.argv) > 4 and sys.argv[4] in ('exec', 'execa')   # v95 : OSExec (ancien / recent apploader)
EXECA = EXEC and sys.argv[4] == 'execa'
if EXEC: mu.mem_write(0x80001F44, struct.pack('>I', 2 if EXECA else 1))
if MODE in ('swap', 'cheat', 'ws', 'cardb', 'pad', 'tick', 'trap'): mu.mem_write(0x80001F44, struct.pack('>I', {'swap': 3, 'cheat': 4, 'ws': 5, 'cardb': 6, 'pad': 7, 'tick': 8, 'trap': 9}[MODE]))   # v121, v122, v133, v139
STOP = 0x80001000
mu.mem_write(STOP, struct.pack('>I', 0x48000000))
mu.reg_write(UC_PPC_REG_1, 0x81000000); mu.reg_write(UC_PPC_REG_LR, STOP)
mu.reg_write(UC_PPC_REG_31, 0xDEAD); mu.reg_write(UC_PPC_REG_30, 0xBEEF)
if MODE == 'ws': mu.reg_write(UC_PPC_REG_MSR, mu.reg_read(UC_PPC_REG_MSR) | 0x2000)   # (v121 : virgule flottante permise, comme dans un jeu)
# v139 : quand le resident active l'option (trap_state = 1), la page 0xCC006000 n'est plus traduite : chaque
# acces du jeu provoque une exception DSI (SRR0, SRR1, DAR, DSISR, vecteur 0x300 sans traduction), comme sur la console
TRAP_ST = 0x817F01C0; armed = [False, False, False]; fault = []; ntrap = [0]
if TRAP:
    def on_arm(uc, access, addr, size, value, data):
        if addr == TRAP_ST and value == 1 and not armed[0]: armed[0] = True; armed[1] = True; uc.emu_stop()
    mu.hook_add(UC_HOOK_MEM_WRITE, on_arm, begin=TRAP_ST, end=TRAP_ST + 3)
    def on_vec(uc, access, addr, size, value, data):     # (vecteur du jeu remis : option coupee, la page redevient normale)
        if armed[0] and value >> 26 != 18: armed[0] = False; armed[2] = True; uc.emu_stop()
    mu.hook_add(UC_HOOK_MEM_WRITE, on_vec, begin=0x80000300, end=0x80000303)
    def on_unmapped(uc, access, addr, size, value, data):
        if armed[0] and 0xCC006000 <= addr < 0xCC007000: fault.append((addr, access == UC_MEM_WRITE_UNMAPPED))
        return False
    mu.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)
    # (injecteur : SRR0 SRR1 DAR DSISR depuis r3-r6, puis r3-r6 du jeu, puis le vecteur)
    mts = lambda spr, rs: 0x7C0003A6 | rs << 21 | (spr & 0x1F) << 16 | (spr >> 5) << 11
    inj = [mts(26, 3), mts(27, 4), mts(19, 5), mts(18, 6)] + [0x80000000 | r << 21 | (0x7F00 + (r - 3) * 4) for r in (3, 4, 5, 6)] + [0x48000302]
    mu.mem_write(0x80007E00, b''.join(struct.pack('>I', w) for w in inj))
pc0 = entry
while True:
    try:
        mu.emu_start(pc0, STOP, timeout=300 * 1000000)
    except UcError as e:
        if fault:
            addr, wr = fault.pop(); pc = mu.reg_read(UC_PPC_REG_PC); msr = mu.reg_read(UC_PPC_REG_MSR)
            ntrap[0] += 1
            if os.environ.get("EMU_DBG"): print("DSI pc=%08X addr=%08X wr=%d msr=%08X" % (pc, addr, wr, msr))
            mu.mem_write(0x80007F00, b''.join(struct.pack('>I', mu.reg_read(UC_PPC_REG_0 + r)) for r in (3, 4, 5, 6)))
            for r, v in zip((3, 4, 5, 6), (pc, msr, addr, 0x40000000 | (0x02000000 if wr else 0))): mu.reg_write(UC_PPC_REG_0 + r, v)
            mu.reg_write(UC_PPC_REG_MSR, msr & ~0x8000); pc0 = 0x00007E00; continue
        print('ERREUR emulateur', e, 'pc=%08X lr=%08X' % (mu.reg_read(UC_PPC_REG_PC), mu.reg_read(UC_PPC_REG_LR))); sys.exit(1)
    if armed[1]:
        armed[1] = False; mu.mem_unmap(0xCC006000, 0x1000); pc0 = mu.reg_read(UC_PPC_REG_PC); continue
    if armed[2]:
        armed[2] = False; mu.mmio_map(0xCC006000, 0x1000, io_read, None, io_write, None); pc0 = mu.reg_read(UC_PPC_REG_PC); continue
    break
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
if MODE in ('swap', 'cheat', 'ws', 'cardb', 'pad', 'video', 'tick', 'trap'):   # v121, v122, v133, v139
    rd32 = lambda a: struct.unpack('>I', mu.mem_read(a, 4))[0]
    u = lambda o: struct.unpack('>I', mu.mem_read(0x817F0000 + o, 4))[0]
    back = rd32(0x80001F50) == 0x42424242 and pc == STOP
    print(('OK   ' if back else 'FAUX ') + 'retour a GC Station a la fin (%08X)' % rd32(0x80001F50)); ok &= back
    if MODE == 'swap':
        d2 = open(sys.argv[5], 'rb').read()
        cv = rd32(0x80001F48) & 7 == 6 and rd32(0x80001F4C) & 7 == 2
        print(('OK   ' if cv else 'FAUX ') + 'couvercle : %X apres le changement (attendu 6 : masque + CVRINT), %X apres acquittement (attendu 2)' % (rd32(0x80001F48), rd32(0x80001F4C))); ok &= cv
        chk('ID de l\'autre disque apres le changement', 0x80110000, d2[0:0x20])
        chk('donnees de l\'autre disque (0x200 @0x1000)', 0x80110100, d2[0x1000:0x1200])
        dz = u(0x198)
        dk = dz & 0xFFFF == 0x101
        print(('OK   ' if dk else 'FAUX ') + 'resident : disque en cours %d, %d changement(s)' % ((dz & 1) + 1, (dz >> 8) & 0xFF)); ok &= dk
    if MODE == 'tick':                                  # v133 : fins donnees depuis l'image (sans lecteur, ou secours)
        nodrive = os.environ.get('EMU_NODRIVE') == '1'
        f1, f2 = rd32(0x80001FC4), rd32(0x80001FC8)
        d1 = bytes(mu.mem_read(0x80130000, 0x200)) == iso[0x1000:0x1200]
        d2 = bytes(mu.mem_read(0x80140000, 0x8000)) == iso[0x20000:0x28000]
        good = d1 and d2 and 0 < f1 < 600 and 0 < f2 < 600 and (f1 <= 2 if nodrive else f1 >= 3)
        print(('OK   ' if good else 'FAUX ') + 'fins de commande %s : petite lecture vue apres %d image(s) (%s), grosse lecture en fond apres %d image(s) (%s)' % (
            'sans le lecteur' if nodrive else 'perdues -> secours', f1, d1, f2, d2)); ok &= good
        sv = rd32(0x80001FCC) == 0x12345678 and di_real[4] != 0x12345678
        print(('OK   ' if sv else 'FAUX ') + 'acces au lecteur trouve seulement par la recherche large : %08X (vrai registre %08X)' % (rd32(0x80001FCC), di_real[4])); ok &= sv
        nk = (len(kicks) == 0) if nodrive else True
        print(('OK   ' if nk else 'FAUX ') + 'vrai lecteur : %d commande(s)%s' % (len(kicks), ' (attendu : aucune)' if nodrive else '')); ok &= nk
        if not nodrive:
            jb = bytes(disk[card.writes[-1] * 512:card.writes[-1] * 512 + 512]) if card.writes else bytes(512)
            dd = [i for i in range(3) if (struct.unpack_from('>I', jb, 0x1D0 + i * 16)[0] >> 24) == 0xDD]   # (v139 : anneau en 0x1D0)
            print(('OK   ' if dd else 'FAUX ') + 'journal : secours note (%d entree(s) DD)' % len(dd)); ok &= bool(dd)
    if MODE == 'trap':                                  # v139
        st, n, last, bad = u(0x1C0), u(0x1C4), u(0x1C8), u(0x1CC)
        d = bytes(mu.mem_read(0x80150000, 0x200)) == iso[0x1000:0x1200]
        good = st == 1 and n >= 10 and bad == 0 and d and 0 < rd32(0x80001FD8) < 600 and rd32(0x80001FD4) == 0xCC006014
        print(('OK   ' if good else 'FAUX ') + 'acces pris par exception : etat %d, %d acces (%d exceptions), dernier %08X, lecture %s, fin apres %d image(s), stwu -> %08X' % (
            st, n, ntrap[0], last, d, rd32(0x80001FD8), rd32(0x80001FD4))); ok &= good
        pt = rd32(0x80001FE0) == 3 and rd32(0x80001FE4) == 3 and di_real[2] != 0xA8000000
        print(('OK   ' if pt else 'FAUX ') + 'autres registres de la page (son, 3 ecrit plus tot par le jeu) : lwz %08X, lbz %02X ; vrai lecteur jamais commande pour lire (%08X)' % (rd32(0x80001FE0), rd32(0x80001FE4), di_real[2])); ok &= pt
        vec = rd32(0x80000300)
        print(('OK   ' if vec == 0 else 'FAUX ') + 'retour a GC Station : vecteur 0x300 du jeu remis (%08X), option %s' % (vec, 'coupee' if st == 1 and vec == 0 else '?')); ok &= vec == 0
    if MODE == 'cheat':
        want = {0x80001F70: 0x12345678, 0x80001F78: 0xABCD0000, 0x80001F80: 0, 0x80001F84: 0xCAFEBABE, 0x80001F88: 0x11111111, 0x80001F8C: 0x80000000, 0x80001F60: 0x00005A5A}
        for a, v in want.items():
            g = rd32(a); good = g == v
            print(('OK   ' if good else 'FAUX ') + 'cheat : %08X = %08X (attendu %08X)' % (a, g, v)); ok &= good
        n = u(0x198) >> 16
        print(('OK   ' if n >= 3 else 'FAUX ') + 'cheats appliques a %d images' % n); ok &= n >= 3
    if MODE == 'ws':
        r = [rd32(a) for a in (0x80001F90, 0x80001F94, 0x80001FA0, 0x80001F98, 0x80001F9C, 0x80001D00, 0x80001D08)]
        good = r == [0x3F400000, 0x3EC00000, 0x40000000, 0x3F800000, 0x3E800000, 0x3F800000, 0x3F000000]
        print(('OK   ' if good else 'FAUX ') + '16:9 force : perspective m00 %08X m02 %08X m11 %08X ; orthographique m00 %08X m03 %08X ; matrice du jeu %08X %08X' % tuple(r)); ok &= good
    print('TOUT OK' if ok else 'ECHEC'); sys.exit(0 if ok else 1)
if EXEC:                                                 # v95 : OSExec : le code de redemarrage puis le programme suivant
    H0 = 0x817F0000
    u = lambda o: struct.unpack('>I', mu.mem_read(H0 + o, 4))[0]
    rd32 = lambda a: struct.unpack('>I', mu.mem_read(a, 4))[0]
    if not EXECA:
        got = bytes(mu.mem_read(0x81300000, 0x48)); want = iso[0x3460:0x3460 + 0x48]
        diff = [i for i in range(0, 0x48, 4) if got[i:i + 4] != want[i:i + 4]]
        rb = len(diff) == 7 and all(got[i] & 0xFC == 0x48 for i in diff)
        print(('OK   ' if rb else 'FAUX ') + 'code de redemarrage lu en 0x81300000, %d acces au lecteur remplaces par un saut' % len(diff)); ok &= rb
    m = rd32(0x80001F60) == 0x77777777 and pc == STOP
    print(('OK   ' if m else 'FAUX ') + 'programme suivant lance (repere %08X, pc %08X)' % (rd32(0x80001F60), pc)); ok &= m
    chk('programme suivant : lecture par ses propres acces au lecteur', 0x80420000, iso[0x1004:0x1024])
    print('exec_n %d exec_last %08X tramp_at %08X tramp_n %d vi %d reboot %08X/%d' % (u(0x168), u(0x16C), u(0x178), u(0x17C), u(0x90), u(0x170), u(0x174)))
    nk = all(k == 0x12000000 for k in kicks)
    print(('OK   ' if nk else 'FAUX ') + 'rien envoye au vrai lecteur pendant OSExec (%d commandes)' % len(kicks)); ok &= nk
    br = u(0x17C) == (7 if EXECA else 7 + 7)
    print(('OK   ' if br else 'FAUX ') + 'acces branches pendant le jeu : %d (attendu : 7 dans le code de redemarrage + 7 dans le programme suivant)' % u(0x17C)); ok &= br
    if MODE == 'cardb':
        cn, ct = u(0x1A0), u(0x1A4)
        ext = [struct.unpack('>III', mu.mem_read(ct + i * 12, 12)) for i in range(cn)]
        def csd(sec):
            for i0, sd, c in ext:
                if i0 <= sec < i0 + c: return sd + sec - i0
            return None
        pat = b''.join(struct.pack('>I', 0xA5000000 | (i * 4)) for i in range(32))
        exp = pat + bytes(((x >> 9) * 7 + x) & 0xFF for x in range(0x4080, 0x4200))
        got = bytes(mu.mem_read(0x80420000, 0x200)); s1 = csd(0x4000 >> 9)
        f1 = bytes(disk[s1 * 512:s1 * 512 + 512]) if s1 else b''
        good = got == exp and f1 == exp and not bad0 and u(0x48) & 64
        print(('OK   ' if good else 'FAUX ') + 'carte du slot B : relu %s, fichier %s, %d morceaux, vraie carte touchee %s' % (got == exp, f1 == exp, cn, bad0[:3])); ok &= bool(good)
    if MODE == 'pad':
        h, l = rd32(0x80001FB0), rd32(0x80001FB4)
        good = h == 0x11D00000 and l == 0x2020C010 and si_poll and si_poll[-1] == 0x003208F0
        print(('OK   ' if good else 'FAUX ') + 'manette : H %08X (attendu 11D00000 : L enfonce), L %08X (attendu 2020C010 : stick C inverse), SIPOLL %s (attendu 003208F0)' % (h, l, ['%08X' % x for x in si_poll])); ok &= bool(good)
        z = bytes(mu.mem_read(0x80120000, 0x200)) == bytes(0x200)
        print(('OK   ' if z else 'FAUX ') + 'lecture apres la fin du fichier (.nkit.iso) : des zeros'); ok &= z
    if MODE == 'video':
        a = rd32(0x80001FAC)
        def rm(o): return struct.unpack('>IHHHHHHHHIBB', bytes(mu.mem_read(a + o, 0x1A))), bytes(mu.mem_read(a + o + 0x32, 7))
        (n, nf), (p, pf) = rm(0), rm(0x3C)
        flat = bytes([0, 0, 21, 22, 21, 0, 0])
        g1 = n[0] == 2 and n[9] == 0 and nf == flat and n[6] == 704 and n[4] == 8 and n[3] == 480
        g2 = p[0] == 2 and p[2] == 480 and p[3] == 480 and p[7] == 480 and p[5] == 0 and pf == flat and p[6] == 704
        print(('OK   ' if g1 else 'FAUX ') + 'mode NTSC 480i -> 480p, large : %s %s' % (n, nf.hex()))
        print(('OK   ' if g2 else 'FAUX ') + 'mode PAL 528i -> 480p (60 Hz), large : %s %s' % (p, pf.hex()))
        ok &= g1 and g2
    print('TOUT OK' if ok else 'ECHEC'); sys.exit(0 if ok else 1)
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
hk = resets == [] and 0x817F0000 <= tbl[0] < 0x81800000 and 0x817F0000 <= tbl[2] < 0x81800000 and rd32(0x80001F00) == 3 and (EXC or 55 <= rd32(0x80001F04) <= 140)   # (v86 : 60 lectures des manettes, SI ou image ; v93 : 1 sur 3 pas encore prise)
print(('OK   ' if hk else 'FAUX ') + 'crochets + A+Z+START / plantage : redemarrages %s, table SI %08X VI %08X, images %d, manettes %d' % (resets, tbl[0], tbl[2], rd32(0x80001F00), rd32(0x80001F04)))
ok &= hk
nolat = all(not (o == 0x40C and n <= 20) for o, n in si_reads) and (EXC or len(si_reads) > 0)
print(('OK   ' if nolat else 'FAUX ') + 'manettes : donnees neuves pas touchees (%d lectures SI)' % len(si_reads))
ok &= nolat
ok &= struct.unpack('>I', mu.mem_read(0x80001F24, 4))[0] == 0 and struct.unpack('>I', mu.mem_read(0x80001F20, 4))[0] == 0
ok &= rd(0x4C) == 6 and len(kicks) >= 6 + 15 and all(k == 0x12000000 for k in kicks)   # (v61 : + reveils pour les morceaux ; v65 : + E1, E2 ; v94 : 16 Ko par reveil)
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
for blk in range(12):
    a = iso[0x10000 + blk * 32:0x10000 + blk * 32 + 32]
    for i in range(28): pcm.append((adp(a[i + 4] & 0xF, a[0], hl), adp(a[i + 4] >> 4, a[1], hr)))
want = b''.join(struct.pack('>hh', max(-0x8000, min(0x7fff, (l * 255) >> 8)), max(-0x8000, min(0x7fff, (r * 255) >> 8))) for l, r in pcm[:160])
got = bytes(mu.mem_read(0x80300000, 0x280))
want2 = b''.join(struct.pack('>hh', max(-0x8000, min(0x7fff, (l * 255) >> 8)), max(-0x8000, min(0x7fff, (r * 255) >> 8))) for l, r in pcm[160:320])
got2 = bytes(mu.mem_read(0x80301000, 0x280))
dsp2 = got2 == want2                                  # v99 : tampon programme dans l'interruption DSP, complete une fois
print(('OK   ' if dsp2 else 'FAUX ') + 'musique du disque (v99 / v106) : tampon programme par le DSP, puis recalcule par le DSP : memes echantillons (%d identiques), tampons rejoues laisses tels quels' % sum(1 for i in range(0, 0x280, 4) if got2[i:i + 4] == want2[i:i + 4]))
ok &= dsp2
dtk = got == want and rd32(0x80001F28) == 1 and rd32(0x80001F08) == 3
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
    SS = None
    if EXC:                                        # v83 : photo de la memoire (secteurs ecrits avant le journal)
        dd = [w for w in card.writes if bytes(disk[w * 512:w * 512 + 4]) == b'GCSM']
        SS = dd[-1] if dd else None
    snap = set(range(SS, SS + 704)) if SS is not None else set()
    jok = set(card.writes) - csecs - snap == ({js, js + 1, js + 2} if EXC else {js}) and jb[:4] == b'GCSN' and u(0x4C) == 6 and u(0x90) == 3
    if EXC:                                        # v79 : details du plantage dans le secteur suivant
        cb = disk[(js + 1) * 512:(js + 2) * 512]; open('journal.bin', 'ab').write(cb)
        cu = lambda o: struct.unpack_from('>I', cb, o)[0]
        cok = cb[:4] == b'GCSC' and cu(0x0C) == 0x80001234 and cu(0x14) == 0x80005678 and cu(0x30 + 4) == 0x80001A00 and cu(0x30 + 16) == 0x01234567
        cok &= cu(0x30 + 25 * 4) == 0x51515151           # v83 : registre sauve par le gestionnaire (pas l'ancien contexte)
        cok &= cu(0x2C) == 2 and cu(0xD0) == 0x80007777 and cu(0xD4) == 0x80008888 and cb[0x110:0x120] == b'm_Do_machine.cpp'
        cok &= all(cu(0xB0 + i * 4) == rd32(0x80000000 + o) for i, o in enumerate((0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0xF0, 0xF4)))
        print(('OK   ' if cok else 'FAUX ') + 'details du plantage : PC %08X LR %08X r1 %08X r4 %08X, %d appels %08X %08X, texte r3 %r' % (
            cu(0x0C), cu(0x14), cu(0x34), cu(0x40), cu(0x2C), cu(0xD0), cu(0xD4), bytes(cb[0x110:0x120])))
        jok &= cok
        sb = disk[(js + 2) * 512:(js + 3) * 512]; open('journal.bin', 'ab').write(sb)   # v82 : pile du jeu
        su = lambda o: struct.unpack_from('>I', sb, o)[0]
        sok = sb[:4] == b'GCSS' and su(4) == 0x80001A00 and su(8) == 80 and su(0x10) == 0x80001A40 and su(0x18) == 0x80001B00
        sok &= su(0x0C) >= 1 and su(0x150) == 0x80001B00 and sb[0x154:0x164] == b'm_Do_machine.cpp'
        print(('OK   ' if sok else 'FAUX ') + 'pile du jeu au plantage : r1 %08X, %d mots, %d textes, 1er texte %r' % (su(4), su(8), su(0x0C), bytes(sb[0x154:0x164])))
        jok &= sok
        nok = SS is not None
        if nok:
            sd0 = bytes(disk[SS * 512:SS * 512 + 512]); su0 = lambda o: struct.unpack_from('>I', sd0, o)[0]
            nz = su0(4); zs = [(su0(8 + z * 16), su0(12 + z * 16), su0(16 + z * 16)) for z in range(nz)]
            nok &= 4 <= nz <= 7 and zs[0][0] == 0x80000000 and zs[1][0] == 0x80008000 and zs[2][0] == 0x80001A00 - 0x200   # (v135 : + zones r3 / r4 / r30)
            nok &= all(su0(20 + z * 16) in (3, 4, 5) for z in range(3, nz - 1)) and su0(20 + (nz - 1) * 16) == 6
            if nok: nok &= bytes(disk[(SS + zs[2][2]) * 512 + 0x200:(SS + zs[2][2]) * 512 + 0x240]) == bytes(mu.mem_read(0x80001A00, 64))
        print(('OK   ' if nok else 'FAUX ') + 'photo de la memoire au plantage : %s' % (['%08X+%X' % (b, l) for b, l, s0 in zs] if SS is not None else 'rien'))
        jok &= nok
    print('journal : musique %d blocs, %d tampons ; carte %d commandes (derniere %02X), %d ecritures, %d interruptions' % (u(0x108), u(0x10C), u(0x110), u(0x118), u(0x114), u(0x11C)))
    print('echantillons : %d, code %08X LR %08X' % (u(0x130), u(0x138), u(0x134)))
    jok &= u(0x130) > 0 and u(0x134) == 0x80003100 and 0x80003100 < u(0x138) < 0x80004000
    jok &= u(0x108) == 18 and u(0x10C) == 3 and u(0x110) == 5 and u(0x118) == 0x52 and u(0x114) == 2
    print('lectures en fond : %d (%d morceaux)' % (u(0xD0), u(0xD4)))
    jok &= u(0xD0) == 2 and u(0xD4) == 132               # (v88 : 0x2000 + 0x40000 octets, morceaux de 2 Ko)
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
