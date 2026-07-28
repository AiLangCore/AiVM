# Production VM Readiness

Status: active hardening checklist.

The production VM goal is a tiny, fast, deterministic execution engine with a
small, explicit host boundary. Adding more syscalls is not the default answer.
The default answer is to shrink and harden the syscall surface.

AiVM follows the normal runtime model for now: host syscalls run with the
permissions of the operating-system process that launched `aivm`. Custom
capability gating is optional future sandboxing work, not a blocker for the
first production-grade runtime.

## Syscall Boundary

- [x] Document the syscall addition rule.
- [x] List all current syscall contracts.
- [x] Add a check for duplicate syscall IDs and targets.
- [x] Add a check that every syscall has docs and contract-test coverage.
- [x] Beta: decide which debug/profiling targets are only available from
  `aivm-debug`, then enforce that split in release builds.
- [x] Distinguish unknown syscall, known-but-unbound syscall, invalid argument,
  timeout, resource-limit, and host-failure VM errors.
- [x] Document beta resource-limit records and error-code families in
  `SPEC/RESOURCE_LIMITS.md`.
- [x] Beta: replace unbounded/all-at-once host operations with bounded
  primitives first, then enforce named resource limit records for filesystem
  reads/writes, network reads/writes, process count, worker count, UI windows,
  debug artifacts, and syscall execution time.
- [x] Beta: document that OS users, containers, app sandboxes, and deployment
  environments are the production security boundary.

Optional future sandboxing:

- [x] Add per-syscall capability groups to the contract metadata.
- [x] Add a runtime capability policy object.
- [x] Add CLI flags for explicitly enabling or denying capability groups.

## Deterministic Library Migration

Status: package metadata and registry validation are no longer the active
readiness thread unless a CI or release blocker appears.

- [x] Review `sys.str.*` contracts and move deterministic behavior into AiLang
  core libraries.
- [x] Review `sys.bytes.*` contracts and move deterministic behavior into AiLang
  core libraries.
- [x] Review deterministic crypto helpers such as base64 and hashes.
- [x] Keep `sys.crypto.randomBytes` as host-boundary because host entropy is
  nondeterministic.
- [x] Update AiLang callers before removing moved syscalls.
- [x] Remove moved `sys.str.*` and `sys.bytes.*` syscall contracts completely;
  do not leave compatibility adapters before the first major or minor release.

Current audit:

- Baseline wrappers exist in `AiLang/src/std/str.aos` and
  `AiLang/src/std/bytes.aos`.
- `std.str` and `std.bytes` now lower deterministic helpers through non-syscall
  intrinsic nodes backed by VM opcodes.
- AiLang CI rejects new direct `sys.str.*` or `sys.bytes.*` usage outside
  syscall-level regression files.
- AiVM syscall contract checks fail if deterministic text/bytes utility
  contracts are reintroduced.
- `sys.crypto.randomBytes` remains a valid host-boundary syscall.

## Production Defaults

Production `aivm` should behave like a normal command-line runtime: it runs with
the OS/process permissions of the caller. The syscall contract defines the VM
host boundary, but it is not currently a sandbox permission model.

Debug syscalls should be separated from production behavior. `aivm-debug` may
bind diagnostic targets by default because it is intentionally diagnostic.

Full debugger, profiler, stack-trace, capture, replay, and agent-inspection
capabilities are required project tooling, but they belong to `aivm-debug` and
debug/profile command surfaces rather than the stripped production VM.

## Memory Strategy

AiVM uses deterministic ceilings, but large VM regions are heap-backed rather
than embedded directly in `AivmVm`. The goal is to keep production behavior
bounded without forcing every host process, test executable, or embedded API
caller to carry a multi-megabyte stack object.

Beta memory/threading direction:

```text
AiVM can run background workers in parallel, but observable AiLang/AiVectra
state changes only happen through deterministic queue dispatch.
```

Generational memory management is post-beta research/hardening work, not a beta
requirement.

- [x] Keep stack, locals, strings, bytes, node, attr, and child arenas under
  explicit VM limits.
- [x] Allocate large arena storage on the heap during VM initialization.
- [x] Provide `aivm_dispose` for embedders that need deterministic release.
- [x] Keep memory pressure telemetry in debug diagnostics.
- [x] Add parser/compiler memory attribution before increasing node limits
  again.
- [x] Add live node-kind attribution to debug diagnostics.
- [x] Gate debug memory telemetry fields for pressure counters, root
  attribution, and node-kind attribution in `test-aivm-c.sh`.
- [x] Add regression coverage for string arena compaction preserving live node
  strings.
- [x] Beta: add parser scratch storage for tokenization and parse-result
  construction.
- [x] Beta: reduce retained parser intermediate nodes enough that compiler
  source parsing no longer depends on raising arena ceilings.
- [x] Beta: formalize deterministic safe-point compaction beyond allocation
  paths.
- [x] Beta: document worker-local heap strategy for mechanical background work.
- [x] Beta: document immutable message passing through deterministic queue
  dispatch.
- [x] Beta: document immutable shared module cache direction.
- [x] Beta: document large-object/blob storage direction.
- [x] Beta: run `AIVM_OP_ASYNC_CALL` bytecode through an isolated worker VM
  state with copied scalar/string/bytes arguments and results.
- [x] Beta: add frozen complex-value handoff for worker node graphs and
  scratch-pair results.
- [x] Beta: add OS-thread scheduling for isolated bytecode worker execution
  without shared mutable semantic heaps.
- [x] Document named runtime profiles for production, debug, and compiler/tooling
  workloads.
- [x] Emit the active runtime profile and VM limit records in debug artifact
  diagnostics.
- [ ] Post-beta: research deterministic generational arenas only after beta
  memory/threading requirements are stable.

## Threading Strategy

AiVM supports multithreaded execution before beta through worker threads and a
deterministic event queue.

Worker threads are mechanical execution resources. They may perform background
work, blocking host operations, parsing, validation, and other runtime tasks,
but they must not mutate observable AiLang or AiVectra semantic state directly.

Before beta:

- no shared mutable semantic heap
- no worker mutation of UI state or live semantic state
- no generational GC requirement
- worker results cross the boundary as immutable messages, frozen payloads, or
  copied semantic values
- observable semantic mutation occurs only when the deterministic queue is
  processed
- host thread scheduling may affect timing, but must not affect semantic order

AiVM owns worker mechanics, queue mechanics, resource limits, and runtime memory
strategy. AiLang owns language-level concurrency semantics. AiVectra owns
UI/Semantic thread integration and UI mutation rules.

## Tracked Beta Tasks

- `aivm-debug` syscall profile: all `sys.debug.*` targets are debug/profile-only.
- Debugger/profiler contract: implement stable stack traces, profiler artifacts,
  debugger stepping/inspection, and agent-readable summaries.
- Runtime profiles: profile names, diagnostic emission, and explicit
  `aivm-debug debug capture run --profile production|debug|tooling` selection
  are wired.
- Resource limits: beta documentation exists in `SPEC/RESOURCE_LIMITS.md`; whole
  file filesystem calls enforce profile limits, file handle/chunk primitives are
  available, and TCP/UDP paths enforce per-call and cumulative per-run byte
  limits.
- Parser retained nodes: parser memory attribution gates representative compiler
  files. Token nodes are scratch strings in AiLang. Parser result helpers lower
  to bounded AiVM scratch-pair values and pair access opcodes.
- Parser/compiler scratch storage: transient compiler analysis state uses scratch
  storage while final AST nodes remain semantic nodes.
- Safe-point compaction: explicit deterministic compaction exists for phase
  boundaries, function return pressure, `AWAIT`, `PAR_JOIN`, and run-complete
  boundaries.
- Worker-local heaps and messaging: immutable deterministic queue messages reject
  live VM node handles, scratch-pair handles, unknown values, null strings, and
  invalid byte views before the host adapter sees them.
- Shared immutable module cache: design read-only module/cache storage that can
  be shared across workers without exposing mutable semantic state.
- Large-object/blob storage: design handle-based storage for large byte buffers,
  assets, and UI payloads with explicit limits and deterministic failures.
- Security boundary docs: document that AiVM is a runtime with explicit syscalls,
  not a sandbox; deployment sandboxing is provided by the OS, container, or app
  environment.

Increasing arena capacities is not the default fix for compiler/parser failures.
First measure retained node kinds and root reachability, then reduce temporary
parser structures or shorten their lifetimes.
