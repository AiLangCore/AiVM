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
- [x] Beta: replace unbounded/all-at-once host operations with bounded
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
  - [x] Add total network byte accounting if a beta runtime profile requires a
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

- [x] Add per-syscall capability groups to the contract metadata.
- [x] Add a runtime capability policy object.
- [x] Add CLI flags for explicitly enabling or denying capability groups.

## Deterministic Library Migration

Status: next priority after package namespace/registry hardening. Package
metadata and registry validation are no longer the active readiness thread
unless a CI or release blocker appears.

- [x] Review `sys.str.*` contracts and move deterministic behavior into AiLang
  core libraries.
- [x] Review `sys.bytes.*` contracts and move deterministic behavior into
  AiLang core libraries.
- [x] Review deterministic crypto helpers such as base64 and hashes.
- [x] Keep `sys.crypto.randomBytes` as host-boundary because host entropy is
  nondeterministic.
- [x] Update AiLang callers before removing moved syscalls.
- [x] Remove moved `sys.str.*` and `sys.bytes.*` syscall contracts completely;
  do not leave compatibility adapters before the first major or minor release.

Current audit:

- Baseline wrappers exist in `AiLang/src/std/str.aos` and
  `AiLang/src/std/bytes.aos`.
- `std.str` now lowers deterministic string helpers through non-syscall
  intrinsic nodes (`StringSlice`, `StringRemove`, `StringFind`,
  `StringFromCodePoint`, `StringDecodeUnicodeHex4`,
  `StringDecodeUnicodeSurrogatePairHex4`, and `StringUtf8ByteCount`) backed by
  VM opcodes.
- `std.bytes.length`, `std.bytes.at`, `std.bytes.slice`, `std.bytes.concat`,
  `std.bytes.fromBase64`, `std.bytes.toBase64`, `std.bytes.fromUtf8String`,
  and `std.bytes.toUtf8String` now lower through non-syscall intrinsic nodes
  backed by VM opcodes.
- AiLang now has a canonical primitive migration note at
  `Docs/Deterministic-Text-Bytes-Primitives.md`; the required non-syscall
  text/bytes primitive surface now exists.
- AiLang now has a static validation contract test for the current and planned
  primitive node names and arities.
- Optional packages `std-json`, `std-http`, and `std-ui-input` now use the
  public `std.str` surface instead of direct `sys.str.*` calls.
- AiVectra library text helpers and the WeatherApp/InteractiveSvgMvp samples
  now use the staged `std.str` surface instead of direct `sys.str.*` calls.
- AiLang CLI string helpers now use the staged `std.str` surface instead of
  direct `sys.str.*` calls.
- AiLang compiler parser character slicing now uses the staged `std.str`
  surface instead of direct `sys.str.*` calls.
- `std.bytes` now exports dotted names such as `bytes.toUtf8String` so it can
  be imported beside `std.str` without flat-name collisions.
- AiLang CLI/compiler runtime and parser profiling/selfhost scripts now use
  the staged `std.bytes` surface instead of direct `sys.bytes.*` calls.
- AiLang CI now rejects new direct `sys.str.*` or `sys.bytes.*` usage outside
  syscall-level regression files.
- AiVM syscall contract checks fail if deterministic text/bytes utility
  contracts are reintroduced.
- AiVM no longer exposes deterministic crypto/base64/hash/HMAC utility
  syscalls; optional crypto packages should own those APIs.
- `sys.crypto.randomBytes` remains a valid host-boundary syscall.
- Deterministic crypto helpers and base64 should become AiLang libraries,
  intrinsic bytecode, or optional packages unless a measured VM/runtime reason
  justifies keeping a host primitive.

Migration order:

1. Replace direct application/package/sample calls with baseline `std.str` and
   `std.bytes` wrappers so deterministic behavior has a single public surface.
2. Move deterministic string and bytes implementations out of host syscall
   handlers and into AiLang-authored libraries or VM intrinsic opcodes.
3. Update compiler/runtime callers to use the AiLang library surface or a
   compiler-owned internal helper, not `sys.*`.
4. Remove the migrated syscall contracts, docs, and tests completely.
5. Keep guard coverage that rejects reintroduced deterministic `sys.str.*` and
   `sys.bytes.*` syscall contracts.

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
- [x] Gate debug memory telemetry fields for pressure counters, root
  attribution, and node-kind attribution in `test-aivm-c.sh`.
- [x] Add regression coverage for string arena compaction preserving live node
  strings.
- [x] Beta: add parser scratch storage for tokenization and parse-result
  construction.
- [x] Beta: reduce retained parser intermediate nodes enough that compiler
  source parsing no longer depends on raising arena ceilings.
- [x] Beta: add validation-analysis memory coverage and reduce retained
  compiler-analysis temporaries enough that validating `validate.aos` completes
  under deterministic memory gates. Initial validation state now uses
  scratch-pair storage for transient `(errors, ids)` state, and AiLang tooling
  evaluator state now uses scratch-pair storage for transient `(value, env)`
  state. Validation's temporary seen-id set is now string-backed instead of a
  semantic block of `Lit` nodes. Scratch-pair node and string roots now include
  only pair handles reachable from VM roots, so dead intermediate pairs no
  longer retain compiler-analysis strings or nodes.
- [x] Beta: broaden compiler-analysis memory gates beyond validation. AiLang
  now gates validation and parser analysis paths by default, and exposes a
  slower explicit `full` stress profile that includes `aic.aos`.
- [x] Beta: formalize deterministic safe-point compaction beyond allocation
  paths.
- [x] Beta: run deterministic return-boundary safe points after accumulated
  node allocation pressure so recursive parser/compiler temporaries are
  reclaimed before run completion.
- [x] Beta: document worker-local heap strategy for mechanical background
  work.
- [x] Beta: document immutable message passing through deterministic queue
  dispatch.
- [x] Beta: document immutable shared module cache direction.
- [x] Beta: document large-object/blob storage direction.
- [x] Beta: implement worker-local heap execution paths for current host
  worker tasks. Host worker task name, payload, result, and error storage now
  live in a worker-owned heap context with deterministic release at slot reuse,
  cancellation, and runtime reset.
- [x] Beta: run `AIVM_OP_ASYNC_CALL` bytecode through an isolated worker VM
  state with copied void/null/bool/int/string/bytes arguments and results.
- [x] Beta: add frozen complex-value handoff for worker node graphs and
  scratch-pair results.
- [x] Beta: add OS-thread scheduling for isolated bytecode worker execution
  without shared mutable semantic heaps.
- [x] Beta: implement immutable message payload validation at deterministic
  queue boundaries.
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
  are wired. `aivm-debug debug capture run --allow <group>` and
  `--deny <group>` expose capability policy overrides for release validation.
  Remaining work is any non-default tooling limits.
- Resource limits: beta documentation exists in
  `Docs/Resource-Limits-And-Errors.md`; current whole-file filesystem
  read/write syscalls enforce profile limits, file handle/chunk primitives are
  available, and TCP/UDP network read/write paths enforce per-call and
  cumulative per-run byte limits.
  `sys.process.spawn` enforces the process-count limit before host child
  process creation, `sys.worker.start` enforces the worker-count limit before
  worker handle allocation, `sys.ui.createWindow` enforces the UI-window limit
  before host window creation, VM syscall dispatch enforces elapsed-time limits
  after host dispatch returns, and `aivm-debug` enforces artifact byte budgets
  before publishing artifact files.
- Parser retained nodes: parser memory attribution now gates final node count,
  node high-water, and scratch-pair use for representative compiler sources
  (`format.aos`, `validate.aos`, and `aic.aos`). Token nodes are scratch
  strings in AiLang. Parser result helpers lower to bounded AiVM scratch-pair
  values and pair access opcodes that safely root/remap contained node
  references during compaction.
- Parser/compiler scratch storage: parser tokenization and parse-result
  construction are routed through scratch storage while final AST nodes remain
  in semantic node storage. Validation state now uses scratch pairs for the
  transient `(errors, ids)` analysis state while diagnostics and ID records
  remain semantic nodes. Validation's seen-id set is string-backed instead of
  retaining semantic `Lit` nodes. AiLang tooling evaluator state now uses
  scratch pairs for transient `(value, env)` state while values, closures, and
  environments remain semantic nodes. Scratch-pair compaction now roots only
  reachable scratch pairs, which prevents dead compiler-analysis intermediates
  from retaining strings and nodes. AiLang gates validation-analysis memory in
  `scripts/profile-compiler-analysis-memory.sh`. Remaining work is additional
  compiler-analysis scratch storage only for later passes that prove they still
  retain temporary structures.
- Safe-point compaction: `aivm_collect_safe_point` exposes explicit
  deterministic compaction for phase boundaries and tests cover reclamation
  below proactive pressure thresholds. `aivm_execute_program*` now runs the
  safe point after successful execution so CLI/tooling and embedders compact at
  a deterministic run-complete boundary. `AWAIT` and `PAR_JOIN` run a
  deterministic handoff safe point and release consumed completed-task records
  when no visible task handle still pins them. Function returns also run a
  deterministic safe point after `AIVM_VM_NODE_GC_RETURN_SAFEPOINT_ALLOCATIONS`
  node allocations, which reduces recursive parser/compiler high-water usage
  without making collection depend on wall-clock timing.
- Worker-local heaps and messaging: immutable deterministic queue messages now
  reject live VM node handles, scratch-pair handles, unknown values, null
  strings, and non-empty null byte views before the host adapter sees them.
  Host worker task name, payload, result, and error values now use
  worker-owned heap storage and are cleared deterministically. `ASYNC_CALL`
  bytecode now executes on a native worker thread in an isolated worker VM
  state and copies
  void/null/bool/int/string/bytes values, node graphs, and scratch pairs across
  the boundary. `AWAIT` joins pending bytecode workers deterministically before
  consuming the task result.
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
