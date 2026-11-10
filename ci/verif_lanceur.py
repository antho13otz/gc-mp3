# Verifie le lanceur de jeux (point d'entree, bloc _arg, sections, taille). Usage : python3 -I ci/verif_lanceur.py gc-mp3-loader.bin
import struct, sys
d = open(sys.argv[1], "rb").read()
e = struct.unpack_from(">I", d, 0xE0)[0]
ok = False
for i in range(18):
    off, adr, n = [struct.unpack_from(">I", d, b + i * 4)[0] for b in (0, 0x48, 0x90)]
    if not n:
        continue
    if off < 0x100 or off + n > len(d) or adr < 0x80000000 or adr + n > 0x81800000:
        sys.exit("ERREUR: section invalide")
    if i < 7 and adr <= e < adr + n and d[off + e + 4 - adr:off + e + 8 - adr] == b"_arg":
        ok = True
if not ok or len(d) > 8 * 1024 * 1024:
    sys.exit("ERREUR: lanceur invalide (point d'entree / _arg / taille)")
print("lanceur valide :", len(d), "octets")
