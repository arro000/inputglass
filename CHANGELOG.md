# Changelog

All notable changes to InputGlass are documented in this file. The project follows
[Semantic Versioning](https://semver.org/).

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
