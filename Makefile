# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Deliberately not the toolchain's template makefile: this builds one flat binary from a handful
# of files, and every flag here is one this suite needs.

ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO first, e.g. export DEVKITPRO=$$HOME/devkitpro)
endif

TARGET   := edenprobe
BUILD    := build
SOURCES  := source source/tests

PREFIX   := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
CC       := $(PREFIX)gcc
LIBNX    := $(DEVKITPRO)/libnx
TOOLS    := $(DEVKITPRO)/tools/bin

# What a loader reads out of the .nro's asset section before it will start the program. Without
# one, at least one emulator refuses the file rather than running it, so the section is not
# optional even though nothing in this suite looks at what is in it.
APP_TITLE   := edenprobe
APP_AUTHOR  := reden
APP_VERSION := 0.1
APP_ICON    := $(LIBNX)/default_icon.jpg

# The processor in the console, and the position independence its loader expects.
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# Warnings are errors because this program's whole job is to be trusted about small numerical
# differences. -MMD -MP is what makes an edited header rebuild the objects that read it, so a run
# can never be reporting a stale binary's answer.
CFLAGS   := $(ARCH) -g -O2 -ffunction-sections -fdata-sections -MMD -MP \
            -Wall -Wextra -Werror -Wno-unused-parameter \
            -D__SWITCH__ -I$(LIBNX)/include -Isource -I$(BUILD)/shaders
# --wrap on the abort is what lets GpuOpen report a failed bring-up instead of dying: the
# graphics library answers a failed constructor by calling the runtime's abort, which never comes
# back. gpu.c arms the wrapper only while the device is being brought up.
LDFLAGS  := $(ARCH) -specs=$(LIBNX)/switch.specs -Wl,--gc-sections -g \
            -Wl,--wrap=diagAbortWithResult
# Order matters: the graphics library leans on the C runtime and on the maths library after it.
# The maths one is not decoration, since building a sampler descriptor calls powf.
LIBS     := -L$(LIBNX)/lib -ldeko3d -lnx -lm

# The shaders, named here and built by the rules further down.
SHADER_DIR  := source/shaders
SHADER_SRC  := $(sort $(wildcard $(SHADER_DIR)/*.glsl))
SHADER_DKSH := $(patsubst $(SHADER_DIR)/%.glsl,$(BUILD)/shaders/%.dksh,$(SHADER_SRC))
SHADER_ASM  := $(BUILD)/shaders/blobs.s
SHADER_HDR  := $(BUILD)/shaders/edenprobe_shaders.h
SHADER_OBJ  := $(BUILD)/shaders/blobs.o

# host_main.c is the other build's entry point, and both define main, so sweeping the directory
# would pick up two of them and fail the link.
HOSTONLY := source/host_main.c
CFILES   := $(filter-out $(HOSTONLY),$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c)))
OFILES   := $(patsubst %.c,$(BUILD)/%.o,$(CFILES)) $(SHADER_OBJ)
# Only the C objects get a .d: the shader blobs are assembled, not compiled, and an assembly
# file made of byte literals reads no headers to depend on.
DFILES   := $(patsubst %.c,$(BUILD)/%.d,$(CFILES))

.PHONY: all clean run

all: $(TARGET).nro

# uam compiles the console's dialect of GLSL and bin2s turns each module into read-only data
# inside the binary, so the file that runs on hardware and the file that runs under an emulator
# carry the same shaders. The stage comes from the name, because uam will not guess it.
$(BUILD)/shaders/%.vert.dksh: $(SHADER_DIR)/%.vert.glsl
	@mkdir -p $(dir $@)
	$(TOOLS)/uam -s vert -o $@ $<

$(BUILD)/shaders/%.frag.dksh: $(SHADER_DIR)/%.frag.glsl
	@mkdir -p $(dir $@)
	$(TOOLS)/uam -s frag -o $@ $<

# One grouped target, because one run of bin2s produces both files: with a rule each, deleting
# only the header leaves make with no recipe that makes it alone. Grouped targets need GNU make
# 4.3 or newer. The alignment is what the graphics library's paired load of a program header
# wants.
$(SHADER_ASM) $(SHADER_HDR) &: $(SHADER_DKSH)
	@mkdir -p $(dir $(SHADER_ASM))
	$(TOOLS)/bin2s -a 16 -H $(SHADER_HDR) $^ > $(SHADER_ASM)
	@echo >> $(SHADER_ASM)	# bin2s ends on a comment with no newline, and the assembler says so

$(SHADER_OBJ): $(SHADER_ASM)
	$(CC) $(ARCH) -c $< -o $@

# The one file that names the modules. It is the only one that includes the generated header, so
# a shader added to the directory and not added there is a build failure rather than dead weight.
$(BUILD)/source/shaders.o: $(SHADER_HDR)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

# Rebuilt when this file changes, so a title edited here cannot leave a stale one in the binary.
$(TARGET).nacp: Makefile
	$(TOOLS)/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	$(TOOLS)/elf2nro $< $@ --nacp=$(TARGET).nacp --icon=$(APP_ICON)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp

# Last, and with a leading dash: on the first build there are no .d files yet and that is not an
# error. They live under $(BUILD), so clean already takes them with it.
-include $(DFILES)

# Runs the binary under the emulator and shows what it reported. EMULATOR points at the build
# being tested, which is the whole point: the same .nro goes to the console unchanged.
EMULATOR ?= $(HOME)/WorkDir/accord/bin/eden-cli-e8d142f2b6

# The emulator keeps its settings, its memory card and its caches under the XDG directories, and
# a frontend saves its settings on the way out. Pointed at a tree inside the build, a run starts
# from the emulator's own defaults every time, the report lands somewhere this makefile knows,
# and nothing of the person's own emulator setup is read or rewritten.
XDG     := $(CURDIR)/$(BUILD)/xdg
SDMC    := $(XDG)/data/eden/sdmc/$(TARGET)

# Everything the previous run left is removed first, so what is there afterwards is this run's:
# a report, and only the pictures that report names. The `|| true` is for the filter alone, which
# finds nothing to print when the emulator prints nothing; whether the run happened is decided by
# the checks either side of it.
run: $(TARGET).nro
	@[ -x "$(EMULATOR)" ] || { echo "make run: no executable emulator at $(EMULATOR)" >&2; exit 1; }
	@mkdir -p $(XDG)/data $(XDG)/config $(XDG)/cache $(SDMC)
	@rm -rf $(SDMC)/report.jsonl $(SDMC)/images $(SDMC)/support-check
	XDG_DATA_HOME=$(XDG)/data XDG_CONFIG_HOME=$(XDG)/config XDG_CACHE_HOME=$(XDG)/cache \
	DISPLAY=:0 timeout 60 $(EMULATOR) -g $(CURDIR)/$(TARGET).nro 2>&1 \
	  | sed 's/\x1b\[[0-9;]*m//g' | { grep -aE "$(TARGET)|EDENPROBE|\"type\":" || true; }
	@[ -f $(SDMC)/report.jsonl ] || \
	  { echo "make run: the emulator left no report at $(SDMC)/report.jsonl" >&2; exit 1; }
	@echo "report: $(SDMC)/report.jsonl"
