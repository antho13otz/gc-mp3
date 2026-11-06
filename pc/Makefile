# GC Station PC (Windows) : sa propre copie du code (source/), sa plateforme (plateforme/ : fenetre, clavier, souris,
# manettes, son, Dolphin, mises a jour) et ses bibliotheques. Rien ici n'est compile pour la console, et rien de la
# console n'est compile ici. Compile avec MinGW 32 bits (adresses sur 32 bits), depuis Linux :
#   apt install clang lld llvm gcc-mingw-w64-i686 g++-mingw-w64-i686   (voir .github/workflows/windows.yml)
#   sh pc/fetch.sh                  (une fois : les bibliotheques, dans pc/work/libs)
#   make -C pc BUILD=123            (-> pc/work/gcstation.exe)
R      := .
W      := work
L      := $(W)/libs
O      := $(W)/win
CC     := i686-w64-mingw32-gcc
CXX    := i686-w64-mingw32-g++
BUILD  ?= 0
INC    := -Iwin/include -Iplateforme/include -I$(W)/gen -I$(L)/source -I$(R)/source -I$(L)/mbedtls/include -I$(L)/helix -I$(R)/source/helixstub -I$(L)/gme -I$(L)/xmp/include -I$(L)/webp
TLS    := -DMBEDTLS_USER_CONFIG_FILE='"gc_tls_config.h"'
GCF    := -O2 -g1 -fno-omit-frame-pointer -fwrapv -fno-strict-aliasing -D__POWERPC__ -DGCMP3_BUILD=$(BUILD) -DGCS_PC=1 -DPHONE_PORT=8080 -DLIBXMP_STATIC $(TLS) $(INC) -include win/pcwin.h
MBED_SRC := $(wildcard $(L)/mbedtls/library/*.c)
HELIX_SRC:= $(wildcard $(L)/helix/*.c)
GME_SRC  := $(wildcard $(L)/gme/*.cpp)
XMP_SRC  := $(wildcard $(L)/xmp/src/*.c) $(wildcard $(L)/xmp/src/loaders/*.c)
WEBP_SRC := $(wildcard $(L)/webp/src/dec/*.c) $(wildcard $(L)/webp/src/dsp/*.c) $(wildcard $(L)/webp/src/utils/*.c)
LIBOBJ   := $(patsubst $(L)/%.c,$(O)/%.o,$(MBED_SRC) $(HELIX_SRC) $(XMP_SRC) $(WEBP_SRC)) $(patsubst $(L)/%.cpp,$(O)/%.o,$(GME_SRC)) $(O)/gme/emu2413.o $(O)/codecs.o \
            $(O)/fatfs/ff.o $(O)/fatfs/ffunicode.o $(O)/fatfs/ffsystem.o
CB_OBJ   := $(patsubst $(R)/source/cb/%.c,$(O)/cb/%.o,$(wildcard $(R)/source/cb/*.c))
SIMOBJ   := $(O)/pcsim.o $(O)/pcsim_gx.o $(O)/pcsim_limits.o $(O)/pcsim_new.o $(O)/pcsim_win.o $(O)/pcsim_dolphin.o $(O)/pcsim_maj.o $(O)/main.o
EXE      := $(W)/gcstation.exe
all: $(EXE)

# (le resident Pulse de la copie : natif.c en a besoin pour compiler, meme si le PC lance les jeux dans Dolphin)
$(W)/gen/natret_bin.h: $(R)/source/natres/natret.c outils/natret.ld
	@mkdir -p $(W)/gen
	clang --target=powerpc-none-eabi -mcpu=750 -Os -ffreestanding -fno-builtin -nostdlib -fno-pic -msoft-float -c $< -o $(W)/gen/natret.o
	ld.lld -T outils/natret.ld $(W)/gen/natret.o -o $(W)/gen/natret.elf
	llvm-objcopy -O binary $(W)/gen/natret.elf $(W)/gen/natret.bin
	python3 outils/bin2h.py $(W)/gen/natret.bin $@ natret_bin
$(W)/gen/natres_bin.h: $(R)/source/natres/natres.c $(R)/source/natres/natres.h outils/natres.ld
	@mkdir -p $(W)/gen
	clang --target=powerpc-none-eabi -mcpu=750 -Oz -ffreestanding -fno-builtin -nostdlib -fno-pic -msoft-float -c $< -o $(W)/gen/natres.o
	ld.lld -T outils/natres.ld $(W)/gen/natres.o -o $(W)/gen/natres.elf
	llvm-objcopy -O binary $(W)/gen/natres.elf $(W)/gen/natres.bin
	python3 outils/bin2h.py $(W)/gen/natres.bin $@ natres_bin
GEN      := $(W)/gen/natret_bin.h $(W)/gen/natres_bin.h

$(O)/mbedtls/%.o: $(L)/mbedtls/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w $(INC) -I$(L)/mbedtls/library $(TLS) -c $< -o $@
$(O)/helix/%.o: $(L)/helix/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -D_WIN32_WCE -I$(R)/source/helixstub -I$(L)/helix -DUSE_DEFAULT_STDLIB -c $< -o $@
$(O)/gme/%.o: $(L)/gme/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -O2 -w -DBLARGG_LITTLE_ENDIAN=1 -DVGM_YM2612_GENS -DUSE_GME_AY -DUSE_GME_GBS -DUSE_GME_GYM -DUSE_GME_HES -DUSE_GME_KSS -DUSE_GME_NSF -DUSE_GME_NSFE -DUSE_GME_SAP -DUSE_GME_SPC -DUSE_GME_VGM -DNDEBUG -I$(L)/gme -fwrapv -c $< -o $@
$(O)/fatfs/%.o: $(R)/source/fatfs/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -I$(R)/source/fatfs -c $< -o $@
$(O)/gme/emu2413.o: $(L)/gme/ext/emu2413.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -I$(L)/gme -c $< -o $@
$(O)/xmp/%.o: $(L)/xmp/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -fwrapv -DLIBXMP_STATIC -DLIBXMP_NO_PROWIZARD -DLIBXMP_NO_DEPACKERS -I$(L)/xmp/include -I$(L)/xmp/src -c $< -o $@
$(W)/gen/src/webp/config.h:
	@mkdir -p $(dir $@) && echo "/* (vide) */" > $@
$(O)/webp/%.o: $(L)/webp/%.c | $(W)/gen/src/webp/config.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -fwrapv -DNDEBUG -DHAVE_CONFIG_H -I$(W)/gen -I$(L)/webp -c $< -o $@
$(O)/codecs.o: $(R)/source/codecs.c
	@mkdir -p $(O)
	$(CC) -O2 -w -I$(L)/source -I$(R)/source -c $< -o $@
$(O)/pcsim_new.o: plateforme/pcsim_new.cpp
	@mkdir -p $(O)
	$(CXX) -O2 -c $< -o $@
$(O)/main.o: $(wildcard $(R)/source/*.c $(R)/source/*.h) $(wildcard plateforme/include/*.h win/*.h) $(GEN)
	@mkdir -p $(O)
	$(CC) $(GCF) -w -c $(R)/source/main.c -o $@
$(O)/cb/%.o: $(R)/source/cb/%.c
	@mkdir -p $(O)/cb
	$(CC) $(GCF) -std=gnu11 -w -I$(R)/source/cb -c $< -o $@
$(O)/%.o: plateforme/%.c plateforme/pcsim.h $(wildcard plateforme/include/*.h win/*.h win/include/*.h win/include/*/*.h)
	@mkdir -p $(O)
	$(CC) $(GCF) -Wall -Wno-unused-parameter -Wno-unused-function -c $< -o $@

$(EXE): $(SIMOBJ) $(LIBOBJ) $(CB_OBJ)
	$(CXX) -static -mwindows -o $@ $^ -lws2_32 -lwinmm -lwinhttp -lbcrypt -lshell32 -ladvapi32 -lpthread && echo "-> pc/$(EXE)"
.PHONY: all
