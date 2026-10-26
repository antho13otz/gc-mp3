#!/usr/bin/env python3
# v159 : scenario « tous les boutons » au hasard (simulateur), pour chercher les plantages : A, B, X, Y, Z, L, R, START,
# croix, sticks, dans tous les ecrans. Jamais A dans JEUX / FICHIERS / TELECHARGEMENTS (lancer un jeu, une appli,
# effacer un fichier) ; le reste peut changer des reglages : a lancer sur une copie de la carte SD du simulateur.
# Usage : python3 tools/pc-sim/fuzz.py <minutes> [graine] > tools/pc-sim/scenarios/fuzz.txt
import random, sys
mins = float(sys.argv[1]) if len(sys.argv) > 1 else 10
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
random.seed(seed)
TABS = ['musique', 'radio', 'web', 'retro', 'podcasts', 'photos', 'fichiers', 'infos', 'telephone', 'jeux', 'telechargements']
RISKY = {'jeux', 'fichiers', 'telechargements'}
out = ['# fuzz : %g min (graine %d)' % (mins, seed), 'wait 3s', 'mark depart']
t, tab, last, nm, menu = 0.0, 0, 0.0, 0, False
def w(ms):
    global t
    out.append('wait %dms' % ms); t += ms / 1000.0
while t < mins * 60:
    r = random.random()
    if r < 0.12 and not menu: out.append('press Z'); tab = (tab + 1) % len(TABS); w(random.choice([200, 500]))
    elif r < 0.40: out.append('press %s' % random.choice(['UP', 'DOWN', 'LEFT', 'RIGHT'])); w(random.choice([60, 120, 250]))
    elif r < 0.50: out.append('press %s' % random.choice(['X', 'Y'])); w(random.choice([200, 600]))
    elif r < 0.58 and TABS[tab] not in RISKY and not menu: out.append('press A'); w(random.choice([300, 1000, 2500]))   # (pas dans le menu START : redemarrer, mise a jour...)
    elif r < 0.68: out.append('press B'); menu = False; w(random.choice([150, 400]))
    elif r < 0.73: out.append('press %s' % random.choice(['L', 'R'])); w(400)
    elif r < 0.77: out.append('press START'); menu = not menu; w(400)
    elif r < 0.83: out.append('stick m %d %d %dms' % (random.choice([-100, 0, 100]), random.choice([-100, 0, 100]), random.choice([100, 400])))
    elif r < 0.87: out.append('stick c %d %d %dms' % (random.choice([-100, 0, 100]), random.choice([-100, 0, 100]), random.choice([100, 400])))
    elif r < 0.90: out.append('hold %s %dms' % (random.choice(['UP', 'DOWN', 'R', 'L']), random.choice([600, 1500]))); t += 1
    else: w(random.choice([500, 2000]))
    if t - last >= 60:
        nm += 1; last = t; out.append('mark minute-%d' % nm)
out += ['mark fin', 'quit']
print('\n'.join(out))
