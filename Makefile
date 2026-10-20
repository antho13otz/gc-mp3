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

CFLAGS   := -O2 -Wall $(MACHDEP) -D__POWERPC__ -I$(LIBOGC_INC) -Isource -I$(MBEDTLS)/include -I$(HELIX) -Isource/helixstub $(TLSDEF)
MBEDFLAGS:= -O2 $(MACHDEP) -I$(LIBOGC_INC) -Isource -I$(MBEDTLS)/include -I$(MBEDTLS)/library $(TLSDEF)
HELIXFLAGS:= -O2 $(MACHDEP) -D__POWERPC__ -Isource/helixstub -I$(HELIX) -DUSE_DEFAULT_STDLIB -Wno-unknown-pragmas
LDFLAGS  := $(MACHDEP) -Wl,-Map,$(TARGET).map
# libbba = reseau (lwIP + Broadband Adapter) de libogc pour GameCube
LIBS     := -L$(LIBOGC_LIB) -lfat -lbba -logc -lm

all: $(TARGET).dol

build/mbedtls/%.o: $(MBEDTLS)/library/%.c source/gc_tls_config.h
> @mkdir -p build/mbedtls
> $(CC) $(MBEDFLAGS) -c $< -o $@

build/codecs.o: source/codecs.c source/dr_flac.h source/stb_vorbis.c
> @mkdir -p build
> $(CC) -O2 $(MACHDEP) -Isource -w -c $< -o $@

build/helix/%.o: $(HELIX)/%.c
> @mkdir -p build/helix
> $(CC) $(HELIXFLAGS) -c $< -o $@

$(TARGET).elf: source/main.c source/font_atlas.h source/minimp3.h source/stb_image.h source/cacert.h source/gc_tls_config.h $(MBED_OBJ) $(HELIX_OBJ) $(CODEC_OBJ)
> $(CC) $(CFLAGS) -DUSE_DEFAULT_STDLIB source/main.c $(MBED_OBJ) $(HELIX_OBJ) $(CODEC_OBJ) $(LDFLAGS) $(LIBS) -o $@

clean:
> rm -rf build $(TARGET).elf $(TARGET).dol $(TARGET).map
