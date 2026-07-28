# AiVM RC Readiness

Status: late beta, entering release-candidate preparation.

AiVM should not be tagged as an RC until every RC gate below is checked or
explicitly deferred in this file with a reason. RC means the native runtime
surface is stable enough for outside developers to evaluate against release
candidate expectations. It does not mean the whole AiLangCore project is RC.

## Current Position

- Current public release: `v0.0.1-beta.2`
- Current branch for integration work: `develop`
- Runtime ownership: native C VM, AiBC loading/execution, syscall dispatch,
  `aivm` executable, and embeddable VM library
- Public production command surface: `aivm --version`, `aivm --help`, and
  bytecode execution with `aivm <program.aibc1>`
- Debug command surface: `aivm-debug`

## RC Gates

- [x] Native C VM ownership is separated from AiLang and AiVectra.
- [x] Production command surface is intentionally tiny.
- [x] Debug/runtime tooling split is documented through `aivm` and
  `aivm-debug`.
- [x] Public Host ABI header and compatibility descriptor are documented and
  unit-tested.
- [x] `aivm_c_abi_version()` reports the canonical AiVM Host ABI version used
  by package target dispatch.
- [x] Syscall boundary is documented and contract-tested.
- [x] Resource limits and error families are documented.
- [x] Deterministic safe-point compaction is implemented and tested.
- [x] Worker-local heap strategy is documented.
- [x] `AIVM_OP_ASYNC_CALL` runs bytecode on isolated native worker threads.
- [x] `AWAIT` joins pending bytecode workers deterministically.
- [x] Worker result handoff copies scalar values, strings/bytes, node graphs,
  and scratch pairs without sharing mutable semantic heaps.
- [x] `./test-aivm-c.sh` passes locally on `develop`.
- [ ] Confirm Linux, macOS, and Windows release artifacts are reproducible from
  the RC tag.
- [ ] Confirm production `aivm` does not expose debug-only `sys.debug.*`
  behavior.
- [ ] Confirm every syscall has docs, contract coverage, error behavior, and
  resource-limit behavior.
- [ ] Add one public embedding smoke that uses the installed headers and
  library from a minimal host program.
- [ ] Make the security model prominent in README and release notes: AiVM is a
  runtime with explicit syscalls, not a sandbox; OS/container/app sandboxing is
  the deployment boundary.

## RC Decision Items

- [ ] Decide whether shared immutable module cache is required for RC or
  explicitly defer it.
- [ ] Decide whether large-object/blob storage is required for RC or explicitly
  defer it.
- [ ] Decide whether non-default tooling limits need additional enforcement
  before RC.

## Deferred Beyond RC Candidate Prep

- Deterministic generational arenas are post-beta/post-RC research unless they
  are explicitly promoted to a release requirement.

## RC Tag Rule

Create `v0.0.1-rc.1` only after the RC gates are complete and the decision
items are closed by implementation or explicit deferral.
