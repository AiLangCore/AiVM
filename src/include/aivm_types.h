#ifndef AIVM_TYPES_H
#define AIVM_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const uint8_t* data;
    size_t length;
} AivmBytesView;

typedef enum {
    AIVM_VAL_VOID = 0,
    AIVM_VAL_INT = 1,
    AIVM_VAL_NUMBER = 2,
    AIVM_VAL_BOOL = 3,
    AIVM_VAL_NULL = 4,
    AIVM_VAL_STRING = 5,
    AIVM_VAL_BYTES = 6,
    AIVM_VAL_NODE = 7,
    AIVM_VAL_PAIR = 8,
    AIVM_VAL_UNKNOWN = 9,
    /* Compiler-internal mutable construction state; never crosses a boundary. */
    AIVM_VAL_NODE_BUILDER = 10
} AivmValueType;

typedef struct {
    AivmValueType type;
    union {
        int64_t int_value;
        double number_value;
        int bool_value;
        const char* string_value;
        AivmBytesView bytes_value;
        int64_t node_handle;
        int64_t pair_handle;
        int64_t node_builder_handle;
    };
} AivmValue;

AivmValue aivm_value_void(void);
AivmValue aivm_value_unknown(void);
AivmValue aivm_value_int(int64_t value);
AivmValue aivm_value_number(double value);
AivmValue aivm_value_bool(int value);
AivmValue aivm_value_null(void);
AivmValue aivm_value_string(const char* value);
AivmValue aivm_value_bytes(const uint8_t* data, size_t length);
AivmValue aivm_value_node(int64_t handle);
AivmValue aivm_value_pair(int64_t handle);
AivmValue aivm_value_node_builder(int64_t handle);
int aivm_value_equals(AivmValue left, AivmValue right);
int aivm_value_is_immutable_message_payload(AivmValue value);

#endif
