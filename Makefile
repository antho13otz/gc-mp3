ifeq  ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC n'est pas defini : installe devkitPro / devkitPPC")
endif

include $(DEVKITPPC)/gamecube_rules

# Les lignes de commande commencent par ">" au lieu d'une tabulation
# (evite les problemes de copier-coller depuis un telephone).
.RECIPEPREFIX := >

TARGET   := gc-mp3player

# mbedTLS (HTTPS) et Helix (AAC) : sources telechargees par le workflow
MBEDTLS  := mbedtls
HELIX    := helix
TLSDEF   := -DMBEDTLS_USER_CONFIG_FILE='"gc_tls_config.h"'
MBED_SRC := $(wildcard $(MBEDTLS)/library/*.c)
MBED_OBJ := $(patsubst $(MBEDTLS)/library/%.c,build/mbedtls/%.o,$(MBED_SRC))
HELIX_SRC:= $(wildcard $(HELIX)/*.c)
HELIX_OBJ:= $(patsubst $(HELIX)/%.c,build/helix/%.o,$(HELIX_SRC))
# FLAC (dr_flac) et OGG Vorbis (stb_vorbis) : telecharges par le workflow, compiles dans codecs.c
CODEC_OBJ:= build/codecs.o
# Musique de jeux retro : game-music-emu (C++) et libxmp complete (modules),
# telecharges par le workflow (tools/fetch_libs.py)
GME      := gme
XMP      := xmp
GME_SRC  := $(wildcard $(GME)/*.cpp)
GME_OBJ  := $(patsubst $(GME)/%.cpp,build/gme/%.o,$(GME_SRC)) build/gme/emu2413.o
XMP_SRC  := $(wildcard $(XMP)/src/*.c) $(wildcard $(XMP)/src/loaders/*.c)
XMP_OBJ  := $(patsubst $(XMP)/src/%.c,build/xmp/%.o,$(XMP_SRC))
GMEDEF   := -DBLARGG_BIG_ENDIAN=1 -DVGM_YM2612_GENS -DUSE_GME_AY -DUSE_GME_GBS \
            -DUSE_GME_GYM -DUSE_GME_HES -DUSE_GME_KSS -DUSE_GME_NSF -DUSE_GME_NSFE \
            -DUSE_GME_SAP -DUSE_GME_SPC -DUSE_GME_VGM
GMEFLAGS := -O2 $(MACHDEP) $(GMEDEF) -DNDEBUG -I$(GME) -w -fwrapv
# decodeur WebP (libwebp, fichiers generiques seulement)
WEBP     := webp
WEBP_SRC := $(wildcard $(WEBP)/src/dec/*.c) $(wildcard $(WEBP)/src/dsp/*.c) $(wildcard $(WEBP)/src/utils/*.c)
WEBP_OBJ := $(patsubst $(WEBP)/src/%.c,build/webp/%.o,$(WEBP_SRC))
WEBPFLAGS:= -O2 $(MACHDEP) -fno-asynchronous-unwind-tables -fno-unwind-tables -DNDEBUG -fwrapv -I$(WEBP) -w
XMPFLAGS := -O2 $(MACHDEP) -fno-asynchronous-unwind-tables -fno-unwind-tables -fwrapv -DLIBXMP_STATIC -DLIBXMP_NO_PROWIZARD -DLIBXMP_NO_DEPACKERS -I$(XMP)/include -I$(XMP)/src -w

# numero de compilation (GitHub Actions) : sert a la mise a jour en un clic
BUILD_NUM ?= 0
CFLAGS   := -O2 -fwrapv -Wall $(MACHDEP) -fno-asynchronous-unwind-tables -fno-unwind-tables -DGCMP3_BUILD=$(BUILD_NUM) -D__POWERPC__ -I$(LIBOGC_INC) -Isource -I$(MBEDTLS)/include -I$(HELIX) -Isource/helixstub -I$(GME) -I$(XMP)/include -I$(WEBP) -DLIBXMP_STATIC $(TLSDEF)
MBEDFLAGS:= -O2 $(MACHDEP) -fno-asynchronous-unwind-tables -fno-unwind-tables -I$(LIBOGC_INC) -Isource -I$(MBEDTLS)/include -I$(MBEDTLS)/library $(TLSDEF)
HELIXFLAGS:= -O2 $(MACHDEP) -D__POWERPC__ -Isource/helixstub -I$(HELIX) -DUSE_DEFAULT_STDLIB -Wno-unknown-pragmas
LDFLAGS  := $(MACHDEP) -Wl,-Map,$(TARGET).map
# v236 : -fno-asynchronous-unwind-tables -fno-unwind-tables : pas de tables de deroulement (.eh_frame,
# ~400 Ko de donnees) pour le C : seul le C++ (gme) les garde
# libbba = reseau (lwIP + Broadband Adapter) de libogc pour GameCube
# reseau plus rapide (tools/fast_net.py) : utilisee si le workflow a pu la construire
NETFAST  := $(wildcard netfast/libbba_fast.a)
LIBS     := -L$(LIBOGC_LIB) -lfat $(NETFAST) -lbba -logc -lstdc++ -lm

NM       ?= powerpc-eabi-gcc-nm
OBJCOPY  ?= powerpc-eabi-objcopy

# v49 : programme resident du lanceur natif (source/natres) : compile a part, sans
# bibliotheque, lie a une adresse fixe (tools/natres.ld), puis range dans build/natres_bin.h
# v398 : -Os (Pulse trop gros pour le mode trap) ; v399 : retour a -O2 pour tous les jeux (Rayman 3 restait noir avec le
# Pulse -Os) ; le Pulse -Os (natres_os_bin, natres_wz_os_bin) ne sert qu'au mode Pulse universel (natif.c, na_petit).
NATRESFLAGS := -O2 -mcpu=750 -meabi -msoft-float -G 0 -msdata=none -fno-pic -ffreestanding -fno-builtin \
               -fno-asynchronous-unwind-tables -fno-exceptions -nostdlib -nostartfiles -Wall

all: $(TARGET).dol

build/mbedtls/%.o: $(MBEDTLS)/library/%.c source/gc_tls_config.h
> @mkdir -p build/mbedtls
> $(CC) $(MBEDFLAGS) -c $< -o $@

build/codecs.o: source/codecs.c source/dr_flac.h source/stb_vorbis.c source/qrcodegen.c source/qrcodegen.h
> @mkdir -p build
> $(CC) -O2 $(MACHDEP) -fno-asynchronous-unwind-tables -fno-unwind-tables -Isource -w -c $< -o $@

build/gme/%.o: $(GME)/%.cpp
> @mkdir -p build/gme
> $(CXX) $(GMEFLAGS) -c $< -o $@

build/gme/emu2413.o: $(GME)/ext/emu2413.c
> @mkdir -p build/gme
> $(CC) $(GMEFLAGS) -c $< -o $@

build/xmp/%.o: $(XMP)/src/%.c
> @mkdir -p $(dir $@)
> $(CC) $(XMPFLAGS) -c $< -o $@

build/webp/%.o: $(WEBP)/src/%.c
> @mkdir -p $(dir $@)
> $(CC) $(WEBPFLAGS) -c $< -o $@

build/helix/%.o: $(HELIX)/%.c
> @mkdir -p build/helix
> $(CC) $(HELIXFLAGS) -c $< -o $@

# v210 : Cube Browser (source/cb, licence MIT, Arial-Bold) : le navigateur de l'onglet WEB
CB_SRC   := $(wildcard source/cb/*.c)
CB_OBJ   := $(patsubst source/cb/%.c,build/cb/%.o,$(CB_SRC))
build/cb/%.o: source/cb/%.c $(wildcard source/cb/*.h) source/gc_tls_config.h
> @mkdir -p build/cb
> $(CC) $(CFLAGS) -std=gnu11 -Wno-format-truncation -Isource/cb -ffunction-sections -fdata-sections -c $< -o $@

# v326 : FatFs (ChaN, licence BSD a 1 clause) : cartes SD en exFAT (source/exfat_sd.c)
FATFS_OBJ:= build/fatfs/ff.o build/fatfs/ffunicode.o build/fatfs/ffsystem.o
build/fatfs/%.o: source/fatfs/%.c source/fatfs/ffconf.h source/fatfs/ff.h source/fatfs/ff_prefix.h
> @mkdir -p build/fatfs
> $(CC) -O2 $(MACHDEP) -fno-asynchronous-unwind-tables -fno-unwind-tables -Isource/fatfs -w -c $< -o $@

LIBOBJ   := $(MBED_OBJ) $(HELIX_OBJ) $(CODEC_OBJ) $(GME_OBJ) $(XMP_OBJ) $(WEBP_OBJ) $(CB_OBJ) $(FATFS_OBJ)
MAINDEPS := $(wildcard source/*.c source/*.h source/natres/*.h)

# build 908 : deux Pulse : Broadband Adapter seul (PULSE_NET=1, natres_bin) et W5500 seul (PULSE_NET=2, natres_wz_bin) ;
# GC Station met le bon selon l'adaptateur branche (natif.c, na_rb)
build/natres.elf: source/natres/natres.c source/natres/natres.h tools/natres.ld
> @mkdir -p build
> $(CC) $(NATRESFLAGS) -DPULSE_NET=1 -Wl,-T,tools/natres.ld -Wl,--no-gc-sections $< -o $@ -lgcc || { \
>   $(CC) $(NATRESFLAGS) -DPULSE_NET=1 -Wl,-T,tools/natres.ld -Wl,--no-gc-sections -Wl,--noinhibit-exec $< -o build/natres-gros.elf -lgcc; \
>   echo "::error::resident Pulse trop gros : fin a $$(powerpc-eabi-nm build/natres-gros.elf | grep __natres_end) (max 817FFF00)"; exit 1; }
> @echo "::notice::resident Pulse BBA : fin a $$(powerpc-eabi-nm $@ | grep __natres_end) (max 817FFF00)"


# v399 : les memes en -Os, pour le mode Pulse universel (trap) : doivent finir sous 817FFC00 (sinon la compilation echoue)
build/natres_os.elf: source/natres/natres.c source/natres/natres.h tools/natres.ld
> @mkdir -p build
> $(CC) $(subst -O2,-Os,$(NATRESFLAGS)) -DPULSE_NET=1 -Wl,-T,tools/natres.ld -Wl,--no-gc-sections $< -o $@ -lgcc
> @e=$$(powerpc-eabi-nm $@ | awk '/__natres_end/{print $$1}'); echo "::notice::resident Pulse BBA -Os (mode trap) : fin a $$e (max 817FFC00)"; \
>   [ $$((0x$$e)) -le $$((0x817FFC00)) ] || { echo "::error::resident Pulse BBA -Os : fin a $$e, au-dessus de 817FFC00"; exit 1; }
build/natres_wz_os.elf: source/natres/natres.c source/natres/natres.h tools/natres.ld
> @mkdir -p build
> $(CC) $(subst -O2,-Os,$(NATRESFLAGS)) -DPULSE_NET=2 -Wl,-T,tools/natres.ld -Wl,--no-gc-sections $< -o $@ -lgcc
> @e=$$(powerpc-eabi-nm $@ | awk '/__natres_end/{print $$1}'); echo "::notice::resident Pulse W5500 -Os (mode trap) : fin a $$e (max 817FFC00)"; \
>   [ $$((0x$$e)) -le $$((0x817FFC00)) ] || { echo "::error::resident Pulse W5500 -Os : fin a $$e, au-dessus de 817FFC00"; exit 1; }
build/natres_os_bin.h: build/natres_os.elf tools/bin2h.py
> $(OBJCOPY) -O binary build/natres_os.elf build/natres_os.bin
> python3 tools/bin2h.py build/natres_os.bin $@ natres_os_bin
build/natres_wz_os_bin.h: build/natres_wz_os.elf tools/bin2h.py
> $(OBJCOPY) -O binary build/natres_wz_os.elf build/natres_wz_os.bin
> python3 tools/bin2h.py build/natres_wz_os.bin $@ natres_wz_os_bin

build/natres_bin.h: build/natres.elf tools/bin2h.py
> $(OBJCOPY) -O binary build/natres.elf build/natres.bin
> python3 tools/bin2h.py build/natres.bin $@ natres_bin

build/natres_wz.elf: source/natres/natres.c source/natres/natres.h tools/natres.ld
> @mkdir -p build
> $(CC) $(NATRESFLAGS) -DPULSE_NET=2 -Wl,-T,tools/natres.ld -Wl,--no-gc-sections $< -o $@ -lgcc || { \
>   echo "::error::resident Pulse W5500 trop gros (max 817FFF00)"; exit 1; }
> @echo "::notice::resident Pulse W5500 : fin a $$(powerpc-eabi-nm $@ | grep __natres_end) (max 817FFF00)"

build/natres_wz_bin.h: build/natres_wz.elf tools/bin2h.py
> $(OBJCOPY) -O binary build/natres_wz.elf build/natres_wz.bin
> python3 tools/bin2h.py build/natres_wz.bin $@ natres_wz_bin

# v359 : Pulse LAN (jeu en reseau local par Internet) : le meme resident, sans telephone ni chercheur de codes, avec le pont
build/natres_lan.elf: source/natres/natres.c source/natres/natres.h tools/natres.ld
> $(CC) $(NATRESFLAGS) -DNATRES_LAN -Wl,-T,tools/natres.ld -Wl,--no-gc-sections $< -o $@ -lgcc
> @echo "::notice::Pulse LAN : fin a $$(powerpc-eabi-nm $@ | grep __natres_end) (max 817FFF00)"
build/natres_lan_bin.h: build/natres_lan.elf tools/bin2h.py
> $(OBJCOPY) -O binary build/natres_lan.elf build/natres_lan.bin
> python3 tools/bin2h.py build/natres_lan.bin $@ natres_lan_bin

# v187 : retour a GC Station en quittant une appli (source/natres/natret.c), en 0x80001800 (-Os : place comptee)
build/natret.elf: source/natres/natret.c tools/natret.ld
> @mkdir -p build
> $(CC) $(subst -O2,-Os,$(NATRESFLAGS)) -Wl,-T,tools/natret.ld -Wl,--no-gc-sections $< -o $@ -lgcc

build/natret_bin.h: build/natret.elf tools/bin2h.py
> $(OBJCOPY) -O binary build/natret.elf build/natret.bin
> python3 tools/bin2h.py build/natret.bin $@ natret_bin

# v397 : bandeau des succes sur la tele (source/natres/bandeau.c) : du code seul (pas de donnees, pas de table de sauts),
# pose par GC Station sous Pulse a une adresse qui change d'un jeu a l'autre ; tools/bandeau.ld refuse toute donnee
# (-lgcc : en -Os, gcc appelle _savegpr / _restgpr, du code seul a branchements relatifs)
build/bandeau.elf: source/natres/bandeau.c source/natres/bandeau.h tools/bandeau.ld
> @mkdir -p build
> $(CC) $(subst -O2,-Os,$(NATRESFLAGS)) -fno-jump-tables -fno-tree-loop-distribute-patterns -fno-tree-switch-conversion -Wl,-T,tools/bandeau.ld -Wl,--no-gc-sections $< -o $@ -lgcc
> @echo "::notice::bandeau des succes : $$(powerpc-eabi-nm $@ | grep __ban_fin) octets de code"

build/bandeau_bin.h: build/bandeau.elf tools/bin2h.py
> $(OBJCOPY) -O binary build/bandeau.elf build/bandeau.bin
> python3 tools/bin2h.py build/bandeau.bin $@ bandeau_bin

build/main.o: $(MAINDEPS) build/natres_bin.h build/natres_wz_bin.h build/natres_os_bin.h build/natres_wz_os_bin.h build/natres_lan_bin.h build/natret_bin.h build/bandeau_bin.h
> @mkdir -p build
> $(CC) $(CFLAGS) -Ibuild -DUSE_DEFAULT_STDLIB -c source/main.c -o $@

# Journal de plantage : 1re edition des liens avec une table des symboles vide,
# puis la vraie table (noms des fonctions). Si le code a bouge entre les deux,
# on garde la 1re version (le journal donnera alors des adresses seules).
build/symtab0.o: tools/mksym.py
> @mkdir -p build
> python3 tools/mksym.py --empty build/symtab0.c
> $(CC) -O2 $(MACHDEP) -c build/symtab0.c -o $@

build/pass1.elf: build/main.o build/symtab0.o $(LIBOBJ)
> $(CC) build/main.o build/symtab0.o $(LIBOBJ) $(LDFLAGS) $(LIBS) -o $@

build/symtab.o: build/pass1.elf tools/mksym.py
> python3 tools/mksym.py $(NM) build/pass1.elf build/symtab.c || python3 tools/mksym.py --empty build/symtab.c
> $(CC) -O2 $(MACHDEP) -c build/symtab.c -o $@

$(TARGET).elf: build/main.o build/symtab.o $(LIBOBJ)
> $(CC) build/main.o build/symtab.o $(LIBOBJ) $(LDFLAGS) $(LIBS) -o $@
> python3 tools/mksym.py --check $(NM) build/pass1.elf $@ || cp build/pass1.elf $@

clean:
> rm -rf build $(TARGET).elf $(TARGET).dol $(TARGET).map
