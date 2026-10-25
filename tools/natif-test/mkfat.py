# image de carte SD FAT32 / FAT16 (MBR + 1 partition) avec un ISO fragmente dedans
import struct, sys, random
kind, iso_path, out, mbr = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == '1'
iso = open(iso_path, 'rb').read()
part = 2048 if mbr else 0
if kind == '32': spc, ncl, fatsz_e = 1, 70000, 4
else: spc, ncl, fatsz_e = 4, 20000, 2
rsvd = 32 if kind == '32' else 4
fatsz = (ncl + 2) * fatsz_e // 512 + 1
rootent = 0 if kind == '32' else 512
rootsec = rootent * 32 // 512
data = rsvd + 2 * fatsz + rootsec
tot = data + ncl * spc
img = open(out, 'wb'); img.truncate((part + tot) * 512)
def w(sec, b): img.seek(sec * 512); img.write(b)
if mbr:
    m = bytearray(512); m[0x1BE + 4] = 0x0C; struct.pack_into('<II', m, 0x1BE + 8, part, tot); m[510:512] = b'\x55\xAA'; w(0, m)
bs = bytearray(512); bs[0:3] = b'\xEB\x58\x90'; bs[3:11] = b'MSWIN4.1'
struct.pack_into('<HBHBHHBH', bs, 0x0B, 512, spc, rsvd, 2, rootent, 0, 0xF8, 0 if kind == '32' else fatsz)
struct.pack_into('<I', bs, 0x20, tot)
if kind == '32': struct.pack_into('<I', bs, 0x24, fatsz); struct.pack_into('<I', bs, 0x2C, 2); bs[0x52:0x5A] = b'FAT32   '
else: bs[0x36:0x3E] = b'FAT16   '
bs[510:512] = b'\x55\xAA'; w(part, bs)
csize = spc * 512
need = (len(iso) + csize - 1) // csize
random.seed(7)
# morceaux volontairement dans le desordre
pool = list(range(10, 10 + need // 3)) + list(range(5000, 5000 + need // 3)) + list(range(900, 900 + need - 2 * (need // 3)))
fat = {}
for i, c in enumerate(pool):
    fat[c] = pool[i + 1] if i + 1 < len(pool) else (0x0FFFFFFF if kind == '32' else 0xFFFF)
    blk = iso[i * csize:(i + 1) * csize]
    w(part + data + (c - 2) * spc, blk)
for f in range(2):
    base = part + rsvd + f * fatsz
    for c, v in fat.items():
        off = c * fatsz_e
        img.seek((base * 512) + off); img.write(struct.pack('<I' if kind == '32' else '<H', v))
# v79 : journal du jeu (2 secteurs) aux clusters 20000 et suivants
nj = (1536 + csize - 1) // csize
for i in range(nj):
    c = 20000 + i
    nxt = 20000 + i + 1 if i + 1 < nj else (0x0FFFFFFF if kind == '32' else 0xFFFF)
    for f in range(2):
        base = part + rsvd + f * fatsz
        img.seek(base * 512 + c * fatsz_e); img.write(struct.pack('<I' if kind == '32' else '<H', nxt))
# v83 : photo de la memoire au plantage (704 secteurs) aux clusters 60000 et suivants
ns = (704 * 512 + csize - 1) // csize
for i in range(ns):
    c = 60000 + i
    nxt = 60000 + i + 1 if i + 1 < ns else (0x0FFFFFFF if kind == '32' else 0xFFFF)
    for f in range(2):
        base = part + rsvd + f * fatsz
        img.seek(base * 512 + c * fatsz_e); img.write(struct.pack('<I' if kind == '32' else '<H', nxt))
# v64 : 2e fichier (faux .dol de GC Station) aux clusters 30000... (a la suite)
if len(sys.argv) > 5:
    extra = open(sys.argv[5], 'rb').read()
    n2 = (len(extra) + csize - 1) // csize
    for i in range(n2):
        c = 30000 + i
        fat2 = 30000 + i + 1 if i + 1 < n2 else (0x0FFFFFFF if kind == '32' else 0xFFFF)
        w(part + data + (c - 2) * spc, extra[i * csize:(i + 1) * csize].ljust(csize, b'\0'))
        for f in range(2):
            base = part + rsvd + f * fatsz
            img.seek(base * 512 + c * fatsz_e); img.write(struct.pack('<I' if kind == '32' else '<H', fat2))
# v65 : 3e fichier : la carte memoire virtuelle (2 Mo) en 2 morceaux, clusters 40000... et 50000...
# (contenu reconnaissable : octet = (secteur * 7 + position) & 0xFF)
if len(sys.argv) > 6:
    size = int(sys.argv[6])
    card = bytes(((o >> 9) * 7 + o) & 0xFF for o in range(size))
    n3 = size // csize
    cl = [40000 + i for i in range(2000 // spc)] + [50000 + i for i in range(n3 - 2000 // spc)]
    for i, c in enumerate(cl):
        nxt = cl[i + 1] if i + 1 < len(cl) else (0x0FFFFFFF if kind == '32' else 0xFFFF)
        w(part + data + (c - 2) * spc, card[i * csize:(i + 1) * csize])
        for f in range(2):
            base = part + rsvd + f * fatsz
            img.seek(base * 512 + c * fatsz_e); img.write(struct.pack('<I' if kind == '32' else '<H', nxt))
# v121 : 4e fichier : l'autre disque d'un jeu a 2 disques (clusters 10000... a la suite)
if len(sys.argv) > 7:
    d2 = open(sys.argv[7], 'rb').read()
    n4 = (len(d2) + csize - 1) // csize
    for i in range(n4):
        c = 10000 + i
        nxt = c + 1 if i + 1 < n4 else (0x0FFFFFFF if kind == '32' else 0xFFFF)
        w(part + data + (c - 2) * spc, d2[i * csize:(i + 1) * csize].ljust(csize, b'\0'))
        for f in range(2):
            base = part + rsvd + f * fatsz
            img.seek(base * 512 + c * fatsz_e); img.write(struct.pack('<I' if kind == '32' else '<H', nxt))
# v122 : 5e fichier : la carte du slot B (clusters 55000... a la suite)
if len(sys.argv) > 8:
    size = int(sys.argv[8])
    card = bytes(((o >> 9) * 7 + o) & 0xFF for o in range(size))
    n5 = size // csize
    for i in range(n5):
        c = 55000 + i
        nxt = c + 1 if i + 1 < n5 else (0x0FFFFFFF if kind == '32' else 0xFFFF)
        w(part + data + (c - 2) * spc, card[i * csize:(i + 1) * csize])
        for f in range(2):
            base = part + rsvd + f * fatsz
            img.seek(base * 512 + c * fatsz_e); img.write(struct.pack('<I' if kind == '32' else '<H', nxt))
img.close()
print(pool[0], len(iso))
