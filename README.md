# AiVM.Core Native

This directory is the canonical source root for the C runtime migration.

Current state:
- `CMakeLists.txt` bridges to the legacy `AiVM.C/` tree.

Target state:
- C runtime sources live directly under `src/AiVM.Core/native/`.
- `AiVM.C/` is removed after zero-C# cutover completes.
