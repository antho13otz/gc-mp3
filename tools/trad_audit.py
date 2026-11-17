#!/usr/bin/env python3
"""Textes en francais sans traduction anglaise (build 1054).

  python3 tools/trad_audit.py console   -> textes de la console absents de source/lang_tab.h
  python3 tools/trad_audit.py appli     -> textes de l'appli (src/main) absents de assets/en.tsv
  python3 tools/trad_audit.py           -> les deux, avec le nombre

Heuristique : une chaine est "francaise" si elle contient un accent ou un mot francais courant.
Console : les morceaux colles ("T\\xC3\\xA9l\\xC3\\xA9" "phone") sont reunis comme le fait le compilateur ;
les textes des journaux (natif.c, crashlog.c), des fichiers de reglages (lignes "# ...") et des appels
d'ecriture de fichiers / de traces ne sont pas comptes (ils restent en francais).
Appli : un texte est couvert s'il est dans en.tsv tel quel ; les modeles ($nom, %d) sont compares tels quels.
"""
import glob
import os
import re
import sys

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FR_C = re.compile(r'\\xC3|\b(le|la|les|des|du|une|un|pas|avec|pour|sur|dans|est|carte|jeu|jeux|fichier|fichiers|Oui|Non|aucun|aucune|'
                  r'appuie|choisir|retour|lancer|reglages|chargement|erreur|pret|termine|introuvable|impossible|morceau|manette|telephone)\b', re.I)
FR_K = re.compile(r'[éèêëàâçùûôîïœÉÈÊÀÇÔ]|\b(le|la|les|des|du|une|un|pas|avec|pour|sur|dans|est|jeu|jeux|Oui|Non|aucun|aucune|appuie|'
                  r'fichier|fichiers|manette|carte|morceau|chargement|erreur|lancer|retour)\b', re.I)
SAUTE_C = {'natif.c', 'crashlog.c', 'lang.c', 'selftest_keys.c'}
ECRIT = re.compile(r'\b(fprintf|fputs|crumb|lib_note|printf|fopen|nr_log\w*|SYS_Report|strcmp|strncmp|strstr|strcasecmp|strncasecmp|memcmp|rep_log|log_line)\s*\(')
AFFICHE = re.compile(r'\b(gx_text\w*|snprintf|TR|tr|toast\w*|popup\w*|msg\w*|status\w*)\s*\(')


def cles_console():
    s = open(os.path.join(RACINE, 'source/lang_tab.h'), encoding='latin-1').read()
    k = set()
    for m in re.finditer(r'\{\s*((?:"(?:[^"\\]|\\.)*"\s*)+),', s):
        k.add(''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))))
    return k


def console():
    cles = cles_console()
    vus, res = set(), []
    for f in sorted(glob.glob(os.path.join(RACINE, 'source/*.c'))):
        nom = os.path.basename(f)
        if nom in SAUTE_C:
            continue
        s = open(f, encoding='latin-1').read()
        s = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), s, flags=re.S)
        s = re.sub(r'//[^\n]*', '', s)
        lignes = s.split('\n')
        # morceaux colles : "a" "b" (meme sur plusieurs lignes)
        for m in re.finditer(r'((?:"(?:[^"\\\n]|\\.)*"\s*){1,})', s):
            bouts = re.findall(r'"((?:[^"\\\n]|\\.)*)"', m.group(1))
            t = ''.join(bouts)
            if len(t) < 3 or not FR_C.search(t) or t.lstrip().startswith('#'):
                continue
            if re.match(r'^(sd:|SD:/|/|%s/|https?:|\\n#)', t):
                continue
            ln = s.count('\n', 0, m.start()) + 1
            ligne = lignes[ln - 1] if ln - 1 < len(lignes) else ''
            if ECRIT.search(ligne) and not AFFICHE.search(ligne):
                continue
            if t in cles or t in vus:
                continue
            vus.add(t)
            res.append((nom, ln, t, bouts))
    return res


def cles_appli():
    k = set()
    for f in glob.glob(os.path.join(RACINE, 'android/app/src/main/assets/en*.tsv')):
        for l in open(f, encoding='utf-8', errors='replace'):
            if '\t' in l and not l.startswith('#'):
                k.add(l.split('\t')[0])
    return k


def appli():
    cles = cles_appli()
    vus, res = set(), []
    for f in sorted(glob.glob(os.path.join(RACINE, 'android/app/src/main/java/fr/gcstation/telecommande/*.kt'))):
        nom = os.path.basename(f)
        s = open(f, encoding='utf-8', errors='replace').read()
        s = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), s, flags=re.S)
        s = re.sub(r'//[^\n]*', '', s)
        for m in re.finditer(r'"((?:[^"\\\n]|\\.){3,})"', s):
            t = m.group(1)
            if not FR_K.search(t) or t in cles or t in vus:
                continue
            ln = s.count('\n', 0, m.start()) + 1
            ligne = s.split('\n')[ln - 1]
            if re.search(r'\b(Log\.\w|getString|putString|getBoolean|putBoolean|getInt|putInt|endsWith|startsWith|contains|split|Regex)\s*\(', ligne):
                continue
            vus.add(t)
            res.append((nom, ln, t))
    return res


if __name__ == '__main__':
    quoi = sys.argv[1] if len(sys.argv) > 1 else 'les deux'
    if quoi in ('console', 'les deux'):
        r = console()
        if quoi == 'console':
            for nom, ln, t, b in r:
                print(f'{nom}:{ln}\t{t}')
        print(f'console : {len(r)} textes sans traduction', file=sys.stderr)
    if quoi in ('appli', 'les deux'):
        r = appli()
        if quoi == 'appli':
            for nom, ln, t in r:
                print(f'{nom}:{ln}\t{t}')
        print(f'appli : {len(r)} textes sans traduction', file=sys.stderr)
