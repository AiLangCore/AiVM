#include "aivm_program.h"
#include "aivm_vm.h"

#include <stdio.h>
#include <string.h>

static void write_u32(uint8_t* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static void write_i64(uint8_t* bytes, size_t offset, int64_t value)
{
    size_t index;
    uint64_t raw = (uint64_t)value;
    for (index = 0U; index < 8U; index += 1U) {
        bytes[offset + index] = (uint8_t)(raw >> (index * 8U));
    }
}

static void write_instruction(
    uint8_t* bytes,
    size_t offset,
    AivmOpcode opcode,
    int64_t operand)
{
    write_u32(bytes, offset, (uint32_t)opcode);
    write_i64(bytes, offset + 4U, operand);
}

static void make_identity_worker(uint8_t artifact[64])
{
    memset(artifact, 0, 64U);
    memcpy(artifact, "AIBC", 4U);
    write_u32(artifact, 4U, 2U);
    write_u32(artifact, 12U, 1U);
    write_u32(artifact, 16U, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
    write_u32(artifact, 20U, 40U);
    write_u32(artifact, 24U, 3U);
    write_instruction(artifact, 28U, AIVM_OP_STORE_LOCAL, 0);
    write_instruction(artifact, 40U, AIVM_OP_LOAD_LOCAL, 0);
    write_instruction(artifact, 52U, AIVM_OP_RET, 0);
}

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int test_worker_ref_run_await(void)
{
    static AivmVm vm;
    uint8_t artifact[64];
    AivmWorkerCatalogEntry entry;
    AivmProgram program;
    AivmValue output;
    static const uint8_t payload[] = { 4U, 5U, 6U, 7U };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_BYTES,
          .bytes_value = { .data = payload, .length = sizeof(payload) } }
    };
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_WORKER_REF, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_WORKER_RUN, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };

    make_identity_worker(artifact);
    memset(&entry, 0, sizeof(entry));
    entry.function_target = 0U;
    entry.transport_abi = AIVM_WORKER_TRANSPORT_ABI_BYTES_V1;
    entry.bytecode_version = 2U;
    entry.artifact = artifact;
    entry.artifact_length = sizeof(artifact);
    aivm_program_init(&program, instructions, 5U);
    program.constants = constants;
    program.constant_count = 1U;
    program.worker_catalog.entries = &entry;
    program.worker_catalog.count = 1U;

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0 ||
        expect(aivm_stack_pop(&vm, &output) == 1) != 0 ||
        expect(output.type == AIVM_VAL_BYTES) != 0 ||
        expect(output.bytes_value.length == sizeof(payload)) != 0 ||
        expect(memcmp(output.bytes_value.data, payload, sizeof(payload)) == 0) != 0 ||
        expect(vm.completed_task_count == 0U) != 0) {
        aivm_dispose(&vm);
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int test_worker_ref_rejects_invalid_catalog_index(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_WORKER_REF, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, instructions, 1U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0 ||
        expect(strcmp(aivm_vm_error_detail(&vm),
            "WORKER_REF requires a valid WorkerCatalog index.") == 0) != 0) {
        aivm_dispose(&vm);
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int test_worker_run_all_task_at_preserves_canonical_index(void)
{
    static AivmVm vm;
    uint8_t artifact[64];
    AivmWorkerCatalogEntry entry;
    AivmProgram program;
    AivmValue output;
    static const uint8_t payload1[] = { 2U, 3U };
    static const uint8_t batch[] = {
        1U, 0U, 0U, 0U, 1U,
        2U, 0U, 0U, 0U, 2U, 3U,
        3U, 0U, 0U, 0U, 4U, 5U, 6U
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_BYTES,
          .bytes_value = { .data = batch, .length = sizeof(batch) } },
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_WORKER_REF, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_WORKER_RUN_ALL, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_WORKER_TASK_AT, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };

    make_identity_worker(artifact);
    memset(&entry, 0, sizeof(entry));
    entry.function_target = 0U;
    entry.transport_abi = AIVM_WORKER_TRANSPORT_ABI_BYTES_V1;
    entry.bytecode_version = 2U;
    entry.artifact = artifact;
    entry.artifact_length = sizeof(artifact);
    aivm_program_init(&program, instructions, 7U);
    program.constants = constants;
    program.constant_count = 2U;
    program.worker_catalog.entries = &entry;
    program.worker_catalog.count = 1U;

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0 ||
        expect(aivm_stack_pop(&vm, &output) == 1) != 0 ||
        expect(output.type == AIVM_VAL_BYTES) != 0 ||
        expect(output.bytes_value.length == sizeof(payload1)) != 0 ||
        expect(memcmp(output.bytes_value.data, payload1, sizeof(payload1)) == 0) != 0 ||
        expect(vm.completed_task_count == 2U) != 0) {
        aivm_dispose(&vm);
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int test_worker_run_all_rejects_truncated_batch(void)
{
    static AivmVm vm;
    uint8_t artifact[64];
    AivmWorkerCatalogEntry entry;
    AivmProgram program;
    static const uint8_t malformed_batch[] = { 2U, 0U, 0U, 0U, 9U };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_BYTES,
          .bytes_value = {
              .data = malformed_batch, .length = sizeof(malformed_batch) } }
    };
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_WORKER_REF, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_WORKER_RUN_ALL, .operand_int = 1 }
    };

    make_identity_worker(artifact);
    memset(&entry, 0, sizeof(entry));
    entry.function_target = 0U;
    entry.transport_abi = AIVM_WORKER_TRANSPORT_ABI_BYTES_V1;
    entry.bytecode_version = 2U;
    entry.artifact = artifact;
    entry.artifact_length = sizeof(artifact);
    aivm_program_init(&program, instructions, 3U);
    program.constants = constants;
    program.constant_count = 1U;
    program.worker_catalog.entries = &entry;
    program.worker_catalog.count = 1U;

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0 ||
        expect(strcmp(aivm_vm_error_detail(&vm),
            "WORKER_RUN_ALL batch payload is truncated.") == 0) != 0 ||
        expect(vm.completed_task_count == 0U) != 0) {
        aivm_dispose(&vm);
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int test_worker_run_all_refills_bounded_window(void)
{
    static AivmVm vm;
    uint8_t artifact[64];
    uint8_t batch[1500];
    AivmWorkerCatalogEntry entry;
    AivmProgram program;
    AivmValue constants[3];
    AivmValue output;
    size_t index;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_WORKER_REF, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_WORKER_RUN_ALL, .operand_int = 1 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_WORKER_TASK_AT, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_POP, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_WORKER_TASK_AT, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };

    for (index = 0U; index < 300U; index += 1U) {
        size_t offset = index * 5U;
        batch[offset] = 1U;
        batch[offset + 1U] = 0U;
        batch[offset + 2U] = 0U;
        batch[offset + 3U] = 0U;
        batch[offset + 4U] = (uint8_t)index;
    }
    constants[0] = aivm_value_bytes(batch, sizeof(batch));
    constants[1] = aivm_value_int(0);
    constants[2] = aivm_value_int(256);
    make_identity_worker(artifact);
    memset(&entry, 0, sizeof(entry));
    entry.function_target = 0U;
    entry.transport_abi = AIVM_WORKER_TRANSPORT_ABI_BYTES_V1;
    entry.bytecode_version = 2U;
    entry.artifact = artifact;
    entry.artifact_length = sizeof(artifact);
    aivm_program_init(&program, instructions, 14U);
    program.constants = constants;
    program.constant_count = 3U;
    program.worker_catalog.entries = &entry;
    program.worker_catalog.count = 1U;

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0 ||
        expect(aivm_stack_pop(&vm, &output) == 1) != 0 ||
        expect(output.type == AIVM_VAL_BYTES) != 0 ||
        expect(output.bytes_value.length == 1U) != 0 ||
        expect(output.bytes_value.data[0] == 0U) != 0 ||
        expect(vm.worker_task_group_count == 1U) != 0 ||
        expect(vm.worker_task_groups[0].task_count == 300U) != 0 ||
        expect(vm.worker_task_groups[0].next_materialize_index == 258U) != 0 ||
        expect(vm.worker_task_groups[0].task_handles[256] != 0) != 0 ||
        expect(vm.worker_task_groups[0].task_handles[257] != 0) != 0 ||
        expect(vm.completed_task_count == AIVM_VM_TASK_CAPACITY) != 0) {
        aivm_dispose(&vm);
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

int main(void)
{
    if (test_worker_ref_run_await() != 0 ||
        test_worker_ref_rejects_invalid_catalog_index() != 0 ||
        test_worker_run_all_task_at_preserves_canonical_index() != 0 ||
        test_worker_run_all_rejects_truncated_batch() != 0 ||
        test_worker_run_all_refills_bounded_window() != 0) {
        return 1;
    }
    printf("aivm VM worker task tests passed\n");
    return 0;
}
