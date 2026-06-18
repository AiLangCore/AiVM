# AiVM Native Verification Tests

This directory is the native verification surface for AiVM.

```text
unit/         isolated runtime component checks
integration/ subsystem and host-boundary interaction checks
fuzz/         malformed-input fuzz smoke tests and future fuzz harnesses
stress/       repeated-operation and long-running abuse checks
golden/       authoritative deterministic behavior fixtures
security/     hostile input and fail-safe validation checks
```

Golden tests define canonical observable behavior. Unit and integration tests
verify implementation mechanics. Fuzz, stress, and security tests verify native
runtime robustness.

Behavioral changes must update the spec first, then the golden fixtures, then
the implementation.
