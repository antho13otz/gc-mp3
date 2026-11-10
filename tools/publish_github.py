#!/usr/bin/env python3
"""Build 516 : copie d'une publication sur GitHub (antho13otz/gc-mp3), pour que les gens la telechargent la-bas aussi.
Compiler et publier se fait toujours par CircleCI -> GitLab ; ceci cree (ou met a jour) une Release GitHub avec les memes
fichiers. Jeton : variable GITHUB_TOKEN (jeton « fine-grained », Contents : Read and write sur gc-mp3 seulement) ; jamais
affiche.

  python3 -I tools/publish_github.py TAG "NOM" NOTES.md fichier1 [fichier2 ...]
  python3 -I tools/publish_github.py depuis-gitlab console|appli
      (CircleCI, apres la publication GitLab : recopie la derniere console ou la derniere appli publique, avec sa note de
       docs/HISTORIQUE.md. Sans GITHUB_TOKEN : ne fait rien, sans echouer. La release « dernier » de GitHub (ancien canal
       de mise a jour des consoles en v377 et avant) n'est mise a jour que si ci/github.env dit DERNIER=1.)

TAG : par exemple v398-b927 ou gc-remote-lite-513. Un fichier deja present sous le meme nom est remplace. Ne jamais y mettre
l'APK prive : refuse si un .apk n'est pas la version publique (fr.gcstation.remote), et tout fichier dont le nom contient
« prive »."""
import json, os, sys, urllib.error, urllib.parse, urllib.request, zipfile

REPO = os.environ.get("GITHUB_REPO", "antho13otz/gc-mp3")
API = "https://api.github.com/repos/" + REPO


def tok():
    t = os.environ.get("GITHUB_TOKEN", "").strip()
    if not t:
        p = os.path.expanduser("~/cles/github-token")
        if os.path.exists(p):
            t = open(p).read().strip()
    if not t:
        sys.exit("publish_github : GITHUB_TOKEN absent")
    return t


def req(url, data=None, method=None, ctype="application/json"):
    h = {"Authorization": "Bearer " + tok(), "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}
    if data is not None:
        h["Content-Type"] = ctype
    r = urllib.request.Request(url, data=data, headers=h, method=method)
    try:
        with urllib.request.urlopen(r, timeout=600) as f:
            b = f.read()
            return json.loads(b) if b else None
    except urllib.error.HTTPError as e:
        if e.code == 404 and method in (None, "GET"):
            return None
        sys.exit("publish_github : %s %s -> %d %s" % (method or "GET", url.split("?")[0], e.code, e.read()[:300].decode("utf-8", "replace")))


def apk_public(p):
    """vrai si l'APK est la version publique (identifiant fr.gcstation.remote dans le manifeste binaire)"""
    with zipfile.ZipFile(p) as z:
        m = z.read("AndroidManifest.xml")
    return "fr.gcstation.remote".encode("utf-16-le") in m or b"fr.gcstation.remote" in m


GL = "https://gitlab.com/api/v4/projects/88617655/packages/generic/"


def gl_get(chemin):
    h = {"PRIVATE-TOKEN": os.environ["GITLAB_TOKEN"]} if os.environ.get("GITLAB_TOKEN") else {}
    with urllib.request.urlopen(urllib.request.Request(GL + chemin, headers=h), timeout=600) as f:
        return f.read()


def note(titre_debut):
    """le paragraphe de docs/HISTORIQUE.md qui commence par titre_debut (jusqu'au titre suivant)"""
    h = open("docs/HISTORIQUE.md", encoding="utf-8").read()
    i = h.find(titre_debut)
    if i < 0:
        return ""
    j = h.find("\n## ", i + 3)
    return h[i:j if j > 0 else len(h)].strip()


def reglage(nom):
    try:
        for l in open("ci/github.env"):
            if l.startswith(nom + "="):
                return l.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def depuis_gitlab(quoi):
    if not os.environ.get("GITHUB_TOKEN", "").strip():
        print("publish_github : pas de GITHUB_TOKEN dans les variables : rien n'est recopie sur GitHub")
        return
    os.makedirs("github", exist_ok=True)
    if quoi == "console":
        rel = json.loads(gl_get("console/dernier/release.json"))
        build = rel["body"].split("\n")[0].split("=")[1].strip()
        ver = ""
        for l in open("source/crashlog.c", encoding="latin-1"):
            if '#define GCMP3_VERSION "' in l:
                ver = l.split('"')[1]
        fichiers = []
        for a in rel["assets"]:
            chemin = a["browser_download_url"].split("/packages/generic/")[1]
            f = os.path.join("github", a["name"])
            open(f, "wb").write(gl_get(chemin))
            fichiers.append(f)
        # v405 : l'APK publique du moment (la derniere publiee, meme inchangee) epinglee a chaque version de la console
        apk = []
        try:
            v = json.loads(gl_get("appli-publique/dernier/version.json"))
            fa = os.path.join("github", v["apk"])
            open(fa, "wb").write(gl_get("appli-publique/dernier/" + v["apk"]))
            apk = [fa]
        except Exception as e:
            print("publish_github : APK publique pas trouvee (%s) : release sans l'appli" % e)
        corps = note("## Nouveautés %s (build %s" % (ver, build)) or ("GC Station %s (build %s)" % (ver, build))
        corps += ("\n\n---\nMise a jour : sur la console, START > Mise a jour. Installation neuve : copier `gc-mp3player-b%s.dol` "
                  "sur la carte SD et le lancer depuis Swiss.\n" % build)
        if apk:
            corps += "Appli Android (GC Remote Lite, version publique du moment) : `%s`.\n" % os.path.basename(apk[0])
        open("github/notes.md", "w", encoding="utf-8").write(corps)
        sys.argv = [sys.argv[0], "%s-b%s" % (ver, build), "GC Station %s (build %s)" % (ver, build), "github/notes.md"] + fichiers + apk
        main()
        if reglage("DERNIER") == "1":                    # (ancien canal : consoles en v377 et avant)
            r = dict(rel)
            r["name"] = "GC Station %s (build %s)" % (ver, build)
            r["assets"] = [dict(a, browser_download_url="https://github.com/%s/releases/download/dernier/%s" % (REPO, a["name"])) for a in rel["assets"]]
            json.dump(r, open("github/release.json", "w"))
            sys.argv = [sys.argv[0], "dernier", r["name"], rel["body"]] + fichiers + ["github/release.json"]
            main()
    elif quoi == "appli":
        v = json.loads(gl_get("appli-publique/dernier/version.json"))
        n, apk = v["build"], v["apk"]
        f = os.path.join("github", apk)
        open(f, "wb").write(gl_get("appli-publique/dernier/" + apk))
        corps = note("## Appli publique %s " % n) or ("GC Remote Lite %s" % n)
        corps += "\n\n---\nGC Remote Lite (Android) : installer l'APK. Les versions 486 et suivantes se mettent a jour toutes seules.\n"
        open("github/notes.md", "w", encoding="utf-8").write(corps)
        sys.argv = [sys.argv[0], "gc-remote-lite-%s" % n, "GC Remote Lite %s (Android)" % n, "github/notes.md", f]
        main()
    else:
        sys.exit("publish_github : depuis-gitlab console|appli")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "depuis-gitlab":
        return depuis_gitlab(sys.argv[2])
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    tag, nom, notes, fichiers = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    for f in fichiers:
        b = os.path.basename(f).lower()
        if "prive" in b or (b.endswith(".apk") and not apk_public(f)):
            sys.exit("publish_github : %s refuse (jamais l'appli privee sur GitHub)" % f)
    corps = open(notes, encoding="utf-8").read() if os.path.exists(notes) else notes
    rel = req(API + "/releases/tags/" + urllib.parse.quote(tag))
    if rel is None:
        rel = req(API + "/releases", json.dumps({"tag_name": tag, "name": nom, "body": corps, "make_latest": "false" if tag == "dernier" else "true"}).encode(), "POST")
    else:
        rel = req(API + "/releases/%d" % rel["id"], json.dumps({"name": nom, "body": corps}).encode(), "PATCH")
    deja = {a["name"]: a["id"] for a in rel.get("assets", [])}
    up = rel["upload_url"].split("{")[0]
    for f in fichiers:
        n = os.path.basename(f)
        if n in deja:
            req(API + "/releases/assets/%d" % deja[n], method="DELETE")
        data = open(f, "rb").read()
        req(up + "?name=" + urllib.parse.quote(n), data, "POST", "application/octet-stream")
        print("  %s (%d octets)" % (n, len(data)))
    print("GitHub : %s -> %s" % (tag, rel["html_url"]))


if __name__ == "__main__":
    main()
