#!/usr/bin/env python3
# v223 : publie des fichiers d'essai dans une release a part (jamais lue par la mise a jour de GC Station,
# qui ne regarde que la release "dernier"). La release est creee si besoin ; un fichier de meme nom est remplace.
# Usage : python3 tools/publish_essai.py TAG "texte de la release" fichier [...]   (GH_TOKEN, GITHUB_REPOSITORY)
import json, os, sys, urllib.error, urllib.request
repo, token = os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"]
api = "https://api.github.com/repos/" + repo
tag, body, files = sys.argv[1], sys.argv[2], sys.argv[3:]
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
st, rel = call("GET", api + "/releases/tags/" + tag)
info = {"name": os.environ.get("RELEASE_NAME", "Essai : .dol compressé"), "body": body, "prerelease": os.environ.get("RELEASE_PRE", "1") == "1"}
if st != 200 or not rel:
    st, rel = call("POST", api + "/releases", json.dumps(dict(info, tag_name=tag, target_commitish=os.environ.get("GITHUB_SHA", "main"))).encode())
    if st != 201 or not rel:
        sys.exit("ERREUR : creation de la release '%s' impossible" % tag)
else:
    call("PATCH", api + "/releases/%d" % rel["id"], json.dumps(info).encode())
for p in files:
    n = os.path.basename(p)
    for a in rel.get("assets", []):
        if a.get("name") == n:
            call("DELETE", api + "/releases/assets/%d" % a["id"])
    st, _ = call("POST", rel["upload_url"].split("{")[0] + "?name=" + n, open(p, "rb").read(), "application/octet-stream")
    print("%s : HTTP %d" % (n, st))
    if st != 201:
        sys.exit(1)
