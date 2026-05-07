# Changelog

All notable changes to AiVM are documented in this file.

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
