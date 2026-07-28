#ifndef AIVM_WORKER_CATALOG_H
#define AIVM_WORKER_CATALOG_H

#include <stddef.h>
#include <stdint.h>

enum {
    AIVM_WORKER_TRANSPORT_ABI_BYTES_V1 = 1,
    AIVM_WORKER_IDENTITY_BYTES = 32,
    AIVM_WORKER_CATALOG_MAX_ENTRIES = 1024
};

typedef struct {
    uint32_t function_target;
    uint32_t required_capabilities;
    uint32_t transport_abi;
    uint32_t bytecode_version;
    uint8_t identity[AIVM_WORKER_IDENTITY_BYTES];
    uint8_t* artifact;
    size_t artifact_length;
} AivmWorkerCatalogEntry;

typedef struct {
    AivmWorkerCatalogEntry* entries;
    size_t count;
} AivmWorkerCatalog;

typedef enum {
    AIVM_WORKER_CATALOG_OK = 0,
    AIVM_WORKER_CATALOG_ERR_FORMAT = 1,
    AIVM_WORKER_CATALOG_ERR_LIMIT = 2,
    AIVM_WORKER_CATALOG_ERR_MEMORY = 3,
    AIVM_WORKER_CATALOG_ERR_IDENTITY = 4,
    AIVM_WORKER_CATALOG_ERR_ARTIFACT = 5,
    AIVM_WORKER_CATALOG_ERR_FUNCTION = 6,
    AIVM_WORKER_CATALOG_ERR_ABI = 7,
    AIVM_WORKER_CATALOG_ERR_CAPABILITY = 8
} AivmWorkerCatalogStatus;

void aivm_worker_catalog_clear(AivmWorkerCatalog* catalog);
void aivm_worker_catalog_release(AivmWorkerCatalog* catalog);
int aivm_worker_catalog_copy(
    AivmWorkerCatalog* destination,
    const AivmWorkerCatalog* source);
AivmWorkerCatalogStatus aivm_worker_catalog_load(
    const uint8_t* bytes,
    size_t byte_count,
    AivmWorkerCatalog* catalog);
void aivm_worker_identity(
    uint32_t function_target,
    uint32_t required_capabilities,
    uint32_t transport_abi,
    uint32_t bytecode_version,
    const uint8_t* artifact,
    size_t artifact_length,
    uint8_t out_identity[AIVM_WORKER_IDENTITY_BYTES]);

#endif
