#include <stdio.h>
#include <string.h>

static int fail_pending(const char* id, const char* reason)
{
    (void)fprintf(stderr, "spec pending: %s\n", id);
    (void)fprintf(stderr, "reason: %s\n", reason);
    return 1;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        (void)fprintf(stderr, "usage: %s <spec-gap-id>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "debug-advanced-debugger") == 0) {
        return fail_pending(
            argv[1],
            "aivm-debug does not yet implement function/node breakpoints, step-over/out, heap inspection, or host-operation inspection.");
    }

    (void)fprintf(stderr, "unknown spec gap id: %s\n", argv[1]);
    return 1;
}
