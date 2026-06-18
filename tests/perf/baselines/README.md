# AiVM Performance Baselines

Performance baseline comparison is intentionally staged.

Current support:

- Benchmark JSON output is deterministic in shape.
- CTest labels distinguish `perf-smoke` from `perf-full`.
- Nightly CI uploads `artifacts/perf/*.json`.
- `aivm_perf_compare` enforces same-name operations/sec regression thresholds.
- `perf-release` validates the comparator against stable fixtures.
- Regression thresholds are defined here for release review.

Threshold policy:

```text
warning   5%
failure   15%
critical  30%
```

Release baseline comparison should point `aivm_perf_compare` at a curated
platform/release baseline and the newly generated `artifacts/perf/results-full.json`.
Curated baselines should be added only after the project has enough stable
cross-platform data to avoid noisy gates.

## Open Coverage

Decode:

- bundle load time
- module load time
- dependency graph resolution

Evaluation:

- opcode-by-opcode coverage

Memory:

- compaction duration under retained nodes
- scratch allocation pressure
- long-lived allocation growth
- process peak memory

Syscalls:

- argument decoding by payload shape

Workers:

- queue saturation behavior
- parallel execution throughput

Runtime profiles:

- CLI
- service
- GUI
- WASM
- future mobile hosts
