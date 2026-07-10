#include <stdio.h>
#include <string.h>

#include "aivm_program.h"

typedef enum {
    PERF_OPCODE_BENCHMARKED = 0,
    PERF_OPCODE_TRACKED_GAP = 1
} PerfOpcodeStatus;

typedef struct {
    AivmOpcode opcode;
    const char* name;
    PerfOpcodeStatus status;
    const char* evidence;
} PerfOpcodeCoverage;

static const PerfOpcodeCoverage g_opcode_coverage[] = {
    { AIVM_OP_NOP, "AIVM_OP_NOP", PERF_OPCODE_BENCHMARKED, "decode/eval/async padding" },
    { AIVM_OP_HALT, "AIVM_OP_HALT", PERF_OPCODE_BENCHMARKED, "all VM perf programs" },
    { AIVM_OP_STUB, "AIVM_OP_STUB", PERF_OPCODE_TRACKED_GAP, "intentional invalid-op sentinel" },
    { AIVM_OP_PUSH_INT, "AIVM_OP_PUSH_INT", PERF_OPCODE_BENCHMARKED, "eval/numeric/worker/golden" },
    { AIVM_OP_POP, "AIVM_OP_POP", PERF_OPCODE_BENCHMARKED, "eval/parallel worker" },
    { AIVM_OP_STORE_LOCAL, "AIVM_OP_STORE_LOCAL", PERF_OPCODE_BENCHMARKED, "locals/loop/parallel worker" },
    { AIVM_OP_LOAD_LOCAL, "AIVM_OP_LOAD_LOCAL", PERF_OPCODE_BENCHMARKED, "locals/loop/parallel worker" },
    { AIVM_OP_ADD_INT, "AIVM_OP_ADD_INT", PERF_OPCODE_BENCHMARKED, "eval/loop/golden" },
    { AIVM_OP_JUMP, "AIVM_OP_JUMP", PERF_OPCODE_BENCHMARKED, "branch/loop" },
    { AIVM_OP_JUMP_IF_FALSE, "AIVM_OP_JUMP_IF_FALSE", PERF_OPCODE_BENCHMARKED, "branch/loop" },
    { AIVM_OP_PUSH_BOOL, "AIVM_OP_PUSH_BOOL", PERF_OPCODE_BENCHMARKED, "branch" },
    { AIVM_OP_CALL, "AIVM_OP_CALL", PERF_OPCODE_BENCHMARKED, "call/recursive" },
    { AIVM_OP_RET, "AIVM_OP_RET", PERF_OPCODE_BENCHMARKED, "async workers" },
    { AIVM_OP_EQ_INT, "AIVM_OP_EQ_INT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add perf row when equality hot path stabilizes" },
    { AIVM_OP_EQ, "AIVM_OP_EQ", PERF_OPCODE_BENCHMARKED, "loop/recursive" },
    { AIVM_OP_CONST, "AIVM_OP_CONST", PERF_OPCODE_BENCHMARKED, "strings/bytes/syscalls" },
    { AIVM_OP_STR_CONCAT, "AIVM_OP_STR_CONCAT", PERF_OPCODE_BENCHMARKED, "string benchmark" },
    { AIVM_OP_TO_STRING, "AIVM_OP_TO_STRING", PERF_OPCODE_TRACKED_GAP, "unit-covered; add format workload benchmark" },
    { AIVM_OP_STR_ESCAPE, "AIVM_OP_STR_ESCAPE", PERF_OPCODE_TRACKED_GAP, "unit-covered; add JSON/string workload benchmark" },
    { AIVM_OP_RETURN, "AIVM_OP_RETURN", PERF_OPCODE_BENCHMARKED, "call/recursive" },
    { AIVM_OP_STR_SUBSTRING, "AIVM_OP_STR_SUBSTRING", PERF_OPCODE_TRACKED_GAP, "unit-covered; add string slicing benchmark" },
    { AIVM_OP_STR_REMOVE, "AIVM_OP_STR_REMOVE", PERF_OPCODE_TRACKED_GAP, "unit-covered; add string editing benchmark" },
    { AIVM_OP_CALL_SYS, "AIVM_OP_CALL_SYS", PERF_OPCODE_BENCHMARKED, "vm_call_sys_console_write" },
    { AIVM_OP_ASYNC_CALL, "AIVM_OP_ASYNC_CALL", PERF_OPCODE_BENCHMARKED, "async worker benchmarks" },
    { AIVM_OP_ASYNC_CALL_SYS, "AIVM_OP_ASYNC_CALL_SYS", PERF_OPCODE_TRACKED_GAP, "unit-covered; add async syscall workload benchmark" },
    { AIVM_OP_AWAIT, "AIVM_OP_AWAIT", PERF_OPCODE_BENCHMARKED, "async worker benchmarks" },
    { AIVM_OP_PAR_BEGIN, "AIVM_OP_PAR_BEGIN", PERF_OPCODE_BENCHMARKED, "par join benchmark" },
    { AIVM_OP_PAR_FORK, "AIVM_OP_PAR_FORK", PERF_OPCODE_BENCHMARKED, "par join benchmark" },
    { AIVM_OP_PAR_JOIN, "AIVM_OP_PAR_JOIN", PERF_OPCODE_BENCHMARKED, "par join benchmark" },
    { AIVM_OP_PAR_CANCEL, "AIVM_OP_PAR_CANCEL", PERF_OPCODE_BENCHMARKED, "par join benchmark" },
    { AIVM_OP_STR_UTF8_BYTE_COUNT, "AIVM_OP_STR_UTF8_BYTE_COUNT", PERF_OPCODE_BENCHMARKED, "string benchmark" },
    { AIVM_OP_STR_SCALAR_LENGTH, "AIVM_OP_STR_SCALAR_LENGTH", PERF_OPCODE_BENCHMARKED, "string benchmark" },
    { AIVM_OP_NODE_KIND, "AIVM_OP_NODE_KIND", PERF_OPCODE_TRACKED_GAP, "unit-covered; add node traversal benchmark" },
    { AIVM_OP_NODE_ID, "AIVM_OP_NODE_ID", PERF_OPCODE_TRACKED_GAP, "unit-covered; add node traversal benchmark" },
    { AIVM_OP_ATTR_COUNT, "AIVM_OP_ATTR_COUNT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add node attr benchmark" },
    { AIVM_OP_ATTR_KEY, "AIVM_OP_ATTR_KEY", PERF_OPCODE_TRACKED_GAP, "unit-covered; add node attr benchmark" },
    { AIVM_OP_ATTR_VALUE_KIND, "AIVM_OP_ATTR_VALUE_KIND", PERF_OPCODE_TRACKED_GAP, "unit-covered; add node attr benchmark" },
    { AIVM_OP_ATTR_VALUE_STRING, "AIVM_OP_ATTR_VALUE_STRING", PERF_OPCODE_TRACKED_GAP, "unit-covered; add node attr benchmark" },
    { AIVM_OP_ATTR_VALUE_INT, "AIVM_OP_ATTR_VALUE_INT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add node attr benchmark" },
    { AIVM_OP_ATTR_VALUE_BOOL, "AIVM_OP_ATTR_VALUE_BOOL", PERF_OPCODE_TRACKED_GAP, "unit-covered; add node attr benchmark" },
    { AIVM_OP_CHILD_COUNT, "AIVM_OP_CHILD_COUNT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add child traversal benchmark" },
    { AIVM_OP_CHILD_AT, "AIVM_OP_CHILD_AT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add child traversal benchmark" },
    { AIVM_OP_MAKE_BLOCK, "AIVM_OP_MAKE_BLOCK", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_APPEND_CHILD, "AIVM_OP_APPEND_CHILD", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_MAKE_ERR, "AIVM_OP_MAKE_ERR", PERF_OPCODE_TRACKED_GAP, "unit-covered; add diagnostic construction benchmark" },
    { AIVM_OP_MAKE_LIT_STRING, "AIVM_OP_MAKE_LIT_STRING", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_MAKE_LIT_INT, "AIVM_OP_MAKE_LIT_INT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_MAKE_LIT_BOOL, "AIVM_OP_MAKE_LIT_BOOL", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_MAKE_NODE, "AIVM_OP_MAKE_NODE", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_MAKE_FIELD_STRING, "AIVM_OP_MAKE_FIELD_STRING", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_MAKE_MAP, "AIVM_OP_MAKE_MAP", PERF_OPCODE_TRACKED_GAP, "unit-covered; add map construction benchmark" },
    { AIVM_OP_MAKE_NODE_EMPTY, "AIVM_OP_MAKE_NODE_EMPTY", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_APPEND_ATTR, "AIVM_OP_APPEND_ATTR", PERF_OPCODE_TRACKED_GAP, "unit-covered; add AST construction benchmark" },
    { AIVM_OP_STR_FIND, "AIVM_OP_STR_FIND", PERF_OPCODE_TRACKED_GAP, "unit-covered; add string search benchmark" },
    { AIVM_OP_STR_FROM_CODEPOINT, "AIVM_OP_STR_FROM_CODEPOINT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add unicode benchmark" },
    { AIVM_OP_STR_DECODE_UNICODE_HEX4, "AIVM_OP_STR_DECODE_UNICODE_HEX4", PERF_OPCODE_TRACKED_GAP, "unit-covered; add unicode benchmark" },
    { AIVM_OP_STR_DECODE_UNICODE_SURROGATE_PAIR_HEX4, "AIVM_OP_STR_DECODE_UNICODE_SURROGATE_PAIR_HEX4", PERF_OPCODE_TRACKED_GAP, "unit-covered; add unicode benchmark" },
    { AIVM_OP_BYTES_LENGTH, "AIVM_OP_BYTES_LENGTH", PERF_OPCODE_BENCHMARKED, "bytes benchmark" },
    { AIVM_OP_BYTES_AT, "AIVM_OP_BYTES_AT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add bytes traversal benchmark" },
    { AIVM_OP_BYTES_SLICE, "AIVM_OP_BYTES_SLICE", PERF_OPCODE_TRACKED_GAP, "unit-covered; add bytes slicing benchmark" },
    { AIVM_OP_BYTES_CONCAT, "AIVM_OP_BYTES_CONCAT", PERF_OPCODE_TRACKED_GAP, "unit-covered; add bytes concat benchmark" },
    { AIVM_OP_BYTES_FROM_UTF8_STRING, "AIVM_OP_BYTES_FROM_UTF8_STRING", PERF_OPCODE_TRACKED_GAP, "unit-covered; add bytes/string conversion benchmark" },
    { AIVM_OP_BYTES_TO_UTF8_STRING, "AIVM_OP_BYTES_TO_UTF8_STRING", PERF_OPCODE_TRACKED_GAP, "unit-covered; add bytes/string conversion benchmark" },
    { AIVM_OP_BYTES_FROM_BASE64, "AIVM_OP_BYTES_FROM_BASE64", PERF_OPCODE_TRACKED_GAP, "unit-covered; add base64 benchmark" },
    { AIVM_OP_BYTES_TO_BASE64, "AIVM_OP_BYTES_TO_BASE64", PERF_OPCODE_TRACKED_GAP, "unit-covered; add base64 benchmark" },
    { AIVM_OP_MAKE_PAIR, "AIVM_OP_MAKE_PAIR", PERF_OPCODE_TRACKED_GAP, "unit-covered; add pair benchmark" },
    { AIVM_OP_PAIR_FIRST, "AIVM_OP_PAIR_FIRST", PERF_OPCODE_TRACKED_GAP, "unit-covered; add pair benchmark" },
    { AIVM_OP_PAIR_SECOND, "AIVM_OP_PAIR_SECOND", PERF_OPCODE_TRACKED_GAP, "unit-covered; add pair benchmark" },
    { AIVM_OP_SUB_NUM, "AIVM_OP_SUB_NUM", PERF_OPCODE_BENCHMARKED, "numeric benchmark" },
    { AIVM_OP_MUL_NUM, "AIVM_OP_MUL_NUM", PERF_OPCODE_BENCHMARKED, "numeric benchmark" },
    { AIVM_OP_DIV_NUM, "AIVM_OP_DIV_NUM", PERF_OPCODE_BENCHMARKED, "numeric benchmark" },
    { AIVM_OP_MOD_NUM, "AIVM_OP_MOD_NUM", PERF_OPCODE_BENCHMARKED, "numeric benchmark" },
    { AIVM_OP_POW_NUM, "AIVM_OP_POW_NUM", PERF_OPCODE_TRACKED_GAP, "unit-covered; add numeric benchmark" },
    { AIVM_OP_LT_NUM, "AIVM_OP_LT_NUM", PERF_OPCODE_BENCHMARKED, "numeric benchmark" },
    { AIVM_OP_BYTES_FROM_BYTE, "AIVM_OP_BYTES_FROM_BYTE", PERF_OPCODE_TRACKED_GAP, "stdlib-covered; add byte construction benchmark" },
    { AIVM_OP_BYTES_U32_LE, "AIVM_OP_BYTES_U32_LE", PERF_OPCODE_TRACKED_GAP, "stdlib-covered; add byte construction benchmark" },
    { AIVM_OP_BYTES_I64_LE, "AIVM_OP_BYTES_I64_LE", PERF_OPCODE_TRACKED_GAP, "stdlib-covered; add byte construction benchmark" }
};

int main(void)
{
    int seen[AIVM_OP_MAX + 1U];
    size_t benchmarked = 0U;
    size_t tracked_gap = 0U;
    size_t index;
    int failed = 0;

    memset(seen, 0, sizeof(seen));

    for (index = 0U; index < sizeof(g_opcode_coverage) / sizeof(g_opcode_coverage[0]); index += 1U) {
        const PerfOpcodeCoverage* row = &g_opcode_coverage[index];
        if ((int)row->opcode < 0 || row->opcode > AIVM_OP_MAX) {
            (void)fprintf(stderr, "opcode coverage row is out of range: %s\n", row->name);
            failed = 1;
            continue;
        }
        if (seen[row->opcode] != 0) {
            (void)fprintf(stderr, "opcode coverage row is duplicated: %s\n", row->name);
            failed = 1;
        }
        seen[row->opcode] = 1;
        if (row->status == PERF_OPCODE_BENCHMARKED) {
            benchmarked += 1U;
        } else {
            tracked_gap += 1U;
        }
    }

    for (index = 0U; index <= (size_t)AIVM_OP_MAX; index += 1U) {
        if (seen[index] == 0) {
            (void)fprintf(stderr, "opcode coverage missing opcode=%zu\n", index);
            failed = 1;
        }
    }

    (void)printf(
        "AiVM opcode perf coverage: benchmarked=%zu tracked_gaps=%zu total=%zu\n",
        benchmarked,
        tracked_gap,
        benchmarked + tracked_gap);

    return failed;
}
