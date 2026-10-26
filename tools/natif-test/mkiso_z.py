# v152 : faux Rogue Squadron III : main.dol = le faux jeu + en 0x80200000 {taille compressee, taille, zlib(2e programme)} ;
# le 2e programme (.dol, texte en 0x80300000) contient des acces au lecteur que GC Station doit brancher.
import struct, subprocess, sys, zlib
sys.argv = [sys.argv[0], sys.argv[1], sys.argv[2]]
inner_txt = struct.pack('>' + 'I' * 8, 0x3D20CC00, 0x80096000, 0x90696008, 0x3D40CC00, 0x810A6010, 0x91AA6014, 0x60000000, 0x4E800020)
inner_txt += bytes(range(256)) * 64                     # (de quoi compresser)
ih = bytearray(0x100); struct.pack_into('>I', ih, 0, 0x100); struct.pack_into('>I', ih, 0x48, 0x80300000); struct.pack_into('>I', ih, 0x90, len(inner_txt))
struct.pack_into('>I', ih, 0xE0, 0x80300000)
inner = bytes(ih) + inner_txt
z = zlib.compress(inner, 6)
blob = struct.pack('>II', len(z), len(inner)) + z
blob += bytes(-len(blob) % 32)
text = open(sys.argv[1], 'rb').read()
import importlib.util, os, random
dol = bytearray(0x100); struct.pack_into('>I', dol, 0, 0x100); struct.pack_into('>I', dol, 0x48, 0x80003100); struct.pack_into('>I', dol, 0x90, len(text))
doff = 0x100 + len(text); doff += -doff % 32
struct.pack_into('>I', dol, 0x1C, doff); struct.pack_into('>I', dol, 0x64, 0x80200000); struct.pack_into('>I', dol, 0xAC, len(blob))
struct.pack_into('>III', dol, 0xD8, 0x80004000, 0x100, 0x80003100)
dol += text; dol += bytes(doff - len(dol)); dol += blob
random.seed(11)
iso = bytearray(random.getrandbits(8) for _ in range(0x80000))
iso[0:0x440] = bytes(0x440); iso[0:6] = b'GLRE64'; struct.pack_into('>I', iso, 0x1C, 0xC2339F3D)
iso[0x20:0x40] = b'Faux Rogue Squadron III'.ljust(32, b'\0')
struct.pack_into('>IIII', iso, 0x420, 0x40000, 0x70000, 0x40, 0x40)
iso[0x440:0x2440] = bytes(0x2000); struct.pack_into('>I', iso, 0x440 + 0x18, 1)
iso[0x2440:0x2460] = bytes(0x20); iso[0x2440:0x244A] = b'2003/10/01'; struct.pack_into('>III', iso, 0x2450, 0x81200000, 0x1000, 0x60)
assert 0x40000 + len(dol) < 0x70000
iso[0x40000:0x40000 + len(dol)] = dol
f = 0x70000; iso[f:f + 0x40] = bytes(0x40)
struct.pack_into('>BxxxII', iso, f, 1, 0, 3); struct.pack_into('>BxxxII', iso, f + 12, 0, 0x100, 4); struct.pack_into('>BxxxII', iso, f + 24, 1, 0, 3)
open(sys.argv[2], 'wb').write(iso)
