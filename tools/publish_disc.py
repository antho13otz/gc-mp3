#!/usr/bin/env python3
# v92 : publie les images du disque de demarrage (gc-station-disque-<region>-bN.iso) dans la release
# "dernier" et enleve les anciennes. Extension .iso : jamais prise pour une mise a jour (.dol).
# Usage : python3 tools/publish_disc.py fichier.iso [...]   (GH_TOKEN, GITHUB_REPOSITORY)
import json, os, sys, urllib.error, urllib.request
repo, token = os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"]
api = "https://api.github.com/repos/" + repo
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
mine = [os.path.basename(p) for p in sys.argv[1:]]
for p in sys.argv[1:]:
    n = os.path.basename(p)
    for a in rel.get("assets", []):
        if a.get("name") == n:
            call("DELETE", api + "/releases/assets/%d" % a["id"])
    st, _ = call("POST", rel["upload_url"].split("{")[0] + "?name=" + n, open(p, "rb").read(), "application/octet-stream")
    print("%s : HTTP %d" % (n, st))
    if st != 201:
        sys.exit(1)
for a in rel.get("assets", []):
    if a.get("name", "").startswith("gc-station-disque-") and a.get("name") not in mine:
        call("DELETE", api + "/releases/assets/%d" % a["id"])
        print("ancien %s supprime" % a["name"])
