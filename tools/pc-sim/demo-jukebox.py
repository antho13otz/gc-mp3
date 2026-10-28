#!/usr/bin/env python3
# v207 : un faux jeu GameCube avec des musiques dedans (AST, HPS, DSP en paire, ADP, + un bruitage DSP)
# pour essayer le jukebox (fiche du jeu > bas). Les musiques sont des gammes de sinus (gauche / droite
# differentes) encodees ici en AFC / DSP ADPCM. Usage : python3 tools/pc-sim/demo-jukebox.py <dossier sd:>
import os, struct, sys
import numpy as np
sd = sys.argv[1]
AFC = [(0,0),(2048,0),(0,2048),(1024,1024),(4096,-2048),(3584,-1536),(3072,-1024),(4608,-2560),
       (4200,-2248),(4800,-2300),(5120,-3072),(2048,-2048),(1024,-1024),(-1024,1024),(-1024,0),(-2048,0)]
DSPC = [(0,0),(2048,0),(4096,-2048),(3584,-1536),(3072,-1024),(4608,-2560),(1024,1024),(3840,-1920)]

def tone(rate, secs, base):
    t = np.arange(int(rate * secs)) / rate
    notes = [base * 2 ** (k / 12) for k in (0, 2, 4, 5, 7, 9, 11, 12)]
    seg = len(t) // len(notes)
    f = np.repeat(notes, seg); f = np.concatenate([f, np.full(len(t) - len(f), notes[-1])])
    ph = np.cumsum(2 * np.pi * f / rate)
    return (np.sin(ph) * 12000).astype(np.int64)

def enc(x, n_per, coefs, scales, dec):        # cherche (indice, echelle) qui donne le moins d'erreur
    out = []; h1 = h2 = 0
    for i in range(0, len(x) - len(x) % n_per, n_per):
        fr = x[i:i + n_per]; best = None
        for ci, (c1, c2) in enumerate(coefs):
            for sc in scales:
                nib, s, e, a, b = dec(fr, c1, c2, sc, h1, h2)
                if best is None or e < best[0]: best = (e, ci, sc, nib, a, b)
        e, ci, sc, nib, h1, h2 = best
        out.append((ci, sc, nib))
    return out

def step(fr, c1, c2, sc, h1, h2, afc):
    nib = []; err = 0
    for v in fr:
        p = c1 * h1 + c2 * h2
        if afc: n = int(round(((int(v) << 11) - p) / ((1 << sc) << 11)))
        else: n = int(round(((int(v) << 11) - p - 1024) / (((1 << sc) * 2048))))
        n = max(-8, min(7, n))
        s = (((n * (1 << sc)) << 11) + p) >> 11 if afc else (n * (1 << sc) * 2048 + 1024 + p) >> 11
        s = max(-32768, min(32767, s))
        err += (s - v) ** 2; h2 = h1; h1 = s; nib.append(n & 15)
    return nib, None, err, h1, h2

def afc_bytes(x):
    b = bytearray()
    for ci, sc, nib in enc(x, 16, AFC, range(0, 13), lambda fr, c1, c2, sc, h1, h2: step(fr, c1, c2, sc, h1, h2, True)):
        b.append((sc << 4) | ci)
        for k in range(0, 16, 2): b.append((nib[k] << 4) | nib[k + 1])
    return bytes(b)

def dsp_bytes(x):
    b = bytearray()
    for ci, sc, nib in enc(x, 14, DSPC, range(0, 12), lambda fr, c1, c2, sc, h1, h2: step(fr, c1, c2, sc, h1, h2, False)):
        b.append((ci << 4) | sc)
        for k in range(0, 14, 2): b.append((nib[k] << 4) | nib[k + 1])
    return bytes(b)

def coefs_blob():
    return b"".join(struct.pack(">hh", c1, c2) for c1, c2 in DSPC)

def dsp_file(x, rate, loop):
    data = dsp_bytes(x); ns = len(data) // 8 * 14
    h = bytearray(0x60)
    struct.pack_into(">IIIHHIII", h, 0, ns, len(data) * 2, rate, 1 if loop else 0, 0, 2, len(data) * 2 - 1, 2)
    h[0x1C:0x3C] = coefs_blob()
    return bytes(h) + data

def ast_file(L, R, rate):
    a, b = afc_bytes(L), afc_bytes(R)
    per = 0x2760; blocks = bytearray(); ns = len(a) // 9 * 16
    for o in range(0, len(a), per):
        ca, cb = a[o:o + per], b[o:o + per]
        blocks += b"BLCK" + struct.pack(">I", len(ca)) + bytes(24) + ca + cb
    h = bytearray(0x40); h[0:4] = b"STRM"
    struct.pack_into(">IHHHHIIIII", h, 4, len(blocks), 0, 16, 2, 0xFFFF, rate, ns, ns // 4, ns, per)
    return bytes(h) + bytes(blocks)

def hps_file(L, R, rate):
    a, b = dsp_bytes(L), dsp_bytes(R)
    h = bytearray(0x80); h[0:8] = b" HALPST\0"
    struct.pack_into(">II", h, 8, rate, 2)
    for c in range(2):
        o = 0x10 + 0x38 * c
        struct.pack_into(">III", h, o, 0x10000, 2, len(a) * 2)
        h[o + 0x10:o + 0x30] = coefs_blob()
    per = 0x2000; offs = []; out = bytearray(); pos = 0x80
    chunks = [(a[o:o + per], b[o:o + per]) for o in range(0, len(a), per)]
    for i, (ca, cb) in enumerate(chunks):
        offs.append(pos); pos += 0x20 + len(ca) * 2
    for i, (ca, cb) in enumerate(chunks):
        nx = offs[i + 1] if i + 1 < len(chunks) else offs[1]          # boucle sur le 2e bloc
        out += struct.pack(">III", len(ca) * 2, len(ca) * 2 - 1, nx) + bytes(20) + ca + cb
    return bytes(h) + bytes(out)

files = [
    ("AudioRes/Stream/course_lumiere.ast", ast_file(tone(32000, 8, 440), tone(32000, 8, 660), 32000)),
    ("audio/menu_principal.hps", hps_file(tone(32000, 6, 330), tone(32000, 6, 495), 32000)),
    ("Audio/Songs/boss_L.dsp", dsp_file(tone(32000, 14, 220), 32000, True)),
    ("Audio/Songs/boss_R.dsp", dsp_file(tone(32000, 14, 277), 32000, True)),
    ("Audio/Sfx/saut.dsp", dsp_file(tone(32000, 0.3, 880), 32000, False)),
    ("audio/stage1.adp", bytes(256 * 1024)),
]
# image : en-tete, FST, fichiers
ents = [[1, 0, 0, 0]]; names = bytearray(b"\0")
def name(s):
    o = len(names); names.extend(s.encode() + b"\0"); return o
tree = {}
for p, d in files:
    parts = p.split("/"); node = tree
    for q in parts[:-1]: node = node.setdefault(q, {})
    node[parts[-1]] = d
data = bytearray(); base = 0x100000
def walk(node, parent):
    for k in sorted(node, key=str.lower):
        v = node[k]
        if isinstance(v, dict):
            i = len(ents); ents.append([1, name(k), parent, 0]); walk(v, i); ents[i][3] = len(ents)
        else:
            off = base + len(data); data.extend(v); data.extend(bytes((-len(data)) % 32))
            ents.append([0, name(k), off, len(v)])
walk(tree, 0); ents[0][3] = len(ents)
fst = b"".join(struct.pack(">III", (t << 24) | no, a, b) for t, no, a, b in ents) + bytes(names)
h = bytearray(0x440); h[0:6] = b"GJBX01"; struct.pack_into(">I", h, 0x1C, 0xC2339F3D)
t = b"Jukebox Test"; h[0x20:0x20 + len(t)] = t
struct.pack_into(">III", h, 0x424, 0x80000, len(fst), len(fst))
os.makedirs(os.path.join(sd, "games"), exist_ok=True)
p = os.path.join(sd, "games", "Jukebox Test.iso")
with open(p, "wb") as f:
    f.write(h); f.seek(0x80000); f.write(fst); f.seek(base); f.write(data)
print(p, len(files), "fichiers")
