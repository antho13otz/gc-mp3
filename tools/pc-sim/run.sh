#!/bin/sh
# GC Station sur PC : tout en une commande (depuis la racine du depot).
#   sh tools/pc-sim/run.sh                      -> tour de tous les onglets (scenarios/tour.txt)
#   sh tools/pc-sim/run.sh scenarios/mon.txt    -> un autre scenario
# Resultat : tools/pc-sim/work/run/out/*.png (captures), planche.png (toutes), son.wav, log.txt
set -e
R=$(pwd); P=$R/tools/pc-sim
SC=${1:-$P/scenarios/tour.txt}; case "$SC" in /*) ;; *) [ -f "$SC" ] || SC=$P/$SC; SC=$(cd "$(dirname "$SC")" && pwd)/$(basename "$SC") ;; esac
[ -d $P/work/libs/mbedtls ] || sh $P/fetch.sh
make -s -C $P -j"$(nproc 2>/dev/null || echo 2)"
RUN=$P/work/run; mkdir -p $RUN
# memoire de la GameCube : taille du vrai programme (le .dol de la release "dernier"), si on peut le lire
if [ -z "$PCSIM_DOL" ] && [ ! -f $P/work/gc.dol ] && command -v gh >/dev/null; then
    ID=$(gh api repos/antho13otz/gc-mp3/releases/tags/dernier --jq '.assets[] | select(.name|endswith(".dol")) | .id' 2>/dev/null | head -1)
    [ -n "$ID" ] && gh api -H "Accept: application/octet-stream" repos/antho13otz/gc-mp3/releases/assets/$ID > $P/work/gc.dol 2>/dev/null || rm -f $P/work/gc.dol
fi
[ -z "$PCSIM_DOL" ] && [ -s $P/work/gc.dol ] && export PCSIM_DOL=$P/work/gc.dol
[ -d "$RUN/sd:/mp3" ] || sh $P/demo-sd.sh $RUN
cd $RUN && rm -rf out
PCSIM_SCRIPT=$SC PCSIM_OUT=out PCSIM_WAV=out/son.wav timeout ${PCSIM_TIMEOUT:-300} $P/gcstation > log.txt 2>&1 || { echo "GC Station s'est arrete (code $?) : voir $RUN/log.txt"; tail -20 log.txt; exit 1; }
python3 - <<'PY' || true
from PIL import Image, ImageDraw
import glob
fs = sorted(glob.glob('out/*.png'))
if fs:
    W, H = 320, 240
    s = Image.new('RGB', (W * 4, (H + 16) * ((len(fs) + 3) // 4)), (20, 20, 20)); d = ImageDraw.Draw(s)
    for i, f in enumerate(fs):
        x, y = (i % 4) * W, (i // 4) * (H + 16)
        s.paste(Image.open(f).convert('RGB').resize((W, H)), (x, y + 16)); d.text((x + 4, y + 2), f.split('/')[-1], fill=(255, 255, 0))
    s.save('planche.png')
PY
grep "capture" log.txt | wc -l | xargs echo "captures :"
grep -E "\[pcsim\] (GameCube|processeur|memoire|fluidite)" log.txt || true
echo "resultats : $RUN/out (et $RUN/planche.png)"
