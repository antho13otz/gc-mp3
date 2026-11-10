#!/usr/bin/env python3
# Build 524 : recopie le CODE sur GitHub (antho13otz/gc-mp3), depuis CircleCI (job github-code, branche github-code).
# Un commit de plus sur la branche par defaut de GitHub (jamais de force) : l'arbre de HEAD, SANS ce qui est prive :
#   - l'appli privee (android/app/src/prive, android/signature.txt) : jamais publiee ;
#   - les notes et outils de travail (CLAUDE.md, tools/claude) ;
#   - .github/workflows : GitHub ne doit rien compiler tout seul (on compile sur CircleCI).
# Jeton : variable CircleCI GITHUB_TOKEN (Contents : Read and write) ; jamais affiche. Sans jeton : ne fait rien.
import os, subprocess, sys, tempfile, shutil, json, urllib.request

REPO = "antho13otz/gc-mp3"
EXCLUS = ("android/app/src/prive/", "android/signature.txt", "CLAUDE.md", "tools/claude/", ".github/workflows/", "ci-cache/")


def run(cmd, cwd=None, cache=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        msg = (r.stdout + r.stderr)
        if cache: msg = msg.replace(cache, "***")
        sys.exit("ERREUR : " + " ".join(c if c != cache else "***" for c in cmd[:3]) + "\n" + msg[-2000:])
    return r.stdout


def main():
    tok = os.environ.get("GITHUB_TOKEN", "")
    essai = "--essai" in sys.argv
    if not tok and not essai:
        print("Pas de GITHUB_TOKEN : rien a faire"); return
    src = run(["git", "rev-parse", "--show-toplevel"]).strip()
    fichiers = [f for f in run(["git", "ls-files", "-z"], cwd=src).split("\0") if f and not f.startswith(EXCLUS)]
    gros = [f for f in fichiers if os.path.getsize(os.path.join(src, f)) > 90 * 1024 * 1024]
    if gros: sys.exit("ERREUR : fichiers trop gros pour GitHub : " + ", ".join(gros))
    prives = [f for f in fichiers if "/prive/" in f or f.endswith((".jks", ".keystore"))]
    if prives: sys.exit("ERREUR : fichiers prives : " + ", ".join(prives))
    ver = "?"
    for l in open(os.path.join(src, "source/crashlog.c"), encoding="latin-1"):
        if "GCMP3_VERSION" in l and '"' in l: ver = l.split('"')[1]; break
    nums = dict(l.strip().split("=", 1) for l in open(os.path.join(src, "ci/numeros.env")) if "=" in l and not l.startswith("#"))
    titre = "Code de GC Station %s (console build %s, appli %s)" % (ver, nums.get("CONSOLE_BUILD"), nums.get("APPLI_BUILD"))
    print(titre, "-", len(fichiers), "fichiers")
    if essai:
        print("\n".join(sorted({f.split("/")[0] + ("/" + f.split("/")[1] if "/" in f else "") for f in fichiers})))
        return
    r = urllib.request.Request("https://api.github.com/repos/" + REPO, headers={"Authorization": "Bearer " + tok, "Accept": "application/vnd.github+json"})
    branche = json.load(urllib.request.urlopen(r, timeout=60))["default_branch"]
    url = "https://x-access-token:%s@github.com/%s.git" % (tok, REPO)
    d = tempfile.mkdtemp()
    run(["git", "clone", "-q", "--depth", "1", "--branch", branche, url, d], cache=url)
    for e in os.listdir(d):
        if e == ".git": continue
        p = os.path.join(d, e)
        shutil.rmtree(p) if os.path.isdir(p) and not os.path.islink(p) else os.remove(p)
    for f in fichiers:
        s, t = os.path.join(src, f), os.path.join(d, f)
        os.makedirs(os.path.dirname(t), exist_ok=True)
        if os.path.islink(s): os.symlink(os.readlink(s), t)
        else: shutil.copy2(s, t)
    run(["git", "add", "-A"], cwd=d)
    if not run(["git", "status", "--porcelain"], cwd=d).strip():
        print("Rien de change sur GitHub"); return
    run(["git", "-c", "user.name=GC Station", "-c", "user.email=gc-station@users.noreply.github.com",
         "commit", "-q", "-m", titre], cwd=d)
    run(["git", "push", "-q", url, "HEAD:" + branche], cwd=d, cache=url)
    print("Code publie sur https://github.com/%s (branche %s)" % (REPO, branche))


main()
