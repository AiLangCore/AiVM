#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "airun_ui_host_aios_drm.h"

#ifdef __linux__

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int aios_drm_trace_enabled(void)
{
    static int initialized = 0;
    static int enabled = 0;
    const char* value;
    if (!initialized) {
        value = getenv("AILANG_DRM_TRACE");
        enabled = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0 && strcmp(value, "false") != 0;
        initialized = 1;
    }
    return enabled;
}

int airun_aios_drm_requested(void)
{
    const char* backend = getenv("AILANG_AIOS_DISPLAY_BACKEND");
    return backend != NULL && strcmp(backend, "drm") == 0;
}

void airun_aios_drm_probe_or_report_fallback(void)
{
    static int reported = 0;
    int fd;
    if (reported || !airun_aios_drm_requested()) {
        return;
    }
    reported = 1;
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) {
        fprintf(
            stderr,
            "AiOS DRM backend unavailable; falling back to framebuffer: /dev/dri/card0 open failed: %s\n",
            strerror(errno));
        return;
    }
    if (aios_drm_trace_enabled()) {
        fprintf(stderr, "AiOS DRM trace: opened /dev/dri/card0 successfully\n");
    }
    close(fd);
    fprintf(
        stderr,
        "AiOS DRM backend unavailable; falling back to framebuffer: KMS presentation is not implemented in this runtime yet\n");
}

#else

int airun_aios_drm_requested(void)
{
    return 0;
}

void airun_aios_drm_probe_or_report_fallback(void)
{
}

#endif
