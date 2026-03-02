#include <stdint.h>

#include "aivm_program.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmProgram program;
    AivmProgramLoadResult result;
    static const uint8_t bad_magic[16] = { 'X', 'I', 'B', 'C', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t truncated[3] = { 'A', 'I', 'B' };
    static const uint8_t unsupported_version[16] = { 'A', 'I', 'B', 'C', 2, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t valid_header[16] = { 'A', 'I', 'B', 'C', 1, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t section_table_truncated[16] = { 'A', 'I', 'B', 'C', 1, 0, 0, 0, 9, 0, 0, 0, 1, 0, 0, 0 };
    static const uint8_t section_oob[24] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        9, 0, 0, 0,
        1, 0, 0, 0,
        3, 0, 0, 0, /* section type */
        4, 0, 0, 0  /* section size larger than remaining payload */
    };
    static const uint8_t one_section_valid[28] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        9, 0, 0, 0,
        1, 0, 0, 0,  /* section count */
        7, 0, 0, 0,  /* section type */
        4, 0, 0, 0,  /* section size */
        1, 2, 3, 4   /* payload */
    };
    static const uint8_t instruction_section_valid[56] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,   /* section type: instructions */
        28, 0, 0, 0,  /* section size */
        2, 0, 0, 0,   /* instruction_count */
        3, 0, 0, 0,   /* PUSH_INT */
        42, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 0, 0,   /* HALT */
        0, 0, 0, 0, 0, 0, 0, 0
    };
    static const uint8_t instruction_section_bad_size[44] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,   /* section type: instructions */
        15, 0, 0, 0,  /* invalid section size for 1 record */
        1, 0, 0, 0,   /* instruction_count */
        3, 0, 0, 0,   /* PUSH_INT */
        1, 0, 0, 0
    };
    static const uint8_t instruction_section_invalid_opcode[44] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,   /* section type: instructions */
        16, 0, 0, 0,  /* section size */
        1, 0, 0, 0,   /* instruction_count */
        99, 0, 0, 0,  /* invalid opcode */
        0, 0, 0, 0
    };
    static const uint8_t constants_section_valid[53] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        2, 0, 0, 0,   /* section type: constants */
        23, 0, 0, 0,  /* section size */
        3, 0, 0, 0,   /* constant_count */
        1,            /* int */
        7, 0, 0, 0, 0, 0, 0, 0,
        2,            /* bool */
        1,
        3,            /* string */
        3, 0, 0, 0,
        'f', 'o', 'o'
    };
    static const uint8_t constants_section_invalid_kind[29] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        2, 0, 0, 0,
        5, 0, 0, 0,
        1, 0, 0, 0,
        9
    };
    static const uint8_t section_limit_exceeded[16] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        9, 0, 0, 0,
        33, 0, 0, 0  /* section count over max (32) */
    };

    result = aivm_program_load_aibc1(NULL, 0U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_NULL) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_string(aivm_program_status_code(result.status)), aivm_value_string("AIVMP001")) == 1) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(valid_header, 16U, NULL);
    if (expect(result.status == AIVM_PROGRAM_ERR_NULL) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(truncated, 3U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_TRUNCATED) != 0) {
        return 1;
    }
    if (expect(result.error_offset == 3U) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_string(aivm_program_status_message(result.status)), aivm_value_string("Program bytes were truncated.")) == 1) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(bad_magic, 16U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_BAD_MAGIC) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(unsupported_version, 16U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_UNSUPPORTED) != 0) {
        return 1;
    }
    if (expect(result.error_offset == 4U) != 0) {
        return 1;
    }
    if (expect(program.format_version == 2U) != 0) {
        return 1;
    }
    if (expect(program.format_flags == 7U) != 0) {
        return 1;
    }
    if (expect(program.section_count == 0U) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(section_table_truncated, 16U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_TRUNCATED) != 0) {
        return 1;
    }
    if (expect(result.error_offset == 16U) != 0) {
        return 1;
    }
    if (expect(program.section_count == 1U) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(section_oob, 24U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_SECTION_OOB) != 0) {
        return 1;
    }
    if (expect(result.error_offset == 24U) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(section_limit_exceeded, 16U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_SECTION_LIMIT) != 0) {
        return 1;
    }
    if (expect(result.error_offset == 12U) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(one_section_valid, 28U, &program);
    if (expect(result.status == AIVM_PROGRAM_OK) != 0) {
        return 1;
    }
    if (expect(program.section_count == 1U) != 0) {
        return 1;
    }
    if (expect(program.sections[0].section_type == 7U) != 0) {
        return 1;
    }
    if (expect(program.sections[0].section_size == 4U) != 0) {
        return 1;
    }
    if (expect(program.sections[0].section_offset == 24U) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(valid_header, 16U, &program);
    if (expect(result.status == AIVM_PROGRAM_OK) != 0) {
        return 1;
    }
    if (expect(result.error_offset == 0U) != 0) {
        return 1;
    }
    if (expect(program.format_version == 1U) != 0) {
        return 1;
    }
    if (expect(program.format_flags == 9U) != 0) {
        return 1;
    }
    if (expect(program.section_count == 0U) != 0) {
        return 1;
    }
    if (expect(program.instructions == NULL) != 0) {
        return 1;
    }
    if (expect(program.instruction_count == 0U) != 0) {
        return 1;
    }
    if (expect(program.constants == NULL) != 0) {
        return 1;
    }
    if (expect(program.constant_count == 0U) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(instruction_section_valid, 56U, &program);
    if (expect(result.status == AIVM_PROGRAM_OK) != 0) {
        return 1;
    }
    if (expect(program.instructions != NULL) != 0) {
        return 1;
    }
    if (expect(program.instruction_count == 2U) != 0) {
        return 1;
    }
    if (expect(program.instructions[0].opcode == AIVM_OP_PUSH_INT) != 0) {
        return 1;
    }
    if (expect(program.instructions[0].operand_int == 42) != 0) {
        return 1;
    }
    if (expect(program.instructions[1].opcode == AIVM_OP_HALT) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(instruction_section_bad_size, 44U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_INVALID_SECTION) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(instruction_section_invalid_opcode, 44U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_INVALID_OPCODE) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(constants_section_valid, 53U, &program);
    if (expect(result.status == AIVM_PROGRAM_OK) != 0) {
        return 1;
    }
    if (expect(program.constants != NULL) != 0) {
        return 1;
    }
    if (expect(program.constant_count == 3U) != 0) {
        return 1;
    }
    if (expect(program.constants[0].type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(program.constants[0].int_value == 7) != 0) {
        return 1;
    }
    if (expect(program.constants[1].type == AIVM_VAL_BOOL) != 0) {
        return 1;
    }
    if (expect(program.constants[1].bool_value == 1) != 0) {
        return 1;
    }
    if (expect(program.constants[2].type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(program.constants[2], aivm_value_string("foo")) == 1) != 0) {
        return 1;
    }

    result = aivm_program_load_aibc1(constants_section_invalid_kind, 29U, &program);
    if (expect(result.status == AIVM_PROGRAM_ERR_INVALID_CONSTANT) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_string(aivm_program_status_code(result.status)), aivm_value_string("AIVMP011")) == 1) != 0) {
        return 1;
    }

    return 0;
}
