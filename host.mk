# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the parts that do not touch the console for the machine you are sitting at, so what
# decides pass and fail can be exercised with no emulator and no device.

HOSTCC   ?= cc
HOSTOUT  := build-host/edenprobe-host
HOSTSRC  := source/platform_host.c $(shell grep -v '^\#' source/portable.txt) source/host_main.c

# One object per source, so the -MMD below has somewhere to put its dependency files. Without
# them an edited header rebuilds nothing and the run reports the previous binary's answer.
# EDENPROBE_HOST: the few judge cases that hold this build to what host_main.c sets up
HOSTFLAGS := -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g -MMD -MP -Isource -DEDENPROBE_HOST
HOSTOBJ  := $(patsubst source/%.c,build-host/%.o,$(HOSTSRC))
HOSTDEP  := $(HOSTOBJ:.o=.d)

.PHONY: host host-run host-clean

host: $(HOSTOUT)

build-host/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTFLAGS) -c $< -o $@

$(HOSTOUT): $(HOSTOBJ)
	$(HOSTCC) $(HOSTFLAGS) $(HOSTOBJ) -lm -pthread -o $@

# Twice, the second from somewhere else on purpose: a path built relative to the working
# directory works from here and nowhere else, and the suite has to be able to notice that. Then
# the report tools, which read what the binary wrote and have tests of their own, the CI's
# scripts in .github/scripts, against a fake GitHub and a fake Discord, and the APK's runners,
# against a fake adb and a fake ScarletNX driver.
host-run: $(HOSTOUT)
	./$(HOSTOUT)
	cd / && exec $(CURDIR)/$(HOSTOUT)
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s .github/scripts
	bash android/tests/test-run-adb.sh
	bash android/tests/test-run-scarletnx.sh

host-clean:
	rm -rf build-host

# Last, and with a leading dash: on the first build there are no .d files yet and that is not an
# error. They live under build-host, so host-clean already takes them with it.
-include $(HOSTDEP)
