#define AIVM_DEBUG_RUNTIME 1
#define main aivm_cli_main_for_test
#include "../../examples/aivm_cli.c"
#undef main

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d\n", __LINE__); \
            remove(".tmp/aivm-debug-budget-test/small.txt"); \
            remove(".tmp/aivm-debug-budget-test/large.txt"); \
            remove(".tmp/aivm-debug-budget-test/large.txt.tmp"); \
            rmdir(".tmp/aivm-debug-budget-test"); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    DebugArtifactBudget budget;
    DebugArtifactFile artifact;
    FILE* file;

    (void)ensure_directory(".tmp");
    (void)ensure_directory(".tmp/aivm-debug-budget-test");
    remove(".tmp/aivm-debug-budget-test/small.txt");
    remove(".tmp/aivm-debug-budget-test/large.txt");
    remove(".tmp/aivm-debug-budget-test/large.txt.tmp");

    debug_artifact_budget_init(&budget, 10U);
    file = debug_artifact_open(&budget, ".tmp/aivm-debug-budget-test", "small.txt", &artifact);
    CHECK(file != NULL);
    fprintf(file, "12345");
    debug_artifact_close(&artifact);
    CHECK(budget.exceeded == 0);
    CHECK(budget.used_bytes == 5U);
    CHECK(debug_artifact_file_size(".tmp/aivm-debug-budget-test/small.txt") == 5U);

    file = debug_artifact_open(&budget, ".tmp/aivm-debug-budget-test", "large.txt", &artifact);
    CHECK(file != NULL);
    fprintf(file, "123456");
    debug_artifact_close(&artifact);
    CHECK(budget.exceeded == 1);
    CHECK(budget.used_bytes == 5U);
    CHECK(debug_artifact_file_size(".tmp/aivm-debug-budget-test/large.txt") == 0U);
    CHECK(debug_artifact_file_size(".tmp/aivm-debug-budget-test/large.txt.tmp") == 0U);

    remove(".tmp/aivm-debug-budget-test/small.txt");
    rmdir(".tmp/aivm-debug-budget-test");
    return 0;
}
