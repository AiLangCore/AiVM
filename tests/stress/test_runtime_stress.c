#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aivm_program.h"
#include "aivm_vm.h"

enum {
    DEFAULT_LOADER_ITERATIONS = 25000U,
    DEFAULT_VM_ITERATIONS = 10000U,
    MAX_LOADER_ITERATIONS = 500000U,
    MAX_VM_ITERATIONS = 200000U,
    MAX_PROGRAM_BYTES = 4096U,
    STACK_CHURN_PAIRS = 2048U
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

static size_t build_loader_program(uint8_t* bytes, size_t capacity, uint32_t count, uint32_t seed)
{
    size_t cursor;
    uint32_t index;

    if (capacity < 28U || count < 1U || count > 256U) {
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
    write_u32_le(bytes, 20U, 4U + (count * 12U));
    write_u32_le(bytes, 24U, count);
    cursor = 28U;

    for (index = 0U; index < count; index += 1U) {
        uint32_t opcode = ((index + seed) % 5U) == 0U ? (uint32_t)AIVM_OP_PUSH_INT : (uint32_t)AIVM_OP_NOP;
        if (index + 1U == count) {
            opcode = (uint32_t)AIVM_OP_HALT;
        }
        write_u32_le(bytes, cursor, opcode);
        write_i64_le(bytes, cursor + 4U, (int64_t)(seed + index));
        cursor += 12U;
    }

    return cursor;
}

static int stress_loader(size_t iterations)
{
    uint8_t bytes[MAX_PROGRAM_BYTES];
    AivmProgram program;
    size_t iteration;

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        uint32_t instruction_count = (uint32_t)((iteration % 255U) + 1U);
        size_t byte_count;
        AivmProgramLoadResult result;

        memset(bytes, 0, sizeof(bytes));
        byte_count = build_loader_program(bytes, sizeof(bytes), instruction_count, (uint32_t)iteration);
        if (expect(byte_count > 0U) != 0) {
            return 1;
        }

        result = aivm_program_load_aibc1(bytes, byte_count, &program);
        if (result.status != AIVM_PROGRAM_OK) {
            (void)fprintf(
                stderr,
                "loader stress failed at iteration %zu status %s offset %zu\n",
                iteration,
                aivm_program_status_code(result.status),
                result.error_offset);
            return 1;
        }
        if (expect(program.instructions == program.instruction_storage) != 0 ||
            expect(program.instruction_count == instruction_count) != 0 ||
            expect(program.instruction_count <= AIVM_PROGRAM_MAX_INSTRUCTIONS) != 0) {
            return 1;
        }
        aivm_program_clear(&program);
    }

    return 0;
}

static void build_stack_churn_program(AivmInstruction* instructions, size_t* out_count)
{
    size_t cursor = 0U;
    size_t index;

    for (index = 0U; index < STACK_CHURN_PAIRS; index += 1U) {
        instructions[cursor].opcode = AIVM_OP_PUSH_INT;
        instructions[cursor].operand_int = (int64_t)index;
        cursor += 1U;
        instructions[cursor].opcode = AIVM_OP_POP;
        instructions[cursor].operand_int = 0;
        cursor += 1U;
    }
    for (index = 0U; index < 128U; index += 1U) {
        instructions[cursor].opcode = AIVM_OP_PUSH_INT;
        instructions[cursor].operand_int = (int64_t)index;
        cursor += 1U;
        instructions[cursor].opcode = AIVM_OP_PUSH_INT;
        instructions[cursor].operand_int = (int64_t)(index * 2U);
        cursor += 1U;
        instructions[cursor].opcode = AIVM_OP_ADD_INT;
        instructions[cursor].operand_int = 0;
        cursor += 1U;
        instructions[cursor].opcode = AIVM_OP_POP;
        instructions[cursor].operand_int = 0;
        cursor += 1U;
    }
    instructions[cursor].opcode = AIVM_OP_HALT;
    instructions[cursor].operand_int = 0;
    cursor += 1U;

    *out_count = cursor;
}

static int stress_vm_execution(size_t iterations)
{
    static AivmVm vm;
    static AivmInstruction instructions[(STACK_CHURN_PAIRS * 2U) + (128U * 4U) + 1U];
    AivmProgram program;
    size_t instruction_count = 0U;
    size_t iteration;
    uint64_t checksum = 0ULL;

    build_stack_churn_program(instructions, &instruction_count);
    aivm_program_init(&program, instructions, instruction_count);

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        aivm_init(&vm, &program);
        aivm_run(&vm);
        if (vm.status != AIVM_VM_STATUS_HALTED || vm.error != AIVM_VM_ERR_NONE) {
            (void)fprintf(
                stderr,
                "vm stress failed at iteration %zu status %d error %d detail %s\n",
                iteration,
                (int)vm.status,
                (int)vm.error,
                aivm_vm_error_detail(&vm));
            aivm_dispose(&vm);
            return 1;
        }
        if (expect(vm.instruction_pointer == instruction_count) != 0 ||
            expect(vm.stack_count == 0U) != 0 ||
            expect(vm.call_frame_count == 0U) != 0) {
            aivm_dispose(&vm);
            return 1;
        }
        checksum += ((uint64_t)vm.instruction_pointer << (iteration % 17U));
        if ((iteration % 257U) == 0U && expect(aivm_collect_safe_point(&vm) != 0) != 0) {
            aivm_dispose(&vm);
            return 1;
        }
    }

    aivm_dispose(&vm);
    if (expect(checksum != 0ULL || iterations == 0U) != 0) {
        return 1;
    }
    return 0;
}

int main(void)
{
    size_t loader_iterations = read_budget(
        "AIVM_STRESS_ITERATIONS",
        DEFAULT_LOADER_ITERATIONS,
        MAX_LOADER_ITERATIONS);
    size_t vm_iterations = read_budget(
        "AIVM_STRESS_VM_ITERATIONS",
        DEFAULT_VM_ITERATIONS,
        MAX_VM_ITERATIONS);

    if (stress_loader(loader_iterations) != 0) {
        return 1;
    }
    if (stress_vm_execution(vm_iterations) != 0) {
        return 1;
    }
    return 0;
}
