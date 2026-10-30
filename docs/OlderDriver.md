# An older driver

`tools/vklayer` builds `VK_LAYER_EDENPROBE_masquerade`, a Vulkan layer on the emulator's host that makes the driver
look older to the emulator, for the paths that run only on such a driver: `EDENPROBE_MASQUERADE_API=1.1` reports
Vulkan 1.1, and `EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1` takes away `VK_EXT_host_query_reset` and the
`hostQueryReset` feature. Nothing else changes.

```sh
make -C tools/vklayer
VK_ADD_LAYER_PATH=$PWD/tools/vklayer VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade \
EDENPROBE_MASQUERADE_API=1.1 EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1 eden-cli -g edenprobe.nro
```

`make -C tools/vklayer check` asks the first device what the layer lets through and submits nothing:
`api 1.1 hostQueryReset 0 VK_EXT_host_query_reset 0` with both on, the driver's own answer with neither. The ring
and occlusion suites need it on RADV, where Eden takes neither path otherwise.
