#include <stddef.h>

#include "aivm_c_api.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef AivmCResult (*AivmExecInstructionsFn)(const AivmInstruction* instructions, size_t instruction_count);
typedef AivmCResult (*AivmExecInstructionsWithConstantsFn)(
    const AivmInstruction* instructions,
    size_t instruction_count,
    const AivmValue* constants,
    size_t constant_count);
typedef uint32_t (*AivmAbiVersionFn)(void);

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmInstruction instructions[2];
    AivmInstruction exit_code_instructions[2];
    AivmExecInstructionsFn exec_fn;
    AivmExecInstructionsWithConstantsFn exec_with_constants_fn;
    AivmAbiVersionFn abi_fn;
    AivmCResult result;

#if defined(_WIN32)
    HMODULE lib = LoadLibraryA("aivm_core_shared.dll");
    if (expect(lib != NULL) != 0) {
        return 1;
    }
    exec_fn = (AivmExecInstructionsFn)GetProcAddress(lib, "aivm_c_execute_instructions");
    if (expect(exec_fn != NULL) != 0) {
        FreeLibrary(lib);
        return 1;
    }
    exec_with_constants_fn = (AivmExecInstructionsWithConstantsFn)GetProcAddress(lib, "aivm_c_execute_instructions_with_constants");
    if (expect(exec_with_constants_fn != NULL) != 0) {
        FreeLibrary(lib);
        return 1;
    }
    abi_fn = (AivmAbiVersionFn)GetProcAddress(lib, "aivm_c_abi_version");
    if (expect(abi_fn != NULL) != 0) {
        FreeLibrary(lib);
        return 1;
    }
#else
#if defined(__APPLE__)
    void* lib = dlopen("./libaivm_core_shared.dylib", RTLD_NOW);
#else
    void* lib = dlopen("./libaivm_core_shared.so", RTLD_NOW);
#endif
    if (expect(lib != NULL) != 0) {
        return 1;
    }
    exec_fn = (AivmExecInstructionsFn)dlsym(lib, "aivm_c_execute_instructions");
    if (expect(exec_fn != NULL) != 0) {
        dlclose(lib);
        return 1;
    }
    exec_with_constants_fn = (AivmExecInstructionsWithConstantsFn)dlsym(lib, "aivm_c_execute_instructions_with_constants");
    if (expect(exec_with_constants_fn != NULL) != 0) {
        dlclose(lib);
        return 1;
    }
    abi_fn = (AivmAbiVersionFn)dlsym(lib, "aivm_c_abi_version");
    if (expect(abi_fn != NULL) != 0) {
        dlclose(lib);
        return 1;
    }
#endif

    instructions[0].opcode = AIVM_OP_NOP;
    instructions[0].operand_int = 0;
    instructions[1].opcode = AIVM_OP_HALT;
    instructions[1].operand_int = 0;

    result = exec_fn(instructions, 2U);
    if (expect(abi_fn() == 1U) != 0) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        return 1;
    }
    if (expect(result.ok == 1) != 0) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        return 1;
    }
    if (expect(result.status == AIVM_VM_STATUS_HALTED) != 0) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        return 1;
    }
    if (expect(result.has_exit_code == 0) != 0) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        return 1;
    }

    exit_code_instructions[0].opcode = AIVM_OP_PUSH_INT;
    exit_code_instructions[0].operand_int = 9;
    exit_code_instructions[1].opcode = AIVM_OP_HALT;
    exit_code_instructions[1].operand_int = 0;

    result = exec_fn(exit_code_instructions, 2U);
    if (expect(result.ok == 1) != 0) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        return 1;
    }
    if (expect(result.has_exit_code == 1) != 0) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        return 1;
    }
    if (expect(result.exit_code == 9) != 0) {
#if defined(_WIN32)
        FreeLibrary(lib);
#else
        dlclose(lib);
#endif
        return 1;
    }

#if defined(_WIN32)
    FreeLibrary(lib);
#else
    dlclose(lib);
#endif
    return 0;
}
