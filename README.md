# Arclight Engine

Arclight is a C++20 real-time rendering engine built as a learning project, alongside my Master's in Advanced Programming for AAA Games (UPC Barcelona).

The goal is to understand *why* engines are built the way they are, not just to make one that works. Every major system is planned, built and written up with the reasoning, alternatives and gotchas behind it.

## Goals

- **Windows and macOS.** A rendering hardware interface (RHI) with a D3D12 backend on Windows and a Metal backend on macOS.
- **Physically based rendering.** Following the course material.
- **Stylized rendering.** Toon/cel shading, comic-book ink and hatching, and pixel-art rendering, living alongside PBR in the same scene.
- **Best practice throughout.** A clean module architecture, explicit resource lifetimes and GPU debugging hooks from day one.

## Status

Phase 0 (foundations) is complete: CMake build for Windows and macOS, an SDL3 window and main loop, logging and asserts, frame timing with a game clock, keyboard/mouse/gamepad input, a math library with unit tests, and a dockable ImGui editor shell. There is no 3D rendering yet; the editor UI is drawn through SDL_Renderer until the engine's own renderer exists (Phase 1).

The course's original Windows-only starter code is still in `Source/` as a reference while its pieces are rebuilt cross-platform. It is not part of the CMake build and will be removed once replaced.

## Building

Requirements:
- **CMake 3.25+**
- **Windows:** Visual Studio 2022 with the *Desktop development with C++* workload
- **macOS:** Xcode 15+ on Apple silicon, plus CMake (`brew install cmake`)

Dependencies (SDL3, Dear ImGui, doctest) are downloaded automatically the first time you configure, so the first configure needs an internet connection and Git.

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

**Editor controls:** F5 play/pause, Shift+F5 stop, F6 step one frame, +/- change time scale, 0 resets it. Panels are listed under View; View > Reset Layout restores the default docking layout.

Everything generated goes into `build/`, which is not committed. The project files are generated from CMake: change `CMakeLists.txt`, not the `.sln` or `.xcodeproj`.

## Repository layout

```
CMakeLists.txt       Root build script
CMakePresets.json    Configure/build presets per platform
cmake/               Build helpers (warnings, dependencies)
Engine/Source/       ArclightEngine static library
  Core/                Platform detection, logging, asserts, timing
  Math/                Vectors, quaternions, matrices (conventions in Math/Math.h)
  Platform/            OS window, input and system info (SDL3)
  Application/         Application, main loop, modules
  UI/                  Dear ImGui integration
Editor/Source/       Arclight editor executable
  Panels/              Dockable editor panels
Tests/Source/        Unit tests (doctest)
Game/                Runtime working directory (assets, imgui.ini)
Source/              Old course starter code (reference only, not built)
```

## Third-party libraries

| Library | Use | How it's included |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | Window, events, input | Fetched by CMake (pinned release + hash) |
| [Dear ImGui](https://github.com/ocornut/imgui) (docking) | Editor UI | Fetched by CMake (pinned release tag) |
| [doctest](https://github.com/doctest/doctest) | Unit tests | Fetched by CMake (pinned release tag) |

Libraries from the old base (ImGuizmo, DirectXTex, tinygltf, WinPixEventRuntime) move into the new build as the roadmap reaches them. Each library is under its own license.
