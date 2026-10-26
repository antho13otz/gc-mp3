#!/bin/sh
# v158 : GC Station sur PC avec AddressSanitizer (debordements de tableaux, memoire liberee puis relue...).
#   sh tools/pc-sim/asan.sh scenario1 scenario2 ...      (noms de tools/pc-sim/scenarios, sans .txt)
# Construit un binaire a part (tools/pc-sim/gcstation-asan, objets dans work/obj-asan), lance chaque scenario sans
# les limites de la GameCube (PCSIM_FAST=1 : plus rapide), et affiche les erreurs trouvees (pile d'appels).
set -e
R=$(pwd); P=$R/tools/pc-sim; RUN=$P/work/run
[ -d "$RUN/sd:/mp3" ] || sh $P/demo-sd.sh $RUN
make -s -C $P -j"$(nproc 2>/dev/null || echo 2)" O=work/obj-asan BIN=gcstation-asan SAN="-fsanitize=address -fno-omit-frame-pointer"
set +e
for sc in "$@"; do
    (cd $RUN && rm -rf out && ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 PCSIM_FAST=1 PCSIM_SCRIPT=$P/scenarios/$sc.txt PCSIM_OUT=out timeout 900 $P/gcstation-asan > asan-$sc.log 2>&1)
    n=$(grep -c "ERROR: AddressSanitizer" $RUN/asan-$sc.log)
    echo "== $sc : $n erreur(s)"
    grep -A14 "ERROR: AddressSanitizer" $RUN/asan-$sc.log | grep "ERROR\|#[0-9] \|READ\|WRITE" | head -12
done
