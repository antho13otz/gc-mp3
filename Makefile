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
LIBS     := -L$(LIBOGC_LIB) -lfat -logc -lm

all: $(TARGET).dol

$(TARGET).elf: source/main.c source/minimp3.h
> $(CC) $(CFLAGS) source/main.c $(LDFLAGS) $(LIBS) -o $@

clean:
> rm -f $(TARGET).elf $(TARGET).dol $(TARGET).map
