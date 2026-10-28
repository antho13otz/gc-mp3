#!/bin/sh
# v201 : banc d'essai du moteur de page sur de vraies pages (Wikipedia, Le Monde, BBC, Guardian... : les pages de test
# de mozilla/readability, telechargees dans tools/pc-sim/work/vraies-pages). Mise en page seule (sans reseau ni
# ecran : PCSIM_WEBDUMP), puis un PNG par page (3 colonnes de 1100 points) dans tools/pc-sim/work/vraies-pages/png.
#   sh tools/pc-sim/websrv/vraies-pages.sh [page ...]      (noms de dossiers de test-pages ; sans : toutes)
#   ASAN=1 : avec AddressSanitizer (binaire gcstation-asan, voir asan.sh)
P=$(cd "$(dirname "$0")/.." && pwd); W=$P/work/vraies-pages
[ -d $W/rd ] || git clone -q --depth 1 --filter=blob:none --sparse https://github.com/mozilla/readability.git $W/rd && (cd $W/rd && git sparse-checkout set test/test-pages)
make -s -C $P -j"$(nproc 2>/dev/null || echo 2)" || exit 1
BIN=$P/gcstation; [ -n "$ASAN" ] && BIN=$P/gcstation-asan
mkdir -p $W/png $W/json
L="$*"; [ -z "$L" ] && L=$(ls $W/rd/test/test-pages)
for p in $L; do
    f=$W/rd/test/test-pages/$p/source.html; [ -f $f ] || continue
    r=$(cd $P && ASAN_OPTIONS=detect_leaks=0 PCSIM_WEBDUMP=$f PCSIM_WEBDUMP_OUT=$W/json/$p.json timeout 120 $BIN 2>&1 | grep -E "^\[webdump\]|ERROR")
    echo "$p : $r"
    [ -f $W/json/$p.json ] && python3 $P/websrv/rendu.py $W/json/$p.json $W/png/$p.png 0 1100 3 > /dev/null
done
