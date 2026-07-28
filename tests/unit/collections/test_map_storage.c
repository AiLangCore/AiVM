#include "../../../src/aivm_vm_internal.h"

#include <stdio.h>
#include <string.h>

static int require(int condition, const char* message)
{
    if (condition) return 1;
    fprintf(stderr, "map storage test failed: %s\n", message);
    return 0;
}

int main(void)
{
    static AivmVm vm;
    int64_t handle;
    int64_t value;
    int found;
    size_t count;
    size_t index;
    char key[64];

    memset(&vm, 0, sizeof(vm));
    aivm_init(&vm, NULL);
    if (!require(vm.status != AIVM_VM_STATUS_ERROR, "VM initialization")) return 1;
    if (!require(aivm_vm_map_builder_new(&vm, &handle), "builder creation")) return 1;

    for (index = 0U; index < 100000U; index += 1U) {
        (void)snprintf(key, sizeof(key), "key:%zu", index);
        if (!require(aivm_vm_map_builder_put_string_int(&vm, handle, key, (int64_t)index), "bulk insertion")) return 1;
    }
    if (!require(aivm_vm_map_builder_put_string_int(&vm, handle, "key:50000", 70000), "replacement")) return 1;
    if (!require(aivm_vm_map_builder_finish(&vm, handle), "builder finish")) return 1;
    if (!require(aivm_vm_map_count(&vm, handle, &count) && count == 100000U, "stable count")) return 1;

    if (!require(
            aivm_vm_map_get_string_int(&vm, handle, "key:0", -1, &value, &found) &&
            found && value == 0,
            "first lookup")) return 1;
    if (!require(
            aivm_vm_map_get_string_int(&vm, handle, "key:50000", -1, &value, &found) &&
            found && value == 70000,
            "replacement lookup")) return 1;
    if (!require(
            aivm_vm_map_get_string_int(&vm, handle, "key:99999", -1, &value, &found) &&
            found && value == 99999,
            "last lookup")) return 1;
    if (!require(
            aivm_vm_map_get_string_int(&vm, handle, "missing", -7, &value, &found) &&
            !found && value == -7,
            "missing lookup")) return 1;

    aivm_dispose(&vm);

    {
        static const AivmInstruction instructions[] = {
            { .opcode = AIVM_OP_MAP_BUILDER_NEW, .operand_int = 0 },
            { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
            { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
            { .opcode = AIVM_OP_CONST, .operand_int = 0 },
            { .opcode = AIVM_OP_PUSH_INT, .operand_int = 7 },
            { .opcode = AIVM_OP_MAP_BUILDER_PUT_STRING_INT, .operand_int = 0 },
            { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
            { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
            { .opcode = AIVM_OP_CONST, .operand_int = 1 },
            { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
            { .opcode = AIVM_OP_MAP_BUILDER_PUT_STRING_INT, .operand_int = 0 },
            { .opcode = AIVM_OP_MAP_BUILDER_FINISH, .operand_int = 0 },
            { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
            { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
            { .opcode = AIVM_OP_MAP_COUNT, .operand_int = 0 },
            { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
            { .opcode = AIVM_OP_CONST, .operand_int = 0 },
            { .opcode = AIVM_OP_MAP_HAS_STRING, .operand_int = 0 },
            { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
            { .opcode = AIVM_OP_CONST, .operand_int = 2 },
            { .opcode = AIVM_OP_PUSH_INT, .operand_int = -5 },
            { .opcode = AIVM_OP_MAP_GET_STRING_INT_OR, .operand_int = 0 },
            { .opcode = AIVM_OP_HALT, .operand_int = 0 }
        };
        static const AivmValue constants[] = {
            { .type = AIVM_VAL_STRING, .string_value = "alpha" },
            { .type = AIVM_VAL_STRING, .string_value = "beta" },
            { .type = AIVM_VAL_STRING, .string_value = "missing" }
        };
        static const AivmProgram program = {
            .instructions = instructions,
            .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
            .constants = constants,
            .constant_count = sizeof(constants) / sizeof(constants[0])
        };
        AivmValue result;

        aivm_init(&vm, &program);
        aivm_run(&vm);
        if (!require(vm.status == AIVM_VM_STATUS_HALTED, "opcode execution")) return 1;
        if (!require(aivm_stack_pop(&vm, &result) && result.type == AIVM_VAL_INT && result.int_value == -5, "opcode missing lookup")) return 1;
        if (!require(aivm_stack_pop(&vm, &result) && result.type == AIVM_VAL_BOOL && result.bool_value, "opcode membership")) return 1;
        if (!require(aivm_stack_pop(&vm, &result) && result.type == AIVM_VAL_INT && result.int_value == 2, "opcode count")) return 1;
        aivm_dispose(&vm);
    }

    puts("map storage test: PASS");
    return 0;
}
