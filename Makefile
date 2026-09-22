# SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Deliberately not the toolchain's template makefile: this builds one flat binary from a handful
# of files, and every flag here is one this suite needs.

ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO first, e.g. export DEVKITPRO=$$HOME/devkitpro)
endif

TARGET   := gfxprobe
BUILD    := build
SOURCES  := source source/tests

PREFIX   := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
CC       := $(PREFIX)gcc
LIBNX    := $(DEVKITPRO)/libnx
TOOLS    := $(DEVKITPRO)/tools/bin

# The processor in the console, and the position independence its loader expects.
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# Warnings are errors because this program's whole job is to be trusted about small numerical
# differences. -MMD -MP is what makes an edited header rebuild the objects that read it, so a run
# can never be reporting a stale binary's answer.
CFLAGS   := $(ARCH) -g -O2 -ffunction-sections -fdata-sections -MMD -MP \
            -Wall -Wextra -Werror -Wno-unused-parameter \
            -D__SWITCH__ -I$(LIBNX)/include -Isource
# --wrap on the abort is what lets GpuOpen report a failed bring-up instead of dying: the
# graphics library answers a failed constructor by calling the runtime's abort, which never comes
# back. gpu.c arms the wrapper only while the device is being brought up.
LDFLAGS  := $(ARCH) -specs=$(LIBNX)/switch.specs -Wl,--gc-sections -g \
            -Wl,--wrap=diagAbortWithResult
# Order matters: the graphics library leans on the C runtime, not the other way round.
LIBS     := -L$(LIBNX)/lib -ldeko3d -lnx

# host_main.c is the other build's entry point, and both define main, so sweeping the directory
# would pick up two of them and fail the link.
HOSTONLY := source/host_main.c
CFILES   := $(filter-out $(HOSTONLY),$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c)))
OFILES   := $(patsubst %.c,$(BUILD)/%.o,$(CFILES))
DFILES   := $(OFILES:.o=.d)

.PHONY: all clean run

all: $(TARGET).nro

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).nro: $(TARGET).elf
	$(TOOLS)/elf2nro $< $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro

# Last, and with a leading dash: on the first build there are no .d files yet and that is not an
# error. They live under $(BUILD), so clean already takes them with it.
-include $(DFILES)

# Runs the binary under the emulator and shows what it reported. EMULATOR points at the build
# being tested, which is the whole point: the same .nro goes to the console unchanged.
EMULATOR ?= $(HOME)/.claude-worktrees/reden-1870300401/66650747/build-qt/bin/eden-cli
run: $(TARGET).nro
	DISPLAY=:0 timeout 60 $(EMULATOR) -g $(CURDIR)/$(TARGET).nro 2>&1 \
	  | sed 's/\x1b\[[0-9;]*m//g' | grep -E "gfxprobe|GFXPROBE|\"type\":" || true
