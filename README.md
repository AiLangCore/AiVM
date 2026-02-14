# AiVM

Standalone repository for the AiVM runtime layer.

## Layout

- `src/AiVM` - VM runtime project (`AiVM.Core.csproj`).
- `.github/workflows` - CI and release workflows.

## CI

- Pull requests and pushes build on Linux, macOS, and Windows.
- Tag pushes matching `v*` publish GitHub releases with per-platform artifacts.
