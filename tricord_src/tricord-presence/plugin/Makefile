ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set in your environment")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

# Chaîne de build 3gx : reprise du Makefile de référence de CTRPluginFramework
# (TestPlugin/Makefile, gitlab.com/thepixellizeross/ctrpluginframework) :
#   1. compiler un ELF lié avec 3gx.ld (code à 0x07000100, entrée _start
#      fournie par libctrpf), SANS 3dsx.specs ;
#   2. `3gxtool -s <elf> <plgInfo> <3gx>` (Nanquitas/3gxtool) empaquette
#      l'ELF + les métadonnées .plgInfo dans le format .3gx du plugin loader.
# Prérequis : libctrpf installée dans $(DEVKITPRO)/libctrpf
# (cd third_party/ctrpf/Library && make install), 3gxtool dans le PATH.

CTRPFLIB ?= $(DEVKITPRO)/libctrpf

TARGET   := tricord_overlay
BUILD    := build
SOURCES  := source
INCLUDES := source
PSF      := $(TOPDIR)/$(TARGET).plgInfo

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -mword-relocations -ffunction-sections -fdata-sections -fno-strict-aliasing \
           -O2 -fomit-frame-pointer -Wall $(ARCH) -D__3DS__ -DNDEBUG=1
CFLAGS  += $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := $(ARCH)
LDFLAGS := -T $(TOPDIR)/3gx.ld $(ARCH) -Os -Wl,--strip-discarded,--strip-debug,--gc-sections,--section-start,.text=0x07000100

LIBS    := -lctrpf -lctru -lm
LIBDIRS := $(CTRPFLIB) $(CTRULIB) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(TOPDIR)/$(SOURCES)
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(notdir $(wildcard $(TOPDIR)/$(SOURCES)/*.c))
CPPFILES := $(notdir $(wildcard $(TOPDIR)/$(SOURCES)/*.cpp))
SFILES   := $(notdir $(wildcard $(TOPDIR)/$(SOURCES)/*.s))

export LD       := $(CXX)
export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3gx

else
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).3gx

$(OUTPUT).3gx: $(OUTPUT).elf $(PSF)
	@echo creating $(notdir $@)
	@3gxtool -s $< $(PSF) $@

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
