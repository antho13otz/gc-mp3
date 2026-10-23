#!/bin/sh
# GC Station sur PC : carte SD "lourde" (pour mesurer les limites de la GameCube).
# Usage : sh tools/pc-sim/stress-sd.sh <dossier>
set -e
D=${1:-tools/pc-sim/work/stress}; S="$D/sd:"
rm -rf "$S"; mkdir -p "$S/mp3" "$S/photos"
python3 - "$S" <<'PY'
import sys, random
from PIL import Image, ImageDraw, ImageFilter
S = sys.argv[1]; random.seed(5)
def photo(w, h, n):
    im = Image.new('RGB', (w, h)); d = ImageDraw.Draw(im)
    for i in range(400):                       # beaucoup de details : JPEG lourd, comme une vraie photo
        x, y, r = random.randrange(w), random.randrange(h), random.randrange(10, w // 6)
        d.ellipse((x - r, y - r, x + r, y + r), fill=tuple(random.randrange(256) for _ in range(3)))
    im = im.filter(ImageFilter.GaussianBlur(1.2))
    px = im.load()
    for i in range(w * h // 40): x, y = random.randrange(w), random.randrange(h); px[x, y] = tuple(random.randrange(256) for _ in range(3))
    im.save('%s/photos/%s.jpg' % (S, n), quality=92)
for i in range(6): photo(1920, 1080, 'fullhd-%d' % (i + 1))
photo(1280, 720, 'hd-1')
photo(4000, 3000, 'appareil-12mpx')
PY
ffmpeg -y -loglevel error -f lavfi -i "aevalsrc=0.2*sin(2*PI*440*t)+0.1*sin(2*PI*660*t)+0.05*random(0):s=96000:d=60" -c:a flac -sample_fmt s32 -bits_per_raw_sample 24 -metadata title="HiRes 24-96" -metadata artist="Stress" "$S/mp3/01 - HiRes 24-96.flac"
ffmpeg -y -loglevel error -f lavfi -i "aevalsrc=0.2*sin(2*PI*330*t)+0.05*random(0)|0.2*sin(2*PI*495*t)+0.05*random(0):s=44100:d=60" -c:a libmp3lame -b:a 320k -metadata title="MP3 320" -metadata artist="Stress" "$S/mp3/02 - MP3 320.mp3"
ls -la "$S/photos" "$S/mp3" | grep -v "^total"
