#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "aivm_program.h"
#include "aivm_vm.h"
#include "sys/aivm_syscall.h"

#ifndef AIVM_BUILD_COMMIT
#define AIVM_BUILD_COMMIT "unknown"
#endif

#ifndef AIVM_BUILD_CHANNEL
#define AIVM_BUILD_CHANNEL "local"
#endif

enum {
    PERF_MAX_RESULTS = 48,
    PERF_PROGRAM_BYTES = 4096,
    PERF_EVAL_INSTRUCTIONS = 4097
};

typedef struct {
    const char* name;
    const char* category;
    const char* input_size;
    size_t iterations;
    double elapsed_seconds;
    double operations_per_second;
    double bytes_per_second;
    size_t peak_memory_bytes;
    size_t allocation_count;
} PerfResult;

static volatile uint64_t g_perf_sink = 0ULL;

static int expect_line(int condition, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(stderr, "expect failed at line %d\n", line);
    return 1;
}

#define expect(condition) expect_line((condition), __LINE__)

static double now_seconds(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

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

static size_t build_decode_program(uint8_t* bytes, size_t capacity, uint32_t instruction_count)
{
    size_t cursor;
    uint32_t index;

    if (capacity < 28U || instruction_count == 0U || instruction_count > 256U) {
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
        uint32_t opcode = index + 1U == instruction_count ? (uint32_t)AIVM_OP_HALT : (uint32_t)AIVM_OP_NOP;
        write_u32_le(bytes, cursor, opcode);
        write_i64_le(bytes, cursor + 4U, (int64_t)index);
        cursor += 12U;
    }

    return cursor;
}

static int add_result(
    PerfResult* results,
    size_t* result_count,
    const char* name,
    const char* category,
    const char* input_size,
    size_t iterations,
    double elapsed_seconds,
    size_t bytes_processed,
    size_t allocation_count)
{
    PerfResult* result;
    if (results == NULL || result_count == NULL || *result_count >= PERF_MAX_RESULTS) {
        return 0;
    }
    result = &results[*result_count];
    result->name = name;
    result->category = category;
    result->input_size = input_size;
    result->iterations = iterations;
    result->elapsed_seconds = elapsed_seconds;
    result->operations_per_second = elapsed_seconds > 0.0 ? (double)iterations / elapsed_seconds : 0.0;
    result->bytes_per_second = elapsed_seconds > 0.0 ? (double)bytes_processed / elapsed_seconds : 0.0;
    result->peak_memory_bytes = 0U;
    result->allocation_count = allocation_count;
    *result_count += 1U;
    return 1;
}

static int bench_decode(PerfResult* results, size_t* result_count, size_t iterations)
{
    uint8_t bytes[PERF_PROGRAM_BYTES];
    AivmProgram program;
    size_t byte_count = build_decode_program(bytes, sizeof(bytes), 256U);
    size_t index;
    double start;
    double elapsed;

    if (expect(byte_count > 0U) != 0) {
        return 1;
    }

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        AivmProgramLoadResult load_result = aivm_program_load_aibc1(bytes, byte_count, &program);
        if (load_result.status != AIVM_PROGRAM_OK) {
            (void)fprintf(stderr, "decode benchmark load failed: %s\n", aivm_program_status_code(load_result.status));
            return 1;
        }
        g_perf_sink += program.instruction_count;
        aivm_program_clear(&program);
    }
    elapsed = now_seconds() - start;

    return add_result(
        results,
        result_count,
        "aibc1_decode_256_instruction",
        "decode",
        "medium",
        iterations,
        elapsed,
        iterations * byte_count,
        iterations) ? 0 : 1;
}

static int bench_invalid_decode_rejection(PerfResult* results, size_t* result_count, size_t iterations)
{
    uint8_t bytes[64];
    AivmProgram program;
    size_t index;
    double start;
    double elapsed;

    memset(bytes, 0, sizeof(bytes));
    bytes[0] = (uint8_t)'A';
    bytes[1] = (uint8_t)'I';
    bytes[2] = (uint8_t)'B';
    bytes[3] = (uint8_t)'C';
    write_u32_le(bytes, 4U, 2U);
    write_u32_le(bytes, 12U, AIVM_PROGRAM_MAX_SECTIONS + 1U);

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        AivmProgramLoadResult load_result = aivm_program_load_aibc1(bytes, sizeof(bytes), &program);
        if (load_result.status != AIVM_PROGRAM_ERR_SECTION_LIMIT) {
            (void)fprintf(
                stderr,
                "invalid decode benchmark failed: %s\n",
                aivm_program_status_code(load_result.status));
            return 1;
        }
        g_perf_sink += load_result.error_offset;
    }
    elapsed = now_seconds() - start;

    return add_result(
        results,
        result_count,
        "aibc1_invalid_section_limit_rejection",
        "decode",
        "tiny",
        iterations,
        elapsed,
        iterations * sizeof(bytes),
        iterations) ? 0 : 1;
}

static void build_eval_program(AivmInstruction* instructions, size_t* out_count)
{
    size_t cursor = 0U;
    size_t index;

    for (index = 0U; index < 1024U; index += 1U) {
        instructions[cursor].opcode = AIVM_OP_PUSH_INT;
        instructions[cursor].operand_int = (int64_t)index;
        cursor += 1U;
        instructions[cursor].opcode = AIVM_OP_POP;
        instructions[cursor].operand_int = 0;
        cursor += 1U;
    }
    for (index = 0U; index < 512U; index += 1U) {
        instructions[cursor].opcode = AIVM_OP_PUSH_INT;
        instructions[cursor].operand_int = (int64_t)index;
        cursor += 1U;
        instructions[cursor].opcode = AIVM_OP_PUSH_INT;
        instructions[cursor].operand_int = (int64_t)(index + 1U);
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

static int bench_eval(PerfResult* results, size_t* result_count, size_t iterations)
{
    static AivmVm vm;
    static AivmInstruction instructions[PERF_EVAL_INSTRUCTIONS];
    AivmProgram program;
    size_t instruction_count = 0U;
    size_t index;
    double start;
    double elapsed;

    build_eval_program(instructions, &instruction_count);
    aivm_program_init(&program, instructions, instruction_count);

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        aivm_init(&vm, &program);
        aivm_run(&vm);
        if (vm.status != AIVM_VM_STATUS_HALTED || vm.error != AIVM_VM_ERR_NONE) {
            (void)fprintf(stderr, "eval benchmark failed: status=%d error=%d\n", (int)vm.status, (int)vm.error);
            aivm_dispose(&vm);
            return 1;
        }
        g_perf_sink += vm.instruction_pointer;
    }
    elapsed = now_seconds() - start;
    aivm_dispose(&vm);

    return add_result(
        results,
        result_count,
        "vm_eval_stack_churn",
        "eval",
        "medium",
        iterations * instruction_count,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int run_program_repeated(
    const AivmProgram* program,
    size_t iterations,
    size_t expected_stack_count,
    AivmValueType expected_top_type,
    const char* failure_name)
{
    static AivmVm vm;
    size_t index;

    for (index = 0U; index < iterations; index += 1U) {
        aivm_init(&vm, program);
        aivm_run(&vm);
        if (vm.status != AIVM_VM_STATUS_HALTED ||
            vm.error != AIVM_VM_ERR_NONE ||
            vm.stack_count != expected_stack_count) {
            (void)fprintf(
                stderr,
                "%s failed: status=%d error=%d stack=%zu\n",
                failure_name,
                (int)vm.status,
                (int)vm.error,
                vm.stack_count);
            aivm_dispose(&vm);
            return 1;
        }
        if (expected_stack_count > 0U && vm.stack[expected_stack_count - 1U].type != expected_top_type) {
            (void)fprintf(stderr, "%s failed: unexpected top value type\n", failure_name);
            aivm_dispose(&vm);
            return 1;
        }
        g_perf_sink += vm.instruction_pointer + vm.stack_count;
        if (expected_stack_count > 0U) {
            g_perf_sink += (uint64_t)vm.stack[expected_stack_count - 1U].type;
        }
    }

    aivm_dispose(&vm);
    return 0;
}

static int bench_numeric_ops(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 97 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 31 },
        { .opcode = AIVM_OP_SUB_NUM, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 3 },
        { .opcode = AIVM_OP_MUL_NUM, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 2 },
        { .opcode = AIVM_OP_DIV_NUM, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_MOD_NUM, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 3 },
        { .opcode = AIVM_OP_LT_NUM, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_BOOL, "numeric benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "vm_numeric_ops_sub_mul_div_mod_lt",
        "eval",
        "small",
        iterations * program.instruction_count,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int bench_branch_dispatch(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_BOOL, .operand_int = 0 },
        { .opcode = AIVM_OP_JUMP_IF_FALSE, .operand_int = 4 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 100 },
        { .opcode = AIVM_OP_JUMP, .operand_int = 5 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 200 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_INT, "branch benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "vm_branch_jump_if_false",
        "eval",
        "tiny",
        iterations * program.instruction_count,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int bench_call_return(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 7 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_INT, "call benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "vm_call_return",
        "eval",
        "tiny",
        iterations * program.instruction_count,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int bench_locals(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 11 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_INT, "locals benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "vm_store_load_local",
        "eval",
        "tiny",
        iterations * program.instruction_count,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int bench_constants_and_strings(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 },
        { .opcode = AIVM_OP_STR_UTF8_BYTE_COUNT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "hello " },
        { .type = AIVM_VAL_STRING, .string_value = "world" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .constants = constants,
        .constant_count = sizeof(constants) / sizeof(constants[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_INT, "string benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "vm_const_string_concat_utf8_count",
        "eval",
        "small",
        iterations * program.instruction_count,
        elapsed,
        iterations * 11U,
        iterations) ? 0 : 1;
}

static int bench_loop(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 64 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_EQ, .operand_int = 0 },
        { .opcode = AIVM_OP_JUMP_IF_FALSE, .operand_int = 10 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_SUB_NUM, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_JUMP, .operand_int = 4 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_INT, "loop benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "vm_loop_64_countdown",
        "eval",
        "small",
        iterations * 64U,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int bench_recursive_tail_call(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 3 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 10 },
        { .opcode = AIVM_OP_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_EQ, .operand_int = 0 },
        { .opcode = AIVM_OP_JUMP_IF_FALSE, .operand_int = 12 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = -1 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = -1 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_INT, "recursive tail-call benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "vm_recursive_tail_call",
        "eval",
        "small",
        iterations * program.instruction_count,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int bench_bytes(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const uint8_t payload[] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U
    };
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_BYTES_LENGTH, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_BYTES, .bytes_value = { payload, sizeof(payload) } }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .constants = constants,
        .constant_count = sizeof(constants) / sizeof(constants[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_INT, "bytes benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "vm_const_bytes_length",
        "eval",
        "small",
        iterations * program.instruction_count,
        elapsed,
        iterations * sizeof(payload),
        iterations) ? 0 : 1;
}

static int bench_memory(PerfResult* results, size_t* result_count, size_t iterations)
{
    static AivmVm vm;
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
    double start;
    double elapsed;

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        aivm_init(&vm, &program);
        if (expect(aivm_stack_push(&vm, aivm_value_int((int64_t)index)) != 0) != 0) {
            aivm_dispose(&vm);
            return 1;
        }
        if (expect(aivm_collect_safe_point(&vm) != 0) != 0) {
            aivm_dispose(&vm);
            return 1;
        }
        g_perf_sink += vm.stack_count;
    }
    elapsed = now_seconds() - start;
    aivm_dispose(&vm);

    return add_result(
        results,
        result_count,
        "vm_reset_stack_safepoint",
        "memory",
        "small",
        iterations,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int perf_console_write(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int perf_worker_poll(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_int(0);
    return AIVM_SYSCALL_OK;
}

static int perf_large_file_read(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static const uint8_t payload[4096] = { 1U };
    (void)target;
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes(payload, sizeof(payload));
    return AIVM_SYSCALL_OK;
}

static int bench_syscall(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmSyscallBinding bindings[] = {
        { .target = "sys.console.write", .handler = perf_console_write }
    };
    AivmValue arg = aivm_value_string("perf");
    AivmValue result = aivm_value_void();
    size_t index;
    double start;
    double elapsed;

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        AivmSyscallStatus status = aivm_syscall_dispatch_checked(
            bindings,
            sizeof(bindings) / sizeof(bindings[0]),
            "sys.console.write",
            &arg,
            1U,
            &result);
        if (status != AIVM_SYSCALL_OK) {
            (void)fprintf(stderr, "syscall benchmark failed: %s\n", aivm_syscall_status_code(status));
            return 1;
        }
        g_perf_sink += (uint64_t)result.type;
    }
    elapsed = now_seconds() - start;

    return add_result(
        results,
        result_count,
        "syscall_checked_console_write",
        "syscall",
        "tiny",
        iterations,
        elapsed,
        iterations * 4U,
        0U) ? 0 : 1;
}

static int bench_large_payload_syscall(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmSyscallBinding bindings[] = {
        { .target = "sys.fs.file.read", .handler = perf_large_file_read }
    };
    AivmValue arg = aivm_value_string("large-payload.bin");
    AivmValue result = aivm_value_void();
    size_t index;
    double start;
    double elapsed;

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        AivmSyscallStatus status = aivm_syscall_dispatch_checked(
            bindings,
            sizeof(bindings) / sizeof(bindings[0]),
            "sys.fs.file.read",
            &arg,
            1U,
            &result);
        if (status != AIVM_SYSCALL_OK || result.type != AIVM_VAL_BYTES) {
            (void)fprintf(stderr, "large payload syscall benchmark failed: %s\n", aivm_syscall_status_code(status));
            return 1;
        }
        g_perf_sink += result.bytes_value.length;
    }
    elapsed = now_seconds() - start;

    return add_result(
        results,
        result_count,
        "syscall_checked_large_bytes_payload",
        "syscall",
        "large",
        iterations,
        elapsed,
        iterations * 4096U,
        0U) ? 0 : 1;
}

static int bench_failed_syscall(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmSyscallBinding bindings[] = {
        { .target = "sys.console.write", .handler = perf_console_write }
    };
    AivmValue result = aivm_value_void();
    size_t index;
    double start;
    double elapsed;

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        AivmSyscallStatus status = aivm_syscall_dispatch_checked(
            bindings,
            sizeof(bindings) / sizeof(bindings[0]),
            "sys.console.write",
            NULL,
            0U,
            &result);
        if (status != AIVM_SYSCALL_ERR_CONTRACT) {
            (void)fprintf(stderr, "failed syscall benchmark failed: %s\n", aivm_syscall_status_code(status));
            return 1;
        }
        g_perf_sink += (uint64_t)(0U - (uint32_t)status);
    }
    elapsed = now_seconds() - start;

    return add_result(
        results,
        result_count,
        "syscall_checked_contract_failure",
        "syscall",
        "tiny",
        iterations,
        elapsed,
        0U,
        0U) ? 0 : 1;
}

static int bench_async_worker(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_INT, "async worker benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "worker_async_call_await",
        "worker",
        "small",
        iterations,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int bench_par_join_queue(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PAR_BEGIN, .operand_int = 2 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 41 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 },
        { .opcode = AIVM_OP_PAR_JOIN, .operand_int = 2 },
        { .opcode = AIVM_OP_PAR_CANCEL, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    double start = now_seconds();
    double elapsed;

    if (run_program_repeated(&program, iterations, 1U, AIVM_VAL_NODE, "par join benchmark") != 0) {
        return 1;
    }
    elapsed = now_seconds() - start;
    return add_result(
        results,
        result_count,
        "worker_par_join_queue",
        "worker",
        "small",
        iterations * program.instruction_count,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static int bench_worker(PerfResult* results, size_t* result_count, size_t iterations)
{
    static const AivmSyscallBinding bindings[] = {
        { .target = "sys.worker.poll", .handler = perf_worker_poll }
    };
    AivmValue arg = aivm_value_int(1);
    AivmValue result = aivm_value_void();
    size_t index;
    double start;
    double elapsed;

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        AivmSyscallStatus status = aivm_syscall_dispatch_checked(
            bindings,
            sizeof(bindings) / sizeof(bindings[0]),
            "sys.worker.poll",
            &arg,
            1U,
            &result);
        if (status != AIVM_SYSCALL_OK) {
            (void)fprintf(stderr, "worker benchmark failed: %s\n", aivm_syscall_status_code(status));
            return 1;
        }
        g_perf_sink += (uint64_t)result.int_value;
    }
    elapsed = now_seconds() - start;

    return add_result(
        results,
        result_count,
        "worker_poll_dispatch",
        "worker",
        "tiny",
        iterations,
        elapsed,
        0U,
        0U) ? 0 : 1;
}

static int bench_golden_replay(PerfResult* results, size_t* result_count, size_t iterations)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 2 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    size_t index;
    double start;
    double elapsed;

    start = now_seconds();
    for (index = 0U; index < iterations; index += 1U) {
        aivm_init(&vm, &program);
        aivm_run(&vm);
        if (vm.status != AIVM_VM_STATUS_HALTED ||
            vm.stack_count != 1U ||
            vm.stack[0].type != AIVM_VAL_INT ||
            vm.stack[0].int_value != 3) {
            (void)fprintf(stderr, "golden replay benchmark failed\n");
            aivm_dispose(&vm);
            return 1;
        }
        g_perf_sink += (uint64_t)vm.stack[0].int_value;
    }
    elapsed = now_seconds() - start;
    aivm_dispose(&vm);

    return add_result(
        results,
        result_count,
        "golden_add_int_replay",
        "golden",
        "tiny",
        iterations,
        elapsed,
        0U,
        iterations) ? 0 : 1;
}

static const char* suite_name(void)
{
    const char* raw = getenv("AIVM_PERF_SUITE");
    if (raw == NULL || raw[0] == '\0') {
        return "smoke";
    }
    return raw;
}

static size_t suite_multiplier(const char* suite)
{
    if (suite != NULL && strcmp(suite, "full") == 0) {
        return 10U;
    }
    if (suite != NULL && strcmp(suite, "release") == 0) {
        return 25U;
    }
    return 1U;
}

static void ensure_perf_artifact_dir(void)
{
#if defined(_WIN32)
    (void)_mkdir("artifacts");
    (void)_mkdir("artifacts\\perf");
#else
    (void)mkdir("artifacts", 0777);
    (void)mkdir("artifacts/perf", 0777);
#endif
}

static const char* output_path(void)
{
    const char* raw = getenv("AIVM_PERF_OUTPUT");
    if (raw == NULL || raw[0] == '\0') {
        return "artifacts/perf/results.json";
    }
    return raw;
}

static int write_json_results(const PerfResult* results, size_t result_count, const char* suite)
{
    const char* path = output_path();
    FILE* file;
    size_t index;

    ensure_perf_artifact_dir();
    file = fopen(path, "w");
    if (file == NULL) {
        (void)fprintf(stderr, "failed to open perf output: %s\n", path);
        return 1;
    }

    (void)fprintf(file, "{\n");
    (void)fprintf(file, "  \"schema\": \"aivm-perf-results-v1\",\n");
    (void)fprintf(file, "  \"suite\": \"%s\",\n", suite);
    (void)fprintf(file, "  \"git_commit\": \"%s\",\n", AIVM_BUILD_COMMIT);
    (void)fprintf(file, "  \"channel\": \"%s\",\n", AIVM_BUILD_CHANNEL);
    (void)fprintf(file, "  \"compiler\": \"%s\",\n",
#if defined(__clang__)
        "clang"
#elif defined(_MSC_VER)
        "msvc"
#elif defined(__GNUC__)
        "gcc"
#else
        "unknown"
#endif
    );
    (void)fprintf(file, "  \"platform\": \"%s\",\n",
#if defined(_WIN32)
        "windows"
#elif defined(__APPLE__)
        "macos"
#elif defined(__linux__)
        "linux"
#else
        "unknown"
#endif
    );
    (void)fprintf(file, "  \"results\": [\n");
    for (index = 0U; index < result_count; index += 1U) {
        const PerfResult* result = &results[index];
        (void)fprintf(file, "    {\n");
        (void)fprintf(file, "      \"name\": \"%s\",\n", result->name);
        (void)fprintf(file, "      \"category\": \"%s\",\n", result->category);
        (void)fprintf(file, "      \"input_size\": \"%s\",\n", result->input_size);
        (void)fprintf(file, "      \"iteration_count\": %zu,\n", result->iterations);
        (void)fprintf(file, "      \"elapsed_seconds\": %.9f,\n", result->elapsed_seconds);
        (void)fprintf(file, "      \"operations_per_second\": %.3f,\n", result->operations_per_second);
        (void)fprintf(file, "      \"bytes_per_second\": %.3f,\n", result->bytes_per_second);
        (void)fprintf(file, "      \"peak_memory_bytes\": %zu,\n", result->peak_memory_bytes);
        (void)fprintf(file, "      \"allocation_count\": %zu\n", result->allocation_count);
        (void)fprintf(file, "    }%s\n", index + 1U == result_count ? "" : ",");
    }
    (void)fprintf(file, "  ]\n");
    (void)fprintf(file, "}\n");

    if (fclose(file) != 0) {
        (void)fprintf(stderr, "failed to close perf output: %s\n", path);
        return 1;
    }
    return 0;
}

int main(void)
{
    PerfResult results[PERF_MAX_RESULTS];
    size_t result_count = 0U;
    const char* suite = suite_name();
    size_t multiplier = suite_multiplier(suite);
    size_t decode_iterations = read_budget("AIVM_PERF_DECODE_ITERATIONS", 1000U * multiplier, 1000000U);
    size_t eval_iterations = read_budget("AIVM_PERF_EVAL_ITERATIONS", 100U * multiplier, 100000U);
    size_t memory_iterations = read_budget("AIVM_PERF_MEMORY_ITERATIONS", 1000U * multiplier, 1000000U);
    size_t syscall_iterations = read_budget("AIVM_PERF_SYSCALL_ITERATIONS", 10000U * multiplier, 10000000U);
    size_t worker_iterations = read_budget("AIVM_PERF_WORKER_ITERATIONS", 10000U * multiplier, 10000000U);
    size_t async_iterations = read_budget("AIVM_PERF_ASYNC_ITERATIONS", 20U * multiplier, 10000U);
    size_t golden_iterations = read_budget("AIVM_PERF_GOLDEN_ITERATIONS", 1000U * multiplier, 1000000U);

    memset(results, 0, sizeof(results));

    if (bench_decode(results, &result_count, decode_iterations) != 0 ||
        bench_invalid_decode_rejection(results, &result_count, decode_iterations) != 0 ||
        bench_eval(results, &result_count, eval_iterations) != 0 ||
        bench_numeric_ops(results, &result_count, eval_iterations) != 0 ||
        bench_branch_dispatch(results, &result_count, eval_iterations) != 0 ||
        bench_call_return(results, &result_count, eval_iterations) != 0 ||
        bench_locals(results, &result_count, eval_iterations) != 0 ||
        bench_constants_and_strings(results, &result_count, eval_iterations) != 0 ||
        bench_loop(results, &result_count, eval_iterations) != 0 ||
        bench_recursive_tail_call(results, &result_count, eval_iterations) != 0 ||
        bench_bytes(results, &result_count, eval_iterations) != 0 ||
        bench_memory(results, &result_count, memory_iterations) != 0 ||
        bench_syscall(results, &result_count, syscall_iterations) != 0 ||
        bench_large_payload_syscall(results, &result_count, syscall_iterations) != 0 ||
        bench_failed_syscall(results, &result_count, syscall_iterations) != 0 ||
        bench_worker(results, &result_count, worker_iterations) != 0 ||
        bench_async_worker(results, &result_count, async_iterations) != 0 ||
        bench_par_join_queue(results, &result_count, worker_iterations) != 0 ||
        bench_golden_replay(results, &result_count, golden_iterations) != 0) {
        return 1;
    }

    if (g_perf_sink == 0ULL) {
        (void)fprintf(stderr, "perf sink remained zero\n");
        return 1;
    }

    return write_json_results(results, result_count, suite);
}
