#include "aivm_program.h"
#include "aivm_worker_catalog.h"

#include <stdio.h>
#include <string.h>

static void write_u32(uint8_t* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static void make_worker_artifact(uint8_t artifact[40])
{
    memset(artifact, 0, 40U);
    memcpy(artifact, "AIBC", 4U);
    write_u32(artifact, 4U, 2U);
    write_u32(artifact, 12U, 1U);
    write_u32(artifact, 16U, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
    write_u32(artifact, 20U, 16U);
    write_u32(artifact, 24U, 1U);
    write_u32(artifact, 28U, AIVM_OP_HALT);
}

static void make_catalog(uint8_t catalog[96])
{
    uint8_t identity[AIVM_WORKER_IDENTITY_BYTES];
    make_worker_artifact(catalog + 56U);
    write_u32(catalog, 0U, 1U);
    write_u32(catalog, 4U, 0U);
    write_u32(catalog, 8U, 1U);
    write_u32(catalog, 12U, AIVM_WORKER_TRANSPORT_ABI_BYTES_V1);
    write_u32(catalog, 16U, 2U);
    write_u32(catalog, 20U, 40U);
    aivm_worker_identity(0U, 1U, AIVM_WORKER_TRANSPORT_ABI_BYTES_V1,
        2U, catalog + 56U, 40U, identity);
    memcpy(catalog + 24U, identity, sizeof(identity));
}

int main(void)
{
    static const uint8_t expected_identity[AIVM_WORKER_IDENTITY_BYTES] = {
        0x74U, 0xa3U, 0xc2U, 0x3bU, 0x56U, 0xfdU, 0xa5U, 0x2aU,
        0xccU, 0xc3U, 0x1bU, 0x03U, 0xeeU, 0xa5U, 0x9bU, 0x00U,
        0xa0U, 0xb5U, 0x6eU, 0xf8U, 0xc0U, 0x5eU, 0xb8U, 0x3eU,
        0x90U, 0x4dU, 0xa5U, 0x32U, 0xbcU, 0xe8U, 0xe1U, 0x96U
    };
    uint8_t bytes[120] = {0};
    uint8_t identity[AIVM_WORKER_IDENTITY_BYTES];
    AivmProgram program;
    AivmProgramLoadResult result;
    memcpy(bytes, "AIBC", 4U);
    write_u32(bytes, 4U, 2U);
    write_u32(bytes, 12U, 1U);
    write_u32(bytes, 16U, AIVM_PROGRAM_SECTION_WORKER_CATALOG);
    write_u32(bytes, 20U, 96U);
    make_catalog(bytes + 24U);
    aivm_worker_identity(0U, 5U, 1U, 2U, bytes + 80U, 40U, identity);
    if (expect(memcmp(identity, expected_identity, sizeof(identity)) == 0) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(bytes, sizeof(bytes), &program);
    if (expect(result.status == AIVM_PROGRAM_OK) != 0 ||
        expect(program.worker_catalog.count == 1U) != 0 ||
        expect(program.worker_catalog.entries[0].function_target == 0U) != 0) {
        return 1;
    }
    aivm_program_release(&program);

    bytes[119U] ^= 1U;
    result = aivm_program_load_aibc1(bytes, sizeof(bytes), &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_WORKER_CATALOG) != 0) {
        return 1;
    }

    make_catalog(bytes + 24U);
    write_u32(bytes, 28U, 2U);
    {
        uint8_t identity[AIVM_WORKER_IDENTITY_BYTES];
        aivm_worker_identity(2U, 1U, 1U, 2U, bytes + 80U, 40U, identity);
        memcpy(bytes + 48U, identity, sizeof(identity));
    }
    result = aivm_program_load_aibc1(bytes, sizeof(bytes), &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_WORKER_CATALOG) != 0) {
        return 1;
    }

    if (expect(aivm_value_equals(aivm_value_worker_ref(1), aivm_value_worker_ref(1)) == 0) != 0 ||
        expect(aivm_value_is_immutable_message_payload(aivm_value_worker_ref(1)) == 0) != 0) {
        return 1;
    }
    (void)printf("worker catalog tests passed\n");
    return 0;
}
