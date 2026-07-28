#include "aivm_program_constants.h"

#include <stdlib.h>

int aivm_program_constants_reserve(AivmProgram* program, size_t count)
{
    AivmValue* storage;
    if (program == NULL) {
        return 0;
    }
    if (count <= AIVM_PROGRAM_INLINE_CONSTANTS) {
        program->constants = count == 0U ? NULL : program->constant_storage;
        program->constant_capacity = AIVM_PROGRAM_INLINE_CONSTANTS;
        return 1;
    }
    if (count > ((size_t)-1) / sizeof(AivmValue)) {
        return 0;
    }
    storage = (AivmValue*)calloc(count, sizeof(AivmValue));
    if (storage == NULL) {
        return 0;
    }
    program->allocated_constant_storage = storage;
    program->constants = storage;
    program->constant_capacity = count;
    return 1;
}

AivmValue* aivm_program_constants_mutable(AivmProgram* program)
{
    if (program == NULL) {
        return NULL;
    }
    if (program->allocated_constant_storage != NULL) {
        return program->allocated_constant_storage;
    }
    return program->constant_storage;
}

void aivm_program_constants_release(AivmProgram* program)
{
    if (program == NULL) {
        return;
    }
    free(program->allocated_constant_storage);
    program->allocated_constant_storage = NULL;
    program->constants = NULL;
    program->constant_count = 0U;
    program->constant_capacity = 0U;
}
