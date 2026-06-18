#include <stdint.h>
#include <string.h>

#include "aivm_program.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmProgram program;
    uint8_t buffer[96];
    size_t i;
    size_t size;

    for (size = 0U; size < sizeof(buffer); size += 1U) {
        for (i = 0U; i < sizeof(buffer); i += 1U) {
            buffer[i] = (uint8_t)((i * 37U) ^ (size * 11U));
        }
        if (size >= 4U && (size % 3U) == 0U) {
            buffer[0] = 'A';
            buffer[1] = 'I';
            buffer[2] = 'B';
            buffer[3] = 'C';
        }
        if (expect(aivm_program_load_aibc1(buffer, size, &program).status != AIVM_PROGRAM_OK) != 0) {
            return 1;
        }
    }

    memset(buffer, 0xff, sizeof(buffer));
    if (expect(aivm_program_load_aibc1(buffer, sizeof(buffer), &program).status != AIVM_PROGRAM_OK) != 0) {
        return 1;
    }

    return 0;
}
