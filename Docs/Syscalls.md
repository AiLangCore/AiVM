# AiVM Syscalls

Syscalls are the only permitted escape hatch from deterministic VM execution.
They exist to cross from AiBC execution into explicit host-provided
capabilities.

## Addition Rule

A new `sys.*` target may only be added when all of these are true:

- It crosses a host boundary: OS, filesystem, process, time, network, UI,
  device, native adapter, debug/profiling host, or external resource.
- It cannot be implemented deterministically in AiLang or AiLang core
  libraries.
- It has an explicit contract entry in `native/sys/aivm_syscall_contracts.c`.
- It has contract tests in `native/tests/test_syscall_contracts.c`.
- It is listed in this document with a host-boundary justification.
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
Security hardening should come from OS permissions, resource limits, deployment
sandboxes, and future explicit runtime profiles, not from treating process
execution as debug-only.

This classification is the starting point for production hardening. It does not
freeze the surface. Until a major or minor release is official, review/remove
items should be moved, renamed, or deleted completely rather than preserved
through compatibility layers.

### Host-Boundary

These targets cross an OS, process, filesystem, network, UI, worker, host,
debug, or external-resource boundary. They may remain syscalls if resource
limits and error behavior are defined. AiVM uses the normal runtime model for
now: these operations run with the OS/process permissions of the host process.

| Target | Capability | Justification |
| --- | --- | --- |
| `sys.console.write` | `console.write` | Writes to host console output. |
| `sys.console.writeLine` | `console.write` | Writes to host console output. |
| `sys.console.writeErrLine` | `console.write` | Writes to host console error output. |
| `sys.console.readLine` | `console.read` | Reads host stdin. |
| `sys.console.readAllStdin` | `console.read` | Reads host stdin. |
| `sys.stdout.writeLine` | `console.write` | Writes to host stdout. |
| `sys.process.cwd` | `process.env` | Reads host process state. |
| `sys.process.env.get` | `process.env` | Reads host environment state. |
| `sys.process.args` | `process.args` | Reads host-provided process arguments. |
| `sys.process.exit` | `process.exit` | Requests host process termination. |
| `sys.process.spawn` | `process.spawn` | Creates host processes. |
| `sys.process.wait` | `process.spawn` | Observes host process state. |
| `sys.process.kill` | `process.spawn` | Mutates host process state. |
| `sys.process.stdout.read` | `process.spawn` | Reads host process pipe output. |
| `sys.process.stderr.read` | `process.spawn` | Reads host process pipe output. |
| `sys.process.poll` | `process.spawn` | Observes host process state. |
| `sys.remote.call` | `remote` | Crosses to a host remote bridge. |
| `sys.host.openDefault` | `host.open` | Opens a host-default handler outside the VM. |
| `sys.image.decodeToRgbaBase64` | `image.decode` | Uses host image decoding for external resource bytes. |
| `sys.platform` | `host.info` | Reads host platform identity. |
| `sys.arch` | `host.info` | Reads host architecture identity. |
| `sys.os.version` | `host.info` | Reads host OS version. |
| `sys.runtime` | `host.info` | Reads host runtime identity. |
| `sys.time.nowUnixMs` | `time.wall` | Reads host wall-clock time. |
| `sys.time.monotonicMs` | `time.monotonic` | Reads host monotonic time. |
| `sys.time.sleepMs` | `time.sleep` | Blocks or schedules against host time. |
| `sys.time.timeZoneId` | `time.zone` | Reads host timezone configuration. |
| `sys.time.timeZoneOffsetMinutesAt` | `time.zone` | Reads host timezone rules. |
| `sys.fs.file.read` | `fs.read` | Reads host filesystem content. |
| `sys.fs.file.exists` | `fs.read` | Observes host filesystem state. |
| `sys.fs.dir.list` | `fs.read` | Reads host directory state. |
| `sys.fs.path.stat` | `fs.read` | Reads host filesystem metadata. |
| `sys.fs.path.exists` | `fs.read` | Observes host filesystem state. |
| `sys.fs.file.write` | `fs.write` | Writes host filesystem content. |
| `sys.fs.dir.create` | `fs.write` | Mutates host filesystem state. |
| `sys.fs.file.delete` | `fs.write` | Mutates host filesystem state. |
| `sys.fs.dir.delete` | `fs.write` | Mutates host filesystem state. |
| `sys.crypto.randomBytes` | `random` | Reads host entropy. |
| `sys.net.tcp.connect` | `network` | Opens host network connections. |
| `sys.net.tcp.listen` | `network` | Opens host network listeners. |
| `sys.net.tcp.listenTls` | `network` | Opens host TLS network listeners. |
| `sys.net.tcp.accept` | `network` | Accepts host network connections. |
| `sys.net.tcp.read` | `network` | Reads host network bytes. |
| `sys.net.tcp.write` | `network` | Writes host network bytes. |
| `sys.net.tcp.connectTls` | `network` | Opens host TLS network connections. |
| `sys.net.tcp.connectStart` | `network` | Starts host async network work. |
| `sys.net.tcp.connectTlsStart` | `network` | Starts host async TLS network work. |
| `sys.net.tcp.readStart` | `network` | Starts host async network reads. |
| `sys.net.tcp.writeStart` | `network` | Starts host async network writes. |
| `sys.net.async.poll` | `network` | Observes host async network work. |
| `sys.net.async.await` | `network` | Waits on host async network work. |
| `sys.net.async.cancel` | `network` | Cancels host async network work. |
| `sys.net.async.resultInt` | `network` | Reads host async network result state. |
| `sys.net.async.resultBytes` | `network` | Reads host async network result bytes. |
| `sys.net.async.error` | `network` | Reads host async network errors. |
| `sys.net.udp.bind` | `network` | Opens host UDP sockets. |
| `sys.net.udp.recv` | `network` | Reads host UDP packets. |
| `sys.net.udp.send` | `network` | Writes host UDP packets. |
| `sys.ui.createWindow` | `ui` | Creates host UI resources. |
| `sys.ui.beginFrame` | `ui` | Begins host UI drawing. |
| `sys.ui.drawRect` | `ui` | Draws through host UI backend. |
| `sys.ui.drawText` | `ui` | Draws through host UI backend. |
| `sys.ui.endFrame` | `ui` | Ends host UI drawing. |
| `sys.ui.pollEvent` | `ui` | Reads host UI input events. |
| `sys.ui.present` | `ui` | Presents host UI output. |
| `sys.ui.closeWindow` | `ui` | Mutates host UI resources. |
| `sys.ui.drawLine` | `ui` | Draws through host UI backend. |
| `sys.ui.drawEllipse` | `ui` | Draws through host UI backend. |
| `sys.ui.drawPath` | `ui` | Draws through host UI backend. |
| `sys.ui.drawImage` | `ui` | Draws through host UI backend. |
| `sys.ui.getWindowSize` | `ui` | Reads host UI state. |
| `sys.ui.waitFrame` | `ui` | Waits on host UI frame timing. |
| `sys.worker.start` | `worker` | Starts host worker execution. |
| `sys.worker.poll` | `worker` | Observes host worker state. |
| `sys.worker.result` | `worker` | Reads host worker result. |
| `sys.worker.error` | `worker` | Reads host worker error. |
| `sys.worker.cancel` | `worker` | Cancels host worker execution. |
| `sys.debug.emit` | `debug` | Emits host debug/profiling data. |
| `sys.debug.mode` | `debug` | Reads host debug runtime mode. |
| `sys.debug.captureFrameBegin` | `debug` | Records host debug frame state. |
| `sys.debug.captureFrameEnd` | `debug` | Records host debug frame state. |
| `sys.debug.captureDraw` | `debug` | Records host debug draw state. |
| `sys.debug.captureInput` | `debug` | Records host debug input state. |
| `sys.debug.captureState` | `debug` | Records host debug state. |
| `sys.debug.replayLoad` | `debug` | Loads host debug replay state. |
| `sys.debug.replayNext` | `debug` | Reads host debug replay state. |
| `sys.debug.assert` | `debug` | Emits host debug assertion output. |
| `sys.debug.artifactWrite` | `debug` | Writes host debug artifacts. |
| `sys.debug.traceAsync` | `debug` | Emits host debug async trace data. |
| `sys.debug.taskReclaimStats` | `debug` | Reads host runtime debug counters. |

### Deterministic Library Candidates

These targets are deterministic utility operations. They should be reviewed for
movement into AiLang core libraries before a major or minor release. Keeping one
as a syscall requires a written host-boundary reason stronger than convenience.

| Target | Proposed owner | Reason |
| --- | --- | --- |
| `sys.crypto.base64Encode` | AiLang core library | Deterministic encoding. |
| `sys.crypto.base64Decode` | AiLang core library | Deterministic decoding. |
| `sys.crypto.sha1` | AiLang core library | Deterministic hashing. |
| `sys.crypto.sha256` | AiLang core library | Deterministic hashing. |
| `sys.crypto.hmacSha256` | AiLang core library | Deterministic hashing. |
| `sys.str.utf8ByteCount` | AiLang core library | Deterministic string inspection. |
| `sys.str.substring` | AiLang core library | Deterministic string transformation. |
| `sys.str.remove` | AiLang core library | Deterministic string transformation. |
| `sys.str.find` | AiLang core library | Deterministic string search. |
| `sys.str.fromCodePoint` | AiLang core library | Deterministic string construction. |
| `sys.str.decodeUnicodeHex4` | AiLang core library | Deterministic string decoding. |
| `sys.str.decodeUnicodeSurrogatePairHex4` | AiLang core library | Deterministic string decoding. |
| `sys.bytes.length` | AiLang core library | Deterministic byte inspection. |
| `sys.bytes.fromBase64` | AiLang core library | Deterministic decoding. |
| `sys.bytes.toBase64` | AiLang core library | Deterministic encoding. |
| `sys.bytes.at` | AiLang core library | Deterministic byte inspection. |
| `sys.bytes.slice` | AiLang core library | Deterministic byte transformation. |
| `sys.bytes.concat` | AiLang core library | Deterministic byte transformation. |
| `sys.bytes.toUtf8String` | AiLang core library | Deterministic decoding. |
| `sys.bytes.fromUtf8String` | AiLang core library | Deterministic encoding. |

### Production Hardening Work

Before the production VM is sponsorship-ready:

- Document that OS users, containers, app sandboxes, and deployment environments
  are the production security boundary.
- Distinguish unknown, known-but-unbound, invalid-argument, timeout,
  resource-limit, and host-failure errors.
- Add resource limits for filesystem, process, network, worker, UI, and debug
  artifact operations.
- Separate debug-only syscalls from production `aivm`; bind them from
  `aivm-debug` where appropriate.
- Move deterministic library candidates into AiLang or document why a specific
  target must remain host-provided.

Optional future sandboxing may add explicit capability groups and `--allow-*`
or `--deny-*` flags, but that is not the current production baseline.

## Current Syscalls

| ID | Target | Args | Returns |
| --- | --- | ---: | --- |
| 5 | `sys.net.tcp.close` | 1 | `void` |
| 6 | `sys.console.write` | 1 | `void` |
| 7 | `sys.console.writeLine` | 1 | `void` |
| 8 | `sys.console.readLine` | 0 | `string` |
| 9 | `sys.console.readAllStdin` | 0 | `string` |
| 10 | `sys.console.writeErrLine` | 1 | `void` |
| 16 | `sys.stdout.writeLine` | 1 | `void` |
| 11 | `sys.process.cwd` | 0 | `string` |
| 12 | `sys.process.env.get` | 1 | `string` |
| 18 | `sys.process.args` | 0 | `node` |
| 105 | `sys.process.spawn` | 4 | `int` |
| 106 | `sys.process.wait` | 1 | `int` |
| 107 | `sys.process.kill` | 1 | `bool` |
| 108 | `sys.process.stdout.read` | 1 | `bytes` |
| 109 | `sys.process.stderr.read` | 1 | `bytes` |
| 110 | `sys.process.poll` | 1 | `int` |
| 117 | `sys.remote.call` | 3 | `int` |
| 120 | `sys.host.openDefault` | 1 | `bool` |
| 121 | `sys.image.decodeToRgbaBase64` | 2 | `string` |
| 28 | `sys.platform` | 0 | `string` |
| 29 | `sys.arch` | 0 | `string` |
| 30 | `sys.os.version` | 0 | `string` |
| 31 | `sys.runtime` | 0 | `string` |
| 13 | `sys.time.nowUnixMs` | 0 | `int` |
| 14 | `sys.time.monotonicMs` | 0 | `int` |
| 15 | `sys.time.sleepMs` | 1 | `void` |
| 122 | `sys.time.timeZoneId` | 0 | `string` |
| 123 | `sys.time.timeZoneOffsetMinutesAt` | 1 | `int` |
| 17 | `sys.process.exit` | 1 | `void` |
| 19 | `sys.fs.file.read` | 1 | `bytes` |
| 20 | `sys.fs.file.exists` | 1 | `bool` |
| 21 | `sys.fs.dir.list` | 1 | `node` |
| 22 | `sys.fs.path.stat` | 1 | `node` |
| 23 | `sys.fs.path.exists` | 1 | `bool` |
| 24 | `sys.fs.file.write` | 2 | `void` |
| 25 | `sys.fs.dir.create` | 1 | `void` |
| 103 | `sys.fs.file.delete` | 1 | `bool` |
| 104 | `sys.fs.dir.delete` | 2 | `bool` |
| 37 | `sys.crypto.base64Encode` | 1 | `string` |
| 38 | `sys.crypto.base64Decode` | 1 | `string` |
| 39 | `sys.crypto.sha1` | 1 | `string` |
| 40 | `sys.crypto.sha256` | 1 | `string` |
| 41 | `sys.crypto.hmacSha256` | 2 | `string` |
| 42 | `sys.crypto.randomBytes` | 1 | `bytes` |
| 27 | `sys.net.tcp.connect` | 2 | `int` |
| 32 | `sys.net.tcp.listen` | 2 | `int` |
| 33 | `sys.net.tcp.listenTls` | 4 | `int` |
| 34 | `sys.net.tcp.accept` | 1 | `int` |
| 35 | `sys.net.tcp.read` | 2 | `bytes` |
| 36 | `sys.net.tcp.write` | 2 | `int` |
| 61 | `sys.net.tcp.connectTls` | 2 | `int` |
| 62 | `sys.net.tcp.connectStart` | 2 | `int` |
| 63 | `sys.net.tcp.connectTlsStart` | 2 | `int` |
| 64 | `sys.net.tcp.readStart` | 2 | `int` |
| 65 | `sys.net.tcp.writeStart` | 2 | `int` |
| 66 | `sys.net.async.poll` | 1 | `int` |
| 67 | `sys.net.async.await` | 1 | `int` |
| 68 | `sys.net.async.cancel` | 1 | `bool` |
| 69 | `sys.net.async.resultInt` | 1 | `int` |
| 70 | `sys.net.async.resultBytes` | 1 | `bytes` |
| 71 | `sys.net.async.error` | 1 | `string` |
| 43 | `sys.net.udp.bind` | 2 | `int` |
| 44 | `sys.net.udp.recv` | 2 | `node` |
| 45 | `sys.net.udp.send` | 4 | `int` |
| 46 | `sys.ui.createWindow` | 3 | `int` |
| 47 | `sys.ui.beginFrame` | 1 | `void` |
| 48 | `sys.ui.drawRect` | 6 | `void` |
| 49 | `sys.ui.drawText` | 6 | `void` |
| 50 | `sys.ui.endFrame` | 1 | `void` |
| 51 | `sys.ui.pollEvent` | 1 | `node` |
| 52 | `sys.ui.present` | 1 | `void` |
| 53 | `sys.ui.closeWindow` | 1 | `void` |
| 54 | `sys.ui.drawLine` | 7 | `void` |
| 55 | `sys.ui.drawEllipse` | 6 | `void` |
| 56 | `sys.ui.drawPath` | 4 | `void` |
| 57 | `sys.ui.drawImage` | 6 | `void` |
| 58 | `sys.ui.getWindowSize` | 1 | `node` |
| 72 | `sys.ui.waitFrame` | 1 | `void` |
| 73 | `sys.worker.start` | 2 | `int` |
| 74 | `sys.worker.poll` | 1 | `int` |
| 75 | `sys.worker.result` | 1 | `string` |
| 76 | `sys.worker.error` | 1 | `string` |
| 77 | `sys.worker.cancel` | 1 | `bool` |
| 78 | `sys.debug.emit` | 2 | `void` |
| 79 | `sys.debug.mode` | 0 | `string` |
| 80 | `sys.debug.captureFrameBegin` | 3 | `void` |
| 81 | `sys.debug.captureFrameEnd` | 1 | `void` |
| 82 | `sys.debug.captureDraw` | 2 | `void` |
| 83 | `sys.debug.captureInput` | 1 | `void` |
| 84 | `sys.debug.captureState` | 2 | `void` |
| 85 | `sys.debug.replayLoad` | 1 | `int` |
| 86 | `sys.debug.replayNext` | 1 | `string` |
| 87 | `sys.debug.assert` | 3 | `void` |
| 88 | `sys.debug.artifactWrite` | 2 | `bool` |
| 89 | `sys.debug.traceAsync` | 3 | `void` |
| 26 | `sys.str.utf8ByteCount` | 1 | `int` |
| 59 | `sys.str.substring` | 3 | `string` |
| 60 | `sys.str.remove` | 3 | `string` |
| 116 | `sys.str.find` | 3 | `int` |
| 111 | `sys.str.fromCodePoint` | 1 | `string` |
| 112 | `sys.str.decodeUnicodeHex4` | 1 | `string` |
| 113 | `sys.str.decodeUnicodeSurrogatePairHex4` | 2 | `string` |
| 97 | `sys.bytes.length` | 1 | `int` |
| 98 | `sys.bytes.fromBase64` | 1 | `bytes` |
| 99 | `sys.bytes.toBase64` | 1 | `string` |
| 100 | `sys.bytes.at` | 2 | `int` |
| 101 | `sys.bytes.slice` | 3 | `bytes` |
| 102 | `sys.bytes.concat` | 2 | `bytes` |
| 114 | `sys.bytes.toUtf8String` | 1 | `string` |
| 118 | `sys.bytes.fromUtf8String` | 1 | `bytes` |
| 115 | `sys.debug.taskReclaimStats` | 0 | `node` |

## Review Notes

Several current contracts are deterministic utility-style operations, especially
`sys.str.*` and `sys.bytes.*`. They are listed here because they currently
exist, not because they are automatically endorsed as permanent VM syscalls.
Before the first major or minor release, each one should be reviewed and either
kept with explicit host-boundary justification or moved into AiLang core
libraries.
