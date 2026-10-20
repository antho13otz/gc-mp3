ifeq ($(strip $(DEVKITPPC)),)
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
# Musique de jeux retro : game-music-emu (C++) et libxmp-lite (modules),
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
XMPFLAGS := -O2 $(MACHDEP) -fwrapv -DLIBXMP_CORE_PLAYER -DLIBXMP_STATIC -I$(XMP)/include -I$(XMP)/src -w

CFLAGS   := -O2 -Wall $(MACHDEP) -D__POWERPC__ -I$(LIBOGC_INC) -Isource -I$(MBEDTLS)/include -I$(HELIX) -Isource/helixstub -I$(GME) -I$(XMP)/include -DLIBXMP_STATIC $(TLSDEF)
MBEDFLAGS:= -O2 $(MACHDEP) -I$(LIBOGC_INC) -Isource -I$(MBEDTLS)/include -I$(MBEDTLS)/library $(TLSDEF)
HELIXFLAGS:= -O2 $(MACHDEP) -D__POWERPC__ -Isource/helixstub -I$(HELIX) -DUSE_DEFAULT_STDLIB -Wno-unknown-pragmas
LDFLAGS  := $(MACHDEP) -Wl,-Map,$(TARGET).map
# libbba = reseau (lwIP + Broadband Adapter) de libogc pour GameCube
LIBS     := -L$(LIBOGC_LIB) -lfat -lbba -logc -lstdc++ -lm

all: $(TARGET).dol

build/mbedtls/%.o: $(MBEDTLS)/library/%.c source/gc_tls_config.h
> @mkdir -p build/mbedtls
> $(CC) $(MBEDFLAGS) -c $< -o $@

build/codecs.o: source/codecs.c source/dr_flac.h source/stb_vorbis.c source/qrcodegen.c source/qrcodegen.h
> @mkdir -p build
> $(CC) -O2 $(MACHDEP) -Isource -w -c $< -o $@

build/gme/%.o: $(GME)/%.cpp
> @mkdir -p build/gme
> $(CXX) $(GMEFLAGS) -c $< -o $@

build/gme/emu2413.o: $(GME)/ext/emu2413.c
> @mkdir -p build/gme
> $(CC) $(GMEFLAGS) -c $< -o $@

build/xmp/%.o: $(XMP)/src/%.c
> @mkdir -p $(dir $@)
> $(CC) $(XMPFLAGS) -c $< -o $@

build/helix/%.o: $(HELIX)/%.c
> @mkdir -p build/helix
> $(CC) $(HELIXFLAGS) -c $< -o $@

LIBOBJ   := $(MBED_OBJ) $(HELIX_OBJ) $(CODEC_OBJ) $(GME_OBJ) $(XMP_OBJ)
MAINDEPS := $(wildcard source/*.c source/*.h)

$(TARGET).elf: $(MAINDEPS) $(LIBOBJ)
> $(CC) $(CFLAGS) -DUSE_DEFAULT_STDLIB source/main.c $(LIBOBJ) $(LDFLAGS) $(LIBS) -o $@

clean:
> rm -rf build $(TARGET).elf $(TARGET).dol $(TARGET).map
