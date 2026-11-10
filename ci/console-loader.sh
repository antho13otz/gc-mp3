#!/bin/bash
# Lanceur de jeux (moteur de Swiss, GPL v2, version figee, modifie par tools/mkloader.py) : gc-mp3-loader.bin + gc-mp3-igr.img.
# Image : ghcr.io/extremscorner/libogc2:latest. Recompile seulement si mkloader.py, tools/loader/ ou la version de Swiss changent
# (sinon : la copie du cache GitLab, dossier ci-cache/loader).
set -euo pipefail
SWISS_COMMIT=99b59e7a042cb5633cd341d86b7b92a65cd39851
S=$(command -v sudo || true)
# (v144 : des accents re-encodes plusieurs fois, "Ã©" au lieu de "é")
if grep -rlP '\xc3\x83\xc2|\xc3\x82\xc2' source; then echo "Accents abimes dans ces fichiers"; exit 1; fi
K=$( { echo $SWISS_COMMIT; cat tools/mkloader.py; find tools/loader -type f -not -path '*__pycache__*' | sort | xargs cat; } | sha256sum | cut -d' ' -f1)
C=ci-cache/loader; mkdir -p $C
if [ "$(cat $C/cle 2>/dev/null)" = "$K" ] && [ -s $C/gc-mp3-loader.bin ] && [ -s $C/gc-mp3-igr.img ]; then
    echo "lanceur deja compile (cache)"
else
    $S apt-get update -q
    $S apt-get install -y -q --no-install-recommends -t bookworm-backports 7zip gcc gcc-mingw-w64-i686 libc6-dev libc6-i386 xorriso
    $S dkp-pacman -Sy
    $S dkp-pacman -S --noconfirm --ask 4 ppc-libiconv-lite
    rm -rf work-swiss
    for i in 1 2 3; do git clone -q https://github.com/emukidid/swiss-gc.git work-swiss && break; rm -rf work-swiss; sleep $((i * 5)); done   # (build 915 : coupures)
    git -C work-swiss checkout -q $SWISS_COMMIT
    git -C work-swiss submodule -q update --init --recursive
    git config --global --add safe.directory "$PWD/work-swiss"
    python3 tools/mkloader.py work-swiss tools/loader
    ( cd work-swiss
      set +e; make compile-patches compile compile-packer > ../swiss-build.log 2>&1; st=$?; set -e
      tail -n 40 ../swiss-build.log
      if [ $st -ne 0 ]; then grep -n -i "error\|undefined" ../swiss-build.log | head -n 25; exit $st; fi
      cp cube/packer/swiss.dol ../$C/gc-mp3-loader.bin
      # v45 : retour en jeu (A + Z + START) : le meme lanceur, dans un apploader.img
      python3 buildtools/dol2ipl.py ../$C/gc-mp3-igr.img cube/packer/reboot.dol "*gc-mp3-b0" )
    echo "$K" > $C/cle
fi
cp $C/gc-mp3-loader.bin $C/gc-mp3-igr.img .
python3 -I ci/verif_lanceur.py gc-mp3-loader.bin
