#!/usr/bin/env python3
# v112 : verifie la mise a jour comme la console : release.json (github.com, sans compte), numero de
# build, puis le .dol, le retour en jeu et le lanceur telechargeables (redirections suivies) et entiers.
# Usage : python3 tools/verif_maj.py [build attendu]
import json, re, sys, urllib.request
REPO = "antho13otz/gc-mp3"
def get(url, n=None):
    req = urllib.request.Request(url, headers={"User-Agent": "gc-station-verif", "Accept": "application/vnd.github+json"})
    if n: req.add_header("Range", "bytes=0-%d" % (n - 1))
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.status, r.read()
ok = True
st, b = get("https://github.com/%s/releases/download/dernier/release.json" % REPO)
rel = json.loads(b)
m = re.search(r"build=(\d+)", rel.get("body") or "")
build = int(m.group(1)) if m else 0
print("release.json : HTTP %d, build=%d, %d fichiers" % (st, build, len(rel["assets"])))
if len(sys.argv) > 1 and build != int(sys.argv[1]): print("FAUX : build attendu %s" % sys.argv[1]); ok = False
dols = [a for a in rel["assets"] if a["name"].endswith(".dol")]
mine = [a for a in dols if a["name"] == "gc-mp3player-b%d.dol" % build]
if not mine: print("FAUX : pas de gc-mp3player-b%d.dol" % build); ok = False
for a in rel["assets"]:
    try:
        st, d = get(a["browser_download_url"])
    except Exception as e:
        print("FAUX %s : %s" % (a["name"], e)); ok = False; continue
    good = st == 200 and len(d) == a["size"] and len(d) > 0
    extra = ""
    if a["name"].endswith(".dol"):
        tag = re.search(rb"GCSTATION-BUILD=(\d+)", d[-64:])
        extra = " ; numero dans le fichier : %s" % (tag.group(1).decode() if tag else "?")
        if a in mine and (not tag or int(tag.group(1)) != build): good = False
    print("%s %s : HTTP %d, %d / %d octets%s" % ("OK  " if good else "FAUX", a["name"], st, len(d), a["size"], extra))
    ok &= good
st, b = get("https://api.github.com/repos/%s/releases/tags/dernier" % REPO)
api = json.loads(b); m2 = re.search(r"build=(\d+)", api.get("body") or "")
print("API (secours) : HTTP %d, build=%s" % (st, m2.group(1) if m2 else "?"))
print("TOUT OK" if ok else "ECHEC")
sys.exit(0 if ok else 1)
