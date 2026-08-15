# Changelog

All notable changes are documented here. Tag-level details and machine-readable
history live under `docs/releases/`.

## [0.1.0] - 2026-08-15

### Added

- C++23/mcpp command framework for `tool`, `field`, `box`, and `interface`.
- Declarative `star.extension/v1` Manifest discovery without Lua execution.
- Restricted Lua 5.4 runtime with inherited `ctx:call`, log, and progress APIs.
- Capability Dispatcher with stable events, errors, permissions, cycle checks,
  and exactly-one-result enforcement.
- Docker, Podman, and Fake Box backends with truthful `inspect`/`exec` support.
- argv-safe POSIX and Windows process execution.
- Unified CLI/JSON input projection and JSON Schema subset validation.
- Firmware Binwalk Tool and firmware Field examples.
- Linux, macOS, and Windows GitHub Actions test matrix.

### Fixed

- Docker and Podman no longer advertise unimplemented lifecycle capabilities.
- Extension discovery now uses a portable directory iterator sentinel accepted
  by the pinned macOS libc++ toolchain.
- Windows builds use LLVM 22.1.8 to avoid an LLVM 20.1.7 compiler crash in the
  Lua runtime module.
- Cross-platform CI accepts platform-specific recipe hashes while continuing
  to reject dependency package, version, or source drift.

[0.1.0]: https://github.com/XxingGoD/star/releases/tag/v0.1.0
