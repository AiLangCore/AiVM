# Hosted Tooling Memory Ownership

Status: implemented runtime ownership model; the normative contract is
`../SPEC/RESOURCE_LIMITS.md`.

## Live-byte classification

| Class | Current owners | Policy |
| --- | --- | --- |
| Permanently live | stack and local byte values, live parallel values, live scratch-pair values, currently observed Task results | Relocated during compaction; never spilled while directly observable. |
| Reclaimable phase-local | dead concatenation outputs and other arena allocations no longer reachable from VM roots | Compact before requesting additional backing and at deterministic safe points. |
| Reloadable | terminal, unobserved tooling-worker results | Spill to opaque host temporary storage and reload at owner Await. |
| Duplicated | framed `runAll` batch bytes plus isolated invocation payload copies; worker result transport plus owner-arena result | Release batch framing after all tasks are materialized; release transport storage immediately after owner copying. |

## Pressure response

Hosted tooling uses the following mechanical sequence:

1. Compact the byte arena and deduplicate identical rooted slices.
2. Recalculate required backing from live bytes plus the pending allocation.
3. Grow arena backing geometrically to demand; the value reported as
   `bytes_arena_capacity = 0` means no profile constant selects semantic failure.
4. Recompute physical worker dispatch capacity from current available host
   memory before taking another queued task. Running work is allowed to finish;
   queued work waits.
5. Keep completed worker results in temporary spill storage until Await.
6. Allow the host virtual-memory system to page arena backing.

Production, debug, embedded, and explicitly constrained profiles continue to
use their declared hard byte capacity. Hosted spill names and offsets are not
observable and therefore cannot influence output ordering or diagnostics.

## Remaining deduplication work

Active isolated invocations still own a payload copy for VM isolation. Removing
that copy requires an immutable reference-counted transport buffer whose lifetime
spans the worker call; it is intentionally separate from compiler semantics.
