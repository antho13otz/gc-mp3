#!/bin/bash
# Compilation de la console (GC Station) : image devkitpro/devkitppc:latest. Meme recette que l'ancien
# .github/workflows/build.yml (bibliotheques aux memes versions, empreintes verifiees).
# Usage : bash ci/console.sh NUMERO   (gc-mp3-loader.bin / gc-mp3-igr.img du job "console-lanceur" a la racine, s'ils existent)
set -euo pipefail
N=$1
# Build 915 : telechargements plus solides : deux compilations de suite (913, 914) se sont arretees sur une coupure
# (curl 56 « connection died », curl 18 « partial file »), que --retry seul ne refait pas. Maintenant : toute erreur est
# refaite (--retry-all-errors), dans un fichier .part (jamais un fichier a moitie), et 4 essais en tout, espaces.
get() {
  for i in 1 2 3 4; do
    if curl -fsSL --connect-timeout 20 --max-time 300 --retry 3 --retry-all-errors --retry-delay 3 -o "$1.part" "$2"; then
      mv -f "$1.part" "$1"; return 0
    fi
    rm -f "$1.part"; echo "telechargement coupe : $2 (essai $i sur 4)" >&2; sleep $((i * 5))
  done
  return 1
}

get source/minimp3.h https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h
get source/stb_image.h https://raw.githubusercontent.com/nothings/stb/a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d/stb_image.h

# mbedTLS 3.6.7 (HTTPS), archive officielle verifiee
get mbedtls.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
echo "a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6  mbedtls.tar.bz2" | sha256sum -c -
rm -rf mbedtls mbedtls-3.6.7
python3 -c "import tarfile; tarfile.open('mbedtls.tar.bz2').extractall('.')"
mv mbedtls-3.6.7 mbedtls

# Decodeur AAC / HE-AAC (Helix, licence RPSL), version figee d'ESP8266Audio
C=e1f2a1a34aa460f5cca7a60b69d9a350fd099938
B=https://raw.githubusercontent.com/earlephilhower/ESP8266Audio/$C/src/libhelix-aac
F="aaccommon.h aacdec.c aacdec.h aactabs.c assembly.h bitstream.c bitstream.h buffers.c coder.h dct4.c decelmnt.c
   dequant.c fft.c filefmt.c huffmanaac.c hufftabs.c imdct.c noiseless.c pns.c sbr.c sbr.h sbrfft.c sbrfreq.c sbrhfadj.c
   sbrhfgen.c sbrhuff.c sbrimdct.c sbrmath.c sbrqmf.c sbrside.c sbrtabs.c statname.h stproc.c tns.c trigtabs.c"
mkdir -p helix
for f in $F; do get helix/$f $B/$f; done
H=$(for f in $F; do cat helix/$f; done | sha256sum | cut -d' ' -f1)
test "$H" = "cee275f6970b6f458f4b4d1e2d42b976fc3abad99d262d7e25b1ccc614a69ff2"

# FLAC (dr_flac) et OGG Vorbis (stb_vorbis), domaine public
get source/dr_flac.h https://raw.githubusercontent.com/mackron/dr_libs/dfe8377631000664666519fdb83da193fd8037f4/dr_flac.h
echo "111144e778f55738db6851cb226015c419e00d04b916a09506d4856d9cff945c  source/dr_flac.h" | sha256sum -c -
get source/stb_vorbis.c https://raw.githubusercontent.com/nothings/stb/a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d/stb_vorbis.c
echo "4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636  source/stb_vorbis.c" | sha256sum -c -

# Musique retro (gme 0.6.5, libxmp 4.7.4) et WebP : archives figees, fichiers utiles extraits et verifies
get gme.tar.gz https://github.com/libgme/game-music-emu/archive/9e23d10f9fd2a6a2f33b10912dd8dc7153258995.tar.gz
get xmp.tar.gz https://github.com/libxmp/libxmp/archive/5d4f6aa2c7dcdf8e9cd8129fe579d45b36a302da.tar.gz
get webp.tar.gz https://github.com/webmproject/libwebp/archive/4fa21912338357f89e4fd51cf2368325b59e9bd9.tar.gz
python3 tools/fetch_libs.py gme.tar.gz xmp.tar.gz webp.tar.gz

# QR code (onglet TELEPHONE), MIT
B=https://raw.githubusercontent.com/nayuki/QR-Code-generator/3c6d0b3cefb4e049dc337e82237c9644399716a8/c
get source/qrcodegen.c $B/qrcodegen.c
get source/qrcodegen.h $B/qrcodegen.h
echo "6a2b9cc65176f2345dde260c74b6d352627e8a0a6385d086ae0e9c5d0913c70c  source/qrcodegen.c" | sha256sum -c -
echo "e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589  source/qrcodegen.h" | sha256sum -c -

# Reseau rapide (pile reseau de libogc recompilee, fenetre TCP 16 paquets + W5500) : ignore en cas de souci
(
  set -e
  HV="$DEVKITPRO/libogc/include/ogc/libversion.h"
  V=$(awk '/define[ \t]+_V_MAJOR_/{a=$3} /define[ \t]+_V_MINOR_/{b=$3} /define[ \t]+_V_PATCH_/{c=$3} END{print a"."b"."c}' "$HV")
  echo "libogc installee : $V"
  get libogc.tar.gz "https://github.com/devkitPro/libogc/archive/refs/tags/v$V.tar.gz"
  python3 tools/fast_net.py libogc.tar.gz
) || echo "ATTENTION : reseau rapide non prepare"
if [ -f netfast/libbba_fast.a ]; then echo "Reseau rapide inclus (fenetre TCP 16 paquets) + adaptateurs W5500"; else echo "ATTENTION : reseau rapide absent : Broadband Adapter seulement, pas de W5500"; fi

# Certificats racines (liste Mozilla)
get cacert.pem https://curl.se/ca/cacert.pem || cp /etc/ssl/certs/ca-certificates.crt cacert.pem
python3 tools/pem2h.py cacert.pem source/cacert.h

make clean
make -j"$(nproc)" BUILD_NUM=$N 2>&1 | tee make.log || { grep -E "error|Error|undefined|\*\*\*" make.log | head -30; exit 1; }

python3 -I ci/dol.py gc-mp3player.dol $N

# v45 : le retour en jeu porte le numero de ce build
if [ -s gc-mp3-igr.img ]; then
  python3 - "$N" <<'PY'
import struct, sys
p = "gc-mp3-igr.img"
d = bytearray(open(p, "rb").read())
d[0:16] = struct.pack("16s", ("*gc-mp3-b%s" % sys.argv[1]).encode())
open(p, "wb").write(d)
print("retour en jeu :", len(d), "octets")
PY
fi
mkdir -p sortie
cp gc-mp3player.dol sortie/gc-mp3player-b$N.dol
[ -s gc-mp3-igr.img ] && cp gc-mp3-igr.img gc-mp3-loader.bin sortie/
ls -la sortie
