#!/usr/bin/env python3
# Publie le .dol dans la release "dernier" du depot (mise a jour a chaque
# compilation reussie). GC-MP3 la consulte pour sa mise a jour en un clic.
# La release existante est mise a jour sur place (texte + fichier .dol) ;
# elle n'est creee que si elle n'existe pas encore.
# Usage (dans GitHub Actions) : python3 tools/publish_release.py fichier.dol NUMERO
# Variables : GH_TOKEN, GITHUB_REPOSITORY, GITHUB_SHA
import json, os, sys, urllib.error, urllib.request

TAG = "dernier"
NAME = "gc-mp3player.dol"
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


def fail(m):
    print("ERREUR : " + m)
    print("Verifie : Settings > Actions > General > Workflow permissions = 'Read and write permissions'")
    sys.exit(1)


body = "build=" + build + "\n\nDerniere version compilee de GC-MP3 (mise a jour automatique : START > Mise a jour)."
info = {"name": "GC-MP3 build " + build, "body": body, "draft": False, "prerelease": False}

st, rel = call("GET", api + "/releases/tags/" + TAG)
print("release '%s' : HTTP %d" % (TAG, st))
if st == 200 and rel:
    st2, rel2 = call("PATCH", api + "/releases/%d" % rel["id"], json.dumps(info).encode())
    if st2 != 200 or not rel2:
        fail("mise a jour du texte de la release impossible (HTTP %d)" % st2)
    rel = rel2
    for a in rel.get("assets", []):
        if a.get("name") == NAME:
            st3, _ = call("DELETE", api + "/releases/assets/%d" % a["id"])
            print("ancien %s supprime : HTTP %d" % (NAME, st3))
            if st3 not in (204, 404):
                fail("suppression de l'ancien .dol impossible (HTTP %d) - release immuable ?" % st3)
else:
    info["tag_name"] = TAG
    info["target_commitish"] = os.environ.get("GITHUB_SHA", "main")
    st, rel = call("POST", api + "/releases", json.dumps(info).encode())
    if st != 201 or not rel:
        fail("creation de la release impossible (HTTP %d)" % st)

up = rel["upload_url"].split("{")[0] + "?name=" + NAME
st, _ = call("POST", up, open(dol, "rb").read(), "application/octet-stream")
if st != 201:
    fail("envoi du .dol impossible (HTTP %d)" % st)
print("release '%s' publiee : build %s" % (TAG, build))
