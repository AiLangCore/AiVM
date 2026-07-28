# AiVM Resource Limits And Error Codes

Status: normative AiVM runtime contract.

This document defines the current AiVM resource-limit and error-code surface for
beta users, agents, contributors, and conforming AiVM implementations. It
documents runtime behavior; it does not define AiLang language semantics.

## Runtime Profiles

AiVM has three named runtime profiles:

- `production`: normal command-line/runtime execution.
- `debug`: diagnostics, tracing, profiling, and developer inspection.
- `tooling`: compiler, parser, package restore, and SDK tooling workloads.

The selected profile is explicit in debug artifact bundles. Profile selection
must remain visible in diagnostics and must not silently change language
semantics.

## Current VM Limits

These limits are compiled into the native VM and exposed through the runtime
profile limit record.

| Limit record | Current value | Notes |
| --- | ---: | --- |
| `stack_capacity` | 20000 | Maximum VM stack values. |
| `call_frame_capacity` | 2048 | Maximum active call frames. |
| `locals_capacity` | 16384 | Maximum VM local slots. |
| `string_arena_capacity` | profile-dependent | Maximum VM-owned string arena bytes. |
| `bytes_arena_capacity` | profile-dependent | Maximum VM-owned byte arena bytes. |
| `node_capacity` | 16384 | Maximum semantic node records. |
| `node_attr_capacity` | 65536 | Maximum semantic node attributes. |
| `node_child_capacity` | 131072 | Maximum semantic node child handles. |
| `scratch_pair_capacity` | 32768 | Maximum scratch-pair records for VM-internal temporary pairs. |
| `task_capacity` | 256 | Maximum completed async task records. |
| `par_value_capacity` | 1024 | Maximum retained parallel branch values. |

Additional bytecode loader limits:

| Loader limit | Current value | Notes |
| --- | ---: | --- |
| `program_section_capacity` | 32 | Maximum AiBC1 sections. |
| `program_inline_instruction_capacity` | 32768 | Instructions retained inline without allocation. Larger validated AiBC1 instruction sections allocate exactly the declared instruction count, subject to checked-size arithmetic and host memory availability. |
| `program_constant_inline_capacity` | 1024 | AiBC1 constants stored without allocation. Larger pools use loader-owned storage sized to the encoded `u32` count. |
| `program_constant_capacity` | dynamic | Bounded by valid AiBC1 section bytes, addressable `size_t`, and successful loader allocation. |
| `program_string_storage_capacity` | 8192 | Maximum loaded program string bytes. |
| `program_bytes_storage_capacity` | 32768 | Maximum loaded program byte storage. |

The VM must fail deterministically when these limits are exceeded. Raising a
limit is not the default fix for compiler/parser failures; first measure
retained roots and reduce temporary structures.

### Profile-Specific Arena Limits

The production VM remains bounded for deployed applications. The tooling
profile has separately bounded string and byte arenas because compiler and
package workloads retain source modules and intermediate artifacts larger than
the production payload budget.

| Profile | `string_arena_capacity` | `bytes_arena_capacity` | Intended workload |
| --- | ---: | ---: | --- |
| `production` | 2097152 | 131072 | Published application execution. |
| `debug` | 2097152 | 131072 | Diagnostic execution with production-sized memory behavior. |
| `tooling` | 16777216 | 16777216 | Compiler, parser, linker, package, and SDK execution. |

The `ailang` tool host defaults to `tooling`. `AILANG_VM_PROFILE` may select a
different named profile explicitly. This changes only bounded runtime resource
limits and capability policy; it does not change AiLang semantics.

## Diagnostic Visibility

`aivm-debug debug capture run <program.aibc1> --out <dir>` writes the active
profile, syscall capability policy, and limit records into the debug artifact
bundle. The core files are:

- `config.toml`: selected runtime profile and command configuration.
- `diagnostics.toml`: status, phase, exit code, VM error code, VM message, and
  VM detail.
- `memory.toml`: arena usage, high-water marks, pressure counters, and limit
  records.
- `profile.toml`: execution/profiling counters.

The field-level machine-readable artifact contract is defined in
`SPEC/DEBUG_ARTIFACTS.md`.

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

Current beta hardening requires named resource records for host resources. Large
host resources should use bounded primitives such as handles, chunks, immutable
messages, worker handoff, or blob storage. Raising a whole-file, whole-response,
or whole-artifact limit is not the default solution.

Tracked host-resource limit records:

| Limit record | Current value | Applies to | Beta status |
| --- | ---: | --- | --- |
| `file_read_bytes` | 16777216 | `sys.fs.file.read`, `sys.fs.file.readChunk` | Enforced for whole-file read and read chunks. |
| `file_write_bytes` | 16777216 | `sys.fs.file.write`, `sys.fs.file.writeChunk` | Enforced for whole-file write and write chunks. |
| `network_read_bytes` | 1048576 | `sys.net.tcp.read`, `sys.net.tcp.readStart`, `sys.net.udp.recv` | Enforced as max read request size per call and cumulative bytes returned per VM run. |
| `network_write_bytes` | 1048576 | `sys.net.tcp.write`, `sys.net.tcp.writeStart`, `sys.net.udp.send` | Enforced as max write payload size per call and cumulative bytes written per VM run. |
| `process_count` | 32 | `sys.process.spawn` child process handles | Enforced before host process creation. |
| `worker_logical_tasks` | 4096 | Accepted logical tasks per owner VM | Checked atomically before workload acceptance. |
| `worker_logical_input_bytes` | profile byte-arena capacity | Aggregate immutable input bytes per owner VM | Checked atomically before workload acceptance. |
| `worker_active_ceiling` | profile-controlled | Maximum simultaneously active isolated invocations | Combined with discovered CPU/container and memory capacity. |
| `worker_pending_materialized` | profile-controlled | Materialized runnable/pending descriptors | Lazy materialization keeps larger accepted workloads bounded. |
| `worker_retained_results` | profile-controlled | Terminal unconsumed results | Includes results hidden behind a canonical straggler. |
| `worker_retained_result_bytes` | profile-controlled | Aggregate terminal result bytes | Reserved before dispatch. |
| `worker_intermediate_bytes` | profile-controlled | Dependency-stage intermediate bytes | Enforces stage backpressure. |
| `ui_window_count` | 16 | `sys.ui.createWindow` active windows | Enforced before host window creation. |
| `debug_artifact_bytes` | 67108864 | `aivm-debug` artifact output | Enforced across the debug artifact bundle. |
| `syscall_elapsed_ms` | 30000 | VM syscall dispatch calls | Enforced after host syscall dispatch returns. |

Filesystem and network APIs should move toward this shape:

```text
open resource -> handle
read/write handle with max chunk size
close/release handle
```

Limits then apply to maximum chunk size, maximum handles, total accounted bytes
where required by the selected profile, and deterministic failure behavior.

## Host Resource Failure Rules

- Filesystem chunk requests above `file_read_bytes` or `file_write_bytes` fail
  with `AIVMS007`.
- Network read requests above `network_read_bytes` and write payloads above
  `network_write_bytes` fail with `AIVMS007` before handle lookup or async
  operation allocation.
- When cumulative network read/write budget is exhausted, sync network syscalls
  fail with `AIVMS007`; async network operations complete as failed operations
  with a resource-limit error string.
- `sys.process.spawn` enforces `process_count` before host process creation.
- Worker workload admission checks logical task and input-byte ceilings using
  owner-visible state only. Rejection allocates no failed Task or scheduler
  record. Production/debug input is bounded by the 131072-byte profile arena;
  tooling input is bounded by the 16777216-byte tooling arena.

## Adaptive Worker Execution

The physical active-worker target is derived mechanically from the runtime
profile, discovered CPU/container allocation, and memory-safe worker capacity.
The production default targets at most 96 percent of the available logical CPU
allocation, rounded down with a minimum of one execution slot:

```text
cpu_target = max(1, floor(available_logical_cpu * 0.96))
active_target = min(cpu_target, profile_ceiling, memory_safe_capacity)
```

The 96 percent value is an operational default, not language semantics and not
a promise of literal CPU utilization. Runtime profiles may override it.
Adaptive scheduling may change performance only. It cannot change accepted
logical work, semantic resource failures, observation order, diagnostics, or
result bytes.

Background completion never restores owner-visible admission capacity. An
already accepted workload may lazily materialize and refill internal work
within its reservations. Task/result accounting is released exactly once by
deterministic owner Await/release or owner shutdown.

Result and intermediate byte capacity is reserved before dispatch. Saturated
buffers apply backpressure; AiVM never creates unbounded completion overflow.
Wall-clock watchdogs are operational aborts and cannot become canonical Task or
compiler failures.
- `sys.ui.createWindow` enforces `ui_window_count` before host window creation.
- `syscall_elapsed_ms` is a deterministic post-call failure signal; it does not
  currently interrupt a blocking host syscall while it is still running.
- Debug artifact output is accounted across files in a single `aivm-debug`
  artifact directory.

Until these records are fully enforced, deployment security and resource
containment come from the host operating system, user account, container, app
sandbox, CI runner, or deployment environment. AiVM is a runtime with an
explicit syscall boundary; it is not currently a general-purpose sandbox.

## VM Error Codes

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

| Code | Meaning |
| --- | --- |
| `AIVMP000` | Program load completed. |
| `AIVMP001` | Program load input was null. |
| `AIVMP002` | Program bytes were truncated. |
| `AIVMP003` | Program magic was invalid. |
| `AIVMP004` | Program version or feature is unsupported. |
| `AIVMP005` | Program section exceeded byte bounds. |
| `AIVMP006` | Program section count exceeded the loader limit. |
| `AIVMP007` | Program instruction count exceeded an explicitly configured loader/profile limit. The default native loader uses checked artifact-sized instruction allocation instead of a fixed semantic ceiling. |
| `AIVMP008` | Program section encoding was invalid. |
| `AIVMP009` | Program instruction opcode was invalid. |
| `AIVMP010` | Program constant count exceeded the loader limit. |
| `AIVMP011` | Program constant encoding was invalid. |
| `AIVMP012` | Program string storage exceeded the loader limit. |
| `AIVMP013` | Program storage allocation failed. |
| `AIVMP999` | Unknown program load status. |

## Blob Storage Error Codes

| Code | Meaning |
| --- | --- |
| `AIVMB000` | Blob operation completed. |
| `AIVMB001` | Blob operation input was invalid. |
| `AIVMB002` | Blob capacity or byte limit was exceeded. |
| `AIVMB003` | Blob handle was not found. |
| `AIVMB004` | Blob allocation failed. |
| `AIVMB999` | Unknown blob status. |

## Module Cache Error Codes

| Code | Meaning |
| --- | --- |
| `AIVMMOD000` | Module cache operation completed. |
| `AIVMMOD001` | Module cache input was invalid. |
| `AIVMMOD002` | Module count or estimated byte limit was exceeded. |
| `AIVMMOD003` | Module name already exists in the cache. |
| `AIVMMOD004` | Module name was not found in the cache. |
| `AIVMMOD999` | Unknown module cache status. |

## Syscall Error Codes

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
| `AIVMS008` | Syscall capability is denied by runtime policy. |
| `AIVMS999` | Unknown syscall dispatch status. |

`AIVMS004` is paired with the `AIVMC` contract family.

| Code | Meaning |
| --- | --- |
| `AIVMC000` | Syscall contract validation passed. |
| `AIVMC001` | Syscall target was not found in the contract table. |
| `AIVMC002` | Syscall argument count did not match the contract. |
| `AIVMC003` | Syscall argument type did not match the contract. |
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
root cause, tool output should include the underlying `AIVM`, `AIVMP`, `AIVMS`,
or `AIVMC` code where available.

## Stability Rules

- Do not reuse an existing code for a different meaning.
- Add new codes rather than changing the meaning of existing beta codes.
- Preserve the code family prefix.
- Keep machine-readable diagnostic fields stable before changing human text.
- Update this document, syscall docs, and contract tests when adding or changing
  VM, program-load, syscall, or contract codes.
