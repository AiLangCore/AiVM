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
  aivm_host_abi.h
  aivm_c_api.h
  aivm_runtime.h
  aivm_vm.h
  sys/aivm_syscall.h
  sys/aivm_syscall_contracts.h
```

The final header list may expand, but target repositories must be able to build
host libraries without copying private VM source files.

`aivm_host_abi.h` is the target-facing entrypoint. It exposes:

- `AIVM_HOST_ABI_VERSION`
- `AIVM_HOST_ABI_MIN_COMPATIBLE_VERSION`
- `AivmHostAbiDescriptor`
- `aivm_host_abi_version()`
- `aivm_host_abi_check_compatible(...)`
- `aivm_host_abi_compatibility_code(...)`

Target hosts should declare one descriptor per host/profile runtime. The
descriptor identifies the target, host implementation name, required core ABI,
syscall binding table, and optional deterministic event adapter.

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

Compatibility status codes:

| Status | Code | Meaning |
| --- | --- | --- |
| `AIVM_HOST_ABI_COMPAT_OK` | `AIVMHOST000` | Host descriptor is compatible with the core ABI. |
| `AIVM_HOST_ABI_COMPAT_INVALID` | `AIVMHOST001` | Host descriptor is missing required identity fields or ABI versions. |
| `AIVM_HOST_ABI_COMPAT_CORE_TOO_OLD` | `AIVMHOST002` | Installed core ABI is older than the target host requires. |
| `AIVM_HOST_ABI_COMPAT_CORE_TOO_NEW` | `AIVMHOST003` | Installed core ABI is newer than the target host was built for. |
| unknown | `AIVMHOST999` | Unknown compatibility status. |

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

- [x] public headers are sufficient to build a minimal host library
- [x] syscall binding tables can be supplied by a target host
- [x] host event queue enqueue/drain behavior is deterministic
- [x] incompatible ABI versions fail deterministically
- `libaivm_core.a` links without platform host source

Core golden tests remain authoritative for VM behavior. Target repositories add
their own CI for platform launch, packaging, QEMU/device/browser execution, and
host-library integration.

The current contract test is `aivm_test_host_abi`. It compiles a small
target-style host using public headers only, registers a syscall table, uses the
host event adapter helpers, validates ABI compatibility failures, and executes a
program through the public C API.

## Migration Order

1. Keep platform host source in AiVM temporarily while target packages stabilize.
2. Move one host at a time into its target repository behind
   `AivmHostAbiDescriptor`.
3. Target package `doctor` checks `aivm_c_abi_version()` against its descriptor.
4. Target package `publish/run` links `libaivm_core.a` with the target-owned
   host library.
5. Remove the matching platform host source from AiVM after the target repo CI
   proves equivalent behavior.
