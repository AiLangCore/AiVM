#include "aivm_worker_catalog.h"

#include "aivm_program.h"

#include <stdlib.h>
#include <string.h>

static uint32_t read_u32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

/*
 * This is a deterministic 256-bit content identity, not an authentication
 * primitive. Package signature/trust policy remains outside AiVM.
 */
void aivm_worker_identity(
    uint32_t function_target,
    uint32_t required_capabilities,
    uint32_t transport_abi,
    uint32_t bytecode_version,
    const uint8_t* artifact,
    size_t artifact_length,
    uint8_t out_identity[AIVM_WORKER_IDENTITY_BYTES])
{
    uint32_t state[8] = {
        UINT32_C(2166136261), UINT32_C(2246822519),
        UINT32_C(3266489917), UINT32_C(668265263),
        UINT32_C(374761393), UINT32_C(1274126177),
        UINT32_C(42595009), UINT32_C(2048419325)
    };
    uint32_t fields[4] = {
        function_target, required_capabilities, transport_abi, bytecode_version
    };
    size_t i;
    size_t field_index;
    size_t lane;
    for (field_index = 0U; field_index < 4U; field_index += 1U) {
        for (i = 0U; i < 4U; i += 1U) {
            uint8_t value = (uint8_t)(fields[field_index] >> (i * 8U));
            for (lane = 0U; lane < 8U; lane += 1U) {
                state[lane] = (state[lane] * UINT32_C(16777619)) +
                    (uint32_t)value + (uint32_t)lane;
            }
        }
    }
    for (i = 0U; i < artifact_length; i += 1U) {
        for (lane = 0U; lane < 8U; lane += 1U) {
            state[lane] = (state[lane] * UINT32_C(16777619)) +
                (uint32_t)artifact[i] + (uint32_t)lane;
        }
    }
    for (lane = 0U; lane < 8U; lane += 1U) {
        for (i = 0U; i < 4U; i += 1U) {
            out_identity[(lane * 4U) + i] = (uint8_t)(state[lane] >> (i * 8U));
        }
    }
}

void aivm_worker_catalog_clear(AivmWorkerCatalog* catalog)
{
    if (catalog != NULL) {
        catalog->entries = NULL;
        catalog->count = 0U;
    }
}

void aivm_worker_catalog_release(AivmWorkerCatalog* catalog)
{
    size_t index;
    if (catalog == NULL) {
        return;
    }
    for (index = 0U; index < catalog->count; index += 1U) {
        free(catalog->entries[index].artifact);
    }
    free(catalog->entries);
    aivm_worker_catalog_clear(catalog);
}

int aivm_worker_catalog_copy(
    AivmWorkerCatalog* destination,
    const AivmWorkerCatalog* source)
{
    size_t index;
    if (destination == NULL || source == NULL) {
        return 0;
    }
    aivm_worker_catalog_clear(destination);
    if (source->count == 0U) {
        return 1;
    }
    destination->entries = (AivmWorkerCatalogEntry*)calloc(
        source->count, sizeof(*destination->entries));
    if (destination->entries == NULL) {
        return 0;
    }
    destination->count = source->count;
    for (index = 0U; index < source->count; index += 1U) {
        destination->entries[index] = source->entries[index];
        destination->entries[index].artifact = (uint8_t*)malloc(
            source->entries[index].artifact_length == 0U
                ? 1U : source->entries[index].artifact_length);
        if (destination->entries[index].artifact == NULL) {
            destination->entries[index].artifact_length = 0U;
            aivm_worker_catalog_release(destination);
            return 0;
        }
        memcpy(destination->entries[index].artifact,
            source->entries[index].artifact,
            source->entries[index].artifact_length);
    }
    return 1;
}

AivmWorkerCatalogStatus aivm_worker_catalog_load(
    const uint8_t* bytes,
    size_t byte_count,
    AivmWorkerCatalog* catalog)
{
    size_t cursor = 4U;
    size_t index;
    uint32_t count;
    if (bytes == NULL || catalog == NULL || byte_count < 4U) {
        return AIVM_WORKER_CATALOG_ERR_FORMAT;
    }
    aivm_worker_catalog_clear(catalog);
    count = read_u32(bytes);
    if (count > AIVM_WORKER_CATALOG_MAX_ENTRIES) {
        return AIVM_WORKER_CATALOG_ERR_LIMIT;
    }
    if (count == 0U) {
        return byte_count == 4U ? AIVM_WORKER_CATALOG_OK : AIVM_WORKER_CATALOG_ERR_FORMAT;
    }
    catalog->entries = (AivmWorkerCatalogEntry*)calloc(count, sizeof(*catalog->entries));
    if (catalog->entries == NULL) {
        return AIVM_WORKER_CATALOG_ERR_MEMORY;
    }
    catalog->count = count;
    for (index = 0U; index < count; index += 1U) {
        AivmWorkerCatalogEntry* entry = &catalog->entries[index];
        uint8_t actual[AIVM_WORKER_IDENTITY_BYTES];
        AivmProgram worker_program;
        AivmProgramLoadResult loaded;
        uint32_t length;
        if (cursor > byte_count || byte_count - cursor < 52U) {
            aivm_worker_catalog_release(catalog);
            return AIVM_WORKER_CATALOG_ERR_FORMAT;
        }
        entry->function_target = read_u32(bytes + cursor);
        entry->required_capabilities = read_u32(bytes + cursor + 4U);
        entry->transport_abi = read_u32(bytes + cursor + 8U);
        entry->bytecode_version = read_u32(bytes + cursor + 12U);
        length = read_u32(bytes + cursor + 16U);
        memcpy(entry->identity, bytes + cursor + 20U, AIVM_WORKER_IDENTITY_BYTES);
        cursor += 52U;
        if ((size_t)length > byte_count - cursor) {
            aivm_worker_catalog_release(catalog);
            return AIVM_WORKER_CATALOG_ERR_FORMAT;
        }
        if (entry->transport_abi != AIVM_WORKER_TRANSPORT_ABI_BYTES_V1) {
            aivm_worker_catalog_release(catalog);
            return AIVM_WORKER_CATALOG_ERR_ABI;
        }
        if ((entry->required_capabilities & UINT32_C(0xffff0000)) != 0U) {
            aivm_worker_catalog_release(catalog);
            return AIVM_WORKER_CATALOG_ERR_CAPABILITY;
        }
        aivm_worker_identity(entry->function_target, entry->required_capabilities,
            entry->transport_abi, entry->bytecode_version, bytes + cursor, length, actual);
        if (memcmp(actual, entry->identity, sizeof(actual)) != 0) {
            aivm_worker_catalog_release(catalog);
            return AIVM_WORKER_CATALOG_ERR_IDENTITY;
        }
        entry->artifact = (uint8_t*)malloc(length == 0U ? 1U : (size_t)length);
        if (entry->artifact == NULL) {
            aivm_worker_catalog_release(catalog);
            return AIVM_WORKER_CATALOG_ERR_MEMORY;
        }
        memcpy(entry->artifact, bytes + cursor, length);
        entry->artifact_length = length;
        loaded = aivm_program_load_aibc1(entry->artifact, entry->artifact_length, &worker_program);
        if (loaded.status != AIVM_PROGRAM_OK || worker_program.format_version != entry->bytecode_version) {
            aivm_program_release(&worker_program);
            aivm_worker_catalog_release(catalog);
            return AIVM_WORKER_CATALOG_ERR_ARTIFACT;
        }
        if ((size_t)entry->function_target >= worker_program.instruction_count) {
            aivm_program_release(&worker_program);
            aivm_worker_catalog_release(catalog);
            return AIVM_WORKER_CATALOG_ERR_FUNCTION;
        }
        aivm_program_release(&worker_program);
        cursor += length;
    }
    if (cursor != byte_count) {
        aivm_worker_catalog_release(catalog);
        return AIVM_WORKER_CATALOG_ERR_FORMAT;
    }
    return AIVM_WORKER_CATALOG_OK;
}
