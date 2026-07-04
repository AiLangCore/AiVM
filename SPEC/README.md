# AiVM Specification Index

Status: normative index for AiVM runtime implementation contracts.

The normative AiVM specification consists of:

- `MEMORY.md`
- `DEBUG_ARTIFACTS.md`
- `SYSCALLS.md`
- `RESOURCE_LIMITS.md`

AiLang owns language semantics, IL evaluation semantics, validation rules, and
bytecode language meaning. AiVM owns runtime mechanics needed to execute AiBC
deterministically: memory mechanics, syscall dispatch, resource limits, debug
artifact shape, and native host ABI behavior.

## Authority Rule

If implementation, `Docs/`, `Design/`, `Planning/`, `Archive/`, tests,
examples, issue templates, or agent instructions conflict with the normative
specification files listed above, the listed specification files win for AiVM
runtime behavior.

This repository must not define AiLang language semantics. If a runtime document
appears to define language meaning, move that language contract to the AiLang
specification first and make the AiVM document reference it.

## Change Control

A change that affects AiVM observable runtime behavior, syscall dispatch,
resource-limit behavior, debug artifact shape, or deterministic memory behavior
must update the relevant normative spec, then tests, then implementation.
