# SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the parts that do not touch the console for the machine you are sitting at, so the
# thing that decides pass and fail can be exercised with no emulator and no device. What this
# cannot cover is anything that draws; that is the point of the other makefile.

HOSTCC   ?= cc
HOSTOUT  := build-host/gfxprobe-host
HOSTSRC  := source/report.c source/check.c source/tests/t_selfcheck.c source/host_main.c

.PHONY: host host-run host-clean

host: $(HOSTOUT)

$(HOSTOUT): $(HOSTSRC)
	@mkdir -p build-host
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g \
	  -Isource $(HOSTSRC) -lm -o $@

host-run: $(HOSTOUT)
	./$(HOSTOUT)

host-clean:
	rm -rf build-host
