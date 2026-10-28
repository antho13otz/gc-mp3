#!/usr/bin/env python3
# v226 : publie le .dol compresse dans la release "dernier" sous le nom gc-station-compresse-bN.bin
# (extension .bin : jamais pris pour le .dol normal), enleve les anciens, et remet release.json a jour
# pour que GC Station le trouve (reglage Mises a jour > Version compressee).
# Usage : python3 tools/publish_xz.py fichier.dol BUILD   (GH_TOKEN, GITHUB_REPOSITORY)
import json, os, re, sys, urllib.error, urllib.request
repo, token = os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"]
api = "https://api.github.com/repos/" + repo
src, build = sys.argv[1], sys.argv[2]
def call(method, url, data=None, ctype="application/json"):
    req = urllib.request.Request(url, data=data, method=method)
    for k, v in (("Authorization", "Bearer " + token), ("Accept", "application/vnd.github+json"), ("User-Agent", "gc-mp3-workflow")):
        req.add_header(k, v)
    if data is not None:
        req.add_header("Content-Type", ctype)
    try:
        with urllib.request.urlopen(req, timeout=600) as r:
            b = r.read()
            return r.status, (json.loads(b) if b else None)
    except urllib.error.HTTPError as e:
        print("  %s -> HTTP %d : %s" % (method, e.code, e.read().decode("utf-8", "replace")[:300]))
        return e.code, None
st, rel = call("GET", api + "/releases/tags/dernier")
if st != 200 or not rel:
    sys.exit("ERREUR : release 'dernier' introuvable")
if not re.search(r"^\s*build=%s\s*$" % re.escape(build), rel.get("body") or "", re.M):
    print("La release 'dernier' n'est plus le build %s (build plus recent publie) : rien a faire" % build)
    sys.exit(0)
mine = "gc-station-compresse-b%s.bin" % build
for a in rel.get("assets", []):
    if a.get("name") == mine:
        call("DELETE", api + "/releases/assets/%d" % a["id"])
st, _ = call("POST", rel["upload_url"].split("{")[0] + "?name=" + mine, open(src, "rb").read(), "application/octet-stream")
print("%s : HTTP %d" % (mine, st))
if st != 201:
    sys.exit(1)
for a in rel.get("assets", []):
    n = a.get("name", "")
    if n.startswith("gc-station-compresse-b") and n != mine:
        call("DELETE", api + "/releases/assets/%d" % a["id"])
        print("ancien %s supprime" % n)
# release.json : meme contenu que celui de publish_release.py, avec le nouveau fichier
st, relf = call("GET", api + "/releases/%d" % rel["id"])
if st != 200 or not relf:
    sys.exit("ERREUR : relecture de la release impossible")
keep = [{"name": a["name"], "size": a.get("size", 0), "browser_download_url": a["browser_download_url"]}
        for a in relf.get("assets", []) if a.get("name", "").endswith((".dol", ".img", ".bin"))]
js = json.dumps({"tag_name": "dernier", "name": relf.get("name"), "body": relf.get("body"), "assets": keep}, ensure_ascii=False).encode()
for a in relf.get("assets", []):
    if a.get("name") == "release.json":
        call("DELETE", api + "/releases/assets/%d" % a["id"])
st, _ = call("POST", rel["upload_url"].split("{")[0] + "?name=release.json", js, "application/json")
print("release.json : HTTP %d (%d fichiers)" % (st, len(keep)))
if st != 201:
    sys.exit(1)
