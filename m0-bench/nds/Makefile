# Nintendo DS build of the M0 rasteriser calibration.
#
#   docker run --rm -v ${PWD}:/work -w /work/m0-bench/nds \
#       devkitpro/devkitarm:latest make
#
# Then run build/m0bench.nds in melonDS and read the bottom screen.
#
# NOTE: no -DPORT_NDS. The point is to run the rasteriser from main RAM on both
# machines; the DS port's ITCM placement is a DS-only advantage and would make
# the comparison flattering to the parent instead of useful.

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
LIBNDS    ?= $(DEVKITPRO)/libnds
CALICO    ?= $(DEVKITPRO)/calico

CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
NDSTOOL := $(DEVKITPRO)/tools/bin/ndstool

TOP       := ../..
BENCH     := ..
TRANSLATE := $(TOP)/translate

# -marm, not -mthumb: MIPS has no 16-bit encoding, so ARM mode is the closer
# comparison. -O2 on both sides.
ARCH := -march=armv5te -mtune=arm946e-s -marm

CFLAGS := -O2 -g -pipe -MMD -MP -ffunction-sections -fdata-sections \
          -fomit-frame-pointer -fsigned-char -fcommon \
          -Wall -Wno-unused -Wno-parentheses \
          $(ARCH) -DARM9 -D__NDS__ \
          -I$(BENCH) -I$(TRANSLATE) \
          -I$(LIBNDS)/include -I$(CALICO)/include

LDFLAGS := -specs=$(CALICO)/share/ds9.specs -g $(ARCH) -Wl,--gc-sections \
           -Wl,-Map,build/m0bench.map
LIBS    := -L$(LIBNDS)/lib -L$(CALICO)/lib -lnds9 -lcalico_ds9 -lm

OBJDIR := build

SRC := main_nds.c $(BENCH)/bench_core.c $(BENCH)/bench_globals.c \
       $(TRANSLATE)/s_poly.c $(TRANSLATE)/s_fillv.c

OBJ := $(addprefix $(OBJDIR)/,$(notdir $(SRC:.c=.o)))

# vpath, not VPATH: a bare VPATH would also resolve the `build` directory
# target against ../ , where the PSX build already made one.
vpath %.c . $(BENCH) $(TRANSLATE)

all: $(OBJDIR)/m0bench.nds

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/m0bench.elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(OBJDIR)/m0bench.nds: $(OBJDIR)/m0bench.elf
	$(NDSTOOL) -c $@ -9 $< -7 $(CALICO)/bin/ds7_maine.elf \
		-b $(CALICO)/share/nds-icon.bmp "M0 BENCH;rasteriser calibration;psx-lba"

clean:
	rm -rf $(OBJDIR)

-include $(OBJ:.o=.d)

.PHONY: all clean
