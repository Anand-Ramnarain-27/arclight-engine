# Changelog

All notable changes to Arclight are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/). While the version is 0.x, each minor version is one roadmap phase.

## [Unreleased]

Phase 3: engine architecture.

### Added

#### Scene
- `Scene/Scene`, `GameObject`, `Transform` and `Component`: objects with a name, a 64-bit `Uuid`, an active flag, a transform and owned components, in an ordered hierarchy under a hidden root. World matrices are cached and invalidated down the hierarchy when a local value or the parent changes.
- `GameObject::setParent()` with an optional sibling index, keeping the world transform by default and refusing cycles. `Transform::setWorldMatrix()`.
- Objects are referred to across frames by `GameObjectHandle` (a generational handle; `Scene::find()` returns nullptr once the object is gone). `Scene::destroy()` is deferred to `flushDestroyed()`, so nothing is removed while the hierarchy is being walked.
- `Scene::duplicate()`: a deep copy with new UUIDs, cloning every component through `Component::clone()`.
- Components declare `ARC_COMPONENT(Class, "Name")` for their type and clone(); `GameObject::addComponent<T>()`, `getComponent<T>()` and `removeComponent()`. Types are compared by address, without RTTI.
- `MeshComponent`: one mesh of a shared `Model`. `hierarchyBounds()` for the world bounds of an object and its children.
- `Scene/SceneModule`: owns the scene, updates components, and draws every active mesh in one scene pass. Models are cached by path and shared while in use.
- `instantiateModel()`: a model's glTF node hierarchy becomes GameObjects under an object named after the model.
- `ModelData::nodes` and `Model::nodes()`: the importer now also keeps the node hierarchy with local translation, rotation and scale (node matrices are decomposed). `Model::meshBounds()` and `meshTriangleCount()`.
- `MeshRenderer::draw()` takes a list of `MeshDrawItem`s (model, mesh, world matrix) from any number of models.
- Math: `decompose()` (matrix to translation, rotation and scale, with mirrors as negative X scale) and `Quat::fromRotationMatrix()`.
- Editor: *Hierarchy* panel (selection, drag and drop to reparent or reorder, context menu, double-click to frame), *Inspector* panel (name, active flag, transform with Euler angles in degrees, component editors, Add Component), *GameObject* menu, *File > Import Model*, Delete and Ctrl+D (Cmd+D) shortcuts, and F to frame the selection in the Scene view. Imported models are selected and framed.
- Editor: the default layout is rebuilt once when `imgui.ini` predates a window, so new panels dock instead of floating.
- Unit tests for UUIDs, decomposition, the transform hierarchy, reparenting and sibling order, deferred destruction, duplication, component updates and model instantiation.
- Continuous integration (`.github/workflows/ci.yml`): pushes to the development branches and pull requests configure, build, test and package both platforms.
- The RenderDoc in-app API licence notice ships in `ThirdPartyLicenses/` of the Windows release zip.

### Changed
- The *Model* panel and `ModelViewerModule` are replaced by the scene: models are imported into it at their real size and framed by the camera, instead of being scaled to fit. The shading debug view moved to the Scene panel's *View* menu.
- `EditorCamera::update()` no longer handles F; the Scene panel frames the selection with `EditorCamera::frame()`, or calls `focusOnOrigin()` when nothing is selected.

### Removed
- `Source/`, the original Windows-only starter code that was kept for reference and was not part of the build.
- `Game/WinPixEventRuntime.dll`, which nothing loaded: PIX captures use `WinPixGpuCapturer.dll` from the PIX installation.

## [0.3.0] - 2026-10-16

Phase 2: textures, resource lifetimes, constants, scene viewport, editor camera, glTF models.

### Added

#### Textures and samplers
- Textures: `rhi::Texture` with mip levels and Sampled / RenderTarget usage, `Device::createTexture()`, the `RGBA8_UNORM_SRGB` format, texture barriers, and `copyBufferToTexture` / `copyTextureToBuffer` with one copy layout for both backends (`kTextureCopyRowAlignment`, `kTextureCopyOffsetAlignment`).
- Samplers: `rhi::Sampler` and `Device::createSampler()` with min/mag/mip filters, repeat / mirrored repeat / clamp / border addressing, border colours, anisotropic filtering and LOD clamps.
- Resource binding: `CommandList::setTexture()` and `setSampler()` bind to slots t0–t7 and s0–s3 of both shader stages.
- D3D12: a shared root signature with an SRV table and a sampler table; descriptor heaps with persistent (free-list) and per-frame transient parts; descriptor tables built per draw by copying from CPU-only heaps; a null SRV and default sampler for unbound slots.
- `RHI/TextureUtils`: `computeTextureCopyLayout`, `uploadTexture`, `readbackTexture`, and a texture round-trip self-test that Debug builds run at start-up.
- `Renderer/Image`: PNG, JPEG, TGA and BMP decoding (stb_image), box-filtered downsampling that averages sRGB images in linear space, and full mip chain generation. `Renderer/TextureLoader` creates and uploads textures from images or files.
- `findAssetPath()` looks for `Assets/` in the working directory, then next to the executable; the release zip now includes `Assets/`.
- Texture demo (`Editor/Source/Scene/TextureDemoModule`): a perspective ground plane and an address-mode quad, with a *Texture Demo* panel for the texture (UV grid, checker, colour-per-mip-level) and sampler settings.
- `Engine/Shaders/Common.hlsli` with `LinearToSrgb()`, and `Textured.hlsl`.
- stb_image's licence notice ships in `ThirdPartyLicenses/` of the release zip.
- Unit tests for mip sizes, the copy layout, texture round trips, samplers, downsampling and PNG decoding.

#### Resource lifetimes
- `Core/Handle.h`: generational handles (`Handle<Tag>`), `HandleAllocator` and `HandlePool`. A freed slot bumps its generation, so stale handles are detected instead of reaching whatever reuses the slot.
- Buffers, textures and samplers get handles (`BufferHandle`, `TextureHandle`, `SamplerHandle`); `Device::find()` returns the resource, or nullptr once it has been destroyed. Systems that do not own a resource keep its handle instead of a pointer.
- Deferred release: a buffer, texture or pipeline may be destroyed while frames in flight still use it. On D3D12 the device keeps the GPU object until the fence value of the last submission that could use it has passed (`RHI/DeferredRelease.h`); objects destroyed while a frame is being recorded wait for that frame's own fence value. Metal needs no queue, because command buffers retain the resources they use.
- `Device::resourceCounts()`: live buffers, textures and samplers, and objects awaiting release; shown live in the System Info panel.
- The device logs every buffer, texture and sampler still alive when it is destroyed, by name.
- D3D12 persistent descriptors carry a handle, so a double free or a stale copy is caught.
- Texture demo: *Recreate textures* button, which replaces every texture while frames in flight still use the old ones.
- Unit tests for handles, the deferred release queue and the resource registry.

#### Constants
- `RHI/UploadRing`: per-frame scratch memory in one Upload buffer, one region per frame in flight plus one for immediate submissions, reset when the frame slot comes round again. Starts with 64 KB of zeros that unset constant slots read.
- `CommandList::setConstants(slot, data)` (and a typed overload) for cbuffer slots b0–b2 in both shader stages: copies the data into the ring and binds it. D3D12 uses root constant buffer views; Metal binds the buffer at `[[buffer(n)]]`.
- `Engine/Shaders/Constants.hlsli` (`ViewConstants` at b0) mirrored by `Renderer/ShaderConstants.h`, with `static_assert`s on every offset against HLSL's 16-byte-row packing.
- Texture demo: the ground plane is a unit quad in world space with a camera in b0 and a per-draw transform, texture-coordinate scale/offset and mip bias in b1. New *Mip bias*, *Field of view* and *Height* controls.
- Unit tests for the upload ring.

#### Scene viewport
- Scene target: the renderer owns a RenderTarget + Sampled texture (`Renderer::sceneTexture()`, `sceneFormat()`), and `beginScenePass()` / `endScenePass()` draw into it. `Renderer::finishScene()`, called by the Application after every module's `render()`, clears it if nothing drew and moves it to ShaderRead for the UI.
- *Scene* panel (`Editor/Source/Panels/ScenePanel`): shows the scene target with `ImGui::Image`, resizes it to the panel's size in pixels (full resolution on Retina), shows the resolution and graphics API, and reports whether the mouse is over it. Docked in the centre by the default layout.
- `ImGuiModule::textureId(texture)`: an ImTextureID for any device texture. On D3D12 the texture's SRV is copied into the shader-visible heap once, cached by `TextureHandle`, and freed when the texture is gone and the last frame that drew it has finished. On Metal the ID is the `MTLTexture`.

#### Editor camera
- Depth buffers: the `D32_FLOAT` format, `TextureUsage::DepthStencil`, the `DepthWrite` resource state, and `RenderPassDesc::depth` (load/store and clear depth). D3D12 creates depth-stencil views in a DSV heap; Metal sets the pass's depth attachment.
- Pipeline depth and blend state: `PipelineDesc::depthFormat`, `depth` (test, write, `CompareOp`) and `blend` (`Opaque` or straight `AlphaBlend`). On Metal the depth test is a separate `MTLDepthStencilState`, set with the pipeline.
- Pipelines with no vertex attributes, which draw without a vertex buffer (e.g. a full-screen triangle from `SV_VertexID`).
- Shared render pass checks: a depth target must match the colour target's size, and a pipeline's colour and depth formats must match the pass it is used in.
- `Renderer/Camera`: position, yaw and pitch, field of view and near plane, view and projection matrices, and a `reversedZ` switch (on by default) with the matching `clearDepth()` and `depthCompare()`.
- The scene target gets a depth target of the same size (`Renderer::sceneDepthFormat()`); scene passes clear it with the scene camera's clear depth. `Renderer::setSceneCamera()` and `sceneViewConstants()`.
- `ViewConstants` gains `inverseViewProjection` and `cameraPosition`.
- Editor camera (`Editor/Source/Scene/EditorCamera`): fly (right drag + WASD/QE), orbit (Alt + left drag), pan (middle drag, or Alt + Shift + left drag), dolly (wheel), F to focus on the origin.
- The Scene panel takes the mouse and keyboard from the UI while the pointer is over it or a drag that started there is held. It adds an axis gizmo that turns the view to look along an axis when an end is clicked, and a *Camera* menu with field of view, near and far plane, Reversed-Z, fly speed and grid settings.
- Infinite editor grid (`Engine/Shaders/Grid.hlsl`, `Editor/Source/Scene/EditorGrid`): one full-screen triangle, a ray-plane intersection per pixel, screen-space anti-aliased lines with major lines, red X and blue Z axes, fading with distance. Depth-tested against the scene with the hit point's depth, alpha-blended, never writing depth. Drawn by `EditorOverlayModule`, the last module.
- Texture demo: a z-fighting test, two walls a small distance apart far down -Z, with distance and separation controls.
- Unit tests for the camera (orientation, view, both depth conventions, and reversed-Z separating surfaces that standard depth cannot) and for depth render passes.

#### glTF models
- `Renderer/GltfImporter`: imports `.gltf` (separate or embedded buffers and images) and `.glb` with tinygltf into `ModelData`, a GPU-free description that unit tests can check. Reads POSITION, NORMAL and TEXCOORD_0 from any component type (normalized integers included, so KHR_mesh_quantization is supported), any byte stride, and sparse accessors; 8/16/32-bit indices widened to 32 bits; triangle strips and fans converted to lists; flat normals generated when a primitive has none. The default scene's node hierarchy is flattened into instances with complete transforms. Materials (base colour factor and texture, metallic/roughness factors, alpha mode and cutoff, double-sided) and samplers are imported; only images a drawn material uses are decoded. Every accessor and index is bounds-checked, and broken parts are skipped with a warning. Files are read through SDL, so UTF-8 paths work on Windows.
- `Renderer/Model`: a model on the GPU, with one vertex buffer and one index buffer for all primitives, sRGB base colour textures with CPU-generated mips, and samplers shared between textures with equal settings.
- `Renderer/MeshRenderer` and `Engine/Shaders/Mesh.hlsl`: base colour (factor × texture) under a fixed directional light and sky/ground ambient; alpha mask and blend (blended primitives after opaque ones, unsorted); double-sided materials; mirrored instances (negative determinant) drawn with clockwise front faces. A white stand-in for materials without a texture, a magenta checker for textures that could not be loaded, and debug views of base colour, normals and texture coordinates.
- `Math/Aabb.h`: axis-aligned bounding boxes, with a transformed() that encloses the eight transformed corners.
- `Platform/FileSystem`: `readFile()`, `fileExists()` and `fileSize()`.
- Editor: a *Model* panel (`Editor/Source/Scene/ModelViewerModule`) that opens the sample model at start-up, opens others by path or by dropping a `.gltf` / `.glb` file on the window, fits the model to the view, rotates it, switches the debug view, and lists its instances and materials.
- The *Lantern* sample model (Khronos glTF sample assets, CC0) is downloaded at configure time, copied next to the editor and included in the release zip. tinygltf and nlohmann/json licence notices are included too.
- Unit tests for the importer: interleaved attributes and node transforms, flat normals, normalized texture coordinates, triangle strips, sparse accessors, out-of-bounds accessors and indices, materials, samplers and embedded images, binary files, and unsupported required extensions.

### Changed

#### Textures and samplers
- The Metal shader build passes `--msl-decoration-binding` to SPIRV-Cross, so HLSL register numbers become the Metal binding indices.
- The ImGui D3D12 backend takes its descriptors from the device's shader-visible heap instead of a heap of its own, so drawing the UI no longer switches descriptor heaps.
- `beginRenderPass()` moves its target to the RenderTarget state through the same texture state tracking as everything else; backbuffers are created with RenderTarget usage.

#### Resource lifetimes
- `Device::createBuffer/createTexture/createSampler` are no longer virtual: they validate the description, call the backend's `create*Impl()` and register the resource.
- The texture demo builds a new vertex buffer when the window is resized instead of calling `waitIdle()` and overwriting the old one.

#### Constants
- The D3D12 root signature gains three root CBVs (b0–b2) after the SRV and sampler tables.
- The texture demo draws one 4-vertex quad twice instead of building clip-space vertices on the CPU, so resizing no longer creates any buffer.

#### Scene viewport
- The texture demo renders into the scene target instead of the backbuffer; its aspect ratio follows the Scene panel. Only the UI draws to the backbuffer now.
- `EditorModule` takes the `ImGuiModule`; the dock space no longer has a transparent central node.

#### Editor camera
- The texture demo is seen through the editor camera; its ground is 40 x 40 units around the origin, just below the grid. Its *Field of view* and *Height* controls are gone, and the corner quad uses a pipeline without depth testing.
- Module order in the editor: ImGui, Editor, Texture Demo, Editor Overlays. The Editor's dock space must exist before other modules submit windows.

#### glTF models
- The texture demo has a *Visible* switch.

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

[Unreleased]: https://github.com/Anand-Ramnarain-27/arclight-engine/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/Anand-Ramnarain-27/arclight-engine/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/Anand-Ramnarain-27/arclight-engine/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Anand-Ramnarain-27/arclight-engine/releases/tag/v0.1.0
