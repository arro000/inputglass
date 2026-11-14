# Changelog

All notable changes to InputGlass are documented in this file. The project follows
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Opt-in **Background capture** button: receive keyboard and mouse Raw Input while another window has
  the focus (`RIDEV_INPUTSINK`). Off at every launch and never persisted; turning it on requires an
  in-app red confirmation that warns about capturing sensitive input. The header shows
  `BACKGROUND ON/OFF` and turns red while it is on.

### Changed

- Held-key states are kept (not cleared) when the window loses focus while background capture is on.

## [1.0.0] - 2026-07-24

### Added

- Live keyboard, mouse, and four-slot XInput diagnostics dashboard.
- CSV and readable text session logs with unique timestamped filenames.
- Raw Input and Win32 fallback comparison timeline.
- Native Windows icon, version metadata, DPI-aware manifest, and InputGlass visual identity.
- CMake and MinGW-w64 build support.
- GitHub Actions build, artifact, checksum, and tagged-release automation.

### Changed

- Buffered log flushing to reduce input measurement interference.
- XInput loading now uses an explicit Windows system directory path.
- Controller samples report changed fields without implying atomic device updates.
- Layout now has a safe minimum size, per-monitor DPI updates, and a monospaced timeline.

[1.0.0]: https://github.com/arro000/inputglass/releases/tag/v1.0.0
