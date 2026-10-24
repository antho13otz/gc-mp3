#!/usr/bin/env python3
# Publie le .dol dans la release "dernier" du depot (mise a jour a chaque
# compilation reussie). GC-MP3 la consulte pour sa mise a jour en un clic.
# La release existante est mise a jour sur place : nouveau .dol
# (gc-mp3player-bN.dol) d'abord, puis le texte "build=N".
# Usage (dans GitHub Actions) : python3 tools/publish_release.py fichier.dol NUMERO
# Variables : GH_TOKEN, GITHUB_REPOSITORY, GITHUB_SHA
import json, os, sys, urllib.error, urllib.request

TAG = "dernier"
dol, build = sys.argv[1], sys.argv[2]
repo = os.environ["GITHUB_REPOSITORY"]
token = os.environ["GH_TOKEN"]
api = "https://api.github.com/repos/" + repo


def call(method, url, data=None, ctype="application/json"):
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("User-Agent", "gc-mp3-workflow")
    if data is not None:
        req.add_header("Content-Type", ctype)
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            body = r.read()
            return r.status, (json.loads(body) if body else None)
    except urllib.error.HTTPError as e:
        msg = e.read().decode("utf-8", "replace")[:400]
        print("  %s %s -> HTTP %d : %s" % (method, url.split("?")[0], e.code, msg))
        return e.code, None


def asset_bytes(a):
    """contenu d'un fichier deja publie (depot public : lien de telechargement direct)"""
    try:
        req = urllib.request.Request(a["browser_download_url"], headers={"User-Agent": "gc-mp3-workflow"})
        with urllib.request.urlopen(req, timeout=180) as r:
            return r.read()
    except Exception as e:
        print("  lecture de %s impossible : %s" % (a.get("name"), e))
        return None


def fail(m):
    print("ERREUR : " + m)
    print("Verifie : Settings > Actions > General > Workflow permissions = 'Read and write permissions'")
    sys.exit(1)


body = "build=" + build + "\n\nDerniere version compilee de GC-MP3 (mise a jour automatique : START > Mise a jour)."
info = {"name": "GC-MP3 build " + build, "body": body, "draft": False, "prerelease": False}

st, rel = call("GET", api + "/releases/tags/" + TAG)
print("release '%s' : HTTP %d" % (TAG, st))
if st != 200 or not rel:
    first = dict(info, tag_name=TAG, target_commitish=os.environ.get("GITHUB_SHA", "main"))
    first["body"] = "build=0\n\nPublication en cours."
    st, rel = call("POST", api + "/releases", json.dumps(first).encode())
    if st != 201 or not rel:
        fail("creation de la release impossible (HTTP %d)" % st)

# 1. le nouveau .dol d'abord, sous un nom propre a ce build
mine = "gc-mp3player-b%s.dol" % build
old = [a for a in rel.get("assets", []) if a.get("name", "").endswith(".dol")]
for a in old:
    if a.get("name") == mine:                       # (compilation relancee)
        call("DELETE", api + "/releases/assets/%d" % a["id"])
up = rel["upload_url"].split("{")[0] + "?name=" + mine
st, _ = call("POST", up, open(dol, "rb").read(), "application/octet-stream")
if st != 201:
    fail("envoi du .dol impossible (HTTP %d)" % st)
print("%s envoye" % mine)

# 2. les anciens .dol (s'ils ne partent pas, GC-MP3 choisit quand meme celui du build)
for a in old:
    if a.get("name") != mine:
        st3, _ = call("DELETE", api + "/releases/assets/%d" % a["id"])
        print("ancien %s supprime : HTTP %d" % (a.get("name"), st3))

# 2b. v39 : le fichier du retour en jeu (apploader.img avec GC-MP3), facultatif
if len(sys.argv) > 3 and os.path.isfile(sys.argv[3]):
    st, rel2 = call("GET", api + "/releases/%d" % rel["id"])
    imgs = [a for a in (rel2 or rel).get("assets", []) if a.get("name", "").endswith(".img")]
    myimg = "gc-mp3-igr-b%s.img" % build
    # v81 : identique a celui deja publie (sauf les 16 octets du numero) : on le garde, avec son
    # numero ; les consoles ne le retelechargent pas a chaque version
    new_img = open(sys.argv[3], "rb").read()
    same = [a for a in imgs if a.get("name") != myimg and (lambda b: b is not None and b[16:] == new_img[16:])(asset_bytes(a))]
    if same:
        print("retour en jeu inchange : %s garde" % same[0]["name"])
        imgs = []
    for a in imgs:
        if a.get("name") == myimg:
            call("DELETE", api + "/releases/assets/%d" % a["id"])
    st = 0
    if not same:
        st, _ = call("POST", rel["upload_url"].split("{")[0] + "?name=" + myimg, new_img, "application/octet-stream")
        print("%s envoye : HTTP %d" % (myimg, st))
    if st == 201:
        for a in imgs:
            if a.get("name") != myimg:
                call("DELETE", api + "/releases/assets/%d" % a["id"])

# 2c. v44 : le lanceur de jeux (moteur Swiss), facultatif. Extension .bin : jamais pris
#     pour une mise a jour de GC-MP3 (les anciennes versions cherchent un .dol)
if len(sys.argv) > 4 and os.path.isfile(sys.argv[4]):
    st, rel3 = call("GET", api + "/releases/%d" % rel["id"])
    bins = [a for a in (rel3 or rel).get("assets", []) if a.get("name", "").startswith("gc-mp3-loader")]
    mybin = "gc-mp3-loader-b%s.bin" % build
    new_bin = open(sys.argv[4], "rb").read()
    same = [a for a in bins if a.get("name") != mybin and asset_bytes(a) == new_bin]   # v81 : inchange : garde
    if same:
        print("lanceur inchange : %s garde" % same[0]["name"])
        bins = []
    for a in bins:
        if a.get("name") == mybin:
            call("DELETE", api + "/releases/assets/%d" % a["id"])
    st = 0
    if not same:
        st, _ = call("POST", rel["upload_url"].split("{")[0] + "?name=" + mybin, new_bin, "application/octet-stream")
        print("%s envoye : HTTP %d" % (mybin, st))
    if st == 201:
        for a in bins:
            if a.get("name") != mybin:
                call("DELETE", api + "/releases/assets/%d" % a["id"])

# 3. le numero de build en dernier : il n'annonce jamais un fichier absent.
#    "build=N" toujours sur la PREMIERE ligne (GC-MP3 v19 et avant ne lisent que
#    le debut du texte) ; si le texte ou le titre ont ete ecrits a la main sur
#    GitHub, ils sont gardes en dessous.
import re
old_body = (rel.get("body") or "").replace("\r\n", "\n")
mine_txt = "Derniere version compilee de GC-MP3"
kept = "\n".join(l for l in old_body.split("\n") if not re.match(r"^\s*build=\d*\s*$", l)).strip()
if kept and mine_txt not in kept and "Publication en cours" not in kept:
    info["body"] = "build=" + build + "\n\n" + kept
old_name = rel.get("name") or ""
if old_name and not old_name.startswith("GC-MP3 build"):
    info["name"] = old_name
st2, _ = call("PATCH", api + "/releases/%d" % rel["id"], json.dumps(info).encode())
if st2 != 200:
    fail("mise a jour du texte de la release impossible (HTTP %d)" % st2)
print("release '%s' publiee : build %s" % (TAG, build))
