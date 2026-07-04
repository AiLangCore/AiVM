# AiVM Docs

## Objective

Provide stable usage documentation for humans and developer agents operating the
AiVM repository.

## Normative Source

- `../SPEC/README.md`
- `../SPEC/MEMORY.md`
- `../SPEC/DEBUG_ARTIFACTS.md`
- `../SPEC/SYSCALLS.md`
- `../SPEC/RESOURCE_LIMITS.md`

If a doc in `Docs/` conflicts with `SPEC/`, follow `SPEC/`.

## Taxonomy

- `../SPEC/` contains normative AiVM runtime specifications.
- `../Docs/` contains stable usage documentation.
- `../Design/` contains non-normative design notes, proposals, rationale, and decisions.
- `../Planning/` contains gated plans, tasks, readiness notes, and checklists.
- `../Archive/` contains historical or superseded documents.
- `*.local.md` / `*.local.*` files are local scratch and must not be committed.

If a planning or design document proposes behavior that becomes runtime contract,
resource-limit behavior, syscall behavior, debug artifact shape, or observable VM
behavior, move that behavior into `../SPEC/` before implementation relies on it.

## Usage Index

- [Host ABI](./Host-ABI.md)
- [Native Test Infrastructure](./Native-Test-Infrastructure.md)

## Related Non-Usage Documents

- [Specification Index](../SPEC/README.md)
- [Design Notes](../Design/README.md)
- [Planning Documents](../Planning/README.md)
- [Archive](../Archive/README.md)
