# AiVM.C

## Purpose

`AiVM.C` initializes a clean C rewrite scaffold of the AiVM deterministic core. This branch is structural scaffolding only and is not a feature-complete VM port.

## Deterministic VM Goal

The VM core is a pure state transition engine:

- deterministic instruction dispatch
- explicit VM state container
- no hidden side effects
- no global mutable state
- no time, randomness, threads, or OS calls in VM core

## Host Separation

The VM does not implement host behavior directly. Syscalls are external and invoked through a typed handler function pointer. This keeps the host mechanical and preserves syscall boundary clarity.

## Why C

C provides a thin, portable, embeddable runtime foundation:

- straightforward embedding across host environments
- no managed runtime dependency in the VM core
- explicit control over memory ownership and state flow

## Semantics Authority

AiLang semantics remain governed by the AiLang specification (`SPEC/IL.md`, `SPEC/EVAL.md`, `SPEC/VALIDATION.md`).

This scaffold does not introduce new language semantics or runtime behavior.

## Utility

`aivm_parity_cli` is provided as an initial harness utility to compare two text outputs using deterministic normalization (CRLF/LF normalization and trailing newline trimming).

`aivm_runtime.h` provides a minimal host-bridge execution API (`aivm_execute_program`) for future CLI/runtime integration phases.
`aivm_syscall_contracts.h` provides deterministic typed syscall-contract validation scaffolding.
`aivm_c_api.h` provides a C-ABI-friendly execution entrypoint for host integration.

## Build and Test

From repository root:

```bash
./scripts/test-aivm-c.sh
```

Optional environment variables:

- `AIVM_C_BUILD_DIR`: override CMake build directory (default `.tmp/aivm-c-build`)
- `AIVM_PARITY_REPORT`: override parity manifest report path
- `AIVM_BUILD_SHARED=1`: enable shared-library build in the test flow

Bridge smoke (shared loader path):

```bash
./scripts/test-aivm-c-bridge.sh
```

For normalized output comparison in dual-run workflows:

```bash
./scripts/aivm-parity-compare.sh <left-output-file> <right-output-file>
```

For command-driven dual-run comparison:

```bash
./scripts/aivm-dualrun-parity.sh "<left-command>" "<right-command>"
```
