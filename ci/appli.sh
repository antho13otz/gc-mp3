#!/bin/bash
# Appli Android (GC Remote / telecommande) : image cimg/android (SDK + NDK). Usage : bash ci/appli.sh NUMERO
# La cle de signature vient de la variable protegee ANDROID_DEBUG_KEYSTORE_B64 (Settings > CI/CD > Variables) :
# toujours la meme cle, pour que chaque APK s'installe par-dessus le precedent. Sans elle : echec (jamais une cle au hasard).
set -euo pipefail
N=$1
GRADLE_V=8.11.1
[ -n "${ANDROID_DEBUG_KEYSTORE_B64:-}" ] || { echo "ERREUR : variable ANDROID_DEBUG_KEYSTORE_B64 absente (cle de signature)"; exit 1; }
export ANDROID_USER_HOME=/tmp/android-user     # (hors du dossier du cache : la cle ne va jamais dans le cache)
mkdir -p "$ANDROID_USER_HOME"
echo "$ANDROID_DEBUG_KEYSTORE_B64" | base64 -d > "$ANDROID_USER_HOME/debug.keystore"

# build 375 : emulateur GBA de l'appli : mGBA (MPL 2.0), version figee 0.10.5
# v393 / appli 491 : RetroAchievements : rcheevos (licence MIT), version figee (commit du 5 novembre 2026)
RC=android/app/src/main/cpp/rcheevos
if [ ! -d $RC/.git ]; then
  git init -q $RC && git -C $RC remote add origin https://github.com/RetroAchievements/rcheevos.git
  git -C $RC fetch -q --depth 1 origin eea6461f172d37d36b0d56568f13a505e0a9fd86 && git -C $RC checkout -q FETCH_HEAD
fi
[ "$(git -C $RC rev-parse HEAD)" = eea6461f172d37d36b0d56568f13a505e0a9fd86 ] || { echo "rcheevos : version inattendue"; exit 1; }
M=android/app/src/main/cpp/mgba
[ -d $M/.git ] || git clone -q --depth 1 --branch 0.10.5 https://github.com/mgba-emu/mgba.git $M
c=$(git -C $M rev-parse HEAD)
[ "$c" = 26b7884bc25a5933960f3cdcd98bac1ae14d42e2 ] || { echo "mGBA : version inattendue ($c)"; exit 1; }

G=ci-cache/gradle-$GRADLE_V
if [ ! -x $G/bin/gradle ]; then
  curl -fsSL --retry 3 -o gradle.zip https://services.gradle.org/distributions/gradle-$GRADLE_V-bin.zip
  mkdir -p ci-cache && rm -rf $G && unzip -q gradle.zip -d ci-cache && rm gradle.zip
fi
export GRADLE_USER_HOME=$PWD/ci-cache/gradle-home
# (build 457 : deux variantes ; ce job ne compile que la privee, celle du proprietaire ; la publique : ci/appli-publique.sh)
BUILD_NUM=$N $G/bin/gradle -p android --no-daemon assemblePriveDebug 2>&1 | tee build.log \
  || { grep -E '^e: |error:|What went wrong|AAPT|ERROR' -A3 build.log | head -80; exit 1; }
cp android/app/build/outputs/apk/prive/debug/app-prive-debug.apk gc-station-telecommande-$N.apk

# meme signature que android/signature.txt : sinon Android refuserait l'installation par-dessus
as=$(ls -d "$ANDROID_HOME"/build-tools/*/apksigner | sort -V | tail -1)
fp=$("$as" verify --print-certs gc-station-telecommande-$N.apk | grep -m1 'SHA-256 digest' | sed 's/.*: *//' | tr -d ' ')
echo "Empreinte de la signature : $fp"
want=$(grep -v '^#' android/signature.txt | tr -d ' \r\n')
[ "$want" = "$fp" ] || { echo "ERREUR : signature differente de android/signature.txt"; exit 1; }
ls -la gc-station-telecommande-$N.apk
