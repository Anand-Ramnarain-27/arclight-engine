# Arclight Engine

Arclight is a C++20 real-time rendering engine built as a learning project, alongside my Master's in Advanced Programming for AAA Games (UPC Barcelona).

The goal is to understand *why* engines are built the way they are, not just to make one that works. Every major system is planned, built and written up with the reasoning, alternatives and gotchas behind it.

## Goals

- **Windows and macOS.** A rendering hardware interface (RHI) with a D3D12 backend on Windows and a Metal backend on macOS.
- **Physically based rendering.** Following the course material.
- **Stylized rendering.** Toon/cel shading, comic-book ink and hatching, and pixel-art rendering, living alongside PBR in the same scene.
- **Best practice throughout.** A clean module architecture, explicit resource lifetimes and GPU debugging hooks from day one.

## Status

Phase 0 (foundations) is complete: CMake build for Windows and macOS, an SDL3 window and main loop, logging and asserts, frame timing with a game clock, keyboard/mouse/gamepad input, a math library with unit tests, and a dockable ImGui editor shell.

Phase 1 (RHI + first triangle) is complete: an RHI with a D3D12 backend on Windows and a Metal backend on macOS (device, swapchain, command lists, frames in flight), GPU-only/upload/readback buffers with barriers, pipelines, indexed draws, HLSL shaders compiled at build time (DXIL on Windows; SPIR-V → Metal Shading Language → `.metallib` on macOS), and in-app GPU frame capture with RenderDoc, PIX or Xcode.

Phase 2 (meshes, textures, camera, viewport) is in progress. Done so far:
- **Textures and samplers.** Mipmapped 2D textures (RGBA8, sRGB or linear), buffer↔texture copies with one copy layout for both backends, texture barriers, and samplers with filtering, addressing, border colours, anisotropy and LOD clamps. Debug builds run a texture upload/readback round-trip self-test at start-up.
- **Resource binding.** `setTexture(slot, …)` and `setSampler(slot, …)` bind to fixed slots t0–t7 and s0–s3 in every shader. On D3D12 this is one shared root signature with descriptor tables built per draw in a shader-visible heap that ImGui shares; on Metal the slots are the `[[texture(n)]]` / `[[sampler(n)]]` indices, kept equal to the HLSL registers by the shader build.
- **Image loading.** PNG, JPEG, TGA and BMP through stb_image, with sRGB-correct mip generation on the CPU.
- **Texture demo.** A ground plane receding to the horizon and an address-mode quad, with a *Texture Demo* panel to switch textures and sampler settings, including a texture that colours each mip level differently.
- **Resource lifetimes.** Buffers, textures and pipelines can be destroyed while the GPU still uses them: D3D12 keeps them in a fence-tagged deferred release queue, and Metal's command buffers retain them. Resources have generational handles (`Device::find()` returns nullptr once a resource is gone), leaks are reported by name at shutdown, and System Info shows live resource counts.

Prebuilt Windows and macOS zips are on the [Releases](https://github.com/Anand-Ramnarain-27/arclight-engine/releases) page. Changes per version are in [CHANGELOG.md](CHANGELOG.md).

The course's original Windows-only starter code is still in `Source/` as a reference while its pieces are rebuilt cross-platform. It is not part of the CMake build and will be removed once replaced.

## Building

Requirements:
- **CMake 3.25+**
- **Windows:** Visual Studio 2022 with the *Desktop development with C++* workload
- **macOS:** Xcode 15+ on Apple silicon, CMake (`brew install cmake`), and the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#mac), which provides the DXC shader compiler (Microsoft publishes no macOS build). Install it to the default `~/VulkanSDK` location, or configure from a terminal where `VULKAN_SDK` is set. If CMake reports that the Metal compiler is missing, run `xcodebuild -downloadComponent MetalToolchain`.

Dependencies (SDL3, Dear ImGui, doctest, and the shader tools DXC on Windows and SPIRV-Cross on macOS) are downloaded automatically the first time you configure, so the first configure needs an internet connection and Git.

**Windows**
```
cmake --preset windows-vs2022
```
Then open `build/windows-vs2022/Arclight.sln`, and build and run (F5). Or from the command line: `cmake --build --preset windows-debug`.

**macOS**
```
cmake --preset macos-xcode
```
Then open `build/macos-xcode/Arclight.xcodeproj`, pick the `Arclight` scheme and run. Or from the command line: `cmake --build --preset macos-debug`.

**Tests:** build and run the `ArclightTests` target, or `ctest --preset windows-debug` / `ctest --preset macos-debug` after building. Set `-DARCLIGHT_BUILD_TESTS=OFF` to skip them.

**Release build and zip:** one command configures, builds, runs the tests and packages the editor:
```
cmake --workflow --preset windows-release
cmake --workflow --preset macos-release
```
The zip goes to `build/packages/` (for example `Arclight-0.2.0-windows-x64.zip`). The Windows Release build has no console window; log output is in the editor's Console panel. Pushing a `v*` tag runs the same workflow on GitHub Actions for both platforms and creates a draft release; see `.github/workflows/release.yml`.

**GPU debugging:** Debug builds enable the D3D12 debug layer (needs the Windows *Graphics Tools* optional feature); on Windows 11 its messages appear in the Console panel. On macOS the generated Xcode scheme enables Metal API validation and GPU frame capture.

**GPU captures:** *Debug → Capture GPU Frame* (F11) captures the next frame with whichever tool is attached:
- **RenderDoc** (Windows): launch the editor from RenderDoc; the capture appears in RenderDoc's window.
- **PIX** (Windows): start the editor with `--pix` (Visual Studio: project *Properties → Debugging → Command Arguments*). Captures are saved to `Game/Captures/*.wpix`; open them in PIX. Or launch it from PIX directly.
- **Xcode** (macOS): run from Xcode and use the menu item, or Xcode's own camera button in the debug bar.

Passes, resources and pipelines carry debug names, so captures show "Texture Demo", "Ground", "ImGui", "Checker" and so on instead of anonymous objects.

**Editor controls:** F5 play/pause, Shift+F5 stop, F6 step one frame, +/- change time scale, 0 resets it, F11 captures a GPU frame. Panels are listed under View; View > Reset Layout restores the default docking layout.

Everything generated goes into `build/`, which is not committed. The project files are generated from CMake: change `CMakeLists.txt`, not the `.sln` or `.xcodeproj`.

## Repository layout

```
CMakeLists.txt       Root build script
CMakePresets.json    Configure/build presets per platform
cmake/               Build helpers (warnings, dependencies, packaging)
.github/workflows/   Release workflow (GitHub Actions)
docs/releases/       Release notes, one file per tag
Engine/Source/       ArclightEngine static library
  Core/                Platform detection, logging, asserts, timing
  Math/                Vectors, quaternions, matrices (conventions in Math/Math.h)
  Platform/            OS window, input and system info (SDL3)
  RHI/                 Rendering hardware interface
    D3D12/               Direct3D 12 backend (Windows)
    Metal/               Metal backend (macOS, Objective-C++)
    Null/                No-GPU backend (headless builds and unit tests)
  Renderer/            Device + swapchain ownership, frame bracketing
  Application/         Application, main loop, modules
  UI/                  Dear ImGui integration
Engine/Shaders/      HLSL shaders, compiled at build time into bin/<Config>/Shaders/
Editor/Source/       Arclight editor executable
  Panels/              Dockable editor panels
  Scene/               Placeholder scene (the triangle) until the scene viewport exists
Tests/Source/        Unit tests (doctest)
Game/                Runtime working directory (imgui.ini)
  Assets/              Files the editor loads; shipped next to the executable in the release zip
Source/              Old course starter code (reference only, not built)
```

## Third-party libraries

| Library | Use | How it's included |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | Window, events, input | Fetched by CMake (pinned release + hash) |
| [Dear ImGui](https://github.com/ocornut/imgui) (docking) | Editor UI | Fetched by CMake (pinned release tag) |
| [doctest](https://github.com/doctest/doctest) | Unit tests | Fetched by CMake (pinned release tag) |
| [DXC](https://github.com/microsoft/DirectXShaderCompiler) | HLSL compiler (build tool) | Windows: fetched by CMake (pinned release + hash). macOS: from the Vulkan SDK |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | SPIR-V → Metal Shading Language (build tool, macOS) | Fetched by CMake (pinned release tag) |
| [stb_image](https://github.com/nothings/stb) | PNG/JPEG/TGA/BMP decoding | Fetched by CMake (pinned commit + hash) |
| [RenderDoc in-app API](https://github.com/baldurk/renderdoc) | Frame capture trigger (header only, Windows) | Fetched by CMake (pinned release + hash) |

Libraries from the old base (ImGuizmo, DirectXTex, tinygltf, WinPixEventRuntime) move into the new build as the roadmap reaches them. Each library is under its own license.

## License

Arclight's own code (`Engine/`, `Editor/`, `Tests/`, `cmake/` and the build files) and its assets (`Game/Assets/`) are released under the [MIT License](LICENSE).

`Source/` contains the course's starter code. It is kept as a reference only, is not part of the build, and is **not** covered by the MIT License.
