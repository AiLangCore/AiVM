# AiVM Agents

AiVM owns the native virtual machine for AiLangCore.

## Scope

- Native C VM implementation.
- AiBC program loading and execution.
- Syscall dispatch boundary.
- `aivm` production executable and embeddable C library.
- `aivm-debug` diagnostic executable and embeddable debug C library.
- Public C headers for host integration.

## Rules

- IMPORTANT: Until a major or minor release is officially released, all
  contracts, APIs, schemas, interfaces, and architectural decisions are
  considered negotiable and may change freely. Do not add backward
  compatibility layers, legacy adapters, or dual-path support unless explicitly
  requested. When changing direction, replace the old implementation completely
  and update the codebase consistently to the new contract. Patch releases are
  for bug fixes only.
- New `sys.*` targets require absolute justification. A syscall may only be
  added when it crosses a host boundary and cannot be implemented
  deterministically in AiLang or AiLang core libraries.
- Do not add syscalls for normal language or library behavior such as string
  replacement, collection operations, template rendering, parsing, validation,
  compiler policy, or compatibility adapters.
- Every syscall change must update `Docs/Syscalls.md`,
  `src/sys/aivm_syscall_contracts.c`, and
  `tests/unit/syscalls/test_syscall_contracts.c`.
- `scripts/check-syscall-contracts.sh` must pass before a syscall change is
  complete.
- Keep VM behavior deterministic.
- Keep host/syscall behavior behind explicit boundaries.
- Host/runtime changes must remain mechanical and must not introduce language,
  library, UI, package, parsing, validation, formatting, or application
  semantics. If behavior can be implemented deterministically in AiLang,
  AiVectra, or an `.aos` package/module, do not put it in AiVM.
- Do not create or continue "blob" files. Split C implementation by VM
  responsibility, and keep syscall/host adapters narrowly scoped to the host
  boundary they serve.
- Do not add AiLang compiler or AiVectra UI behavior here.
- Run verification from the AiVM repository root.
- Preserve user work and avoid destructive git operations.

## Verification

```bash
./build.sh
./test-aivm-c.sh
```

For broader host/parity checks during migration, use the sibling AiLang checkout
when required by a specific test script.
