# GC Station PC (Windows) : le simulateur (pcsim.c) devenu un programme Windows, avec fenetre, clavier / manette, son,
# et les jeux dans Dolphin (pcsim_win.c, pcsim_dolphin.c). Compile avec MinGW 32 bits (adresses sur 32 bits, comme
# la console), depuis Linux : apt install gcc-mingw-w64-i686 g++-mingw-w64-i686 (voir .github/workflows/windows.yml)
#   make -C tools/pc-sim gen       (une fois : en-tetes du resident Pulse, avec clang)
#   make -C tools/pc-sim -f win/Makefile BUILD=123
R      := ../..
W      := work
L      := $(W)/libs
O      := $(W)/win
CC     := i686-w64-mingw32-gcc
CXX    := i686-w64-mingw32-g++
BUILD  ?= 0
INC    := -Iwin/include -Iinclude -I$(W)/gen -I$(L)/source -I$(R)/source -I$(L)/mbedtls/include -I$(L)/helix -I$(R)/source/helixstub -I$(L)/gme -I$(L)/xmp/include -I$(L)/webp
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
$(O)/pcsim_new.o: pcsim_new.cpp
	@mkdir -p $(O)
	$(CXX) -O2 -c $< -o $@
$(O)/main.o: $(wildcard $(R)/source/*.c $(R)/source/*.h) $(wildcard include/*.h win/*.h) win/pc_main.c
	@mkdir -p $(O)
	$(CC) $(GCF) -w -c win/pc_main.c -o $@
$(O)/cb/%.o: $(R)/source/cb/%.c
	@mkdir -p $(O)/cb
	$(CC) $(GCF) -std=gnu11 -w -I$(R)/source/cb -c $< -o $@
$(O)/%.o: %.c pcsim.h $(wildcard include/*.h win/*.h win/include/*.h win/include/*/*.h)
	@mkdir -p $(O)
	$(CC) $(GCF) -Wall -Wno-unused-parameter -Wno-unused-function -c $< -o $@

$(EXE): $(SIMOBJ) $(LIBOBJ) $(CB_OBJ)
	$(CXX) -static -mwindows -o $@ $^ -lws2_32 -lwinmm -lwinhttp -lbcrypt -lpthread && echo "-> tools/pc-sim/$(EXE)"
.PHONY: all
