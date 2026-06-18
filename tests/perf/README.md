# AiVM Performance Verification

This directory contains AiVM performance verification. These tests measure
runtime scalability and regression risk; they do not define language behavior.

Correctness, determinism, memory safety, and security failures take precedence
over performance results.

## Layout

```text
decode/      AiBC1 decode and loading benchmarks
eval/        VM evaluation and instruction dispatch benchmarks
memory/      arena reset, safe-point, and memory pressure benchmarks
syscall/     syscall dispatch and payload benchmarks
worker/      worker dispatch and queue benchmarks
golden/      golden replay timing benchmarks
stress/      long-running performance stress benchmarks
baselines/   checked-in baseline metadata and threshold policy
tools/       benchmark harnesses and comparison tools
```

## CTest Labels

```bash
ctest --test-dir .tmp/aivm-c-build-native -L perf-smoke --output-on-failure
ctest --test-dir .tmp/aivm-c-build-native -L perf-full --output-on-failure
```

`perf-smoke` is for pull requests and local checks. `perf-full` is for nightly,
release candidate, and explicit hardening runs.

## JSON Output

The harness writes:

```text
artifacts/perf/results-smoke.json
artifacts/perf/results-full.json
```

The result schema includes:

```text
test name
category
input size
iteration count
elapsed time
operations/sec
bytes/sec
peak memory bytes
allocation count
platform
compiler
git commit
```

`peak_memory_bytes` is currently `0` where cross-platform process memory
measurement is not yet implemented.

## Budgets

The benchmark harness supports explicit iteration budgets:

```bash
AIVM_PERF_DECODE_ITERATIONS=10000
AIVM_PERF_EVAL_ITERATIONS=1000
AIVM_PERF_MEMORY_ITERATIONS=10000
AIVM_PERF_SYSCALL_ITERATIONS=100000
AIVM_PERF_WORKER_ITERATIONS=100000
AIVM_PERF_GOLDEN_ITERATIONS=10000
```

## Current Coverage

The first harness covers:

```text
decode   aibc1_decode_256_instruction
eval     vm_eval_stack_churn
memory   vm_reset_stack_safepoint
syscall  syscall_checked_console_write
worker   worker_poll_dispatch
golden   golden_add_int_replay
```

Known gaps are tracked in `baselines/README.md`. New opcodes, runtime profiles,
and host profiles should add benchmark coverage as they become stable.
