# AiVM Syscall Boundary

Status: normative AiVM runtime contract.

Syscalls are the only permitted escape hatch from deterministic VM execution.
They cross from AiBC execution into explicit host-provided capabilities.

AiVM owns syscall dispatch mechanics, capability metadata, host binding policy,
resource-limit failure behavior, and deterministic error reporting. AiLang owns
language semantics above the syscall boundary.

## Addition Rule

A new `sys.*` target may only be added when all of these are true:

- It crosses a host boundary: OS, filesystem, process, time, network, UI,
  device, native adapter, debug/profiling host, or external resource.
- It cannot be implemented deterministically in AiLang or AiLang core libraries.
- It has an explicit contract entry in `src/sys/aivm_syscall_contracts.c`.
- The contract entry includes the syscall capability group.
- It has contract tests in `tests/unit/syscalls/test_syscall_contracts.c`.
- It is listed in this document or generated contract documentation with a
  host-boundary justification.
- The change notes explain why it is a syscall instead of AiLang library code.

Do not add syscalls for normal language or library behavior such as string
replacement, collection operations, template rendering, parsing, validation,
compiler policy, or compatibility adapters.

The automated check in `scripts/check-syscall-contracts.sh` rejects duplicate
syscall IDs, duplicate targets, undocumented targets, and targets missing from
the contract test file.

## Production Classification

AiLang is a general-purpose programming language. Production `aivm` therefore
binds normal host-boundary capabilities, including process spawning. These
syscalls run with the OS/process permissions of the launched `aivm` process.
Security hardening comes from OS permissions, resource limits, deployment
sandboxes, and explicit runtime profiles; process execution is not debug-only.

All `sys.debug.*` targets are debug/profile-only. They remain documented
contracts because bytecode validation and debug tooling need stable target
metadata, but production release hosts must not bind them by default.
`aivm-debug` and debug command surfaces may bind them.

## Capability Groups

Each syscall contract has exactly one coarse capability group. The group is
runtime metadata, not language semantics. It gives release builds, debug builds,
sandbox profiles, and agent tooling a single place to reason about the host
boundary a syscall crosses.

The VM owns a runtime capability policy object. Production profile policy allows
normal host-boundary groups and denies `debug`; debug/tooling profiles allow
`debug` as well. A denied capability fails before host dispatch with `AIVMS008`.

Current groups:

- `core`: temporary deterministic helpers that should move into AiLang core
  libraries when self-hosting allows it.
- `console`: host stdin/stdout/stderr.
- `process`: host process state and child process execution.
- `platform`: host platform/runtime identity.
- `time`: host wall-clock, monotonic time, sleep, and timezone data.
- `filesystem`: host filesystem access.
- `crypto`: host entropy and temporary deterministic crypto helpers.
- `network`: host TCP/UDP/network async primitives.
- `ui`: host UI/window/rendering resources.
- `worker`: mechanical worker execution resources.
- `remote`: host remote bridge calls.
- `host`: host-default handlers outside the VM.
- `image`: host image decoding.
- `storage`: host app storage and host secure storage/keychain adapters.
- `debug`: debug/profiling/capture artifacts, bound only by `aivm-debug` and
  debug command surfaces.

## Contract Table Source

The executable syscall registry lives in `src/sys/aivm_syscall_contracts.c`.
That table is the implementation source for IDs, arity, return type, and
capability metadata. This specification defines the rules that table must obey.

A conforming AiVM build must keep these in sync:

- `SPEC/SYSCALLS.md`
- `src/sys/aivm_syscall_contracts.c`
- `tests/unit/syscalls/test_syscall_contracts.c`
- `scripts/check-syscall-contracts.sh`

## Host-Boundary Categories

The current syscall surface includes host-boundary targets for:

- console I/O
- process state and child process execution
- filesystem access
- network access
- host platform/runtime identity
- host time and timezone data
- UI/window/rendering resources
- worker execution resources
- remote bridge calls
- host-default handlers
- image decoding
- app storage and secure storage
- debug/profiling/capture artifacts

A syscall that does not cross one of these boundaries must be removed or moved
into AiLang libraries, optional packages, or intrinsic bytecode operations.

## Removed Deterministic Utilities

Deterministic text, bytes, base64, hash, and HMAC helpers are not public VM
syscalls. They belong in AiLang libraries, packages, or intrinsic bytecode when
there is a measured runtime reason. `sys.crypto.randomBytes` remains a syscall
because it reads host entropy.

## Production Readiness Rules

Before the production VM is release-candidate ready:

- Document that OS users, containers, app sandboxes, and deployment environments
  are the production security boundary.
- Distinguish unknown, known-but-unbound, invalid-argument, timeout,
  resource-limit, and host-failure errors.
- Replace all-at-once host operations with bounded primitives first, then add
  resource limits for filesystem, process, network, worker, UI, and debug
  artifact operations.
- Separate debug-only syscalls from production `aivm`; bind them from
  `aivm-debug` where appropriate.
- Use syscall capability groups as the policy metadata for explicit allow/deny
  controls.
- Move deterministic library candidates into AiLang or document why a specific
  target must remain host-provided.

Future sandboxing may extend the current debug-capture `--allow` and `--deny`
flags to other command surfaces, but that is not the current production
baseline.

## Stability Rules

- Do not reuse a syscall ID for a different target.
- Do not change target arity or return type without updating this spec, contract
  tests, and compatibility notes.
- Known-but-unbound syscalls fail deterministically with `AIVMS006`.
- Capability-denied syscalls fail deterministically with `AIVMS008`.
- Resource-limit failures fail deterministically with `AIVMS007`.
