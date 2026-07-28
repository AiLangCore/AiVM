#include "aivm_program.h"
#include "aivm_program_constants.h"
#include "aivm_program_instructions.h"

#include <string.h>

static int size_add_checked(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return 0;
    }
    if (a > ((size_t)-1) - b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int size_mul_checked(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return 0;
    }
    if (a != 0U && b > ((size_t)-1) / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static uint32_t read_u32_le(const uint8_t* bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1U] << 8U) |
           ((uint32_t)bytes[offset + 2U] << 16U) |
           ((uint32_t)bytes[offset + 3U] << 24U);
}

static int64_t read_i64_le(const uint8_t* bytes, size_t offset)
{
    uint64_t low = (uint64_t)read_u32_le(bytes, offset);
    uint64_t high = (uint64_t)read_u32_le(bytes, offset + 4U);
    uint64_t combined = low | (high << 32U);
    return (int64_t)combined;
}

static double read_f64_le(const uint8_t* bytes, size_t offset)
{
    uint64_t low = (uint64_t)read_u32_le(bytes, offset);
    uint64_t high = (uint64_t)read_u32_le(bytes, offset + 4U);
    uint64_t combined = low | (high << 32U);
    double value = 0.0;
    memcpy(&value, &combined, sizeof(value));
    return value;
}

static AivmProgramLoadResult constant_load_error(
    AivmProgram* program,
    AivmProgramStatus status,
    size_t offset)
{
    AivmProgramLoadResult result;
    aivm_program_release(program);
    result.status = status;
    result.error_offset = offset;
    return result;
}

static int write_string_constant(
    AivmProgram* program,
    uint32_t constant_index,
    const uint8_t* bytes,
    size_t length)
{
    size_t base_offset;
    size_t needed_storage = 0U;
    size_t i;

    if (program == NULL || bytes == NULL) {
        return 0;
    }
    base_offset = program->string_storage_used;
    if (!size_add_checked(base_offset, length, &needed_storage) ||
        !size_add_checked(needed_storage, 1U, &needed_storage) ||
        needed_storage > AIVM_PROGRAM_MAX_STRING_BYTES) {
        return 0;
    }

    for (i = 0U; i < length; i += 1U) {
        program->string_storage[base_offset + i] = (char)bytes[i];
    }
    program->string_storage[base_offset + length] = '\0';
    program->string_storage_used = needed_storage;
    aivm_program_constants_mutable(program)[constant_index] =
        aivm_value_string(&program->string_storage[base_offset]);
    return 1;
}

static int write_bytes_constant(
    AivmProgram* program,
    uint32_t constant_index,
    const uint8_t* bytes,
    size_t length)
{
    size_t base_offset;
    size_t needed_storage = 0U;
    size_t i;

    if (program == NULL || (length > 0U && bytes == NULL)) {
        return 0;
    }
    base_offset = program->bytes_storage_used;
    if (!size_add_checked(base_offset, length, &needed_storage) ||
        needed_storage > AIVM_PROGRAM_MAX_BYTES_STORAGE) {
        return 0;
    }
    for (i = 0U; i < length; i += 1U) {
        program->bytes_storage[base_offset + i] = bytes[i];
    }
    program->bytes_storage_used = needed_storage;
    aivm_program_constants_mutable(program)[constant_index] =
        aivm_value_bytes(&program->bytes_storage[base_offset], length);
    return 1;
}

void aivm_program_clear(AivmProgram* program)
{
    size_t index;
    if (program == NULL) {
        return;
    }

    program->instructions = NULL;
    program->instruction_count = 0U;
    program->allocated_instruction_storage = NULL;
    program->instruction_capacity = AIVM_PROGRAM_INLINE_INSTRUCTIONS;
    program->constants = NULL;
    program->constant_count = 0U;
    program->allocated_constant_storage = NULL;
    program->constant_capacity = AIVM_PROGRAM_INLINE_CONSTANTS;
    program->format_version = 0U;
    program->format_flags = 0U;
    program->section_count = 0U;
    program->string_storage_used = 0U;
    program->bytes_storage_used = 0U;
    aivm_worker_catalog_clear(&program->worker_catalog);
    for (index = 0U; index < AIVM_PROGRAM_MAX_INSTRUCTIONS; index += 1U) {
        program->instruction_storage[index].opcode = AIVM_OP_NOP;
        program->instruction_storage[index].operand_int = 0;
    }
    for (index = 0U; index < AIVM_PROGRAM_INLINE_CONSTANTS; index += 1U) {
        program->constant_storage[index] = aivm_value_void();
    }
    for (index = 0U; index < AIVM_PROGRAM_MAX_STRING_BYTES; index += 1U) {
        program->string_storage[index] = '\0';
    }
    for (index = 0U; index < AIVM_PROGRAM_MAX_BYTES_STORAGE; index += 1U) {
        program->bytes_storage[index] = 0U;
    }
    for (index = 0U; index < AIVM_PROGRAM_MAX_SECTIONS; index += 1U) {
        program->sections[index].section_type = 0U;
        program->sections[index].section_size = 0U;
        program->sections[index].section_offset = 0U;
    }
}

void aivm_program_release(AivmProgram* program)
{
    if (program == NULL) {
        return;
    }
    aivm_program_constants_release(program);
    aivm_program_instructions_release(program);
    aivm_worker_catalog_release(&program->worker_catalog);
}

void aivm_program_init(AivmProgram* program, const AivmInstruction* instructions, size_t instruction_count)
{
    if (program == NULL) {
        return;
    }

    aivm_program_clear(program);
    program->instructions = instructions;
    program->instruction_count = instruction_count;
}

AivmProgramLoadResult aivm_program_load_aibc1(const uint8_t* bytes, size_t byte_count, AivmProgram* out_program)
{
    AivmProgramLoadResult result;
    size_t cursor;
    uint32_t section_index;
    int has_instruction_section = 0;
    int has_constants_section = 0;
    int has_worker_catalog_section = 0;

    if (out_program != NULL) {
        aivm_program_clear(out_program);
    }

    if (bytes == NULL || out_program == NULL) {
        result.status = AIVM_PROGRAM_ERR_NULL;
        result.error_offset = 0U;
        return result;
    }

    if (byte_count < 16U) {
        result.status = AIVM_PROGRAM_ERR_TRUNCATED;
        result.error_offset = byte_count;
        return result;
    }

    if (bytes[0] != (uint8_t)'A' ||
        bytes[1] != (uint8_t)'I' ||
        bytes[2] != (uint8_t)'B' ||
        bytes[3] != (uint8_t)'C') {
        result.status = AIVM_PROGRAM_ERR_BAD_MAGIC;
        result.error_offset = 0U;
        return result;
    }

    out_program->format_version = read_u32_le(bytes, 4U);
    out_program->format_flags = read_u32_le(bytes, 8U);
    out_program->section_count = read_u32_le(bytes, 12U);

    if (out_program->format_version != 2U) {
        result.status = AIVM_PROGRAM_ERR_UNSUPPORTED;
        result.error_offset = 4U;
        return result;
    }

    if (out_program->section_count > AIVM_PROGRAM_MAX_SECTIONS) {
        result.status = AIVM_PROGRAM_ERR_SECTION_LIMIT;
        result.error_offset = 12U;
        return result;
    }

    cursor = 16U;
    for (section_index = 0U; section_index < out_program->section_count; section_index += 1U) {
        uint32_t section_type;
        uint32_t section_size;
        size_t section_payload_start;
        size_t section_end;

        if (!size_add_checked(cursor, 8U, &section_end) || section_end > byte_count) {
            result.status = AIVM_PROGRAM_ERR_TRUNCATED;
            result.error_offset = cursor;
            return result;
        }

        section_type = read_u32_le(bytes, cursor);
        section_size = read_u32_le(bytes, cursor + 4U);
        cursor += 8U;
        section_payload_start = cursor;

        if (!size_add_checked(cursor, (size_t)section_size, &section_end) || section_end > byte_count) {
            result.status = AIVM_PROGRAM_ERR_SECTION_OOB;
            result.error_offset = cursor;
            return result;
        }

        out_program->sections[section_index].section_type = section_type;
        out_program->sections[section_index].section_size = section_size;
        out_program->sections[section_index].section_offset = (uint32_t)cursor;

        if (section_type == AIVM_PROGRAM_SECTION_INSTRUCTIONS) {
            uint32_t instruction_count;
            uint32_t instruction_index;
            size_t instruction_cursor;
            size_t expected_instruction_bytes;
            size_t expected_section_size;

            if (has_instruction_section != 0) {
                result.status = AIVM_PROGRAM_ERR_INVALID_SECTION;
                result.error_offset = section_payload_start;
                return result;
            }
            has_instruction_section = 1;

            if (section_size < 4U) {
                result.status = AIVM_PROGRAM_ERR_INVALID_SECTION;
                result.error_offset = section_payload_start;
                return result;
            }

            instruction_count = read_u32_le(bytes, section_payload_start);
            if (!size_mul_checked((size_t)instruction_count, 12U, &expected_instruction_bytes) ||
                !size_add_checked(4U, expected_instruction_bytes, &expected_section_size) ||
                (size_t)section_size != expected_section_size) {
                result.status = AIVM_PROGRAM_ERR_INVALID_SECTION;
                result.error_offset = section_payload_start;
                return result;
            }
            if (!aivm_program_instructions_reserve(
                    out_program, (size_t)instruction_count)) {
                return constant_load_error(
                    out_program, AIVM_PROGRAM_ERR_MEMORY, section_payload_start);
            }

            instruction_cursor = section_payload_start + 4U;
            for (instruction_index = 0U; instruction_index < instruction_count; instruction_index += 1U) {
                uint32_t raw_opcode = read_u32_le(bytes, instruction_cursor);
                int64_t operand_int = read_i64_le(bytes, instruction_cursor + 4U);
                if (raw_opcode > (uint32_t)AIVM_OP_MAX) {
                    result.status = AIVM_PROGRAM_ERR_INVALID_OPCODE;
                    result.error_offset = instruction_cursor;
                    return result;
                }

                aivm_program_instructions_mutable(out_program)[instruction_index].opcode =
                    (AivmOpcode)raw_opcode;
                aivm_program_instructions_mutable(out_program)[instruction_index].operand_int =
                    operand_int;
                instruction_cursor += 12U;
            }

            out_program->instruction_count = (size_t)instruction_count;
        } else if (section_type == AIVM_PROGRAM_SECTION_CONSTANTS) {
            uint32_t constant_count;
            uint32_t constant_index;
            size_t constant_cursor;

            if (has_constants_section != 0) {
                result.status = AIVM_PROGRAM_ERR_INVALID_SECTION;
                result.error_offset = section_payload_start;
                return result;
            }
            has_constants_section = 1;

            if (section_size < 4U) {
                result.status = AIVM_PROGRAM_ERR_INVALID_SECTION;
                result.error_offset = section_payload_start;
                return result;
            }

            constant_count = read_u32_le(bytes, section_payload_start);
            if (!aivm_program_constants_reserve(out_program, (size_t)constant_count)) {
                result.status = AIVM_PROGRAM_ERR_MEMORY;
                result.error_offset = section_payload_start;
                return result;
            }

            constant_cursor = section_payload_start + 4U;
            for (constant_index = 0U; constant_index < constant_count; constant_index += 1U) {
                uint8_t kind;
                if (constant_cursor >= section_end) {
                    return constant_load_error(
                        out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
                }

                kind = bytes[constant_cursor];
                constant_cursor += 1U;

                if (kind == 1U) {
                    size_t next_cursor;
                    if (!size_add_checked(constant_cursor, 8U, &next_cursor) || next_cursor > section_end) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
                    }
                    aivm_program_constants_mutable(out_program)[constant_index] =
                        aivm_value_int(read_i64_le(bytes, constant_cursor));
                    constant_cursor += 8U;
                } else if (kind == 7U) {
                    size_t next_cursor;
                    if (!size_add_checked(constant_cursor, 8U, &next_cursor) || next_cursor > section_end) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
                    }
                    aivm_program_constants_mutable(out_program)[constant_index] =
                        aivm_value_number(read_f64_le(bytes, constant_cursor));
                    constant_cursor += 8U;
                } else if (kind == 2U) {
                    size_t next_cursor;
                    if (!size_add_checked(constant_cursor, 1U, &next_cursor) || next_cursor > section_end) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
                    }
                    aivm_program_constants_mutable(out_program)[constant_index] =
                        aivm_value_bool(bytes[constant_cursor] != 0U ? 1 : 0);
                    constant_cursor += 1U;
                } else if (kind == 6U) {
                    aivm_program_constants_mutable(out_program)[constant_index] = aivm_value_null();
                } else if (kind == 3U) {
                    uint32_t string_length;
                    size_t next_cursor;
                    size_t needed_string_storage;
                    if (!size_add_checked(constant_cursor, 4U, &next_cursor) || next_cursor > section_end) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
                    }
                    string_length = read_u32_le(bytes, constant_cursor);
                    constant_cursor += 4U;

                    if (!size_add_checked(constant_cursor, (size_t)string_length, &next_cursor) ||
                        next_cursor > section_end) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
                    }
                    if (!size_add_checked(out_program->string_storage_used, (size_t)string_length, &needed_string_storage) ||
                        !size_add_checked(needed_string_storage, 1U, &needed_string_storage) ||
                        needed_string_storage > AIVM_PROGRAM_MAX_STRING_BYTES) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_STRING_LIMIT, constant_cursor);
                    }

                    if (!write_string_constant(
                            out_program,
                            constant_index,
                            &bytes[constant_cursor],
                            (size_t)string_length)) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_STRING_LIMIT, constant_cursor);
                    }
                    constant_cursor += (size_t)string_length;
                } else if (kind == 4U) {
                    aivm_program_constants_mutable(out_program)[constant_index] = aivm_value_void();
                } else if (kind == 5U) {
                    uint32_t bytes_length;
                    size_t next_cursor;
                    size_t needed_bytes_storage;
                    if (!size_add_checked(constant_cursor, 4U, &next_cursor) || next_cursor > section_end) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
                    }
                    bytes_length = read_u32_le(bytes, constant_cursor);
                    constant_cursor += 4U;
                    if (!size_add_checked(constant_cursor, (size_t)bytes_length, &next_cursor) ||
                        next_cursor > section_end) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
                    }
                    if (!size_add_checked(out_program->bytes_storage_used, (size_t)bytes_length, &needed_bytes_storage) ||
                        needed_bytes_storage > AIVM_PROGRAM_MAX_BYTES_STORAGE) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_STRING_LIMIT, constant_cursor);
                    }
                    if (!write_bytes_constant(
                            out_program,
                            constant_index,
                            &bytes[constant_cursor],
                            (size_t)bytes_length)) {
                        return constant_load_error(
                            out_program, AIVM_PROGRAM_ERR_STRING_LIMIT, constant_cursor);
                    }
                    constant_cursor += (size_t)bytes_length;
                } else {
                    return constant_load_error(
                        out_program, AIVM_PROGRAM_ERR_INVALID_CONSTANT, constant_cursor - 1U);
                }
            }

            if (constant_cursor != section_end) {
                return constant_load_error(
                    out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, constant_cursor);
            }

            out_program->constant_count = (size_t)constant_count;
        } else if (section_type == AIVM_PROGRAM_SECTION_WORKER_CATALOG) {
            AivmWorkerCatalogStatus catalog_status;
            if (has_worker_catalog_section != 0) {
                return constant_load_error(
                    out_program, AIVM_PROGRAM_ERR_INVALID_SECTION, section_payload_start);
            }
            has_worker_catalog_section = 1;
            catalog_status = aivm_worker_catalog_load(
                bytes + section_payload_start,
                (size_t)section_size,
                &out_program->worker_catalog);
            if (catalog_status != AIVM_WORKER_CATALOG_OK) {
                return constant_load_error(
                    out_program, AIVM_PROGRAM_ERR_WORKER_CATALOG, section_payload_start);
            }
        }

        cursor = section_end;
    }

    result.status = AIVM_PROGRAM_OK;
    result.error_offset = 0U;
    return result;
}

const char* aivm_program_status_code(AivmProgramStatus status)
{
    switch (status) {
        case AIVM_PROGRAM_OK:
            return "AIVMP000";
        case AIVM_PROGRAM_ERR_NULL:
            return "AIVMP001";
        case AIVM_PROGRAM_ERR_TRUNCATED:
            return "AIVMP002";
        case AIVM_PROGRAM_ERR_BAD_MAGIC:
            return "AIVMP003";
        case AIVM_PROGRAM_ERR_UNSUPPORTED:
            return "AIVMP004";
        case AIVM_PROGRAM_ERR_SECTION_OOB:
            return "AIVMP005";
        case AIVM_PROGRAM_ERR_SECTION_LIMIT:
            return "AIVMP006";
        case AIVM_PROGRAM_ERR_INSTRUCTION_LIMIT:
            return "AIVMP007";
        case AIVM_PROGRAM_ERR_INVALID_SECTION:
            return "AIVMP008";
        case AIVM_PROGRAM_ERR_INVALID_OPCODE:
            return "AIVMP009";
        case AIVM_PROGRAM_ERR_CONSTANT_LIMIT:
            return "AIVMP010";
        case AIVM_PROGRAM_ERR_INVALID_CONSTANT:
            return "AIVMP011";
        case AIVM_PROGRAM_ERR_STRING_LIMIT:
            return "AIVMP012";
        case AIVM_PROGRAM_ERR_MEMORY:
            return "AIVMP013";
        case AIVM_PROGRAM_ERR_WORKER_CATALOG:
            return "AIVMP014";
        default:
            return "AIVMP999";
    }
}

const char* aivm_program_status_message(AivmProgramStatus status)
{
    switch (status) {
        case AIVM_PROGRAM_OK:
            return "Program load completed.";
        case AIVM_PROGRAM_ERR_NULL:
            return "Program load input was null.";
        case AIVM_PROGRAM_ERR_TRUNCATED:
            return "Program bytes were truncated.";
        case AIVM_PROGRAM_ERR_BAD_MAGIC:
            return "Program magic was invalid.";
        case AIVM_PROGRAM_ERR_UNSUPPORTED:
            return "Program version or feature is unsupported.";
        case AIVM_PROGRAM_ERR_SECTION_OOB:
            return "Program section exceeded byte bounds.";
        case AIVM_PROGRAM_ERR_SECTION_LIMIT:
            return "Program section count exceeded limit.";
        case AIVM_PROGRAM_ERR_INSTRUCTION_LIMIT:
            return "Program instruction count exceeded limit.";
        case AIVM_PROGRAM_ERR_INVALID_SECTION:
            return "Program section encoding was invalid.";
        case AIVM_PROGRAM_ERR_INVALID_OPCODE:
            return "Program instruction opcode was invalid.";
        case AIVM_PROGRAM_ERR_CONSTANT_LIMIT:
            return "Program constant count exceeded limit.";
        case AIVM_PROGRAM_ERR_INVALID_CONSTANT:
            return "Program constant encoding was invalid.";
        case AIVM_PROGRAM_ERR_STRING_LIMIT:
            return "Program string storage exceeded limit.";
        case AIVM_PROGRAM_ERR_MEMORY:
            return "Program storage allocation failed.";
        case AIVM_PROGRAM_ERR_WORKER_CATALOG:
            return "Program worker catalog was invalid.";
        default:
            return "Unknown program load status.";
    }
}
