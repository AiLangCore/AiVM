## Summary

- 

## Scope

- [ ] Native VM execution change
- [ ] Syscall boundary change
- [ ] Memory, diagnostics, profiling, or debug change
- [ ] Build, CI, release, or documentation change

## Verification

- [ ] `./build.sh`
- [ ] `./test-aivm-c.sh`
- [ ] `./scripts/check-syscall-contracts.sh` when syscall contracts changed

## Architecture Checklist

- [ ] Production `aivm` command surface remains tiny
- [ ] Host effects remain behind explicit `sys.*` contracts
- [ ] New syscalls have host-boundary justification and contract tests
- [ ] AiLang compiler/tooling behavior was not added to AiVM
- [ ] AiVectra UI policy was not added to AiVM
- [ ] Generated files are not included (`.tmp/`, `.artifacts/`, local SDK files,
      local notes)
- [ ] No backward compatibility layer was added before the first major/minor
      release unless explicitly requested
