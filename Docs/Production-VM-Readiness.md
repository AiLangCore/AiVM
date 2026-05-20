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
- [x] Distinguish unknown syscall, known-but-unbound syscall, invalid
  argument, timeout, resource-limit, and host-failure VM errors.
- [x] Document beta resource-limit records and error-code families in
  `Docs/Resource-Limits-And-Errors.md`.
- [ ] Beta: replace unbounded/all-at-once host operations with bounded
  primitives first, then enforce named resource limit records for filesystem
  reads/writes, network reads/writes, process count, worker count, UI windows,
  debug artifacts, and syscall execution time.
  - [x] Define named host-resource limit records in runtime profiles.
  - [x] Enforce `file_read_bytes` and `file_write_bytes` on the current
    whole-file filesystem syscalls.
  - [x] Design and implement handle/chunk filesystem primitives so large files
    stream through bounded chunks instead of raising whole-file limits.
  - [x] Enforce per-call network read/write limits on existing TCP and UDP
    handle-based network primitives.
  - [ ] Add total network byte accounting if a beta runtime profile requires a
    cumulative network budget.
  - [x] Enforce `process_count` before host child process creation.
  - [x] Enforce `worker_count` before worker handle allocation.
  - [x] Enforce `ui_window_count` before host window creation.
  - [x] Enforce `syscall_elapsed_ms` after host syscall dispatch returns.
  - [x] Enforce `debug_artifact_bytes` across published `aivm-debug`
    artifact bundles.
- [x] Beta: document that OS users, containers, app sandboxes, and deployment
  environments are the production security boundary.

Optional future sandboxing:

- [ ] Add per-syscall capability groups to the contract metadata.
- [ ] Add a runtime capability policy object.
- [ ] Add CLI flags for explicitly enabling or denying capability groups.

## Deterministic Library Migration

- [ ] Review `sys.str.*` contracts and move deterministic behavior into AiLang
  core libraries.
- [ ] Review `sys.bytes.*` contracts and move deterministic behavior into
  AiLang core libraries.
- [ ] Review deterministic crypto helpers such as base64 and hashes.
- [ ] Keep `sys.crypto.randomBytes` as host-boundary because host entropy is
  nondeterministic.
- [ ] Update AiLang callers before removing moved syscalls.
- [ ] Remove moved syscall contracts completely; do not leave compatibility
  adapters before the first major or minor release.

## Production Defaults

Production `aivm` should behave like a normal command-line runtime: it runs with
the OS/process permissions of the caller. The syscall contract defines the VM
host boundary, but it is not currently a sandbox permission model.

AiLang is a general-purpose programming language, so production `aivm` binds
process syscalls. Process spawning, environment reads, child-process waits, and
pipe reads are normal runtime capabilities. Future hardening should add explicit
profiles/resource limits rather than classify process execution as debug-only.

Debug syscalls should be separated from production behavior. `aivm-debug` may
bind diagnostic targets by default because it is intentionally diagnostic.

Full debugger, profiler, stack-trace, capture, replay, and agent-inspection
capabilities are required project tooling, but they belong to `aivm-debug` and
debug/profile command surfaces rather than the stripped production VM. The
canonical direction is documented in `SPEC/DEBUGGING.md`.

The current debug-tooling slice is allowed to temporarily lead the work only
until the required items in `SPEC/DEBUGGING.md` under
`Return-To-Production-Readiness Checklist` are checked and `./test-aivm-c.sh`
passes. After that, return to this production readiness checklist.

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
- [x] Add regression coverage for string arena compaction preserving live node
  strings.
- [ ] Beta: add parser/compiler scratch arenas for tokenization, parse
  construction, and compiler analysis passes.
- [ ] Beta: reduce retained parser intermediate nodes so compiler source
  parsing does not depend on repeatedly raising arena ceilings.
- [ ] Beta: formalize deterministic safe-point compaction beyond allocation
  paths.
- [ ] Beta: add worker-local heap strategy for mechanical background work.
- [ ] Beta: add immutable message passing through deterministic queue dispatch.
- [ ] Beta: document immutable shared module cache direction.
- [ ] Beta: document large-object/blob storage direction.
- [x] Document named runtime profiles for production, debug, and
  compiler/tooling workloads.
- [x] Emit the active runtime profile and VM limit records in debug artifact
  diagnostics.
- [x] Beta: add profile selection beyond default production/debug build
  behavior.
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

AiVM owns worker mechanics, queue mechanics, resource limits, and runtime
memory strategy. AiLang owns language-level concurrency semantics. AiVectra owns
UI/Semantic thread integration and UI mutation rules.

## Tracked Beta Tasks

These are the immediate hardening tasks before beta:

- `aivm-debug` syscall profile: all `sys.debug.*` targets are debug/profile-only;
  the transitional native host no longer binds them for normal production runs,
  and contract tests cover debug-target classification.
- Debugger/profiler contract: implement the `SPEC/DEBUGGING.md` target surface
  for stack traces, profiler artifacts, debugger stepping/inspection, and
  agent-readable summaries.
- Runtime profiles: profile names, diagnostic emission, and explicit
  `aivm-debug debug capture run --profile production|debug|tooling` selection
  are wired; remaining work is any non-default tooling limits.
- Resource limits: beta documentation exists in
  `Docs/Resource-Limits-And-Errors.md`; current whole-file filesystem
  read/write syscalls enforce profile limits, file handle/chunk primitives are
  available, and TCP/UDP network read/write paths enforce per-call byte limits.
  `sys.process.spawn` enforces the process-count limit before host child
  process creation, `sys.worker.start` enforces the worker-count limit before
  worker handle allocation, `sys.ui.createWindow` enforces the UI-window limit
  before host window creation, VM syscall dispatch enforces elapsed-time limits
  after host dispatch returns, and `aivm-debug` enforces artifact byte budgets
  before publishing artifact files. Remaining work is any cumulative network
  byte accounting required by beta profiles.
- Parser retained nodes: use parser memory attribution to reduce temporary
  token/result nodes retained during compiler source parsing.
- Parser/compiler scratch arenas: route parser/compiler internals through
  scratch storage where possible while keeping final AST nodes in semantic node
  storage.
- Safe-point compaction: define deterministic safe points for allocation,
  compiler/tooling phase boundaries, and worker result handoff.
- Worker-local heaps and messaging: document and implement worker-local
  mechanical storage plus immutable deterministic queue messages.
- Shared immutable module cache: design read-only module/cache storage that can
  be shared across workers without exposing mutable semantic state.
- Large-object/blob storage: design handle-based storage for large byte buffers,
  assets, and UI payloads with explicit limits and deterministic failures.
- Security boundary docs: document that AiVM is a runtime with explicit
  syscalls, not a sandbox; deployment sandboxing is provided by the OS,
  container, or app environment.

Increasing arena capacities is not the default fix for compiler/parser
failures. First measure retained node kinds and root reachability, then reduce
temporary parser structures or shorten their lifetimes.
