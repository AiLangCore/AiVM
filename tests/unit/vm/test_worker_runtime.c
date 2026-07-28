#include "aivm_worker_capacity.h"
#include "aivm_worker_capabilities.h"
#include "aivm_worker_runtime.h"

#include <stdio.h>
#include <string.h>

static void write_u32(uint8_t* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static void write_i64(uint8_t* bytes, size_t offset, int64_t value)
{
    size_t index;
    uint64_t raw = (uint64_t)value;
    for (index = 0U; index < 8U; index += 1U) {
        bytes[offset + index] = (uint8_t)(raw >> (index * 8U));
    }
}

static void write_instruction(
    uint8_t* bytes,
    size_t offset,
    AivmOpcode opcode,
    int64_t operand)
{
    write_u32(bytes, offset, (uint32_t)opcode);
    write_i64(bytes, offset + 4U, operand);
}

static void make_identity_worker(uint8_t artifact[64])
{
    memset(artifact, 0, 64U);
    memcpy(artifact, "AIBC", 4U);
    write_u32(artifact, 4U, 2U);
    write_u32(artifact, 12U, 1U);
    write_u32(artifact, 16U, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
    write_u32(artifact, 20U, 40U);
    write_u32(artifact, 24U, 3U);
    write_instruction(artifact, 28U, AIVM_OP_STORE_LOCAL, 0);
    write_instruction(artifact, 40U, AIVM_OP_LOAD_LOCAL, 0);
    write_instruction(artifact, 52U, AIVM_OP_RET, 0);
}

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    uint8_t artifact[64];
    AivmWorkerCatalogEntry entry;
    AivmProgram owner;
    AivmSyscallCapabilityPolicy parent_policy;
    AivmWorkerRuntime* runtime = NULL;
    AivmWorkerRuntimeResult first;
    AivmWorkerRuntimeResult second;
    static const uint8_t first_payload[] = { 1U, 2U, 3U };
    static const uint8_t second_payload[] = { 9U, 8U };
    size_t expected_active;

    make_identity_worker(artifact);
    memset(&entry, 0, sizeof(entry));
    entry.function_target = 0U;
    entry.transport_abi = AIVM_WORKER_TRANSPORT_ABI_BYTES_V1;
    entry.bytecode_version = 2U;
    entry.artifact = artifact;
    entry.artifact_length = sizeof(artifact);
    aivm_program_clear(&owner);
    owner.worker_catalog.entries = &entry;
    owner.worker_catalog.count = 1U;
    aivm_syscall_policy_allow_none(&parent_policy);
    if (expect(aivm_worker_capability_syscall_mask(
        AIVM_WORKER_CAPABILITY_FILESYSTEM) ==
        aivm_syscall_capability_mask(AIVM_SYSCALL_CAPABILITY_FILESYSTEM)) != 0 ||
        expect(aivm_worker_capability_syscall_mask(
        AIVM_WORKER_CAPABILITY_STANDARD_STREAMS) ==
        aivm_syscall_capability_mask(AIVM_SYSCALL_CAPABILITY_CONSOLE)) != 0) {
        return 1;
    }

    if (expect(aivm_worker_runtime_create(
        &owner, &parent_policy, NULL, 0U,
        AIVM_RUNTIME_PROFILE_TOOLING, 8U, &runtime) ==
        AIVM_WORKER_RUNTIME_OK) != 0) {
        return 1;
    }
    expected_active = aivm_worker_active_capacity(
        aivm_runtime_profile_limits(AIVM_RUNTIME_PROFILE_TOOLING).worker_count,
        8U);
    if (expect(aivm_worker_runtime_active_limit(runtime) == expected_active) != 0) {
        return 1;
    }
    if (expect(aivm_worker_runtime_submit(
        runtime, 0U, 1U, first_payload, sizeof(first_payload)) ==
        AIVM_WORKER_RUNTIME_OK) != 0 ||
        expect(aivm_worker_runtime_submit(
        runtime, 0U, 2U, second_payload, sizeof(second_payload)) ==
        AIVM_WORKER_RUNTIME_OK) != 0) {
        return 1;
    }
    if (expect(aivm_worker_runtime_await(runtime, 2U, &second) ==
        AIVM_WORKER_RUNTIME_OK) != 0 ||
        expect(second.length == sizeof(second_payload)) != 0 ||
        expect(memcmp(second.data, second_payload, sizeof(second_payload)) == 0) != 0) {
        return 1;
    }
    if (expect(aivm_worker_runtime_await(runtime, 1U, &first) ==
        AIVM_WORKER_RUNTIME_OK) != 0 ||
        expect(first.length == sizeof(first_payload)) != 0 ||
        expect(memcmp(first.data, first_payload, sizeof(first_payload)) == 0) != 0) {
        return 1;
    }
    if (expect(aivm_worker_runtime_release(runtime, 1U) ==
        AIVM_WORKER_RUNTIME_OK) != 0 ||
        expect(aivm_worker_runtime_release(runtime, 2U) ==
        AIVM_WORKER_RUNTIME_OK) != 0) {
        return 1;
    }
    aivm_worker_runtime_destroy(runtime);

    entry.required_capabilities = AIVM_WORKER_CAPABILITY_FILESYSTEM;
    runtime = NULL;
    if (expect(aivm_worker_runtime_create(
        &owner, &parent_policy, NULL, 0U,
        AIVM_RUNTIME_PROFILE_TOOLING, 1U, &runtime) ==
        AIVM_WORKER_RUNTIME_OK) != 0 ||
        expect(aivm_worker_runtime_submit(
            runtime, 0U, 3U, first_payload, sizeof(first_payload)) ==
        AIVM_WORKER_RUNTIME_ERR_CAPABILITY) != 0) {
        return 1;
    }
    aivm_worker_runtime_destroy(runtime);

    printf("aivm worker runtime tests passed\n");
    return 0;
}
