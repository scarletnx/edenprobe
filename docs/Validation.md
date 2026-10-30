# The validation layer

`make validate EMULATOR=...` runs the graphics family once under the Khronos validation layer, with synchronisation
validation (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`), through
`tools/run.py` as `make run` does, and prints a row per message id: the VUID or SYNC-HAZARD, how many times, and the
case it came before. `tools/validate.py diff a.log b.log` lists the ids whose counts moved between two emulators.
It needs the layer installed on the host, and takes several times a plain run.
