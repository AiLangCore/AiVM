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
  fuzz/         deterministic corpus/mutation targets and future libFuzzer harnesses
  stress/       long-running and repeated-operation abuse tests
  perf/         benchmark and performance regression checks
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
ctest --test-dir .tmp/aivm-c-build-native -L perf-smoke --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L perf-full --output-on-failure
```

`./test-aivm-c.sh` remains the repository-level verification entrypoint.

Default local verification runs `unit|integration|golden|security`. Fuzz and
stress labels are available locally and are intended for nightly or explicit
long-running validation.

## Fuzz And Stress Budgets

The current fuzz target is a deterministic loader corpus/mutation harness. It
does not replace libFuzzer or AFL++, but it already abuses malformed AiBC
headers, section counts, section sizes, instruction records, constant records,
truncated buffers, and mutations of valid programs. The primary assertion is
that every input either loads into a bounded, internally consistent program or
fails with a known deterministic loader status. Sanitizers turn this into a
memory-safety regression gate.

The current stress target repeatedly loads and clears valid AiBC programs of
varying sizes, then repeatedly executes a stack-churn VM program and forces
periodic safe-point collection. This is meant to catch allocator, arena,
cleanup, reinitialization, and execution-loop regressions.

Local defaults are intentionally bounded. Override them when intentionally
thrashing the runtime:

```bash
AIVM_FUZZ_ITERATIONS=50000 \
  ctest --test-dir .tmp/aivm-c-build-native -L fuzz --output-on-failure

AIVM_STRESS_ITERATIONS=100000 \
AIVM_STRESS_VM_ITERATIONS=25000 \
  ctest --test-dir .tmp/aivm-c-build-native -L stress --output-on-failure
```

Nightly CI uses elevated budgets. RC hardening should also run these labels
under ASAN and UBSAN before release. For interactive sanitizer checks, use a
smaller VM stress budget because ASAN deliberately slows repeated arena resets:

```bash
AIVM_FUZZ_ITERATIONS=5000 \
AIVM_STRESS_ITERATIONS=5000 \
AIVM_STRESS_VM_ITERATIONS=250 \
  ctest --test-dir .tmp/aivm-c-build-asan -L "fuzz|stress" --output-on-failure
```

## Performance Verification

Performance checks live under `tests/perf/`. They are not correctness tests;
they measure scalability and regression risk. PR CI runs `perf-smoke`, nightly
CI runs `perf-full`, and the harness writes JSON artifacts under
`artifacts/perf/`.

The first harness covers decode, evaluation, memory reset/safe-point, syscall
dispatch, worker dispatch, and golden replay timing. Baseline comparison is
staged until enough stable cross-platform data exists to avoid noisy gates.

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
