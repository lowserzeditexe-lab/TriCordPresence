ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set in your environment")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

# --- Config -----------------------------------------------------------
TARGET       := tricord_presenced
BUILD        := build
SOURCES      := source
INCLUDES     := include
# UniqueId 20 bits -> Title ID 000401300F000102 (voir tricord_presenced.rsf).
# Choisi hors de toute plage utilisée par la NAND (sysmodules Nintendo : 0x00xx).
UNIQUE_ID    := 0xF0001
TITLE_ID     := 000401300F000102
RSF          := $(TOPDIR)/$(TARGET).rsf

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations \
           -fomit-frame-pointer -ffunction-sections \
           $(ARCH) -D__3DS__
CFLAGS  += $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := -g $(ARCH)
# Schéma Luma3DS sysmodules/rosalina : ELF puis makerom -f ncch.
# On utilise un specs LOCAL (tricord.specs) qui remplace le script de lien
# 3dsx.ld par tricord_sysmodule.ld (copie de 3dsx.ld avec .init_array/
# .fini_array VIDÉS). Raison (crash_dump_00000010) : le loader NCCH de Luma
# zéro-remplissait la fin du segment RW où tombaient ces tableaux, si bien que
# __libc_init_array() appelait un pointeur nul (blx r3=0, pc=0) avant main().
# frame_dummy (seule entrée d'.init_array) sert l'unwinding C++, inutile ici.
# -Wl,-L,$(TOPDIR) : pour que ld trouve tricord_sysmodule.ld.
LDFLAGS := -specs=$(TOPDIR)/tricord.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map),--gc-sections -Wl,-T,$(TOPDIR)/tricord_sysmodule.ld

# wslay (WebSocket) + mbedtls (TLS) + jansson (JSON) viennent des portlibs
# devkitPro (paquets 3ds-wslay, 3ds-mbedtls, 3ds-jansson).
LIBS    := -lwslay -lmbedtls -lmbedx509 -lmbedcrypto -ljansson -lctru -lm
LIBDIRS := $(PORTLIBS) $(CTRULIB)

# --- Standard devkitARM boilerplate ------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

CFILES    := $(notdir $(wildcard $(TOPDIR)/$(SOURCES)/*.c))
CPPFILES  := $(notdir $(wildcard $(TOPDIR)/$(SOURCES)/*.cpp))
SFILES    := $(notdir $(wildcard $(TOPDIR)/$(SOURCES)/*.s))

export LD       := $(CC)
export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export VPATH    := $(TOPDIR)/$(SOURCES)
export DEPSDIR  := $(CURDIR)/$(BUILD)
export INCLUDE  := -I$(CURDIR)/$(INCLUDES) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@echo "  -> $(TITLE_ID).cxi (à copier dans sdmc:/luma/sysmodules/)"

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).map $(TITLE_ID).cxi

else
DEPENDS := $(OFILES:.o=.d)

# Sysmodule = ELF puis converti en CXI via makerom, PAS un .3dsx classique.
# NOTE (crash_dump_00000010) : on N'utilise PAS -nocodepadding. Sans padding, le
# code-set "data" se termine en page partielle (0x1df8) et le loader NCCH de
# Luma3DS zéro-remplit la fin de cette page — où tombent .init_array/.fini_array.
# __libc_init_array() appelait alors un pointeur nul (blx r3=0, pc=0) AVANT main.
# En paginant les code-sets, .init_array (frame_dummy) est chargé correctement.
all: $(OUTPUT).elf $(TOPDIR)/$(TITLE_ID).cxi

$(TOPDIR)/$(TITLE_ID).cxi: $(OUTPUT).elf $(RSF)
	@echo makerom $(notdir $@)
	@makerom -f ncch -rsf $(RSF) -DUNIQUE_ID=$(UNIQUE_ID) -o $@ -elf $<

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
