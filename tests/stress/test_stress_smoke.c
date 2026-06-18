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
    size_t i;
    static const uint8_t instruction_section_valid[44] = {
        'A', 'I', 'B', 'C',
        2, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,
        16, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    for (i = 0U; i < 10000U; i += 1U) {
        result = aivm_program_load_aibc1(instruction_section_valid, sizeof(instruction_section_valid), &program);
        if (expect(result.status == AIVM_PROGRAM_OK) != 0) {
            return 1;
        }
        aivm_program_clear(&program);
    }

    return 0;
}
