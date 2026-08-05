#include "aivm_worker_spill.h"

#include <stdio.h>
#include <stdlib.h>

struct AivmWorkerSpill {
    FILE* file;
    size_t length;
};

int aivm_worker_spill_write(
    const uint8_t* data,
    size_t length,
    AivmWorkerSpill** out_spill)
{
    AivmWorkerSpill* spill;
    if (out_spill == NULL || (length > 0U && data == NULL)) return 0;
    *out_spill = NULL;
    spill = (AivmWorkerSpill*)calloc(1U, sizeof(*spill));
    if (spill == NULL) return 0;
    spill->file = tmpfile();
    if (spill->file == NULL ||
        (length > 0U && fwrite(data, 1U, length, spill->file) != length) ||
        fflush(spill->file) != 0) {
        aivm_worker_spill_destroy(spill);
        return 0;
    }
    spill->length = length;
    *out_spill = spill;
    return 1;
}

int aivm_worker_spill_read(
    AivmWorkerSpill* spill,
    uint8_t* destination,
    size_t length)
{
    if (spill == NULL || length != spill->length ||
        (length > 0U && destination == NULL) ||
        fseek(spill->file, 0L, SEEK_SET) != 0) {
        return 0;
    }
    return length == 0U || fread(destination, 1U, length, spill->file) == length;
}

void aivm_worker_spill_destroy(AivmWorkerSpill* spill)
{
    if (spill == NULL) return;
    if (spill->file != NULL) (void)fclose(spill->file);
    free(spill);
}
