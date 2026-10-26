#!/bin/sh
# GC Station sur PC : un morceau difficile par format audio (fluidite de lecture), dans <dossier>/sd:/mp3/Formats.
# Bruit rose + accords (le plus lourd a decoder / le moins compressible), 30 s chacun.
# Usage : sh tools/pc-sim/formats-sd.sh <dossier du simulateur>   (puis scenarios/son-formats.txt)
set -e
D="$1/sd:/mp3/Formats"; mkdir -p "$D"
[ -f "$D/1 mp3 320k.mp3" ]   || ffmpeg -v error -y -f lavfi -i "anoisesrc=c=pink:a=0.25:r=44100:d=30" -f lavfi -i "sine=f=220:r=44100:d=30" -filter_complex "[0][1]amix=inputs=2" -ac 2 -c:a libmp3lame -b:a 320k "$D/1 mp3 320k.mp3"
[ -f "$D/2 flac 24-96.flac" ] || ffmpeg -v error -y -f lavfi -i "anoisesrc=c=pink:a=0.25:r=96000:d=30" -f lavfi -i "sine=f=220:r=96000:d=30" -filter_complex "[0][1]amix=inputs=2" -ac 2 -sample_fmt s32 -bits_per_raw_sample 24 "$D/2 flac 24-96.flac"
[ -f "$D/3 ogg q8.ogg" ]      || ffmpeg -v error -y -f lavfi -i "anoisesrc=c=pink:a=0.25:r=48000:d=30" -f lavfi -i "sine=f=220:r=48000:d=30" -filter_complex "[0][1]amix=inputs=2" -ac 2 -c:a libvorbis -q:a 8 "$D/3 ogg q8.ogg"
[ -f "$D/4 wav.wav" ]         || ffmpeg -v error -y -f lavfi -i "anoisesrc=c=pink:a=0.25:r=44100:d=30" -f lavfi -i "sine=f=220:r=44100:d=30" -filter_complex "[0][1]amix=inputs=2" -ac 2 -c:a pcm_s16le "$D/4 wav.wav"
[ -f "$D/5 aac 256k.m4a" ]    || ffmpeg -v error -y -f lavfi -i "anoisesrc=c=pink:a=0.25:r=44100:d=30" -f lavfi -i "sine=f=220:r=44100:d=30" -filter_complex "[0][1]amix=inputs=2" -ac 2 -c:a aac -b:a 256k "$D/5 aac 256k.m4a"
ls -la "$D"
