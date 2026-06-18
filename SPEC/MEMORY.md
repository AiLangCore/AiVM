# AiVM Memory Model

Status: normative implementation strategy for AiVM.

AiLang owns semantic memory contracts: value identity, evaluation behavior,
validation rules, and observable failure semantics. AiVM owns the runtime memory
mechanics that make those contracts deterministic and bounded.

## Goals

- Keep VM execution deterministic under memory pressure.
- Keep runtime memory bounded by explicit limits.
- Preserve stable AiLang value behavior while allowing implementation-level
  compaction and reclamation.
- Keep host resources outside semantic memory unless explicitly represented by a
  syscall contract.
- Support beta multithreaded execution without shared mutable semantic heaps.

## Beta Memory and Threading Strategy

For beta, AiVM may run background workers in parallel. Observable AiLang and
AiVectra state changes must occur only through deterministic queue dispatch.

Canonical rule:

```text
AiVM can run background workers in parallel, but observable AiLang/AiVectra
state changes only happen through deterministic queue dispatch.
```

Beta requirements:

- worker threads are mechanical execution resources
- workers do not share a mutable semantic heap
- workers do not mutate observable semantic state directly
- worker results are copied, frozen, or represented as immutable messages
- the deterministic event queue serializes worker results before semantic state
  changes are applied
- thread scheduling may affect completion timing, but must not affect
  observable semantic order

Generational memory management is not a beta requirement. Deterministic
generational arenas are post-beta research and hardening work.

## Arena Model

AiVM uses explicit arenas for:

- stack values
- locals
- strings
- byte buffers
- node records
- node attributes
- node children

Large arena storage is heap-backed during VM initialization. Arena capacity is
still bounded by compile-time VM limits. Heap-backed storage is an
implementation detail; it does not make allocation unbounded.

Arena growth and compaction must be deterministic for the same program, inputs,
runtime profile, and syscall results.

## String Arena

Strings stored in VM values, node kinds, node IDs, and string attributes are
owned by the VM string arena unless they are immutable program constants or host
constants whose lifetime exceeds the VM execution.

String arena compaction must:

- relocate all live stack string values
- relocate all live local string values
- relocate completed task string results
- relocate parallel branch string values
- relocate live node kind, ID, attr key, and attr string pointers
- leave no live VM pointer referencing discarded arena storage

Dead node records are not semantic roots. Diagnostic tooling must not interpret
dead node records as live program state.

## Node Arena

Node records are VM-owned immutable semantic values after construction.

Node compaction must:

- mark live node handles from VM roots
- preserve reachable node records, attributes, and children
- remap all live node handles
- remap child handles in preserved nodes
- reject dangling live handles as invalid VM state

Live node roots include:

- stack values
- locals
- completed task results
- parallel branch values
- process argument nodes
- UI runtime root nodes owned by AiVM
- explicit extra handles supplied by an in-progress allocation path

## Scratch Arenas

Temporary implementation buffers are scratch memory only. They must not become
observable semantic storage.

Scratch memory may be used for:

- allocation planning
- compaction maps
- temporary string snapshots
- temporary child handle remapping
- syscall marshaling
- parser/compiler tokenization and parse construction
- compiler/tooling analysis passes

Scratch memory must be released or discarded before returning to normal VM
execution. Live VM records must not point into scratch buffers.

Parser/compiler scratch arenas are the next memory hardening step. Compiler
workloads may create large volumes of temporary parser nodes and parse results.
Those temporaries should be placed in scratch regions or shortened lifetimes
before raising semantic node limits again.

Scratch result pairs are the first runtime mechanism for removing parser result
wrapper nodes. AiVM owns a bounded scratch-pair arena and `MAKE_PAIR`,
`PAIR_FIRST`, and `PAIR_SECOND` opcodes. Pair values are VM/compiler
implementation values, not syscalls or public semantic objects.

Scratch-pair roots are derived from live VM roots. Only scratch pairs reachable
from stack values, locals, completed task results, or parallel branch values
may retain contained strings or node handles during compaction. Dead scratch
pairs must not keep compiler/parser strings or nodes alive. Contained node
references in reachable scratch pairs are roots during safe-point compaction
and are remapped with other live node values.

## Retained Intermediate Node Reduction

Compiler and parser workloads must minimize retained intermediate nodes.

Targets:

- avoid retaining token nodes after the final AST is constructed
- avoid retaining parse result wrapper nodes longer than needed
- keep parser diagnostics rooted only while diagnostics are needed
- prefer scratch-owned intermediate structures for parser/compiler internals
- keep final AST nodes in semantic node storage

Debug diagnostics should continue reporting retained node kinds and live root
attribution so parser/compiler memory work is measurable.

## Worker-Local Heaps

Worker threads may use worker-local heap storage for mechanical execution.

Worker-local storage must not contain shared mutable AiLang semantic state.
Worker results cross back into semantic execution only through deterministic VM
queues or completed task records.

Worker-local heaps are a beta-direction feature. They are allowed for
background execution, blocking host work, parsing, validation, and other
mechanical tasks. They are not shared semantic heaps.

Worker-local heaps must:

- be owned by one worker
- own worker task inputs and temporary execution payloads before work starts
- be released when the worker task completes or is canceled
- produce immutable message payloads or copied semantic values at the boundary
- avoid exposing worker-local pointers to the UI/Semantic thread

Current host worker tasks use a worker-owned heap context for task name,
payload, result, and error strings.

`AIVM_OP_ASYNC_CALL` bytecode execution runs through an isolated worker VM
state. The worker VM owns its stack, locals, arenas, node records, scratch
pairs, and temporary execution state. Arguments and results cross the boundary
only by copy. The current beta handoff supports void, null, bool, int, string,
bytes, node graphs, and scratch pairs. Node graphs are copied into the
destination VM with child handles remapped to copied destination handles.
Scratch pairs are copied recursively so pair contents never expose worker-local
handles. Unknown values are rejected at the boundary.

The isolated worker VM runs on a native worker thread. `AIVM_OP_ASYNC_CALL`
creates a pending task record and returns its task handle to the parent VM.
`AIVM_OP_AWAIT` joins the pending worker, copies the frozen result into the
parent VM, marks the task completed, and then consumes the task result. VM reset
and disposal join and release any unconsumed pending bytecode workers.

## Immutable Shared Memory

The following may be shared across workers because they are immutable:

- compiled module bytecode
- constant tables
- read-only metadata
- immutable frozen assets
- immutable module cache entries

Mutable semantic values are not shared across workers. If a value must cross a
worker boundary, it must be copied, frozen, or represented as a deterministic
message.

The immutable shared module cache is exposed through `AivmModuleCache`. Cache
entries deep-copy loaded `AivmProgram` data into cache-owned storage and expose
only `const AivmProgram*` views to callers. Cached modules must not contain
mutable per-execution semantic state.

Module cache operations must:

- reject duplicate module names deterministically
- reject invalid or oversized names deterministically
- enforce `AIVM_MODULE_CACHE_MAX_MODULES`
- enforce `AIVM_MODULE_CACHE_MAX_BYTES`
- preserve cached program contents even if the caller mutates or clears the
  source program after insertion
- keep string and byte constants pointed at cache-owned immutable storage

## Deterministic Queue Dispatch

AiVM owns the mechanical runtime queue used to serialize worker results back
into observable execution.

Queue requirements:

- messages are immutable when enqueued
- enqueue validation rejects live VM node handles, scratch-pair handles,
  unknown values, null strings, and non-empty null byte views
- messages have deterministic ordering metadata
- dequeue/application order is deterministic
- batching is allowed when it preserves deterministic ordering
- cancellation/shutdown messages are ordered deterministically
- UI/Semantic thread state mutation occurs only while processing queue messages

AiVM documents queue mechanics here. AiLang owns language-level concurrency
semantics in its canonical specs. AiVectra owns UI runtime integration rules.

## Large Object and Blob Storage

Large object/blob storage exists for assets, byte buffers, large UI payloads,
and host data that should not pressure node/string arenas.

Blob storage must:

- be handle-based
- have explicit resource limits
- report deterministic allocation, read, and release failures
- avoid changing semantic ordering
- be released deterministically by VM lifetime, profile policy, or explicit
  resource ownership rules

Large object storage must not become a shared mutable semantic heap.

Initial native support is exposed through the AiVM C API:

- `aivm_blob_create`
- `aivm_blob_read`
- `aivm_blob_release`
- `aivm_blob_active_count`

Blob handles are VM-local. They are not AiLang semantic values and must not be
shared across VMs or workers as mutable state. Runtime profiles expose
`blob_capacity` and `blob_bytes`; exceeding either limit returns a deterministic
`AIVMB002` failure and increments blob pressure accounting. `aivm_reset_state`
and `aivm_dispose` release all active blobs deterministically.

## Safe Points

Compaction and reclamation may occur only at deterministic safe points.

Required safe points include:

- before arena capacity failure is reported
- before proactive node compaction
- explicit `aivm_collect_safe_point` calls from host/tooling phase boundaries
- at explicit VM reset/dispose boundaries
- at allocation paths that can prove all temporary handles are protected
- at deterministic compiler/tooling phase boundaries
- at deterministic worker result handoff boundaries
- at function return boundaries after enough node allocation pressure has
  accumulated

Compaction must not depend on wall-clock timing, host thread scheduling, or
non-deterministic host state.

Safe-point collection is the beta memory strategy before any generational
memory work. The VM may prepare compaction mechanically on background workers
only if the observable collection point and resulting state transition remain
deterministic.

## Resource Limits

Resource limits are part of the runtime profile. The production VM must expose
stable limits for:

- stack capacity
- call frame capacity
- local slot capacity
- string arena capacity
- byte arena capacity
- node capacity
- node attribute capacity
- node child capacity
- task capacity
- parallel value capacity

Host-resource limits are guardrails over bounded primitives. Large files,
network streams, debug artifacts, UI assets, and blobs should not rely on
unbounded all-at-once allocation. The preferred runtime shape is handle/chunk
processing, immutable message passing, worker handoff, and explicit blob
storage with deterministic release rules.

Future runtime profiles may tune those limits for production, debug, and
compiler/tooling workloads. A profile change must be explicit and visible in
diagnostics.

Initial profile names:

- `production`: normal command-line/runtime execution
- `debug`: diagnostics, tracing, profiling, and developer inspection
- `tooling`: compiler, parser, package restore, and SDK tooling workloads

Runtime profiles select limits and diagnostics first. They must not silently
change language semantics.

## Allocation Failure

Allocation failure must be deterministic.

When memory cannot be reclaimed or grown within the active profile limits, AiVM
must:

- enter VM error state
- stop execution deterministically
- report a stable VM error code and detail
- preserve debug telemetry when running under `aivm-debug`

The VM must not silently fall back to unbounded host allocation to complete a
semantic allocation.

## Diagnostics

Debug diagnostics may report:

- arena usage and high-water marks
- memory pressure counters
- node compaction attempts
- reclaimed node, attr, and child counts
- live-root attribution
- live node-kind attribution

Diagnostics must distinguish live semantic roots from dead arena records.

## Roadmap Order

Memory and threading hardening proceeds in this order:

1. Stabilize current bounded arenas.
2. Add parser/compiler scratch arenas.
3. Reduce retained intermediate nodes.
4. Formalize deterministic safe-point compaction.
5. Add worker-local heaps and deterministic messaging.
6. Add immutable shared module cache.
7. Add large-object/blob storage.
8. Add runtime memory profiles.
9. Research deterministic generational arenas after beta.

Deterministic generational arenas are explicitly post-beta. They must not block
beta readiness.

## Ownership Boundary

AiVM owns this memory implementation strategy.

AiLang specs may define semantic requirements that constrain the strategy, but
they must not duplicate runtime implementation constants or compaction mechanics
as language semantics.

AiVM must not duplicate AiLang concurrency semantics in this file. This document
defines runtime memory, worker, queue, and compaction strategy only.
