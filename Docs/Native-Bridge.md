# AiLang Native Bridge

Status: alpha intermediary contract.

The native bridge is a temporary C library boundary for host/native
functionality that AiLang cannot implement yet during self-hosting.

It is also the long-term shape for wrapping native libraries. Native packages
should expose a small C wrapper and register functions with the bridge instead
of adding VM syscalls for library-level behavior.

## Ownership

- AiVM owns the bridge ABI and execution boundary.
- AiLang packages may contain native wrapper source/artifacts.
- AiLang libraries own the high-level API over a native wrapper.

## Rules

- Do not add a `sys.*` syscall for normal library behavior.
- Use the native bridge when behavior genuinely crosses into native host code
  or a native third-party library.
- Native functions must be explicitly registered by name.
- Duplicate native function names fail registration.
- Native wrappers must expose deterministic AiLang-facing behavior or surface
  typed errors.
- Build and publish must include only referenced native wrappers and only for
  the selected target.

## ABI Surface

Current C header:

```text
src/include/ailang_native_bridge.h
```

Current implementation:

```text
src/ailang_native_bridge.c
```

Supported value types:

- null
- bool
- int
- string
- bytes

The bridge intentionally starts small. Structured values should be represented
above this layer in AiLang unless a native wrapper has absolute justification
for accepting or returning bytes.

## Self-Hosting Use

During self-hosting, package management and compiler bootstrap helpers may use
this bridge as a temporary C-backed implementation. The target end state is
still AiLang-authored package/compiler logic running on AiVM.

Current bridge-backed alpha helper:

- `package.list`
- `package.restore`

The native CLI calls these through the bridge for:

```bash
ailang package list [project-dir]
ailang package restore [project-dir]
```
