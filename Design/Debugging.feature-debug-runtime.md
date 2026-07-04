# AiVM Debugging And Profiling

Status: non-normative runtime/tooling design direction.

This document defines the runtime/tooling direction for full AiLang debugging.
It is an AiVM implementation/runtime strategy note. AiLang owns language
semantics. AiVectra owns UI/runtime integration and visual debug semantics.

The normative machine-readable debug artifact shape lives in
`../SPEC/DEBUG_ARTIFACTS.md`.

## Runtime Split

AiVM has two runtime personalities:

- `aivm`: production runtime
- `aivm-debug`: diagnostic runtime

The production runtime must stay tiny, fast, and predictable. It should expose
only production-safe diagnostics:

- stdout
- stderr
- minimal logging
- deterministic failures
- resource-limit diagnostics

The debug runtime may carry the larger machinery needed for agents and humans
to inspect behavior:

- debugger protocol
- profiler
- stack traces
- VM traces
- syscall traces
- memory diagnostics
- UI/debug capture
- replay support
- source/bytecode mapping
- machine-readable artifact bundles

Debugging capability is not optional for the project. It is optional for the
production VM binary.

## Required Debug Capabilities

`aivm-debug` should provide these capabilities as stable tooling contracts:

- `debug run`: execute with diagnostics enabled.
- `debug trace run`: emit VM step and syscall trace artifacts.
- `debug capture run`: emit deterministic run artifact bundle.
- `debug profile`: emit timing, allocation, arena, syscall, and hot-path data.
- `debug disasm`: inspect compiled bytecode by address/function range.
- `debug dns`: inspect host DNS behavior through the same host adapter path used
  by runtime network syscalls.
- stack trace emission on VM failure.
- load-failure artifact emission before VM execution when bytecode loading fails.
- structured error records with VM code, phase, function, pc, opcode, node id,
  syscall target, and detail.
- deterministic injected input for UI/agent scenarios.
- machine-readable artifacts designed for agent ingestion.

## Debugger

The debugger should support launch, attach, breakpoints, pause/continue, step,
step over, step out, and machine-readable inspection of stack, locals, current
frame, heap/node handles, current task/worker state, deterministic queue events,
and active host operations.

The debugger protocol must be deterministic at the semantic boundary. Host
timing may affect when a pause request is observed, but stepping and state
inspection must report the VM state at explicit safe points.

## Profiler

The profiler should report total runtime, per-function instruction counts,
per-opcode counts, syscall counts and wall time, allocation counts by arena,
high-water memory by arena, compaction counts, scratch-pair count/capacity,
retained node kind counts, parser/compiler memory attribution, worker/task
counts, and queue length/dispatch counts.

Profiles must be emitted as stable machine-readable files, not only human text.
Human summaries can be derived from those files.

## Agent-Targeted Artifacts

Agent debugging must not depend on screenshots or human interpretation as the
primary evidence. A debug run should write a directory that agents can parse:

```text
debug-run/
  config.toml
  diagnostics.toml
  stdout.txt
  stderr.txt
  vm_trace.toml
  syscall_trace.toml
  stack_trace.toml
  profile.toml
  memory.toml
  state_snapshots.toml
  events.toml
  ui_capture.toml
  suggestions.toml
```

Not every command must write every file. Missing files must be intentional and
documented per command/profile.

Artifacts should follow these rules:

- stable file names
- stable field names
- deterministic ordering
- no machine-local absolute paths unless the path itself is the subject under test
- bounded file sizes or explicit truncation records
- every truncation must be visible to the agent
- include enough context for an agent to propose the next command

## Agent Debug Commands

The agent-facing command surface should support:

```bash
aivm-debug explain <debug-run-dir>
aivm-debug suggest <debug-run-dir>
aivm-debug inspect stack <debug-run-dir>
aivm-debug inspect profile <debug-run-dir>
aivm-debug inspect memory <debug-run-dir>
aivm-debug inspect syscalls <debug-run-dir>
aivm-debug compare <left-debug-run-dir> <right-debug-run-dir>
```

`debug session` is the deterministic debugger control surface. It reads a
line-oriented command file and emits `debugger.toml`.

## Syscall Policy

Debug syscalls may exist when they cross the host/debugger boundary. They must
not become a general-purpose library escape hatch.

Production `aivm` should not bind debug/profile-only targets by default.
`aivm-debug` may bind them by default.

The debug/profile-only syscall set is all `sys.debug.*` targets. Contract
metadata remains available to validation and tooling, but production release
hosts must not bind those targets unless the user explicitly enters a debug
command/runtime surface.

## Beta Exit Criteria

For beta, debugging is acceptable when:

- production `aivm` has a small diagnostics surface
- `aivm-debug` can emit deterministic artifact bundles
- load failures and VM execution failures produce machine-readable artifacts
- VM failures include structured stack traces in debug mode
- syscall and VM traces are machine-readable
- agent workflow docs explain how to gather and inspect artifacts
- AiVectra has at least one semantic UI capture flow that agents can inspect
- debug/profile-only targets are not silently available through production
  release bindings
