#!/bin/sh
# Banc d'essai radio / musique : genere (une fois) dans tools/pc-sim/work/net-test les certificats de test,
# les flux (MP3, AAC, OGG, HLS) et les images. Besoins : openssl, ffmpeg, python3 + Pillow.
set -e
R=$(pwd); T=$R/tools/pc-sim/net-test; W=$R/tools/pc-sim/work/net-test
mkdir -p $W/hls; cd $W
[ -f ca.pem ] || openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days 3650 -subj "/CN=GC Test CA" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" 2>/dev/null
if [ ! -f srv2.pem ]; then
    SAN=$(sed 's/\([^,]*\)/DNS:\1/g' $T/hosts.txt)
    printf "subjectAltName=$SAN\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n" > ext.cnf
    openssl req -newkey rsa:2048 -nodes -keyout srv2.key -out srv2.csr -subj "/CN=radio.test" 2>/dev/null
    openssl x509 -req -in srv2.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out srv2.pem -days 3650 -extfile ext.cnf 2>/dev/null
fi
[ -f flux.mp3 ] || ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=440:duration=120" -ac 2 -ar 44100 -b:a 128k flux.mp3
[ -f flux.aac ] || ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=330:duration=60" -ac 2 -ar 44100 -c:a aac -b:a 128k -f adts flux.aac
[ -f flux.ogg ] || ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=550:duration=60" -ac 2 -ar 44100 -c:a libvorbis -b:a 128k flux.ogg
[ -f hls/seg0.ts ] || ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=620:duration=40" -ac 2 -ar 44100 -c:a aac -b:a 96k \
    -f hls -hls_time 4 -hls_list_size 0 -hls_segment_filename "hls/seg%d.ts" hls/vod.m3u8
[ -f grande.jpg ] || python3 -c "
from PIL import Image
im=Image.new('RGB',(250,250)); px=im.load()
for x in range(250):
  for y in range(250): px[x,y]=((x*7)%256,(y*5)%256,(x*y)%256)
im.save('front-250.jpg',quality=90); im.resize((1200,1200)).save('grande.jpg',quality=92)"
cp $T/net_srv.py $W/
cat $R/tools/pc-sim/work/libs/cacert.pem ca.pem > cacert-test.pem
echo "pret : $W"
