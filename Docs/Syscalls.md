# AiVM Syscalls

Status: moved.

The normative syscall boundary and syscall addition rules now live in:

- `../SPEC/SYSCALLS.md`

This compatibility pointer remains so existing links do not fail. Do not add new
syscall contract material here. The release verifier reads `../SPEC/SYSCALLS.md`.
Update `../SPEC/SYSCALLS.md`,
`../src/sys/aivm_syscall_contracts.c`, and
`../tests/unit/syscalls/test_syscall_contracts.c` together.
