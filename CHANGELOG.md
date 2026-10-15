# Changelog

All notable changes to Arclight are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/). While the version is 0.x, each minor version is one roadmap phase.

## [Unreleased]

Phase 2 in progress: textures, resource lifetimes, constants, scene viewport.

### Added (scene viewport)
- Scene target: the renderer owns a RenderTarget + Sampled texture (`Renderer::sceneTexture()`, `sceneFormat()`), and `beginScenePass()` / `endScenePass()` draw into it. `Renderer::finishScene()`, called by the Application after every module's `render()`, clears it if nothing drew and moves it to ShaderRead for the UI.
- *Scene* panel (`Editor/Source/Panels/ScenePanel`): shows the scene target with `ImGui::Image`, resizes it to the panel's size in pixels (full resolution on Retina), shows the resolution and graphics API, and reports whether the mouse is over it. Docked in the centre by the default layout.
- `ImGuiModule::textureId(texture)`: an ImTextureID for any device texture. On D3D12 the texture's SRV is copied into the shader-visible heap once, cached by `TextureHandle`, and freed when the texture is gone and the last frame that drew it has finished. On Metal the ID is the `MTLTexture`.

### Changed (scene viewport)
- The texture demo renders into the scene target instead of the backbuffer; its aspect ratio follows the Scene panel. Only the UI draws to the backbuffer now.
- `EditorModule` takes the `ImGuiModule`; the dock space no longer has a transparent central node.

### Added (constants)
- `RHI/UploadRing`: per-frame scratch memory in one Upload buffer, one region per frame in flight plus one for immediate submissions, reset when the frame slot comes round again. Starts with 64 KB of zeros that unset constant slots read.
- `CommandList::setConstants(slot, data)` (and a typed overload) for cbuffer slots b0–b2 in both shader stages: copies the data into the ring and binds it. D3D12 uses root constant buffer views; Metal binds the buffer at `[[buffer(n)]]`.
- `Engine/Shaders/Constants.hlsli` (`ViewConstants` at b0) mirrored by `Renderer/ShaderConstants.h`, with `static_assert`s on every offset against HLSL's 16-byte-row packing.
- Texture demo: the ground plane is a unit quad in world space with a camera in b0 and a per-draw transform, texture-coordinate scale/offset and mip bias in b1. New *Mip bias*, *Field of view* and *Height* controls.
- Unit tests for the upload ring.

### Changed (constants)
- The D3D12 root signature gains three root CBVs (b0–b2) after the SRV and sampler tables.
- The texture demo draws one 4-vertex quad twice instead of building clip-space vertices on the CPU, so resizing no longer creates any buffer.

### Added (resource lifetimes)
- `Core/Handle.h`: generational handles (`Handle<Tag>`), `HandleAllocator` and `HandlePool`. A freed slot bumps its generation, so stale handles are detected instead of reaching whatever reuses the slot.
- Buffers, textures and samplers get handles (`BufferHandle`, `TextureHandle`, `SamplerHandle`); `Device::find()` returns the resource, or nullptr once it has been destroyed. Systems that do not own a resource keep its handle instead of a pointer.
- Deferred release: a buffer, texture or pipeline may be destroyed while frames in flight still use it. On D3D12 the device keeps the GPU object until the fence value of the last submission that could use it has passed (`RHI/DeferredRelease.h`); objects destroyed while a frame is being recorded wait for that frame's own fence value. Metal needs no queue, because command buffers retain the resources they use.
- `Device::resourceCounts()`: live buffers, textures and samplers, and objects awaiting release; shown live in the System Info panel.
- The device logs every buffer, texture and sampler still alive when it is destroyed, by name.
- D3D12 persistent descriptors carry a handle, so a double free or a stale copy is caught.
- Texture demo: *Recreate textures* button, which replaces every texture while frames in flight still use the old ones.
- Unit tests for handles, the deferred release queue and the resource registry.

### Changed (resource lifetimes)
- `Device::createBuffer/createTexture/createSampler` are no longer virtual: they validate the description, call the backend's `create*Impl()` and register the resource.
- The texture demo builds a new vertex buffer when the window is resized instead of calling `waitIdle()` and overwriting the old one.

### Added
- Textures: `rhi::Texture` with mip levels and Sampled / RenderTarget usage, `Device::createTexture()`, the `RGBA8_UNORM_SRGB` format, texture barriers, and `copyBufferToTexture` / `copyTextureToBuffer` with one copy layout for both backends (`kTextureCopyRowAlignment`, `kTextureCopyOffsetAlignment`).
- Samplers: `rhi::Sampler` and `Device::createSampler()` with min/mag/mip filters, repeat / mirrored repeat / clamp / border addressing, border colours, anisotropic filtering and LOD clamps.
- Resource binding: `CommandList::setTexture()` and `setSampler()` bind to slots t0–t7 and s0–s3 of both shader stages.
- D3D12: a shared root signature with an SRV table and a sampler table; descriptor heaps with persistent (free-list) and per-frame transient parts; descriptor tables built per draw by copying from CPU-only heaps; a null SRV and default sampler for unbound slots.
- `RHI/TextureUtils`: `computeTextureCopyLayout`, `uploadTexture`, `readbackTexture`, and a texture round-trip self-test that Debug builds run at start-up.
- `Renderer/Image`: PNG, JPEG, TGA and BMP decoding (stb_image), box-filtered downsampling that averages sRGB images in linear space, and full mip chain generation. `Renderer/TextureLoader` creates and uploads textures from images or files.
- `findAssetPath()` looks for `Assets/` in the working directory, then next to the executable; the release zip now includes `Assets/`.
- Texture demo (`Editor/Source/Scene/TextureDemoModule`): a perspective ground plane and an address-mode quad, with a *Texture Demo* panel for the texture (UV grid, checker, colour-per-mip-level) and sampler settings.
- `Engine/Shaders/Common.hlsli` with `LinearToSrgb()`, and `Textured.hlsl`.
- Unit tests for mip sizes, the copy layout, texture round trips, samplers, downsampling and PNG decoding.

### Changed
- The Metal shader build passes `--msl-decoration-binding` to SPIRV-Cross, so HLSL register numbers become the Metal binding indices.
- The ImGui D3D12 backend takes its descriptors from the device's shader-visible heap instead of a heap of its own, so drawing the UI no longer switches descriptor heaps.
- `beginRenderPass()` moves its target to the RenderTarget state through the same texture state tracking as everything else; backbuffers are created with RenderTarget usage.

### Removed
- The Phase 1 triangle (`TriangleModule`, `Triangle.hlsl`), replaced by the texture demo.

## [0.2.0] - 2026-10-15

Phase 1: RHI and first triangle.

### Added
- Rendering hardware interface (`Engine/Source/RHI/`): device, swapchain, command list, texture and buffer interfaces, with two frames in flight.
- Direct3D 12 backend: debug layer (messages routed to the Console panel where supported), high-performance adapter selection, flip-model swapchain with a frame-latency waitable object, tearing when VSync is off, swapchain resize, live-object report at shutdown.
- Metal backend: command buffers with semaphore-based frames in flight, `CAMetalLayer` swapchain, Metal API validation and GPU frame capture enabled in the Xcode scheme.
- Null backend for headless builds and unit tests.
- Buffers in GPU-only, upload and readback memory; buffer copies; resource-state barriers; immediate submission for load-time work; `uploadBuffer` / `readbackBuffer` helpers and a start-up round-trip self-test in Debug builds.
- System Info panel shows the graphics API, GPU, memory, driver version and the attached GPU capture tool.
- In-app GPU frame capture (*Debug → Capture GPU Frame*, F11): RenderDoc in-app API when launched from RenderDoc, PIX's GPU capturer with `--pix`, and `MTLCaptureManager` under Xcode. Command-line options `--pix` and `--gpu-validation`.
- Build-time shader compilation (`cmake/ArclightShaders.cmake`): HLSL → DXIL with DXC on Windows (pinned v1.9.2607); HLSL → SPIR-V → MSL (SPIRV-Cross) → `.metallib` on macOS. Debug shaders keep debug info; Release shaders are optimised. Compiled shaders are copied into the release zip.
- Graphics pipelines: `rhi::Pipeline` with vertex layout, cull mode and front-face winding; D3D12 root signature and pipeline state object, Metal render pipeline state with a vertex descriptor.
- Draw commands: `setPipeline`, `setVertexBuffer`, `setIndexBuffer`, `draw`, `drawIndexed`.
- The editor draws a coloured triangle from GPU-only vertex and index buffers (`Editor/Source/Scene/TriangleModule`).
- `rhi::Shader`, `Device::createShader()` and a `ShaderLibrary` that loads compiled shaders from the `Shaders/` folder next to the executable.

### Changed
- The editor executable is built into `build/<preset>/bin/<Config>/`, next to its `Shaders/` folder.
- macOS builds need the Vulkan SDK for DXC; the release workflow installs it.
- The editor UI renders through the engine's RHI (`imgui_impl_dx12` / `imgui_impl_metal`) instead of SDL_Renderer.
- Resizing the window resizes the swapchain.

## [0.1.0] - 2026-10-14

Phase 0: Foundations. No GPU rendering yet.

### Added
- CMake build with presets for Visual Studio 2022 (Windows x64) and Xcode (macOS, Apple silicon).
- Dependencies fetched by CMake at pinned versions: SDL3 3.4.18 (with SHA-256 check), Dear ImGui 1.92.9b (docking), doctest 2.5.3.
- `ArclightEngine` static library and `Arclight` editor executable.
- SDL3 window and an application loop with ordered module init and reverse-order shutdown; the loop sleeps while the window is minimised.
- Logging (`ARC_LOG_*`) using `std::format` and `std::source_location`, and asserts (`ARC_ASSERT`, `ARC_VERIFY`, `ARC_UNREACHABLE`).
- Frame timing on `steady_clock`, a game clock with play, pause, stop, single-step and time scale.
- Keyboard, mouse and gamepad input.
- Math library (vectors, quaternions, matrices) with unit tests.
- Dockable Dear ImGui editor shell with Console, Performance (FPS graph), Playback and System Info panels, and View > Reset Layout.
- Release packaging: the Release editor build has no console window on Windows, and `cmake --workflow --preset <platform>-release` configures, builds, tests and zips it.
- GitHub Actions workflow that builds the Windows and macOS zips and drafts a GitHub release when a `v*` tag is pushed.

[Unreleased]: https://github.com/Anand-Ramnarain-27/arclight-engine/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/Anand-Ramnarain-27/arclight-engine/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Anand-Ramnarain-27/arclight-engine/releases/tag/v0.1.0
