#!/usr/bin/env python3
# GC Station : construit le catalogue d'applis (release "catalogue" du depot).
# Pour chaque appli de catalogue/applis.txt : derniere release de son depot GitHub, archive choisie,
# programme (.dol) et fichiers en plus extraits, rangés comme sur la carte SD (apps/<id>/...), avec un meta.xml
# (format de la Homebrew Channel). Chaque appli devient un paquet <id>-<version>.gcsa :
#   zlib( "GCSA1\n" + pour chaque fichier : chemin "\n" taille "\n" contenu )
# et catalogue.json les decrit (version, taille, empreinte SHA-256, depot des sources).
# Une appli qui echoue garde l'entree du catalogue precedent (--ancien).
# Usage : catalogue.py --liste catalogue/applis.txt --sortie out [--api https://api.github.com] [--ancien ancien.json]
import argparse, hashlib, io, json, lzma, os, re, sys, tarfile, time, urllib.request, zipfile, zlib

def get(url, token=None):
    rq = urllib.request.Request(url, headers={'User-Agent': 'gc-station-catalogue', 'Accept': 'application/vnd.github+json'})
    if token: rq.add_header('Authorization', 'Bearer ' + token)
    with urllib.request.urlopen(rq, timeout=120) as r: return r.read()

def members(name, data):
    """ (chemin, contenu) de chaque fichier de l'archive """
    n = name.lower()
    if n.endswith('.zip'):
        z = zipfile.ZipFile(io.BytesIO(data))
        return [(i.filename, z.read(i)) for i in z.infolist() if not i.is_dir()]
    if n.endswith(('.tar.xz', '.txz', '.tar.gz', '.tgz', '.tar')):
        t = tarfile.open(fileobj=io.BytesIO(data))
        return [(m.name, t.extractfile(m).read()) for m in t.getmembers() if m.isfile()]
    if n.endswith('.7z'):
        import py7zr
        with py7zr.SevenZipFile(io.BytesIO(data)) as z: return [(k, v.read()) for k, v in z.readall().items()]
    return [(name, data)]

def esc(s): return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

def build(app, api, token, out):
    rel = json.loads(get('%s/repos/%s/releases/latest' % (api, app['depot']), token))
    ver = rel.get('tag_name', '').lstrip('v') or 'inconnue'
    asset = next((a for a in rel.get('assets', []) if re.search(app['archive'], a['name'])), None)
    if not asset: raise RuntimeError('aucune archive %r dans %s' % (app['archive'], [a['name'] for a in rel.get('assets', [])]))
    m = re.search(r'\d+(?:\.\d+)+', asset['name'])
    if m and len(rel.get('assets', [])) > 1 and not asset['name'].lower().startswith(app['id'].lower() + '_r'): ver = m.group(0)   # (plusieurs applis dans la meme release)
    files = members(asset['name'], get(asset['browser_download_url'], token))
    dols = [(p, d) for p, d in files if re.search(app['dol'], p) and d[:4] != b'\x7fELF']
    if not dols: raise RuntimeError('aucun programme %r dans %s' % (app['dol'], [p for p, _ in files][:30]))
    dp, dol = max(dols, key=lambda x: len(x[1]))           # (le plus gros : pas une version reduite)
    base = 'apps/%s/' % app['id']
    pk = [(base + 'boot.dol', dol)]
    if app['extra']:
        root = os.path.dirname(dp)
        root = os.path.dirname(root) if os.path.basename(root).lower() in ('dol', 'gc', 'gamecube') else root
        for p, d in files:
            if p == dp or not re.search(app['extra'], p) or p.lower().endswith(('.elf', '.dol')): continue
            rel_p = os.path.relpath(p, root) if root and p.startswith(root) else p
            if rel_p.startswith('..') or rel_p.startswith('/'): continue
            for pre in ('apps/%s/' % app['id'], 'apps/'):
                if rel_p.startswith(pre): rel_p = rel_p[len(pre):]; break
            pk.append((base + rel_p, d))
    meta = ('<?xml version="1.0" encoding="UTF-8"?>\n<app version="1">\n  <name>%s</name>\n  <version>%s</version>\n'
            '  <coder>%s</coder>\n  <short_description>%s</short_description>\n  <long_description>Source : https://github.com/%s</long_description>\n</app>\n'
            % (esc(app['nom']), esc(ver), esc(app['depot'].split('/')[0]), esc(app['desc']), app['depot']))
    pk.append((base + 'meta.xml', meta.encode()))
    raw = b'GCSA1\n' + b''.join(p.encode() + b'\n' + str(len(d)).encode() + b'\n' + d for p, d in pk)
    blob = zlib.compress(raw, 9)
    if len(blob) > 8 << 20: raise RuntimeError('paquet de %d octets : trop gros pour la memoire de la GameCube (8 Mo au plus)' % len(blob))
    fn = '%s-%s.gcsa' % (app['id'], re.sub(r'[^A-Za-z0-9._-]', '_', ver))
    open(os.path.join(out, fn), 'wb').write(blob)
    return {'id': app['id'], 'nom': app['nom'], 'version': ver, 'depot': app['depot'], 'description': app['desc'],
            'fichier': fn, 'taille': len(blob), 'taille_sd': sum(len(d) for _, d in pk), 'sha256': hashlib.sha256(blob).hexdigest(),
            'programme': dp, 'date': rel.get('published_at', '')}

def main():
    a = argparse.ArgumentParser()
    a.add_argument('--liste', default='catalogue/applis.txt'); a.add_argument('--sortie', default='out')
    a.add_argument('--api', default='https://api.github.com'); a.add_argument('--ancien')
    o = a.parse_args()
    os.makedirs(o.sortie, exist_ok=True)
    token = os.environ.get('GITHUB_TOKEN') or os.environ.get('GH_TOKEN')
    old = {}
    if o.ancien and os.path.exists(o.ancien):
        try: old = {x['id']: x for x in json.load(open(o.ancien))['applis']}
        except Exception: old = {}
    apps, cat, errs = [], [], 0
    for line in open(o.liste, encoding='utf-8'):
        if not line.strip() or line.startswith('#'): continue
        f = [x.strip() for x in line.split('|')]
        apps.append(dict(id=f[0], nom=f[1], depot=f[2], archive=f[3], dol=f[4], extra=f[5], desc=f[6]))
    for app in apps:
        try:
            e = build(app, o.api, token, o.sortie); cat.append(e)
            print('OK   %-10s %-10s %s (%d octets)' % (app['id'], e['version'], e['programme'], e['taille']))
        except Exception as ex:
            errs += 1
            print('FAUX %-10s %s' % (app['id'], ex))
            if app['id'] in old: cat.append(dict(old[app['id']], ancien=True))      # (le fichier est deja dans la release)
    json.dump({'format': 1, 'date': time.strftime('%Y-%m-%d %H:%M'), 'applis': cat}, open(os.path.join(o.sortie, 'catalogue.json'), 'w'), ensure_ascii=False, indent=1)
    print('%d applis, %d erreurs' % (len(cat), errs))

if __name__ == '__main__': main()
