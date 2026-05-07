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
- [ ] Decide which debug/profiling targets are only available from
  `aivm-debug`.
- [x] Distinguish unknown syscall, known-but-unbound syscall, invalid
  argument, timeout, resource-limit, and host-failure VM errors.
- [ ] Add resource limits for filesystem reads/writes, network reads/writes,
  process count, worker count, UI windows, debug artifacts, and syscall
  execution time.
- [ ] Document that OS users, containers, app sandboxes, and deployment
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
