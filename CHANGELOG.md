# Changelog

All notable changes to Arclight are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/). While the version is 0.x, each minor version is one roadmap phase.

## [Unreleased]

Phase 1: RHI and first triangle (in progress).

### Added
- Rendering hardware interface (`Engine/Source/RHI/`): device, swapchain, command list, texture and buffer interfaces, with two frames in flight.
- Direct3D 12 backend: debug layer (messages routed to the Console panel where supported), high-performance adapter selection, flip-model swapchain with a frame-latency waitable object, tearing when VSync is off, swapchain resize, live-object report at shutdown.
- Metal backend: command buffers with semaphore-based frames in flight, `CAMetalLayer` swapchain, Metal API validation and GPU frame capture enabled in the Xcode scheme.
- Null backend for headless builds and unit tests.
- Buffers in GPU-only, upload and readback memory; buffer copies; resource-state barriers; immediate submission for load-time work; `uploadBuffer` / `readbackBuffer` helpers and a start-up round-trip self-test in Debug builds.
- System Info panel shows the graphics API, GPU, memory and driver version.
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

[Unreleased]: https://github.com/Anand-Ramnarain-27/arclight-engine/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Anand-Ramnarain-27/arclight-engine/releases/tag/v0.1.0
