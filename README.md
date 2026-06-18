# AiVM

Standalone repository for the AiVM runtime layer.

![AiVM](assets/AiVM_Logo.png)

For contributor setup and verification, see [CONTRIBUTING.md](CONTRIBUTING.md).

## Status

This repository owns the native C AiVM implementation. It is intentionally
independent from the AiLang compiler and AiVectra UI SDK.

Current public beta: `v0.0.1-beta.2`.

Install the public AiLangCore SDK, including `aivm`:

```bash
curl -fsSL https://ailang.codes/install.sh | sh
export PATH="$HOME/.ailang/bin:$PATH"
aivm --version
```

Branch status: `develop` is the public default branch while the native C VM is
being hardened for beta. Release tags and GitHub prereleases are the public
artifact source; `main` is not the current integration branch during this beta
cycle.

The native C VM lives under:

```text
src/
```

## Layout

- `src` - imported native C VM source, tests, native launcher code, and CMake build.
- `src/ailang_cli` - temporary native AiLang launcher/host adapter code.
- `.github/workflows` - CI and release workflows.

Source layout:

```text
src/
src/include/
src/tests/
src/examples/
scripts/
src/CMakeLists.txt
src/CMakePresets.json
```

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
- `Docs/Resource-Limits-And-Errors.md` defines beta resource limits and
  error-code families.
- `Docs/Native-Test-Infrastructure.md` defines the CTest native test layout.
- `SPEC/MEMORY.md` defines the deterministic AiVM memory implementation model.
- `Docs/Production-VM-Readiness.md` tracks production hardening work.
- [AiLangCore roadmap](https://ailang.codes/docs/roadmap.html) tracks the
  Alpha -> Beta -> RC -> 1.0 direction across AiLang, AiVM, and AiVectra.

## Versioning

The CMake project declaration in `src/CMakeLists.txt` is the base semantic
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

Focused native CTest labels are available for implementation work:

```bash
ctest --test-dir .tmp/aivm-c-build-native -L vm --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L memory --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L bytecode --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L syscalls --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L stdlib --output-on-failure
```

## CI

- Pull requests and pushes build on Linux, macOS, and Windows.
- Tag pushes matching `v*` publish GitHub releases with per-platform artifacts.
