#!/bin/bash
# Build 457 : version publique allegee de l'appli (« GC Remote Lite », fr.gcstation.remote). Usage : bash ci/appli-publique.sh NUMERO
# Sa propre cle (pas celle de l'appli privee) : variables protegees ANDROID_PUBLIC_KEYSTORE_B64 (le fichier .jks en base64)
# et ANDROID_PUBLIC_KEYSTORE_PASS (son mot de passe), alias « gcremote ». Sans elles : echec (jamais une cle au hasard).
# Puis publication dans le paquet generique PUBLIC « appli-publique » (version « dernier ») : version.json + l'APK ;
# l'appli publique se met a jour seule depuis la, sans jeton. Ne JAMAIS publier ici l'APK prive.
set -euo pipefail
N=$1
GRADLE_V=8.11.1
[ -n "${ANDROID_PUBLIC_KEYSTORE_B64:-}" ] || { echo "ERREUR : variable ANDROID_PUBLIC_KEYSTORE_B64 absente (cle de la version publique)"; exit 1; }
[ -n "${ANDROID_PUBLIC_KEYSTORE_PASS:-}" ] || { echo "ERREUR : variable ANDROID_PUBLIC_KEYSTORE_PASS absente"; exit 1; }
K=/tmp/cle-publique.jks
echo "$ANDROID_PUBLIC_KEYSTORE_B64" | base64 -d > $K
export PUBLIC_KEYSTORE=$K PUBLIC_KEYSTORE_PASS="$ANDROID_PUBLIC_KEYSTORE_PASS" PUBLIC_KEY_ALIAS=gcremote
export ANDROID_USER_HOME=/tmp/android-user; mkdir -p "$ANDROID_USER_HOME"

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
BUILD_NUM=$N $G/bin/gradle -p android --no-daemon assemblePubliqueDebug 2>&1 | tee build.log \
  || { grep -E '^e: |error:|What went wrong|AAPT|ERROR' -A3 build.log | head -80; exit 1; }
A=gc-remote-lite-$N.apk
cp android/app/build/outputs/apk/publique/debug/app-publique-debug.apk $A
rm -f $K

# verifications : bien la variante publique (identifiant), bien signee avec la cle publique (pas la cle de test d'Android)
aa=$(ls -d "$ANDROID_HOME"/build-tools/*/aapt2 | sort -V | tail -1)
# (build 486 : sortie lue en entier d'abord : avec pipefail, grep -q qui s'arrete au 1er resultat coupait aapt2 (SIGPIPE)
#  et la verification echouait meme sur le bon APK)
bad=$("$aa" dump badging $A 2>/dev/null || true)
[[ "$bad" == *"package: name='fr.gcstation.remote'"* ]] || { echo "ERREUR : ce n'est pas la version publique"; exit 1; }
as=$(ls -d "$ANDROID_HOME"/build-tools/*/apksigner | sort -V | tail -1)
cert=$("$as" verify --print-certs $A 2>&1 || true)
# (build 512 : l'empreinte exacte, pas seulement le nom : l'ancienne cle « CN=GC Remote Lite » passait et aurait publie
#  une appli que les telephones refusent d'installer par-dessus)
# (« Signer #1 certificate SHA-256 digest: … » ; selon la version d'apksigner : « Signer (minSdkVersion=…) … » : on prend
#  la 1re ligne qui donne une empreinte SHA-256, quel que soit son debut)
fp=$(printf '%s\n' "$cert" | grep -a 'certificate SHA-256 digest:' | head -1 | awk '{print $NF}' | tr -d ' \r' || true)
want=$(grep -v '^#' android/signature-publique.txt | tr -d ' \r\n')
echo "Empreinte de la signature : $fp"
[ -n "$fp" ] && [ "$fp" = "$want" ] || { printf '%s\n' "$cert" | grep -av JAVA_TOOL_OPTIONS | head -20; echo "ERREUR : signature differente de android/signature-publique.txt (mauvaise cle dans les variables ?)"; exit 1; }
# (aucune classe privee dans l'APK : extraction d'un .iso, export des packs, Salon. Build 486 : la vue 3D, le compagnon et
#  l'installation des packs sont aussi dans la version publique, demande du proprietaire ; le Salon public est une coquille vide)
for cl in IsoTelKt ExtraireFichier ExportPacks FichiersGC DiscordPresence; do
  if unzip -p $A 'classes*.dex' | grep -aq "fr/gcstation/telecommande/$cl"; then echo "ERREUR : classe privee $cl dans l'APK public"; exit 1; fi
done
printf '{"build": %s, "apk": "%s"}\n' "$N" "$A" > version.json
# (appli 493 : aussi sous un nom fixe, gc-remote-lite.apk : une adresse qui ne change jamais, pour le lien court is.gd)
cp $A gc-remote-lite.apk
python3 -I tools/publish_gitlab.py fichiers appli-publique $A gc-remote-lite.apk version.json   # (version.json en dernier : il n'annonce jamais un fichier absent)
ls -la $A
