# faux ISO GameCube autour du code de game.s
import struct, random, sys
text = open(sys.argv[1], 'rb').read()
dol = bytearray(0x100); struct.pack_into('>I', dol, 0, 0x100); struct.pack_into('>I', dol, 0x48, 0x80003100); struct.pack_into('>I', dol, 0x90, len(text))
struct.pack_into('>III', dol, 0xD8, 0x80004000, 0x100, 0x80003100); dol += text
random.seed(11)
iso = bytearray(random.getrandbits(8) for _ in range(0x80000))
iso[0:0x440] = bytes(0x440); iso[0:6] = b'GE2E01'; struct.pack_into('>I', iso, 0x1C, 0xC2339F3D)
iso[0x20:0x34] = b'Faux jeu de test E2E'
struct.pack_into('>IIII', iso, 0x420, 0x60000, 0x70000, 0x40, 0x40)
iso[0x440:0x2440] = bytes(0x2000); struct.pack_into('>I', iso, 0x440 + 0x18, 1)
iso[0x2440:0x2460] = bytes(0x20); iso[0x2440:0x244A] = b'2026/10/22'; struct.pack_into('>III', iso, 0x2450, 0x81200000, 0x1000, 0)
iso[0x60000:0x60000 + len(dol)] = dol
f = 0x70000; iso[f:f + 0x40] = bytes(0x40)
struct.pack_into('>BxxxII', iso, f, 1, 0, 3); struct.pack_into('>BxxxII', iso, f + 12, 0, 0x100, 4); struct.pack_into('>BxxxII', iso, f + 24, 1, 0, 3)
open(sys.argv[2], 'wb').write(iso)
