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

AivmValue aivm_value_number(double input)
{
    AivmValue value;
    value.type = AIVM_VAL_NUMBER;
    value.number_value = input;
    return value;
}

AivmValue aivm_value_bool(int input)
{
    AivmValue value;
    value.type = AIVM_VAL_BOOL;
    value.bool_value = (input != 0) ? 1 : 0;
    return value;
}

AivmValue aivm_value_null(void)
{
    AivmValue value;
    value.type = AIVM_VAL_NULL;
    value.int_value = 0;
    return value;
}

AivmValue aivm_value_string(const char* input)
{
    AivmValue value;
    value.type = AIVM_VAL_STRING;
    value.string_value = input;
    return value;
}

AivmValue aivm_value_bytes(const uint8_t* data, size_t length)
{
    AivmValue value;
    value.type = AIVM_VAL_BYTES;
    value.bytes_value.data = data;
    value.bytes_value.length = length;
    return value;
}

AivmValue aivm_value_node(int64_t input)
{
    AivmValue value;
    value.type = AIVM_VAL_NODE;
    value.node_handle = input;
    return value;
}

AivmValue aivm_value_pair(int64_t input)
{
    AivmValue value;
    value.type = AIVM_VAL_PAIR;
    value.pair_handle = input;
    return value;
}

int aivm_value_equals(AivmValue left, AivmValue right)
{
    if ((left.type == AIVM_VAL_INT || left.type == AIVM_VAL_NUMBER) &&
        (right.type == AIVM_VAL_INT || right.type == AIVM_VAL_NUMBER)) {
        double left_number = (left.type == AIVM_VAL_INT) ? (double)left.int_value : left.number_value;
        double right_number = (right.type == AIVM_VAL_INT) ? (double)right.int_value : right.number_value;
        return left_number == right_number ? 1 : 0;
    }

    if (left.type != right.type) {
        return 0;
    }

    switch (left.type) {
        case AIVM_VAL_VOID:
            return 1;

        case AIVM_VAL_INT:
            return left.int_value == right.int_value ? 1 : 0;

        case AIVM_VAL_NUMBER:
            return left.number_value == right.number_value ? 1 : 0;

        case AIVM_VAL_BOOL:
            return left.bool_value == right.bool_value ? 1 : 0;

        case AIVM_VAL_NULL:
            return 1;

        case AIVM_VAL_STRING:
            return aivm_cstring_equals(left.string_value, right.string_value);

        case AIVM_VAL_BYTES: {
            size_t i;
            if (left.bytes_value.length != right.bytes_value.length) {
                return 0;
            }
            if (left.bytes_value.length == 0U) {
                return 1;
            }
            if (left.bytes_value.data == NULL || right.bytes_value.data == NULL) {
                return 0;
            }
            for (i = 0U; i < left.bytes_value.length; i += 1U) {
                if (left.bytes_value.data[i] != right.bytes_value.data[i]) {
                    return 0;
                }
            }
            return 1;
        }

        case AIVM_VAL_NODE:
            return left.node_handle == right.node_handle ? 1 : 0;

        case AIVM_VAL_PAIR:
            return left.pair_handle == right.pair_handle ? 1 : 0;

        case AIVM_VAL_UNKNOWN:
            return 1;

        default:
            return 0;
    }
}

int aivm_value_is_immutable_message_payload(AivmValue value)
{
    switch (value.type) {
        case AIVM_VAL_VOID:
        case AIVM_VAL_INT:
        case AIVM_VAL_NUMBER:
        case AIVM_VAL_BOOL:
        case AIVM_VAL_NULL:
            return 1;

        case AIVM_VAL_STRING:
            return value.string_value != NULL ? 1 : 0;

        case AIVM_VAL_BYTES:
            return value.bytes_value.length == 0U || value.bytes_value.data != NULL ? 1 : 0;

        case AIVM_VAL_NODE:
        case AIVM_VAL_PAIR:
        case AIVM_VAL_UNKNOWN:
        default:
            return 0;
    }
}
