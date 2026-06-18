# Native C Test Infrastructure

AiVM uses CTest as the native test runner. Native unit tests verify mechanical
implementation behavior in the C VM; they do not replace the golden/parity
tests that define observable runtime behavior.

## Authority

Golden tests remain authoritative for behavior:

- `tests/golden/parity_cases/`
- `tests/golden/parity_commands*.txt`

Native C unit tests are implementation guards. They should cover VM mechanics,
memory ownership, bytecode loading, syscall contracts, and host-boundary
plumbing. If a C unit test disagrees with a golden behavior test, the golden
test owns the observable contract unless the spec and golden test are being
changed deliberately.

## Layout

```text
tests/
  unit/         isolated mechanical checks for VM, memory, bytecode, syscalls
  integration/ subsystem interaction checks and host-boundary smoke tests
  fuzz/         deterministic fuzz-smoke targets and future libFuzzer harnesses
  stress/       long-running and repeated-operation abuse tests
  golden/       authoritative deterministic behavior fixtures
  security/     hostile input and fail-safe validation checks
```

Component subdirectories under `tests/unit/` and `tests/integration/` keep the
VM, memory, bytecode, syscall, and stdlib ownership boundaries visible.

## Labels

CTest labels are the supported way to run focused native checks:

```bash
ctest --test-dir .tmp/aivm-c-build-native -L unit --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L vm --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L memory --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L bytecode --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L syscalls --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L stdlib --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L golden --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L security --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L fuzz --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L stress --output-on-failure
```

`./test-aivm-c.sh` remains the repository-level verification entrypoint.

Default local verification runs `unit|integration|golden|security`. Fuzz and
stress labels are available locally and are intended for nightly or explicit
long-running validation.

## Sanitizers

CMake exposes sanitizer toggles:

- `AIVM_ENABLE_ASAN`
- `AIVM_ENABLE_UBSAN`
- `AIVM_ENABLE_LSAN`
- `AIVM_ENABLE_MSAN`

ASAN and UBSAN are required RC-1 CI gates. LSAN and MSAN are available where the
host compiler and platform support them.

## Unit Framework

Tests are plain C executables today to keep the bootstrap dependency surface
small. Criterion can be introduced later for richer assertions and fixtures, but
it should remain optional until it is available cleanly across the supported CI
targets.
