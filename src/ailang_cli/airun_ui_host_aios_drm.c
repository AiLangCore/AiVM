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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<drm/drm.h>) && __has_include(<drm/drm_mode.h>)
#include <drm/drm.h>
#include <drm/drm_mode.h>
#define AIRUN_HAVE_DRM_UAPI_HEADERS 1
#elif __has_include(<linux/drm.h>) && __has_include(<linux/drm_mode.h>)
#include <linux/drm.h>
#include <linux/drm_mode.h>
#define AIRUN_HAVE_DRM_UAPI_HEADERS 1
#endif
#endif

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif

#ifndef AIRUN_HAVE_DRM_UAPI_HEADERS
#define DRM_IOCTL_BASE 'd'
#define DRM_IOWR(nr, type) _IOWR(DRM_IOCTL_BASE, nr, type)

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
};

struct drm_mode_card_res {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
};

struct drm_mode_get_connector {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t pad;
};

struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
};

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_fb_cmd {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct drm_mode_destroy_dumb {
    uint32_t handle;
};

#define DRM_IOCTL_MODE_GETRESOURCES DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_SETCRTC DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_ADDFB DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB DRM_IOWR(0xAF, uint32_t)
#define DRM_IOCTL_MODE_CREATE_DUMB DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)
#endif

typedef struct {
    uint32_t handle;
    uint32_t fb_id;
    uint32_t pitch;
    uint64_t size;
    uint8_t* memory;
} AirunAiosDrmBuffer;

static int g_drm_fd = -1;
static uint32_t g_drm_connector_id = 0U;
static uint32_t g_drm_crtc_id = 0U;
static struct drm_mode_modeinfo g_drm_mode;
static AirunAiosDrmBuffer g_drm_buffers[2];
static int g_drm_front_index = 0;
static int g_drm_present_index = 1;
static int g_drm_active = 0;

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

static void aios_drm_report_fallback(const char* detail)
{
    if (detail == NULL || detail[0] == '\0') {
        detail = "unknown error";
    }
    fprintf(stderr, "AiOS DRM backend unavailable; falling back to framebuffer: %s\n", detail);
}

static int aios_drm_ioctl(int fd, unsigned long request, void* arg)
{
    int result;
    do {
        result = ioctl(fd, request, arg);
    } while (result != 0 && errno == EINTR);
    return result;
}

static int aios_drm_get_resources(int fd, struct drm_mode_card_res* out_res, uint32_t** out_connectors, uint32_t** out_crtcs)
{
    uint32_t* connectors;
    uint32_t* crtcs;
    memset(out_res, 0, sizeof(*out_res));
    if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, out_res) != 0 ||
        out_res->count_connectors == 0U ||
        out_res->count_crtcs == 0U) {
        return 0;
    }
    connectors = (uint32_t*)calloc(out_res->count_connectors, sizeof(uint32_t));
    crtcs = (uint32_t*)calloc(out_res->count_crtcs, sizeof(uint32_t));
    if (connectors == NULL || crtcs == NULL) {
        free(connectors);
        free(crtcs);
        return 0;
    }
    out_res->connector_id_ptr = (uint64_t)(uintptr_t)connectors;
    out_res->crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, out_res) != 0) {
        free(connectors);
        free(crtcs);
        return 0;
    }
    *out_connectors = connectors;
    *out_crtcs = crtcs;
    return 1;
}

static int aios_drm_select_crtc(
    int fd,
    const struct drm_mode_card_res* res,
    const uint32_t* crtcs,
    const struct drm_mode_get_connector* connector,
    uint32_t* out_crtc_id)
{
    struct drm_mode_get_encoder encoder;
    uint32_t i;
    if (connector->encoder_id != 0U) {
        memset(&encoder, 0, sizeof(encoder));
        encoder.encoder_id = connector->encoder_id;
        if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &encoder) == 0 && encoder.crtc_id != 0U) {
            *out_crtc_id = encoder.crtc_id;
            return 1;
        }
    }
    for (i = 0U; i < connector->count_encoders; i += 1U) {
        uint32_t encoder_id = ((const uint32_t*)(uintptr_t)connector->encoders_ptr)[i];
        uint32_t crtc_index;
        memset(&encoder, 0, sizeof(encoder));
        encoder.encoder_id = encoder_id;
        if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &encoder) != 0) {
            continue;
        }
        for (crtc_index = 0U; crtc_index < res->count_crtcs; crtc_index += 1U) {
            if ((encoder.possible_crtcs & (1U << crtc_index)) != 0U) {
                *out_crtc_id = crtcs[crtc_index];
                return 1;
            }
        }
    }
    if (res->count_crtcs > 0U) {
        *out_crtc_id = crtcs[0];
        return 1;
    }
    return 0;
}

static int aios_drm_select_display(int fd)
{
    struct drm_mode_card_res res;
    uint32_t* connectors = NULL;
    uint32_t* crtcs = NULL;
    uint32_t pass;
    if (!aios_drm_get_resources(fd, &res, &connectors, &crtcs)) {
        return 0;
    }
    if (aios_drm_trace_enabled()) {
        fprintf(stderr, "AiOS DRM trace: resources connectors=%u crtcs=%u\n", res.count_connectors, res.count_crtcs);
    }
    for (pass = 0U; pass < 2U; pass += 1U) {
        uint32_t i;
        for (i = 0U; i < res.count_connectors; i += 1U) {
            struct drm_mode_get_connector connector;
            struct drm_mode_modeinfo* modes;
            uint32_t* encoders;
            memset(&connector, 0, sizeof(connector));
            connector.connector_id = connectors[i];
            if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) != 0) {
                if (aios_drm_trace_enabled()) {
                    fprintf(stderr, "AiOS DRM trace: connector id=%u get failed errno=%d\n", connectors[i], errno);
                }
                continue;
            }
            if (aios_drm_trace_enabled()) {
                fprintf(stderr,
                    "AiOS DRM trace: connector id=%u connection=%u modes=%u encoders=%u encoder=%u pass=%u\n",
                    connector.connector_id,
                    connector.connection,
                    connector.count_modes,
                    connector.count_encoders,
                    connector.encoder_id,
                    pass);
            }
            if (connector.count_modes == 0U) {
                continue;
            }
            if (pass == 0U && connector.connection != DRM_MODE_CONNECTED) {
                continue;
            }
            modes = (struct drm_mode_modeinfo*)calloc(connector.count_modes, sizeof(struct drm_mode_modeinfo));
            encoders = connector.count_encoders == 0U ? NULL : (uint32_t*)calloc(connector.count_encoders, sizeof(uint32_t));
            if (modes == NULL || (connector.count_encoders > 0U && encoders == NULL)) {
                free(modes);
                free(encoders);
                continue;
            }
            connector.modes_ptr = (uint64_t)(uintptr_t)modes;
            connector.encoders_ptr = (uint64_t)(uintptr_t)encoders;
            if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) == 0 &&
                connector.count_modes > 0U &&
                (pass == 1U || connector.connection == DRM_MODE_CONNECTED) &&
                aios_drm_select_crtc(fd, &res, crtcs, &connector, &g_drm_crtc_id)) {
                g_drm_connector_id = connector.connector_id;
                g_drm_mode = modes[0];
                if (aios_drm_trace_enabled()) {
                    fprintf(stderr,
                        "AiOS DRM trace: selected connector=%u crtc=%u mode=%ux%u name=%s pass=%u\n",
                        g_drm_connector_id,
                        g_drm_crtc_id,
                        (unsigned)g_drm_mode.hdisplay,
                        (unsigned)g_drm_mode.vdisplay,
                        g_drm_mode.name,
                        pass);
                }
                free(modes);
                free(encoders);
                free(connectors);
                free(crtcs);
                return 1;
            }
            free(modes);
            free(encoders);
        }
    }
    free(connectors);
    free(crtcs);
    return 0;
}

static int aios_drm_create_buffer(int fd, int width, int height, AirunAiosDrmBuffer* out_buffer)
{
    struct drm_mode_create_dumb create_args;
    struct drm_mode_fb_cmd fb_args;
    struct drm_mode_map_dumb map_args;
    memset(out_buffer, 0, sizeof(*out_buffer));
    memset(&create_args, 0, sizeof(create_args));
    create_args.width = (uint32_t)width;
    create_args.height = (uint32_t)height;
    create_args.bpp = 32U;
    if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_args) != 0) {
        return 0;
    }
    memset(&fb_args, 0, sizeof(fb_args));
    fb_args.width = create_args.width;
    fb_args.height = create_args.height;
    fb_args.pitch = create_args.pitch;
    fb_args.bpp = 32U;
    fb_args.depth = 24U;
    fb_args.handle = create_args.handle;
    if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb_args) != 0) {
        struct drm_mode_destroy_dumb destroy_args;
        memset(&destroy_args, 0, sizeof(destroy_args));
        destroy_args.handle = create_args.handle;
        (void)aios_drm_ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_args);
        return 0;
    }
    memset(&map_args, 0, sizeof(map_args));
    map_args.handle = create_args.handle;
    if (aios_drm_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_args) != 0) {
        (void)aios_drm_ioctl(fd, DRM_IOCTL_MODE_RMFB, &fb_args.fb_id);
        {
            struct drm_mode_destroy_dumb destroy_args;
            memset(&destroy_args, 0, sizeof(destroy_args));
            destroy_args.handle = create_args.handle;
            (void)aios_drm_ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_args);
        }
        return 0;
    }
    out_buffer->memory = (uint8_t*)mmap(NULL, (size_t)create_args.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)map_args.offset);
    if (out_buffer->memory == MAP_FAILED) {
        out_buffer->memory = NULL;
        (void)aios_drm_ioctl(fd, DRM_IOCTL_MODE_RMFB, &fb_args.fb_id);
        {
            struct drm_mode_destroy_dumb destroy_args;
            memset(&destroy_args, 0, sizeof(destroy_args));
            destroy_args.handle = create_args.handle;
            (void)aios_drm_ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_args);
        }
        return 0;
    }
    out_buffer->handle = create_args.handle;
    out_buffer->fb_id = fb_args.fb_id;
    out_buffer->pitch = create_args.pitch;
    out_buffer->size = create_args.size;
    memset(out_buffer->memory, 0, (size_t)out_buffer->size);
    return 1;
}

static int aios_drm_set_crtc(uint32_t fb_id)
{
    struct drm_mode_crtc crtc;
    uint32_t connector_id = g_drm_connector_id;
    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = g_drm_crtc_id;
    crtc.fb_id = fb_id;
    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&connector_id;
    crtc.count_connectors = 1U;
    crtc.mode_valid = 1U;
    crtc.mode = g_drm_mode;
    return aios_drm_ioctl(g_drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) == 0;
}

static void aios_drm_destroy_buffer(AirunAiosDrmBuffer* buffer)
{
    if (buffer == NULL || g_drm_fd < 0) {
        return;
    }
    if (buffer->memory != NULL) {
        munmap(buffer->memory, (size_t)buffer->size);
        buffer->memory = NULL;
    }
    if (buffer->fb_id != 0U) {
        (void)aios_drm_ioctl(g_drm_fd, DRM_IOCTL_MODE_RMFB, &buffer->fb_id);
        buffer->fb_id = 0U;
    }
    if (buffer->handle != 0U) {
        struct drm_mode_destroy_dumb destroy_args;
        memset(&destroy_args, 0, sizeof(destroy_args));
        destroy_args.handle = buffer->handle;
        (void)aios_drm_ioctl(g_drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_args);
        buffer->handle = 0U;
    }
}

int airun_aios_drm_open(AirunAiosDrmSurface* out_surface)
{
    int width;
    int height;
    if (out_surface == NULL || !airun_aios_drm_requested()) {
        return 0;
    }
    memset(out_surface, 0, sizeof(*out_surface));
    g_drm_fd = open("/dev/dri/card0", O_RDWR);
    if (g_drm_fd < 0) {
        aios_drm_report_fallback("/dev/dri/card0 open failed");
        return 0;
    }
    if (!aios_drm_select_display(g_drm_fd)) {
        aios_drm_report_fallback("no connected DRM display mode found");
        airun_aios_drm_shutdown();
        return 0;
    }
    width = (int)g_drm_mode.hdisplay;
    height = (int)g_drm_mode.vdisplay;
    if (!aios_drm_create_buffer(g_drm_fd, width, height, &g_drm_buffers[0]) ||
        !aios_drm_create_buffer(g_drm_fd, width, height, &g_drm_buffers[1])) {
        aios_drm_report_fallback("failed to allocate DRM dumb buffers");
        airun_aios_drm_shutdown();
        return 0;
    }
    if (!aios_drm_set_crtc(g_drm_buffers[0].fb_id)) {
        aios_drm_report_fallback("failed to set DRM CRTC");
        airun_aios_drm_shutdown();
        return 0;
    }
    g_drm_front_index = 0;
    g_drm_present_index = 1;
    g_drm_active = 1;
    out_surface->width = width;
    out_surface->height = height;
    out_surface->bits_per_pixel = 32;
    out_surface->line_length = (size_t)g_drm_buffers[g_drm_front_index].pitch;
    out_surface->bytes = (size_t)g_drm_buffers[g_drm_front_index].size;
    out_surface->memory = g_drm_buffers[g_drm_front_index].memory;
    fprintf(stderr, "AiOS display backend selected: drm (%dx%d)\n", width, height);
    return 1;
}

int airun_aios_drm_active(void)
{
    return g_drm_active;
}

uint8_t* airun_aios_drm_begin_present(AirunAiosDrmSurface* out_surface)
{
    AirunAiosDrmBuffer* buffer;
    if (!g_drm_active) {
        return NULL;
    }
    g_drm_present_index = g_drm_front_index == 0 ? 1 : 0;
    buffer = &g_drm_buffers[g_drm_present_index];
    if (out_surface != NULL) {
        out_surface->width = (int)g_drm_mode.hdisplay;
        out_surface->height = (int)g_drm_mode.vdisplay;
        out_surface->bits_per_pixel = 32;
        out_surface->line_length = (size_t)buffer->pitch;
        out_surface->bytes = (size_t)buffer->size;
        out_surface->memory = buffer->memory;
    }
    return buffer->memory;
}

void airun_aios_drm_end_present(void)
{
    if (!g_drm_active) {
        return;
    }
    if (aios_drm_trace_enabled()) {
        fprintf(stderr, "AiOS DRM trace: present fb=%u connector=%u crtc=%u\n",
            g_drm_buffers[g_drm_present_index].fb_id,
            g_drm_connector_id,
            g_drm_crtc_id);
    }
    if (aios_drm_set_crtc(g_drm_buffers[g_drm_present_index].fb_id)) {
        g_drm_front_index = g_drm_present_index;
    } else {
        fprintf(stderr, "AiOS DRM warning: present failed; keeping previous frame\n");
    }
}

void airun_aios_drm_shutdown(void)
{
    aios_drm_destroy_buffer(&g_drm_buffers[0]);
    aios_drm_destroy_buffer(&g_drm_buffers[1]);
    if (g_drm_fd >= 0) {
        close(g_drm_fd);
        g_drm_fd = -1;
    }
    g_drm_connector_id = 0U;
    g_drm_crtc_id = 0U;
    memset(&g_drm_mode, 0, sizeof(g_drm_mode));
    g_drm_front_index = 0;
    g_drm_present_index = 1;
    g_drm_active = 0;
}

#else

int airun_aios_drm_requested(void)
{
    return 0;
}

int airun_aios_drm_open(AirunAiosDrmSurface* out_surface)
{
    (void)out_surface;
    return 0;
}

int airun_aios_drm_active(void)
{
    return 0;
}

uint8_t* airun_aios_drm_begin_present(AirunAiosDrmSurface* out_surface)
{
    (void)out_surface;
    return NULL;
}

void airun_aios_drm_end_present(void)
{
}

void airun_aios_drm_shutdown(void)
{
}

#endif
