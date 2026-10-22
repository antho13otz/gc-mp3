#!/usr/bin/env python3
"""Genere source/font_atlas.h : atlas de polices en texture GX I4 (tuiles 8x8,
4 bits par texel) + tables de glyphes pour GC-MP3.
Polices : DejaVu Sans (licence libre Bitstream Vera / DejaVu).
Usage : python3 gen_font.py chemin/vers/font_atlas.h [dossier_polices]"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

D = sys.argv[2] if len(sys.argv) > 2 else '/usr/share/fonts/truetype/dejavu/'
if not D.endswith('/'):
    D += '/'
FONTS = [  # (fichier, taille, jeu de caracteres)  0=SMALL 1=MED 2=LARGE 3=LOGO
    (D + 'DejaVuSansCondensed.ttf',      12, 'full'),
    (D + 'DejaVuSansCondensed.ttf',      15, 'full'),
    (D + 'DejaVuSansCondensed-Bold.ttf', 22, 'full'),
    (D + 'DejaVuSans-BoldOblique.ttf',   34, 'GC-MP3 STAION'),   # v47 : logo GC STATION
]
ATLAS_W = 512
GAP = 2                      # marge entre glyphes (evite les fuites du filtrage bilineaire)


def charset(kind):
    if kind == 'full':
        return list(range(32, 127)) + list(range(160, 256))
    return [ord(c) for c in kind]


ALL = charset('full')        # 191 caracteres : ASCII + Latin-1 (accents francais)
NG = len(ALL)
index_of = {cp: i for i, cp in enumerate(ALL)}

glyphs = {}                  # (police, index) -> (bitmap | None, xoff, yoff, avance)
metrics = []
for fi, (path, size, cs) in enumerate(FONTS):
    font = ImageFont.truetype(path, size)
    asc, desc = font.getmetrics()
    metrics.append((asc, asc + desc))
    wanted = set(charset(cs))
    for cp in ALL:
        ch = chr(cp)
        adv = int(round(font.getlength(ch)))
        if cp not in wanted:
            glyphs[(fi, index_of[cp])] = (None, 0, 0, adv)
            continue
        pad = 6
        W = adv + size + 2 * pad
        H = asc + desc + 2 * pad
        im = Image.new('L', (W, H), 0)
        ImageDraw.Draw(im).text((pad, pad), ch, font=font, fill=255, anchor='la')
        a = np.array(im)
        ys, xs = np.nonzero(a)
        if len(xs) == 0:
            glyphs[(fi, index_of[cp])] = (None, 0, 0, adv)
            continue
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        glyphs[(fi, index_of[cp])] = (a[y0:y1, x0:x1], int(x0 - pad), int(y0 - pad), adv)

# --- rangement en etageres, du plus haut au plus bas ---
items = sorted(((k, v) for k, v in glyphs.items() if v[0] is not None),
               key=lambda kv: -kv[1][0].shape[0])
pos = {}
x, y, shelf = GAP, GAP, 0
for key, (bm, *_rest) in items:
    h, w = bm.shape
    if x + w + GAP > ATLAS_W:
        x = GAP
        y += shelf + GAP
        shelf = 0
    pos[key] = (x, y)
    x += w + GAP
    shelf = max(shelf, h)
used_h = y + shelf + GAP
ATLAS_H = 8
while ATLAS_H < used_h:      # hauteur en puissance de 2 (le plus compatible)
    ATLAS_H *= 2

atlas = np.zeros((ATLAS_H, ATLAS_W), dtype=np.uint8)
for key, (bm, *_rest) in items:
    px, py = pos[key]
    h, w = bm.shape
    atlas[py:py + h, px:px + w] = bm
q = np.rint(atlas / 17.0).astype(np.uint8)          # 0..15

# --- format I4 GX : tuiles 8x8, texel de gauche = quartet de poids fort ---
tiles = []
for ty in range(0, ATLAS_H, 8):
    for tx in range(0, ATLAS_W, 8):
        t = q[ty:ty + 8, tx:tx + 8]
        tiles.append(((t[:, 0::2] << 4) | t[:, 1::2]).reshape(-1))
data = np.concatenate(tiles).astype(np.uint8)
assert len(data) == ATLAS_W * ATLAS_H // 2
words = [int.from_bytes(bytes(data[i:i + 4]), 'big') for i in range(0, len(data), 4)]

o = []
o.append('// Genere par tools/gen_font.py - ne pas modifier a la main.\n')
o.append('// Polices : DejaVu Sans (licence libre Bitstream Vera / DejaVu).\n')
o.append('#ifndef FONT_ATLAS_H\n#define FONT_ATLAS_H\n')
o.append('#define FONT_TEX_W %d\n#define FONT_TEX_H %d\n#define FONT_NGLYPHS %d\n#define FONT_COUNT %d\n'
         % (ATLAS_W, ATLAS_H, NG, len(FONTS)))
o.append('typedef struct { u16 x, y; u8 w, h; s8 xoff, yoff; u8 adv, pad; } fglyph_t;\n')
o.append('static const u8 font_ascent[FONT_COUNT] = {%s};\n' % ','.join(str(m[0]) for m in metrics))
o.append('static const u8 font_height[FONT_COUNT] = {%s};\n' % ','.join(str(m[1]) for m in metrics))
o.append('static const fglyph_t font_glyphs[FONT_COUNT][FONT_NGLYPHS] = {\n')
for fi in range(len(FONTS)):
    row = []
    for gi in range(NG):
        bm, xo, yo, adv = glyphs[(fi, gi)]
        if bm is None:
            row.append('{0,0,0,0,0,0,%d,0}' % adv)
        else:
            px, py = pos[(fi, gi)]
            h, w = bm.shape
            row.append('{%d,%d,%d,%d,%d,%d,%d,0}' % (px, py, w, h, xo, yo, adv))
    o.append('{' + ','.join(row) + '},\n')
o.append('};\n')
o.append('static const u32 font_tex[%d] ATTRIBUTE_ALIGN(32) = {\n' % len(words))
for i in range(0, len(words), 12):
    o.append(','.join('0x%08X' % w for w in words[i:i + 12]) + ',\n')
o.append('};\n#endif\n')
with open(sys.argv[1], 'w') as f:
    f.write(''.join(o))
print('atlas %dx%d (%d octets), %d glyphes' % (ATLAS_W, ATLAS_H, len(data), len(items)))
