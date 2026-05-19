# Changelog

All notable changes to AiVM are documented in this file.

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
