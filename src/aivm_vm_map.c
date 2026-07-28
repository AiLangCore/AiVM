#include "aivm_vm_internal.h"

#include <stdlib.h>
#include <string.h>

static uint64_t map_hash_string(const char* key)
{
    const unsigned char* cursor = (const unsigned char*)key;
    uint64_t hash = 1469598103934665603ULL;
    while (*cursor != 0U) {
        hash ^= (uint64_t)*cursor++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static char* map_copy_string(const char* key)
{
    size_t length;
    char* copy;
    if (key == NULL) return NULL;
    length = strlen(key);
    copy = (char*)malloc(length + 1U);
    if (copy == NULL) return NULL;
    memcpy(copy, key, length + 1U);
    return copy;
}

static void map_dispose(AivmMapRecord* map)
{
    size_t index;
    if (map == NULL) return;
    for (index = 0U; index < map->slot_capacity; index += 1U) {
        if (map->slots[index].occupied) free(map->slots[index].key);
    }
    free(map->slots);
    memset(map, 0, sizeof(*map));
}

static AivmMapRecord* map_lookup(AivmVm* vm, int64_t handle)
{
    if (vm == NULL || handle <= 0 || handle > (int64_t)AIVM_VM_MAP_CAPACITY) return NULL;
    if (!vm->maps[handle - 1].active) return NULL;
    return &vm->maps[handle - 1];
}

static const AivmMapRecord* map_lookup_const(const AivmVm* vm, int64_t handle)
{
    if (vm == NULL || handle <= 0 || handle > (int64_t)AIVM_VM_MAP_CAPACITY) return NULL;
    if (!vm->maps[handle - 1].active) return NULL;
    return &vm->maps[handle - 1];
}

static size_t map_slot_for(const AivmMapSlot* slots, size_t capacity, const char* key, uint64_t hash)
{
    size_t slot = (size_t)(hash % (uint64_t)capacity);
    while (slots[slot].occupied && (slots[slot].hash != hash || strcmp(slots[slot].key, key) != 0)) {
        slot = (slot + 1U) % capacity;
    }
    return slot;
}

static int map_resize(AivmVm* vm, AivmMapRecord* map, size_t capacity)
{
    AivmMapSlot* replacement;
    size_t index;
    size_t growth_bytes;
    if (capacity > ((size_t)-1) / sizeof(AivmMapSlot)) return 0;
    growth_bytes = capacity * sizeof(AivmMapSlot);
    if (growth_bytes > map->slot_capacity * sizeof(AivmMapSlot) &&
        !aivm_vm_admit_host_memory_growth(vm, growth_bytes - map->slot_capacity * sizeof(AivmMapSlot))) {
        return 0;
    }
    replacement = (AivmMapSlot*)calloc(capacity, sizeof(AivmMapSlot));
    if (replacement == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "MAP_BUILDER growth failed.");
        return 0;
    }
    for (index = 0U; index < map->slot_capacity; index += 1U) {
        if (map->slots[index].occupied) {
            size_t slot = map_slot_for(replacement, capacity, map->slots[index].key, map->slots[index].hash);
            replacement[slot] = map->slots[index];
        }
    }
    free(map->slots);
    map->slots = replacement;
    map->slot_capacity = capacity;
    return 1;
}

void aivm_vm_reset_maps(AivmVm* vm)
{
    size_t index;
    if (vm == NULL) return;
    for (index = 0U; index < AIVM_VM_MAP_CAPACITY; index += 1U) map_dispose(&vm->maps[index]);
}

int aivm_vm_map_builder_new(AivmVm* vm, int64_t* out_handle)
{
    size_t index;
    AivmMapRecord* map;
    if (vm == NULL || out_handle == NULL) return 0;
    for (index = 0U; index < AIVM_VM_MAP_CAPACITY; index += 1U) {
        if (!vm->maps[index].active) break;
    }
    if (index == AIVM_VM_MAP_CAPACITY) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "MAP_BUILDER capacity exceeded.");
        return 0;
    }
    map = &vm->maps[index];
    map->active = 1;
    if (!map_resize(vm, map, 16U)) {
        map_dispose(map);
        return 0;
    }
    *out_handle = (int64_t)(index + 1U);
    return 1;
}

int aivm_vm_map_builder_put_string_int(AivmVm* vm, int64_t handle, const char* key, int64_t value)
{
    AivmMapRecord* map = map_lookup(vm, handle);
    uint64_t hash;
    size_t slot;
    char* key_copy;
    if (map == NULL || map->frozen || key == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAP_BUILDER_PUT requires an active builder and string key.");
        return 0;
    }
    if ((map->count + 1U) * 10U >= map->slot_capacity * 7U &&
        !map_resize(vm, map, map->slot_capacity * 2U)) {
        return 0;
    }
    hash = map_hash_string(key);
    slot = map_slot_for(map->slots, map->slot_capacity, key, hash);
    if (map->slots[slot].occupied) {
        map->slots[slot].value = value;
        return 1;
    }
    key_copy = map_copy_string(key);
    if (key_copy == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "MAP_BUILDER key allocation failed.");
        return 0;
    }
    map->slots[slot].hash = hash;
    map->slots[slot].key = key_copy;
    map->slots[slot].value = value;
    map->slots[slot].occupied = 1;
    map->count += 1U;
    return 1;
}

int aivm_vm_map_builder_finish(AivmVm* vm, int64_t handle)
{
    AivmMapRecord* map = map_lookup(vm, handle);
    if (map == NULL || map->frozen) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAP_BUILDER_FINISH requires an active builder.");
        return 0;
    }
    map->frozen = 1;
    return 1;
}

int aivm_vm_map_count(const AivmVm* vm, int64_t handle, size_t* out_count)
{
    const AivmMapRecord* map = map_lookup_const(vm, handle);
    if (map == NULL || !map->frozen || out_count == NULL) return 0;
    *out_count = map->count;
    return 1;
}

int aivm_vm_map_get_string_int(
    const AivmVm* vm,
    int64_t handle,
    const char* key,
    int64_t fallback,
    int64_t* out_value,
    int* out_found)
{
    const AivmMapRecord* map = map_lookup_const(vm, handle);
    uint64_t hash;
    size_t slot;
    if (map == NULL || !map->frozen || key == NULL || out_value == NULL || out_found == NULL) return 0;
    hash = map_hash_string(key);
    slot = map_slot_for(map->slots, map->slot_capacity, key, hash);
    if (!map->slots[slot].occupied) {
        *out_value = fallback;
        *out_found = 0;
        return 1;
    }
    *out_value = map->slots[slot].value;
    *out_found = 1;
    return 1;
}
