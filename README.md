# Arclight Engine

Arclight is a C++20 real-time rendering engine built as a learning project, alongside my Master's in Advanced Programming for AAA Games (UPC Barcelona).

The goal is to understand *why* engines are built the way they are, not just to make one that works. Every major system is planned, built and written up with the reasoning, alternatives and gotchas behind it.

## Goals

- **Windows and macOS.** A rendering hardware interface (RHI) with a D3D12 backend on Windows and a Metal backend on macOS.
- **Physically based rendering.** Following the course material.
- **Stylized rendering.** Toon/cel shading, comic-book ink and hatching, and pixel-art rendering, living alongside PBR in the same scene.
- **Best practice throughout.** A clean module architecture, explicit resource lifetimes and GPU debugging hooks from day one.

## Status

Early days. The current code is the course's starter base: a Win32 window, an `Application`/`Module` update loop, input, ImGui and debug drawing. It is Windows-only for now; the cross-platform layer comes next.

## Building (Windows)

Requirements: Visual Studio 2022 (MSVC v143) with the *Desktop development with C++* workload and a Windows 10/11 SDK.

1. Open `Source/Arclight.sln`.
2. Pick `Debug|x64` or `Release|x64`.
3. Build and run (F5).

Build output goes to `Source/build/`, which is not committed.

## Repository layout

```
Game/      Runtime folder: assets and DLLs the executable needs
Source/    Engine source, solution and project files
  3rdParty/  External libraries (DirectXTex, Dear ImGui (docking), ImGuizmo, tinygltf, WinPixEventRuntime)
```

## Third-party libraries

| Library | Use |
|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) | Editor UI |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | Transform gizmos |
| [DirectXTex](https://github.com/microsoft/DirectXTex) | Texture loading |
| [tinygltf](https://github.com/syoyo/tinygltf) | glTF model loading |
| [WinPixEventRuntime](https://devblogs.microsoft.com/pix/winpixeventruntime/) | PIX GPU markers |
| DirectXTK `SimpleMath`, `Keyboard`, `Mouse`, `GamePad` | Math and input (Windows) |
| [debug-draw](https://github.com/glampert/debug-draw), [par_shapes](https://github.com/prideout/par) | Debug drawing, procedural meshes |

Each library is under its own license; see its folder.
