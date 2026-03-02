#ifndef AIVM_PROGRAM_H
#define AIVM_PROGRAM_H

#include <stddef.h>
#include <stdint.h>

#include "aivm_types.h"

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
    AIVM_OP_MAKE_NODE = 46
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
    AIVM_PROGRAM_MAX_INSTRUCTIONS = 4096,
    AIVM_PROGRAM_MAX_CONSTANTS = 1024,
    AIVM_PROGRAM_MAX_STRING_BYTES = 8192,
    AIVM_PROGRAM_SECTION_INSTRUCTIONS = 1,
    AIVM_PROGRAM_SECTION_CONSTANTS = 2
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
    AivmValue constant_storage[AIVM_PROGRAM_MAX_CONSTANTS];
    char string_storage[AIVM_PROGRAM_MAX_STRING_BYTES];
    size_t string_storage_used;
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
    AIVM_PROGRAM_ERR_STRING_LIMIT = 12
} AivmProgramStatus;

typedef struct {
    AivmProgramStatus status;
    size_t error_offset;
} AivmProgramLoadResult;

void aivm_program_clear(AivmProgram* program);
void aivm_program_init(AivmProgram* program, const AivmInstruction* instructions, size_t instruction_count);
AivmProgramLoadResult aivm_program_load_aibc1(const uint8_t* bytes, size_t byte_count, AivmProgram* out_program);
const char* aivm_program_status_code(AivmProgramStatus status);
const char* aivm_program_status_message(AivmProgramStatus status);

#endif
