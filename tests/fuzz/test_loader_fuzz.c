#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aivm_program.h"

enum {
    DEFAULT_FUZZ_ITERATIONS = 10000U,
    MAX_FUZZ_ITERATIONS = 250000U,
    MAX_MUTATION_BYTES = 768U
};

static int expect_line(int condition, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(stderr, "expect failed at line %d\n", line);
    return 1;
}

#define expect(condition) expect_line((condition), __LINE__)

static size_t read_budget(const char* name, size_t default_value, size_t max_value)
{
    const char* raw = getenv(name);
    char* end = NULL;
    unsigned long long parsed;

    if (raw == NULL || raw[0] == '\0') {
        return default_value;
    }
    parsed = strtoull(raw, &end, 10);
    if (end == raw || *end != '\0' || parsed == 0ULL) {
        return default_value;
    }
    if (parsed > (unsigned long long)max_value) {
        return max_value;
    }
    return (size_t)parsed;
}

static uint64_t next_random(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value << 13U;
    value ^= value >> 7U;
    value ^= value << 17U;
    *state = value;
    return value;
}

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

static int is_known_status(AivmProgramStatus status)
{
    switch (status) {
        case AIVM_PROGRAM_OK:
        case AIVM_PROGRAM_ERR_NULL:
        case AIVM_PROGRAM_ERR_TRUNCATED:
        case AIVM_PROGRAM_ERR_BAD_MAGIC:
        case AIVM_PROGRAM_ERR_UNSUPPORTED:
        case AIVM_PROGRAM_ERR_SECTION_OOB:
        case AIVM_PROGRAM_ERR_SECTION_LIMIT:
        case AIVM_PROGRAM_ERR_INSTRUCTION_LIMIT:
        case AIVM_PROGRAM_ERR_INVALID_SECTION:
        case AIVM_PROGRAM_ERR_INVALID_OPCODE:
        case AIVM_PROGRAM_ERR_CONSTANT_LIMIT:
        case AIVM_PROGRAM_ERR_INVALID_CONSTANT:
        case AIVM_PROGRAM_ERR_STRING_LIMIT:
        case AIVM_PROGRAM_ERR_MEMORY:
            return 1;
        default:
            return 0;
    }
}

static int validate_loaded_program(const AivmProgram* program)
{
    if (expect(program != NULL) != 0) {
        return 1;
    }
    if (expect(program->section_count <= AIVM_PROGRAM_MAX_SECTIONS) != 0) {
        return 1;
    }
    if (expect(program->string_storage_used <= AIVM_PROGRAM_MAX_STRING_BYTES) != 0) {
        return 1;
    }
    if (expect(program->bytes_storage_used <= AIVM_PROGRAM_MAX_BYTES_STORAGE) != 0) {
        return 1;
    }
    if (program->instruction_count > 0U) {
        if (program->instruction_count <= AIVM_PROGRAM_INLINE_INSTRUCTIONS &&
            expect(program->instructions == program->instruction_storage) != 0) {
            return 1;
        }
        if (program->instruction_count > AIVM_PROGRAM_INLINE_INSTRUCTIONS &&
            expect(program->instructions == program->allocated_instruction_storage) != 0) {
            return 1;
        }
    }
    if (program->constant_count > 0U) {
        if (program->constant_count <= AIVM_PROGRAM_INLINE_CONSTANTS &&
            expect(program->constants == program->constant_storage) != 0) {
            return 1;
        }
        if (program->constant_count > AIVM_PROGRAM_INLINE_CONSTANTS &&
            expect(program->constants == program->allocated_constant_storage) != 0) {
            return 1;
        }
    }
    return 0;
}

static int load_and_validate(const uint8_t* bytes, size_t byte_count)
{
    AivmProgram program;
    AivmProgramLoadResult result = aivm_program_load_aibc1(bytes, byte_count, &program);

    if (expect(is_known_status(result.status) != 0) != 0) {
        return 1;
    }
    if (result.status == AIVM_PROGRAM_OK) {
        if (validate_loaded_program(&program) != 0) {
            return 1;
        }
    } else if (bytes != NULL && expect(result.error_offset <= byte_count) != 0) {
        return 1;
    }

    aivm_program_release(&program);
    return 0;
}

static size_t build_instruction_program(
    uint8_t* bytes,
    size_t capacity,
    uint32_t instruction_count,
    uint32_t opcode_seed)
{
    size_t cursor;
    uint32_t index;

    if (capacity < 28U || instruction_count > 32U) {
        return 0U;
    }

    bytes[0] = (uint8_t)'A';
    bytes[1] = (uint8_t)'I';
    bytes[2] = (uint8_t)'B';
    bytes[3] = (uint8_t)'C';
    write_u32_le(bytes, 4U, 2U);
    write_u32_le(bytes, 8U, 0U);
    write_u32_le(bytes, 12U, 1U);
    write_u32_le(bytes, 16U, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
    write_u32_le(bytes, 20U, 4U + (instruction_count * 12U));
    write_u32_le(bytes, 24U, instruction_count);
    cursor = 28U;

    for (index = 0U; index < instruction_count; index += 1U) {
        uint32_t opcode = (index + opcode_seed) % ((uint32_t)AIVM_OP_MAX + 1U);
        if (index + 1U == instruction_count) {
            opcode = (uint32_t)AIVM_OP_HALT;
        } else if (opcode == (uint32_t)AIVM_OP_STUB ||
                   opcode == (uint32_t)AIVM_OP_CALL_SYS ||
                   opcode == (uint32_t)AIVM_OP_ASYNC_CALL_SYS) {
            opcode = (uint32_t)AIVM_OP_NOP;
        }
        write_u32_le(bytes, cursor, opcode);
        write_i64_le(bytes, cursor + 4U, (int64_t)(index * 17U));
        cursor += 12U;
    }

    return cursor;
}

static size_t build_constant_program(
    uint8_t* bytes,
    size_t capacity,
    uint32_t constant_count,
    uint64_t seed)
{
    size_t cursor;
    uint32_t index;

    if (capacity < 28U || constant_count > 32U) {
        return 0U;
    }

    bytes[0] = (uint8_t)'A';
    bytes[1] = (uint8_t)'I';
    bytes[2] = (uint8_t)'B';
    bytes[3] = (uint8_t)'C';
    write_u32_le(bytes, 4U, 2U);
    write_u32_le(bytes, 8U, 0U);
    write_u32_le(bytes, 12U, 1U);
    write_u32_le(bytes, 16U, AIVM_PROGRAM_SECTION_CONSTANTS);
    cursor = 24U;
    write_u32_le(bytes, cursor, constant_count);
    cursor += 4U;

    for (index = 0U; index < constant_count; index += 1U) {
        uint8_t kind = (uint8_t)((index + (uint32_t)seed) % 7U);
        if (kind == 0U) {
            kind = 1U;
        }
        bytes[cursor] = kind;
        cursor += 1U;
        if (kind == 1U || kind == 7U) {
            write_i64_le(bytes, cursor, (int64_t)(seed + index));
            cursor += 8U;
        } else if (kind == 2U) {
            bytes[cursor] = (uint8_t)(index & 1U);
            cursor += 1U;
        } else if (kind == 3U) {
            uint32_t length = (uint32_t)((seed + index) % 13U);
            uint32_t char_index;
            write_u32_le(bytes, cursor, length);
            cursor += 4U;
            for (char_index = 0U; char_index < length; char_index += 1U) {
                bytes[cursor] = (uint8_t)('a' + ((char_index + index) % 26U));
                cursor += 1U;
            }
        } else if (kind == 5U) {
            uint32_t length = (uint32_t)((seed + index) % 17U);
            uint32_t byte_index;
            write_u32_le(bytes, cursor, length);
            cursor += 4U;
            for (byte_index = 0U; byte_index < length; byte_index += 1U) {
                bytes[cursor] = (uint8_t)(byte_index ^ index);
                cursor += 1U;
            }
        }
    }

    write_u32_le(bytes, 20U, (uint32_t)(cursor - 24U));
    return cursor;
}

static int run_null_cases(void)
{
    AivmProgram program;

    if (expect(aivm_program_load_aibc1(NULL, 0U, &program).status == AIVM_PROGRAM_ERR_NULL) != 0) {
        return 1;
    }
    if (expect(aivm_program_load_aibc1((const uint8_t*)"AIBC", 4U, NULL).status == AIVM_PROGRAM_ERR_NULL) != 0) {
        return 1;
    }
    return 0;
}

static int run_boundary_cases(void)
{
    uint8_t bytes[MAX_MUTATION_BYTES];
    static const uint32_t section_counts[] = { 0U, 1U, 2U, 31U, 32U, 33U, UINT32_MAX };
    static const uint32_t section_sizes[] = { 0U, 1U, 3U, 4U, 15U, 16U, 17U, 64U, UINT32_MAX };
    size_t index;
    size_t size_index;

    for (index = 0U; index < sizeof(section_counts) / sizeof(section_counts[0]); index += 1U) {
        memset(bytes, 0, sizeof(bytes));
        bytes[0] = (uint8_t)'A';
        bytes[1] = (uint8_t)'I';
        bytes[2] = (uint8_t)'B';
        bytes[3] = (uint8_t)'C';
        write_u32_le(bytes, 4U, 2U);
        write_u32_le(bytes, 12U, section_counts[index]);
        if (load_and_validate(bytes, sizeof(bytes)) != 0) {
            return 1;
        }
    }

    for (size_index = 0U; size_index < sizeof(section_sizes) / sizeof(section_sizes[0]); size_index += 1U) {
        memset(bytes, 0, sizeof(bytes));
        bytes[0] = (uint8_t)'A';
        bytes[1] = (uint8_t)'I';
        bytes[2] = (uint8_t)'B';
        bytes[3] = (uint8_t)'C';
        write_u32_le(bytes, 4U, 2U);
        write_u32_le(bytes, 12U, 1U);
        write_u32_le(bytes, 16U, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
        write_u32_le(bytes, 20U, section_sizes[size_index]);
        if (load_and_validate(bytes, sizeof(bytes)) != 0) {
            return 1;
        }
    }

    return 0;
}

static int run_mutation_campaign(size_t iterations)
{
    uint8_t bytes[MAX_MUTATION_BYTES];
    uint8_t base[MAX_MUTATION_BYTES];
    uint64_t state = 0x6149564d4c6f6164ULL;
    size_t iteration;

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        size_t size;
        size_t mutation_count;
        size_t mutation;
        uint64_t selector = next_random(&state);

        memset(base, 0, sizeof(base));
        if ((selector & 1ULL) == 0ULL) {
            size = build_instruction_program(
                base,
                sizeof(base),
                (uint32_t)((selector % 31ULL) + 1ULL),
                (uint32_t)(selector >> 8U));
        } else {
            size = build_constant_program(
                base,
                sizeof(base),
                (uint32_t)((selector % 17ULL) + 1ULL),
                selector);
        }
        if (size == 0U) {
            return 1;
        }

        memcpy(bytes, base, sizeof(bytes));
        mutation_count = (size_t)((next_random(&state) % 8ULL) + 1ULL);
        for (mutation = 0U; mutation < mutation_count; mutation += 1U) {
            size_t offset = (size_t)(next_random(&state) % sizeof(bytes));
            uint8_t mask = (uint8_t)(1U << (next_random(&state) % 8ULL));
            bytes[offset] ^= mask;
        }
        if ((iteration % 11U) == 0U) {
            size = (size_t)(next_random(&state) % sizeof(bytes));
        }
        if ((iteration % 17U) == 0U && size >= 16U) {
            write_u32_le(bytes, 12U, (uint32_t)next_random(&state));
        }
        if ((iteration % 23U) == 0U && size >= 24U) {
            write_u32_le(bytes, 20U, (uint32_t)next_random(&state));
        }

        if (load_and_validate(bytes, size) != 0) {
            (void)fprintf(stderr, "loader fuzz failed at iteration %zu size %zu\n", iteration, size);
            return 1;
        }
    }

    return 0;
}

int main(void)
{
    size_t iterations = read_budget(
        "AIVM_FUZZ_ITERATIONS",
        DEFAULT_FUZZ_ITERATIONS,
        MAX_FUZZ_ITERATIONS);

    if (run_null_cases() != 0) {
        return 1;
    }
    if (run_boundary_cases() != 0) {
        return 1;
    }
    if (run_mutation_campaign(iterations) != 0) {
        return 1;
    }
    return 0;
}
