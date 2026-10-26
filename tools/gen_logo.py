#!/usr/bin/env python3
# GC Station v159 : le logo (docs/logo/icone-N.png) -> source/logo_gcs.h : textures RGB5A3 en tuiles 4x4
# (format des textures de la GameCube, avec transparence), en 128, 64 et 32 points.
import sys
from PIL import Image
out = ['/* Genere par tools/gen_logo.py depuis docs/logo/icone-*.png : ne pas modifier a la main */']
for n in (128, 64, 32):
    im = Image.open('docs/logo/icone-%d.png' % n).convert('RGBA')
    assert im.size == (n, n)
    px = im.load()
    words = []
    for ty in range(0, n, 4):
        for tx in range(0, n, 4):
            for y in range(4):
                for x in range(4):
                    r, g, b, a = px[tx + x, ty + y]
                    if a >= 0xE0: v = 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
                    else: v = (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)
                    words.append(v)
    out.append('static const u16 logo_gcs_%d[%d * %d] ATTRIBUTE_ALIGN(32) = {' % (n, n, n))
    for i in range(0, len(words), 16): out.append('    ' + ', '.join('0x%04X' % w for w in words[i:i + 16]) + ',')
    out.append('};')
open('source/logo_gcs.h', 'w').write('\n'.join(out) + '\n')
print('source/logo_gcs.h : %d octets de textures' % (2 * (128 * 128 + 64 * 64 + 32 * 32)))
