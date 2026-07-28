#include <stdio.h>

#include "aivm_debugger.h"
#include "aivm_program.h"
#include "aivm_vm.h"

static int expect_line(int condition, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(stderr, "expect failed at line %d\n", line);
    return 1;
}

#define expect(condition) expect_line((condition), __LINE__)

typedef int (*AivmTestFn)(void);

static int run_test(const char* name, AivmTestFn fn)
{
    (void)fprintf(stderr, "aivm_test_debugger_protocol: %s\n", name);
    return fn();
}

static void init_program(AivmProgram* program, const AivmInstruction* instructions, size_t instruction_count)
{
    aivm_program_init(program, instructions, instruction_count);
}

static int breakpoint_continue_and_step(void)
{
    AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 10 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 32 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    AivmProgram program;
    AivmVm vm;
    AivmDebugger debugger;
    AivmDebuggerSnapshot snapshot;
    AivmValue value;

    init_program(&program, instructions, sizeof(instructions) / sizeof(instructions[0]));
    aivm_init(&vm, &program);
    aivm_debugger_init(&debugger, &vm);

    if (expect(aivm_debugger_break_pc(&debugger, 2U) == AIVM_DEBUGGER_OK) != 0) {
        return 1;
    }
    if (expect(aivm_debugger_continue(&debugger, 16U, &snapshot) == AIVM_DEBUGGER_OK) != 0) {
        return 1;
    }
    if (expect(snapshot.pc == 2U) != 0 ||
        expect(snapshot.opcode == (int)AIVM_OP_ADD_INT) != 0 ||
        expect(snapshot.stack_count == 2U) != 0 ||
        expect(snapshot.debugger_state == AIVM_DEBUGGER_STATE_PAUSED) != 0) {
        return 1;
    }
    if (expect(aivm_debugger_inspect(&debugger, AIVM_DEBUGGER_INSPECT_STACK, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(snapshot.stack_count == 2U) != 0) {
        return 1;
    }
    if (expect(aivm_debugger_step(&debugger, &snapshot) == AIVM_DEBUGGER_OK) != 0) {
        return 1;
    }
    if (expect(snapshot.pc == 3U) != 0 ||
        expect(snapshot.opcode == (int)AIVM_OP_HALT) != 0 ||
        expect(snapshot.stack_count == 1U) != 0 ||
        expect(vm.stack_count == 1U) != 0 ||
        expect(aivm_stack_pop(&vm, &value) == 1) != 0 ||
        expect(value.type == AIVM_VAL_INT) != 0 ||
        expect(value.int_value == 42) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int breakpoints_are_deterministic_and_bounded(void)
{
    AivmInstruction instructions[160];
    AivmProgram program;
    AivmVm vm;
    AivmDebugger debugger;
    size_t index;

    for (index = 0U; index < (sizeof(instructions) / sizeof(instructions[0])); index += 1U) {
        instructions[index].opcode = AIVM_OP_NOP;
        instructions[index].operand_int = 0;
    }
    instructions[(sizeof(instructions) / sizeof(instructions[0])) - 1U].opcode = AIVM_OP_HALT;
    init_program(&program, instructions, sizeof(instructions) / sizeof(instructions[0]));
    aivm_init(&vm, &program);
    aivm_debugger_init(&debugger, &vm);

    if (expect(aivm_debugger_break_pc(&debugger, 3U) == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_break_pc(&debugger, 3U) == AIVM_DEBUGGER_OK) != 0 ||
        expect(debugger.breakpoint_count == 1U) != 0 ||
        expect(aivm_debugger_break_pc(&debugger, sizeof(instructions) / sizeof(instructions[0])) ==
               AIVM_DEBUGGER_ERR_INVALID) != 0) {
        return 1;
    }

    for (index = 0U; index < AIVM_DEBUGGER_MAX_BREAKPOINTS; index += 1U) {
        if (expect(aivm_debugger_break_pc(&debugger, index) == AIVM_DEBUGGER_OK) != 0) {
            return 1;
        }
    }
    if (expect(debugger.breakpoint_count == AIVM_DEBUGGER_MAX_BREAKPOINTS) != 0 ||
        expect(aivm_debugger_break_pc(&debugger, AIVM_DEBUGGER_MAX_BREAKPOINTS) ==
               AIVM_DEBUGGER_ERR_BREAKPOINT_LIMIT) != 0) {
        return 1;
    }
    if (expect(aivm_debugger_clear_breakpoints(&debugger) == AIVM_DEBUGGER_OK) != 0 ||
        expect(debugger.breakpoint_count == 0U) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int continue_requires_explicit_step_budget(void)
{
    AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    AivmProgram program;
    AivmVm vm;
    AivmDebugger debugger;
    AivmDebuggerSnapshot snapshot;

    init_program(&program, instructions, sizeof(instructions) / sizeof(instructions[0]));
    aivm_init(&vm, &program);
    aivm_debugger_init(&debugger, &vm);

    if (expect(aivm_debugger_continue(&debugger, 0U, &snapshot) == AIVM_DEBUGGER_ERR_INVALID) != 0 ||
        expect(aivm_debugger_continue(&debugger, 2U, &snapshot) == AIVM_DEBUGGER_ERR_STEP_LIMIT) != 0 ||
        expect(snapshot.pc == 2U) != 0 ||
        expect(snapshot.debugger_state == AIVM_DEBUGGER_STATE_PAUSED) != 0 ||
        expect(aivm_debugger_continue(&debugger, 8U, &snapshot) == AIVM_DEBUGGER_ERR_HALTED) != 0 ||
        expect(snapshot.debugger_state == AIVM_DEBUGGER_STATE_HALTED) != 0 ||
        expect(snapshot.vm_status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int inspect_rejects_unknown_kind(void)
{
    AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    AivmProgram program;
    AivmVm vm;
    AivmDebugger debugger;
    AivmDebuggerSnapshot snapshot;

    init_program(&program, instructions, sizeof(instructions) / sizeof(instructions[0]));
    aivm_init(&vm, &program);
    aivm_debugger_init(&debugger, &vm);

    if (expect(aivm_debugger_inspect(&debugger, AIVM_DEBUGGER_INSPECT_LOCALS, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(snapshot.locals_count == 0U) != 0 ||
        expect(aivm_debugger_inspect(&debugger, (AivmDebuggerInspectKind)99, &snapshot) ==
               AIVM_DEBUGGER_ERR_INVALID) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int source_mapped_breakpoints_and_step_controls(void)
{
    AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CALL, .operand_int = 3 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 37 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    AivmVm vm;
    AivmDebugger debugger;
    AivmDebuggerSnapshot snapshot;

    init_program(&program, instructions, sizeof(instructions) / sizeof(instructions[0]));
    aivm_init(&vm, &program);
    aivm_debugger_init(&debugger, &vm);

    if (expect(aivm_debugger_register_function(&debugger, "worker", 3U) == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_register_node(&debugger, "call-node", 0U) == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_break_function(&debugger, "worker") == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_break_node(&debugger, "call-node") == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_break_function(&debugger, "missing") == AIVM_DEBUGGER_ERR_INVALID) != 0) {
        return 1;
    }
    if (expect(aivm_debugger_continue(&debugger, 8U, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(snapshot.pc == 0U) != 0 ||
        expect(snapshot.debugger_state == AIVM_DEBUGGER_STATE_PAUSED) != 0) {
        return 1;
    }
    if (expect(aivm_debugger_clear_breakpoints(&debugger) == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_break_function(&debugger, "worker") == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_continue(&debugger, 8U, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(snapshot.pc == 3U) != 0 ||
        expect(snapshot.call_frame_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_debugger_step_out(&debugger, 8U, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(snapshot.pc == 1U) != 0 ||
        expect(snapshot.call_frame_count == 0U) != 0 ||
        expect(snapshot.stack_count == 1U) != 0) {
        return 1;
    }
    aivm_dispose(&vm);

    aivm_init(&vm, &program);
    aivm_debugger_init(&debugger, &vm);
    if (expect(aivm_debugger_step_over(&debugger, 8U, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(snapshot.pc == 1U) != 0 ||
        expect(snapshot.call_frame_count == 0U) != 0 ||
        expect(snapshot.stack_count == 1U) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int heap_and_host_operation_inspection_reports_counts(void)
{
    AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    AivmValue constants[] = {
        aivm_value_string("debug-node")
    };
    AivmProgram program;
    AivmVm vm;
    AivmDebugger debugger;
    AivmDebuggerSnapshot snapshot;

    aivm_program_init(&program, instructions, sizeof(instructions) / sizeof(instructions[0]));
    program.constants = constants;
    program.constant_count = sizeof(constants) / sizeof(constants[0]);
    aivm_init(&vm, &program);
    aivm_debugger_init(&debugger, &vm);
    if (expect(aivm_debugger_step(&debugger, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_step(&debugger, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(aivm_debugger_inspect(&debugger, AIVM_DEBUGGER_INSPECT_HEAP, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(snapshot.node_count > 0U) != 0 ||
        expect(snapshot.node_attr_count == 0U) != 0 ||
        expect(snapshot.node_child_count == 0U) != 0 ||
        expect(snapshot.blob_count == 0U) != 0) {
        return 1;
    }
    aivm_debugger_set_active_host_operation_count(&debugger, 3U);
    if (expect(aivm_debugger_inspect(&debugger, AIVM_DEBUGGER_INSPECT_HOST_OPS, &snapshot) == AIVM_DEBUGGER_OK) != 0 ||
        expect(snapshot.active_host_operation_count == 3U) != 0 ||
        expect(snapshot.syscall_binding_count == 0U) != 0 ||
        expect(snapshot.process_argv_count == 0U) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

int main(void)
{
    if (run_test("breakpoint_continue_and_step", breakpoint_continue_and_step) != 0) {
        return 1;
    }
    if (run_test("breakpoints_are_deterministic_and_bounded", breakpoints_are_deterministic_and_bounded) != 0) {
        return 1;
    }
    if (run_test("continue_requires_explicit_step_budget", continue_requires_explicit_step_budget) != 0) {
        return 1;
    }
    if (run_test("inspect_rejects_unknown_kind", inspect_rejects_unknown_kind) != 0) {
        return 1;
    }
    if (run_test("source_mapped_breakpoints_and_step_controls", source_mapped_breakpoints_and_step_controls) != 0) {
        return 1;
    }
    if (run_test("heap_and_host_operation_inspection_reports_counts", heap_and_host_operation_inspection_reports_counts) != 0) {
        return 1;
    }
    return 0;
}
