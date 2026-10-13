# Making the driver look older

Some emulator paths run only on an older driver: a device that reports Vulkan 1.1 enables later
features through their extensions, and one without host query reset resets queries in command
buffers. `tools/vklayer` builds `VK_LAYER_EDENPROBE_masquerade`, a Vulkan layer for the host that
runs the emulator, which makes this machine's driver look like such a one to the emulator above
it: with `EDENPROBE_MASQUERADE_API=1.1` the physical device reports Vulkan 1.1, and with
`EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1` it has no `VK_EXT_host_query_reset` and no
`hostQueryReset` feature. Nothing else changes, and the driver underneath is the same.

```sh
make -C tools/vklayer
VK_ADD_LAYER_PATH=$PWD/tools/vklayer VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade \
EDENPROBE_MASQUERADE_API=1.1 EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1 eden-cli -g edenprobe.nro
```

`make -C tools/vklayer check` asks the first device, through the loader, what the layer lets
through, and submits nothing: `api 1.1 hostQueryReset 0 VK_EXT_host_query_reset 0` with both
masks on, and the driver's own answer with neither. The ring and occlusion suites were written
for it: on RADV, which is Vulkan 1.4 with host query reset, Eden takes neither path without it.
