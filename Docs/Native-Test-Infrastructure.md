# Native C Test Infrastructure

AiVM uses CTest as the native test runner. Native unit tests verify mechanical
implementation behavior in the C VM; they do not replace the golden/parity
tests that define observable runtime behavior.

## Authority

Golden tests remain authoritative for behavior:

- `src/tests/parity_cases/`
- `src/tests/parity_commands*.txt`

Native C unit tests are implementation guards. They should cover VM mechanics,
memory ownership, bytecode loading, syscall contracts, and host-boundary
plumbing. If a C unit test disagrees with a golden behavior test, the golden
test owns the observable contract unless the spec and golden test are being
changed deliberately.

## Layout

```text
src/tests/
  vm/        VM execution, diagnostics, runtime adapter, C API, remote transport
  memory/    reference counting, cycles, artifact budgets, memory limits
  bytecode/  program loading, value representation, shared bridge loading
  syscalls/  syscall dispatch, contracts, host-boundary implementations
  stdlib/    native bridge/package manager tests and parity harness glue
```

Golden fixtures and CTest shell wrappers remain at `src/tests/` so parity
commands keep stable paths.

## Labels

CTest labels are the supported way to run focused native checks:

```bash
ctest --test-dir .tmp/aivm-c-build-native -L unit --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L vm --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L memory --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L bytecode --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L syscalls --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L stdlib --output-on-failure
```

`./test-aivm-c.sh` remains the repository-level verification entrypoint.

## Unit Framework

Tests are plain C executables today to keep the bootstrap dependency surface
small. Criterion can be introduced later for richer assertions and fixtures, but
it should remain optional until it is available cleanly across the supported CI
targets.
