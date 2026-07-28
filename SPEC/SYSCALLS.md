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

## Current Contract Inventory

This inventory is the normative, human-readable list of registered syscall
targets. The C contract table remains the executable source for numeric IDs,
arity, value types, and capability metadata. The release verifier requires the
two to remain in sync.

| ID | Target |
| --- | --- |
| 5 | `sys.net.tcp.close` |
| 6 | `sys.console.write` |
| 7 | `sys.console.writeLine` |
| 8 | `sys.console.readLine` |
| 9 | `sys.console.readAllStdin` |
| 10 | `sys.console.writeErrLine` |
| 16 | `sys.stdout.writeLine` |
| 11 | `sys.process.cwd` |
| 12 | `sys.process.env.get` |
| 18 | `sys.process.args` |
| 105 | `sys.process.spawn` |
| 106 | `sys.process.wait` |
| 107 | `sys.process.kill` |
| 108 | `sys.process.stdout.read` |
| 109 | `sys.process.stderr.read` |
| 110 | `sys.process.poll` |
| 117 | `sys.remote.call` |
| 120 | `sys.host.openDefault` |
| 121 | `sys.image.decodeToRgbaBase64` |
| 28 | `sys.platform` |
| 29 | `sys.arch` |
| 30 | `sys.os.version` |
| 31 | `sys.runtime` |
| 13 | `sys.time.nowUnixMs` |
| 14 | `sys.time.monotonicMs` |
| 15 | `sys.time.sleepMs` |
| 122 | `sys.time.timeZoneId` |
| 123 | `sys.time.timeZoneOffsetMinutesAt` |
| 17 | `sys.process.exit` |
| 19 | `sys.fs.file.read` |
| 20 | `sys.fs.file.exists` |
| 21 | `sys.fs.dir.list` |
| 22 | `sys.fs.path.stat` |
| 23 | `sys.fs.path.exists` |
| 24 | `sys.fs.file.write` |
| 25 | `sys.fs.dir.create` |
| 103 | `sys.fs.file.delete` |
| 104 | `sys.fs.dir.delete` |
| 124 | `sys.fs.file.openRead` |
| 125 | `sys.fs.file.readChunk` |
| 126 | `sys.fs.file.close` |
| 127 | `sys.fs.file.openWrite` |
| 128 | `sys.fs.file.writeChunk` |
| 42 | `sys.crypto.randomBytes` |
| 27 | `sys.net.tcp.connect` |
| 32 | `sys.net.tcp.listen` |
| 33 | `sys.net.tcp.listenTls` |
| 34 | `sys.net.tcp.accept` |
| 35 | `sys.net.tcp.read` |
| 36 | `sys.net.tcp.write` |
| 61 | `sys.net.tcp.connectTls` |
| 62 | `sys.net.tcp.connectStart` |
| 63 | `sys.net.tcp.connectTlsStart` |
| 64 | `sys.net.tcp.readStart` |
| 65 | `sys.net.tcp.writeStart` |
| 66 | `sys.net.async.poll` |
| 67 | `sys.net.async.await` |
| 68 | `sys.net.async.cancel` |
| 69 | `sys.net.async.resultInt` |
| 70 | `sys.net.async.resultBytes` |
| 71 | `sys.net.async.error` |
| 43 | `sys.net.udp.bind` |
| 44 | `sys.net.udp.recv` |
| 45 | `sys.net.udp.send` |
| 46 | `sys.ui.createWindow` |
| 47 | `sys.ui.beginFrame` |
| 48 | `sys.ui.drawRect` |
| 49 | `sys.ui.drawText` |
| 50 | `sys.ui.endFrame` |
| 51 | `sys.ui.pollEvent` |
| 52 | `sys.ui.present` |
| 53 | `sys.ui.closeWindow` |
| 54 | `sys.ui.drawLine` |
| 55 | `sys.ui.drawEllipse` |
| 56 | `sys.ui.drawPath` |
| 57 | `sys.ui.drawImage` |
| 58 | `sys.ui.getWindowSize` |
| 130 | `sys.runtime.platform` |
| 131 | `sys.runtime.target` |
| 132 | `sys.storage.local.available` |
| 133 | `sys.storage.local.get` |
| 134 | `sys.storage.local.set` |
| 135 | `sys.storage.local.delete` |
| 136 | `sys.storage.local.exists` |
| 137 | `sys.storage.secure.available` |
| 138 | `sys.storage.secure.get` |
| 139 | `sys.storage.secure.set` |
| 140 | `sys.storage.secure.delete` |
| 141 | `sys.storage.secure.exists` |
| 142 | `sys.ui.pushClipPath` |
| 143 | `sys.ui.popClipPath` |
| 129 | `sys.ui.measureText` |
| 72 | `sys.ui.waitFrame` |
| 73-77 | Reserved after removal of the beta string-name worker simulation |
| 78 | `sys.debug.emit` |
| 79 | `sys.debug.mode` |
| 80 | `sys.debug.captureFrameBegin` |
| 81 | `sys.debug.captureFrameEnd` |
| 82 | `sys.debug.captureDraw` |
| 83 | `sys.debug.captureInput` |
| 84 | `sys.debug.captureState` |
| 85 | `sys.debug.replayLoad` |
| 86 | `sys.debug.replayNext` |
| 87 | `sys.debug.assert` |
| 88 | `sys.debug.artifactWrite` |
| 89 | `sys.debug.traceAsync` |
| 115 | `sys.debug.taskReclaimStats` |

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

## Worker Scheduler Boundary

Worker invocation is not selected by a host path, package path, task-name
string, or raw function-name string. The AiBC1 loader validates an embedded
worker catalog and creates opaque WorkerRef capabilities. Task creation,
dependency readiness, Await, cancellation, and workload indexing are VM
operations over opaque values rather than public physical-worker syscalls.

The removed `sys.worker.start/poll/result/error/cancel` family must not be used
as a compatibility path. Normal AiLang code uses `std.worker` and `std.task`.
Any internal host adapter used by the scheduler is private mechanical runtime
plumbing and is not an AiLang semantic API.

AiVM may choose threads, processes, or another isolated mechanism. It performs
no package resolution, filesystem artifact search, compiler selection,
function-name lookup, payload interpretation, validation policy, linker
behavior, diagnostic selection, or canonical ordering.
