ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set in your environment")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET   := tricord-presence-installer
BUILD    := build
SOURCES  := source
DATA     := data
INCLUDES := include
# romfs = uniquement les binaires à installer (cxi, 3gx, titles.txt), rempli
# par build.sh. Aucun asset visuel (installeur CLI).
ROMFS    := romfs
ASSETS   := assets
RSF      := $(TOPDIR)/$(TARGET).rsf

# Installeur = .3dsx classique (Homebrew Launcher) + .cia (FBI / QR code),
# MAIS sans UI graphique : pas de banner/icône soignée, juste console texte.
# L'icône 48x48 et la bannière 256x128 sont des aplats unis générés par
# tools/gen_assets.py (obligatoires techniquement pour un .cia HOME menu).
APP_TITLE       := TriCord Presence Installer
APP_DESCRIPTION := CLI installer (no GUI yet)
APP_AUTHOR      := TriCord
APP_ICON        := $(TOPDIR)/$(ASSETS)/icon.png

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -fomit-frame-pointer \
           -ffunction-sections $(ARCH) -D__3DS__
CFLAGS  += $(INCLUDE)
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS    := -lctru -lm
LIBDIRS := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
CFILES   := $(notdir $(wildcard $(TOPDIR)/$(SOURCES)/*.c))
SFILES   := $(notdir $(wildcard $(TOPDIR)/$(SOURCES)/*.s))
export LD       := $(CC)
export OFILES   := $(CFILES:.c=.o) $(SFILES:.s=.o)
export VPATH    := $(TOPDIR)/$(SOURCES)
export DEPSDIR  := $(CURDIR)/$(BUILD)
export INCLUDE  := -I$(CURDIR)/$(INCLUDES) -I$(CTRULIB)/include -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh --romfs=$(CURDIR)/$(ROMFS)

.PHONY: all cia clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# .cia : même ELF, empaqueté par makerom (3DSGuy/Project_CTR) avec le RSF
# minimal, l'icône SMDH et une bannière (bannertool). Commande documentée
# dans RAPPORT.md §Distribution.
cia: all
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $(OUTPUT).cia

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).cia $(TARGET).map

else
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).3dsx

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf: $(OFILES)

$(DEPSDIR)/banner.bnr: $(TOPDIR)/$(ASSETS)/banner.png $(TOPDIR)/$(ASSETS)/silence.wav
	@bannertool makebanner -i $(TOPDIR)/$(ASSETS)/banner.png -a $(TOPDIR)/$(ASSETS)/silence.wav -o $@ >/dev/null

$(DEPSDIR)/icon.icn: $(TOPDIR)/$(ASSETS)/icon.png
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $< -o $@ >/dev/null

$(OUTPUT).cia: $(OUTPUT).elf $(DEPSDIR)/banner.bnr $(DEPSDIR)/icon.icn $(RSF)
	@echo makerom $(notdir $@)
	@makerom -f cia -o $@ -elf $(OUTPUT).elf -rsf $(RSF) -target t -exefslogo \
	         -icon $(DEPSDIR)/icon.icn -banner $(DEPSDIR)/banner.bnr \
	         -DAPP_ROMFS=$(TOPDIR)/$(ROMFS)

-include $(DEPENDS)

endif
