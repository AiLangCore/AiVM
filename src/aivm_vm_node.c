#include "aivm_vm_internal.h"

int aivm_vm_lookup_node(const AivmVm* vm, int64_t handle, const AivmNodeRecord** out_node)
{
    if (vm == NULL || out_node == NULL || handle <= 0 || handle > (int64_t)vm->node_count) {
        return 0;
    }
    *out_node = &vm->nodes[(size_t)(handle - 1)];
    return 1;
}

int aivm_vm_lookup_scratch_pair(const AivmVm* vm, int64_t handle, const AivmScratchPair** out_pair)
{
    if (vm == NULL || out_pair == NULL || handle <= 0 || handle > (int64_t)vm->scratch_pair_count) {
        return 0;
    }
    *out_pair = &vm->scratch_pairs[(size_t)(handle - 1)];
    return 1;
}

int aivm_vm_create_scratch_pair(AivmVm* vm, AivmValue first, AivmValue second, int64_t* out_handle)
{
    size_t next_count = 0U;
    if (vm == NULL || out_handle == NULL) {
        return 0;
    }
    if (vm->scratch_pair_count >= AIVM_VM_SCRATCH_PAIR_CAPACITY ||
        !aivm_size_add_checked(vm->scratch_pair_count, 1U, &next_count)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: scratch pair capacity exceeded.");
        return 0;
    }
    vm->scratch_pairs[vm->scratch_pair_count].first = first;
    vm->scratch_pairs[vm->scratch_pair_count].second = second;
    *out_handle = (int64_t)next_count;
    vm->scratch_pair_count = next_count;
    return 1;
}
