#include "aivm_worker_program.h"

#include "aivm_worker_capabilities.h"

#include <stdlib.h>

void aivm_worker_program_clear(AivmWorkerProgram* program)
{
    if (program == NULL) {
        return;
    }
    program->loaded = NULL;
    program->function_target = 0U;
    program->required_capabilities = 0U;
}

void aivm_worker_program_release(AivmWorkerProgram* program)
{
    if (program == NULL) {
        return;
    }
    if (program->loaded != NULL) {
        aivm_program_release(program->loaded);
        free(program->loaded);
    }
    aivm_worker_program_clear(program);
}

AivmWorkerProgramStatus aivm_worker_program_prepare(
    const AivmWorkerCatalogEntry* entry,
    const AivmSyscallCapabilityPolicy* parent_policy,
    AivmWorkerProgram* out_program)
{
    AivmProgramLoadResult loaded;
    uint64_t required;
    if (entry == NULL || parent_policy == NULL || out_program == NULL) {
        return AIVM_WORKER_PROGRAM_ERR_ARGUMENT;
    }
    aivm_worker_program_clear(out_program);
    required = aivm_worker_capability_syscall_mask(entry->required_capabilities);
    if ((required & ~parent_policy->allowed_capability_mask) != 0U) {
        return AIVM_WORKER_PROGRAM_ERR_CAPABILITY;
    }
    out_program->loaded = (AivmProgram*)malloc(sizeof(*out_program->loaded));
    if (out_program->loaded == NULL) {
        return AIVM_WORKER_PROGRAM_ERR_MEMORY;
    }
    loaded = aivm_program_load_aibc1(
        entry->artifact, entry->artifact_length, out_program->loaded);
    if (loaded.status != AIVM_PROGRAM_OK) {
        free(out_program->loaded);
        aivm_worker_program_clear(out_program);
        return AIVM_WORKER_PROGRAM_ERR_ARTIFACT;
    }
    if ((size_t)entry->function_target >= out_program->loaded->instruction_count ||
        out_program->loaded->instructions[entry->function_target].opcode != AIVM_OP_STORE_LOCAL ||
        (size_t)entry->function_target + 1U >= out_program->loaded->instruction_count ||
        out_program->loaded->instructions[entry->function_target + 1U].opcode == AIVM_OP_STORE_LOCAL) {
        aivm_worker_program_release(out_program);
        return AIVM_WORKER_PROGRAM_ERR_SIGNATURE;
    }
    out_program->function_target = (size_t)entry->function_target;
    out_program->required_capabilities = required;
    return AIVM_WORKER_PROGRAM_OK;
}
