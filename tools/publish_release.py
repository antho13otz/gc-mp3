#!/usr/bin/env python3
# Publie le .dol dans la release "dernier" du depot (remplacee a chaque
# compilation reussie). GC-MP3 la consulte pour sa mise a jour en un clic.
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
        with urllib.request.urlopen(req, timeout=120) as r:
            body = r.read()
            return r.status, (json.loads(body) if body else None)
    except urllib.error.HTTPError as e:
        return e.code, None


# l'ancienne release "dernier" et son etiquette
st, rel = call("GET", api + "/releases/tags/" + TAG)
if st == 200 and rel:
    call("DELETE", api + "/releases/%d" % rel["id"])
call("DELETE", api + "/git/refs/tags/" + TAG)

info = {
    "tag_name": TAG,
    "target_commitish": os.environ.get("GITHUB_SHA", "main"),
    "name": "GC-MP3 build " + build,
    "body": "build=" + build + "\n\nDerniere version compilee de GC-MP3 (mise a jour automatique : START > Mise a jour).",
    "draft": False,
    "prerelease": False,
}
st, rel = call("POST", api + "/releases", json.dumps(info).encode())
if st != 201 or not rel:
    sys.exit("publication impossible (code %s) : verifie les permissions du workflow" % st)
up = rel["upload_url"].split("{")[0] + "?name=gc-mp3player.dol"
st, _ = call("POST", up, open(dol, "rb").read(), "application/octet-stream")
if st != 201:
    sys.exit("envoi du .dol impossible (code %s)" % st)
print("release '%s' publiee : build %s" % (TAG, build))
