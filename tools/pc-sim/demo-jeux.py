#!/usr/bin/env python3
# v92 : beaucoup de faux jeux GameCube pour mesurer l'onglet JEUX dans le simulateur.
# Usage : python3 tools/pc-sim/demo-jeux.py <dossier sd:> [nombre=120]
# Cree <sd>/jeux-test/*.iso (fichiers creux : en-tete, FST avec des dossiers et opening.bnr)
# et <sd>/gc-mp3/jaquettes/posters.pak + stills.pak (format SWPK v1, CMPR).
import os, struct, sys, random
sd = sys.argv[1]; N = int(sys.argv[2]) if len(sys.argv) > 2 else 120
random.seed(5)
os.makedirs(os.path.join(sd, "jeux-test"), exist_ok=True)
os.makedirs(os.path.join(sd, "gc-mp3", "jaquettes"), exist_ok=True)
noms = ["Aventure", "Course", "Football", "Combat", "Puzzle", "Plateforme", "Tennis", "Golf", "Espace", "Pirates", "Robots", "Dragons"]
ids = []
def bnr(title, color):
    b = bytearray(0x1960); b[0:4] = b"BNR1"
    px = struct.pack(">H", color)
    for i in range(96 * 32): b[0x20 + i * 2:0x22 + i * 2] = px
    t = title.encode("latin-1")[:0x3F]
    b[0x1820:0x1820 + len(t)] = t; b[0x1820 + 0x40:0x1820 + 0x40 + len(t)] = t
    d = ("Un faux jeu pour les tests : " + title).encode("latin-1")[:0x7F]
    b[0x1820 + 0xC0:0x1820 + 0xC0 + len(d)] = d
    return bytes(b)
for i in range(N):
    gid = "G%02d%sXY" % (i % 100, "EPJ"[i % 3]) if False else ("G%03d" % i)[:4] + "EP"[i % 2] + "1"
    gid = ("T%03d" % i) + "E" + "1"
    gid = gid[:6]
    ids.append(gid)
    title = "%s %d" % (noms[i % len(noms)], i + 1)
    # FST : racine, 40 dossiers de 30 fichiers, puis opening.bnr a la fin (cas lent)
    ents = []; names = bytearray()
    def name(s):
        o = len(names); names.extend(s.encode() + b"\0"); return o
    ents.append([1, 0, 0, 0])
    for dnum in range(40):
        di = len(ents); ents.append([1, name("dir%02d" % dnum), 0, 0])
        for fnum in range(30): ents.append([0, name("f%02d.bin" % fnum), 0x100000, 16])
        ents[di][3] = len(ents)
    ents.append([0, name("opening.bnr"), 0x200000, 0x1960])
    ents[0][3] = len(ents)
    fst = bytearray()
    for t, no, a, b in ents: fst += struct.pack(">II I", (t << 24) | no, a, b)
    fst += names
    h = bytearray(0x440); h[0:6] = gid.encode(); struct.pack_into(">I", h, 0x1C, 0xC2339F3D)
    h[0x20:0x20 + len(title)] = title.encode()
    struct.pack_into(">III", h, 0x424, 0x300000, len(fst), len(fst))
    p = os.path.join(sd, "jeux-test", "%s.iso" % title)
    with open(p, "wb") as f:
        f.write(h); f.seek(0x200000); f.write(bnr(title, 0x8000 | random.getrandbits(15)))
        f.seek(0x300000); f.write(fst); f.truncate(0x400000)
def cmpr(w, h, c0, c1):
    blk = struct.pack(">HH", c0, c1) + bytes([0x1B, 0x6C, 0xB1, 0xE4])
    return blk * (w * h // 16)
def pack(path, cw, ch, iw, ih, levels):
    ids_s = sorted(ids); n = len(ids_s)
    rs = sum((cw >> l) * (ch >> l) // 2 for l in range(levels))
    hd = bytearray(64); hd[0:4] = b"SWPK"; struct.pack_into(">I", hd, 4, 1); struct.pack_into(">I", hd, 0x0C, n)
    struct.pack_into(">II", hd, 0x10, 64, n * 32); struct.pack_into(">I", hd, 0x20, rs)
    struct.pack_into(">HHHH", hd, 0x24, cw, ch, iw, ih); hd[0x2C] = levels; hd[0x2D] = 14
    idx = bytearray(); data = bytearray(); base = 64 + n * 32
    for g in ids_s:
        e = bytearray(32); e[0:6] = g.encode(); struct.pack_into(">I", e, 8, base + len(data)); idx += e
        c0 = random.getrandbits(16) | 0x8000; c1 = random.getrandbits(15)
        for l in range(levels): data += cmpr(cw >> l, ch >> l, c0, c1)
    open(path, "wb").write(bytes(hd) + bytes(idx) + bytes(data))
pack(os.path.join(sd, "gc-mp3", "jaquettes", "posters.pak"), 256, 256, 192, 256, 2)
pack(os.path.join(sd, "gc-mp3", "jaquettes", "stills.pak"), 320, 240, 320, 240, 1)
print(N, "jeux")
