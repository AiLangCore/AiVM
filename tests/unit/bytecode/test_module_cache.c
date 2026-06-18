#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aivm_module_cache.h"

static int expect_line(int condition, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(stderr, "expect failed at line %d\n", line);
    return 1;
}

#define expect(condition) expect_line((condition), __LINE__)

static void write_u32_le(uint8_t* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value & 0xffU);
    bytes[offset + 1U] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[offset + 2U] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[offset + 3U] = (uint8_t)((value >> 24U) & 0xffU);
}

static void write_i64_le(uint8_t* bytes, size_t offset, int64_t value)
{
    uint64_t raw = (uint64_t)value;
    write_u32_le(bytes, offset, (uint32_t)(raw & 0xffffffffULL));
    write_u32_le(bytes, offset + 4U, (uint32_t)((raw >> 32U) & 0xffffffffULL));
}

static size_t build_minimal_aibc(uint8_t* bytes, size_t capacity)
{
    size_t cursor = 0U;
    if (capacity < 56U) {
        return 0U;
    }
    bytes[cursor++] = (uint8_t)'A';
    bytes[cursor++] = (uint8_t)'I';
    bytes[cursor++] = (uint8_t)'B';
    bytes[cursor++] = (uint8_t)'C';
    write_u32_le(bytes, cursor, 2U);
    cursor += 4U;
    write_u32_le(bytes, cursor, 0U);
    cursor += 4U;
    write_u32_le(bytes, cursor, 1U);
    cursor += 4U;
    write_u32_le(bytes, cursor, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
    cursor += 4U;
    write_u32_le(bytes, cursor, 16U);
    cursor += 4U;
    write_u32_le(bytes, cursor, 1U);
    cursor += 4U;
    write_u32_le(bytes, cursor, (uint32_t)AIVM_OP_HALT);
    cursor += 4U;
    write_i64_le(bytes, cursor, 0);
    cursor += 8U;
    return cursor;
}

static int put_get_deep_copies_program(void)
{
    AivmModuleCache cache;
    const AivmProgram* cached = NULL;
    static AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = NULL }
    };
    AivmProgram program;

    aivm_module_cache_init(&cache);
    aivm_program_clear(&program);
    memcpy(program.string_storage, "module", 7U);
    program.string_storage_used = 7U;
    constants[0] = aivm_value_string(program.string_storage);
    program.instructions = instructions;
    program.instruction_count = sizeof(instructions) / sizeof(instructions[0]);
    program.constants = constants;
    program.constant_count = 1U;

    if (expect(aivm_module_cache_put(&cache, "core/main", &program) == AIVM_MODULE_CACHE_OK) != 0 ||
        expect(aivm_module_cache_get(&cache, "core/main", &cached) == AIVM_MODULE_CACHE_OK) != 0 ||
        expect(cached != NULL) != 0 ||
        expect(cached->instruction_count == 2U) != 0 ||
        expect(cached->instructions != instructions) != 0 ||
        expect(cached->constants != constants) != 0 ||
        expect(cached->constants[0].type == AIVM_VAL_STRING) != 0 ||
        expect(strcmp(cached->constants[0].string_value, "module") == 0) != 0 ||
        expect(cached->constants[0].string_value != program.string_storage) != 0) {
        return 1;
    }

    instructions[0].opcode = AIVM_OP_HALT;
    program.string_storage[0] = 'X';
    if (expect(cached->instructions[0].opcode == AIVM_OP_CONST) != 0 ||
        expect(strcmp(cached->constants[0].string_value, "module") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int duplicate_and_missing_are_deterministic(void)
{
    AivmModuleCache cache;
    const AivmProgram* cached = NULL;
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

    aivm_module_cache_init(&cache);
    if (expect(aivm_module_cache_put(&cache, "one", &program) == AIVM_MODULE_CACHE_OK) != 0 ||
        expect(aivm_module_cache_put(&cache, "one", &program) == AIVM_MODULE_CACHE_ERR_DUPLICATE) != 0 ||
        expect(aivm_module_cache_get(&cache, "missing", &cached) == AIVM_MODULE_CACHE_ERR_NOT_FOUND) != 0 ||
        expect(cached == NULL) != 0) {
        return 1;
    }
    return 0;
}

static int module_limit_is_deterministic(void)
{
    AivmModuleCache cache;
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
    size_t index;

    aivm_module_cache_init(&cache);
    for (index = 0U; index < AIVM_MODULE_CACHE_MAX_MODULES; index += 1U) {
        char name[32];
        (void)snprintf(name, sizeof(name), "module_%zu", index);
        if (expect(aivm_module_cache_put(&cache, name, &program) == AIVM_MODULE_CACHE_OK) != 0) {
            return 1;
        }
    }
    if (expect(aivm_module_cache_put(&cache, "overflow", &program) == AIVM_MODULE_CACHE_ERR_LIMIT) != 0 ||
        expect(aivm_module_cache_count(&cache) == AIVM_MODULE_CACHE_MAX_MODULES) != 0) {
        return 1;
    }
    return 0;
}

static int load_aibc1_into_cache(void)
{
    AivmModuleCache cache;
    uint8_t bytes[64];
    size_t byte_count = build_minimal_aibc(bytes, sizeof(bytes));
    AivmProgramLoadResult load_result;
    const AivmProgram* cached = NULL;

    aivm_module_cache_init(&cache);
    if (expect(byte_count > 0U) != 0 ||
        expect(aivm_module_cache_load_aibc1(&cache, "loaded", bytes, byte_count, &load_result) == AIVM_MODULE_CACHE_OK) != 0 ||
        expect(load_result.status == AIVM_PROGRAM_OK) != 0 ||
        expect(aivm_module_cache_get(&cache, "loaded", &cached) == AIVM_MODULE_CACHE_OK) != 0 ||
        expect(cached != NULL) != 0 ||
        expect(cached->instruction_count == 1U) != 0 ||
        expect(cached->instructions[0].opcode == AIVM_OP_HALT) != 0 ||
        expect(aivm_module_cache_estimated_bytes(&cache) > 0U) != 0) {
        return 1;
    }
    bytes[28] = (uint8_t)AIVM_OP_NOP;
    if (expect(cached->instructions[0].opcode == AIVM_OP_HALT) != 0) {
        return 1;
    }
    return 0;
}

int main(void)
{
    if (put_get_deep_copies_program() != 0) {
        return 1;
    }
    if (duplicate_and_missing_are_deterministic() != 0) {
        return 1;
    }
    if (module_limit_is_deterministic() != 0) {
        return 1;
    }
    if (load_aibc1_into_cache() != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_module_cache_status_code(AIVM_MODULE_CACHE_ERR_DUPLICATE), "AIVMMOD003") == 0) != 0) {
        return 1;
    }
    return 0;
}
