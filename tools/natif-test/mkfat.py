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
img.close()
print(pool[0], len(iso))
