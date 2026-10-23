#!/bin/sh
# GC Station sur PC : carte SD de demonstration (dossier "sd:" du dossier donne).
# Besoins : ffmpeg, python3 (Pillow facultatif). Usage : sh tools/pc-sim/demo-sd.sh <dossier>
set -e
D=${1:-tools/pc-sim/work/run}; S="$D/sd:"
rm -rf "$S"; mkdir -p "$S/mp3/Demo Album" "$S/photos" "$S/games" "$S/videos" "$S/retro"
T=$(mktemp -d)
# pochette (degrade + disque), photos
python3 - "$T" "$S" <<'PY'
import sys, math
T, S = sys.argv[1], sys.argv[2]
try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit(0)
def grad(w, h, c1, c2):
    im = Image.new('RGB', (w, h)); px = im.load()
    for y in range(h):
        for x in range(w):
            t = (x + y) / (w + h); px[x, y] = tuple(int(c1[i] * (1 - t) + c2[i] * t) for i in range(3))
    return im
c = grad(300, 300, (40, 10, 90), (240, 120, 60)); d = ImageDraw.Draw(c)
d.ellipse((60, 60, 240, 240), fill=(20, 20, 30)); d.ellipse((135, 135, 165, 165), fill=(230, 200, 90))
c.save(T + '/cover.jpg', quality=90)
for i, (a, b) in enumerate([((10, 60, 120), (120, 220, 255)), ((90, 40, 10), (250, 200, 80)), ((10, 80, 30), (180, 240, 120))]):
    p = grad(640, 480, a, b); d = ImageDraw.Draw(p)
    for k in range(6): d.ellipse((60 + k * 90, 300 - (k % 2) * 60, 140 + k * 90, 380 - (k % 2) * 60), outline=(255, 255, 255), width=4)
    p.save('%s/photos/photo-%d.jpg' % (S, i + 1), quality=88)
PY
tone() {  # tone <fichier> <titre> <artiste> <piste> <frequences...>
    f=$1; ti=$2; ar=$3; tr=$4; shift 4
    e=""; for q in "$@"; do e="$e+0.18*sin(2*PI*$q*t)*(0.6+0.4*sin(2*PI*2*t))"; done
    set -- -y -loglevel error -f lavfi -i "aevalsrc=${e#+}:s=44100:d=20"
    case "$f" in
    *.mp3) if [ -f "$T/cover.jpg" ]; then ffmpeg "$@" -i "$T/cover.jpg" -map 0:a -map 1:v -c:a libmp3lame -b:a 128k -id3v2_version 3 -c:v copy -metadata:s:v title="Album cover" -metadata:s:v comment="Cover (front)" -metadata title="$ti" -metadata artist="$ar" -metadata album="Demo Album" -metadata track="$tr" -metadata date=2026 "$f"
           else ffmpeg "$@" -c:a libmp3lame -b:a 128k -metadata title="$ti" -metadata artist="$ar" -metadata album="Demo Album" -metadata track="$tr" "$f"; fi ;;
    *.flac) ffmpeg "$@" -metadata title="$ti" -metadata artist="$ar" -metadata album="Demo Album" -metadata track="$tr" "$f" ;;
    *.ogg) ffmpeg "$@" -c:a libvorbis -metadata title="$ti" -metadata artist="$ar" -metadata album="Demo Album" -metadata track="$tr" "$f" ;;
    esac
}
tone "$S/mp3/Demo Album/01 - Aurore.mp3" "Aurore" "GC Station Band" 1 261.63 329.63 392
tone "$S/mp3/Demo Album/02 - Minuit.mp3" "Minuit" "GC Station Band" 2 220 277.18 329.63
tone "$S/mp3/Demo Album/03 - Lagon.flac" "Lagon" "Les Testeurs" 3 196 246.94 293.66
tone "$S/mp3/04 - Vent du nord.ogg" "Vent du nord" "Les Testeurs" 4 174.61 220 261.63
[ -f "$T/cover.jpg" ] && cp "$T/cover.jpg" "$S/mp3/Demo Album/cover.jpg"
# un faux jeu GameCube (le meme que le banc d'essai du lanceur maison)
R=$(cd "$(dirname "$0")/../.." && pwd)
if command -v clang >/dev/null && command -v ld.lld >/dev/null; then
    clang --target=powerpc-none-eabi -mcpu=750 -c "$R/tools/natif-test/game.s" -o "$T/game.o" && ld.lld -Ttext=0x80003100 -e _start "$T/game.o" -o "$T/game.elf" \
      && llvm-objcopy -O binary -j .text "$T/game.elf" "$T/game.text" && python3 "$R/tools/natif-test/mkiso.py" "$T/game.text" "$S/games/Faux jeu de test.iso"
fi
rm -rf "$T"
echo "carte SD de demonstration : $S"
