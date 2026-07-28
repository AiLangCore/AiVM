#include "aivm_program_instructions.h"

#include <stdlib.h>

int aivm_program_instructions_reserve(AivmProgram* program, size_t count)
{
    AivmInstruction* storage;
    if (program == NULL) {
        return 0;
    }
    if (count <= AIVM_PROGRAM_INLINE_INSTRUCTIONS) {
        program->instructions = count == 0U ? NULL : program->instruction_storage;
        program->instruction_capacity = AIVM_PROGRAM_INLINE_INSTRUCTIONS;
        return 1;
    }
    if (count > ((size_t)-1) / sizeof(AivmInstruction)) {
        return 0;
    }
    storage = (AivmInstruction*)calloc(count, sizeof(AivmInstruction));
    if (storage == NULL) {
        return 0;
    }
    program->allocated_instruction_storage = storage;
    program->instructions = storage;
    program->instruction_capacity = count;
    return 1;
}

AivmInstruction* aivm_program_instructions_mutable(AivmProgram* program)
{
    if (program == NULL) {
        return NULL;
    }
    if (program->allocated_instruction_storage != NULL) {
        return program->allocated_instruction_storage;
    }
    return program->instruction_storage;
}

void aivm_program_instructions_release(AivmProgram* program)
{
    if (program == NULL) {
        return;
    }
    free(program->allocated_instruction_storage);
    program->allocated_instruction_storage = NULL;
    program->instructions = NULL;
    program->instruction_count = 0U;
    program->instruction_capacity = 0U;
}
