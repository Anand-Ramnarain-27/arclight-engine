# Changelog

All notable changes to Arclight are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/). While the version is 0.x, each minor version is one roadmap phase.

## [Unreleased]

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
