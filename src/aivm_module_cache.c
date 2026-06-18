#include "aivm_module_cache.h"

#include <stdio.h>
#include <string.h>

static int size_add_checked(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return 0;
    }
    if (a > ((size_t)-1) - b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int valid_name(const char* name)
{
    size_t length;
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    length = strlen(name);
    return length < AIVM_MODULE_CACHE_MAX_NAME_LENGTH;
}

static size_t estimate_program_bytes(const AivmProgram* program)
{
    size_t total = sizeof(AivmProgram);
    size_t add = 0U;
    if (program == NULL) {
        return 0U;
    }
    if (size_add_checked(total, program->instruction_count * sizeof(AivmInstruction), &add)) {
        total = add;
    }
    if (size_add_checked(total, program->constant_count * sizeof(AivmValue), &add)) {
        total = add;
    }
    if (size_add_checked(total, program->string_storage_used, &add)) {
        total = add;
    }
    if (size_add_checked(total, program->bytes_storage_used, &add)) {
        total = add;
    }
    return total;
}

static int pointer_in_range(const void* pointer, const void* start, size_t length, size_t* out_offset)
{
    const unsigned char* p = (const unsigned char*)pointer;
    const unsigned char* s = (const unsigned char*)start;
    if (pointer == NULL || start == NULL || out_offset == NULL) {
        return 0;
    }
    if (p < s || p > s + length) {
        return 0;
    }
    *out_offset = (size_t)(p - s);
    return 1;
}

static int copy_program(AivmProgram* destination, const AivmProgram* source)
{
    size_t index;
    if (destination == NULL || source == NULL) {
        return 0;
    }
    if (source->instruction_count > AIVM_PROGRAM_MAX_INSTRUCTIONS ||
        source->constant_count > AIVM_PROGRAM_MAX_CONSTANTS ||
        source->section_count > AIVM_PROGRAM_MAX_SECTIONS ||
        source->string_storage_used > AIVM_PROGRAM_MAX_STRING_BYTES ||
        source->bytes_storage_used > AIVM_PROGRAM_MAX_BYTES_STORAGE) {
        return 0;
    }

    aivm_program_clear(destination);
    destination->format_version = source->format_version;
    destination->format_flags = source->format_flags;
    destination->section_count = source->section_count;
    destination->instruction_count = source->instruction_count;
    destination->constant_count = source->constant_count;
    destination->string_storage_used = source->string_storage_used;
    destination->bytes_storage_used = source->bytes_storage_used;

    for (index = 0U; index < source->section_count; index += 1U) {
        destination->sections[index] = source->sections[index];
    }
    for (index = 0U; index < source->instruction_count; index += 1U) {
        destination->instruction_storage[index] = source->instructions[index];
    }
    if (source->string_storage_used > 0U) {
        memcpy(destination->string_storage, source->string_storage, source->string_storage_used);
    }
    if (source->bytes_storage_used > 0U) {
        memcpy(destination->bytes_storage, source->bytes_storage, source->bytes_storage_used);
    }
    for (index = 0U; index < source->constant_count; index += 1U) {
        AivmValue value = source->constants[index];
        if (value.type == AIVM_VAL_STRING) {
            size_t offset = 0U;
            if (!pointer_in_range(value.string_value, source->string_storage, source->string_storage_used, &offset)) {
                return 0;
            }
            value.string_value = &destination->string_storage[offset];
        } else if (value.type == AIVM_VAL_BYTES) {
            size_t offset = 0U;
            if (value.bytes_value.length > 0U &&
                !pointer_in_range(value.bytes_value.data, source->bytes_storage, source->bytes_storage_used, &offset)) {
                return 0;
            }
            value.bytes_value.data = value.bytes_value.length == 0U ? NULL : &destination->bytes_storage[offset];
        }
        destination->constant_storage[index] = value;
    }
    destination->instructions = destination->instruction_count == 0U ? NULL : destination->instruction_storage;
    destination->constants = destination->constant_count == 0U ? NULL : destination->constant_storage;
    return 1;
}

static AivmModuleCacheEntry* find_entry(AivmModuleCache* cache, const char* name)
{
    size_t index;
    if (cache == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0U; index < AIVM_MODULE_CACHE_MAX_MODULES; index += 1U) {
        if (cache->entries[index].active != 0 && strcmp(cache->entries[index].name, name) == 0) {
            return &cache->entries[index];
        }
    }
    return NULL;
}

static const AivmModuleCacheEntry* find_entry_const(const AivmModuleCache* cache, const char* name)
{
    size_t index;
    if (cache == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0U; index < AIVM_MODULE_CACHE_MAX_MODULES; index += 1U) {
        if (cache->entries[index].active != 0 && strcmp(cache->entries[index].name, name) == 0) {
            return &cache->entries[index];
        }
    }
    return NULL;
}

void aivm_module_cache_init(AivmModuleCache* cache)
{
    if (cache == NULL) {
        return;
    }
    memset(cache, 0, sizeof(*cache));
}

void aivm_module_cache_clear(AivmModuleCache* cache)
{
    if (cache == NULL) {
        return;
    }
    aivm_module_cache_init(cache);
}

AivmModuleCacheStatus aivm_module_cache_put(
    AivmModuleCache* cache,
    const char* name,
    const AivmProgram* program)
{
    size_t index;
    size_t estimate;
    size_t needed;
    AivmModuleCacheEntry* slot = NULL;

    if (cache == NULL || !valid_name(name) || program == NULL) {
        return AIVM_MODULE_CACHE_ERR_INVALID;
    }
    if (find_entry(cache, name) != NULL) {
        return AIVM_MODULE_CACHE_ERR_DUPLICATE;
    }
    if (cache->count >= AIVM_MODULE_CACHE_MAX_MODULES) {
        return AIVM_MODULE_CACHE_ERR_LIMIT;
    }
    estimate = estimate_program_bytes(program);
    if (!size_add_checked(cache->estimated_bytes_used, estimate, &needed) ||
        needed > AIVM_MODULE_CACHE_MAX_BYTES) {
        return AIVM_MODULE_CACHE_ERR_LIMIT;
    }
    for (index = 0U; index < AIVM_MODULE_CACHE_MAX_MODULES; index += 1U) {
        if (cache->entries[index].active == 0) {
            slot = &cache->entries[index];
            break;
        }
    }
    if (slot == NULL) {
        return AIVM_MODULE_CACHE_ERR_LIMIT;
    }
    memset(slot, 0, sizeof(*slot));
    if (!copy_program(&slot->program, program)) {
        memset(slot, 0, sizeof(*slot));
        return AIVM_MODULE_CACHE_ERR_INVALID;
    }
    (void)snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->estimated_bytes = estimate;
    slot->active = 1;
    cache->count += 1U;
    cache->estimated_bytes_used = needed;
    return AIVM_MODULE_CACHE_OK;
}

AivmModuleCacheStatus aivm_module_cache_get(
    const AivmModuleCache* cache,
    const char* name,
    const AivmProgram** out_program)
{
    const AivmModuleCacheEntry* entry;
    if (out_program == NULL || !valid_name(name)) {
        return AIVM_MODULE_CACHE_ERR_INVALID;
    }
    *out_program = NULL;
    entry = find_entry_const(cache, name);
    if (entry == NULL) {
        return AIVM_MODULE_CACHE_ERR_NOT_FOUND;
    }
    *out_program = &entry->program;
    return AIVM_MODULE_CACHE_OK;
}

AivmModuleCacheStatus aivm_module_cache_load_aibc1(
    AivmModuleCache* cache,
    const char* name,
    const uint8_t* bytes,
    size_t byte_count,
    AivmProgramLoadResult* out_load_result)
{
    AivmProgram loaded;
    AivmProgramLoadResult load_result;
    if (out_load_result != NULL) {
        out_load_result->status = AIVM_PROGRAM_ERR_NULL;
        out_load_result->error_offset = 0U;
    }
    if (cache == NULL || !valid_name(name) || bytes == NULL) {
        return AIVM_MODULE_CACHE_ERR_INVALID;
    }
    load_result = aivm_program_load_aibc1(bytes, byte_count, &loaded);
    if (out_load_result != NULL) {
        *out_load_result = load_result;
    }
    if (load_result.status != AIVM_PROGRAM_OK) {
        return AIVM_MODULE_CACHE_ERR_INVALID;
    }
    return aivm_module_cache_put(cache, name, &loaded);
}

size_t aivm_module_cache_count(const AivmModuleCache* cache)
{
    return cache == NULL ? 0U : cache->count;
}

size_t aivm_module_cache_estimated_bytes(const AivmModuleCache* cache)
{
    return cache == NULL ? 0U : cache->estimated_bytes_used;
}

const char* aivm_module_cache_status_code(AivmModuleCacheStatus status)
{
    switch (status) {
        case AIVM_MODULE_CACHE_OK:
            return "AIVMMOD000";
        case AIVM_MODULE_CACHE_ERR_INVALID:
            return "AIVMMOD001";
        case AIVM_MODULE_CACHE_ERR_LIMIT:
            return "AIVMMOD002";
        case AIVM_MODULE_CACHE_ERR_DUPLICATE:
            return "AIVMMOD003";
        case AIVM_MODULE_CACHE_ERR_NOT_FOUND:
            return "AIVMMOD004";
        default:
            return "AIVMMOD999";
    }
}
