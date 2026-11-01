# Avatar Chat for PS3: PSL1GHT build. Needs the ps3dev toolchain with
# PSL1GHT installed and $PSL1GHT set (https://github.com/ps3dev/ps3toolchain).
#
#   make            -> avatar_chat_ps3.elf / .self   (run with ps3load)
#   make pkg        -> avatar_chat_ps3.pkg           (install from the XMB)
#   make clean
#
# The host (PC) build and the tests are in Makefile.host.

ifeq ($(strip $(PSL1GHT)),)
$(error "PSL1GHT is not set. Install the ps3dev toolchain and export PSL1GHT=$$PS3DEV/psl1ght")
endif

include $(PSL1GHT)/ppu_rules

TARGET      := avatar_chat_ps3
BUILD       := build
SOURCES     := src src/core src/platform/ps3
INCLUDES    := src
DATA        :=

TITLE       := Avatar Chat
APPID       := AVCH00001
CONTENTID   := UP0001-$(APPID)_00-0000000000000000
ICON0       := pkg/ICON0.PNG
SFOXML      := pkg/sfo.xml

CFLAGS      = -O2 -Wall -Wextra -Wno-unused-parameter -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS    = $(CFLAGS)
LDFLAGS     = $(MACHDEP) -Wl,-Map,$(notdir $@).map

# libcurl (with PolarSSL), libjpeg and zlib come from the toolchain's
# portlibs; polarssl's library sits under its include tree there.
PORTLIBS    ?= $(PS3DEV)/portlibs/ppu
LIBDIRS     := $(PORTLIBS)
LIBS        := -lcurl -lpolarssl -ljpeg -lz -lmad -lvorbisfile -lvorbis -logg -lFLAC -laudio -lrsx -lgcm_sys -lio -lsysutil -lnet -lsysmodule -lrt -llv2 -lm
LDFLAGS     += -L$(PORTLIBS)/include/polarssl/lib

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
export BUILDDIR := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD   := $(CC)
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) -I$(CURDIR)/$(BUILD) $(LIBPSL1GHT_INC) $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) $(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean pkg run

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).pkg $(OUTPUT).fake.self $(OUTPUT).elf.map

run: $(BUILD)
	ps3load $(OUTPUT).self

pkg: $(BUILD) $(OUTPUT).pkg

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
