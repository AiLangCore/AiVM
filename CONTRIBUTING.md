# Contributing to AiVM

AiVM owns the native C virtual machine, AiBC loading/execution, syscall
dispatch boundary, diagnostics, and embeddable runtime libraries. AiLang owns
compiler/tooling behavior. AiVectra owns UI runtime behavior.

## Branches

AiVM uses Git Flow. The default integration branch is `develop`.

- Branch feature work from `develop`.
- Keep the production `aivm` command surface tiny.
- Do not add compatibility layers before the first major or minor release unless
  explicitly requested. Replace old contracts consistently when direction
  changes.

## Local Setup

AiVM can build and test by itself:

```bash
git clone https://github.com/AiLangCore/AiVM.git
cd AiVM
git checkout develop
```

For cross-repo migration work, clone the core repositories as siblings:

```bash
mkdir AiLangCore
cd AiLangCore
git clone https://github.com/AiLangCore/AiLang.git
git clone https://github.com/AiLangCore/AiVM.git
git clone https://github.com/AiLangCore/AiVectra.git
```

## Verification

Run from the AiVM repository root:

```bash
./build.sh
./test-aivm-c.sh
```

For syscall changes, also run:

```bash
./scripts/check-syscall-contracts.sh
```

## Contribution Rules

- Keep VM behavior deterministic.
- Keep all host effects behind explicit `sys.*` contracts.
- New syscalls require absolute host-boundary justification.
- Every syscall change must update `Docs/Syscalls.md`,
  `native/sys/aivm_syscall_contracts.c`, and
  `native/tests/test_syscall_contracts.c`.
- Do not add AiLang compiler/tooling behavior or AiVectra UI policy here.
- Keep generated outputs out of commits: `.tmp/`, `.artifacts/`, local SDK
  files, and local notes.
