# The roadmap

The suites are ordered by how much one case tells you when two machines disagree, each leaning on
the ones above it. That order is the whole point: when a run comes back with forty failures, the
first suite that failed is the only one worth reading, because everything below it was measured with
a broken instrument. It is the order they run in and were built in; anything added later goes where
its dependencies put it, not at the end.

| suite | tier | what a failure points at |
|---|---|---|
| selfcheck | none, no hardware | the comparators themselves. Nothing below can be believed. |
| support | none, no hardware | what is written and drawn around a verdict, never a verdict. |
| reference | none, no hardware | the expected answers: the coverage rule, the blend arithmetic, the texel address. The judge, not the machine. |
| cpu | exact | the emulator's translation of one processor instruction, the one the case names. In Eden: dynarmic's x64 backend (`src/dynarmic/src/dynarmic/backend/x64/`), the emitter or the soft-float fallback of that instruction. For the compile case, the register allocator (`backend/x64/reg_alloc.cpp`, `backend/arm64/reg_alloc.cpp`) before any one instruction: the word that came out wrong names the register. For the three NEON gap cases, an instruction the decoder table has no entry for (`frontend/A64/decoder/a64.inc`). For the system register cases, `frontend/A64/translate/impl/system.cpp`. |
| pad | exact, level for the sticks' edges and the vibration | a person's controller reaching a game: the binding of a host button to the console's (`input_common/drivers/sdl_driver.cpp`, `GetDefaultButtonBinding`, which binds by position), a stick's dead zone and range (`input_common/input_mapping.cpp`), the slot and style a controller is given (`hid_core/`), and the vibration's curve and frequency scaling (`sdl_driver.cpp`, `RumblePlay`). It runs only when named, right after cpu. |
| touch | position for where a tap lands and the drag, exact for two fingers and the lift | the touch screen reaching a game: where a click or a finger on the window becomes a touch (`yuzu/bootmanager.cpp`, `core/frontend/emu_window.cpp`, `MapToTouchScreen`; the command line frontend's `MouseToTouchPos`), the scale to the screen's pixels (`hid_core/resources/touch_screen/touch_screen_driver.cpp`, `WaitForInput`), the ids a touch is given (`hid_core/frontend/emulated_console.cpp`, `SetTouch`), and the Start and End states. It runs with pad, after its prompts. |
| clear | exact | the render target's memory layout and the path that copies it back. Everything below draws into that same target. |
| format | exact, ulp for floats | the render target format mapping for one format: its host format, its clear and its fragment output type. In an emulator: `video_core/surface.cpp` and `video_core/renderer_vulkan/maxwell_to_vk.cpp`, and the clear of an integer target. Inside or outside the quad tells a conversion from a coverage fault. |
| raster | position for edges, exact for interiors | vertex fetch, the clip space to screen space transform, and the coverage rule. In an emulator: the viewport registers, the y direction, the half pixel offset, and the fill rule the host rasteriser is asked for. |
| intmath | exact | the shader translator, bit for bit, and the delivery of the fragment's own coordinate. In an emulator: one wrongly decoded or untranslated instruction (`shader_recompiler/frontend/maxwell/translate/impl/`; the log names the instruction, the failing family names the shader), or an integer render target mapped to a normalised host format. |
| blend | exact | the fixed function blend unit: the equation and factor registers, and the rounding on the way back to eight bits. |
| depth | exact | the depth test: the depth target's format and layout, the depth range set up by the viewport, and the compare operation. |
| texture | exact | the texture and sampler descriptors: the wrap mode and filter fields, and the block linear layout a sampled image is stored in. |
| coherence | exact | a cache between two uses of the same memory. In an emulator: the texture cache not marking an image the graphics unit wrote as modified, or missing a processor write to one (`texture_cache/texture_cache.h`, `MarkModification`, `WriteMemory`); the buffer cache serving a uniform buffer the processor rewrote, or dropping an update pushed inside the list (`buffer_cache/buffer_cache.h`, `Maxwell3D::ProcessCBData`); the copy engine's writes not reaching the images it overlaps (`engines/maxwell_dma.cpp`), or a render target not flushed before the copy engine reads it; or two formats over one memory joined as a conversion rather than a view (`texture_cache/texture_cache.h`, `JoinImages`). The region two pixels that still hold region one's contents say which. |
| fmath | units in the last place | the float side of the shader translator, and the transcendental unit behind it; for the edge cases, the minimum and maximum's NaN rule, denormal handling and the rounding modes. The only suite where two correct machines are allowed to differ, which is why it is last among the graphics suites. |
| order | exact | the host side of the guest's wait for idle: a compute write not ordered before the vertex, index, indirect, uniform or storage read after it (`renderer_vulkan/vk_rasterizer.cpp`, `WaitForIdle`), or indirect arguments read from memory before the dispatch that wrote them finished; and the same after the full, fragments and tiles barriers (`engines/puller.cpp`, `RefCnt`; `FragmentBarrier`, `TiledCacheBarrier`). |
| index | exact | the size an index buffer is bound with: a limit register read as one past the end drops the last index (`buffer_cache/buffer_cache.h`, `UpdateIndexBuffer`). |
| instance | exact | when an instanced array draw happens and as which instance (`engines/draw_manager.cpp`): deferred past a state change, extra instances added to the next draw, or SUBSEQUENT not counted as the next instance. |
| geometry | exact | the geometry and tessellation stages: points expanded or dropped, a patch's coverage and the levels reaching the evaluation stage; on a host without those stages, whatever the emulator does instead. The twins without PRET and without a level read tell a missing stage from the translator's PRET (`control_flow.cpp`) and its level read in the evaluation stage (`collect_shader_info_pass.cpp`). |
| bindless | exact | a texture reached through a handle at a dynamic index: the host descriptor array the translator builds for it, its size and how it is filled (`shader_recompiler/ir_opt/texture_pass.cpp`). |
| astc | exact, either of two bytes | the emulator's ASTC decoder: weight and endpoint unquantization, the integer sequence, the partition hash, infill, dual plane, and the error colour for HDR modes and illegal blocks (`host_shaders/astc_decoder.comp`, `textures/astc.cpp`). |
| eac | exact, either of two bytes | the EAC formats: their mapping to a host format and the decode on a host that has none (`texture_cache/format_lookup_table.cpp`, `surface.h`, `maxwell_to_vk.cpp`). |
| etc2 | exact | the ETC2 colour formats: the decode on a host without ETC2 textures, mode by mode, and the punch-through rules (`maxwell_to_vk.cpp`, `texture_cache/util.cpp`, `ConvertImage`); a detail that names the block's own bytes is an upload with no decode at all. |
| bcn | exact | the BCn decode on a host without BCn textures, across the layers and the small levels of an array (`texture_cache/decode_bc.cpp`). |
| zeta | exact | a depth texture's upload: 24-bit depth converted for a host without it and taken from the right bits (`vk_texture_cache.cpp`, `TransformBufferImageCopies`). |
| swizzle | exact | the block linear unswizzle an emulator runs when a texture is first used, by texel size and block height (`textures/decoders.cpp`, `host_shaders/block_linear_unswizzle_2d.comp`). |
| texel | exact | the offset a buffer texture's host view starts at, when the host's texel buffer alignment is coarser than the guest's (`vk_buffer_cache.cpp`, `Buffer::View`). |
| stream | exact | uploads staged through a ring inside one list: a later draw's bytes handed to an earlier one when the ring wraps (`vk_staging_buffer_pool.cpp`, `GetStreamBuffer`), or an upload path that asks the host for far more memory than the guest data it moves. |
| fill | exact | fill rate with and without depth; the time, not the verdict, is the point: a depth buffer the host cannot compress shows as the second case slowing against the first. |
| ring | exact | a draw's descriptors taken from a ring after its geometry is bound: the ring's wait for the GPU submits the binds apart from the draw, which is lost (`renderer_vulkan/vk_descriptor_buffer.cpp`, the order of `vk_graphics_pipeline.cpp`, `ConfigureImpl`). |
| occlusion | exact | a query bank reset inside a draw or clear's render pass: the pass ends and the draw or clear lands outside it (`renderer_vulkan/vk_query_cache.cpp`, `SamplesQueryBank::Reset`); or counts that are not the pixels drawn. |
| drawtex | exact | the state DRAW_TEXTURE leaves bound: the next draw keeps the blit's pipeline or its viewport and scissor (`renderer_vulkan/blit_image.cpp`, `BlitColor`, and `vk_rasterizer.cpp`, `DrawTexture`). |
| query | exact | the samples counter (a count scaled with the render target, `vk_query_cache.cpp`) and the render enable over values and reports (`query_cache.h`, `AccelerateHostConditionalRendering`; `maxwell_3d.cpp`, `ProcessQueryCondition`), draws and clears alike; a clear wrong over the processor's values ignores the render enable, one wrong only over reports in the same list loses them (`vk_query_cache.cpp`, `HostConditionalRenderingCompareValues`). |
| dma | exact | copies of a buffer by the copy engine, and what copying data the graphics unit wrote costs (`buffer_cache/buffer_cache.h`, `DMACopy`). |
| blit2d | exact | the 2D engine's scaled copy: where its result goes (an emulator that draws it into its texture cache leaves the memory as filled: `engines/fermi_2d.cpp`, `Blit`; `texture_cache/texture_cache.h`, `BlitImage`), which texel each origin samples, the span a fraction in du/dx covers (`fermi_2d.cpp`, the source rectangle cut to whole texels), and whether a window cut mid-run is written and nothing else. |
| fs | exact | reads through the file system service: the cost per call and per byte, seeks, small files, and the edges of a file (`hle/service/filesystem`, `file_sys/vfs/vfs_real.cpp`, `common/fs/file.cpp`). |
| sched | position | the scheduler (`hle/kernel/`): an owner that does not inherit the priority of the thread waiting for its mutex, or does not pass it down a chain (`k_thread.cpp`, `RestorePriority`); a thread spinning with no supervisor call that keeps its core from a better one (`k_scheduler.cpp`, `RescheduleCores`, and the CPU's interrupt); a ready thread that waits behind it with an idle core in its mask (`k_scheduler.cpp`, `UpdateHighestPriorityThreadsImpl`); `svcSleepThread(0)` that moves a thread. |
| binder | exact, position | the buffer producer's answer to transaction 17, `GetBufferHistory` (`hle/service/nvnflinger/buffer_queue_producer.cpp`, `Transact`): its layout, its count, its order. |
| vic | exact | the VIC: where its input comes from (an emulator that composes only what its decoder made writes nothing), the configuration structure field by field, the colour matrix and the soft clamp, the alpha fill, the target, source and destination rects, the background and the output formats (`video_core/host1x/vic.cpp`, `Execute`, `Blend`, `WriteABGR`). |
| nvdec | exact | the decode engine on VP9: where a decoded image goes (an emulator that hands it to its own compositor leaves the surface untouched), when it is there, the picture setup and probability table field by field, a hidden frame and `LastShowFrame`, a frame shown again, and the chroma quantizer bytes (`video_core/host1x/codecs/vp9.cpp`, `GetCurrentFrame`, `ComposeCompressedHeader`; `codecs/decoder.cpp`, `Decode`). |
| audio | level | the audio renderer (`audio_core/renderer/`): the table the resampler takes for a ratio (`command/resample/resample.cpp`), a circular buffer sink's parameters and in-use check (`sink/sink_info_base.h`, `sink/circular_buffer_sink_info.cpp`), and the limiter's parameters (`effect/light_limiter.h`). |
| hwopus | exact | the hardware Opus decoder's limit on live decoders, and the result past it (`hle/service/audio/hardware_opus_decoder_manager.cpp`, `audio_core/opus/hardware_opus.cpp`, `RegisterDecoder`). |
| am | exact | the application's account save size: the user read from the wrong byte of the request, or the size ExtendSaveData asks for not kept for GetSaveDataSize (`hle/service/am/service/application_functions.cpp`, `SaveDataSizeTarget`). |
| fsp | exact | the temporary storage not emptied at a launch of the application, or emptied each time a program of it opens its saves (`core.cpp`, `Load`; `hle/service/filesystem/filesystem.cpp`, `InitTempStorage`). Asks from the second launch on. |
| account | exact | the user picked with nobody asked: TrySelectUserWithoutInteraction (52, 19.0.0) missing from acc:u0's table, or answering otherwise than 51 (`hle/service/acc/acc.cpp`). |
| ldn | exact | the client process monitor of 18.0.0 missing from ldn:u, or handing out no object for RegisterClient (`hle/service/ldn/ldn.cpp`, `IUserServiceCreator`). |
| set | exact | the language codes: the console's own not in the list, an index made into another code, one past the table not refused, a count below the list (`hle/service/set/settings_server.cpp`). |
| csrng | exact | random bytes: a buffer not written to its end, or the same bytes twice (`hle/service/spl/spl_module.cpp`, `GenerateRandomBytes`). |
| apm | exact | apm, or a session on it, refused to a game, or a configuration not kept per mode or not the one SetCpuBoostMode picks (`hle/service/apm/apm.cpp`, `apm_interface.cpp`, `apm_controller.cpp`). |
| nfp | exact | the detection of 23.0.0: StartDetectionWithFilter answering otherwise than StartDetection, an unknown command's success where there is no device (`hle/service/nfp/nfp.cpp`). |
| aoc | exact | the add-on content a game counts and lists: one counted or listed with none installed, a success past the end where reden answers ApplicationRecordNotFound, a list that fits its room refused or written past, a base id other than the program's base and 0x1000, or a prepare of an index not installed refused where reden answers success (`hle/service/aoc/addon_content_manager.cpp`, `CountAddOnContent`, `ListAddOnContent`, `GetAddOnContentBaseId`, `PrepareAddOnContent`). |
| time | exact | the clocks and dates a game reads at start: the user clock off its context and the steady clock, a steady clock that does not count seconds, dates under UTC and under the device's rule (`hle/service/psc/time/clocks/system_clock_core.cpp`, `hle/service/glue/time/time_zone.cpp`). |
| pl | exact | the shared fonts: a font not loaded, outside the memory, behind another magic or not a font, and a priority list that is not the six as told (`hle/service/ns/platform_service_manager.cpp`). |
| profile | exact | the user's profile: two bases, no name, a picture that is not a 256x256 JPEG of the size told (`hle/service/acc/acc.cpp`, `acc/profile_picture.cpp`), and a user that does not exist. |
| am-state | exact | the state getter: a resolution not the mode's, out of focus, no message 95 after 130, 3D VR mode not kept, a region that is not Global (`hle/service/am/service/common_state_getter.cpp`). |
| am-self | exact | the self controller's settings: the idle time detection extension or auto sleep not read back as set (`hle/service/am/service/self_controller.cpp`). |
| am-functions | exact | the application's functions: a desired language set does not list, an event handed out as no handle, a channel not refused as empty (`hle/service/am/service/application_functions.cpp`). |
| auddev | exact | the audio device service: an active output not listed, a volume not read back as set (`hle/service/audio/audio_device.cpp`). |
| fsdir | exact | a game's own directories on the card: a rename that leaves the old name or loses what it holds, a listing that is not the one file once, a recursive delete that leaves the directory (`hle/service/filesystem/fsp/fs_i_filesystem.cpp`). |
| am-audio | exact | the audio controller: the expected master volumes or a changed main volume not read back as set (`hle/service/am/service/audio_controller.cpp`). |
| am-channel | exact | the user channel: a storage unpopped not popped back, the order or the clear wrong, or a previous program index other than -1 (`hle/service/am/service/application_functions.cpp`). |
| am-storage | exact | a library applet's storage: its size or bytes not kept, or an access past its end not refused with InvalidOffset (`hle/service/am/library_applet_storage.cpp`). |
| mii | exact | mii:u's default Miis: not six, not given by Get, not BuildDefault's builds, the database said full, or a seventh built (`hle/service/mii/mii_manager.cpp`). |
| am-mode | exact | the state getter's modes: VR mode not read back as turned, a boot mode other than normal, or a controller update said running (`hle/service/am/service/common_state_getter.cpp`). |
| bsd-local | exact | bsd:u without a network: a raw socket opened, a timeout out of range or short not refused as a BSD stack does, or an address's length not cut to its buffer (`hle/service/sockets/bsd.cpp`). |
| ipc | exact | the IpcController and Close: no pointer buffer, a clone that does not serve the same object, or a Close not answered SessionClosed (`hle/service/sm/sm_controller.cpp`). |
| hid | exact | the npad LIFOs: an entry's storage not numbered twice its state, or states not one apart (`hid_core/resources/ring_lifo.h`). |
| time-snap | exact | the clock snapshot: not the user clock's context, time or location, or a span between two not their steady difference in nanoseconds (`hle/service/psc/time/static.cpp`). |
| vi | exact | the application display service: another display list, a scaling mode not converted to the window's numbering, or an indirect layer's memory not rounded to 0x20000 (`hle/service/vi/application_display_service.cpp`). |
| renderer | exact | an audio renderer's parameters not the ones it opened with, or the rendering time limit or voice drop parameter not starting at the default and read back as set (`hle/service/audio/audio_renderer.cpp`). |
| ngc | exact | the profanity filter: no version, a word found in a clean name, or a clean name not given back as it was sent (`hle/service/ngc/ngc.cpp`). |
| nifm | exact | the network state a program is told: the connection status, the address, and a request's life from submitted to accepted (`hle/service/nifm/nifm.cpp`, `IRequest`, `HasConnection`). |
| dns | exact | name resolution: the two serialisations sfdnsres writes, nsd's placeholder, and, signed in, the rewrite table and the loopback redirect (`hle/service/sockets/{sfdnsres,nsd}.cpp`, `core/online_initiator.cpp`, `ResolveUrl`). |
| bsd | exact | sockets on the host: bind, datagrams, streams, a refused connect, a receive timeout (`SetSockOptImpl`, and `internal_network/network.cpp`), a connect that does not block, and the duplicate an ssl connection is handed, which shares the program's socket and dies with it (`hle/service/sockets/bsd.cpp`, `DuplicateSocketImpl`, `CloseImpl`). |
| ssl | exact | the secure connection: its defaults and getters, the descriptor it hands back, verification, an imported certificate, the handshake, and a request and its answer (`hle/service/ssl/ssl.cpp`, `ssl_backend_openssl.cpp`). |
| acc | exact | the account a signed in session reports and the id token it caches (`hle/service/acc/acc.cpp`, `IManagerForApplication`, `EnsureIdTokenCacheAsyncInterface`). |
| friend | exact | the blocked list and the profiles a session fetches (`hle/service/friend/{friend,profile_query}.cpp`). |

What comes next, in this order, each its own change with its own critic:

- **the net family, sharper**: a rewrite whose destination has an address of its own, so the two
  sfdnsres rewrite cases can tell a published rewrite from the session's loopback redirect (today
  the fixture's destination is `localhost`, which the redirect also gives); and a verifying
  handshake with a name the certificate does not carry, which must be refused.
- **another emulator**: the same binary under Ryujinx, with its own keys, and the two reports diffed
  against Eden's and a console's. Nothing in this program depends on the emulator it runs under, and
  that run is what proves it.

Two rules hold across all of them. Every case computes what it expects on the processor and checks
every pixel, so there is no stored image anywhere and no machine's answer is the reference for
another's. And every rule a case judges by, wherever it is more than one line of arithmetic, is
implemented once in `source/` and pinned by the `reference` suite, which runs with no graphics
device: `make -f host.mk host-run` proves the judge before a console or an emulator is involved.

The picture a failing case leaves and the detailed mode that shows it are support, not suites.
Neither decides anything, neither is compared to anything, and no run's answer changes if either is
turned off; they sit in the `support` row only because that suite checks them.
