# AiVM Resource Limits And Error Codes

Status: beta contract documentation.

This document describes the current AiVM resource-limit and error-code surface
for beta users, agents, and contributors. It documents runtime behavior; it
does not define AiLang language semantics.

## Runtime Profiles

AiVM has three named runtime profiles:

- `production`: normal command-line/runtime execution.
- `debug`: diagnostics, tracing, profiling, and developer inspection.
- `tooling`: compiler, parser, package restore, and SDK tooling workloads.

The selected profile is explicit in debug artifact bundles. Current beta
profiles share the same core VM capacity values; future beta hardening may tune
non-default `debug` and `tooling` limits, but profile selection must remain
visible in diagnostics and must not silently change language semantics.

## Current VM Limits

These limits are compiled into the native VM and exposed through the runtime
profile limit record.

| Limit record | Current value | Notes |
| --- | ---: | --- |
| `stack_capacity` | 20000 | Maximum VM stack values. |
| `call_frame_capacity` | 2048 | Maximum active call frames. |
| `locals_capacity` | 16384 | Maximum VM local slots. |
| `string_arena_capacity` | 524288 | Maximum VM-owned string arena bytes. |
| `bytes_arena_capacity` | 131072 | Maximum VM-owned byte arena bytes. |
| `node_capacity` | 16384 | Maximum semantic node records. |
| `node_attr_capacity` | 65536 | Maximum semantic node attributes. |
| `node_child_capacity` | 131072 | Maximum semantic node child handles. |
| `task_capacity` | 256 | Maximum completed async task records. |
| `par_value_capacity` | 1024 | Maximum retained parallel branch values. |

Additional bytecode loader limits:

| Loader limit | Current value | Notes |
| --- | ---: | --- |
| `program_section_capacity` | 32 | Maximum AiBC1 sections. |
| `program_instruction_capacity` | 16384 | Maximum AiBC1 instructions. |
| `program_constant_capacity` | 1024 | Maximum AiBC1 constants. |
| `program_string_storage_capacity` | 8192 | Maximum loaded program string bytes. |
| `program_bytes_storage_capacity` | 32768 | Maximum loaded program byte storage. |

The VM must fail deterministically when these limits are exceeded. Raising a
limit is not the default fix for compiler/parser failures; first measure
retained roots and reduce temporary structures.

## Diagnostic Visibility

`aivm-debug debug capture run <program.aibc1> --out <dir>` writes the active
profile and limit records into the debug artifact bundle. The core files are:

- `config.toml`: selected runtime profile and command configuration.
- `diagnostics.toml`: status, phase, exit code, VM error code, VM message, and
  VM detail.
- `memory.toml`: arena usage, high-water marks, pressure counters, and limit
  records.
- `profile.toml`: execution/profiling counters.

Production `aivm` keeps output concise. Full memory/profile detail belongs to
`aivm-debug`.

## Allocation Failure Behavior

When memory cannot be reclaimed or grown within the selected runtime profile,
AiVM must:

- enter VM error state
- stop execution deterministically
- report a stable VM error code and message
- include a deterministic detail string when useful
- preserve debug telemetry when running under `aivm-debug`

The VM must not silently fall back to unbounded host allocation to complete a
semantic allocation.

## Host Resource Limits

Current beta hardening direction requires named resource records for host
resources. Limits are guardrails, not the main design mechanism. Large host
resources should use bounded primitives such as handles, chunks, immutable
messages, worker handoff, or blob storage. Raising a whole-file, whole-response,
or whole-artifact limit is not the default solution.

The current whole-file filesystem calls are still present, so they enforce
profile limits immediately. Read-side handle/chunk filesystem primitives are
available for large files. The remaining filesystem direction is to add
write-side handle/chunk primitives so large outputs can also be processed in
bounded chunks.

Tracked host-resource limit records:

| Limit record | Current value | Applies to | Beta status |
| --- | ---: | --- | --- |
| `file_read_bytes` | 16777216 | Current `sys.fs.file.read`; `sys.fs.file.readChunk` | Enforced for whole-file read and read chunks. |
| `file_write_bytes` | 16777216 | Current `sys.fs.file.write`; future chunk writes | Enforced for whole-file write. |
| `network_read_bytes` | 1048576 | `sys.net.*` read paths | Defined; chunked enforcement pending. |
| `network_write_bytes` | 1048576 | `sys.net.*` write paths | Defined; chunked enforcement pending. |
| `process_count` | 32 | `sys.process.*` child processes | Defined; enforcement pending. |
| `worker_count` | 64 | worker/task execution resources | Defined; enforcement pending. |
| `ui_window_count` | 16 | host UI windows | Defined; enforcement pending. |
| `debug_artifact_bytes` | 67108864 | `aivm-debug` artifact output | Defined; enforcement pending. |
| `syscall_elapsed_ms` | 30000 | syscall execution timeout budget | Defined; enforcement pending. |

Filesystem and network APIs should move toward this shape:

```text
open resource -> handle
read/write handle with max chunk size
close/release handle
```

Limits then apply to maximum chunk size, maximum handles, total accounted bytes
where required by the selected profile, and deterministic failure behavior.

Current read-side filesystem primitives:

| Target | Behavior |
| --- | --- |
| `sys.fs.file.openRead(path)` | Opens a host file for bounded chunk reads and returns a positive handle, or `-1` when unavailable. |
| `sys.fs.file.readChunk(handle, maxBytes)` | Reads up to `maxBytes` bytes and returns an empty byte array for invalid handles or EOF. Requests above `file_read_bytes` fail with `AIVMS007`. |
| `sys.fs.file.close(handle)` | Closes a file handle and returns whether a live handle was closed. |

Until these records are fully enforced, deployment security and resource
containment come from the host operating system, user account, container, app
sandbox, CI runner, or deployment environment. AiVM is a runtime with an
explicit syscall boundary; it is not currently a general-purpose sandbox.

## VM Error Codes

The `AIVM` family reports VM execution errors.

| Code | Meaning |
| --- | --- |
| `AIVM000` | No error. |
| `AIVM001` | Unsupported or invalid opcode. |
| `AIVM002` | VM stack overflow. |
| `AIVM003` | VM stack underflow. |
| `AIVM004` | Call frame overflow. |
| `AIVM005` | Call frame underflow. |
| `AIVM006` | Local index out of range. |
| `AIVM007` | Runtime type mismatch. |
| `AIVM008` | Invalid program state. |
| `AIVM009` | VM string arena overflow. |
| `AIVM010` | Syscall dispatch failed. |
| `AIVM011` | VM memory pressure limit exceeded. |
| `AIVM999` | Unknown VM error. |

`AIVM011` details may include memory subcodes:

| Detail subcode | Meaning |
| --- | --- |
| `AIVMM001` | String arena allocation/compaction limit. |
| `AIVMM002` | Byte arena allocation limit. |
| `AIVMM003` | Node mark/compaction scratch limit. |
| `AIVMM004` | Node attribute/child compaction limit. |
| `AIVMM005` | Node arena capacity limit. |

## Program Load Error Codes

The `AIVMP` family reports AiBC1 program loading failures.

| Code | Meaning |
| --- | --- |
| `AIVMP000` | Program load completed. |
| `AIVMP001` | Program load input was null. |
| `AIVMP002` | Program bytes were truncated. |
| `AIVMP003` | Program magic was invalid. |
| `AIVMP004` | Program version or feature is unsupported. |
| `AIVMP005` | Program section exceeded byte bounds. |
| `AIVMP006` | Program section count exceeded the loader limit. |
| `AIVMP007` | Program instruction count exceeded the loader limit. |
| `AIVMP008` | Program section encoding was invalid. |
| `AIVMP009` | Program instruction opcode was invalid. |
| `AIVMP010` | Program constant count exceeded the loader limit. |
| `AIVMP011` | Program constant encoding was invalid. |
| `AIVMP012` | Program string storage exceeded the loader limit. |
| `AIVMP999` | Unknown program load status. |

## Syscall Error Codes

The `AIVMS` family reports syscall dispatch failures.

| Code | Meaning |
| --- | --- |
| `AIVMS000` | Syscall dispatch succeeded. |
| `AIVMS001` | Syscall dispatch input was invalid. |
| `AIVMS002` | Syscall dispatch result pointer was null. |
| `AIVMS003` | Syscall target was not found. |
| `AIVMS004` | Syscall contract validation failed. |
| `AIVMS005` | Syscall return type violated the contract. |
| `AIVMS006` | Syscall target is known but has no host binding. |
| `AIVMS007` | Syscall resource limit exceeded. |
| `AIVMS999` | Unknown syscall dispatch status. |

`AIVMS004` is paired with the `AIVMC` contract family.

| Code | Meaning |
| --- | --- |
| `AIVMC000` | Syscall contract validation passed. |
| `AIVMC001` | Syscall target was not found in the contract table. |
| `AIVMC002` | Syscall argument count was invalid. |
| `AIVMC003` | Syscall argument type was invalid. |
| `AIVMC004` | Syscall contract ID was not found. |
| `AIVMC999` | Unknown syscall contract validation status. |

## Tooling Error Codes

The SDK also emits tool-layer codes from the temporary native AiLang launcher
and package tooling. These are not VM execution errors, but they are stable
enough for beta scripts and agents to route failures.

| Family | Owner | Meaning |
| --- | --- | --- |
| `AILANG###` | AiLang CLI | User-facing command, init, template, and agent errors. |
| `PKG###` | AiLang package tooling | Package restore/list/tool dispatch errors. |
| `DEV###` | Development bridge/tooling | Unsupported transitional source, publish, or bridge path. |
| `RUN###` | AiLang runtime/tooling wrapper | Build, run, publish, or wrapper execution failure. |
| `VAL###` | Validation/syscall contract layer | User program or syscall argument validation failure. |
| `CAP###` | Capability warning | Publish-time warning for target capability requirements. |

Tooling codes should not be used to mask VM errors. When a VM failure is the
root cause, tool output should include the underlying `AIVM`, `AIVMP`,
`AIVMS`, or `AIVMC` code where available.

## Stability Rules

- Do not reuse an existing code for a different meaning.
- Add new codes rather than changing the meaning of existing beta codes.
- Preserve the code family prefix.
- Keep machine-readable diagnostic fields stable before changing human text.
- Update this document, syscall docs, and contract tests when adding or
  changing VM, program-load, syscall, or contract codes.
