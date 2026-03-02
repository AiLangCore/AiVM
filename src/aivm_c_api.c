#include "aivm_c_api.h"

#include "aivm_runtime.h"

static AivmCResult result_defaults(void)
{
    AivmCResult result;
    result.ok = 0;
    result.loaded = 0;
    result.has_exit_code = 0;
    result.exit_code = 0;
    result.status = AIVM_VM_STATUS_READY;
    result.error = AIVM_VM_ERR_NONE;
    result.load_status = AIVM_PROGRAM_ERR_NULL;
    result.load_error_offset = 0U;
    return result;
}

static void capture_exit_code(AivmCResult* result, const AivmVm* vm)
{
    const AivmValue* top;
    if (result == NULL || vm == NULL) {
        return;
    }
    if (vm->status != AIVM_VM_STATUS_HALTED || vm->stack_count == 0U) {
        return;
    }

    top = &vm->stack[vm->stack_count - 1U];
    if (top->type != AIVM_VAL_INT) {
        return;
    }

    result->has_exit_code = 1;
    result->exit_code = (int)top->int_value;
}

AivmCResult aivm_c_execute_instructions(const AivmInstruction* instructions, size_t instruction_count)
{
    return aivm_c_execute_instructions_with_constants(
        instructions,
        instruction_count,
        NULL,
        0U);
}

AivmCResult aivm_c_execute_instructions_with_constants(
    const AivmInstruction* instructions,
    size_t instruction_count,
    const AivmValue* constants,
    size_t constant_count)
{
    return aivm_c_execute_instructions_with_syscalls(
        instructions,
        instruction_count,
        constants,
        constant_count,
        NULL,
        0U);
}

AivmCResult aivm_c_execute_instructions_with_syscalls(
    const AivmInstruction* instructions,
    size_t instruction_count,
    const AivmValue* constants,
    size_t constant_count,
    const AivmSyscallBinding* bindings,
    size_t binding_count)
{
    AivmProgram program;
    AivmVm vm;
    AivmCResult result = result_defaults();

    aivm_program_init(&program, instructions, instruction_count);
    if (constants != NULL && constant_count > 0U) {
        program.constants = constants;
        program.constant_count = constant_count;
    }
    result.loaded = 1;
    result.load_status = AIVM_PROGRAM_OK;
    result.load_error_offset = 0U;

    result.ok = aivm_execute_program_with_syscalls(
        &program,
        bindings,
        binding_count,
        &vm);
    result.status = vm.status;
    result.error = vm.error;
    capture_exit_code(&result, &vm);
    return result;
}

AivmCResult aivm_c_execute_program_with_syscalls(
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count)
{
    AivmVm vm;
    AivmCResult result = result_defaults();

    if (program == NULL) {
        result.load_status = AIVM_PROGRAM_ERR_NULL;
        result.load_error_offset = 0U;
        result.status = AIVM_VM_STATUS_ERROR;
        result.error = AIVM_VM_ERR_INVALID_PROGRAM;
        return result;
    }

    result.loaded = 1;
    result.load_status = AIVM_PROGRAM_OK;
    result.load_error_offset = 0U;
    result.ok = aivm_execute_program_with_syscalls(program, bindings, binding_count, &vm);
    result.status = vm.status;
    result.error = vm.error;
    capture_exit_code(&result, &vm);
    return result;
}

AivmCResult aivm_c_execute_aibc1(const uint8_t* bytes, size_t byte_count)
{
    AivmProgram program;
    AivmVm vm;
    AivmProgramLoadResult load_result;
    AivmCResult result = result_defaults();

    load_result = aivm_program_load_aibc1(bytes, byte_count, &program);
    result.load_status = load_result.status;
    result.load_error_offset = load_result.error_offset;

    if (load_result.status != AIVM_PROGRAM_OK) {
        result.status = AIVM_VM_STATUS_ERROR;
        result.error = AIVM_VM_ERR_INVALID_PROGRAM;
        return result;
    }

    result.loaded = 1;
    result.ok = aivm_execute_program(&program, &vm);
    result.status = vm.status;
    result.error = vm.error;
    capture_exit_code(&result, &vm);
    return result;
}

uint32_t aivm_c_abi_version(void)
{
    return 1U;
}
