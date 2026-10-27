#!/usr/bin/env python3
# v171 : un VRAI jeu sous Pulse, dans l'emulateur PowerPC : la memoire preparee par GC Station (lancement
# natif, programme du jeu branche + resident), avec la carte SD simulee et le vrai lecteur simule de
# emu_e2e.py, plus ce qu'il faut pour que le systeme du jeu tourne : interruptions (PI), image (VI),
# horloge (time base, decrementeur), fins de dessin (PE). Sert a suivre ce que fait le jeu (carte
# memoire virtuelle de Crash Bandicoot) sans console.
#   emu_game.py boot.bin sd.img jeu.iso [secondes] [options]
import os, sys, struct, ctypes, collections
os.environ.setdefault('UC_NO_MEM_EXIT', '1')            # (Unicorn corrige par run.sh : pas d'arret au milieu d'un acces memoire)
T = os.path.dirname(os.path.abspath(__file__))
src = open(T + '/emu_e2e.py').read()
src = src[:src.index("MODE = sys.argv[4]")]
import mmap                                           # (v172 : grandes images creuses du banc : lues a la demande)
src = src.replace("disk = bytearray(open(SDIMG, 'rb').read())", "_fsd = open(SDIMG, 'rb'); disk = mmap.mmap(_fsd.fileno(), 0, access=mmap.ACCESS_COPY)")
src = src.replace("iso = open(ISO, 'rb').read()", "_fiso = open(ISO, 'rb'); iso = mmap.mmap(_fiso.fileno(), 0, access=mmap.ACCESS_READ)")
SECS = float(sys.argv[4]) if len(sys.argv) > 4 else 20
OPTS = sys.argv[5:]
sys.argv = sys.argv[:4]
exec(compile(src, 'emu_e2e.py', 'exec'))             # (carte SD, vrai lecteur, EXI, SI, AI : ceux du banc)
MODE = ''

RAM = ctypes.create_string_buffer(0x01800000)
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
mu.ctl_set_cpu_model(UC_CPU_PPC32_750CL_V2_0)        # (750CL : pas d'AltiVec a la place des instructions "paired single")
for base in (0x00000000, 0x80000000, 0xC0000000): mu.mem_map_ptr(base, 0x01800000, UC_PROT_ALL, ctypes.addressof(RAM))
rd32 = lambda a: struct.unpack('>I', mu.mem_read(a, 4))[0]
wr32 = lambda a, v: mu.mem_write(a, struct.pack('>I', v & 0xFFFFFFFF))

# ---- horloge : 1 tick de time base pour 12 instructions (486 MHz / 40,5 MHz)
import time
RATE = float(os.environ.get('EMU_RATE', '50e6'))  # (instructions par seconde reelle supposees : l'horloge du jeu suit le temps reel)
T0 = time.perf_counter()
icount = [0]
def now(): icount[0] = int((time.perf_counter() - T0) * RATE); return icount[0]
tbx = [0]
TB0 = 60 * 40500000                                   # (console allumee depuis une minute)
def tb(): tbx[0] += 2; return TB0 + now() // 12 + tbx[0]
dec = {'base': 0, 'val': 0, 'armed': False}
pend = collections.Counter()
def dec_now(): return (dec['val'] - (tb() - dec['base'])) & 0xFFFFFFFF

# ---- registres du materiel qui manquent au banc
pi = {'intsr': 0, 'intmr': 0}
vi = collections.defaultdict(int); vi[0x02] = 0x0001; vi[0x6C] = 0; vi[0x6E] = 1; vi[0x30] = 0x1001; vi[0x32] = 0x0001   # (interruption 0 de l image laissee active par l IPL)
pe = collections.defaultdict(int)
cp = collections.defaultdict(int)
mi = collections.defaultdict(int)
log = []
def note(*a):
    s = ' '.join(str(x) for x in a); log.append(s)
    if 'v' in OPTS: print(s)

def exi_pend():
    p = 0
    for ch in range(3):
        c = exi[ch][0]
        if (c & 0x3) == 0x3 or (c & 0xC) == 0xC or (c & 0xC00) == 0xC00: p = 1
    return p
def pi_cause():
    c = 0
    if (di_real[0] & 0x18) == 0x18 or (di_real[0] & 0x6) == 0x6: c |= 0x4
    if exi_pend(): c |= 0x10
    if (ai[0] & 0xC) == 0xC: c |= 0x20
    if (vi[0x30] & 0x9000) == 0x9000 or (vi[0x34] & 0x9000) == 0x9000: c |= 0x100
    if (pe[0x0A] & 0x5) == 0x5 or (pe[0x0A] & 0xA) == 0xA: c |= (0x200 if (pe[0x0A] & 0x5) == 0x5 else 0) | (0x400 if (pe[0x0A] & 0xA) == 0xA else 0)
    dc = dspr.get(0x0A, 0)
    if (dc & 0x18) == 0x18 or (dc & 0x60) == 0x60 or (dc & 0x180) == 0x180: c |= 0x40
    return c | 0x10000                                   # (bouton reset relache)
def pi_read2(uc, off, size, data):
    if off == 0x00: return pi_cause()
    if off == 0x04: return pi['intmr']
    if off == 0x2C: return 0x246500B1
    return pi.get(off, 0)
def pi_write2(uc, off, size, value, data):
    value &= 0xFFFFFFFF
    if off == 0x04: pi['intmr'] = value
    elif off == 0x24:
        resets.append(value)
        if value == 0: uc.emu_stop()
    else: pi[off] = value
def r16(d, off, size): return d[off] if size == 2 else (d[off] << 16) | d[off + 2]
def w16(d, off, size, value):
    if size == 2: d[off] = value & 0xFFFF
    else: d[off] = (value >> 16) & 0xFFFF; d[off + 2] = value & 0xFFFF
def vi_read(uc, off, size, data): return r16(vi, off, size)
def vi_write(uc, off, size, value, data):
    if 'vi' in OPTS and 0x30 <= off < 0x40: print('VI %02X <- %08X (%d) pc %08X' % (off, value, size, uc.reg_read(UC_PPC_REG_PC)))
    w16(vi, off, size, value)
def pe_read(uc, off, size, data): return r16(pe, off, size)
def pe_write(uc, off, size, value, data):
    if 'pe' in OPTS and off in (8, 0xA): print('PE %02X <- %08X (%d) pc %08X avant %X' % (off, value, size, uc.reg_read(UC_PPC_REG_PC), pe[0x0A]))
    if size == 4 and off == 0x08: w16(pe, 0x08, 2, value >> 16); off, size, value = 0x0A, 2, value & 0xFFFF
    if off == 0x0A and size == 2:                        # (acquitter : 1 efface)
        pe[0x0A] = (pe[0x0A] & 0xC & ~(value & 0xC)) | (value & 0x3); return
    w16(pe, off, size, value)
def cp_read(uc, off, size, data):
    if off == 0x00: return 0x14                          # (CP : au repos)
    return r16(cp, off, size)
def cp_write(uc, off, size, value, data): w16(cp, off, size, value)
def mi_read(uc, off, size, data): return r16(mi, off, size)
def mi_write(uc, off, size, value, data): w16(mi, off, size, value)
aram = bytearray(0x01000000)
# (boites aux lettres du DSP, comme la ROM du DSP puis le microcode : la reponse est donnee tout de suite)
dsp_q = [0x8071FEED]; dsp_in = []; dsp_hi = [0]
def dsp_mail_in(m):
    dsp_in.append(m); note('DSP <- %08X' % m)
    if len(dsp_in) >= 2 and dsp_in[-2] == 0x80F3D001: dsp_q.append(0xDCD10000); dspr[0x0A] = dspr.get(0x0A, 0) | 0x80   # (microcode lance)
def dsp_read2(uc, off, size, data):
    d = collections.defaultdict(int, dspr)
    d[0x16] |= 1                                         # (memoire ARAM prete)
    d[0x00] &= 0x7FFF                                    # (lettre vers le DSP : prise tout de suite)
    if dsp_q: d[0x04] = 0x8000 | (dsp_q[0] >> 16); d[0x06] = dsp_q[0] & 0xFFFF
    else: d[0x04] = 0
    if off == 0x06 or (off == 0x04 and size == 4):
        if dsp_q: note('DSP -> %08X' % dsp_q[0]); dsp_q.pop(0)
    return r16(d, off, size)
def dsp_write2(uc, off, size, value, data):
    d = collections.defaultdict(int, dspr)
    if off == 0x0A and size == 2:
        old = d[0x0A]; v = value & 0xFFFF
        dspr[0x0A] = ((old & 0xA8) & ~(v & 0xA8)) | (v & ~0xA8 & ~0x801)   # (bits d'interruption : 1 efface ; reset fait tout de suite)
        return
    w16(d, off, size, value); dspr.update(d)
    if off == 0x02 or (off == 0x00 and size == 4): dsp_mail_in(((d[0x00] & 0x7FFF) << 16 | d[0x02]) | 0x80000000 if d[0x00] & 0x8000 else (d[0x00] << 16 | d[0x02]))
    if off in (0x28, 0x2A) and (off == 0x2A or size == 4):   # DMA ARAM : tout de suite, puis ARINT
        mm = ((d[0x20] << 16) | d[0x22]) & 0x03FFFFE0; ar = ((d[0x24] << 16) | d[0x26]) & 0x00FFFFE0
        cnt = (d[0x28] << 16) | d[0x2A]; n = cnt & 0x7FFFFFE0
        if n and mm + n <= 0x01800000 and ar + n <= len(aram):
            if cnt & 0x80000000: mu.mem_write(0x80000000 | mm, bytes(aram[ar:ar + n]))
            else: aram[ar:ar + n] = mu.mem_read(0x80000000 | mm, n)
        dspr[0x0A] = dspr.get(0x0A, 0) | 0x20
def ai_adv(n):
    if not ai[0] & 1: return
    old = ai[2]; ai[2] = (ai[2] + n) & 0xFFFFFFFF
    if ai[0] & 0x10 == 0 and old < ai[3] <= ai[2] or (old > ai[2] and ai[3] <= ai[2]): ai[0] |= 0x8   # (AIIT atteint : AIINT)
mmio_last = [0]
def io_read2(uc, off, size, data):
    try: return io_read2_(uc, off, size, data)
    except Exception as e:
        import traceback; traceback.print_exc(); raise
def io_read2_(uc, off, size, data):
    mmio_last[0] = uc.reg_read(UC_PPC_REG_PC)
    if off == 0xC08: ai_adv(16)                          # (compteur d'echantillons du son : avance quand on le lit)
    return io_read(uc, off, size, data)
DI_LAT = float(os.environ.get('EMU_DILAT', '0'))       # (vrai lecteur : fin de commande apres ce temps, en s de jeu)
di_due = [None]
# (v172 : SRAM de la console, canal 0 peripherique 1 : ID des cartes a 0 avec leur somme de controle, comme une
#  console qui a deja vu une carte formatee par le SDK ; sommes de la partie principale justes)
sram = bytearray(64); sram[0x12] = 0; sram[0x13] = 0x2C
sram[0x3A] = 0xFF; sram[0x3B] = 0xFF
def sram_fix():
    a = b = 0
    for i in range(0x0C, 0x14, 2): v = sram[i] << 8 | sram[i + 1]; a = (a + v) & 0xFFFF; b = (b + (v ^ 0xFFFF)) & 0xFFFF
    sram[0:4] = struct.pack('>HH', a, b)
sram_fix()
srm = {'cmd': None, 'pos': 0}
def sram_xfer(uc, value):
    n = ((value >> 4) & 3) + 1; mode = (value >> 2) & 3
    if value & 2:                                         # DMA
        ln = exi[0][2]; a = 0x80000000 | exi[0][1]
        if mode == 0:
            if srm['cmd'] is not None and srm['cmd'] & 0x7FFFFF00 == 0x20000100:
                o = srm['pos']; d = bytes(sram[o:o + ln]) + bytes(max(0, ln - (64 - o))); srm['pos'] += ln
            else: d = bytes(ln)
            uc.mem_write(a, d[:ln])
        elif srm['cmd'] is not None and srm['cmd'] & 0xFFFFFF00 == 0xA0000100:
            d = bytes(uc.mem_read(a, ln)); o = srm['pos']; sram[o:o + len(d)] = d[:64 - o]; srm['pos'] += ln
        return
    if mode == 1:
        if srm['cmd'] is None: srm['cmd'] = exi[0][4]; srm['pos'] = 0; return
        if srm['cmd'] & 0xFFFFFF00 == 0xA0000100:
            for k in range(n):
                if srm['pos'] < 64: sram[srm['pos']] = (exi[0][4] >> (24 - 8 * k)) & 0xFF
                srm['pos'] += 1
    elif mode == 0:
        v = 0
        for k in range(n):
            b = sram[srm['pos']] if srm['cmd'] is not None and srm['cmd'] & 0x7FFFFF00 == 0x20000100 and srm['pos'] < 64 else 0
            v |= b << (24 - 8 * k); srm['pos'] += 1
        exi[0][4] = v
def io_write2(uc, off, size, value, data):
    mmio_last[0] = uc.reg_read(UC_PPC_REG_PC)
    if off == 0x800 and not (value & 0x100): srm['cmd'] = None     # (SRAM deselectionnee)
    if off == 0x80C and value & 1 and exi[0][0] & 0x100 and not exi[0][0] & 0x280:
        exi[0][3] = value; sram_xfer(uc, value); exi[0][3] = value & ~1; exi[0][0] |= 0x8; return
    if off == 0x1C and value & 1 and DI_LAT > 0:          # (vrai lecteur : occupe un moment)
        io_write(uc, off, size, value, data)
        di_real[0] &= ~0x14; di_real[7] |= 1; di_due[0] = now() + int(DI_LAT * 486e6); return
    if off == 0xC00:                                     # AICR : AIINT 1 = efface, SCRESET
        v = value & 0xFFFFFFFF; ai[0] = (v & ~0x28) | (ai[0] & 0x8 & ~v)
        if v & 0x20: ai[2] = 0
        return
    if off >= 0x800 and (off - 0x800) % 0x14 == 0 and off < 0x83C:   # CSR : bits d'interruption 1 = efface
        ch = (off - 0x800) // 0x14
        old = exi[ch][0]
        io_write(uc, off, size, value, data)
        exi[ch][0] = (exi[ch][0] & ~0x80A) | (old & 0x80A & ~(value & 0x80A))
        return
    old3 = [exi[c][3] for c in range(3)]
    io_write(uc, off, size, value, data)
    for c in range(3):
        if old3[c] & 1 == 0 and off == 0x80C + c * 0x14 and value & 1 and not (exi[c][3] & 1): exi[c][0] |= 0x8   # (fin de transfert : TCINT)
mu.mmio_map(0xCC000000, 0x1000, cp_read, None, cp_write, None)
mu.mmio_map(0x0C000000, 0x1000, cp_read, None, cp_write, None)
mu.mmio_map(0xCC001000, 0x1000, pe_read, None, pe_write, None)
mu.mmio_map(0x0C001000, 0x1000, pe_read, None, pe_write, None)
mu.mmio_map(0xCC002000, 0x1000, vi_read, None, vi_write, None)
mu.mmio_map(0x0C002000, 0x1000, vi_read, None, vi_write, None)
mu.mmio_map(0xCC003000, 0x1000, pi_read2, None, pi_write2, None)
mu.mmio_map(0x0C003000, 0x1000, pi_read2, None, pi_write2, None)
mu.mmio_map(0xCC004000, 0x1000, mi_read, None, mi_write, None)
mu.mmio_map(0x0C004000, 0x1000, mi_read, None, mi_write, None)
mu.mmio_map(0xCC005000, 0x1000, dsp_read2, None, dsp_write2, None)
mu.mmio_map(0x0C005000, 0x1000, dsp_read2, None, dsp_write2, None)
mu.mmio_map(0xCC006000, 0x1000, io_read2, None, io_write2, None)
mu.mmio_map(0x0C006000, 0x1000, io_read2, None, io_write2, None)
mu.mmio_map(0xCC008000, 0x1000, lambda *a: 0, None, lambda *a: None, None)   # (tuyau d'ecriture du GPU : ignore)
mu.mmio_map(0x0C008000, 0x1000, lambda *a: 0, None, lambda *a: None, None)   # (tuyau d'ecriture du GPU : ignore)
mu.mem_map(0xFFFF0000, 0x10000)                       # (petit code d'entree des exceptions, hors de la memoire du jeu)
mu.mem_map(0xE0000000, 0x10000)                       # (cache verrouille du Gekko : 16 Ko en 0xE0000000)

b = open(BOOT, 'rb').read()
entry, n = struct.unpack_from('>II', b, 0); p = 8
fst = bi2 = None
for i in range(n):
    dst, sz = struct.unpack_from('>II', b, p); p += 8
    mu.mem_write(dst, b[p:p + sz])
    if dst >= 0x81000000 and b[p:p + 4] == b'\x01\x00\x00\x00' and fst is None: fst = (dst, sz)
    if dst >= 0x81000000 and sz == 0x2000 and bi2 is None: bi2 = dst
    p += sz
mu.mem_write(0x80000000, b[p:p + 0x20])
for a, v in ((0x80000020, 0x0D15EA5E), (0x80000024, 1), (0x80000028, 0x01800000), (0x8000002C, 1), (0x800000CC, 1),
             (0x800000F0, 0x01800000), (0x800000F8, 162000000), (0x800000FC, 486000000)):
    if rd32(a) == 0: wr32(a, v)
# (memoire basse comme la laisse GC Station avant le saut : natif.c, nat_run / na_lowmem)
if fst: wr32(0x80000034, fst[0]); wr32(0x80000038, fst[0]); wr32(0x8000003C, fst[1])
if bi2: wr32(0x800000F4, bi2)
wr32(0x800000D0, 0x01000000); wr32(0x800000E8, 0x81800000 - (fst[0] if fst else 0x81800000)); wr32(0x800000EC, fst[0] if fst else 0)
wr32(0x800030C0, TB0 // 40500 // 100 + 1)              # (carte du slot A vue depuis longtemps : comme na_lowmem)
mu.mem_write(0x800030E0, bytes([0, 6])); mu.mem_write(0x800030E9, bytes([5]))
print('FST %s, bi2 %s' % (fst and '%08X (%d)' % fst, bi2 and '%08X' % bi2))

# (v172 : reconnus dans n'importe quel jeu : __OSInitAudioSystem (attend le vrai DSP) sautee ; PPCHalt = arret du jeu)
def find_code(words, lo=0x80003100, hi=0x80600000):
    code = bytes(mu.mem_read(lo, hi - lo)); pat = b''.join(struct.pack('>I', w) for w in words); out = []; i = code.find(pat)
    while i >= 0:
        if i % 4 == 0: out.append(lo + i)
        i = code.find(pat, i + 4)
    return out
def func_start(a):
    for k in range(400):
        if rd32(a - 4 * k) == 0x7C0802A6: return a - 4 * k
    return None
auto_stub = []
for a in find_code([0x38000043, 0xB0030012]):
    f = func_start(a)
    if f: auto_stub.append(f)
HALT = find_code([0x7C0004AC, 0x60000000, 0x38600000, 0x60000000, 0x4BFFFFF4])
print('reconnu : initialisation du DSP %s, arret %s' % (' '.join('%08X' % x for x in auto_stub) or '-', ' '.join('%08X' % x for x in HALT) or '-'))
for f in auto_stub:
    if '%08X' % f not in os.environ.get('EMU_STUB', '').upper(): wr32(f, 0x38600000); wr32(f + 4, 0x4E800020)
for a in filter(None, os.environ.get('EMU_STUB', '').split(',')):   # (fonctions du jeu sautees : retour tout de suite, r3 = 0)
    wr32(int(a, 16), 0x38600000); wr32(int(a, 16) + 4, 0x4E800020)
# ---- time base et decrementeur : les instructions mftb / mfspr / mtspr du jeu et du resident sont prises ici
def spr_of(ins): return ((ins >> 16) & 0x1F) | ((ins >> 6) & 0x3E0)
orig_ins = {}
def on_tb(uc, addr, size, data):
    ins = orig_ins[addr]; rd = (ins >> 21) & 31; spr = spr_of(ins); op = (ins >> 1) & 0x3FF
    t = tb()
    if op == 371:                                        # mftb
        uc.reg_write(UC_PPC_REG_0 + rd, (t >> 32) if spr == 269 else t & 0xFFFFFFFF)
    elif op == 339 and spr == 22: uc.reg_write(UC_PPC_REG_0 + rd, dec_now())   # mfdec
    elif op == 467 and spr == 22:                        # mtdec
        dec['base'] = t; dec['val'] = uc.reg_read(UC_PPC_REG_0 + rd); dec['armed'] = True
    elif op == 467 and spr in (284, 285): pass           # (mttb : ignore)
    elif op == 339: uc.reg_write(UC_PPC_REG_0 + rd, sprs.get(spr, 0))   # (registres du Gekko que l'emulateur n'a pas)
    elif op == 467:
        sprs[spr] = uc.reg_read(UC_PPC_REG_0 + rd)
        if spr == 923 and sprs[spr] & 2: lc_dma()       # (DMA du cache verrouille)
def lc_dma():
    u_, l_ = sprs.get(922, 0), sprs[923]
    n = ((u_ & 0x1F) << 2 | (l_ >> 2) & 3) or 128; n *= 32
    mem, lc = u_ & 0xFFFFFFE0, l_ & 0xFFFFFFE0
    mem = (mem & 0x01FFFFFF) | 0x80000000; lcp = 0xE0000000 | (lc & 0x3FFF)
    try:
        if l_ & 0x10: mu.mem_write(lcp, bytes(mu.mem_read(mem, n)))
        else: mu.mem_write(mem, bytes(mu.mem_read(lcp, n)))
    except UcError: pass
    sprs[923] = l_ & ~2
ntb = 0
sprs = {1008: 0x0011C464}
GEKKO = {22, 1008, 1009, 1010, 1013, 1017, 1019, 1020, 1021, 1022, 920, 921, 922, 923} | set(range(912, 920)) | set(range(936, 960))
# (v173 : toutes les sections de code du main.dol, d'apres l'en-tete du disque ; puis le resident)
dol_off = struct.unpack_from('>I', iso, 0x420)[0]
TEXT = []
for i in range(7):
    o_, a_, z_ = (struct.unpack_from('>I', iso, dol_off + k * 4 + i * 4)[0] for k in (0, 0x48 // 4, 0x90 // 4))
    if a_ and z_ and 0x80000000 <= a_ < 0x81800000: TEXT.append((a_, a_ + z_))
for lo, hi in TEXT + [(0x817E9F60, 0x81800000)]:
    code = bytes(mu.mem_read(lo, hi - lo))
    for o in range(0, len(code), 4):
        ins = struct.unpack_from('>I', code, o)[0]
        if ins >> 26 != 31: continue
        op = (ins >> 1) & 0x3FF; spr = spr_of(ins)
        if op == 371 or (op in (339, 467) and spr in GEKKO) or (op == 467 and spr in (284, 285)):
            orig_ins[lo + o] = ins; wr32(lo + o, 0x60000000)   # (remplacee par nop : le crochet fait le travail)
            mu.hook_add(UC_HOOK_CODE, on_tb, begin=lo + o, end=lo + o); ntb += 1
print('instructions d\'horloge prises : %d' % ntb)

# ---- exceptions : SRR0/SRR1 puis le vecteur (0x500 interruption externe, 0x900 decrementeur)
def mts(spr, rs): return 0x7C0003A6 | rs << 21 | (spr & 0x1F) << 16 | (spr >> 5) << 11
def lwz0(rd, d): return 0x80000000 | rd << 21 | (d & 0xFFFF)
INJ = {}
for k, v in enumerate((0x500, 0x800, 0x900, 0x700)):   # (un petit code par vecteur, ecrit une fois : l'emulateur garde le code traduit)
    INJ[v] = 0xFFFFF000 + 0x40 * k
    code = [mts(26, 3), mts(27, 4), lwz0(3, 0xFF00), lwz0(4, 0xFF04), 0x48000002 | v]
    mu.mem_write(INJ[v], b''.join(struct.pack('>I', w) for w in code))
def raise_exc(vec, pc=None):
    if pc is None: pc = mu.reg_read(UC_PPC_REG_PC)
    msr = mu.reg_read(UC_PPC_REG_MSR)
    wr32(0xFFFFFF00, mu.reg_read(UC_PPC_REG_3)); wr32(0xFFFFFF04, mu.reg_read(UC_PPC_REG_4))
    mu.reg_write(UC_PPC_REG_3, pc); mu.reg_write(UC_PPC_REG_4, msr)
    mu.reg_write(UC_PPC_REG_MSR, msr & ~(0x8000 | 0x4000 | 0x2000 | 0x30 | 0x400 | 0x200 | 0x900))
    return INJ[vec]

def backtrace(sp, n=16):
    out = []
    for _ in range(n):
        if not (0x80000000 <= sp < 0x81800000): break
        nsp = rd32(sp)
        if not (0x80000000 <= nsp < 0x81800000): break
        out.append(rd32(nsp + 4)); sp = nsp
    return out
# ---- suivi : fonctions de la carte memoire du jeu (Crash GCBP7D) et du resident
watch = {}
def add_watch(a, name):
    def h(uc, addr, size, data):
        r = [uc.reg_read(UC_PPC_REG_3 + i) for i in range(3)]
        note('%10d %-22s r3=%08X r4=%08X r5=%08X lr=%08X pile %s' % (icount[0], name, r[0], r[1], r[2], uc.reg_read(UC_PPC_REG_LR), ' '.join('%08X' % x for x in backtrace(uc.reg_read(UC_PPC_REG_1), 8))))
    mu.hook_add(UC_HOOK_CODE, h, begin=a, end=a); watch[a] = name
WATCH = os.environ.get('EMU_WATCH', '')
for item in filter(None, WATCH.split(',')):
    a, nm = item.split('=') if '=' in item else (item, item)
    add_watch(int(a, 16), nm)


# ---- "paired singles" du Gekko (l'emulateur ne les a pas) : faits ici, 2e moitie des registres a part
import math
ps1 = [0.0] * 32
def f2b(x): return struct.unpack('>Q', struct.pack('>d', x))[0]
def b2f(b): return struct.unpack('>d', struct.pack('>Q', b & 0xFFFFFFFFFFFFFFFF))[0]
def rs(x):
    try: return struct.unpack('>f', struct.pack('>f', x))[0]
    except OverflowError: return math.copysign(math.inf, x)
def getf(i): return b2f(mu.reg_read(UC_PPC_REG_FPR0 + i))
def setf(i, a, b): mu.reg_write(UC_PPC_REG_FPR0 + i, f2b(a)); ps1[i] = b
def gpr(i): return mu.reg_read(UC_PPC_REG_0 + i) if i else 0
QT = {4: ('B', 1), 5: ('H', 2), 6: ('b', 1), 7: ('h', 2)}
def qload(a, typ, sc):
    if typ in QT:
        f, n = QT[typ]; v = struct.unpack('>' + f, mu.mem_read(a, n))[0]; return v / (2.0 ** sc), n
    return struct.unpack('>f', mu.mem_read(a, 4))[0], 4
def qstore(a, x, typ, sc):
    if typ in QT:
        f, n = QT[typ]; lo, hi = {'B': (0, 255), 'H': (0, 65535), 'b': (-128, 127), 'h': (-32768, 32767)}[f]
        v = x * (2.0 ** sc)
        v = 0 if v != v else int(max(lo, min(hi, v)))
        mu.mem_write(a, struct.pack('>' + f, v)); return n
    mu.mem_write(a, struct.pack('>f', rs(x))); return 4
def sx6(v): return v - 64 if v & 32 else v
def psq_mem(ins, a, load, w, i):
    g = sprs.get(912 + i, 0)
    if load:
        typ, sc = (g >> 16) & 7, sx6((g >> 24) & 0x3F)
        x, n = qload(a, typ, sc)
        y = 1.0 if w else qload(a + n, typ, sc)[0]
        setf((ins >> 21) & 31, x, y)
    else:
        typ, sc = g & 7, sx6((g >> 8) & 0x3F)
        r = (ins >> 21) & 31
        n = qstore(a, getf(r), typ, sc)
        if not w: qstore(a + n, ps1[r], typ, sc)
def setcr(fld, a, b):
    if a != a or b != b: c = 1
    elif a < b: c = 8
    elif a > b: c = 4
    else: c = 2
    cr = mu.reg_read(UC_PPC_REG_CR); sh = 28 - 4 * fld
    mu.reg_write(UC_PPC_REG_CR, (cr & ~(0xF << sh)) | (c << sh))
def rsqrt(x): return math.inf if x == 0 else (math.nan if x < 0 else 1 / math.sqrt(x))
def rcp(x): return math.copysign(math.inf, x) if x == 0 else 1 / x
def safe(f, *a):
    try: return f(*a)
    except (OverflowError, ZeroDivisionError, ValueError): return math.nan
nps = [0]
def do_ps(addr):
    ins = rd32(addr); op = ins >> 26
    d, ra, rb, rc = (ins >> 21) & 31, (ins >> 16) & 31, (ins >> 11) & 31, (ins >> 6) & 31
    nps[0] += 1
    if op in (56, 57, 60, 61):                           # psq_l, psq_lu, psq_st, psq_stu
        off = ins & 0xFFF; off = off - 0x1000 if off & 0x800 else off
        a = (gpr(ra) + off) & 0xFFFFFFFF
        psq_mem(ins, a, op < 60, (ins >> 15) & 1, (ins >> 12) & 7)
        if op in (57, 61): mu.reg_write(UC_PPC_REG_0 + ra, a)
        return True
    if op != 4: return False
    x10 = (ins >> 1) & 0x3FF; x6 = (ins >> 1) & 0x3F; x5 = (ins >> 1) & 0x1F
    if x6 in (6, 7, 38, 39):                             # psq_lx, psq_stx, psq_lux, psq_stux
        a = (gpr(ra) + gpr(rb)) & 0xFFFFFFFF
        psq_mem(ins, a, x6 in (6, 38), (ins >> 10) & 1, (ins >> 7) & 7)
        if x6 in (38, 39): mu.reg_write(UC_PPC_REG_0 + ra, a)
        return True
    A0, A1 = getf(ra), ps1[ra]; B0, B1 = getf(rb), ps1[rb]; C0, C1 = getf(rc), ps1[rc]
    if x10 == 1014:                                      # dcbz_l : zero dans le cache verrouille = memoire ici
        a = (gpr(ra) + gpr(rb)) & 0xFFFFFFE0; mu.mem_write(a, bytes(32)); return True
    xf = {40: lambda: (-B0, -B1), 72: lambda: (B0, B1), 136: lambda: (-abs(B0), -abs(B1)), 264: lambda: (abs(B0), abs(B1)),
          528: lambda: (A0, B0), 560: lambda: (A0, B1), 592: lambda: (A1, B0), 624: lambda: (A1, B1)}
    if x10 in xf:
        r = xf[x10](); setf(d, rs(r[0]) if x10 >= 528 else r[0], rs(r[1]) if x10 >= 528 else r[1]); return True
    if x10 in (0, 32, 64, 96):
        fld = d >> 2
        if x10 < 64: setcr(fld, A0, B0)
        else: setcr(fld, A1, B1)
        return True
    af = {10: lambda: (A0 + B1, C1), 11: lambda: (C0, A0 + B1), 12: lambda: (A0 * C0, A1 * C0), 13: lambda: (A0 * C1, A1 * C1),
          14: lambda: (A0 * C0 + B0, A1 * C0 + B1), 15: lambda: (A0 * C1 + B0, A1 * C1 + B1), 18: lambda: (A0 / B0, A1 / B1),
          20: lambda: (A0 - B0, A1 - B1), 21: lambda: (A0 + B0, A1 + B1), 23: lambda: (C0 if A0 >= 0 else B0, C1 if A1 >= 0 else B1),
          24: lambda: (rcp(B0), rcp(B1)), 25: lambda: (A0 * C0, A1 * C1), 26: lambda: (rsqrt(B0), rsqrt(B1)),
          28: lambda: (A0 * C0 - B0, A1 * C1 - B1), 29: lambda: (A0 * C0 + B0, A1 * C1 + B1),
          30: lambda: (-(A0 * C0 - B0), -(A1 * C1 - B1)), 31: lambda: (-(A0 * C0 + B0), -(A1 * C1 + B1))}
    if x5 in af:
        try: r = af[x5]()
        except (OverflowError, ZeroDivisionError, ValueError):
            r = (math.nan, math.nan)
        setf(d, rs(r[0]), rs(r[1])); return True
    return False
FPOPS = {4, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 59, 60, 61, 63}
if 'memvi' in OPTS:
    def mw(uc, access, addr, size, value, data): print('ECRIT %08X <- %X (%d) pc %08X' % (addr, value, size, uc.reg_read(UC_PPC_REG_PC)))
    mu.hook_add(UC_HOOK_MEM_WRITE, mw, begin=0xCC002030, end=0xCC002033)
    mu.hook_add(UC_HOOK_MEM_WRITE, mw, begin=0x0C002030, end=0x0C002033)
# (EMU_CALL=adresse:fonction[:r3:r4:r5] : la 1re fois que le jeu passe a l'adresse, la fonction est appelee
#  d'abord (dans sa tache, comme s'il l'appelait lui-meme), puis le jeu reprend avec ses registres)
CALL = os.environ.get('EMU_CALL')                      # (plusieurs appels a la suite : separes par ;)
if CALL:
    calls = [[int(x, 16) for x in c.split(':')] for c in CALL.split(';')]
    cp_ = calls[0]
    call_st = {'n': 0}
    def on_call(uc, addr, size, data):
        k = call_st['n']
        if k > len(calls): return
        if k == 0: call_st['regs'] = [uc.reg_read(UC_PPC_REG_0 + i) for i in range(32)] + [uc.reg_read(UC_PPC_REG_LR)]
        else: note('%10d retour de %08X : r3=%08X' % (icount[0], calls[k - 1][1], uc.reg_read(UC_PPC_REG_3)))
        call_st['n'] = k + 1
        if k < len(calls):
            c = calls[k]
            for i in range(32): uc.reg_write(UC_PPC_REG_0 + i, call_st['regs'][i])
            for i, v in enumerate(c[2:6]): uc.reg_write(UC_PPC_REG_3 + i, v)
            for it in filter(None, os.environ.get('EMU_POKE', '').split(',')) if k == 0 else []:
                a_, v_ = it.split('=')
                if v_ == 'sram':
                    b_ = int(a_, 16); fid = bytes(uc.mem_read(b_, 12)); uc.mem_write(b_ + 38, bytes([~sum(fid) & 0xFF]))
                else: uc.mem_write(int(a_, 16), bytes([int(v_, 16)]))
            uc.reg_write(UC_PPC_REG_LR, addr); uc.reg_write(UC_PPC_REG_PC, c[1])
            note('%10d appel %08X depuis %08X' % (icount[0], c[1], addr))
        else:
            for i in range(32): uc.reg_write(UC_PPC_REG_0 + i, call_st['regs'][i])
            uc.reg_write(UC_PPC_REG_LR, call_st['regs'][32])
    mu.hook_add(UC_HOOK_CODE, on_call, begin=cp_[0], end=cp_[0])
# (exceptions du processeur : l'emulateur a deja avance de 4 ; sc et paired singles faits ici sans s'arreter,
#  virgule flottante pas permise -> vecteur 0x800 du jeu ; le reste : arret et rapport)
last_intr = [None]; exc_redirect = [None]
def on_intr(uc, intno, data):
    epc = uc.reg_read(UC_PPC_REG_PC); fa = epc - 4; ins = rd32(fa); op = ins >> 26
    last_intr[0] = (intno, fa)
    if ins == 0x44000002: return                      # sc : sync, rien d'autre
    msr = uc.reg_read(UC_PPC_REG_MSR)
    if not msr & 0x2000 and op in FPOPS:
        pend['fpu'] += 1; uc.reg_write(UC_PPC_REG_PC, raise_exc(0x800, fa)); return
    if op in (4, 56, 57, 60, 61) and do_ps(fa): return
    if 'v' in OPTS and intno == 2: print('DSI a %08X, dernier acces au materiel par %08X' % (fa, mmio_last[0]))
    if intno == 2 and "dsiskip" in OPTS and 0 <= fa - mmio_last[0] < 0x100:   # (arret de l'emulateur pendant un acces au materiel : faux defaut, l'acces est fait)
        pend['dsi_mmio'] += 1; return
    exc_redirect[0] = (intno, fa); uc.emu_stop()
mu.hook_add(UC_HOOK_INTR, on_intr)
halted = []
def on_halt(uc, addr, size, data):
    if not halted: halted.append((icount[0], uc.reg_read(UC_PPC_REG_LR), backtrace(uc.reg_read(UC_PPC_REG_1)))); uc.emu_stop()
for a in HALT: mu.hook_add(UC_HOOK_CODE, on_halt, begin=a, end=a)
# ---- boucle : morceaux d'instructions, interruptions entre deux
FRAME = 810000 * 12                                    # (une image PAL : 1/50 s en instructions)
CH = int(os.environ.get("EMU_CH", "3000"))      # (morceaux de 3 ms)
STOP = 0x80001000; mu.mem_write(STOP, struct.pack('>I', 0x48000000))
mu.reg_write(UC_PPC_REG_1, 0x81000000); mu.reg_write(UC_PPC_REG_LR, STOP)
# (BAT comme les laisse l'IPL : 0x80000000 -> 0 en cache, 0xC0000000 -> 0 sans cache, 256 Mo chacun)
bat = [(528, 0x80001FFF), (529, 0x00000002), (536, 0x80001FFF), (537, 0x00000002), (538, 0xC0001FFF), (539, 0x0000002A)]
code = []
for spr, v in bat: code += [0x3C600000 | (v >> 16), 0x60630000 | (v & 0xFFFF), mts(spr, 3)]
code += [0x48000002 | (entry & 0x03FFFFFC)] if entry < 0x02000000 else []
mu.mem_write(0xFFFFE000, b''.join(struct.pack('>I', w) for w in code))
mu.emu_start(0xFFFFE000, 0xFFFFE000 + 4 * len(code))
pc = entry
next_vi = FRAME; frames = 0
hist = collections.Counter()
end = int(SECS * 486000000)
T0 = time.perf_counter()
while icount[0] < end:
    try:
        mu.emu_start(pc, STOP, timeout=CH)
    except UcError as e:
        epc = mu.reg_read(UC_PPC_REG_PC)
        if rd32(epc - 4) == 0x44000002: pc = epc; continue
        emsr = mu.reg_read(UC_PPC_REG_MSR)
        if emsr & 0x2000 and rd32(epc - 4) >> 26 in (4, 56, 57, 60, 61) and do_ps(epc - 4): pc = epc; continue
        if not emsr & 0x2000:                            # (virgule flottante pas permise : exception 0x800, le systeme du jeu la permet)
            fa = [a for a in (epc - 4, epc) if rd32(a) >> 26 in FPOPS]
            if fa: pend['fpu'] += 1; pc = raise_exc(0x800, fa[0]); continue   # (sc : appel systeme du SDK = sync ; rien a faire ici)
        print('ERREUR emulateur %s pc=%08X lr=%08X apres %d instructions' % (e, mu.reg_read(UC_PPC_REG_PC), mu.reg_read(UC_PPC_REG_LR), icount[0]))
        print("  instruction %08X msr %08X" % (rd32(mu.reg_read(UC_PPC_REG_PC)), mu.reg_read(UC_PPC_REG_MSR)))
        print("  " + " ".join("r%d=%08X" % (i, mu.reg_read(UC_PPC_REG_0 + i)) for i in range(32)))
        print("  pile :", " ".join("%08X" % x for x in backtrace(mu.reg_read(UC_PPC_REG_1))))
        def mfs(spr, rt): return 0x7C0002A6 | rt << 21 | (spr & 0x1F) << 16 | (spr >> 5) << 11
        sp_ = [19, 18] + list(range(528, 544))
        mu.mem_write(0xFFFFF800, b''.join(struct.pack('>I', w) for w in [mfs(sp, 3 + i) for i, sp in enumerate(sp_)] + [0x60000000]))
        mu.reg_write(UC_PPC_REG_MSR, 0x2002)
        try: mu.emu_start(0xFFFFF800, 0xFFFFF800 + 4 * len(sp_))
        except UcError as e: print('  (lecture DAR :', e, ')')
        v_ = [mu.reg_read(UC_PPC_REG_3 + i) for i in range(18)]
        print('  DAR %08X DSISR %08X' % (v_[0], v_[1]))
        print('  IBAT ' + ' '.join('%08X/%08X' % (v_[2 + 2 * k], v_[3 + 2 * k]) for k in range(4)))
        print('  DBAT ' + ' '.join('%08X/%08X' % (v_[10 + 2 * k], v_[11 + 2 * k]) for k in range(4)))
        break
    now()
    pc = mu.reg_read(UC_PPC_REG_PC)
    if exc_redirect[0]:
        intno, fa = exc_redirect[0]
        print('EXCEPTION %d a %08X (instruction %08X) lr=%08X msr=%08X' % (intno, fa, rd32(fa), mu.reg_read(UC_PPC_REG_LR), mu.reg_read(UC_PPC_REG_MSR)))
        print("  " + " ".join("r%d=%08X" % (i, mu.reg_read(UC_PPC_REG_0 + i)) for i in range(32)))
        print("  pile :", " ".join("%08X" % x for x in backtrace(mu.reg_read(UC_PPC_REG_1))))
        def mfs(spr, rt): return 0x7C0002A6 | rt << 21 | (spr & 0x1F) << 16 | (spr >> 5) << 11
        sp_ = [19, 18] + list(range(528, 544))
        mu.mem_write(0xFFFFF800, b''.join(struct.pack('>I', w) for w in [mfs(sp, 3 + i) for i, sp in enumerate(sp_)] + [0x60000000]))
        mu.reg_write(UC_PPC_REG_MSR, 0x2002)
        try: mu.emu_start(0xFFFFF800, 0xFFFFF800 + 4 * len(sp_))
        except UcError as e: print('  (lecture DAR :', e, ')')
        v_ = [mu.reg_read(UC_PPC_REG_3 + i) for i in range(18)]
        print('  DAR %08X DSISR %08X' % (v_[0], v_[1]))
        print('  IBAT ' + ' '.join('%08X/%08X' % (v_[2 + 2 * k], v_[3 + 2 * k]) for k in range(4)))
        print('  DBAT ' + ' '.join('%08X/%08X' % (v_[10 + 2 * k], v_[11 + 2 * k]) for k in range(4)))
        break
    if pc == STOP: print('fin du jeu (retour)'); break
    if halted:
        t_, lr_, bt_ = halted[0]
        print('ARRET DU JEU (PPCHalt : erreur du jeu, OSPanic / OSFatal) apres %d instructions, appele depuis %08X ; pile %s' % (t_, lr_, ' '.join('%08X' % x for x in bt_)))
        break
    hist[pc & ~0xFF] += 1
    if di_due[0] is not None and icount[0] >= di_due[0]:
        di_due[0] = None; di_real[7] &= ~1; di_real[0] |= 0x10 if di_real[2] >> 24 == 0x12 else 0x04
    if icount[0] >= next_vi:
        next_vi += FRAME; frames += 1
        if vi[0x30] & 0x1000: vi[0x30] |= 0x8000         # (retrace : interruption de l'image)
        if pe[0x0A] & 0x2: pe[0x0A] |= 0x8              # (fin de dessin)
        ai_adv(960)
        if dspr.get(0x36, 0) & 0x8000: dspr[0x0A] = dspr.get(0x0A, 0) | 0x08   # (DMA du son : bloc suivant)
        if frames % 50 == 0 and 'q' not in OPTS:
            top = ', '.join('%08X:%d' % kv for kv in hist.most_common(4)); hist.clear()
            print('%3d s  pc=%08X  %s' % (frames // 50, pc, top)); sys.stdout.flush()
    msr = mu.reg_read(UC_PPC_REG_MSR)
    if msr & 0x8000:
        c = pi_cause() & pi['intmr'] & ~0x10000
        if c:
            pend['ext'] += 1
            for bit in range(16):
                if c & (1 << bit): pend['ext%X' % (1 << bit)] += 1
            pc = raise_exc(0x500)
            if 'dbg' in OPTS and pend['ext'] < 3:
                last = pc
                print('  avant : pc %08X msr %08X r3 %08X r4 %08X code %s' % (mu.reg_read(UC_PPC_REG_PC), mu.reg_read(UC_PPC_REG_MSR), mu.reg_read(UC_PPC_REG_3), mu.reg_read(UC_PPC_REG_4), bytes(mu.mem_read(0xFFFFF000, 20)).hex()))
                for k in range(3000):
                    try: mu.emu_start(pc, STOP, count=1)
                    except UcError as e: print('  pas: erreur', e, '%08X' % mu.reg_read(UC_PPC_REG_PC)); pc = mu.reg_read(UC_PPC_REG_PC); break
                    pc = mu.reg_read(UC_PPC_REG_PC)
                    if pc != last + 4: print('  pas %4d %08X -> %08X msr=%08X' % (k, last, pc, mu.reg_read(UC_PPC_REG_MSR)))
                    last = pc
        elif dec['armed'] and dec_now() & 0x80000000: dec['armed'] = False; pend['dec'] += 1; pc = raise_exc(0x900)
print('fin : %d instructions, %d images, interruptions %s, %d instructions paired single' % (icount[0], frames, dict(pend), nps[0]))
H0 = 0x817F0000
u = lambda o: rd32(H0 + o)
print('carte virtuelle : %d commandes (derniere %02X), %d ecritures, %d interruptions ; vrai lecteur : %d commandes' % (u(0x110), u(0x118), u(0x114), u(0x11C), len(kicks)))

def threads():
    t = rd32(0x800000DC); seen = 0
    cur = rd32(0x800000E4)
    while t and seen < 32:
        st = struct.unpack('>H', mu.mem_read(t + 0x2C8, 2))[0]; pr = rd32(t + 0x2D0)
        srr0, lr, sp = rd32(t + 0x198), rd32(t + 0x84), rd32(t + 4)
        bt = []
        for _ in range(12):
            if not (0x80000000 <= sp < 0x81800000): break
            nsp = rd32(sp)
            if not (0x80000000 <= nsp < 0x81800000): break
            bt.append('%08X' % rd32(nsp + 4)); sp = nsp
        print('tache %08X%s etat %d prio %d pc %08X lr %08X pile %s' % (t, ' (courante)' if t == cur else '', st, pr, srr0, lr, ' '.join(bt)))
        t = rd32(t + 0x2FC); seen += 1
threads()
print('VI', {hex(k): hex(v) for k, v in vi.items()}, 'PI', {hex(k) if isinstance(k, int) else k: hex(v) for k, v in pi.items()})
print('vecteur 0x500 :', ' '.join('%08X' % rd32(0x80000500 + 4 * i) for i in range(12)))
print('table des exceptions 0x80003000 :', ' '.join('%08X' % rd32(0x80003000 + 4 * i) for i in range(16)))
print('table des interruptions 0x80003040 :', ' '.join('%08X' % rd32(0x80003040 + 4 * i) for i in range(32)))
print('masques C4 %08X C8 %08X' % (rd32(0x800000C4), rd32(0x800000C8)))
