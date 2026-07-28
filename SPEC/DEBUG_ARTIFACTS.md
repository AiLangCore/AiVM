# AiVM Debug Artifact Contract

Status: beta contract documentation.

This document defines the machine-readable debug artifact bundle emitted by
AiVM debug tooling. It is an AiVM runtime/tooling contract. AiLang owns language
semantics; AiVectra owns UI runtime semantics and visual capture behavior.

## Scope

`aivm-debug` and AiLang tooling that embeds the AiVM debug host may emit debug
artifact directories. The artifact format is for agents, CI, and humans who
need deterministic evidence from a run.

Production `aivm` is not required to emit this bundle.

## Bundle Rules

Artifact writers must follow these rules:

- use stable file names
- use stable field names
- keep deterministic field ordering within each file
- write numeric counters as base-10 integers
- keep file sizes bounded by the active debug artifact budget
- make truncation visible when truncation is introduced
- avoid machine-local absolute paths unless the path itself is diagnostic data

Missing files must be intentional for the command/profile. A command that
claims to emit a full debug capture must write the core files listed below.

## Core Files

The full debug capture bundle uses these file names:

```text
config.toml
diagnostics.toml
stdout.txt
stderr.txt
vm_trace.toml
syscall_trace.toml
stack_trace.toml
profile.toml
memory.toml
state_snapshots.toml
events.toml
ui_capture.toml
suggestions.toml
```

Not every current command writes every optional file. The current required
machine-readable memory contract is `diagnostics.toml` and
`state_snapshots.toml`; standalone `aivm-debug debug capture run` also writes
`memory.toml`.

## `diagnostics.toml`

`diagnostics.toml` summarizes run status and the final memory state. It must
include a single-line `memory = { ... }` table with these fields:

| Field | Meaning |
| --- | --- |
| `string_arena_used` | Current VM-owned string arena bytes. |
| `string_arena_high_water` | Peak VM-owned string arena bytes. |
| `bytes_arena_used` | Current VM-owned byte arena bytes. |
| `bytes_arena_high_water` | Peak VM-owned byte arena bytes. |
| `node_count` | Current semantic node records. |
| `node_high_water` | Peak semantic node records. |
| `scratch_pair_count` | Current scratch-pair records. |
| `scratch_pair_capacity` | Maximum scratch-pair records. |
| `node_attr_count` | Current semantic node attributes. |
| `node_attr_high_water` | Peak semantic node attributes. |
| `node_child_count` | Current semantic node child handles. |
| `node_child_high_water` | Peak semantic node child handles. |
| `node_gc_compactions` | Completed deterministic node compactions. |
| `node_gc_attempts` | Deterministic node compaction attempts. |
| `node_gc_reclaimed_nodes` | Nodes reclaimed by deterministic compaction. |
| `node_gc_allocations_since_gc` | Node allocations since the last compaction attempt. |
| `node_gc_interval_allocations` | Proactive node-GC allocation interval. |
| `node_gc_pressure_threshold_nodes` | Node-count pressure threshold. |
| `node_gc_pressure_threshold_attrs` | Node-attribute pressure threshold. |
| `node_gc_pressure_threshold_children` | Node-child pressure threshold. |
| `string_arena_pressure_count` | String arena pressure events. |
| `bytes_arena_pressure_count` | Byte arena pressure events. |
| `node_arena_pressure_count` | Node arena pressure events. |

`diagnostics.toml` must also include:

- `node_roots = { ... }`: structured root attribution by root class
- `node_kind_counts = [...]`: retained node attribution by node kind

The root attribution classes are:

- `stack`
- `locals`
- `completed_tasks`
- `par_values`
- `process_argv`
- `ui_window_size`
- `ui_empty_event`

## `state_snapshots.toml`

`state_snapshots.toml` stores flat live counters for agent and shell-script
inspection. It must include:

- `stack_count`
- `locals_count`
- all arena usage/high-water fields from the diagnostics memory table
- `scratch_pair_count`
- `scratch_pair_capacity`
- node GC counters and pressure policy fields
- flat `node_root_*` counters for every root attribution class
- `node_kind_counts = [...]`

## `memory.toml`

Standalone `aivm-debug` captures write `memory.toml` as a focused memory view.
It must include arena usage, `scratch_pair_count`, and a `limits = { ... }`
table containing `scratch_pair_capacity` and the active runtime profile limits.

## Compatibility Policy

Before the first major or minor release, this contract is negotiable. Changes
should replace the previous contract completely and update tests, docs, and
tooling consistently. Do not add compatibility aliases or dual-path readers
unless explicitly requested.

