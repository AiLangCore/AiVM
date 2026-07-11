# Changelog

All notable changes to AiVM are documented in this file.

## [0.0.1-rc.4] - 2026-07-10

### Changed

- Restored the release channel after the incorrectly labeled `v0.0.1-beta.4`
  prerelease. This release continues the existing RC line from `v0.0.1-rc.3`.

### Added

- Added the `STR_SCALAR_LENGTH` intrinsic opcode for deterministic UTF-8 scalar
  counting, with unit and performance coverage.
- Added a normative syscall inventory to `SPEC/SYSCALLS.md`.

### Fixed

- Updated syscall-contract verification to enforce the normative spec rather
  than the retired compatibility pointer under `Docs/`.

## [0.0.1-beta.4] - 2026-07-10

### Added

- Added the `STR_SCALAR_LENGTH` intrinsic opcode for deterministic UTF-8 scalar
  counting, with unit and performance coverage.
- Added a normative syscall inventory to `SPEC/SYSCALLS.md`.

### Changed

- Updated syscall-contract verification to enforce the normative spec rather
  than the retired compatibility pointer under `Docs/`.

### Notes

- This beta verifies the native C runtime through the full CTest release gate,
  including syscall-contract, debug-artifact, and package-dispatch coverage.

## [0.0.1-beta.2] - 2026-05-27

### Added

- Added VM scratch-pair values and pair access opcodes for bounded temporary
  compiler/parser state.
- Added scratch-pair debug telemetry in `diagnostics.toml`,
  `state_snapshots.toml`, and `memory.toml`.
- Added the machine-readable debug artifact field contract in
  `SPEC/DEBUG_ARTIFACTS.md`.
- Added broader memory audit workloads for process, async, and parallel
  cleanup paths.

### Changed

- Moved deterministic text, byte, and crypto utility behavior out of host
  syscall contracts and into deterministic VM/library surfaces.
- Hardened resource-limit enforcement for filesystem, network, process,
  worker, UI, syscall elapsed-time, and debug artifact budgets.
- Added deterministic safe-point compaction at runtime phase boundaries,
  task handoff points, and allocation-pressure return boundaries.

### Notes

- This is a beta runtime release. Pre-1.0 contracts may still change, but the
  current runtime/debug artifact surface is documented and regression-gated.

## [0.0.1-beta.1] - 2026-05-19

### Changed

- Promoted the native C runtime line to the first beta release.
- Renamed the active AiLang bootstrap CLI source from `airun.c` to `ailang.c`.
- Updated native integration tests and parity command fixtures to use
  `tools/ailang`.
- Kept `aivm` as the production runtime and `aivm-debug` as the diagnostic
  runtime.

### Notes

- This is a beta runtime release. Pre-1.0 contracts may still change, but the
  public command surface is now `ailang` plus `aivm`.

## [0.0.1-alpha.11] - 2026-04-29

### Added

- Native C `aivm` release artifacts for Linux, macOS, and Windows.
- Native C `aivm-debug` diagnostic runtime artifacts for VM debugging, profiling, and benchmarking.
- Static embeddable `aivm_core` library and public C headers in release packages.
- Static embeddable `aivm_core_debug` library in release packages.
- Cross-platform CI for native build and unit tests.
- Bytecode execution support for deterministic VM operations, syscall dispatch, remote transport primitives, memory tests, and parity utilities.

### Notes

- This is an alpha runtime release. APIs and bytecode details may change before `1.0`.
- AiLang source compilation and AiVectra UI tooling are released from their own repositories.
