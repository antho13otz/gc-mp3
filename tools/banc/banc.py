#!/usr/bin/env python3
# v172 : banc d'essai automatique : pour chaque image reduite (.gcb) exportee par GC Station (Jeux > Options >
# Banc d'essai), le vrai programme du jeu tourne avec Pulse dans l'emulateur (tools/natif-test/emu_game.py) :
# demarrage, premieres lectures du disque, carte memoire virtuelle. Rapport jeu par jeu dans rapport.txt.
#   python3 tools/banc/banc.py dossier/banc [secondes par jeu]
# (avant : sh tools/natif-test/run.sh, qui prepare h2, le faux .dol de GC Station et Unicorn corrige)
import os, sys, glob, subprocess, re
B = os.path.dirname(os.path.abspath(__file__)); T = os.path.join(B, '..', 'natif-test'); W = os.path.join(T, 'work')
sys.path.insert(0, B)
import gcb
src = sys.argv[1]; secs = sys.argv[2] if len(sys.argv) > 2 else '20'
out = os.path.join(src, 'essais'); os.makedirs(out, exist_ok=True)
import concurrent.futures
JOBS = int(os.environ.get('BANC_JOBS', str(os.cpu_count() or 1)))   # (plusieurs jeux a la fois : un par processeur)
def essai(g_path):
    g = gcb.lire(g_path); nom = os.path.basename(g_path)[:-4]
    lignes = []
    d = os.path.join(out, nom); os.makedirs(os.path.join(d, 'out', 'resident'), exist_ok=True); os.makedirs(os.path.join(d, 'out', 'natif'), exist_ok=True)
    open(os.path.join(d, 'out', 'resident', 'carte-A.raw'), 'wb').truncate(2 << 20)
    gcb.ecrire_iso(g, os.path.join(d, 'jeu.iso'))
    subprocess.run([sys.executable, os.path.join(B, 'mkfat_sparse.py'), g_path, os.path.join(d, 'sd.img'), os.path.join(W, 'selfdol.dol')], check=True, stdout=subprocess.DEVNULL)
    env = dict(os.environ, EMU_GCB=os.path.abspath(g_path), NATIF_SELF=os.path.join(W, 'selfdol.dol'), NATIF_DUMP='boot.bin', UNICORN_DIR=os.path.join(W, 'unicorn'))
    h = subprocess.run([os.path.join(W, 'h2'), 'jeu.iso', 'sd.img', '70000'], cwd=d, env=env, capture_output=True, text=True, errors='replace')
    open(os.path.join(d, 'h2.txt'), 'w').write(h.stdout + h.stderr)
    if not os.path.exists(os.path.join(d, 'boot.bin')):
        return '%-8s %-40s PREPARATION : %s' % (nom, g['title'][:40], (h.stdout.strip().splitlines() or ['?'])[-1])
    r = subprocess.run([sys.executable, os.path.join(T, 'emu_game.py'), 'boot.bin', 'sd.img', 'jeu.iso', secs, 'q'], cwd=d, env=env, capture_output=True, text=True, errors='replace')
    txt = r.stdout + r.stderr; open(os.path.join(d, 'emu.txt'), 'w').write(txt)
    m = re.search(r'fin : (\d+) instructions, (\d+) images', txt); img = int(m.group(2)) if m else 0
    c = re.search(r'carte virtuelle : (\d+) commandes', txt); cartes = int(c.group(1)) if c else 0
    l = re.search(r'vrai lecteur : (\d+) commandes', txt)
    hx = re.search(r'LECTURES HORS EXPORT : (\d+) secteurs, 1re a ([0-9A-F]+)', txt)
    if hx and ('ARRET DU JEU' in txt or 'EXCEPTION' in txt or 'ERREUR' in txt or img < 10): etat = 'LIMITE DU BANC : lit des donnees pas exportees (%s)' % gcb.fichier(g, int(hx.group(2), 16))
    elif 'ARRET DU JEU' in txt: etat = 'ARRET DU JEU (erreur du jeu : souvent un fichier pas exporte)'
    elif 'EXCEPTION' in txt or 'ERREUR emulateur' in txt: etat = 'PLANTAGE (voir emu.txt)'
    elif img < 10: etat = 'PAS D\'IMAGE'
    else: etat = 'tourne'
    return '%-8s %-40s %-60s images %4d, carte %3d commandes' % (nom, g['title'][:40], etat, img, cartes)
lignes = []
with concurrent.futures.ThreadPoolExecutor(JOBS) as ex:
    for l in ex.map(essai, sorted(glob.glob(os.path.join(src, '*.gcb')))):
        lignes.append(l); print(l); sys.stdout.flush()
open(os.path.join(src, 'rapport.txt'), 'w').write('\n'.join(lignes) + '\n')
print('rapport :', os.path.join(src, 'rapport.txt'))
