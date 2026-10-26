#!/usr/bin/env python3
# v157 : scenario d'endurance (simulateur) : des dizaines de minutes d'utilisation au hasard (onglets, listes,
# morceaux, photos, menu START), avec un point de mesure (« mark ») chaque minute : memoire utilisee, fluidite.
# Usage : python3 tools/pc-sim/endurance.py <minutes> [graine] > tools/pc-sim/scenarios/endurance.txt
# Seuls des boutons sans danger : A seulement dans MUSIQUE (lecture) et PHOTOS (ouvrir / fermer une photo) ;
# jamais de lancement de jeu ou d'appli.
import random, sys
mins = float(sys.argv[1]) if len(sys.argv) > 1 else 30
random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
TABS = ['musique', 'radio', 'web', 'retro', 'podcasts', 'photos', 'fichiers', 'infos', 'telephone', 'jeux', 'telechargements']
out = ['# endurance : %g min (graine %s)' % (mins, sys.argv[2] if len(sys.argv) > 2 else 1), 'wait 3s', 'press A', 'wait 2s', 'mark depart']
t, tab, last_mark, nmark = 0.0, 0, 0.0, 0
def w(ms):
    global t
    out.append('wait %dms' % ms); t += ms / 1000.0
while t < mins * 60:
    r = random.random()
    if r < 0.18:                                           # onglet suivant
        out.append('press Z'); tab = (tab + 1) % len(TABS); w(random.choice([300, 600, 1500]))
    elif r < 0.55:                                         # navigation dans la liste
        b = random.choice(['UP', 'DOWN', 'DOWN', 'LEFT', 'RIGHT'])
        if random.random() < 0.15: out.append('hold %s %dms' % (b, random.choice([500, 1200]))); t += 1
        else: out.append('press %s' % b)
        w(random.choice([80, 150, 300]))
    elif r < 0.65 and TABS[tab] == 'musique':              # lire le morceau choisi
        out.append('press A'); w(random.choice([2000, 8000, 20000]))
    elif r < 0.72 and TABS[tab] == 'photos':               # ouvrir une photo, la suivante, fermer
        out.append('press A'); w(1500); out.append('press RIGHT'); w(1500); out.append('press B'); w(600)
    elif r < 0.78:                                         # morceau suivant / precedent
        out.append('press %s' % random.choice(['R', 'R', 'L'])); w(random.choice([500, 3000]))
    elif r < 0.82:                                         # menu START, un peu de navigation, sortie
        out.append('press START'); w(500)
        for _ in range(random.randint(1, 6)): out.append('press DOWN'); w(120)
        out.append('press B'); w(500)
    elif r < 0.88:                                         # retour (sortir d'un sous-ecran)
        out.append('press B'); w(300)
    else:
        w(random.choice([1000, 3000, 6000]))               # ecoute
    if t - last_mark >= 60:
        nmark += 1; last_mark = t
        out.append('press B'); w(200); out.append('press B'); w(200)
        out.append('mark minute-%d' % nmark)
out.append('mark fin')
out.append('quit')
print('\n'.join(out))
