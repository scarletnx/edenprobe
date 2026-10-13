# The validation layer's view

`make validate EMULATOR=...` runs the graphics family once under the Khronos validation layer,
through `tools/run.py` like `make run` (a launch that crashes is followed by one that goes on),
synchronisation validation on (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`,
`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`), with the same XDG override as `make run`, and prints
one row per message id: the VUID or the SYNC-HAZARD, how many times, and the case it came
before, since the layer speaks while a case works and the case reports when it is done.
`tools/validate.py diff a.log b.log` lists the ids whose counts moved between two emulators. It
is the host half of what the suites check: a wrong index buffer size or an image created with
view formats it cannot have is a VUID here long before it is a wrong pixel on a forgiving
driver. It needs the layer installed on the host, and it is slow: several times a plain run.
