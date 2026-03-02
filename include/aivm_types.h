#ifndef AIVM_TYPES_H
#define AIVM_TYPES_H

#include <stdint.h>

typedef enum {
    AIVM_VAL_VOID = 0,
    AIVM_VAL_INT = 1,
    AIVM_VAL_BOOL = 2,
    AIVM_VAL_STRING = 3,
    AIVM_VAL_NODE = 4,
    AIVM_VAL_UNKNOWN = 5
} AivmValueType;

typedef struct {
    AivmValueType type;
    union {
        int64_t int_value;
        int bool_value;
        const char* string_value;
        int64_t node_handle;
    };
} AivmValue;

AivmValue aivm_value_void(void);
AivmValue aivm_value_unknown(void);
AivmValue aivm_value_int(int64_t value);
AivmValue aivm_value_bool(int value);
AivmValue aivm_value_string(const char* value);
AivmValue aivm_value_node(int64_t handle);
int aivm_value_equals(AivmValue left, AivmValue right);

#endif
