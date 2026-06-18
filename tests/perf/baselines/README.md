# AiVM Performance Baselines

Performance baseline comparison is intentionally staged.

Current support:

- Benchmark JSON output is deterministic in shape.
- CTest labels distinguish `perf-smoke` from `perf-full`.
- Nightly CI uploads `artifacts/perf/*.json`.
- Regression thresholds are defined here for release review.

Threshold policy:

```text
warning   5%
failure   15%
critical  30%
```

Baseline comparison must be enabled only after the project has collected enough
stable cross-platform data to avoid noisy release gates.

## Open Coverage

Decode:

- bundle load time
- module load time
- dependency graph resolution
- invalid bundle rejection time

Evaluation:

- branch execution cost
- loop execution cost
- function call overhead
- recursive call overhead
- opcode-by-opcode coverage

Memory:

- compaction duration under retained nodes
- scratch allocation pressure
- long-lived allocation growth
- process peak memory

Syscalls:

- failed syscall overhead
- large payload throughput
- argument decoding by payload shape

Workers:

- worker startup and shutdown
- real message queue throughput
- queue saturation behavior
- parallel execution throughput

Runtime profiles:

- CLI
- service
- GUI
- WASM
- future mobile hosts
