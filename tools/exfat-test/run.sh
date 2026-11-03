#!/bin/sh
# v326 : banc d'essai sur PC du montage exFAT (FatFs) : sh tools/exfat-test/run.sh (depuis la racine du depot)
set -e
D=tools/exfat-test; W=$D/work; mkdir -p $W
cc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DEXF_HOST_TEST -Wall -Wno-unused-function \
   -Isource/fatfs $D/test.c source/fatfs/ff.c source/fatfs/ffunicode.c source/fatfs/ffsystem.c -o $W/exfat-test
$W/exfat-test $W/exfat.img
