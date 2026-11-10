# .dol de la console : aligne sur les blocs de lecture de Swiss (128 Kio), numero de build dans les 32 derniers octets
# (v92, lu par GC Station sans relire le fichier), puis verification (sections, alignement, point d'entree).
# Usage : python3 -I ci/dol.py fichier.dol NUMERO
import os, struct, sys
path, build = sys.argv[1], sys.argv[2]
block = 131072
size = os.path.getsize(path)
if size < 0x100:
    sys.exit("ERREUR: DOL trop petit")
padded = ((size + 32 + block - 1) // block) * block
tag = ("GCSTATION-BUILD=%s" % build).encode().ljust(32, b"\0")
with open(path, "ab") as f:
    f.write(b"\0" * (padded - size - 32) + tag)
print("DOL aligne:", size, "->", padded)
if sys.argv[3:] == ["sans-verif"]:
    sys.exit(0)
data = open(path, "rb").read()
size = len(data)
if size < 0x100 or size % block:
    sys.exit("ERREUR: taille DOL invalide")
u = lambda fmt, off: struct.unpack_from(fmt, data, off)
t_off, d_off = u(">7I", 0x00), u(">11I", 0x1C)
t_adr, d_adr = u(">7I", 0x48), u(">11I", 0x64)
t_len, d_len = u(">7I", 0x90), u(">11I", 0xAC)
entry = u(">3I", 0xD8)[2]
secs = []
for kind, offs, adrs, lens in (("text", t_off, t_adr, t_len), ("data", d_off, d_adr, d_len)):
    for off, adr, n in zip(offs, adrs, lens):
        if n == 0:
            continue
        if off < 0x100 or off + n > size:
            sys.exit("ERREUR: section hors fichier")
        if off % 4 or adr % 4 or n % 4:
            sys.exit("ERREUR: section non alignee")
        if adr < 0x80000000 or adr + n > 0x81800000:
            sys.exit("ERREUR: section hors memoire")
        secs.append((kind, adr, adr + n))
if not secs:
    sys.exit("ERREUR: aucune section")
if not any(a <= entry < b for k, a, b in secs if k == "text"):
    sys.exit("ERREUR: point d'entree invalide")
secs.sort(key=lambda s: s[1])
for a, b in zip(secs, secs[1:]):
    if a[2] > b[1]:
        sys.exit("ERREUR: sections qui se chevauchent")
print("DOL valide:", size, "octets,", len(secs), "sections")
