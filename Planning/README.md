# AiVM Planning Documents

Status: non-normative active work tracking.

This directory contains gated work plans, release-readiness checklists,
migration plans, task lists, and hardening notes.

Planning documents do not define AiVM runtime contracts. If a planning item
becomes authoritative runtime behavior, move the behavior into `../SPEC/` before
implementation relies on it.

## Filename Classes

- `*.feature-<name>.md` - active feature plan.
- `*.rc1.md`, `*.rc2.md`, ... - release-candidate gate.
- `*.milestone-<name>.md` - milestone gate.
- `*.note.md` - shared developer planning note.

Planning documents should identify status, scope, exit criteria, and validation
commands when applicable.
