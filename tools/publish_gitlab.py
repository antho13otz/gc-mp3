#!/usr/bin/env python3
# Build 907 : publication sur GitLab (le compte GitHub est suspendu depuis le 6 novembre 2026).
# Les fichiers vont dans les paquets generiques du projet (Deploy > Package registry), publics comme le projet :
#   console/<N>/...            les fichiers d'un build de la console (.dol, compresse, retour en jeu, lanceur)
#   console/dernier/release.json   ce que la console lit pour sa mise a jour (meme forme que le release.json de GitHub :
#                              "build=N" dans "body", et "assets" avec "browser_download_url")
#   catalogue/dernier/...      le catalogue d'applis (catalogue.json + paquets)
#   fiches/dernier/...         les fiches des jeux (fiches-gc.txt)
# Quand un nom de fichier est envoye plusieurs fois dans le meme paquet, GitLab donne le plus recent.
#
# Usage (dans un job GitLab, variables CI_JOB_TOKEN, CI_API_V4_URL, CI_PROJECT_ID ; ou CircleCI : GITLAB_TOKEN, CI_PROJECT_ID) :
#   publish_gitlab.py console N fichier.dol [compresse.bin] [igr.img] [loader.bin]
#   publish_gitlab.py fichiers <paquet> <fichier>...        (version "dernier")
import json, os, re, sys, urllib.error, urllib.request

API = os.environ.get("CI_API_V4_URL", "https://gitlab.com/api/v4")
PID = os.environ["CI_PROJECT_ID"]
# (8 novembre 2026 : aussi hors de GitLab, par CircleCI : cle d'acces GITLAB_TOKEN creee par le proprietaire)
if os.environ.get("GITLAB_TOKEN"):
    TOKEN, HDR = os.environ["GITLAB_TOKEN"], "PRIVATE-TOKEN"
else:
    TOKEN, HDR = os.environ["CI_JOB_TOKEN"], "JOB-TOKEN"
BASE = "%s/projects/%s/packages/generic" % (API, PID)


def url(pkg, ver, name):
    return "%s/%s/%s/%s" % (BASE, pkg, ver, name)


def put(pkg, ver, name, data):
    req = urllib.request.Request(url(pkg, ver, name), data=data, method="PUT")
    req.add_header(HDR, TOKEN)
    req.add_header("Content-Type", "application/octet-stream")
    try:
        with urllib.request.urlopen(req, timeout=600) as r:
            st = r.status
    except urllib.error.HTTPError as e:
        print("  PUT %s/%s/%s -> HTTP %d : %s" % (pkg, ver, name, e.code, e.read().decode("utf-8", "replace")[:300]))
        st = e.code
    print("%s/%s/%s : %d octets, HTTP %d" % (pkg, ver, name, len(data), st))
    if st not in (200, 201):
        sys.exit("ERREUR : envoi refuse")
    return url(pkg, ver, name)


def get(u):
    try:
        req = urllib.request.Request(u, headers={HDR: TOKEN})
        with urllib.request.urlopen(req, timeout=300) as r:
            return r.read()
    except Exception as e:
        print("  lecture de %s impossible : %s" % (u.rsplit("/", 1)[-1], e))
        return None


def console(n, dol, xz=None, igr=None, loader=None):
    old = {}
    raw = get(url("console", "dernier", "release.json"))
    if raw:
        try:
            old = json.loads(raw)
        except ValueError:
            old = {}
    m = re.search(r"build=(\d+)", old.get("body") or "")
    if m and int(m.group(1)) > int(n):
        print("release.json annonce deja le build %s (plus recent) : rien a faire" % m.group(1))
        return
    oldf = {a.get("name", ""): a for a in old.get("assets", [])}
    assets = []

    def add(name, data, u):
        assets.append({"name": name, "size": len(data), "browser_download_url": u})

    # 1. les fichiers d'abord ; release.json en dernier : il n'annonce jamais un fichier absent
    d = open(dol, "rb").read()
    add("gc-mp3player-b%s.dol" % n, d, put("console", n, "gc-mp3player-b%s.dol" % n, d))
    if xz and os.path.isfile(xz):
        d = open(xz, "rb").read()
        add("gc-station-compresse-b%s.bin" % n, d, put("console", n, "gc-station-compresse-b%s.bin" % n, d))
    # retour en jeu (v45) et lanceur (v44) : le fichier deja publie est garde s'il n'a pas change (les consoles ne
    # le retelechargent pas) ; le retour en jeu porte le numero du build dans ses 16 premiers octets
    for path, prefix, ext, skip in ((igr, "gc-mp3-igr-b", ".img", 16), (loader, "gc-mp3-loader-b", ".bin", 0)):
        if not path or not os.path.isfile(path):
            continue
        d = open(path, "rb").read()
        kept = None
        for name, a in oldf.items():
            if name.startswith(prefix) and name.endswith(ext):
                b = get(a["browser_download_url"])
                if b is not None and b[skip:] == d[skip:]:
                    kept = a
                    print("%s inchange : garde" % name)
                break
        if kept:
            assets.append(kept)
        else:
            name = "%s%s%s" % (prefix, n, ext)
            add(name, d, put("console", n, name, d))
    body = "build=%s\n\nDerniere version compilee de GC Station (mise a jour automatique : START > Mise a jour)." % n
    js = json.dumps({"tag_name": "dernier", "name": "GC Station build %s" % n, "body": body, "assets": assets},
                    ensure_ascii=False).encode()
    put("console", "dernier", "release.json", js)
    print("publie : build %s (%d fichiers)" % (n, len(assets)))


def fichiers(pkg, paths):
    for p in paths:
        put(pkg, "dernier", os.path.basename(p), open(p, "rb").read())


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "console":
        a = sys.argv[2:] + [None] * 4
        console(a[0], a[1], a[2], a[3], a[4])
    elif len(sys.argv) >= 4 and sys.argv[1] == "fichiers":
        fichiers(sys.argv[2], sys.argv[3:])
    else:
        sys.exit(__doc__ or "usage : voir l'en-tete du fichier")
