ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC n'est pas defini : installe devkitPro / devkitPPC")
endif

include $(DEVKITPPC)/gamecube_rules

# Les lignes de commande commencent par ">" au lieu d'une tabulation
# (evite les problemes de copier-coller depuis un telephone).
.RECIPEPREFIX := >

TARGET   := gc-mp3player

# mbedTLS (HTTPS de l'onglet RADIO) : sources telechargees par le workflow dans ./mbedtls
MBEDTLS  := mbedtls
TLSDEF   := -DMBEDTLS_USER_CONFIG_FILE='"gc_tls_config.h"'
MBED_SRC := $(wildcard $(MBEDTLS)/library/*.c)
MBED_OBJ := $(patsubst $(MBEDTLS)/library/%.c,build/mbedtls/%.o,$(MBED_SRC))

CFLAGS   := -O2 -Wall $(MACHDEP) -I$(LIBOGC_INC) -Isource -I$(MBEDTLS)/include $(TLSDEF)
MBEDFLAGS:= -O2 $(MACHDEP) -I$(LIBOGC_INC) -Isource -I$(MBEDTLS)/include -I$(MBEDTLS)/library $(TLSDEF)
LDFLAGS  := $(MACHDEP) -Wl,-Map,$(TARGET).map
# libbba = reseau (lwIP + Broadband Adapter) de libogc pour GameCube
LIBS     := -L$(LIBOGC_LIB) -lfat -lbba -logc -lm

all: $(TARGET).dol

build/mbedtls/%.o: $(MBEDTLS)/library/%.c source/gc_tls_config.h
> @mkdir -p build/mbedtls
> $(CC) $(MBEDFLAGS) -c $< -o $@

$(TARGET).elf: source/main.c source/font_atlas.h source/minimp3.h source/stb_image.h source/cacert.h source/gc_tls_config.h $(MBED_OBJ)
> $(CC) $(CFLAGS) source/main.c $(MBED_OBJ) $(LDFLAGS) $(LIBS) -o $@

clean:
> rm -rf build $(TARGET).elf $(TARGET).dol $(TARGET).map
