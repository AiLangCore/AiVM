#include <stdint.h>

#include "aivm_program.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmProgram program;
    static const uint8_t huge_section_count[16] = {
        'A', 'I', 'B', 'C',
        2, 0, 0, 0,
        0, 0, 0, 0,
        0xff, 0xff, 0xff, 0x7f
    };
    static const uint8_t section_size_overflow[24] = {
        'A', 'I', 'B', 'C',
        2, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,
        0xff, 0xff, 0xff, 0xff
    };
    static const uint8_t invalid_opcode[44] = {
        'A', 'I', 'B', 'C',
        2, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,
        16, 0, 0, 0,
        1, 0, 0, 0,
        0xff, 0xff, 0xff, 0x7f,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    if (expect(aivm_program_load_aibc1(huge_section_count, sizeof(huge_section_count), &program).status == AIVM_PROGRAM_ERR_SECTION_LIMIT) != 0) {
        return 1;
    }
    if (expect(aivm_program_load_aibc1(section_size_overflow, sizeof(section_size_overflow), &program).status == AIVM_PROGRAM_ERR_SECTION_OOB) != 0) {
        return 1;
    }
    if (expect(aivm_program_load_aibc1(invalid_opcode, sizeof(invalid_opcode), &program).status == AIVM_PROGRAM_ERR_INVALID_OPCODE) != 0) {
        return 1;
    }

    return 0;
}
