# Arclight Engine

Arclight is a C++20 real-time rendering engine built as a learning project, alongside my Master's in Advanced Programming for AAA Games (UPC Barcelona).

The goal is to understand *why* engines are built the way they are, not just to make one that works. Every major system is planned, built and written up with the reasoning, alternatives and gotchas behind it.

## Goals

- **Windows and macOS.** A rendering hardware interface (RHI) with a D3D12 backend on Windows and a Metal backend on macOS.
- **Physically based rendering.** Following the course material.
- **Stylized rendering.** Toon/cel shading, comic-book ink and hatching, and pixel-art rendering, living alongside PBR in the same scene.
- **Best practice throughout.** A clean module architecture, explicit resource lifetimes and GPU debugging hooks from day one.

## Status

Phase 0 (foundations): CMake build for Windows and macOS, an SDL3 window and main loop, logging and asserts, frame timing with a game clock, and keyboard/mouse/gamepad input. No rendering yet.

The course's original Windows-only starter code is still in `Source/` as a reference while its pieces are rebuilt cross-platform. It is not part of the CMake build and will be removed once replaced.

## Building

Requirements:
- **CMake 3.25+**
- **Windows:** Visual Studio 2022 with the *Desktop development with C++* workload
- **macOS:** Xcode 15+ on Apple silicon, plus CMake (`brew install cmake`)

Dependencies (SDL3) are downloaded automatically the first time you configure, so the first configure needs an internet connection.

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

**Editor controls:** F5 play/pause, Shift+F5 stop, F6 step one frame, +/- change time scale, 0 resets it. The window title shows FPS and the game clock.

Everything generated goes into `build/`, which is not committed. The project files are generated from CMake: change `CMakeLists.txt`, not the `.sln` or `.xcodeproj`.

## Repository layout

```
CMakeLists.txt       Root build script
CMakePresets.json    Configure/build presets per platform
cmake/               Build helpers (warnings, dependencies)
Engine/Source/       ArclightEngine static library
  Core/                Platform detection, logging, asserts, timing
  Platform/            OS window and input (SDL3)
  Application/         Application, main loop, modules
Editor/Source/       Arclight editor executable and editor modules
Game/                Runtime working directory (assets)
Source/              Old course starter code (reference only, not built)
```

## Third-party libraries

| Library | Use | How it's included |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | Window, events, input | Fetched by CMake (pinned release + hash) |

Libraries from the old base (Dear ImGui, ImGuizmo, DirectXTex, tinygltf, WinPixEventRuntime, DirectXTK) move into the new build as the roadmap reaches them. Each library is under its own license.
