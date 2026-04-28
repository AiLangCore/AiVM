# AiVM.Core

Execution-host layer for AiLang VM mode.

## Responsibilities

- AiBC1 VM model/load/run (`VmProgram*`, `VmEngine`, `VmRunner`).
- Host syscall adapters (`VmSyscalls`, dispatchers, host wrappers).
- Bundle publish/load helpers.
- Deterministic runtime error surfaces (`VmRuntimeException`).

## Public Contracts Used by Other Layers

- `SyscallContracts`
- `VmRunner` / `VmEngine`
- `VmProgramLoader`
- `VmPublishArtifacts`

## Design Rule

`AiVM.Core` should expose execution primitives and syscall boundaries.
Language semantics remain in `AiLang.Core` and AiLang `.aos` sources.
