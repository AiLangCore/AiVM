#ifndef AIVM_PROGRAM_INSTRUCTIONS_H
#define AIVM_PROGRAM_INSTRUCTIONS_H

#include "aivm_program.h"

int aivm_program_instructions_reserve(AivmProgram* program, size_t count);
AivmInstruction* aivm_program_instructions_mutable(AivmProgram* program);
void aivm_program_instructions_release(AivmProgram* program);

#endif
