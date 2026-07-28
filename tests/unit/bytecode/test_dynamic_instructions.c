#include <stdint.h>
#include <stdlib.h>

#include "aivm_program.h"

enum {
    TEST_INSTRUCTION_COUNT = AIVM_PROGRAM_INLINE_INSTRUCTIONS + 1,
    TEST_INSTRUCTION_PAYLOAD_SIZE = 4 + TEST_INSTRUCTION_COUNT * 12,
    TEST_PROGRAM_SIZE = 16 + 8 + TEST_INSTRUCTION_PAYLOAD_SIZE
};

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
    write_u32_le(bytes, offset, (uint32_t)(raw & 0xffffffffU));
    write_u32_le(bytes, offset + 4U, (uint32_t)(raw >> 32U));
}

static uint8_t* build_program(void)
{
    uint8_t* bytes = (uint8_t*)calloc(TEST_PROGRAM_SIZE, 1U);
    size_t cursor;
    uint32_t index;
    if (bytes == NULL) {
        return NULL;
    }

    bytes[0] = 'A';
    bytes[1] = 'I';
    bytes[2] = 'B';
    bytes[3] = 'C';
    write_u32_le(bytes, 4U, 2U);
    write_u32_le(bytes, 12U, 1U);

    cursor = 16U;
    write_u32_le(bytes, cursor, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
    write_u32_le(bytes, cursor + 4U, TEST_INSTRUCTION_PAYLOAD_SIZE);
    write_u32_le(bytes, cursor + 8U, TEST_INSTRUCTION_COUNT);
    cursor += 12U;
    for (index = 0U; index < TEST_INSTRUCTION_COUNT; index += 1U) {
        write_u32_le(
            bytes,
            cursor,
            index + 1U == TEST_INSTRUCTION_COUNT
                ? (uint32_t)AIVM_OP_HALT
                : (uint32_t)AIVM_OP_NOP);
        write_i64_le(bytes, cursor + 4U, 0);
        cursor += 12U;
    }
    return bytes;
}

int main(void)
{
    uint8_t* bytes = build_program();
    AivmProgram program;
    AivmProgramLoadResult loaded;
    int failed = 0;

    if (bytes == NULL) {
        return 1;
    }
    loaded = aivm_program_load_aibc1(bytes, TEST_PROGRAM_SIZE, &program);
    if (loaded.status != AIVM_PROGRAM_OK ||
        program.instruction_count != TEST_INSTRUCTION_COUNT ||
        program.instruction_capacity != TEST_INSTRUCTION_COUNT ||
        program.allocated_instruction_storage == NULL ||
        program.instructions != program.allocated_instruction_storage ||
        program.instructions[TEST_INSTRUCTION_COUNT - 1U].opcode != AIVM_OP_HALT) {
        failed = 1;
    }

    aivm_program_release(&program);
    free(bytes);
    return failed;
}
