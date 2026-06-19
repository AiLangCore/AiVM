# AiVM Debugging And Profiling

This document defines the runtime/tooling direction for full AiLang debugging.
It is an AiVM implementation/runtime strategy spec. AiLang owns language
semantics. AiVectra owns UI/runtime integration and visual debug semantics.

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

Before beta, `aivm-debug` should provide these capabilities as stable tooling
contracts:

- `debug run`: execute with diagnostics enabled.
- `debug trace run`: emit VM step and syscall trace artifacts.
- `debug capture run`: emit deterministic run artifact bundle. Initial native
  support exists for `.aibc1` programs:
  `aivm-debug debug capture run <program.aibc1> --out <debug-run-dir>`.
  Capture accepts `--profile production|debug|tooling`; this stamps the
  selected runtime profile and limit record into artifacts without changing the
  stripped production `aivm` command surface.
- `debug profile`: emit timing, allocation, arena, syscall, and hot-path data.
  Initial profile data is written into debug capture bundles as
  `profile.toml` with elapsed runtime, executed instruction count, per-opcode
  counts, syscall count, total syscall elapsed time, and per-syscall-target
  counts/timing.
- `debug disasm`: inspect compiled bytecode by address/function range.
- `debug dns`: inspect host DNS behavior through the same host adapter path used
  by runtime network syscalls.
- stack trace emission on VM failure.
- load-failure artifact emission before VM execution when bytecode loading
  fails.
- structured error records with VM code, phase, function, pc, opcode, node id,
  syscall target, and detail.
- deterministic injected input for UI/agent scenarios.
- machine-readable artifacts designed for agent ingestion.

## Debugger

The debugger should support:

- launch program
- attach to a paused debug run
- break by function
- break by bytecode pc
- break by node id when source mapping is available
- continue
- pause
- step instruction
- step over call
- step out of function
- inspect stack
- inspect locals
- inspect current frame
- inspect heap/node handles
- inspect current task/worker state
- list pending deterministic queue events
- list active host operations

The debugger protocol must be deterministic at the semantic boundary. Host
timing may affect when a pause request is observed, but stepping and state
inspection must report the VM state at explicit safe points.

## Profiler

The profiler should report:

- total runtime
- per-function instruction counts
- per-opcode counts
- syscall counts and wall time
- allocation counts by arena
- high-water memory by arena
- compaction counts
- scratch-pair count and capacity
- retained node kind counts
- parser/compiler memory attribution when running tooling workloads
- worker/task counts
- queue length and dispatch counts

Profiles must be emitted as stable machine-readable files, not only human text.
Human summaries can be derived from those files.

## Stack Traces

Every VM failure in `aivm-debug` should include a structured stack trace:

- frame index
- function name
- bytecode pc
- source node id when known
- opcode
- call target when applicable
- selected locals summary

Production `aivm` should keep concise deterministic errors. It may include a
compact stack summary if the cost is low, but full stack/locals inspection
belongs to `aivm-debug`.

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
- no machine-local absolute paths unless the path itself is the subject under
  test
- bounded file sizes or explicit truncation records
- every truncation must be visible to the agent
- include enough context for an agent to propose the next command

The field-level debug artifact contract is defined in
`SPEC/DEBUG_ARTIFACTS.md`.

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

Initial native support exists for:

- `aivm-debug debug session <program.aibc1> --commands <file> --out <dir>`
- `aivm-debug explain <debug-run-dir>`
- `aivm-debug inspect stack <debug-run-dir>`
- `aivm-debug inspect memory <debug-run-dir>`
- `aivm-debug inspect profile <debug-run-dir>`
- `aivm-debug inspect syscalls <debug-run-dir>`
- `aivm-debug suggest <debug-run-dir>`
- `aivm-debug compare <left-debug-run-dir> <right-debug-run-dir>`

`explain` summarizes status, exit code, VM error code/message/detail, current
bytecode pc, current opcode, and memory counts from the debug-run artifact
files. The diagnostics include a `phase` field so agents can distinguish
`load` failures from `execute` failures. The `inspect` commands expose narrower
summaries for agent workflows that already know which diagnostic area they need;
`inspect profile` includes elapsed runtime, executed instruction count,
per-opcode counts, syscall count, total syscall elapsed time, and
per-syscall-target counts/timing. `inspect syscalls` reads the same per-target
summary from `syscall_trace.toml`. `suggest` emits a concise next-action list
from the diagnostic status and phase. `compare` prints changed status, phase,
error, bytecode, profile, and memory fields between two captures.

These commands should print concise deterministic summaries and optionally emit
TOML/JSON for higher-level agent tools.

`debug session` is the deterministic debugger control surface. It reads a
line-oriented command file and emits `debugger.toml`. The supported beta command
set is:

```text
break pc <bytecode-pc>
map function <name> <bytecode-pc>
map node <source-node-id> <bytecode-pc>
break function <name>
break node <source-node-id>
continue [max-steps]
pause
step
step over
step out
inspect stack
inspect locals
inspect frame
inspect tasks
inspect queue
inspect heap
inspect host-ops
```

The command file format is intentionally simple so agents can generate and
replay sessions without depending on terminal control. `continue` is bounded;
omitting `max-steps` uses the runtime default session ceiling. Function and
node breakpoints use debugger-owned source mappings registered by `map`
commands; compilers and higher-level tools can replace those commands with
source-map derived registrations when source maps are emitted.

## Return-To-Production-Readiness Checklist

Use this checklist to decide when the current debug-tooling slice is complete
enough to return focus to `Docs/Production-VM-Readiness.md`.

Required before switching back:

- [x] Create `SPEC/DEBUGGING.md` as the canonical debug/profiling direction.
- [x] Keep full debug/profiling machinery in `aivm-debug`, not production
  `aivm`.
- [x] Add `aivm-debug debug capture run <program.aibc1> --out <dir>`.
- [x] Emit deterministic debug-run bundle files:
  - `config.toml`
  - `diagnostics.toml`
  - `stdout.txt`
  - `stderr.txt`
  - `vm_trace.toml`
  - `syscall_trace.toml`
  - `stack_trace.toml`
  - `profile.toml`
  - `memory.toml`
  - `suggestions.toml`
- [x] Emit bundles for VM execution failures.
- [x] Emit bundles for bytecode load failures before VM execution.
- [x] Add phase-aware diagnostics with `phase = "load"` and
  `phase = "execute"`.
- [x] Add structured VM failure stack data with current pc/opcode.
- [x] Add structured load-failure data with program error code/message/offset.
- [x] Add `aivm-debug explain <debug-run-dir>`.
- [x] Add `aivm-debug suggest <debug-run-dir>`.
- [x] Add `aivm-debug compare <left-debug-run-dir> <right-debug-run-dir>`.
- [x] Add `aivm-debug inspect stack <debug-run-dir>`.
- [x] Add `aivm-debug inspect memory <debug-run-dir>`.
- [x] Add `aivm-debug inspect profile <debug-run-dir>`.
- [x] Add `aivm-debug inspect syscalls <debug-run-dir>`.
- [x] Add deterministic debugger controls: break by pc/function/source node,
  step, step over, step out, bounded continue, pause, and inspect while paused.
- [x] Add `aivm-debug debug session <program.aibc1> --commands <file> --out
  <dir>` with machine-readable `debugger.toml` artifacts.
- [x] Add heap/node-handle and host-operation inspection summary fields to
  debugger snapshots.
- [x] Add debug-only instruction and opcode counters.
- [x] Add debug-only syscall count and per-target syscall counts.
- [x] Add debug-only syscall timing totals and per-target timings.
- [x] Include stderr diagnostics in `stderr.txt` for load and execute failures.
- [x] Add an automated debug artifact contract test.
- [x] Wire the debug artifact contract test into `./test-aivm-c.sh`.
- [x] Confirm production `aivm` still builds without debug profiler fields.
- [x] Confirm `./test-aivm-c.sh` passes with debug artifact coverage.

Optional follow-up after returning to production readiness:

- [ ] Add ordered bounded VM step trace events.
- [ ] Add ordered syscall event records with argument/result summaries.
- [ ] Add source map fields: source file, source line, source node id, and
  function name.
- [ ] Add richer frame-local summaries in `stack_trace.toml`.
- [ ] Add JSON output mode for agent tools that prefer JSON over TOML.
- [ ] Add artifact size limits and explicit truncation records.
- [ ] Add AiVectra semantic UI capture artifacts.

Decision:

- If every required item is checked and `./test-aivm-c.sh` passes, move back to
  `Docs/Production-VM-Readiness.md`.
- Optional follow-up items should become tracked beta tasks only when they block
  a concrete demo, debugging workflow, or release requirement.

## UI And AiVectra Debugging

AiVectra debug tooling should use the same artifact philosophy:

- semantic scene capture
- deterministic input events
- event queue records
- visual contract checks
- optional screenshot comparison as secondary evidence

The human and agent should be able to inspect the same semantic UI state.
Screenshots validate rendering reality, but semantic debug capture is the
primary agent-readable surface.

## Syscall Policy

Debug syscalls may exist when they cross the host/debugger boundary. They must
not become a general-purpose library escape hatch.

Production `aivm` should not bind debug/profile-only targets by default.
`aivm-debug` may bind them by default.

The debug/profile-only syscall set is all `sys.debug.*` targets. Contract
metadata remains available to validation and tooling, but production release
hosts must not bind those targets unless the user explicitly enters a debug
command/runtime surface.

Debug-only syscall groups:

- frame/UI capture
- replay
- artifact writing
- VM tracing
- profiler collection
- debugger control
- inspector queries

Production-safe syscall groups:

- stdout
- stderr
- process/file/network/time syscalls allowed by the selected runtime profile
- minimal runtime identity

## Beta Exit Criteria

For beta, debugging is acceptable when:

- production `aivm` has a small diagnostics surface
- `aivm-debug` can emit deterministic artifact bundles
- `aivm-debug` emits deterministic artifact bundles for load failures and VM
  execution failures
- VM failures include structured stack traces in debug mode
- profiler output is machine-readable
- syscall and VM traces are machine-readable
- agent workflow docs explain how to gather and inspect artifacts
- AiVectra has at least one semantic UI capture flow that agents can inspect
- debug/profile-only targets are not silently available through production
  release bindings
