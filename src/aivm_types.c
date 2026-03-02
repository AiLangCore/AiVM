#include "aivm_types.h"

#include <stddef.h>

static int aivm_cstring_equals(const char* left, const char* right)
{
    size_t i;

    if (left == right) {
        return 1;
    }
    if (left == NULL || right == NULL) {
        return 0;
    }

    i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        i += 1U;
    }

    return left[i] == right[i] ? 1 : 0;
}
AivmValue aivm_value_void(void)
{
    AivmValue value;
    value.type = AIVM_VAL_VOID;
    value.int_value = 0;
    return value;
}

AivmValue aivm_value_unknown(void)
{
    AivmValue value;
    value.type = AIVM_VAL_UNKNOWN;
    value.int_value = 0;
    return value;
}

AivmValue aivm_value_int(int64_t input)
{
    AivmValue value;
    value.type = AIVM_VAL_INT;
    value.int_value = input;
    return value;
}

AivmValue aivm_value_bool(int input)
{
    AivmValue value;
    value.type = AIVM_VAL_BOOL;
    value.bool_value = (input != 0) ? 1 : 0;
    return value;
}

AivmValue aivm_value_string(const char* input)
{
    AivmValue value;
    value.type = AIVM_VAL_STRING;
    value.string_value = input;
    return value;
}

AivmValue aivm_value_node(int64_t input)
{
    AivmValue value;
    value.type = AIVM_VAL_NODE;
    value.node_handle = input;
    return value;
}

int aivm_value_equals(AivmValue left, AivmValue right)
{
    if (left.type != right.type) {
        return 0;
    }

    switch (left.type) {
        case AIVM_VAL_VOID:
            return 1;

        case AIVM_VAL_INT:
            return left.int_value == right.int_value ? 1 : 0;

        case AIVM_VAL_BOOL:
            return left.bool_value == right.bool_value ? 1 : 0;

        case AIVM_VAL_STRING:
            return aivm_cstring_equals(left.string_value, right.string_value);

        case AIVM_VAL_NODE:
            return left.node_handle == right.node_handle ? 1 : 0;

        case AIVM_VAL_UNKNOWN:
            return 1;

        default:
            return 0;
    }
}
