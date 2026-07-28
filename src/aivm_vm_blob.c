#include "aivm_vm_internal.h"
#include <stdlib.h>
#include <string.h>

static void clear_blob_record(AivmBlobRecord* blob)
{
    if (blob == NULL) {
        return;
    }
    free(blob->data);
    blob->data = NULL;
    blob->length = 0U;
    blob->handle = 0;
    blob->active = 0;
}

void aivm_release_all_blobs(AivmVm* vm)
{
    size_t index;
    if (vm == NULL) {
        return;
    }
    for (index = 0U; index < AIVM_VM_BLOB_CAPACITY; index += 1U) {
        clear_blob_record(&vm->blobs[index]);
    }
    vm->blob_count = 0U;
    vm->blob_bytes_used = 0U;
}

static AivmBlobRecord* find_blob(AivmVm* vm, int64_t handle)
{
    size_t index;
    if (vm == NULL || handle <= 0) {
        return NULL;
    }
    for (index = 0U; index < AIVM_VM_BLOB_CAPACITY; index += 1U) {
        if (vm->blobs[index].active != 0 && vm->blobs[index].handle == handle) {
            return &vm->blobs[index];
        }
    }
    return NULL;
}

static const AivmBlobRecord* find_blob_const(const AivmVm* vm, int64_t handle)
{
    size_t index;
    if (vm == NULL || handle <= 0) {
        return NULL;
    }
    for (index = 0U; index < AIVM_VM_BLOB_CAPACITY; index += 1U) {
        if (vm->blobs[index].active != 0 && vm->blobs[index].handle == handle) {
            return &vm->blobs[index];
        }
    }
    return NULL;
}

const char* aivm_blob_status_code(AivmBlobStatus status)
{
    switch (status) {
        case AIVM_BLOB_OK:
            return "AIVMB000";
        case AIVM_BLOB_ERR_INVALID:
            return "AIVMB001";
        case AIVM_BLOB_ERR_LIMIT:
            return "AIVMB002";
        case AIVM_BLOB_ERR_NOT_FOUND:
            return "AIVMB003";
        case AIVM_BLOB_ERR_OOM:
            return "AIVMB004";
        default:
            return "AIVMB999";
    }
}

AivmBlobStatus aivm_blob_create(AivmVm* vm, const uint8_t* data, size_t length, int64_t* out_handle)
{
    size_t index;
    size_t needed;
    AivmBlobRecord* slot = NULL;
    uint8_t* copy = NULL;

    if (vm == NULL || out_handle == NULL || (length > 0U && data == NULL)) {
        return AIVM_BLOB_ERR_INVALID;
    }
    if (!aivm_vm_ensure_storage(vm)) {
        aivm_counter_increment_saturating(&vm->blob_pressure_count);
        return AIVM_BLOB_ERR_OOM;
    }
    if (vm->blob_count >= AIVM_VM_BLOB_CAPACITY ||
        !aivm_size_add_checked(vm->blob_bytes_used, length, &needed) ||
        needed > AIVM_VM_BLOB_BYTES ||
        vm->next_blob_handle <= 0) {
        aivm_counter_increment_saturating(&vm->blob_pressure_count);
        return AIVM_BLOB_ERR_LIMIT;
    }
    for (index = 0U; index < AIVM_VM_BLOB_CAPACITY; index += 1U) {
        if (vm->blobs[index].active == 0) {
            slot = &vm->blobs[index];
            break;
        }
    }
    if (slot == NULL) {
        aivm_counter_increment_saturating(&vm->blob_pressure_count);
        return AIVM_BLOB_ERR_LIMIT;
    }
    if (length > 0U) {
        copy = (uint8_t*)malloc(length);
        if (copy == NULL) {
            aivm_counter_increment_saturating(&vm->blob_pressure_count);
            return AIVM_BLOB_ERR_OOM;
        }
        memcpy(copy, data, length);
    }

    slot->handle = vm->next_blob_handle;
    slot->data = copy;
    slot->length = length;
    slot->active = 1;
    *out_handle = vm->next_blob_handle;
    vm->next_blob_handle += 1;
    vm->blob_count += 1U;
    vm->blob_bytes_used = needed;
    if (vm->blob_bytes_used > vm->blob_bytes_high_water) {
        vm->blob_bytes_high_water = vm->blob_bytes_used;
    }
    return AIVM_BLOB_OK;
}

AivmBlobStatus aivm_blob_read(
    const AivmVm* vm,
    int64_t handle,
    size_t offset,
    uint8_t* out_data,
    size_t length,
    size_t* out_read)
{
    const AivmBlobRecord* blob;
    size_t available;
    size_t read_length;

    if (out_read == NULL || (length > 0U && out_data == NULL)) {
        return AIVM_BLOB_ERR_INVALID;
    }
    *out_read = 0U;
    blob = find_blob_const(vm, handle);
    if (blob == NULL) {
        return AIVM_BLOB_ERR_NOT_FOUND;
    }
    if (offset >= blob->length) {
        return AIVM_BLOB_OK;
    }
    available = blob->length - offset;
    read_length = length < available ? length : available;
    if (read_length > 0U) {
        memcpy(out_data, blob->data + offset, read_length);
    }
    *out_read = read_length;
    return AIVM_BLOB_OK;
}

AivmBlobStatus aivm_blob_release(AivmVm* vm, int64_t handle)
{
    AivmBlobRecord* blob = find_blob(vm, handle);
    size_t length;
    if (blob == NULL || vm == NULL) {
        return AIVM_BLOB_ERR_NOT_FOUND;
    }
    length = blob->length;
    clear_blob_record(blob);
    if (vm->blob_count > 0U) {
        vm->blob_count -= 1U;
    }
    if (vm->blob_bytes_used >= length) {
        vm->blob_bytes_used -= length;
    } else {
        vm->blob_bytes_used = 0U;
    }
    return AIVM_BLOB_OK;
}

size_t aivm_blob_active_count(const AivmVm* vm)
{
    if (vm == NULL) {
        return 0U;
    }
    return vm->blob_count;
}
