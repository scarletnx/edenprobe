# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The masquerade layer, for the host that runs the emulator. `make` builds it beside its
# manifest; then, for an emulator run:
#
#   VK_ADD_LAYER_PATH=$(pwd) VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade \
#   EDENPROBE_MASQUERADE_API=1.1 EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1 eden-cli -g ...
#
# `make check` asks the first device what the layer lets through, with both masks and with none.
# It submits nothing to the device.

CC     ?= cc
CFLAGS := -std=c11 -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror
OUT    := libVkLayer_edenprobe_masquerade.so
LAYER  := VK_ADD_LAYER_PATH=$(CURDIR) VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade

$(OUT): masquerade.c
	$(CC) $(CFLAGS) -shared -o $@ $<

check_masquerade: check_masquerade.c
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ $< -lvulkan

check: $(OUT) check_masquerade
	$(LAYER) EDENPROBE_MASQUERADE_API=1.1 EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1 \
	    ./check_masquerade | tee /dev/stderr | grep -q 'api 1\.1 hostQueryReset 0 VK_EXT_host_query_reset 0'
	$(LAYER) ./check_masquerade | tee /dev/stderr | grep -qv 'api 1\.1 hostQueryReset 0'
	@echo "check: both masks hide what they name, and none changes nothing"

clean:
	rm -f $(OUT) check_masquerade

.PHONY: check clean
