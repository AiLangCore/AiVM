# AiVM

Standalone repository for the AiVM runtime layer.

## Migration Status

This repository is moving to the native C AiVM implementation.

The native C VM has been imported under:

```text
native/
```

The sibling AiLang checkout still contains the pre-split source path during
migration:

```text
../AiLang/src/AiVM.Core/native
```

The previous C# runtime project has been archived under:

```text
legacy/csharp/src/AiVM
```

## Layout

- `native` - imported native C VM source, tests, and CMake build.
- `legacy/csharp/src/AiVM` - archived legacy C# runtime project.
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

- `aivm` executable.
- Embeddable native VM library.
- Public C headers for host integration.

## Versioning

The CMake project declaration in `native/CMakeLists.txt` is the base semantic
version for AiVM release automation. Release tags use `v` plus the derived
version, for example `v0.0.1-alpha.12`.

## Build and Test

Build native host artifacts:

```bash
./build.sh
```

Run the standalone native unit test surface:

```bash
./test-aivm-c.sh
```

The full integration/parity suite still depends on AiLang tooling during this
migration and should be ported after AiVM owns the native runtime fully.

## CI

- Pull requests and pushes build on Linux, macOS, and Windows.
- Tag pushes matching `v*` publish GitHub releases with per-platform artifacts.
