#!/usr/bin/env python3
# GC Station : fabrique le .zip de notice d'un jeu (a installer depuis TELECHARGEMENTS, ou a envoyer depuis le telephone).
# Entrees, dans l'ordre naturel des noms ("page 2" avant "page 10") : un ou des PDF, des images (pages scannees ou
# captures d'ecran du site de modes d'emploi), ou un .zip d'images.
#   - Images et captures d'ecran : la page est retrouvee toute seule (barres du telephone et du navigateur, fond du site enleves) ;
#     deux captures de la meme page a la suite : une seule est gardee (la plus grande). Pages d'un PDF : telles quelles.
#   - Double page (plus large que haute) : coupee en deux pages (gauche puis droite).
#   - Planche d'impression (grandes marges blanches, 2 doubles pages l'une sur l'autre) : decoupee et remise dans
#     l'ordre du livret (couverture en premier, dos en dernier), comme l'appli.
# Sortie : notice-<ID>.zip avec notices/<ID>/01.jpg, 02.jpg... (01 = la couverture), JPEG de 480 x 816 au plus,
# comme l'appli les envoie (la console les lit dans SD:/gc-mp3/notices/<ID>/). --apercu : planche-contact pour verifier.
# (range avec l'appli : le modifier ne refait pas de version de la console)
# Usage : notice.py GALE01 fichiers... [--sortie dossier] [--sans-rognage] [--sans-double] [--haut N] [--bas N]
#                   [--garder 1,2,5-9] [--apercu]
# Besoin : Python 3, Pillow, numpy ; pdftoppm (poppler) pour les PDF.
import argparse, io, os, re, subprocess, sys, tempfile, zipfile
import numpy as np
from PIL import Image, ImageOps

PW, PH = 480, 816

def cle(s):
    return re.sub(r'\d+', lambda m: m.group(0).zfill(6), os.path.basename(s).lower())

def lire_entrees(fichiers, tmp):
    """-> liste de (image PIL RGB, a rogner ?) dans l'ordre (les pages d'un PDF ne sont pas rognees)"""
    out = []
    for f in sorted(fichiers, key=cle):
        low = f.lower()
        if low.endswith('.pdf'):
            d = tempfile.mkdtemp(dir=tmp)
            subprocess.run(['pdftoppm', '-r', '200', '-png', f, os.path.join(d, 'p')], check=True)
            out += [(Image.open(os.path.join(d, n)).convert('RGB'), False) for n in sorted(os.listdir(d), key=cle)]
        elif low.endswith('.zip'):
            with zipfile.ZipFile(f) as z:
                for n in sorted(z.namelist(), key=cle):
                    if n.lower().endswith(('.jpg', '.jpeg', '.png', '.webp', '.bmp')):
                        out.append((ImageOps.exif_transpose(Image.open(io.BytesIO(z.read(n)))).convert('RGB'), True))
        else:
            out.append((ImageOps.exif_transpose(Image.open(f)).convert('RGB'), True))
    return out

# ---------------------------------------------------------------- page dans une capture d'ecran
def runs(flag, fusion):
    """suites de True -> [(debut, fin)], en reunissant celles separees de moins de `fusion`"""
    r, d = [], None
    for i, v in enumerate(list(flag) + [False]):
        if v and d is None: d = i
        if not v and d is not None: r.append([d, i]); d = None
    m = []
    for a in r:
        if m and a[0] - m[-1][1] < fusion: m[-1][1] = a[1]
        else: m.append(a)
    return m

def fond_de(p):
    """couleur du fond du site : la plus frequente sur les bords gauche et droit (moitie du milieu en hauteur)"""
    h = p.shape[0]
    b = np.concatenate([p[h // 4: 3 * h // 4, :3].reshape(-1, 3), p[h // 4: 3 * h // 4, -3:].reshape(-1, 3)]) // 8
    v, n = np.unique(b, axis=0, return_counts=True)
    m = (b == v[n.argmax()]).all(axis=1)                       # (moyenne exacte des points de cette couleur)
    return np.concatenate([p[h // 4: 3 * h // 4, :3].reshape(-1, 3), p[h // 4: 3 * h // 4, -3:].reshape(-1, 3)])[m].mean(axis=0)

def de_fond(a, fond, axe):
    """lignes (axe 1) ou colonnes (axe 0) du fond : 97 % des points a moins de 8 de sa couleur"""
    return (np.abs(a - fond).max(axis=2) < 8).mean(axis=axe) > 0.97

def rogner(im, haut=0, bas=0):
    """la page dans une capture : la plus grande zone qui n'est pas le fond uni du site (barres du telephone et du
    navigateur, separees de la page par le fond, enlevees aussi)"""
    if haut or bas: im = im.crop((0, haut, im.width, im.height - bas))
    k = 600 / max(im.size)
    p = np.asarray(im.resize((max(8, int(im.width * k)), max(8, int(im.height * k))), Image.BILINEAR), dtype=np.int16)
    h, w = p.shape[:2]
    fond = fond_de(p)
    lig = runs(~de_fond(p, fond, 1), 3)
    if not lig: return im
    y0, y1 = max(lig, key=lambda r: r[1] - r[0])
    col = runs(~de_fond(p[y0:y1], fond, 0), 3)
    if not col: return im
    x0, x1 = max(col, key=lambda r: r[1] - r[0])
    if (x1 - x0) * (y1 - y0) < 0.12 * w * h: return im            # (rien de net : on garde tout)
    # (bords exacts en pleine taille : on retire les lignes et colonnes du fond qui restent)
    X0, Y0 = max(0, int((x0 - 1) / k)), max(0, int((y0 - 1) / k))
    X1, Y1 = min(im.width, int((x1 + 1) / k + 1)), min(im.height, int((y1 + 1) / k + 1))
    q = np.asarray(im.crop((X0, Y0, X1, Y1)), dtype=np.int16)
    l = np.where(~de_fond(q, fond, 1))[0]; c = np.where(~de_fond(q, fond, 0))[0]
    if len(l) and len(c): X0, X1, Y0, Y1 = X0 + c[0], X0 + c[-1] + 1, Y0 + l[0], Y0 + l[-1] + 1
    return im.crop((X0, Y0, X1, Y1))

# ---------------------------------------------------------------- planches d'impression (comme l'appli)
def cadre(a):
    """contenu non blanc d'une bande : (x0, y0, x1, y1) ou None, et la part de points non blancs"""
    nb = a.sum(axis=2) < 720
    h, w = nb.shape
    part = nb.mean()
    xs = np.where(nb.sum(axis=0) > h / 2)[0]; ys = np.where(nb.sum(axis=1) > w / 2)[0]
    if len(xs) < w / 8 or len(ys) < h / 8: return None, part
    return (xs[0], ys[0], xs[-1] + 1, ys[-1] + 1), part

def est_planche(im):
    a = np.asarray(im, dtype=np.int32); h2 = im.height // 2
    ref = cadre(a[:h2])[0] or cadre(a[h2:])[0]
    return ref is not None and ref[0] > im.width * 0.06 and ref[2] < im.width * 0.94 and (ref[2] - ref[0]) > (ref[3] - ref[1])

def decouper(im, z):
    a = np.asarray(im, dtype=np.int32); h2 = im.height // 2
    ch, ph = cadre(a[:h2]); cb, pb = cadre(a[h2:])
    if cb: cb = (cb[0], cb[1] + h2, cb[2], cb[3] + h2)
    haut = ch or (cb and (cb[0], cb[1] - h2, cb[2], cb[3] - h2)); bas = cb or (ch and (ch[0], ch[1] + h2, ch[2], ch[3] + h2))
    for c, part, ok in ((haut, ph, ch is not None), (bas, pb, cb is not None)):
        if c is None or (not ok and part < 0.005): continue
        x0, y0, x1, y1 = c; mid = (x0 + x1) // 2
        g = im.crop((x0, y0, mid, y1)); d = im.crop((mid, y0, x1, y1))
        if z['premiere']: z['pages'].append(d); z['dos'] = g; z['premiere'] = False
        else: z['pages'] += [g, d]

# ----------------------------------------------------------------
def pareille(a, b):
    """deux captures de la meme page ? (vignettes 120 x 200 en gris : moins de 0,5 % de points vraiment differents)"""
    if abs(a.width / a.height - b.width / b.height) > 0.05 * a.width / a.height: return False
    va = np.asarray(a.convert('L').resize((120, 200), Image.BILINEAR), dtype=np.int16)
    vb = np.asarray(b.convert('L').resize((120, 200), Image.BILINEAR), dtype=np.int16)
    return (np.abs(va - vb) > 40).mean() < 0.005

def jpeg(im):
    k = min(PW / im.width, PH / im.height, 1)
    if k < 1: im = im.resize((max(8, int(im.width * k)), max(8, int(im.height * k))), Image.LANCZOS)
    o = io.BytesIO(); im.save(o, 'JPEG', quality=88, optimize=True)
    return o.getvalue()

def selection(s, n):
    g = set()
    for p in s.split(','):
        if '-' in p: a, b = p.split('-'); g.update(range(int(a), int(b) + 1))
        elif p: g.add(int(p))
    return [i for i in range(1, n + 1) if i in g]

def main():
    ap = argparse.ArgumentParser(description='Fabrique le .zip de notice d\'un jeu pour GC Station')
    ap.add_argument('id'); ap.add_argument('fichiers', nargs='+')
    ap.add_argument('--sortie', default='.'); ap.add_argument('--sans-rognage', action='store_true')
    ap.add_argument('--sans-double', action='store_true'); ap.add_argument('--haut', type=int, default=0)
    ap.add_argument('--bas', type=int, default=0); ap.add_argument('--garder', default='')
    ap.add_argument('--apercu', action='store_true')
    o = ap.parse_args()
    gid = o.id.upper()
    if not re.fullmatch(r'[A-Z0-9]{6}', gid): sys.exit('ID du jeu : 6 lettres ou chiffres (ex. GALE01)')
    with tempfile.TemporaryDirectory() as tmp:
        ims = lire_entrees(o.fichiers, tmp)
    pages, z = [], {'pages': [], 'dos': None, 'premiere': True}
    for im, rg in ims:
        if est_planche(im): decouper(im, z); continue
        p = rogner(im, o.haut, o.bas) if rg and not o.sans_rognage else im
        if not o.sans_double and p.width > p.height * 1.15:
            m = p.width // 2; nouv = [p.crop((0, 0, m, p.height)), p.crop((m, 0, p.width, p.height))]
        else: nouv = [p]
        for q in nouv:
            if pages and pareille(pages[-1], q):
                if q.width * q.height > pages[-1].width * pages[-1].height: pages[-1] = q
                continue
            pages.append(q)
    pages += z['pages'] + ([z['dos']] if z['dos'] is not None else [])
    if o.garder: pages = [pages[i - 1] for i in selection(o.garder, len(pages))]
    if not pages: sys.exit('aucune page')
    os.makedirs(o.sortie, exist_ok=True)
    zp = os.path.join(o.sortie, 'notice-%s.zip' % gid)
    with zipfile.ZipFile(zp, 'w', zipfile.ZIP_STORED) as zf:
        for i, p in enumerate(pages):
            zf.writestr('notices/%s/%02d.jpg' % (gid, i + 1), jpeg(p))
    print('%s : %d pages' % (zp, len(pages)))
    if o.apercu:
        n = len(pages); cols = 6; rows = (n + cols - 1) // cols
        pl = Image.new('RGB', (cols * 170, rows * 270), (40, 40, 44))
        from PIL import ImageDraw
        dr = ImageDraw.Draw(pl)
        for i, p in enumerate(pages):
            t = p.copy(); t.thumbnail((160, 240))
            x, y = (i % cols) * 170 + 5, (i // cols) * 270 + 5
            pl.paste(t, (x, y)); dr.text((x, y + 245), '%02d' % (i + 1), fill=(255, 255, 255))
        ap_ = os.path.join(o.sortie, 'notice-%s-apercu.jpg' % gid); pl.save(ap_, quality=85)
        print(ap_)

if __name__ == '__main__':
    main()
