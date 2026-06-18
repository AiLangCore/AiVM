# AiVM Native Core

## Purpose

`src` contains the active native C implementation of the deterministic AiVM
core and its native test/build surface.

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

`aivm` is the standalone production VM executable. It intentionally keeps a
small command surface: version/help plus bytecode execution through `aivm
<program.aibc1>`.

`aivm-debug` is the standalone diagnostic VM executable. It is built from the
debug VM library with debug symbols and no optimization, and is the place for VM
debugging, profiling, and benchmarking commands. Because this binary is already
the debug runtime, `aivm-debug <program.aibc1>` is the debug run path.

`aivm_parity_cli` is provided as an initial harness utility to compare two text outputs using deterministic normalization (CRLF/LF normalization and trailing newline trimming).

`aivm_runtime.h` provides host-bridge execution APIs:

- `aivm_execute_program(...)`
- `aivm_execute_program_with_syscalls(...)`
- `aivm_execute_program_with_syscalls_and_argv(...)`

`aivm_runtime.h` also provides host adapter helpers for deterministic event queue integration:

- `aivm_runtime_host_enqueue_event(...)`
- `aivm_runtime_host_drain_events(...)`

Adapter contract:

- VM semantic state mutation remains owner-thread only.
- Worker/host threads may produce events, but they must enqueue through host adapter callbacks.
- Drain step is explicit and bounded (`max_events`) to keep deterministic sequencing at host boundary.
- Adapter failures map to explicit statuses:
  - `AIVM_RUNTIME_HOST_EVENT_OK`
  - `AIVM_RUNTIME_HOST_EVENT_INVALID`
  - `AIVM_RUNTIME_HOST_EVENT_REJECTED`

Local host integration shape:

1. Provide `AivmRuntimeHostAdapter` with `enqueue` and `drain` callbacks plus host context.
2. Use `aivm_execute_program_with_syscalls*` to run VM steps with syscall bindings.
3. Route external events into `aivm_runtime_host_enqueue_event(...)`.
4. On owner-thread loop, call `aivm_runtime_host_drain_events(...)` and apply drained events in deterministic order.

`aivm_syscall_contracts.h` provides deterministic typed syscall-contract validation scaffolding.
`aivm_c_api.h` provides a C-ABI-friendly execution entrypoint for host integration.

## Build and Test

From repository root:

```bash
./test-aivm-c.sh
```

Optional environment variables:

- `AIVM_CMAKE_PRESET`: override CMake configure preset.
- `AIVM_CTEST_LABEL`: override CTest label filter.

CTest labels are available after configuring/building the native tree. Useful groups include:

- `unit`
- `integration`
- `vm`
- `host`
- `memory`
- `bytecode`
- `syscalls`
- `stdlib`
- `parity`
- `wasm`

Example:

```bash
ctest --test-dir .tmp/aivm-c-build-native -L host --output-on-failure
```

Preset-based workflows are also available via `src/CMakePresets.json`.

Examples:

```bash
cmake --preset aivm-native-unix -S src
cmake --build .tmp/aivm-c-build-native
ctest --test-dir .tmp/aivm-c-build-native -L unit --output-on-failure
```

Host integration and parity tests that depend on AiLang tooling are disabled by
default in this standalone repository. Configure with `AIVM_BUILD_HOST_TESTS=ON`
only when the AiLang host tooling path is available.
