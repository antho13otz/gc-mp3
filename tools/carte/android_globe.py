#!/usr/bin/env python3
# v255 : donnees du globe des radios de l'appli Android (android/app/src/main/res/raw/).
#   python3 tools/carte/android_globe.py <dossier geojson de natural-earth-vector> <dossier img de three-globe> <sortie res/raw>
# Sources (domaine public) :
#   - Natural Earth (www.naturalearthdata.com ; github.com/nvkelso/natural-earth-vector) : terres, lacs, cotes 1:10m,
#     frontieres 1:50m, pays et villes (noms en francais) ;
#   - NASA (Black Marble : lumieres des villes la nuit ; relief) : copies de github.com/vasturiano/three-globe (example/img).
# Fichiers :
#   terre_sdf.bin   "GCS1", largeur, hauteur (int32 LE), puis 8192 x 4096 octets compresses (zlib) : distance au rivage
#                   (128 = rivage, plus grand = terre ; 8 niveaux par point) -> terres et cotes nettes a tous les zooms ;
#   terre_nuit.jpg  4096 x 2048 : lumieres des villes (luminance) ;
#   terre_relief.jpg 2048 x 1024 : relief (altitude) ;
#   ciel.jpg        2048 x 1024 : ciel de nuit (Voie lactee), en fond ;
#   traits.bin      frontieres (lignes simplifiees) :
#                   "GCT1", nombre de lignes (int32 LE), puis par ligne : type (u8 : 0 cote, 1 lac, 2 frontiere),
#                   nombre de points (u32 LE), points (lon, lat en int16 LE : degres * 32767 / 180) ;
#   lieux.txt       pays et villes : type(P/V)|rang|lat|lon|nom (rang : 0 = toujours, plus grand = plus pres seulement).
import json, struct, sys, os
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

GJ, IMG, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(OUT, exist_ok=True)
def load(n): return json.load(open(os.path.join(GJ, n), encoding='utf-8'))['features']

# ---------- terres : champ de distance ----------
W, H = 8192, 4096
def px(lon, lat): return ((lon + 180) / 360 * W, (90 - lat) / 180 * H)
def polys(feats):
    for f in feats:
        g = f['geometry']
        if not g: continue
        for p in (g['coordinates'] if g['type'] == 'MultiPolygon' else [g['coordinates']]):
            yield p
# (calcul par tuiles, en 4 fois plus fin : le rivage est place a un quart de point pres, sans escaliers)
SS, T, M = 4, 512, 24                                     # suréchantillonnage, tuile (points), marge (points)
land_polys = [p for p in polys(load('ne_10m_land.geojson'))]
lake_polys = [p for p in polys(load('ne_50m_lakes.geojson'))]
def bbox(p):
    xs = [c[0] for c in p[0]]; ys = [c[1] for c in p[0]]
    return min(xs), max(xs), min(ys), max(ys)
land_bb = [bbox(p) for p in land_polys]; lake_bb = [bbox(p) for p in lake_polys]
v = np.zeros((H, W), dtype=np.uint8)
for ty in range(0, H, T):
    for tx in range(0, W, T):
        x0, y0 = tx - M, ty - M                            # (points de la carte, marge comprise)
        tw = (T + 2 * M) * SS
        lon0, lon1 = x0 / W * 360 - 180, (x0 + T + 2 * M) / W * 360 - 180
        lat1, lat0 = 90 - y0 / H * 180, 90 - (y0 + T + 2 * M) / H * 180
        img = Image.new('1', (tw, tw), 0); d = ImageDraw.Draw(img)
        def draw(ps, bbs, fill_out, fill_hole):
            for p, (a0, a1, b0, b1) in zip(ps, bbs):
                if a1 < lon0 or a0 > lon1 or b1 < lat0 or b0 > lat1: continue
                for k, ring in enumerate(p):
                    pts = [(((lo + 180) / 360 * W - x0) * SS, ((90 - la) / 180 * H - y0) * SS) for lo, la in ring]
                    if len(pts) > 2: d.polygon(pts, fill=fill_out if k == 0 else fill_hole)
        draw(land_polys, land_bb, 1, 0)
        draw(lake_polys, lake_bb, 0, 1)
        land = np.array(img, dtype=bool)
        if land.all() or not land.any():
            v[ty:ty + T, tx:tx + T] = 255 if land.any() else 0
            continue
        sd = np.where(land, ndimage.distance_transform_edt(land) - 0.5, -(ndimage.distance_transform_edt(~land) - 0.5)) / SS
        c = SS // 2                                        # (centre de chaque point de la carte)
        sub = sd[M * SS + c::SS, M * SS + c::SS][:T, :T]
        v[ty:ty + T, tx:tx + T] = np.clip(128 + sub * 8, 0, 255).astype(np.uint8)
import zlib
with open(os.path.join(OUT, 'terre_sdf.bin'), 'wb') as f:      # (octets bruts compresses : pas de decodage d'image de 128 Mo sur le telephone)
    f.write(b'GCS1' + struct.pack('<ii', W, H) + zlib.compress(v.tobytes(), 9))
del v

# ---------- lumieres de la nuit, relief ----------
n = Image.open(os.path.join(IMG, 'earth-night.jpg')).convert('L')
n = n.point(lambda x: 0 if x < 14 else min(255, int((x - 14) * 1.25)))   # (le fond noir reste noir)
n.save(os.path.join(OUT, 'terre_nuit.jpg'), quality=88, optimize=True)
Image.open(os.path.join(IMG, 'earth-topology.png')).convert('L').save(os.path.join(OUT, 'terre_relief.jpg'), quality=90, optimize=True)
Image.open(os.path.join(IMG, 'night-sky.png')).convert('L').resize((2048, 1024), Image.LANCZOS).save(os.path.join(OUT, 'ciel.jpg'), quality=82, optimize=True)   # (ciel : Voie lactee)

# ---------- traits : cotes, lacs, frontieres ----------
def dp(pts, tol):                                         # Douglas-Peucker (iteratif)
    pts = np.asarray(pts, dtype=np.float64)
    if len(pts) < 3: return pts
    keep = np.zeros(len(pts), dtype=bool); keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1: continue
        A, B = pts[a], pts[b]; seg = B - A; L = np.hypot(*seg)
        P = pts[a + 1:b]
        dd = np.hypot(*(P - A).T) if L == 0 else np.abs(seg[0] * (P[:, 1] - A[1]) - seg[1] * (P[:, 0] - A[0])) / L
        i = int(np.argmax(dd))
        if dd[i] > tol: m = a + 1 + i; keep[m] = True; stack += [(a, m), (m, b)]
    return pts[keep]
def lines(feats):
    for f in feats:
        g = f['geometry']
        if not g: continue
        t = g['type']
        if t == 'LineString': yield g['coordinates']
        elif t == 'MultiLineString': yield from g['coordinates']
        elif t == 'Polygon': yield from g['coordinates']
        elif t == 'MultiPolygon':
            for p in g['coordinates']: yield from p
out = []
for typ, name, tol in ((2, 'ne_50m_admin_0_boundary_lines_land.geojson', 0.01),):   # (cotes et lacs : dessines par le champ de distance)
    feats = load(name)
    if typ == 1: feats = [f for f in feats if (f['properties'].get('scalerank') or f['properties'].get('ScaleRank') or 99) <= 6]
    for l in lines(feats):
        s = dp(l, tol)
        if len(s) >= 2: out.append((typ, s))
with open(os.path.join(OUT, 'traits.bin'), 'wb') as f:
    f.write(b'GCT1'); f.write(struct.pack('<i', len(out)))
    total = 0
    for typ, s in out:
        q = np.clip(np.round(s * 32767 / 180), -32767, 32767).astype('<i2')
        f.write(struct.pack('<BI', typ, len(q))); f.write(q.tobytes()); total += len(q)
print('traits :', len(out), 'lignes,', total, 'points', file=sys.stderr)

# ---------- lieux : pays, villes ----------
L = []
for f in load('ne_50m_admin_0_countries.geojson'):
    p = f['properties']
    nm = p.get('NAME_FR') or p.get('NAME')
    if p.get('LABEL_X') is None or not nm: continue
    L.append(('P', int(p.get('LABELRANK') or 5), p['LABEL_Y'], p['LABEL_X'], nm))
for f in load('ne_10m_populated_places.geojson'):
    p = f['properties']
    pop = p.get('POP_MAX') or 0
    cap = (p.get('ADM0CAP') or 0) == 1
    if pop < 250000 and not cap: continue
    nm = p.get('NAME_FR') or p.get('NAME')
    rank = 0 if pop >= 8e6 else 1 if pop >= 3e6 else 2 if (pop >= 1e6 or cap) else 3
    L.append(('V', rank, p['LATITUDE'], p['LONGITUDE'], nm))
with open(os.path.join(OUT, 'lieux.txt'), 'w', encoding='utf-8') as f:
    for t, r, la, lo, nm in L: f.write('%s|%d|%.3f|%.3f|%s\n' % (t, r, la, lo, nm.replace('|', ' ')))
print('lieux :', len(L), file=sys.stderr)
