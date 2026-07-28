#include "../../../src/ailang_cli/airun_storage_path.h"

#include <stdio.h>
#include <string.h>

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    char segment[32];
    if (expect(native_storage_sanitize_segment("app.one", segment,
            sizeof(segment)) == 1) != 0 ||
        expect(strcmp(segment, "app.one") == 0) != 0 ||
        expect(native_storage_sanitize_segment(".", segment,
            sizeof(segment)) == 0) != 0 ||
        expect(native_storage_sanitize_segment("..", segment,
            sizeof(segment)) == 0) != 0 ||
        expect(native_storage_sanitize_segment("../app", segment,
            sizeof(segment)) == 1) != 0 ||
        expect(strcmp(segment, ".._app") == 0) != 0) {
        return 1;
    }
    (void)printf("storage path tests passed\n");
    return 0;
}
