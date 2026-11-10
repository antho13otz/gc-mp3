#!/bin/bash
# GC Station PC (Windows), compile depuis Linux (MinGW 32 bits) : image ubuntu:24.04. Pas d'essai sur un vrai Windows
# (pas de Windows gratuit sur GitLab). Usage : bash ci/pc.sh NUMERO -> GC-Station-PC.zip (artefact prive)
set -euo pipefail
N=$1
export DEBIAN_FRONTEND=noninteractive
A="-o Acquire::Retries=5 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30"
apt-get $A update -q >/dev/null
apt-get $A install -y -q --no-install-recommends clang lld llvm gcc-mingw-w64-i686 g++-mingw-w64-i686 zip make git curl python3 ca-certificates >/dev/null
# (threads POSIX : la variante "posix" de MinGW, avec winpthreads)
update-alternatives --set i686-w64-mingw32-gcc /usr/bin/i686-w64-mingw32-gcc-posix || true
update-alternatives --set i686-w64-mingw32-g++ /usr/bin/i686-w64-mingw32-g++-posix || true
[ -d pc/work/libs/mbedtls ] || sh pc/fetch.sh
make -C pc -j"$(nproc)" BUILD=$N 2>&1 | grep -v "^i686-w64-mingw32-g" || true
test -s pc/work/gcstation.exe
rm -rf paquet; mkdir -p paquet/sd
i686-w64-mingw32-strip -o "paquet/GC Station.exe" pc/work/gcstation.exe
cp pc/win/LISEZMOI.txt paquet/
( cd paquet && zip -q -r ../GC-Station-PC.zip . )
cp pc/work/gcstation.exe gcstation-symboles.exe     # (avec les symboles : pour lire un plantage)
ls -la GC-Station-PC.zip
