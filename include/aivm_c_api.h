#ifndef AIVM_C_API_H
#define AIVM_C_API_H

#include <stddef.h>
#include <stdint.h>

#include "aivm_program.h"
#include "aivm_syscall.h"
#include "aivm_vm.h"

#if defined(_WIN32) && defined(AIVM_BUILD_SHARED_LIB)
#if defined(AIVM_CORE_SHARED_IMPL)
#define AIVM_API __declspec(dllexport)
#else
#define AIVM_API __declspec(dllimport)
#endif
#else
#define AIVM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int ok;
    int loaded;
    int has_exit_code;
    int exit_code;
    AivmVmStatus status;
    AivmVmError error;
    AivmProgramStatus load_status;
    size_t load_error_offset;
} AivmCResult;

AIVM_API AivmCResult aivm_c_execute_instructions(const AivmInstruction* instructions, size_t instruction_count);
AIVM_API AivmCResult aivm_c_execute_instructions_with_constants(
    const AivmInstruction* instructions,
    size_t instruction_count,
    const AivmValue* constants,
    size_t constant_count);
AIVM_API AivmCResult aivm_c_execute_instructions_with_syscalls(
    const AivmInstruction* instructions,
    size_t instruction_count,
    const AivmValue* constants,
    size_t constant_count,
    const AivmSyscallBinding* bindings,
    size_t binding_count);
AIVM_API AivmCResult aivm_c_execute_program_with_syscalls(
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count);
AIVM_API AivmCResult aivm_c_execute_aibc1(const uint8_t* bytes, size_t byte_count);
AIVM_API uint32_t aivm_c_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
