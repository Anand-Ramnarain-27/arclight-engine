# Roadmap

Arclight is built in milestones. Each one is released as a minor version while the engine is on 0.x, works on both Windows (Direct3D 12) and macOS (Metal) before the next one starts, and is recorded in [CHANGELOG.md](../CHANGELOG.md).

Status: **Done** (released or merged and working on both platforms), **In progress**, **Planned**.

| Milestone | Version | Status |
|---|---|---|
| [Foundations](#foundations) | 0.1.0 | Done |
| [Rendering hardware interface](#rendering-hardware-interface) | 0.2.0 | Done |
| [Meshes, textures, camera and viewport](#meshes-textures-camera-and-viewport) | 0.3.0 | Done |
| [Engine architecture](#engine-architecture) | 0.4.0 | Done |
| [Lighting and PBR](#lighting-and-pbr) | | In progress |
| [Compute, threading and profiling](#compute-threading-and-profiling) | | Planned |
| [Advanced rendering](#advanced-rendering) | | Planned |
| [Visual effects](#visual-effects) | | Planned |
| [Animation](#animation) | | Planned |
| [Gameplay systems](#gameplay-systems) | | Planned |

Stylized rendering (toon/cel shading, ink and hatching, pixel art) is planned as a second material model alongside PBR, chosen per material, so both styles can share a scene.

## Foundations

**Done (0.1.0).**

- CMake build with presets for Visual Studio 2022 and Xcode, dependencies pinned to exact versions.
- SDL3 window, events and keyboard, mouse and gamepad input.
- Real-time and game clocks with play, pause, stop, single-step and time scale.
- Logging and asserts on `std::format` and `std::source_location`.
- Math library with written-down conventions: right-handed, +Y up, column vectors, column-major storage, radians.
- Dockable Dear ImGui editor shell with Console, Performance, Playback and System Info panels.

## Rendering hardware interface

**Done (0.2.0).**

- A thin RHI shaped like Metal (explicit render passes with load and store actions), with Direct3D 12 and Metal backends and a Null backend for headless tests.
- Device, swapchain, command lists and two frames in flight.
- Buffers in GPU-only, upload and readback memory, copies and resource-state barriers.
- Pipelines and indexed drawing.
- HLSL compiled at build time: DXIL with DXC on Windows; SPIR-V, then Metal Shading Language through SPIRV-Cross, then `.metallib` on macOS.
- In-app GPU frame capture with RenderDoc, PIX and Xcode.

## Meshes, textures, camera and viewport

**Done (0.3.0).**

- Mipmapped textures, samplers and fixed binding slots shared by both backends.
- Generational handles and fence-based deferred release, so resources can be replaced while frames are in flight.
- Per-frame constants from an upload ring buffer.
- Render-to-texture scene viewport in a dockable panel.
- Editor camera (fly, orbit, pan, dolly), reversed-Z depth with an infinite far plane, and an anti-aliased infinite grid.
- glTF 2.0 import with tinygltf, bounds-checked against malformed files.

## Engine architecture

**Done (0.4.0).**

- Scene of GameObjects and components with a transform hierarchy, and Hierarchy and Inspector panels.
- Mouse picking and transform gizmos, with a selection outline.
- Renderer structure: a render list built from the scene with sorted opaque and blended queues, and pooled GPU geometry with suballocation.
- Visibility: hierarchical frustum culling, discrete levels of detail and a skybox.
- Virtual file system with layered mount points, and native file dialogs.
- Asset pipeline: `.meta` files with stable IDs and import settings, cooked binary formats, and a reference-counted resource manager.
- Scene and prefab serialization with stable IDs, and Play mode that restores the scene on Stop.

## Lighting and PBR

**In progress.**

- Directional, point and spot lights from a structured buffer; HDR rendering with tone mapping; Lambert, Phong and Blinn-Phong with energy-conserving normalisation and Schlick Fresnel. *(Done.)*
- Cook-Torrance GGX with the glTF metallic-roughness model. *(Done.)*
- Normal maps with MikkTSpace tangents, ambient occlusion and emissive. *(Done.)*
- Image-based lighting: irradiance, prefiltered specular and a BRDF lookup table. *(Done.)*
- Block-compressed textures (BC1 to BC7) cooked offline. *(In progress.)*
- **Stylized rendering, part 1:** toon/cel lighting (ramp textures, banded diffuse, hard specular, rim light) and inverted-hull outlines.

## Compute, threading and profiling

**Planned.**

- Compute shaders and unordered-access resources on both backends.
- Job system with a thread pool, and game/render thread synchronisation.
- Profiling with Tracy and GPU timestamps.

## Advanced rendering

**Planned.**

- Depth prepass, deferred shading with a G-buffer, and tiled light culling (Forward+).
- Shadows: shadow maps, cascaded shadow maps and filtering (PCF, VSM).
- Screen-space ambient occlusion, reflection probes and planar reflections.
- Post-processing: exposure, colour grading, bloom, FXAA, and height and volumetric fog.
- Deferred decals.
- **Stylized rendering, part 2:** screen-space ink lines from depth and normal edges, hatching, halftone, and a pixel-art mode (low-resolution target, palette quantisation, ordered dithering, pixel-snapped camera).

## Visual effects

**Planned.**

- CPU and GPU particle systems with emitters, modules and over-lifetime curves, soft particles.
- Noise functions and trails.

## Animation

**Planned.**

- Keyframe animation, skeletons and GPU skinning from glTF skins.
- Morph targets.
- Animation state machines and blend trees.

## Gameplay systems

**Planned.**

- Collision queries and spatial partitioning.
- Physics with Jolt.
- AI: steering, pathfinding and navigation meshes (Recast/Detour).
- Scripting and UI (sprite batching, fonts, anchors).
- Audio with miniaudio.

## Later

Terrain, water and baked lightmaps.
