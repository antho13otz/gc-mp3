# v152 : verifie l'image de lancement du faux Rogue Squadron III (boot-z.bin, ecrite par h2)
import struct, sys, zlib
b = open(sys.argv[1], 'rb').read()
entry, n = struct.unpack_from('>II', b, 0); p = 8; mem = {}
for i in range(n):
    dst, sz = struct.unpack_from('>II', b, p); p += 8; mem[dst] = b[p:p + sz]; p += sz
def rd(addr, ln):
    for a, d in mem.items():
        if a <= addr and addr + ln <= a + len(d): return d[addr - a:addr - a + ln]
    return None
ok = True
dl, il = struct.unpack('>II', rd(0x80200000, 8))
inner = zlib.decompress(rd(0x80200008, dl))
t = 0x100
w = [struct.unpack_from('>I', inner, t + i * 4)[0] for i in range(8)]
br = [i for i in (1, 2, 4, 5) if w[i] >> 26 == 18]
tg = [(0x80300000 + i * 4 + ((w[i] & 0x03FFFFFC) - (0x04000000 if w[i] & 0x02000000 else 0))) & 0xFFFFFFFF for i in br]
good = len(inner) == il and len(br) == 4 and all(0x81700000 <= x < 0x81800000 for x in tg) and w[0] == 0x3D20CC00 and w[7] == 0x4E800020   # (v159 : aiguillages sous le resident, plus dedans)
print(('OK   ' if good else 'FAUX ') + 'programme compresse : %d -> %d octets (zlib relu), %d acces branches vers %s' % (dl, il, len(br), ['%08X' % x for x in tg])); ok &= good
res = rd(0x817EE000, 0x200)
opt = struct.unpack_from('>I', res, 0x1B8)[0] if res else 0xFFFFFFFF
good = res is not None and not (opt & 0x02000000)
print(('OK   ' if good else 'FAUX ') + 'Pulse universel coupe (options du resident %08X)' % opt); ok &= good
log = open('out/dernier-lancement.txt', encoding='latin-1').read() if False else ''
print('TOUT OK' if ok else 'ECHEC')
