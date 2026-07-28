#include "aivm_worker_capacity.h"

#include <stdio.h>

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    size_t available = aivm_worker_available_logical_cpu();
    size_t expected = (available * 96U) / 100U;
    size_t capacity;

    if (expected == 0U) {
        expected = 1U;
    }
    capacity = aivm_worker_active_capacity(0U, 0U);
    if (expect(capacity == expected) != 0) {
        fprintf(stderr, "expected cpu capacity %zu, got %zu\n", expected, capacity);
        return 1;
    }
    if (expect(aivm_worker_active_capacity(3U, 0U) <= 3U) != 0) {
        return 1;
    }
    if (expect(aivm_worker_active_capacity(0U, 2U) <= 2U) != 0) {
        return 1;
    }
    if (expect(aivm_worker_active_capacity(1U, 1U) == 1U) != 0) {
        return 1;
    }
    return 0;
}
