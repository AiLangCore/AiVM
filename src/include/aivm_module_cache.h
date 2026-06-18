#ifndef AIVM_MODULE_CACHE_H
#define AIVM_MODULE_CACHE_H

#include <stddef.h>

#include "aivm_program.h"

typedef enum {
    AIVM_MODULE_CACHE_OK = 0,
    AIVM_MODULE_CACHE_ERR_INVALID = 1,
    AIVM_MODULE_CACHE_ERR_LIMIT = 2,
    AIVM_MODULE_CACHE_ERR_DUPLICATE = 3,
    AIVM_MODULE_CACHE_ERR_NOT_FOUND = 4
} AivmModuleCacheStatus;

enum {
    AIVM_MODULE_CACHE_MAX_MODULES = 64,
    AIVM_MODULE_CACHE_MAX_NAME_LENGTH = 128,
    AIVM_MODULE_CACHE_MAX_BYTES = 32 * 1024 * 1024
};

typedef struct {
    char name[AIVM_MODULE_CACHE_MAX_NAME_LENGTH];
    AivmProgram program;
    size_t estimated_bytes;
    int active;
} AivmModuleCacheEntry;

typedef struct {
    AivmModuleCacheEntry entries[AIVM_MODULE_CACHE_MAX_MODULES];
    size_t count;
    size_t estimated_bytes_used;
} AivmModuleCache;

void aivm_module_cache_init(AivmModuleCache* cache);
void aivm_module_cache_clear(AivmModuleCache* cache);
AivmModuleCacheStatus aivm_module_cache_put(
    AivmModuleCache* cache,
    const char* name,
    const AivmProgram* program);
AivmModuleCacheStatus aivm_module_cache_get(
    const AivmModuleCache* cache,
    const char* name,
    const AivmProgram** out_program);
AivmModuleCacheStatus aivm_module_cache_load_aibc1(
    AivmModuleCache* cache,
    const char* name,
    const uint8_t* bytes,
    size_t byte_count,
    AivmProgramLoadResult* out_load_result);
size_t aivm_module_cache_count(const AivmModuleCache* cache);
size_t aivm_module_cache_estimated_bytes(const AivmModuleCache* cache);
const char* aivm_module_cache_status_code(AivmModuleCacheStatus status);

#endif
