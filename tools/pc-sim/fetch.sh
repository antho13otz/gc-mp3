#!/bin/sh
# GC Station sur PC : recupere les bibliotheques aux MEMES versions que .github/workflows/build.yml,
# mais avec git (dans les sessions ou seul git vers GitHub passe). Empreintes verifiees.
# Lancer depuis la racine du depot : sh tools/pc-sim/fetch.sh
set -e
R=$(pwd); W=$R/tools/pc-sim/work/src; mkdir -p $W; cd $W
get() {   # get <depot> <commit> <dossier>
    [ -d "$3/.git" ] && return 0
    rm -rf "$3"; git init -q "$3"; git -C "$3" remote add origin "https://github.com/$1"
    git -C "$3" fetch -q --depth 1 origin "$2" && git -C "$3" checkout -q FETCH_HEAD
}
get lieff/minimp3 master minimp3
get nothings/stb a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d stb
get Mbed-TLS/mbedtls mbedtls-3.6.7 mbedtls
get earlephilhower/ESP8266Audio e1f2a1a34aa460f5cca7a60b69d9a350fd099938 esp8266audio
get mackron/dr_libs dfe8377631000664666519fdb83da193fd8037f4 dr_libs
get libgme/game-music-emu 9e23d10f9fd2a6a2f33b10912dd8dc7153258995 gme
get libxmp/libxmp 5d4f6aa2c7dcdf8e9cd8129fe579d45b36a302da xmp
get webmproject/libwebp 4fa21912338357f89e4fd51cf2368325b59e9bd9 webp
get nayuki/QR-Code-generator 3c6d0b3cefb4e049dc337e82237c9644399716a8 qr
# meme disposition que le workflow (fichiers dans source/, dossiers a la racine)
O=$R/tools/pc-sim/work/libs; rm -rf $O; mkdir -p $O/source
cp minimp3/minimp3.h stb/stb_image.h stb/stb_vorbis.c dr_libs/dr_flac.h qr/c/qrcodegen.c qr/c/qrcodegen.h $O/source/
echo "111144e778f55738db6851cb226015c419e00d04b916a09506d4856d9cff945c  $O/source/dr_flac.h" | sha256sum -c - >/dev/null
echo "4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636  $O/source/stb_vorbis.c" | sha256sum -c - >/dev/null
echo "6a2b9cc65176f2345dde260c74b6d352627e8a0a6385d086ae0e9c5d0913c70c  $O/source/qrcodegen.c" | sha256sum -c - >/dev/null
cp -r mbedtls $O/mbedtls; rm -rf $O/mbedtls/.git
mkdir -p $O/helix
F="aaccommon.h aacdec.c aacdec.h aactabs.c assembly.h bitstream.c bitstream.h buffers.c coder.h dct4.c decelmnt.c dequant.c fft.c filefmt.c huffmanaac.c hufftabs.c imdct.c noiseless.c pns.c sbr.c sbr.h sbrfft.c sbrfreq.c sbrhfadj.c sbrhfgen.c sbrhuff.c sbrimdct.c sbrmath.c sbrqmf.c sbrside.c sbrtabs.c statname.h stproc.c tns.c trigtabs.c"
for f in $F; do cp esp8266audio/src/libhelix-aac/$f $O/helix/; done
H=$(cd $O/helix && for f in $F; do cat $f; done | sha256sum | cut -d' ' -f1)
test "$H" = "cee275f6970b6f458f4b4d1e2d42b976fc3abad99d262d7e25b1ccc614a69ff2"
# gme / xmp / webp : memes archives que GitHub (git archive), extraites par tools/fetch_libs.py
git -C gme archive --prefix=game-music-emu-9e23d10f9fd2a6a2f33b10912dd8dc7153258995/ -o $O/gme.tar.gz HEAD
git -C xmp archive --prefix=libxmp-5d4f6aa2c7dcdf8e9cd8129fe579d45b36a302da/ -o $O/xmp.tar.gz HEAD
git -C webp archive --prefix=libwebp-4fa21912338357f89e4fd51cf2368325b59e9bd9/ -o $O/webp.tar.gz HEAD
(cd $O && python3 $R/tools/fetch_libs.py gme.tar.gz xmp.tar.gz webp.tar.gz >/dev/null && rm -f *.tar.gz)
cp /etc/ssl/certs/ca-certificates.crt $O/cacert.pem
python3 $R/tools/pem2h.py $O/cacert.pem $O/source/cacert.h >/dev/null
echo "bibliotheques pretes dans tools/pc-sim/work/libs"
