# SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
# SPDX-License-Identifier: MIT
#
# Builds the parts that do not touch the console for the machine you are sitting at, so what
# decides pass and fail can be exercised with no emulator and no device.

HOSTCC   ?= cc
HOSTOUT  := build-host/gfxprobe-host
HOSTSRC  := source/report.c source/check.c source/picture.c source/ui.c \
            source/raster.c source/intref.c source/blendref.c source/texref.c \
            source/fmathref.c \
            source/tests/t_selfcheck.c source/tests/t_support.c source/tests/t_reference.c \
            source/host_main.c

# One object per source, so the -MMD below has somewhere to put its dependency files. Without
# them an edited header rebuilds nothing and the run reports the previous binary's answer.
HOSTFLAGS := -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g -MMD -MP -Isource
HOSTOBJ  := $(patsubst source/%.c,build-host/%.o,$(HOSTSRC))
HOSTDEP  := $(HOSTOBJ:.o=.d)

.PHONY: host host-run host-clean

host: $(HOSTOUT)

build-host/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTFLAGS) -c $< -o $@

$(HOSTOUT): $(HOSTOBJ)
	$(HOSTCC) $(HOSTFLAGS) $(HOSTOBJ) -lm -o $@

host-run: $(HOSTOUT)
	./$(HOSTOUT)

host-clean:
	rm -rf build-host

# Last, and with a leading dash: on the first build there are no .d files yet and that is not an
# error. They live under build-host, so host-clean already takes them with it.
-include $(HOSTDEP)
