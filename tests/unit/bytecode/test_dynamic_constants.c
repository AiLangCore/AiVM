#include <stdint.h>
#include <stdlib.h>

#include "aivm_program.h"
#include "aivm_runtime.h"
#include "aivm_vm.h"

enum {
    TEST_CONSTANT_COUNT = AIVM_PROGRAM_INLINE_CONSTANTS + 1,
    TEST_INSTRUCTION_PAYLOAD_SIZE = 28,
    TEST_CONSTANT_PAYLOAD_SIZE = 4 + TEST_CONSTANT_COUNT * 9,
    TEST_PROGRAM_SIZE = 16 + 8 + TEST_INSTRUCTION_PAYLOAD_SIZE + 8 + TEST_CONSTANT_PAYLOAD_SIZE
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
    write_u32_le(bytes, 12U, 2U);

    cursor = 16U;
    write_u32_le(bytes, cursor, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
    write_u32_le(bytes, cursor + 4U, TEST_INSTRUCTION_PAYLOAD_SIZE);
    write_u32_le(bytes, cursor + 8U, 2U);
    write_u32_le(bytes, cursor + 12U, (uint32_t)AIVM_OP_CONST);
    write_i64_le(bytes, cursor + 16U, TEST_CONSTANT_COUNT - 1);
    write_u32_le(bytes, cursor + 24U, (uint32_t)AIVM_OP_HALT);
    write_i64_le(bytes, cursor + 28U, 0);

    cursor += 8U + TEST_INSTRUCTION_PAYLOAD_SIZE;
    write_u32_le(bytes, cursor, AIVM_PROGRAM_SECTION_CONSTANTS);
    write_u32_le(bytes, cursor + 4U, TEST_CONSTANT_PAYLOAD_SIZE);
    write_u32_le(bytes, cursor + 8U, TEST_CONSTANT_COUNT);
    cursor += 12U;
    for (index = 0U; index < TEST_CONSTANT_COUNT; index += 1U) {
        bytes[cursor] = 1U;
        write_i64_le(bytes, cursor + 1U, (int64_t)index);
        cursor += 9U;
    }
    return bytes;
}

int main(void)
{
    uint8_t* bytes = build_program();
    AivmProgram program;
    static AivmVm vm;
    AivmProgramLoadResult loaded;
    int failed = 0;

    if (bytes == NULL) {
        return 1;
    }
    loaded = aivm_program_load_aibc1(bytes, TEST_PROGRAM_SIZE, &program);
    if (loaded.status != AIVM_PROGRAM_OK ||
        program.constant_count != TEST_CONSTANT_COUNT ||
        program.constant_capacity != TEST_CONSTANT_COUNT ||
        program.allocated_constant_storage == NULL ||
        program.constants[TEST_CONSTANT_COUNT - 1].type != AIVM_VAL_INT ||
        program.constants[TEST_CONSTANT_COUNT - 1].int_value != TEST_CONSTANT_COUNT - 1) {
        failed = 1;
    } else if (!aivm_execute_program(&program, &vm) ||
               vm.stack_count != 1U ||
               vm.stack[0].type != AIVM_VAL_INT ||
               vm.stack[0].int_value != TEST_CONSTANT_COUNT - 1) {
        failed = 1;
    }

    aivm_dispose(&vm);
    aivm_program_release(&program);
    free(bytes);
    return failed;
}
