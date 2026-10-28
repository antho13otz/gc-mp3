#!/usr/bin/env python3
# v208 : carte du monde du globe des radios (source/globe_map.h).
# Donnees : Natural Earth 1:50m (terres, lacs), domaine public : https://www.naturalearthdata.com
#   python3 tools/carte/globe_map.py ne_50m_land.geojson ne_50m_lakes.geojson > source/globe_map.h
# Carte equirectangulaire 2048 x 1024 (deux textures 1024 x 1024, ouest puis est), format GX I4
# (16 niveaux : bords adoucis), compressee (zlib) ; decompressee a la premiere ouverture du globe.
import json, sys, zlib
from PIL import Image, ImageDraw
W, H, SS = 2048, 1024, 4
def polys(path):
    g = json.load(open(path))
    for f in g["features"]:
        geo = f["geometry"]
        for p in (geo["coordinates"] if geo["type"] == "MultiPolygon" else [geo["coordinates"]]):
            yield p
def draw(img, path, fill_outer, fill_hole):
    d = ImageDraw.Draw(img)
    for p in polys(path):
        for k, ring in enumerate(p):
            pts = [((lon + 180) / 360 * W * SS, (90 - lat) / 180 * H * SS) for lon, lat in ring]
            if len(pts) > 2: d.polygon(pts, fill=fill_outer if k == 0 else fill_hole)
big = Image.new("L", (W * SS, H * SS), 0)
draw(big, sys.argv[1], 255, 0)
if len(sys.argv) > 2: draw(big, sys.argv[2], 0, 255)
img = big.resize((W, H), Image.LANCZOS)
px = img.load()
out = bytearray()
for half in range(2):
    x0 = half * 1024
    for ty in range(0, 1024, 8):
        for tx in range(0, 1024, 8):
            for y in range(8):
                for x in range(0, 8, 2):
                    a = px[x0 + tx + x, ty + y] >> 4; b = px[x0 + tx + x + 1, ty + y] >> 4
                    out.append((a << 4) | b)
z = zlib.compress(bytes(out), 9)
print("// GC Station : carte du monde du globe des radios (genere par tools/carte/globe_map.py, ne pas modifier).")
print("// Natural Earth 1:50m, terres et lacs : domaine public (www.naturalearthdata.com).")
print("#define GLOBE_MAP_RAW %d   /* deux textures I4 1024 x 1024 */" % len(out))
print("#define GLOBE_MAP_Z %d" % len(z))
print("static const unsigned char globe_map_z[GLOBE_MAP_Z] = {")
for i in range(0, len(z), 24): print("  " + ",".join(str(c) for c in z[i:i + 24]) + ",")
print("};")
