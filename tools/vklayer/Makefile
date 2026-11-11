# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The masquerade layer, for the host that runs the emulator. `make` builds it beside its
# manifest; then, for an emulator run:
#
#   VK_ADD_LAYER_PATH=$(pwd) VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade \
#   EDENPROBE_MASQUERADE_HIDE=VK_EXT_graphics_pipeline_library,shaderFloat16 eden-cli -g ...
#
# `make check` asks the first device what the layer lets through, against what the driver says
# without it: every mask hides what it names, an extension the core of the device's version has
# keeps its features, none changes nothing, and a name the registry has not stops the instance.
# It submits nothing to the device. `make features VK_XML=...` makes features.inc again from a
# newer Vulkan registry.

CC     ?= cc
CFLAGS := -std=c11 -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror
OUT    := libVkLayer_edenprobe_masquerade.so
VK_XML ?= /usr/share/vulkan/registry/vk.xml
ADD    := VK_ADD_LAYER_PATH=$(CURDIR) VK_INSTANCE_LAYERS=
# an extension with a structure of its own, one the core took in with its feature, a core 1.0
# feature and a core 1.3 one
NAMES  := VK_EXT_extended_dynamic_state3 VK_EXT_host_query_reset hostQueryReset independentBlend synchronization2
comma  := ,
empty  :=
space  := $(empty) $(empty)

$(OUT): masquerade.c features.h features.inc
	$(CC) $(CFLAGS) -shared -o $@ $<

check_masquerade: check_masquerade.c features.h features.inc
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -o $@ $< -lvulkan

features:
	python3 features.py $(VK_XML) features.inc

check: $(OUT) check_masquerade
	$(ADD) EDENPROBE_MASQUERADE_HIDE=$(subst $(space),$(comma),$(NAMES)) ./check_masquerade --expect hidden $(NAMES)
	$(ADD) ./check_masquerade --expect same $(NAMES)
	$(ADD) EDENPROBE_MASQUERADE_HIDE=VK_KHR_synchronization2 ./check_masquerade --expect same synchronization2
	$(ADD) EDENPROBE_MASQUERADE_API=1.1 EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1 \
	    ./check_masquerade --expect hidden --api 1.1 VK_EXT_host_query_reset hostQueryReset
	$(ADD) EDENPROBE_MASQUERADE_HIDE=VK_EXT_no_such_thing ./check_masquerade independentBlend \
	    2>check.err; test $$? = 3 && grep -q 'names VK_EXT_no_such_thing' check.err
	$(ADD) EDENPROBE_MASQUERADE_API=1.9 ./check_masquerade independentBlend \
	    2>check.err; test $$? = 3 && grep -q 'API=1.9 is not' check.err
	@rm -f check.err
	@echo "check: each mask hides what it names, the core keeps its features, none changes nothing, unknown names stop"

clean:
	rm -f $(OUT) check_masquerade check.err

.PHONY: check clean features
