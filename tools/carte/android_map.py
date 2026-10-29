#!/usr/bin/env python3
# v253 : texture de la Terre du globe des radios de l'appli Android, tiree de la carte du globe de la console
# (source/globe_map.h : Natural Earth 1:50m, domaine public). Equirectangulaire 2048 x 1024, niveaux de gris
# (blanc = terre, bords adoucis).  python3 tools/carte/android_map.py source/globe_map.h android/app/src/main/res/drawable-nodpi/terre.png
import re, sys, zlib
from PIL import Image
src = open(sys.argv[1]).read()
data = bytes(int(v) for v in re.findall(r'\d+', src.split('{', 1)[1].split('}', 1)[0]))
raw = zlib.decompress(data)
img = Image.new('L', (2048, 1024)); px = img.load()
i = 0
for half in range(2):                                  # deux textures I4 1024 x 1024 (ouest, est), tuiles 8 x 8
    x0 = half * 1024
    for ty in range(0, 1024, 8):
        for tx in range(0, 1024, 8):
            for y in range(8):
                for x in range(0, 8, 2):
                    b = raw[i]; i += 1
                    px[x0 + tx + x, ty + y] = (b >> 4) * 17
                    px[x0 + tx + x + 1, ty + y] = (b & 15) * 17
img.save(sys.argv[2], optimize=True)
