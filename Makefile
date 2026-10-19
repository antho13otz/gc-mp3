ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC n'est pas defini : installe devkitPro / devkitPPC")
endif

include $(DEVKITPPC)/gamecube_rules

# Les lignes de commande commencent par ">" au lieu d'une tabulation
# (evite les problemes de copier-coller depuis un telephone).
.RECIPEPREFIX := >

TARGET   := gc-mp3player

CFLAGS   := -O2 -Wall $(MACHDEP) -I$(LIBOGC_INC)
LDFLAGS  := $(MACHDEP) -Wl,-Map,$(TARGET).map
# libbba = reseau (lwIP + Broadband Adapter) de libogc pour GameCube (onglet RADIO)
LIBS     := -L$(LIBOGC_LIB) -lfat -lbba -logc -lm

all: $(TARGET).dol

$(TARGET).elf: source/main.c source/font_atlas.h source/minimp3.h source/stb_image.h
> $(CC) $(CFLAGS) source/main.c $(LDFLAGS) $(LIBS) -o $@

clean:
> rm -f $(TARGET).elf $(TARGET).dol $(TARGET).map
