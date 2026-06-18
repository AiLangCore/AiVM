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

    if (strcmp(argv[1], "memory-shared-module-cache") == 0) {
        return fail_pending(
            argv[1],
            "AiVM does not yet expose a module/cache loader API for immutable shared module cache verification.");
    }

    if (strcmp(argv[1], "memory-large-object-storage") == 0) {
        return fail_pending(
            argv[1],
            "AiVM does not yet expose handle-based large object/blob storage with deterministic limits.");
    }

    if (strcmp(argv[1], "debug-full-debugger") == 0) {
        return fail_pending(
            argv[1],
            "aivm-debug does not yet implement the full debugger protocol command surface.");
    }

    (void)fprintf(stderr, "unknown spec gap id: %s\n", argv[1]);
    return 1;
}
