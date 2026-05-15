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
- [ ] Beta: decide which debug/profiling targets are only available from
  `aivm-debug`, then enforce that split in release builds.
- [x] Distinguish unknown syscall, known-but-unbound syscall, invalid
  argument, timeout, resource-limit, and host-failure VM errors.
- [ ] Beta: add named resource limit records for filesystem reads/writes,
  network reads/writes, process count, worker count, UI windows, debug
  artifacts, and syscall execution time.
- [ ] Beta: document that OS users, containers, app sandboxes, and deployment
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

## Memory Strategy

AiVM uses deterministic ceilings, but large VM regions are heap-backed rather
than embedded directly in `AivmVm`. The goal is to keep production behavior
bounded without forcing every host process, test executable, or embedded API
caller to carry a multi-megabyte stack object.

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
- [ ] Beta: add named runtime profiles for production, debug, and
  compiler/tooling workloads.
- [ ] Beta: reduce retained parser intermediate nodes so compiler source
  parsing does not depend on repeatedly raising arena ceilings.

## Tracked Beta Tasks

These are the immediate hardening tasks before beta:

- `aivm-debug` syscall profile: identify debug-only targets, make production
  release binding explicit, and add a contract test.
- Runtime profiles: define `production`, `debug`, and `tooling` limits, emit the
  active profile in diagnostics, and document profile selection.
- Resource limits: define stable limit records for file, network, process,
  worker, UI, debug artifact, and syscall timeout behavior.
- Parser retained nodes: use parser memory attribution to reduce temporary
  token/result nodes retained during compiler source parsing.
- Security boundary docs: document that AiVM is a runtime with explicit
  syscalls, not a sandbox; deployment sandboxing is provided by the OS,
  container, or app environment.

Increasing arena capacities is not the default fix for compiler/parser
failures. First measure retained node kinds and root reachability, then reduce
temporary parser structures or shorten their lifetimes.
