#include "aivm_vm_internal.h"
#include <stdint.h>
#include <stdio.h>

const char* aivm_vm_value_type_name(AivmValueType type)
{
    switch (type) {
        case AIVM_VAL_VOID: return "void";
        case AIVM_VAL_INT: return "int";
        case AIVM_VAL_NUMBER: return "number";
        case AIVM_VAL_BOOL: return "bool";
        case AIVM_VAL_NULL: return "null";
        case AIVM_VAL_STRING: return "string";
        case AIVM_VAL_BYTES: return "bytes";
        case AIVM_VAL_NODE: return "node";
        case AIVM_VAL_PAIR: return "pair";
        default: return "unknown";
    }
}

int aivm_vm_value_is_numeric(AivmValue value)
{
    return value.type == AIVM_VAL_INT || value.type == AIVM_VAL_NUMBER;
}

double aivm_vm_value_as_number(AivmValue value)
{
    return value.type == AIVM_VAL_INT ? (double)value.int_value : value.number_value;
}

static int64_t double_truncate_to_i64(double value)
{
    return (int64_t)value;
}

static int double_is_i64_value(double value, int64_t* out)
{
    int64_t truncated = double_truncate_to_i64(value);
    if ((double)truncated != value) {
        return 0;
    }
    if (out != NULL) {
        *out = truncated;
    }
    return 1;
}

AivmValue aivm_vm_numeric_result(double value)
{
    int64_t int_value = 0;
    if (double_is_i64_value(value, &int_value)) {
        return aivm_value_int(int_value);
    }
    return aivm_value_number(value);
}

double aivm_vm_double_trunc_toward_zero(double value)
{
    return (double)((int64_t)value);
}

double aivm_vm_double_pow_whole(double base, double exponent)
{
    int64_t exp = (int64_t)exponent;
    int negative = 0;
    double result = 1.0;
    if ((double)exp != exponent) {
        return 0.0;
    }
    if (exp < 0) {
        negative = 1;
        exp = -exp;
    }
    while (exp > 0) {
        result *= base;
        exp -= 1;
    }
    return negative ? (1.0 / result) : result;
}

void aivm_vm_set_add_numeric_type_error(AivmVm* vm, AivmValue left, AivmValue right)
{
    const char* left_text = "";
    const char* right_text = "";
    unsigned long long ret0 = 0ULL;
    unsigned long long ret1 = 0ULL;
    if (vm == NULL) {
        return;
    }
    if (vm->call_frame_count > 0U) {
        ret0 = (unsigned long long)vm->call_frames[vm->call_frame_count - 1U].return_instruction_pointer;
        if (vm->call_frame_count > 1U) {
            ret1 = (unsigned long long)vm->call_frames[vm->call_frame_count - 2U].return_instruction_pointer;
        }
    }
    if (left.type == AIVM_VAL_STRING && left.string_value != NULL) {
        left_text = left.string_value;
    }
    if (right.type == AIVM_VAL_STRING && right.string_value != NULL) {
        right_text = right.string_value;
    }
    (void)snprintf(
        vm->error_detail_storage,
        sizeof(vm->error_detail_storage),
        "ADD_INT requires int operands. left=%s(\"%.40s\") right=%s(\"%.40s\") ip=%llu ret0=%llu ret1=%llu",
        aivm_vm_value_type_name(left.type),
        left_text,
        aivm_vm_value_type_name(right.type),
        right_text,
        (unsigned long long)vm->instruction_pointer,
        ret0,
        ret1);
    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, vm->error_detail_storage);
}
