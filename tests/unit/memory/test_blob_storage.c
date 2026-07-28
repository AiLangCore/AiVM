#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aivm_vm.h"

static int expect_line(int condition, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(stderr, "expect failed at line %d\n", line);
    return 1;
}

#define expect(condition) expect_line((condition), __LINE__)

static const AivmProgram* empty_program(void)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    return &program;
}

static int create_read_release_roundtrip(void)
{
    static AivmVm vm;
    static const uint8_t payload[] = { 10U, 20U, 30U, 40U, 50U };
    uint8_t out[3] = { 0U, 0U, 0U };
    int64_t handle = 0;
    size_t out_read = 0U;

    aivm_init(&vm, empty_program());
    if (expect(aivm_blob_create(&vm, payload, sizeof(payload), &handle) == AIVM_BLOB_OK) != 0) {
        return 1;
    }
    if (expect(handle == 1) != 0 ||
        expect(aivm_blob_active_count(&vm) == 1U) != 0 ||
        expect(vm.blob_bytes_used == sizeof(payload)) != 0 ||
        expect(vm.blob_bytes_high_water == sizeof(payload)) != 0) {
        return 1;
    }
    if (expect(aivm_blob_read(&vm, handle, 1U, out, sizeof(out), &out_read) == AIVM_BLOB_OK) != 0 ||
        expect(out_read == sizeof(out)) != 0 ||
        expect(memcmp(out, &payload[1], sizeof(out)) == 0) != 0) {
        return 1;
    }
    if (expect(aivm_blob_release(&vm, handle) == AIVM_BLOB_OK) != 0 ||
        expect(aivm_blob_active_count(&vm) == 0U) != 0 ||
        expect(vm.blob_bytes_used == 0U) != 0 ||
        expect(aivm_blob_release(&vm, handle) == AIVM_BLOB_ERR_NOT_FOUND) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int capacity_limit_is_deterministic(void)
{
    static AivmVm vm;
    size_t index;
    int64_t handle = 0;

    aivm_init(&vm, empty_program());
    for (index = 0U; index < AIVM_VM_BLOB_CAPACITY; index += 1U) {
        if (expect(aivm_blob_create(&vm, NULL, 0U, &handle) == AIVM_BLOB_OK) != 0) {
            return 1;
        }
        if (expect(handle == (int64_t)index + 1) != 0) {
            return 1;
        }
    }
    if (expect(aivm_blob_create(&vm, NULL, 0U, &handle) == AIVM_BLOB_ERR_LIMIT) != 0 ||
        expect(vm.blob_pressure_count == 1U) != 0 ||
        expect(aivm_blob_active_count(&vm) == AIVM_VM_BLOB_CAPACITY) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int byte_limit_is_deterministic(void)
{
    static AivmVm vm;
    uint8_t one = 1U;
    int64_t handle = 0;

    aivm_init(&vm, empty_program());
    if (expect(aivm_blob_create(&vm, &one, AIVM_VM_BLOB_BYTES + 1U, &handle) == AIVM_BLOB_ERR_LIMIT) != 0 ||
        expect(vm.blob_pressure_count == 1U) != 0 ||
        expect(aivm_blob_active_count(&vm) == 0U) != 0 ||
        expect(vm.blob_bytes_used == 0U) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int profile_limits_expose_blob_limits(void)
{
    AivmRuntimeProfileLimits limits = aivm_runtime_profile_limits(AIVM_RUNTIME_PROFILE_PRODUCTION);
    if (expect(limits.blob_capacity == AIVM_VM_BLOB_CAPACITY) != 0 ||
        expect(limits.blob_bytes == AIVM_VM_BLOB_BYTES) != 0) {
        return 1;
    }
    return 0;
}

static int reset_releases_blobs(void)
{
    static AivmVm vm;
    static const uint8_t payload[] = { 1U, 2U, 3U };
    int64_t first_handle = 0;
    int64_t second_handle = 0;
    size_t out_read = 99U;

    aivm_init(&vm, empty_program());
    if (expect(aivm_blob_create(&vm, payload, sizeof(payload), &first_handle) == AIVM_BLOB_OK) != 0 ||
        expect(aivm_blob_active_count(&vm) == 1U) != 0) {
        return 1;
    }
    aivm_reset_state(&vm);
    if (expect(aivm_blob_active_count(&vm) == 0U) != 0 ||
        expect(vm.blob_bytes_used == 0U) != 0 ||
        expect(aivm_blob_read(&vm, first_handle, 0U, NULL, 0U, &out_read) == AIVM_BLOB_ERR_NOT_FOUND) != 0 ||
        expect(out_read == 0U) != 0) {
        return 1;
    }
    if (expect(aivm_blob_create(&vm, payload, sizeof(payload), &second_handle) == AIVM_BLOB_OK) != 0 ||
        expect(second_handle == 1) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

int main(void)
{
    if (create_read_release_roundtrip() != 0) {
        return 1;
    }
    if (capacity_limit_is_deterministic() != 0) {
        return 1;
    }
    if (byte_limit_is_deterministic() != 0) {
        return 1;
    }
    if (profile_limits_expose_blob_limits() != 0) {
        return 1;
    }
    if (reset_releases_blobs() != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_blob_status_code(AIVM_BLOB_ERR_LIMIT), "AIVMB002") == 0) != 0) {
        return 1;
    }
    return 0;
}
