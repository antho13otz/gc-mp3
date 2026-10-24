#!/bin/sh
# GC Station : banc d'essai du lanceur natif (sur PC, sans console).
# Besoins : clang, ld.lld, llvm-objcopy, gcc, cmake, python3, git.
# Lancer depuis la racine du depot : sh tools/natif-test/run.sh
set -e
R=$(pwd); T=$R/tools/natif-test; W=$T/work; mkdir -p $W; cd $W
[ -d unicorn ] || git clone -q --depth 1 https://github.com/unicorn-engine/unicorn
[ -f unicorn/build/libunicorn.so ] || (mkdir -p unicorn/build && cd unicorn/build && cmake .. -DUNICORN_ARCH=ppc -DCMAKE_BUILD_TYPE=Release -DUNICORN_BUILD_TESTS=OFF >/dev/null && make -j8 >/dev/null)
[ -d libogc ] || git clone -q --depth 1 https://github.com/devkitPro/libogc
export UNICORN_DIR=$W/unicorn
# 1) programme resident (clang, PowerPC 750)
clang --target=powerpc-none-eabi -mcpu=750 -Os -ffreestanding -fno-builtin -nostdlib -fno-pic -msoft-float -Wall -Wextra -c $R/source/natres/natres.c -o natres.o
ld.lld -T $R/tools/natres.ld natres.o -o natres.elf
llvm-objcopy -O binary natres.elf natres.bin
python3 $R/tools/bin2h.py natres.bin natres_bin.h natres_bin
for a in "1 2 0" "0 2 0" "1 0 0" "1 1 0" "1 0 2"; do python3 $T/emu_natres.py natres.bin $a | tail -1; done
EMU_NODMA=1 python3 $T/emu_natres.py natres.bin 1 0 0 | tail -1     # (v94 : adaptateur sans DMA)
EMU_BAD32=1 python3 $T/emu_natres.py natres.bin 1 0 0 | tail -1     # (v94 : 32 MHz qui abime les donnees)
# (natif.c sans la partie QR code / telephone, qui depend du reste de GC Station)
sed '/^\/\* ---------- v51 : le rapport sur le telephone/,$d' $R/source/natif.c > natif.c
echo 'static void nat_qr_open(const char *id) { (void)id; }' >> natif.c
# 2) partie console de natif.c : verification de syntaxe avec les en-tetes de libogc
mkdir -p fakeinc/gnu; touch fakeinc/gnu/stubs-32.h
printf '#pragma once\n#include <stdbool.h>\n#include <sys/types.h>\nbool fatMountSimple(const char*, const void*);\nvoid fatUnmount(const char*);\n' > fakeinc/fat.h
sed -n '/^\/\* ---------- lecture des disques/,/^static int gl_id_ok/p' $R/source/gclib.c | sed '$d' > gd.inc
clang --target=powerpc-linux-gnu -DNATIF_BENCH -fsyntax-only -Wall -Wno-unused-function -Wno-unused-variable -DHW_DOL -D__gamecube__ -Ilibogc/gc -Ifakeinc -I. -I$R/source -isystem /usr/include -isystem /usr/include/x86_64-linux-gnu $T/gk.c && echo "console : syntaxe OK"
# 3) bout en bout : faux jeu -> ISO -> carte SD FAT32 -> lancement natif -> emulateur
gcc -DNATIF_BENCH -Wall -Wno-unused-function -Wno-unused-variable -I. -I$R/source -o h2 $T/h2.c
clang --target=powerpc-none-eabi -mcpu=750 -c $T/game.s -o game.o && ld.lld -Ttext=0x80003100 -e _start game.o -o game.elf && llvm-objcopy -O binary -j .text game.elf game.text
for x in reboot prog2; do clang --target=powerpc-none-eabi -mcpu=750 -c $T/$x.s -o $x.o && ld.lld -Ttext=0x80400000 -e _start $x.o -o $x.elf && llvm-objcopy -O binary -j .text $x.elf $x.text; done   # (v95 : OSExec)
python3 $T/mkiso.py game.text game.iso reboot.text prog2.text
# (v64 : faux .dol de GC Station, pour le retour depuis le jeu)
clang --target=powerpc-none-eabi -mcpu=750 -c $T/selfdol.s -o selfdol.o && ld.lld -Ttext=0x80004000 -e _start selfdol.o -o selfdol.elf && llvm-objcopy -O binary -j .text selfdol.elf selfdol.text
python3 -c "import struct,sys; t=open('selfdol.text','rb').read(); t+=b'\0'*(-len(t)%32); h=bytearray(0x100); struct.pack_into('>I',h,0,0x100); struct.pack_into('>I',h,0x48,0x80004000); struct.pack_into('>I',h,0x90,len(t)); struct.pack_into('>I',h,0xE0,0x80004000); open('selfdol.dol','wb').write(bytes(h)+t)"
python3 $T/mkfat.py 32 game.iso sd.img 1 selfdol.dol 2097152 >/dev/null     # (v65 : + la carte memoire virtuelle)
rm -rf out; mkdir -p out; NATIF_SELF=selfdol.dol NATIF_DUMP=boot.bin ./h2 game.iso sd.img 10 >/dev/null
python3 $T/emu_e2e.py boot.bin sd.img game.iso | tail -1
EMU_SISTALE=1 python3 $T/emu_e2e.py boot.bin sd.img game.iso | tail -1     # (v99 : ports vides a 0 : A+Z+START doit marcher quand meme)
python3 $T/emu_e2e.py boot.bin sd.img game.iso exc | tail -1     # (v60 : plantage du jeu -> journal + redemarrage)
python3 $T/emu_e2e.py boot.bin sd.img game.iso exec | tail -1    # (v95 : OSExec : code de redemarrage et programme suivant branches au faux lecteur)
python3 $T/mkiso.py game.text game-a.iso reboot.text prog2.text 2026/10/22 && python3 $T/mkfat.py 32 game-a.iso sd-a.img 1 selfdol.dol 2097152 >/dev/null
NATIF_SELF=selfdol.dol NATIF_DUMP=boot-a.bin ./h2 game-a.iso sd-a.img 10 >/dev/null
python3 $T/emu_e2e.py boot-a.bin sd-a.img game-a.iso execa | tail -1   # (v95 : OSExec, apploader recent : .dol recopie a sa place)
