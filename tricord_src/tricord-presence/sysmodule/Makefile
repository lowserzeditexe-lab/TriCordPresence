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
# Même schéma que Luma3DS sysmodules/rosalina/Makefile : ELF "3dsx.specs" puis
# makerom -f ncch. -Wl,--gc-sections pour réduire la taille .code (pas de romfs).
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map),--gc-sections

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
# Commande identique à celle de Luma3DS pour rosalina (-f ncch -nocodepadding).
all: $(OUTPUT).elf $(TOPDIR)/$(TITLE_ID).cxi

$(TOPDIR)/$(TITLE_ID).cxi: $(OUTPUT).elf $(RSF)
	@echo makerom $(notdir $@)
	@makerom -f ncch -rsf $(RSF) -DUNIQUE_ID=$(UNIQUE_ID) -nocodepadding -o $@ -elf $<

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
