# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: MIT
#
# Builds the parts that do not touch the console for the machine you are sitting at, so what
# decides pass and fail can be exercised with no emulator and no device.

HOSTCC   ?= cc
HOSTOUT  := build-host/edenprobe-host
HOSTSRC  := source/report.c source/journal.c source/check.c source/mode.c source/picture.c \
            source/ui.c \
            source/raster.c source/intref.c source/blendref.c source/texref.c \
            source/fmathref.c source/coherref.c source/formatref.c source/netref.c source/cpuref.c source/perf.c \
            source/rawref.c source/astcref.c source/astcblocks.c source/eacref.c \
            source/tests/t_selfcheck.c source/tests/t_support.c source/tests/t_screens.c \
            source/tests/t_perf.c source/tests/t_reference.c source/tests/t_netref.c \
            source/tests/t_cpuref.c source/tests/t_rawref.c source/tests/t_astcref.c \
            source/tests/t_eacref.c source/tests/t_journal.c \
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

# Twice, the second from somewhere else on purpose: a path built relative to the working
# directory works from here and nowhere else, and the suite has to be able to notice that. Then
# the report tools, which read what the binary wrote and have tests of their own.
host-run: $(HOSTOUT)
	./$(HOSTOUT)
	cd / && exec $(CURDIR)/$(HOSTOUT)
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools

host-clean:
	rm -rf build-host

# Last, and with a leading dash: on the first build there are no .d files yet and that is not an
# error. They live under build-host, so host-clean already takes them with it.
-include $(HOSTDEP)
