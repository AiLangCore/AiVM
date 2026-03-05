#include "aivm_types.h"

#include <stddef.h>

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmValue void_value;
    AivmValue unknown_value;
    AivmValue int_value;
    AivmValue true_value;
    AivmValue false_value;
    AivmValue string_a;
    AivmValue string_b;
    AivmValue bytes_a;
    AivmValue bytes_b;
    AivmValue node_a;
    AivmValue node_b;
    const char hello_chars[] = { 'h', 'e', 'l', 'l', 'o', '\0' };
    const char hello_chars_copy[] = { 'h', 'e', 'l', 'l', 'o', '\0' };
    const uint8_t bytes_raw[] = { 0x00U, 0x01U, 0x7fU, 0xffU };
    const uint8_t bytes_raw_copy[] = { 0x00U, 0x01U, 0x7fU, 0xffU };
    const uint8_t bytes_raw_diff[] = { 0x00U, 0x02U, 0x7fU, 0xffU };

    void_value = aivm_value_void();
    if (expect(void_value.type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    unknown_value = aivm_value_unknown();
    if (expect(unknown_value.type == AIVM_VAL_UNKNOWN) != 0) {
        return 1;
    }

    int_value = aivm_value_int(42);
    if (expect(int_value.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(int_value.int_value == 42) != 0) {
        return 1;
    }

    true_value = aivm_value_bool(7);
    false_value = aivm_value_bool(0);
    if (expect(true_value.bool_value == 1) != 0) {
        return 1;
    }
    if (expect(false_value.bool_value == 0) != 0) {
        return 1;
    }

    string_a = aivm_value_string(hello_chars);
    string_b = aivm_value_string(hello_chars_copy);

    if (expect(aivm_value_equals(void_value, aivm_value_void()) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(unknown_value, aivm_value_unknown()) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(int_value, aivm_value_int(42)) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(true_value, false_value) == 0) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(string_a, string_b) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(string_a, aivm_value_string("world")) == 0) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_string(NULL), aivm_value_string(NULL)) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_string(NULL), string_a) == 0) != 0) {
        return 1;
    }
    bytes_a = aivm_value_bytes(bytes_raw, sizeof(bytes_raw));
    bytes_b = aivm_value_bytes(bytes_raw_copy, sizeof(bytes_raw_copy));
    if (expect(bytes_a.type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(bytes_a, bytes_b) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(bytes_a, aivm_value_bytes(bytes_raw_diff, sizeof(bytes_raw_diff))) == 0) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_bytes(NULL, 0U), aivm_value_bytes(NULL, 0U)) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_bytes(NULL, 1U), aivm_value_bytes(bytes_raw, 1U)) == 0) != 0) {
        return 1;
    }
    node_a = aivm_value_node(10);
    node_b = aivm_value_node(10);
    if (expect(node_a.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(node_a, node_b) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(node_a, aivm_value_node(11)) == 0) != 0) {
        return 1;
    }

    return 0;
}
