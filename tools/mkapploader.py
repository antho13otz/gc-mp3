#!/usr/bin/env python3
# Fabrique gc-mp3-igr.img : le fichier swiss/patches/apploader.img qui ramene sur GC-MP3
# quand on fait A + Z + START en jeu (Indigo / Swiss, reglage "Retour en jeu" = Apploader).
#
# Meme format que l'apploader.img d'Indigo / Swiss (outil dol2ipl.py et "reboot.dol" du
# packer de Swiss, GPL v2, (c) Extrems) : en-tete de 32 octets, puis un petit programme
# charge en 0x81300000 qui decompresse (xz) le programme et le lance en 0x80003100.
# tools/igr_stub.bin = ce petit programme (0x3720 octets) + ses donnees de fin (140 octets),
# repris tels quels ; seul le programme compresse au milieu change (GC-MP3 au lieu d'Indigo).
#
# Usage : python3 tools/mkapploader.py gc-mp3player.dol sortie.img NUMERO_DE_BUILD
import lzma, struct, sys, zlib

CODE = 0x3720            # taille du petit programme (le flux xz commence juste apres)
ROOM = 0x259C74          # place du flux xz (les donnees de fin doivent rester au meme endroit)
IN_SIZE, OUT_SIZE = 0x5C, 0x68   # dans les donnees de fin : taille du flux xz, taille decompressee

dol, out, build = sys.argv[1], sys.argv[2], int(sys.argv[3])
stub = open(sys.argv[4] if len(sys.argv) > 4 else "tools/igr_stub.bin", "rb").read()
code, tail = stub[:CODE], bytearray(stub[CODE:])
assert len(tail) == 140, "igr_stub.bin inattendu"
assert struct.unpack_from(">I", tail, IN_SIZE - 8)[0] == 0x01303720, "igr_stub.bin inattendu (adresse du flux)"

d = open(dol, "rb").read()
h = struct.unpack(">64I", d[:256])
offs, addrs, sizes, entry = h[:18], h[18:36], h[36:54], h[56]
lo = min(a for a, s in zip(addrs, sizes) if s)
hi = max(a + s for a, s in zip(addrs, sizes) if s)
if lo != 0x80003100 or entry != 0x80003100:
    sys.exit("ERREUR : le .dol doit commencer en 0x80003100 (debut %08x, entree %08x)" % (lo, entry))
flat = bytearray(hi - lo)
for o, a, s in zip(offs, addrs, sizes):
    if s:
        flat[a - lo:a - lo + s] = d[o:o + s]

xz = lzma.compress(bytes(flat), format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32,
                   filters=[{"id": lzma.FILTER_POWERPC}, {"id": lzma.FILTER_LZMA2, "preset": 9 | lzma.PRESET_EXTREME}])
if len(xz) > ROOM:
    sys.exit("ERREUR : GC-MP3 compresse fait %d octets, place disponible %d" % (len(xz), ROOM))
assert lzma.decompress(xz) == bytes(flat)
struct.pack_into(">I", tail, IN_SIZE, len(xz))
struct.pack_into(">I", tail, OUT_SIZE, len(flat))
img = code + xz + b"\0" * (ROOM - len(xz)) + bytes(tail)
name = ("*gc-mp3-b%d" % build).encode()[:16]
hdr = struct.pack(">16s8xII", name, len(img), zlib.crc32(img) & 0xFFFFFFFF)
open(out, "wb").write(hdr + img)
print("apploader GC-MP3 : %d octets (programme %d -> %d compresses, %d %% de la place)" % (len(hdr) + len(img), len(flat), len(xz), len(xz) * 100 // ROOM))
