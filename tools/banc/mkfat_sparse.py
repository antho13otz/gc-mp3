#!/usr/bin/env python3
# v171 : banc d'essai : carte SD FAT32 creuse (sparse) avec une ISO de taille reelle (seuls ses morceaux exportes
# sont ecrits), le faux .dol de GC Station, et une carte memoire virtuelle FORMATEE (comme le fait le SDK) pour le slot A.
# Clusters de 32 Ko ; places attendues par h2 (natif-test) : ISO au cluster 70000 (d'un seul morceau), journal 20000,
# photo de la memoire 60000, faux .dol 30000, carte A 40000 + 50000.
#   mkfat_sparse.py image.gcb sd.img selfdol.dol
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gcb
g = gcb.lire(sys.argv[1]); out = sys.argv[2]; selfdol = open(sys.argv[3], 'rb').read()
SPC = 64; CS = SPC * 512; ISO_CL = 70000
niso = (g['iso'] + CS - 1) // CS
ncl = ISO_CL + niso + 16
part = 2048; rsvd = 32; fatsz = (ncl + 2) * 4 // 512 + 1
data = rsvd + 2 * fatsz; tot = data + ncl * SPC
img = open(out, 'wb'); img.truncate((part + tot) * 512)
def w(sec, b): img.seek(sec * 512); img.write(b)
m = bytearray(512); m[0x1BE + 4] = 0x0C; struct.pack_into('<II', m, 0x1BE + 8, part, tot); m[510:512] = b'\x55\xAA'; w(0, m)
bs = bytearray(512); bs[0:3] = b'\xEB\x58\x90'; bs[3:11] = b'MSWIN4.1'
struct.pack_into('<HBHBHHBH', bs, 0x0B, 512, SPC, rsvd, 2, 0, 0, 0xF8, 0)
struct.pack_into('<I', bs, 0x20, tot); struct.pack_into('<I', bs, 0x24, fatsz); struct.pack_into('<I', bs, 0x2C, 2); bs[0x52:0x5A] = b'FAT32   '
bs[510:512] = b'\x55\xAA'; w(part, bs)
fat = {}
def chain(cls):
    for i, c in enumerate(cls): fat[c] = cls[i + 1] if i + 1 < len(cls) else 0x0FFFFFFF
def put(c0, blob):
    w(part + data + (c0 - 2) * SPC, blob)
chain(list(range(ISO_CL, ISO_CL + niso)))
base_iso = (part + data + (ISO_CL - 2) * SPC) * 512
for off, d in gcb.morceaux(g): img.seek(base_iso + off); img.write(d)
chain([20000]); chain(list(range(60000, 60000 + (704 * 512 + CS - 1) // CS)))
n2 = (len(selfdol) + CS - 1) // CS; chain(list(range(30000, 30000 + n2))); put(30000, selfdol)

# carte memoire de 2 Mo (16 Mbits, 256 blocs de 8 Ko), formatee comme le SDK (__CARDFormatRegion), ID de la SRAM = 0, date 0
def cks(b):
    s = si = 0
    for i in range(0, len(b), 2):
        v = (b[i] << 8) | b[i + 1]; s = (s + v) & 0xFFFF; si = (si + (v ^ 0xFFFF)) & 0xFFFF
    if s == 0xFFFF: s = 0
    if si == 0xFFFF: si = 0
    return s, si
card = bytearray(b'\xFF' * (2 << 20))
hd = bytearray(b'\xFF' * 0x2000); hd[0:0x200] = bytes(0x200)
struct.pack_into('>HHH', hd, 0x20, 0, 16, 0)            # deviceID, taille (Mbits), encodage
s, si = cks(hd[:0x1FC]); struct.pack_into('>HH', hd, 0x1FC, s, si)
card[0:0x2000] = hd
for k, cnt in ((1, 0), (2, 1)):                         # repertoires
    d = bytearray(b'\xFF' * 0x2000); struct.pack_into('>h', d, 0x1FFA, cnt)
    s, si = cks(d[:0x1FFC]); struct.pack_into('>HH', d, 0x1FFC, s, si); card[k * 0x2000:(k + 1) * 0x2000] = d
for k, cnt in ((3, 0), (4, 1)):                         # tables des blocs
    b = bytearray(0x2000); struct.pack_into('>hHH', b, 4, cnt, 256 - 5, 4)
    s, si = cks(b[4:]); struct.pack_into('>HH', b, 0, s, si); card[k * 0x2000:(k + 1) * 0x2000] = b
ncard = len(card) // CS
cl = [40000 + i for i in range(ncard // 2)] + [50000 + i for i in range(ncard - ncard // 2)]
chain(cl)
for i, c in enumerate(cl): put(c, bytes(card[i * CS:(i + 1) * CS]))
for f in range(2):
    base = part + rsvd + f * fatsz
    for c, v in fat.items():
        img.seek(base * 512 + c * 4); img.write(struct.pack('<I', v))
img.close()
open(out + '.iso', 'w').write('%d\n' % (base_iso // 512))   # (1er secteur de l'ISO sur la carte : pour emu_game.py)
print(ISO_CL, g['iso'])
