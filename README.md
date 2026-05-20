# AiVM

Standalone repository for the AiVM runtime layer.

![AiVM](assets/AiVM_Logo.png)

For contributor setup and verification, see [CONTRIBUTING.md](CONTRIBUTING.md).

## Status

This repository owns the native C AiVM implementation. It is intentionally
independent from the AiLang compiler and AiVectra UI SDK.

Current public beta: `v0.0.1-beta.1`.

Install the public AiLangCore SDK, including `aivm`:

```bash
curl -fsSL https://ailang.codes/install.sh | sh
export PATH="$HOME/.ailang/bin:$PATH"
aivm --version
```

The native C VM lives under:

```text
native/
```

## Layout

- `native` - imported native C VM source, tests, native launcher code, and CMake build.
- `native/ailang_cli` - temporary native AiLang launcher/host adapter code.
- `.github/workflows` - CI and release workflows.

Target native layout:

```text
include/
src/
tests/
examples/
scripts/
CMakeLists.txt
CMakePresets.json
```

The native tree is intentionally under `native/` during the first import. A
later cleanup can flatten it to the repository root after AiLang is rewired to
consume this repository.

## Deliverables

- `aivm` executable for production bytecode execution.
- `aivm-debug` executable for VM diagnostics, profiling, and benchmarking.
- Embeddable native VM library.
- Embeddable native debug VM library.
- Public C headers for host integration.
- Temporary native launcher sources used to package SDK command-line tools.

The production `aivm` surface is intentionally tiny: version/help plus bytecode
execution through `aivm <program.aibc1>`. Project commands such as `build`,
`publish`, and developer workflow modes are owned by AiLang tooling and should
call into AiVM rather than expanding the production VM command surface.

## Specifications

- `Docs/Syscalls.md` defines the syscall boundary and syscall addition rules.
- `SPEC/MEMORY.md` defines the deterministic AiVM memory implementation model.
- `Docs/Production-VM-Readiness.md` tracks production hardening work.

## Versioning

The CMake project declaration in `native/CMakeLists.txt` is the base semantic
version for AiVM release automation. Release tags use `v` plus the derived
version, for example `v0.0.1-beta.1`.

## Build and Test

Build native host artifacts:

```bash
./build.sh
```

Run the standalone native unit test surface:

```bash
./test-aivm-c.sh
```

Optional host/parity tests that exercise AiLang tooling must be run explicitly
with an installed AiLang toolchain. The default AiVM build and test path does
not require an AiLang checkout.

## CI

- Pull requests and pushes build on Linux, macOS, and Windows.
- Tag pushes matching `v*` publish GitHub releases with per-platform artifacts.
