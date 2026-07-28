#ifndef AIVM_PROGRAM_H
#define AIVM_PROGRAM_H

#include <stddef.h>
#include <stdint.h>

#include "aivm_types.h"
#include "aivm_worker_catalog.h"

typedef enum {
    AIVM_OP_NOP = 0,
    AIVM_OP_HALT = 1,
    AIVM_OP_STUB = 2,
    AIVM_OP_PUSH_INT = 3,
    AIVM_OP_POP = 4,
    AIVM_OP_STORE_LOCAL = 5,
    AIVM_OP_LOAD_LOCAL = 6,
    AIVM_OP_ADD_INT = 7,
    AIVM_OP_JUMP = 8,
    AIVM_OP_JUMP_IF_FALSE = 9,
    AIVM_OP_PUSH_BOOL = 10,
    AIVM_OP_CALL = 11,
    AIVM_OP_RET = 12,
    AIVM_OP_EQ_INT = 13,
    AIVM_OP_EQ = 14,
    AIVM_OP_CONST = 15,
    AIVM_OP_STR_CONCAT = 16,
    AIVM_OP_TO_STRING = 17,
    AIVM_OP_STR_ESCAPE = 18,
    AIVM_OP_RETURN = 19,
    AIVM_OP_STR_SUBSTRING = 20,
    AIVM_OP_STR_REMOVE = 21,
    AIVM_OP_CALL_SYS = 22,
    AIVM_OP_ASYNC_CALL = 23,
    AIVM_OP_ASYNC_CALL_SYS = 24,
    AIVM_OP_AWAIT = 25,
    AIVM_OP_PAR_BEGIN = 26,
    AIVM_OP_PAR_FORK = 27,
    AIVM_OP_PAR_JOIN = 28,
    AIVM_OP_PAR_CANCEL = 29,
    AIVM_OP_STR_UTF8_BYTE_COUNT = 30,
    AIVM_OP_NODE_KIND = 31,
    AIVM_OP_NODE_ID = 32,
    AIVM_OP_ATTR_COUNT = 33,
    AIVM_OP_ATTR_KEY = 34,
    AIVM_OP_ATTR_VALUE_KIND = 35,
    AIVM_OP_ATTR_VALUE_STRING = 36,
    AIVM_OP_ATTR_VALUE_INT = 37,
    AIVM_OP_ATTR_VALUE_BOOL = 38,
    AIVM_OP_CHILD_COUNT = 39,
    AIVM_OP_CHILD_AT = 40,
    AIVM_OP_MAKE_BLOCK = 41,
    AIVM_OP_APPEND_CHILD = 42,
    AIVM_OP_MAKE_ERR = 43,
    AIVM_OP_MAKE_LIT_STRING = 44,
    AIVM_OP_MAKE_LIT_INT = 45,
    AIVM_OP_MAKE_LIT_BOOL = 46,
    AIVM_OP_MAKE_NODE = 47,
    AIVM_OP_MAKE_FIELD_STRING = 48,
    AIVM_OP_MAKE_MAP = 49,
    AIVM_OP_MAKE_NODE_EMPTY = 50,
    AIVM_OP_APPEND_ATTR = 51,
    AIVM_OP_STR_FIND = 52,
    AIVM_OP_STR_FROM_CODEPOINT = 53,
    AIVM_OP_STR_DECODE_UNICODE_HEX4 = 54,
    AIVM_OP_STR_DECODE_UNICODE_SURROGATE_PAIR_HEX4 = 55,
    AIVM_OP_BYTES_LENGTH = 56,
    AIVM_OP_BYTES_AT = 57,
    AIVM_OP_BYTES_SLICE = 58,
    AIVM_OP_BYTES_CONCAT = 59,
    AIVM_OP_BYTES_FROM_UTF8_STRING = 60,
    AIVM_OP_BYTES_TO_UTF8_STRING = 61,
    AIVM_OP_BYTES_FROM_BASE64 = 62,
    AIVM_OP_BYTES_TO_BASE64 = 63,
    AIVM_OP_MAKE_PAIR = 64,
    AIVM_OP_PAIR_FIRST = 65,
    AIVM_OP_PAIR_SECOND = 66,
    AIVM_OP_SUB_NUM = 67,
    AIVM_OP_MUL_NUM = 68,
    AIVM_OP_DIV_NUM = 69,
    AIVM_OP_MOD_NUM = 70,
    AIVM_OP_POW_NUM = 71,
    AIVM_OP_LT_NUM = 72,
    AIVM_OP_BYTES_FROM_BYTE = 73,
    AIVM_OP_BYTES_U32_LE = 74,
    AIVM_OP_BYTES_I64_LE = 75,
    AIVM_OP_STR_SCALAR_LENGTH = 76,
    AIVM_OP_VALUE_KIND = 77,
    AIVM_OP_NODE_BUILDER_NEW = 78,
    AIVM_OP_NODE_BUILDER_APPEND_CHILD = 79,
    AIVM_OP_NODE_BUILDER_APPEND_ATTR = 80,
    AIVM_OP_NODE_BUILDER_FINISH = 81,
    AIVM_OP_MAP_BUILDER_NEW = 82,
    AIVM_OP_MAP_BUILDER_PUT_STRING_INT = 83,
    AIVM_OP_MAP_BUILDER_FINISH = 84,
    AIVM_OP_MAP_COUNT = 85,
    AIVM_OP_MAP_HAS_STRING = 86,
    AIVM_OP_MAP_GET_STRING_INT_OR = 87,
    AIVM_OP_WORKER_REF = 88,
    AIVM_OP_WORKER_RUN = 89,
    AIVM_OP_TASK_CANCEL = 90,
    AIVM_OP_WORKER_RUN_ALL = 91,
    AIVM_OP_WORKER_TASK_AT = 92,
    AIVM_OP_MAX = AIVM_OP_WORKER_TASK_AT
} AivmOpcode;

typedef struct {
    AivmOpcode opcode;
    int64_t operand_int;
} AivmInstruction;

typedef struct {
    uint32_t section_type;
    uint32_t section_size;
    uint32_t section_offset;
} AivmProgramSection;

enum {
    AIVM_PROGRAM_MAX_SECTIONS = 32,
    AIVM_PROGRAM_INLINE_INSTRUCTIONS = 32768,
    AIVM_PROGRAM_MAX_INSTRUCTIONS = AIVM_PROGRAM_INLINE_INSTRUCTIONS,
    AIVM_PROGRAM_INLINE_CONSTANTS = 1024,
    AIVM_PROGRAM_MAX_STRING_BYTES = 65536,
    AIVM_PROGRAM_MAX_BYTES_STORAGE = 32768,
    AIVM_PROGRAM_SECTION_INSTRUCTIONS = 1,
    AIVM_PROGRAM_SECTION_CONSTANTS = 2,
    AIVM_PROGRAM_SECTION_WORKER_CATALOG = 3
};

typedef struct {
    const AivmInstruction* instructions;
    size_t instruction_count;
    const AivmValue* constants;
    size_t constant_count;
    uint32_t format_version;
    uint32_t format_flags;
    uint32_t section_count;
    AivmProgramSection sections[AIVM_PROGRAM_MAX_SECTIONS];
    AivmInstruction instruction_storage[AIVM_PROGRAM_MAX_INSTRUCTIONS];
    AivmInstruction* allocated_instruction_storage;
    size_t instruction_capacity;
    AivmValue constant_storage[AIVM_PROGRAM_INLINE_CONSTANTS];
    AivmValue* allocated_constant_storage;
    size_t constant_capacity;
    char string_storage[AIVM_PROGRAM_MAX_STRING_BYTES];
    size_t string_storage_used;
    uint8_t bytes_storage[AIVM_PROGRAM_MAX_BYTES_STORAGE];
    size_t bytes_storage_used;
    AivmWorkerCatalog worker_catalog;
} AivmProgram;

typedef enum {
    AIVM_PROGRAM_OK = 0,
    AIVM_PROGRAM_ERR_NULL = 1,
    AIVM_PROGRAM_ERR_TRUNCATED = 2,
    AIVM_PROGRAM_ERR_BAD_MAGIC = 3,
    AIVM_PROGRAM_ERR_UNSUPPORTED = 4,
    AIVM_PROGRAM_ERR_SECTION_OOB = 5,
    AIVM_PROGRAM_ERR_SECTION_LIMIT = 6,
    AIVM_PROGRAM_ERR_INSTRUCTION_LIMIT = 7,
    AIVM_PROGRAM_ERR_INVALID_SECTION = 8,
    AIVM_PROGRAM_ERR_INVALID_OPCODE = 9,
    AIVM_PROGRAM_ERR_CONSTANT_LIMIT = 10,
    AIVM_PROGRAM_ERR_INVALID_CONSTANT = 11,
    AIVM_PROGRAM_ERR_STRING_LIMIT = 12,
    AIVM_PROGRAM_ERR_MEMORY = 13,
    AIVM_PROGRAM_ERR_WORKER_CATALOG = 14
} AivmProgramStatus;

typedef struct {
    AivmProgramStatus status;
    size_t error_offset;
} AivmProgramLoadResult;

void aivm_program_clear(AivmProgram* program);
void aivm_program_release(AivmProgram* program);
void aivm_program_init(AivmProgram* program, const AivmInstruction* instructions, size_t instruction_count);
AivmProgramLoadResult aivm_program_load_aibc1(const uint8_t* bytes, size_t byte_count, AivmProgram* out_program);
const char* aivm_program_status_code(AivmProgramStatus status);
const char* aivm_program_status_message(AivmProgramStatus status);

#endif
