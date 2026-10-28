# GC Station sur PC (simulateur) : le vrai code de GC Station, compile pour Linux,
# avec pcsim.c / pcsim_gx.c a la place de libogc. Voir README.md.
# Lancer depuis la racine du depot : make -C tools/pc-sim   (apres sh tools/pc-sim/fetch.sh)
R      := ../..
W      := work
L      := $(W)/libs
O      := $(W)/obj
CC     ?= gcc
CXX    ?= g++
INC    := -Iinclude -I$(W)/gen -I$(L)/source -I$(R)/source -I$(L)/mbedtls/include -I$(L)/helix -I$(R)/source/helixstub -I$(L)/gme -I$(L)/xmp/include -I$(L)/webp
TLS    := -DMBEDTLS_USER_CONFIG_FILE='"gc_tls_config.h"'
CFLAGS := -fno-pie -O1 -g -fno-omit-frame-pointer -fwrapv -fno-strict-aliasing -D__POWERPC__ -DGCMP3_BUILD=0 -DLIBXMP_STATIC $(TLS) $(INC) $(SAN)
# (-D__POWERPC__ : comme sur la console ; mbedTLS / helix / gme utilisent leur code C portable)
MBED_SRC := $(wildcard $(L)/mbedtls/library/*.c)
HELIX_SRC:= $(wildcard $(L)/helix/*.c)
GME_SRC  := $(wildcard $(L)/gme/*.cpp)
XMP_SRC  := $(wildcard $(L)/xmp/src/*.c) $(wildcard $(L)/xmp/src/loaders/*.c)
WEBP_SRC := $(wildcard $(L)/webp/src/dec/*.c) $(wildcard $(L)/webp/src/dsp/*.c) $(wildcard $(L)/webp/src/utils/*.c)
LIBOBJ   := $(patsubst $(L)/%.c,$(O)/%.o,$(MBED_SRC) $(HELIX_SRC) $(XMP_SRC) $(WEBP_SRC)) $(patsubst $(L)/%.cpp,$(O)/%.o,$(GME_SRC)) $(O)/gme/emu2413.o $(O)/codecs.o
# v209 : Cube Browser (source/cb, MIT, Arial-Bold) : le navigateur de l'onglet WEB
CB_SRC   := $(wildcard $(R)/source/cb/*.c)
CB_OBJ   := $(patsubst $(R)/source/cb/%.c,$(O)/cb/%.o,$(CB_SRC))
SIMOBJ   := $(O)/pcsim.o $(O)/pcsim_gx.o $(O)/pcsim_limits.o $(O)/pcsim_new.o $(O)/main.o
# limites de la GameCube (pcsim_limits.c) : memoire du tas, carte SD
WRAP     := -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc,--wrap=memalign,--wrap=posix_memalign,--wrap=aligned_alloc,--wrap=mallinfo,--wrap=strdup,--wrap=strndup,--wrap=fopen,--wrap=fileno,--wrap=stat,--wrap=opendir,--wrap=readdir,--wrap=closedir

BIN    ?= gcstation                                   # (v158 : asan.sh -> gcstation-asan, objets a part)
all: $(BIN)

$(W)/gen/natret_bin.h: $(R)/source/natres/natret.c $(R)/tools/natret.ld
	@mkdir -p $(W)/gen
	clang --target=powerpc-none-eabi -mcpu=750 -Os -ffreestanding -fno-builtin -nostdlib -fno-pic -msoft-float -c $< -o $(W)/gen/natret.o
	ld.lld -T $(R)/tools/natret.ld $(W)/gen/natret.o -o $(W)/gen/natret.elf
	llvm-objcopy -O binary $(W)/gen/natret.elf $(W)/gen/natret.bin
	python3 $(R)/tools/bin2h.py $(W)/gen/natret.bin $@ natret_bin

$(W)/gen/natres_bin.h: $(R)/source/natres/natres.c $(R)/source/natres/natres.h $(R)/tools/natres.ld
	@mkdir -p $(W)/gen
	clang --target=powerpc-none-eabi -mcpu=750 -Os -ffreestanding -fno-builtin -nostdlib -fno-pic -msoft-float -c $< -o $(W)/gen/natres.o
	ld.lld -T $(R)/tools/natres.ld $(W)/gen/natres.o -o $(W)/gen/natres.elf
	llvm-objcopy -O binary $(W)/gen/natres.elf $(W)/gen/natres.bin
	python3 $(R)/tools/bin2h.py $(W)/gen/natres.bin $@ natres_bin

$(O)/mbedtls/%.o: $(L)/mbedtls/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w $(INC) -I$(L)/mbedtls/library $(TLS) -c $< -o $@
$(O)/helix/%.o: $(L)/helix/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -I$(R)/source/helixstub -I$(L)/helix -DUSE_DEFAULT_STDLIB -c $< -o $@
$(O)/gme/%.o: $(L)/gme/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -O2 -w -DBLARGG_LITTLE_ENDIAN=1 -DVGM_YM2612_GENS -DUSE_GME_AY -DUSE_GME_GBS -DUSE_GME_GYM -DUSE_GME_HES -DUSE_GME_KSS -DUSE_GME_NSF -DUSE_GME_NSFE -DUSE_GME_SAP -DUSE_GME_SPC -DUSE_GME_VGM -DNDEBUG -I$(L)/gme -fwrapv -c $< -o $@
$(O)/gme/emu2413.o: $(L)/gme/ext/emu2413.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -I$(L)/gme -c $< -o $@
$(O)/xmp/%.o: $(L)/xmp/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -fwrapv -DLIBXMP_STATIC -DLIBXMP_NO_PROWIZARD -DLIBXMP_NO_DEPACKERS -I$(L)/xmp/include -I$(L)/xmp/src -c $< -o $@
$(W)/gen/src/webp/config.h:
	@mkdir -p $(dir $@) && echo "/* (vide : WebP sans SSE2, comme sur la console) */" > $@
$(O)/webp/%.o: $(L)/webp/%.c | $(W)/gen/src/webp/config.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -fwrapv -DNDEBUG -DHAVE_CONFIG_H -I$(W)/gen -I$(L)/webp -c $< -o $@
$(O)/codecs.o: $(R)/source/codecs.c
	@mkdir -p $(O)
	$(CC) -O2 -w -I$(L)/source -I$(R)/source -c $< -o $@
$(O)/pcsim.o: pcsim.c pcsim.h $(wildcard include/*.h include/*/*.h)
	@mkdir -p $(O)
	$(CC) -fno-pie -O2 -g -Wall -Wno-unused-parameter $(INC) $(SAN) -c $< -o $@
$(O)/pcsim_gx.o: pcsim_gx.c pcsim.h $(wildcard include/*.h)
	@mkdir -p $(O)
	$(CC) -fno-pie -O2 -g -Wall $(INC) -c $< -o $@
$(O)/pcsim_limits.o: pcsim_limits.c pcsim.h
	@mkdir -p $(O)
	$(CC) -fno-pie -O2 -g -Wall $(INC) -c $< -o $@
$(O)/pcsim_new.o: pcsim_new.cpp
	@mkdir -p $(O)
	$(CXX) -fno-pie -O2 -g -Wall -c $< -o $@
$(O)/main.o: $(wildcard $(R)/source/*.c $(R)/source/*.h $(R)/source/natres/*.h) $(W)/gen/natres_bin.h $(W)/gen/natret_bin.h $(wildcard include/*.h)
	@mkdir -p $(O)
	$(CC) $(CFLAGS) -w -c $(R)/source/main.c -o $@

$(O)/cb/%.o: $(R)/source/cb/%.c $(wildcard $(R)/source/cb/*.h)
	@mkdir -p $(O)/cb
	$(CC) $(CFLAGS) -std=gnu11 -Wall -Wno-unused-function -I$(R)/source/cb -c $< -o $@

$(BIN): $(SIMOBJ) $(LIBOBJ) $(CB_OBJ)
	@$(CXX) -no-pie $(SAN) $(WRAP) $^ -o $@ -lpthread -lm && echo "-> tools/pc-sim/$(BIN)"

clean:
	rm -rf $(O) gcstation
.PHONY: all clean
