#!/usr/bin/env python3
"""GC Station v281 : fiches des jeux GameCube (pour la fiche d'un jeu sur la console).

Telecharge la base de GameTDB (wiitdb.zip, en francais et en anglais), garde les jeux GameCube et ecrit
fiches-gc.txt, une ligne par jeu (UTF-8, champs separes par des tabulations) :
  ID  titre  genre  joueurs  developpeur  editeur  annee  classification  resume
La console ne lit que les lignes de ses jeux.
  python3 tools/fiches_gametdb.py --sortie out/fiches-gc.txt [--xml wiitdb.xml ...]   (--xml : fichiers deja la, essais)
"""
import argparse, io, re, sys, urllib.request, zipfile
import xml.etree.ElementTree as ET

URLS = ["https://www.gametdb.com/wiitdb.zip?LANG=FR&FALLBACK=true&GAMECUBE=true&WIIWARE=false",
        "https://www.gametdb.com/wiitdb.zip?LANG=EN&FALLBACK=true&GAMECUBE=true&WIIWARE=false"]

GENRES = {
    "action": "Action", "adventure": "Aventure", "racing": "Course", "sports": "Sport", "sport": "Sport", "fighting": "Combat",
    "platformer": "Plates-formes", "platform": "Plates-formes", "puzzle": "Réflexion", "role-playing": "RPG", "rpg": "RPG",
    "shooter": "Tir", "first-person shooter": "Tir à la 1re personne", "third-person shooter": "Tir à la 3e personne",
    "strategy": "Stratégie", "simulation": "Simulation", "party": "Party game", "music": "Musique", "rhythm": "Rythme",
    "board game": "Jeu de plateau", "card game": "Jeu de cartes", "stealth": "Infiltration", "survival horror": "Survival horror",
    "horror": "Horreur", "educational": "Éducatif", "fitness": "Fitness", "arcade": "Arcade", "beat 'em up": "Beat 'em up",
    "hack and slash": "Hack and slash", "life simulation": "Simulation de vie", "flight": "Vol", "driving": "Conduite",
    "soccer": "Football", "football": "Football américain", "basketball": "Basket", "baseball": "Baseball", "golf": "Golf",
    "tennis": "Tennis", "skateboarding": "Skate", "snowboarding": "Snowboard", "wrestling": "Catch", "boxing": "Boxe",
    "hockey": "Hockey", "pinball": "Flipper", "compilation": "Compilation", "quiz": "Quiz", "trivia": "Quiz", "mini-games": "Mini-jeux",
    "minigame": "Mini-jeux", "open world": "Monde ouvert", "action adventure": "Action-aventure", "kart racing": "Course de karts",
    "dancing": "Danse", "fishing": "Pêche", "hunting": "Chasse", "space": "Espace", "mecha": "Mecha", "rail shooter": "Tir sur rail",
}

def clean(s):
    return re.sub(r"\s+", " ", (s or "").replace("\t", " ")).strip()

def genre_fr(g):
    out = []
    for p in (g or "").split(","):
        p = p.strip().lower()
        if not p:
            continue
        t = GENRES.get(p, p[:1].upper() + p[1:])
        if t not in out:
            out.append(t)
    return ", ".join(out[:3])

def parse(xml_bytes, lang, games):
    for _, el in ET.iterparse(io.BytesIO(xml_bytes), events=("end",)):
        if el.tag != "game":
            continue
        gid = (el.findtext("id") or "").strip().upper()
        typ = (el.findtext("type") or "").strip().lower()
        if len(gid) != 6 or ("gamecube" not in typ and typ not in ("gc", "ngc")):
            el.clear(); continue
        g = games.setdefault(gid, {})
        for loc in el.findall("locale"):
            l = (loc.get("lang") or "").upper()
            t, syn = clean(loc.findtext("title")), clean(loc.findtext("synopsis"))
            if t and (l == "FR" or "title" not in g):
                g["title"] = t
            if syn and (l == "FR" or "syn" not in g):
                g["syn"] = syn
        if not g.get("title"):
            g["title"] = clean(el.get("name"))
        g.setdefault("dev", clean(el.findtext("developer")))
        g.setdefault("pub", clean(el.findtext("publisher")))
        d = el.find("date")
        if d is not None and d.get("year") and not g.get("year"):
            g["year"] = d.get("year").strip()
        if not g.get("genre"):
            g["genre"] = genre_fr(el.findtext("genre"))
        r = el.find("rating")
        if r is not None and r.get("type") and r.get("value") and not g.get("rating"):
            g["rating"] = "%s %s" % (r.get("type").strip(), r.get("value").strip())
        i = el.find("input")
        if i is not None and i.get("players") and not g.get("players"):
            g["players"] = i.get("players").strip()
        el.clear()

def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "GC-Station-fiches/1.0"})
    data = urllib.request.urlopen(req, timeout=120).read()
    z = zipfile.ZipFile(io.BytesIO(data))
    name = next(n for n in z.namelist() if n.lower().endswith(".xml"))
    return z.read(name)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sortie", required=True)
    ap.add_argument("--xml", nargs="*")
    a = ap.parse_args()
    games = {}
    sources = [open(p, "rb").read() for p in a.xml] if a.xml else [fetch(u) for u in URLS]
    for i, x in enumerate(sources):
        parse(x, "FR" if i == 0 else "EN", games)
    n = 0
    with open(a.sortie, "w", encoding="utf-8", newline="\n") as f:
        f.write("# GC Station : fiches des jeux GameCube, d'apres GameTDB (www.gametdb.com)\n")
        for gid in sorted(games):
            g = games[gid]
            if not g.get("title"):
                continue
            syn = g.get("syn", "")
            if len(syn) > 1500:
                syn = syn[:1497].rsplit(" ", 1)[0] + "..."
            f.write("\t".join([gid, g.get("title", ""), g.get("genre", ""), g.get("players", ""), g.get("dev", ""), g.get("pub", ""),
                               g.get("year", ""), g.get("rating", ""), syn]) + "\n")
            n += 1
    print("fiches :", n, "jeux GameCube")
    if n < 100 and not a.xml:
        sys.exit("trop peu de jeux : base illisible ?")

if __name__ == "__main__":
    main()
