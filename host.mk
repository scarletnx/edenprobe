# SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
# SPDX-License-Identifier: MIT
#
# Builds the parts that do not touch the console for the machine you are sitting at, so what
# decides pass and fail can be exercised with no emulator and no device.

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
