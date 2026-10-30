#!/bin/sh
# GC Station : banc d'essai du lanceur natif (sur PC, sans console).
# Besoins : clang, ld.lld, llvm-objcopy, gcc, cmake, python3, git.
# Lancer depuis la racine du depot : sh tools/natif-test/run.sh
set -e
R=$(pwd); T=$R/tools/natif-test; W=$T/work; mkdir -p $W; cd $W
[ -d unicorn ] || git clone -q --depth 1 https://github.com/unicorn-engine/unicorn
[ -f unicorn/build/libunicorn.so ] || (mkdir -p unicorn/build && cd unicorn/build && cmake .. -DUNICORN_ARCH=ppc -DCMAKE_BUILD_TYPE=Release -DUNICORN_BUILD_TESTS=OFF >/dev/null && make -j8 >/dev/null)
[ -d libogc ] || git clone -q --depth 1 https://github.com/devkitPro/libogc
python3 $T/uc_patch.py unicorn || (cd unicorn/build && make -j8 >/dev/null)   # (v171 : pour emu_game.py, sans effet sur le reste)
export UNICORN_DIR=$W/unicorn
# 1) programme resident (clang, PowerPC 750)
clang --target=powerpc-none-eabi -mcpu=750 -Os -ffreestanding -fno-builtin -nostdlib -fno-pic -msoft-float -Wall -Wextra -c $R/source/natres/natres.c -o natres.o
ld.lld -T $R/tools/natres.ld natres.o -o natres.elf
llvm-objcopy -O binary natres.elf natres.bin
python3 $R/tools/bin2h.py natres.bin natres_bin.h natres_bin
for a in "1 2 0" "0 2 0" "1 0 0" "1 1 0" "1 0 2"; do python3 $T/emu_natres.py natres.bin $a | tail -1; done
EMU_ACH=$(llvm-nm natres.elf | awk '$3=="ach_run"{print $1}') EMU_DIREADY=$(llvm-nm natres.elf | awk '$3=="di_ready"{print $1}') NATRES_H=$R/source/natres/natres.h python3 $T/emu_natres.py natres.bin 1 2 0 | tail -2   # (v268 : succes)
python3 $T/emu_pad.py natres.bin natres.elf $R/source/natres/natres.h | tail -1   # (prive : manettes du telephone)
EMU_NODMA=1 python3 $T/emu_natres.py natres.bin 1 0 0 | tail -1     # (v94 : adaptateur sans DMA)
EMU_BAD32=1 python3 $T/emu_natres.py natres.bin 1 0 0 | tail -1     # (v94 : 32 MHz qui abime les donnees)
# v187 : retour a GC Station en quittant une appli (natret en 0x80001800) : SD2SP2 SDHC, SD Gecko slot A carte SD, slot B
clang --target=powerpc-none-eabi -mcpu=750 -Os -ffreestanding -fno-builtin -nostdlib -fno-pic -msoft-float -Wall -Wextra -c $R/source/natres/natret.c -o natret.o
ld.lld -T $R/tools/natret.ld natret.o -o natret.elf && llvm-objcopy -O binary natret.elf natret.bin
for a in "1 2" "0 0" "1 1"; do python3 $T/emu_natret.py natret.bin $a | tail -1; done
python3 $T/emu_appstub.py $R/source/gclib.c natret.bin | tail -1     # (v189 : lancement d'une appli : la copie et le retour se gardent)
# (natif.c sans la partie QR code / telephone, qui depend du reste de GC Station)
sed '/^\/\* ---------- v51 : le rapport sur le telephone/,$d' $R/source/natif.c > natif.c
echo 'static void nat_qr_open(const char *id) { (void)id; }' >> natif.c
# 2) partie console de natif.c : verification de syntaxe avec les en-tetes de libogc
mkdir -p fakeinc/gnu; touch fakeinc/gnu/stubs-32.h
printf '#pragma once\n#include <stdbool.h>\n#include <sys/types.h>\nbool fatMountSimple(const char*, const void*);\nvoid fatUnmount(const char*);\n' > fakeinc/fat.h
sed -n '/^\/\* ---------- lecture des disques/,/^static int gl_id_ok/p' $R/source/gclib.c | sed '$d' > gd.inc
cp $T/v262.inc .                                    # (bouchons v239 / v262 : main.c, tab_phone.c)
clang --target=powerpc-linux-gnu -DNATIF_BENCH -fsyntax-only -Wall -Wno-unused-function -Wno-unused-variable -DHW_DOL -D__gamecube__ -Ilibogc/gc -Ifakeinc -I. -I$R/source -isystem /usr/include -isystem /usr/include/x86_64-linux-gnu $T/gk.c && echo "console : syntaxe OK"
# 3) bout en bout : faux jeu -> ISO -> carte SD FAT32 -> lancement natif -> emulateur
gcc -DNATIF_BENCH -Wall -Wno-unused-function -Wno-unused-variable -I. -I$R/source -o h2 $T/h2.c -lz
sed -n '1,/^static int na_rawsec/p' $T/h2.c > t146.c && cat $T/t146.c >> t146.c && gcc -DNATIF_BENCH -Wall -Wno-unused-function -Wno-unused-variable -I. -I$R/source -o t146 t146.c -lz && ./t146 | tail -1   # (v146)
clang --target=powerpc-none-eabi -mcpu=750 -c $T/game.s -o game.o && ld.lld -Ttext=0x80003100 -e _start game.o -o game.elf && llvm-objcopy -O binary -j .text game.elf game.text
for x in reboot prog2; do clang --target=powerpc-none-eabi -mcpu=750 -c $T/$x.s -o $x.o && ld.lld -Ttext=0x80400000 -e _start $x.o -o $x.elf && llvm-objcopy -O binary -j .text $x.elf $x.text; done   # (v95 : OSExec)
python3 $T/mkiso.py game.text game.iso reboot.text prog2.text
# (v64 : faux .dol de GC Station, pour le retour depuis le jeu)
clang --target=powerpc-none-eabi -mcpu=750 -c $T/selfdol.s -o selfdol.o && ld.lld -Ttext=0x80004000 -e _start selfdol.o -o selfdol.elf && llvm-objcopy -O binary -j .text selfdol.elf selfdol.text
python3 -c "import struct,sys; t=open('selfdol.text','rb').read(); t+=b'\0'*(-len(t)%32); h=bytearray(0x100); struct.pack_into('>I',h,0,0x100); struct.pack_into('>I',h,0x48,0x80004000); struct.pack_into('>I',h,0x90,len(t)); struct.pack_into('>I',h,0xE0,0x80004000); open('selfdol.dol','wb').write(bytes(h)+t)"
python3 $T/mkfat.py 32 game.iso sd.img 1 selfdol.dol 2097152 >/dev/null     # (v65 : + la carte memoire virtuelle)
rm -rf out; mkdir -p out; NATIF_SELF=selfdol.dol NATIF_DUMP=boot.bin ./h2 game.iso sd.img 10 >/dev/null
python3 $T/emu_e2e.py boot.bin sd.img game.iso | tail -1
EMU_EXIBUSY=2000 python3 $T/emu_e2e.py boot.bin sd.img game.iso | tail -1   # (v145 : bus EXI pris par le jeu : lectures remises a plus tard)
EMU_SISTALE=1 python3 $T/emu_e2e.py boot.bin sd.img game.iso | tail -1     # (v99 : ports vides a 0 : A+Z+START doit marcher quand meme)
python3 $T/emu_e2e.py boot.bin sd.img game.iso exc | tail -1     # (v60 : plantage du jeu -> journal + redemarrage)
python3 $T/emu_e2e.py boot.bin sd.img game.iso exec | tail -1    # (v95 : OSExec : code de redemarrage et programme suivant branches au faux lecteur)
python3 $T/mkiso.py game.text game-a.iso reboot.text prog2.text 2026/10/22 && python3 $T/mkfat.py 32 game-a.iso sd-a.img 1 selfdol.dol 2097152 >/dev/null
NATIF_SELF=selfdol.dol NATIF_DUMP=boot-a.bin ./h2 game-a.iso sd-a.img 10 >/dev/null
python3 $T/emu_e2e.py boot-a.bin sd-a.img game-a.iso execa | tail -1   # (v95 : OSExec, apploader recent : .dol recopie a sa place)
# v121 : .ciso (un bloc absent = des zeros), jeux a 2 disques, cheats (codes Gecko), 16:9 force
python3 - <<'PY'
import struct
iso = bytearray(open('game.iso', 'rb').read()); bs = 0x8000
iso[9 * bs:10 * bs] = bytes(bs)                       # (bloc 9 : des zeros -> absent du .ciso)
open('game-c.iso', 'wb').write(iso)
hdr = bytearray(0x8000); hdr[0:4] = b'CISO'; struct.pack_into('<I', hdr, 4, bs)
body = bytearray()
for b in range(len(iso) // bs):
    blk = iso[b * bs:(b + 1) * bs]
    if any(blk): hdr[8 + b] = 1; body += blk
open('game.ciso', 'wb').write(hdr + body)
d2 = bytearray(open('game.iso', 'rb').read()); d2[6] = 1; d2[0x1000:0x1200] = bytes(x ^ 0xFF for x in d2[0x1000:0x1200])
open('disc2.iso', 'wb').write(d2)
PY
python3 $T/mkfat.py 32 game.ciso sd-c.img 1 selfdol.dol 2097152 >/dev/null
NATIF_SELF=selfdol.dol NATIF_DUMP=boot-c.bin ./h2 game.ciso sd-c.img 10 ciso >/dev/null
python3 $T/emu_e2e.py boot-c.bin sd-c.img game-c.iso | tail -1                  # (.ciso : meme jeu, bloc absent lu comme des zeros)
python3 $T/mkfat.py 32 game.iso sd-s.img 1 selfdol.dol 2097152 disc2.iso >/dev/null
NATIF_SELF=selfdol.dol NATIF_DISC2=disc2.iso NATIF_DUMP=boot-s.bin ./h2 game.iso sd-s.img 10 >/dev/null
python3 $T/emu_e2e.py boot-s.bin sd-s.img game.iso swap disc2.iso | tail -1      # (changement de disque)
printf '04001F70 12345678\n20001F74 00000000\n02001F78 0000ABCD\nE2000001 00000000\n20001F7C 00000001\n04001F80 DEADBEEF\nE2000001 00000000\n4A000000 80001F00\n14000084 CAFEBABE\nDE000000 80008180\n14000088 11111111\nE0000000 80008000\nC0000000 00000002\n3C608000 90631F8C\n4E800020 00000000\n00001F62 0001005A\nF0000000 00000000\n' > cheats.txt
NATIF_SELF=selfdol.dol NATIF_CHEATS=cheats.txt NATIF_WS=1 NATIF_DUMP=boot-g.bin ./h2 game.iso sd.img 10 > h2-g.txt
python3 $T/emu_e2e.py boot-g.bin sd.img game.iso cheat | tail -1                # (cheats appliques par Pulse)
python3 $T/emu_e2e.py boot-g.bin sd.img game.iso ws | tail -1                   # (16:9 force : GXSetProjection)
# v122 : carte du slot B, manettes (stick C inverse, seuil des gachettes, frequence), lecture apres la fin (.nkit), modes video
python3 $T/mkfat.py 32 game.iso sd-b.img 1 selfdol.dol 2097152 disc2.iso 2097152 >/dev/null
NATIF_SELF=selfdol.dol NATIF_CARD=2 NATIF_CSTICK=1 NATIF_TRIG=2 NATIF_POLL=2 NATIF_DUMP=boot-b.bin ./h2 game.iso sd-b.img 10 > h2-b.txt
python3 $T/emu_e2e.py boot-b.bin sd-b.img game.iso cardb | tail -1               # (carte memoire du slot B)
python3 $T/emu_e2e.py boot-b.bin sd-b.img game.iso pad | tail -1                 # (manettes, lecture apres la fin du fichier)
NATIF_SELF=selfdol.dol NATIF_VMODE=1 NATIF_WIDE=1 NATIF_DUMP=boot-v.bin ./h2 game.iso sd.img 10 > h2-v.txt
python3 $T/emu_e2e.py boot-v.bin sd.img game.iso video | tail -1               # (480p force, image large)
# v133 : fins de commande : sans le vrai lecteur (console sans lecteur), et reveil perdu (secours a l'image)
NATIF_SELF=selfdol.dol NATIF_NODRIVE=1 NATIF_DUMP=boot-t.bin ./h2 game.iso sd.img 10 > h2-t.txt
EMU_NODRIVE=1 python3 $T/emu_e2e.py boot-t.bin sd.img game.iso tick | tail -1
python3 $T/emu_e2e.py boot.bin sd.img game.iso tick | tail -1
# v139 : Pulse universel : acces au lecteur que personne n'a remplaces, pris par exception (option du jeu)
NATIF_SELF=selfdol.dol NATIF_TRAP=1 NATIF_DUMP=boot-x.bin ./h2 game.iso sd.img 10 > h2-x.txt
python3 $T/emu_e2e.py boot-x.bin sd.img game.iso trap | tail -1
# v143 : Pulse par le reseau : ISO lue par le Broadband Adapter sur un serveur (simule : ARP, UDP, une reponse perdue)
NATIF_SELF=selfdol.dol NATIF_NET=1 NATIF_DUMP=boot-n.bin ./h2 game.iso sd.img 10 > h2-n.txt
python3 $T/emu_e2e.py boot-n.bin sd.img game.iso net | tail -1
# v152 : Rogue Squadron III : programme compresse (zlib en 0x80200000) decompresse, branche, recompresse a la meme place
python3 $T/mkiso_z.py game.text game-z.iso && python3 $T/mkfat.py 32 game-z.iso sd-z.img 1 selfdol.dol 2097152 >/dev/null
NATIF_SELF=selfdol.dol NATIF_TRAP=1 NATIF_DUMP=boot-z.bin ./h2 game-z.iso sd-z.img 10 > h2-z.txt
python3 $T/check_z.py boot-z.bin | tail -1
# v153 : jeu en reseau local : le jeu se sert du BBA (canal 0, peripherique 2) avec la carte virtuelle du slot A et Pulse
python3 $T/emu_e2e.py boot.bin sd.img game.iso lan | tail -1
# v167 : BSS mis a zero avant le saut (Crash Bandicoot GCBP7D) : morceaux hors des sections, puis le programme de saut
awk '/^static u32 na_bss_gaps\(/{f=1} f{print} f&&/^}$/{exit}' $R/source/natif.c > na_bss_gaps.inc && gcc -O1 -Wall -I. -o t167 $T/t167.c && ./t167 | tail -1
python3 $T/t167stub.py $R/source/natif.c | tail -1
