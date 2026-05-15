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

Scratch memory must be released or discarded before returning to normal VM
execution. Live VM records must not point into scratch buffers.

## Worker-Local Heaps

Worker threads may use worker-local heap storage for mechanical execution.

Worker-local storage must not contain shared mutable AiLang semantic state.
Worker results cross back into semantic execution only through deterministic VM
queues or completed task records.

## Immutable Shared Memory

The following may be shared across workers because they are immutable:

- compiled module bytecode
- constant tables
- read-only metadata
- immutable frozen assets

Mutable semantic values are not shared across workers. If a value must cross a
worker boundary, it must be copied, frozen, or represented as a deterministic
message.

## Safe Points

Compaction and reclamation may occur only at deterministic safe points.

Required safe points include:

- before arena capacity failure is reported
- before proactive node compaction
- at explicit VM reset/dispose boundaries
- at allocation paths that can prove all temporary handles are protected

Compaction must not depend on wall-clock timing, host thread scheduling, or
non-deterministic host state.

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

Future runtime profiles may tune those limits for production, debug, and
compiler/tooling workloads. A profile change must be explicit and visible in
diagnostics.

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

## Ownership Boundary

AiVM owns this memory implementation strategy.

AiLang specs may define semantic requirements that constrain the strategy, but
they must not duplicate runtime implementation constants or compaction mechanics
as language semantics.
