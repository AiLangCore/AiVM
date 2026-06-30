# AiVM Host ABI

Status: required contract before target repositories move to independent
release cycles.

AiVM is platform neutral. Targets integrate AiVM with operating systems,
devices, browsers, emulators, or embedded profiles through a host ABI and
target-owned host libraries.

## Ownership

AiVM owns:

- `aivm` executable
- `libaivm_core.a`
- VM execution APIs
- syscall binding contract types
- host event queue adapter APIs
- host ABI headers and versioning

Target repositories own:

- host library implementation
- platform syscall bindings
- platform input/output adapters
- packaging and deployment tools
- target-specific CI/CD

AiVM must not own platform target implementations such as macOS bundles, Windows
installers, Linux desktop shells, browser launchers, or AiOS QEMU profiles.

## Deliverables

AiVM release artifacts must include:

```text
aivm
libaivm_core.a
include/
  aivm_c_api.h
  aivm_runtime.h
  aivm_vm.h
  sys/aivm_syscall.h
  sys/aivm_syscall_contracts.h
```

The final header list may expand, but target repositories must be able to build
host libraries without copying private VM source files.

## Host Library Shape

Each official target supplies one or more host libraries:

```text
libaivm_host_windows.a
libaivm_host_macos.a
libaivm_host_linux.a
libaivm_host_wasm.a
libaivm_host_aios.a
```

Host libraries link against `libaivm_core.a` or embed it according to the
target package's publish strategy. They must implement only mechanical host
behavior:

- syscall binding registration
- filesystem/process/network/time/device calls justified by syscall contracts
- UI/input/window/framebuffer/browser integration where required
- deterministic event queue enqueue/drain integration
- target runtime startup and shutdown plumbing

Host libraries must not implement language semantics, compiler behavior,
validation behavior, formatting behavior, package semantics, UI widget
semantics, or application lifecycle semantics.

## ABI Versioning

AiVM exposes a numeric ABI version through:

```text
aivm_c_abi_version()
```

Target packages must declare the ABI version they require. Publish and doctor
tools must fail deterministically when the installed AiVM core ABI is
incompatible with the target host library.

Patch releases may fix ABI bugs without changing the ABI number. Any breaking
ABI change requires a minor or major release and a target package update.

## Runtime Boundary

The build runtime and target runtime are separate:

- Build runtime runs the self-hosted compiler and package tooling on the local
  development or CI host.
- Target runtime is the distributable combination of VM core, target host
  library, application bytecode/bundles, assets, and platform metadata.

Target repositories may use the build runtime to produce artifacts, but target
artifacts must contain only runtime files required by the selected target.

## Verification Requirements

Before target repositories are independently released, AiVM must provide tests
that prove:

- public headers are sufficient to build a minimal host library
- syscall binding tables can be supplied by a target host
- host event queue enqueue/drain behavior is deterministic
- incompatible ABI versions fail deterministically
- `libaivm_core.a` links without platform host source

Core golden tests remain authoritative for VM behavior. Target repositories add
their own CI for platform launch, packaging, QEMU/device/browser execution, and
host-library integration.
