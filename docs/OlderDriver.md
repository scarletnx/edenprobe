# An older driver

`tools/vklayer` builds `VK_LAYER_EDENPROBE_masquerade`, a Vulkan layer on the emulator's host that makes the driver
look like one that lacks what its variables name, for the paths an emulator takes only on such a driver: the path a
driver ban sends it down, or an older driver's. Each variable is off unless set:

| variable | what the emulator sees |
|---|---|
| `EDENPROBE_MASQUERADE_API=1.1` | that Vulkan version, `1.0` to `1.4`, where the device's is newer: an emulator then takes the later versions' features through their extensions |
| `EDENPROBE_MASQUERADE_HIDE=NAME,NAME` | each `NAME` a device extension or a feature, as the Vulkan registry names them. An extension leaves the device's list, and a feature structure that only hidden extensions add reads all `VK_FALSE`, unless the version the layer reports has that structure in its core. A feature reads `VK_FALSE` in every structure that has it, `VkPhysicalDeviceFeatures` and `VkPhysicalDeviceVulkan1xFeatures` too |
| `EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1` | the same as `HIDE=VK_EXT_host_query_reset,hostQueryReset` |

An extension the device's version has in its core keeps its features: hiding `VK_KHR_synchronization2` on a 1.3
device takes the name away and leaves `synchronization2` on, as a 1.3 driver that no longer listed the extension
would. Name the feature too to take it away. A structure that the reported version lacks and that no extension still
listed adds reads all `VK_FALSE`, so `EDENPROBE_MASQUERADE_API=1.1` also turns off `VkPhysicalDeviceVulkan12Features`
for an emulator that asks for it anyway.

A name the registry has not, or an API that is no version, stops the instance (`VK_ERROR_INITIALIZATION_FAILED`)
with the reason on stderr, so a typo never runs as a driver with nothing hidden:
`VK_LAYER_EDENPROBE_masquerade: EDENPROBE_MASQUERADE_HIDE names VK_EXT_no_such_thing, which is no device extension or
feature of the Vulkan registry 1.4.357 this layer was made from: no instance is made`. An instance that masks
anything says what on stderr as it is made, so the emulator's log carries it:
`VK_LAYER_EDENPROBE_masquerade: hidden: VK_EXT_graphics_pipeline_library shaderFloat16`.

The driver itself is not changed: an emulator that enables what was hidden still gets it, so the masks decide what
it chooses, not what the driver can do.

```sh
make -C tools/vklayer
VK_ADD_LAYER_PATH=$PWD/tools/vklayer VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade \
EDENPROBE_MASQUERADE_HIDE=VK_EXT_graphics_pipeline_library,shaderFloat16 eden-cli -g edenprobe.nro
```

A run per extension the emulator can do without, each against a run with nothing hidden and a `label=` of its own,
says where a ban would be safe (the path without the extension passes the same cases) and what it costs (the
suites' times and the emulator's memory).

`make -C tools/vklayer check` asks the first device about a few names, once without the layer and once through it,
and submits nothing: every mask hides what it names (an extension with a structure of its own, one the core took in
with its feature, a core 1.0 feature and a core 1.3 one), a hidden extension that the core has keeps its feature,
nothing hidden shows everything as the driver does, `EDENPROBE_MASQUERADE_API=1.1` with the host query reset mask
still works, and an unknown name or version stops the instance. `check_masquerade --expect hidden NAME...` asks the
same of any names. The ring and occlusion suites need `EDENPROBE_MASQUERADE_API=1.1` and the host query reset mask on
RADV, where Eden takes neither path otherwise.

The layer finds the names in `tools/vklayer/features.inc`, which `features.py` makes from the Vulkan registry
(`vk.xml`); `make -C tools/vklayer features VK_XML=...` makes it again from a newer one.
