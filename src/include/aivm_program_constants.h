#ifndef AIVM_PROGRAM_CONSTANTS_H
#define AIVM_PROGRAM_CONSTANTS_H

#include <stddef.h>

#include "aivm_program.h"

int aivm_program_constants_reserve(AivmProgram* program, size_t count);
AivmValue* aivm_program_constants_mutable(AivmProgram* program);
void aivm_program_constants_release(AivmProgram* program);

#endif
