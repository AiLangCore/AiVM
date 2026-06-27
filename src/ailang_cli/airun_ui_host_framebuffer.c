#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "airun_ui_host.h"

#ifdef __linux__

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int64_t handle;
    int width;
    int height;
} NativeUiFramebufferWindow;

static int g_fb_fd = -1;
static uint8_t* g_fb_mem = NULL;
static uint8_t* g_back_mem = NULL;
static uint8_t* g_draw_mem = NULL;
static size_t g_fb_bytes = 0U;
static struct fb_var_screeninfo g_fb_var;
static struct fb_fix_screeninfo g_fb_fix;
static int64_t g_next_handle = 1;
static NativeUiFramebufferWindow g_windows[NATIVE_HOST_UI_WINDOW_CAPACITY];

enum {
    FB_INPUT_DEVICE_CAPACITY = 32,
    FB_EVENT_QUEUE_CAPACITY = 128,
    FB_IDLE_INPUT_WAIT_MS = 4,
    FB_EVDEV_READ_EVENT_CAPACITY = 32
};

static int g_input_fds[FB_INPUT_DEVICE_CAPACITY];
static char g_input_paths[FB_INPUT_DEVICE_CAPACITY][64];
static char g_input_names[FB_INPUT_DEVICE_CAPACITY][128];
static size_t g_input_fd_count = 0U;
static int g_input_initialized = 0;
static NativeHostUiEvent g_event_queue[FB_EVENT_QUEUE_CAPACITY];
static size_t g_event_head = 0U;
static size_t g_event_tail = 0U;
static int g_cursor_x = 32;
static int g_cursor_y = 32;
static int g_presented_cursor_x = -1;
static int g_presented_cursor_y = -1;
static int g_pointer_down = 0;
static int g_shift_down = 0;
static int g_dirty_valid = 0;
static int g_dirty_x0 = 0;
static int g_dirty_y0 = 0;
static int g_dirty_x1 = 0;
static int g_dirty_y1 = 0;
static int g_dirty_suppressed = 0;

static void fb_present_cursor_overlay(const NativeUiFramebufferWindow* window);

static int fb_event_loop_trace_enabled(void)
{
    static int initialized = 0;
    static int enabled = 0;
    const char* value;
    if (!initialized) {
        value = getenv("AILANG_EVENT_LOOP_TRACE");
        enabled = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
        initialized = 1;
    }
    return enabled;
}

static int fb_trace_enabled(void)
{
    static int initialized = 0;
    static int enabled = 0;
    const char* value;
    if (!initialized) {
        value = getenv("AILANG_FB_TRACE");
        enabled = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
        initialized = 1;
    }
    return enabled;
}

static int fb_evdev_trace_enabled(void)
{
    static int initialized = 0;
    static int enabled = 0;
    const char* value;
    if (!initialized) {
        value = getenv("AILANG_EVDEV_TRACE");
        enabled = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
        initialized = 1;
    }
    return enabled;
}

static const char* fb_evdev_type_name(unsigned short type)
{
    switch (type) {
        case EV_SYN: return "EV_SYN";
        case EV_KEY: return "EV_KEY";
        case EV_REL: return "EV_REL";
        case EV_ABS: return "EV_ABS";
        case EV_MSC: return "EV_MSC";
        default: return "EV_OTHER";
    }
}

static const char* fb_evdev_code_name(unsigned short type, unsigned short code)
{
    if (type == EV_SYN) {
        switch (code) {
            case SYN_REPORT: return "SYN_REPORT";
            case SYN_CONFIG: return "SYN_CONFIG";
            case SYN_MT_REPORT: return "SYN_MT_REPORT";
            case SYN_DROPPED: return "SYN_DROPPED";
            default: return "SYN_OTHER";
        }
    }
    if (type == EV_REL) {
        switch (code) {
            case REL_X: return "REL_X";
            case REL_Y: return "REL_Y";
            case REL_WHEEL: return "REL_WHEEL";
            case REL_HWHEEL: return "REL_HWHEEL";
            default: return "REL_OTHER";
        }
    }
    if (type == EV_ABS) {
        switch (code) {
            case ABS_X: return "ABS_X";
            case ABS_Y: return "ABS_Y";
            default: return "ABS_OTHER";
        }
    }
    if (type == EV_KEY) {
        switch (code) {
            case BTN_LEFT: return "BTN_LEFT";
            case BTN_RIGHT: return "BTN_RIGHT";
            case KEY_ENTER: return "KEY_ENTER";
            case KEY_BACKSPACE: return "KEY_BACKSPACE";
            case KEY_DELETE: return "KEY_DELETE";
            case KEY_ESC: return "KEY_ESC";
            case KEY_TAB: return "KEY_TAB";
            case KEY_SPACE: return "KEY_SPACE";
            default: return "KEY_OTHER";
        }
    }
    return "CODE_OTHER";
}

static uint64_t fb_now_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static void fb_mark_dirty(int x0, int y0, int x1, int y1)
{
    if (g_dirty_suppressed) {
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)g_fb_var.xres) x1 = (int)g_fb_var.xres;
    if (y1 > (int)g_fb_var.yres) y1 = (int)g_fb_var.yres;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    if (!g_dirty_valid) {
        g_dirty_x0 = x0;
        g_dirty_y0 = y0;
        g_dirty_x1 = x1;
        g_dirty_y1 = y1;
        g_dirty_valid = 1;
        return;
    }
    if (x0 < g_dirty_x0) g_dirty_x0 = x0;
    if (y0 < g_dirty_y0) g_dirty_y0 = y0;
    if (x1 > g_dirty_x1) g_dirty_x1 = x1;
    if (y1 > g_dirty_y1) g_dirty_y1 = y1;
}

static void fb_copy_back_to_front_rect(int x, int y, int width, int height)
{
    int yy;
    int x0;
    int y0;
    int x1;
    int y1;
    size_t bytes_per_pixel;
    if (g_fb_mem == NULL || g_back_mem == NULL || width <= 0 || height <= 0) {
        return;
    }
    bytes_per_pixel = (size_t)g_fb_var.bits_per_pixel / 8U;
    if (bytes_per_pixel == 0U) {
        return;
    }
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + width;
    y1 = y + height;
    if (x1 > (int)g_fb_var.xres) x1 = (int)g_fb_var.xres;
    if (y1 > (int)g_fb_var.yres) y1 = (int)g_fb_var.yres;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    for (yy = y0; yy < y1; yy += 1) {
        size_t row_offset = (size_t)yy * (size_t)g_fb_fix.line_length + (size_t)x0 * bytes_per_pixel;
        size_t row_bytes = (size_t)(x1 - x0) * bytes_per_pixel;
        memcpy(g_fb_mem + row_offset, g_back_mem + row_offset, row_bytes);
    }
}

static int fb_open(void)
{
    const char* path;
    if (g_fb_fd >= 0 && g_fb_mem != NULL) {
        return 1;
    }
    path = getenv("AILANG_FBDEV");
    if (path == NULL || path[0] == '\0') {
        path = "/dev/fb0";
    }
    g_fb_fd = open(path, O_RDWR);
    if (g_fb_fd < 0) {
        perror("aivectra framebuffer open");
        return 0;
    }
    if (ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &g_fb_fix) != 0 ||
        ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &g_fb_var) != 0) {
        perror("aivectra framebuffer ioctl");
        close(g_fb_fd);
        g_fb_fd = -1;
        return 0;
    }
    g_fb_bytes = (size_t)g_fb_fix.line_length * (size_t)g_fb_var.yres_virtual;
    g_fb_mem = (uint8_t*)mmap(NULL, g_fb_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb_mem == MAP_FAILED) {
        perror("aivectra framebuffer mmap");
        g_fb_mem = NULL;
        close(g_fb_fd);
        g_fb_fd = -1;
        return 0;
    }
    g_back_mem = (uint8_t*)malloc(g_fb_bytes);
    if (g_back_mem == NULL) {
        perror("aivectra framebuffer back buffer");
        munmap(g_fb_mem, g_fb_bytes);
        g_fb_mem = NULL;
        close(g_fb_fd);
        g_fb_fd = -1;
        return 0;
    }
    memcpy(g_back_mem, g_fb_mem, g_fb_bytes);
    g_draw_mem = g_back_mem;
    return 1;
}

static int fb_event_queue_empty(void)
{
    return g_event_head == g_event_tail;
}

static size_t fb_event_queue_depth(void)
{
    return (g_event_tail + FB_EVENT_QUEUE_CAPACITY - g_event_head) % FB_EVENT_QUEUE_CAPACITY;
}

static int fb_event_queue_full(void)
{
    return ((g_event_tail + 1U) % FB_EVENT_QUEUE_CAPACITY) == g_event_head;
}

static void fb_queue_event(const NativeHostUiEvent* event)
{
    if (event == NULL) {
        return;
    }
    if (fb_event_queue_full()) {
        g_event_head = (g_event_head + 1U) % FB_EVENT_QUEUE_CAPACITY;
    }
    g_event_queue[g_event_tail] = *event;
    g_event_tail = (g_event_tail + 1U) % FB_EVENT_QUEUE_CAPACITY;
}

static int fb_pop_event(NativeHostUiEvent* out_event)
{
    if (out_event == NULL || fb_event_queue_empty()) {
        return 0;
    }
    *out_event = g_event_queue[g_event_head];
    g_event_head = (g_event_head + 1U) % FB_EVENT_QUEUE_CAPACITY;
    return 1;
}

static void fb_close_input(void)
{
    size_t i;
    for (i = 0U; i < g_input_fd_count; i += 1U) {
        if (g_input_fds[i] >= 0) {
            close(g_input_fds[i]);
            g_input_fds[i] = -1;
        }
        g_input_paths[i][0] = '\0';
        g_input_names[i][0] = '\0';
    }
    g_input_fd_count = 0U;
    g_input_initialized = 0;
    g_event_head = 0U;
    g_event_tail = 0U;
}

static void fb_open_input_path(const char* path)
{
    int fd;
    size_t index;
    if (path == NULL || path[0] == '\0' || g_input_fd_count >= FB_INPUT_DEVICE_CAPACITY) {
        return;
    }
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return;
    }
    index = g_input_fd_count;
    g_input_fds[index] = fd;
    (void)snprintf(g_input_paths[index], sizeof(g_input_paths[index]), "%s", path);
    g_input_names[index][0] = '\0';
    if (ioctl(fd, EVIOCGNAME(sizeof(g_input_names[index])), g_input_names[index]) < 0 ||
        g_input_names[index][0] == '\0') {
        (void)snprintf(g_input_names[index], sizeof(g_input_names[index]), "unknown");
    }
    if (fb_evdev_trace_enabled()) {
        fprintf(stderr, "aivectra evdev trace: open fd=%d device=%s name=\"%s\"\n",
            fd,
            g_input_paths[index],
            g_input_names[index]);
    }
    g_input_fd_count += 1U;
}

static void fb_ensure_input_open(void)
{
    const char* explicit_path;
    int i;
    char path[64];
    if (g_input_initialized) {
        return;
    }
    g_input_initialized = 1;
    explicit_path = getenv("AILANG_INPUTDEV");
    if (explicit_path != NULL && explicit_path[0] != '\0') {
        fb_open_input_path(explicit_path);
        return;
    }
    for (i = 0; i < FB_INPUT_DEVICE_CAPACITY; i += 1) {
        (void)snprintf(path, sizeof(path), "/dev/input/event%d", i);
        fb_open_input_path(path);
    }
}

static NativeUiFramebufferWindow* fb_find_window(int64_t handle)
{
    size_t i;
    for (i = 0U; i < sizeof(g_windows) / sizeof(g_windows[0]); i += 1U) {
        if (g_windows[i].handle == handle) {
            return &g_windows[i];
        }
    }
    return NULL;
}

static NativeUiFramebufferWindow* fb_alloc_window(void)
{
    size_t i;
    for (i = 0U; i < sizeof(g_windows) / sizeof(g_windows[0]); i += 1U) {
        if (g_windows[i].handle == 0) {
            return &g_windows[i];
        }
    }
    return NULL;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

static void parse_color(const char* color, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a)
{
    *r = 0U;
    *g = 0U;
    *b = 0U;
    *a = 255U;
    if (color == NULL || color[0] == '\0' || strcmp(color, "none") == 0) {
        *a = 0U;
        return;
    }
    if (color[0] == '#' && strlen(color) >= 7U) {
        *r = (uint8_t)((hex_digit(color[1]) << 4) | hex_digit(color[2]));
        *g = (uint8_t)((hex_digit(color[3]) << 4) | hex_digit(color[4]));
        *b = (uint8_t)((hex_digit(color[5]) << 4) | hex_digit(color[6]));
    }
}

static uint32_t pack_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t pixel = 0U;
    if (g_fb_var.bits_per_pixel == 16U) {
        return (uint32_t)((r >> 3U) << 11U) | (uint32_t)((g >> 2U) << 5U) | (uint32_t)(b >> 3U);
    }
    pixel |= ((uint32_t)r << g_fb_var.red.offset) & (((1U << g_fb_var.red.length) - 1U) << g_fb_var.red.offset);
    pixel |= ((uint32_t)g << g_fb_var.green.offset) & (((1U << g_fb_var.green.length) - 1U) << g_fb_var.green.offset);
    pixel |= ((uint32_t)b << g_fb_var.blue.offset) & (((1U << g_fb_var.blue.length) - 1U) << g_fb_var.blue.offset);
    return pixel;
}

static void fb_put_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    size_t offset;
    uint32_t pixel;
    if (g_draw_mem == NULL || x < 0 || y < 0 || (uint32_t)x >= g_fb_var.xres || (uint32_t)y >= g_fb_var.yres) {
        return;
    }
    offset = (size_t)y * (size_t)g_fb_fix.line_length + (size_t)x * ((size_t)g_fb_var.bits_per_pixel / 8U);
    if (offset + (g_fb_var.bits_per_pixel / 8U) > g_fb_bytes) {
        return;
    }
    pixel = pack_pixel(r, g, b);
    if (g_fb_var.bits_per_pixel == 16U) {
        *(uint16_t*)(void*)(g_draw_mem + offset) = (uint16_t)pixel;
    } else if (g_fb_var.bits_per_pixel == 24U) {
        g_draw_mem[offset] = (uint8_t)(pixel & 0xffU);
        g_draw_mem[offset + 1U] = (uint8_t)((pixel >> 8U) & 0xffU);
        g_draw_mem[offset + 2U] = (uint8_t)((pixel >> 16U) & 0xffU);
    } else {
        *(uint32_t*)(void*)(g_draw_mem + offset) = pixel;
    }
    fb_mark_dirty(x, y, x + 1, y + 1);
}

static void fb_fill_rect(int x, int y, int width, int height, uint8_t r, uint8_t g, uint8_t b)
{
    int yy;
    int xx;
    int x0;
    int y0;
    int x1;
    int y1;
    size_t bytes_per_pixel;
    uint32_t pixel;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (g_draw_mem == NULL) {
        return;
    }
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + width;
    y1 = y + height;
    if (x1 > (int)g_fb_var.xres) x1 = (int)g_fb_var.xres;
    if (y1 > (int)g_fb_var.yres) y1 = (int)g_fb_var.yres;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    bytes_per_pixel = (size_t)g_fb_var.bits_per_pixel / 8U;
    if (bytes_per_pixel == 0U) {
        return;
    }
    fb_mark_dirty(x0, y0, x1, y1);
    pixel = pack_pixel(r, g, b);
    for (yy = y0; yy < y1; yy += 1) {
        uint8_t* row = g_draw_mem + (size_t)yy * (size_t)g_fb_fix.line_length + (size_t)x0 * bytes_per_pixel;
        if (g_fb_var.bits_per_pixel == 32U) {
            uint32_t* out = (uint32_t*)(void*)row;
            for (xx = x0; xx < x1; xx += 1) {
                *out++ = pixel;
            }
        } else if (g_fb_var.bits_per_pixel == 16U) {
            uint16_t* out = (uint16_t*)(void*)row;
            for (xx = x0; xx < x1; xx += 1) {
                *out++ = (uint16_t)pixel;
            }
        } else if (g_fb_var.bits_per_pixel == 24U) {
            uint8_t p0 = (uint8_t)(pixel & 0xffU);
            uint8_t p1 = (uint8_t)((pixel >> 8U) & 0xffU);
            uint8_t p2 = (uint8_t)((pixel >> 16U) & 0xffU);
            uint8_t* out = row;
            for (xx = x0; xx < x1; xx += 1) {
                *out++ = p0;
                *out++ = p1;
                *out++ = p2;
            }
        } else {
            for (xx = x0; xx < x1; xx += 1) {
                fb_put_pixel(xx, yy, r, g, b);
            }
        }
    }
}

static void fb_draw_line_raw(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, int stroke_width)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int half = stroke_width > 1 ? stroke_width / 2 : 0;
    for (;;) {
        fb_fill_rect(x0 - half, y0 - half, stroke_width > 0 ? stroke_width : 1, stroke_width > 0 ? stroke_width : 1, r, g, b);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (2 * err >= dy) {
            err += dy;
            x0 += sx;
        }
        if (2 * err <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static const uint8_t* fb_glyph_rows(char c)
{
    static const uint8_t blank[7] = { 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t unknown[7] = { 0x1e, 0x11, 0x01, 0x06, 0x04, 0x00, 0x04 };
    static const uint8_t glyph_0[7] = { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e };
    static const uint8_t glyph_1[7] = { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e };
    static const uint8_t glyph_2[7] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f };
    static const uint8_t glyph_3[7] = { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e };
    static const uint8_t glyph_4[7] = { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 };
    static const uint8_t glyph_5[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e };
    static const uint8_t glyph_6[7] = { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e };
    static const uint8_t glyph_7[7] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
    static const uint8_t glyph_8[7] = { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e };
    static const uint8_t glyph_9[7] = { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c };
    static const uint8_t glyph_a[7] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
    static const uint8_t glyph_b[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e };
    static const uint8_t glyph_c[7] = { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e };
    static const uint8_t glyph_d[7] = { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e };
    static const uint8_t glyph_e[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f };
    static const uint8_t glyph_f[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 };
    static const uint8_t glyph_g[7] = { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f };
    static const uint8_t glyph_h[7] = { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
    static const uint8_t glyph_i[7] = { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e };
    static const uint8_t glyph_j[7] = { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e };
    static const uint8_t glyph_k[7] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
    static const uint8_t glyph_l[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f };
    static const uint8_t glyph_m[7] = { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 };
    static const uint8_t glyph_n[7] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
    static const uint8_t glyph_o[7] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
    static const uint8_t glyph_p[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 };
    static const uint8_t glyph_q[7] = { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d };
    static const uint8_t glyph_r[7] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 };
    static const uint8_t glyph_s[7] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e };
    static const uint8_t glyph_t[7] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
    static const uint8_t glyph_u[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
    static const uint8_t glyph_v[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 };
    static const uint8_t glyph_w[7] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a };
    static const uint8_t glyph_x[7] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 };
    static const uint8_t glyph_y[7] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 };
    static const uint8_t glyph_z[7] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f };
    static const uint8_t colon[7] = { 0, 0x04, 0x04, 0, 0x04, 0x04, 0 };
    static const uint8_t dot[7] = { 0, 0, 0, 0, 0, 0x0c, 0x0c };
    static const uint8_t comma[7] = { 0, 0, 0, 0, 0x0c, 0x04, 0x08 };
    static const uint8_t dash[7] = { 0, 0, 0, 0x1f, 0, 0, 0 };
    static const uint8_t slash[7] = { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
    static const uint8_t percent[7] = { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 };
    static const uint8_t paren_l[7] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
    static const uint8_t paren_r[7] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - ('a' - 'A'));
    }
    switch (c) {
        case ' ': return blank;
        case '0': return glyph_0; case '1': return glyph_1; case '2': return glyph_2; case '3': return glyph_3; case '4': return glyph_4;
        case '5': return glyph_5; case '6': return glyph_6; case '7': return glyph_7; case '8': return glyph_8; case '9': return glyph_9;
        case 'A': return glyph_a; case 'B': return glyph_b; case 'C': return glyph_c; case 'D': return glyph_d; case 'E': return glyph_e;
        case 'F': return glyph_f; case 'G': return glyph_g; case 'H': return glyph_h; case 'I': return glyph_i; case 'J': return glyph_j;
        case 'K': return glyph_k; case 'L': return glyph_l; case 'M': return glyph_m; case 'N': return glyph_n; case 'O': return glyph_o;
        case 'P': return glyph_p; case 'Q': return glyph_q; case 'R': return glyph_r; case 'S': return glyph_s; case 'T': return glyph_t;
        case 'U': return glyph_u; case 'V': return glyph_v; case 'W': return glyph_w; case 'X': return glyph_x; case 'Y': return glyph_y; case 'Z': return glyph_z;
        case ':': return colon; case '.': return dot; case ',': return comma; case '-': return dash; case '/': return slash; case '%': return percent;
        case '(': return paren_l; case ')': return paren_r;
        default: return unknown;
    }
}

static int fb_font_scale(int font_size)
{
    int scale = (font_size + 5) / 8;
    return scale > 0 ? scale : 1;
}

static void fb_set_event_base(NativeHostUiEvent* event, const char* type, int x, int y)
{
    memset(event, 0, sizeof(*event));
    (void)snprintf(event->type, sizeof(event->type), "%s", type != NULL ? type : "none");
    event->x = x;
    event->y = y;
}

static void fb_clamp_cursor(const NativeUiFramebufferWindow* window)
{
    int max_x;
    int max_y;
    if (window == NULL) {
        return;
    }
    max_x = window->width > 0 ? window->width - 1 : 0;
    max_y = window->height > 0 ? window->height - 1 : 0;
    if (g_cursor_x < 0) g_cursor_x = 0;
    if (g_cursor_y < 0) g_cursor_y = 0;
    if (g_cursor_x > max_x) g_cursor_x = max_x;
    if (g_cursor_y > max_y) g_cursor_y = max_y;
}

static const char* fb_key_name(unsigned short code)
{
    switch (code) {
        case KEY_ENTER: return "enter";
        case KEY_BACKSPACE: return "backspace";
        case KEY_DELETE: return "delete";
        case KEY_ESC: return "escape";
        case KEY_TAB: return "tab";
        case KEY_SPACE: return "space";
        case KEY_LEFT: return "left";
        case KEY_RIGHT: return "right";
        case KEY_UP: return "up";
        case KEY_DOWN: return "down";
        case KEY_HOME: return "home";
        case KEY_END: return "end";
        case KEY_PAGEUP: return "pageup";
        case KEY_PAGEDOWN: return "pagedown";
        case KEY_MINUS: return "-";
        case KEY_EQUAL: return "=";
        case KEY_LEFTBRACE: return "[";
        case KEY_RIGHTBRACE: return "]";
        case KEY_BACKSLASH: return "\\";
        case KEY_SEMICOLON: return ";";
        case KEY_APOSTROPHE: return "'";
        case KEY_GRAVE: return "`";
        case KEY_COMMA: return ",";
        case KEY_DOT: return ".";
        case KEY_SLASH: return "/";
        default: break;
    }
    if (code >= KEY_A && code <= KEY_Z) {
        static char names[26][2];
        size_t index = (size_t)(code - KEY_A);
        if (names[index][0] == '\0') {
            names[index][0] = (char)('a' + index);
            names[index][1] = '\0';
        }
        return names[index];
    }
    if (code >= KEY_1 && code <= KEY_9) {
        static char digits[9][2];
        size_t index = (size_t)(code - KEY_1);
        if (digits[index][0] == '\0') {
            digits[index][0] = (char)('1' + index);
            digits[index][1] = '\0';
        }
        return digits[index];
    }
    if (code == KEY_0) {
        return "0";
    }
    return "";
}

static char fb_key_text(unsigned short code)
{
    static const char unshifted_digits[] = "1234567890";
    static const char shifted_digits[] = "!@#$%^&*()";
    if (code >= KEY_A && code <= KEY_Z) {
        char base = (char)('a' + (code - KEY_A));
        return g_shift_down ? (char)(base - ('a' - 'A')) : base;
    }
    if (code >= KEY_1 && code <= KEY_9) {
        size_t index = (size_t)(code - KEY_1);
        return g_shift_down ? shifted_digits[index] : unshifted_digits[index];
    }
    if (code == KEY_0) {
        return g_shift_down ? ')' : '0';
    }
    switch (code) {
        case KEY_SPACE: return ' ';
        case KEY_MINUS: return g_shift_down ? '_' : '-';
        case KEY_EQUAL: return g_shift_down ? '+' : '=';
        case KEY_LEFTBRACE: return g_shift_down ? '{' : '[';
        case KEY_RIGHTBRACE: return g_shift_down ? '}' : ']';
        case KEY_BACKSLASH: return g_shift_down ? '|' : '\\';
        case KEY_SEMICOLON: return g_shift_down ? ':' : ';';
        case KEY_APOSTROPHE: return g_shift_down ? '"' : '\'';
        case KEY_GRAVE: return g_shift_down ? '~' : '`';
        case KEY_COMMA: return g_shift_down ? '<' : ',';
        case KEY_DOT: return g_shift_down ? '>' : '.';
        case KEY_SLASH: return g_shift_down ? '?' : '/';
        default: return '\0';
    }
}

static void fb_queue_key_event(unsigned short code, int value)
{
    NativeHostUiEvent event;
    const char* key;
    char text;
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        g_shift_down = value != 0;
        return;
    }
    if (value == 0) {
        return;
    }
    key = fb_key_name(code);
    if (key == NULL || key[0] == '\0') {
        return;
    }
    fb_set_event_base(&event, "key", -1, -1);
    (void)snprintf(event.key, sizeof(event.key), "%s", key);
    text = fb_key_text(code);
    if (text != '\0') {
        event.text[0] = text;
        event.text[1] = '\0';
    }
    event.repeat = value == 2;
    fb_queue_event(&event);
}

static void fb_queue_pointer_event(const NativeUiFramebufferWindow* window, const char* type, int dx, int dy)
{
    NativeHostUiEvent event;
    size_t i;
    fb_clamp_cursor(window);
    if (strcmp(type, "drag") == 0 || strcmp(type, "mousemove") == 0 || strcmp(type, "wheel") == 0) {
        size_t count = (g_event_tail + FB_EVENT_QUEUE_CAPACITY - g_event_head) % FB_EVENT_QUEUE_CAPACITY;
        for (i = 0U; i < count; i += 1U) {
            size_t index = (g_event_head + i) % FB_EVENT_QUEUE_CAPACITY;
            if (strcmp(g_event_queue[index].type, type) != 0) {
                continue;
            }
            g_event_queue[index].x = g_cursor_x;
            g_event_queue[index].y = g_cursor_y;
            g_event_queue[index].dx += dx;
            g_event_queue[index].dy += dy;
            return;
        }
    }
    fb_set_event_base(&event, type, g_cursor_x, g_cursor_y);
    event.dx = dx;
    event.dy = dy;
    fb_queue_event(&event);
}

static void fb_process_input_event(const NativeUiFramebufferWindow* window, const struct input_event* ev)
{
    if (window == NULL || ev == NULL) {
        return;
    }
    if (ev->type == EV_REL) {
        if (ev->code == REL_X && ev->value != 0) {
            g_cursor_x += ev->value;
            fb_queue_pointer_event(window, g_pointer_down ? "drag" : "mousemove", ev->value, 0);
        } else if (ev->code == REL_Y && ev->value != 0) {
            g_cursor_y += ev->value;
            fb_queue_pointer_event(window, g_pointer_down ? "drag" : "mousemove", 0, ev->value);
        } else if (ev->code == REL_WHEEL && ev->value != 0) {
            fb_queue_pointer_event(window, "wheel", 0, -ev->value * 32);
        } else if (ev->code == REL_HWHEEL && ev->value != 0) {
            fb_queue_pointer_event(window, "wheel", ev->value * 32, 0);
        }
    } else if (ev->type == EV_ABS) {
        if (ev->code == ABS_X) {
            g_cursor_x = ev->value;
            fb_queue_pointer_event(window, g_pointer_down ? "drag" : "mousemove", 0, 0);
        } else if (ev->code == ABS_Y) {
            g_cursor_y = ev->value;
            fb_queue_pointer_event(window, g_pointer_down ? "drag" : "mousemove", 0, 0);
        }
    } else if (ev->type == EV_KEY) {
        if (ev->code == BTN_LEFT) {
            g_pointer_down = ev->value != 0;
            fb_queue_pointer_event(window, ev->value != 0 ? "mouse_down" : "click", 0, 0);
        } else if (ev->code == BTN_RIGHT && ev->value == 0) {
            fb_queue_pointer_event(window, "click", 0, 0);
        } else {
            fb_queue_key_event(ev->code, ev->value);
        }
    }
}

static int fb_wait_for_input_events(int timeout_ms)
{
    struct pollfd fds[FB_INPUT_DEVICE_CAPACITY];
    size_t i;
    nfds_t count = 0;
    int ready;
    uint64_t start_ns = 0U;
    uint64_t end_ns = 0U;
    fb_ensure_input_open();
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    if (fb_event_loop_trace_enabled()) {
        start_ns = fb_now_ns();
        fprintf(stderr, "aivectra event-loop trace: input-wait-begin timeoutMs=%d queueDepth=%llu inputFds=%llu\n",
            timeout_ms,
            (unsigned long long)fb_event_queue_depth(),
            (unsigned long long)g_input_fd_count);
    }
    for (i = 0U; i < g_input_fd_count; i += 1U) {
        if (g_input_fds[i] < 0) {
            continue;
        }
        fds[count].fd = g_input_fds[i];
        fds[count].events = POLLIN;
        fds[count].revents = 0;
        count += 1U;
    }
    if (count == 0U) {
        struct timespec delay;
        delay.tv_sec = timeout_ms / 1000;
        delay.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
        }
        ready = 0;
    } else {
        ready = poll(fds, count, timeout_ms);
        if (ready < 0 && errno == EINTR) {
            ready = 0;
        }
    }
    if (fb_event_loop_trace_enabled()) {
        end_ns = fb_now_ns();
        fprintf(stderr, "aivectra event-loop trace: input-wait-end ready=%d elapsedMs=%.3f queueDepth=%llu\n",
            ready,
            (double)(end_ns - start_ns) / 1000000.0,
            (unsigned long long)fb_event_queue_depth());
    }
    return ready > 0;
}

static size_t fb_poll_input_devices(const NativeUiFramebufferWindow* window)
{
    size_t i;
    size_t event_count = 0U;
    int old_cursor_x = g_cursor_x;
    int old_cursor_y = g_cursor_y;
    uint64_t start_ns = 0U;
    uint64_t end_ns = 0U;
    fb_ensure_input_open();
    if (fb_event_loop_trace_enabled()) {
        start_ns = fb_now_ns();
        fprintf(stderr, "aivectra event-loop trace: input-poll-begin queueDepth=%llu inputFds=%llu\n",
            (unsigned long long)fb_event_queue_depth(),
            (unsigned long long)g_input_fd_count);
    }
    for (i = 0U; i < g_input_fd_count; i += 1U) {
        for (;;) {
            struct input_event events[FB_EVDEV_READ_EVENT_CAPACITY];
            ssize_t read_result;
            size_t record_count;
            size_t event_index;
            int batch_cursor_x = g_cursor_x;
            int batch_cursor_y = g_cursor_y;
            uint64_t read_start_ns = 0U;
            uint64_t read_end_ns = 0U;
            if (fb_evdev_trace_enabled()) {
                read_start_ns = fb_now_ns();
            }
            read_result = read(g_input_fds[i], events, sizeof(events));
            if (fb_evdev_trace_enabled()) {
                read_end_ns = fb_now_ns();
            }
            if (read_result > 0) {
                record_count = (size_t)read_result / sizeof(events[0]);
                if (fb_evdev_trace_enabled()) {
                    fprintf(stderr,
                        "aivectra evdev trace: read fd=%d device=%s name=\"%s\" bytes=%lld records=%llu readStartNs=%llu readEndNs=%llu cursorBefore=%d,%d elapsedMs=%.3f\n",
                        g_input_fds[i],
                        g_input_paths[i],
                        g_input_names[i],
                        (long long)read_result,
                        (unsigned long long)record_count,
                        (unsigned long long)read_start_ns,
                        (unsigned long long)read_end_ns,
                        batch_cursor_x,
                        batch_cursor_y,
                        (double)(read_end_ns - read_start_ns) / 1000000.0);
                }
                for (event_index = 0U; event_index < record_count; event_index += 1U) {
                    const struct input_event* ev = &events[event_index];
                    int before_x = g_cursor_x;
                    int before_y = g_cursor_y;
                    if (fb_evdev_trace_enabled()) {
                        fprintf(stderr,
                            "aivectra evdev trace: event fd=%d index=%llu type=%u(%s) code=%u(%s) value=%d syn=%s cursorBefore=%d,%d\n",
                            g_input_fds[i],
                            (unsigned long long)event_index,
                            (unsigned int)ev->type,
                            fb_evdev_type_name(ev->type),
                            (unsigned int)ev->code,
                            fb_evdev_code_name(ev->type, ev->code),
                            ev->value,
                            ev->type == EV_SYN ? "boundary" : "no",
                            before_x,
                            before_y);
                    }
                    fb_process_input_event(window, ev);
                    event_count += 1U;
                    if (fb_evdev_trace_enabled()) {
                        fprintf(stderr,
                            "aivectra evdev trace: event-applied fd=%d index=%llu cursorAfter=%d,%d queueDepth=%llu\n",
                            g_input_fds[i],
                            (unsigned long long)event_index,
                            g_cursor_x,
                            g_cursor_y,
                            (unsigned long long)fb_event_queue_depth());
                    }
                }
                if (fb_evdev_trace_enabled()) {
                    fprintf(stderr,
                        "aivectra evdev trace: read-applied fd=%d records=%llu cursorAfter=%d,%d queueDepth=%llu\n",
                        g_input_fds[i],
                        (unsigned long long)record_count,
                        g_cursor_x,
                        g_cursor_y,
                        (unsigned long long)fb_event_queue_depth());
                }
                continue;
            }
            if (read_result < 0 && (errno == EINTR)) {
                continue;
            }
            if (fb_evdev_trace_enabled() && read_result > 0) {
                fprintf(stderr,
                    "aivectra evdev trace: partial-read fd=%d device=%s bytes=%lld ignoredTrailingBytes=%llu\n",
                    g_input_fds[i],
                    g_input_paths[i],
                    (long long)read_result,
                    (unsigned long long)((size_t)read_result % sizeof(events[0])));
            }
            break;
        }
    }
    if (old_cursor_x != g_cursor_x || old_cursor_y != g_cursor_y) {
        fb_present_cursor_overlay(window);
    }
    if (fb_event_loop_trace_enabled()) {
        end_ns = fb_now_ns();
        fprintf(stderr, "aivectra event-loop trace: input-poll-end inputEvents=%llu queueDepth=%llu cursor=%d,%d elapsedMs=%.3f\n",
            (unsigned long long)event_count,
            (unsigned long long)fb_event_queue_depth(),
            g_cursor_x,
            g_cursor_y,
            (double)(end_ns - start_ns) / 1000000.0);
    }
    return event_count;
}

static void fb_draw_glyph(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, int scale)
{
    int row;
    int col;
    const uint8_t* rows = fb_glyph_rows(c);
    if (scale < 1) {
        scale = 1;
    }
    for (row = 0; row < 7; row += 1) {
        for (col = 0; col < 5; col += 1) {
            if ((rows[row] & (uint8_t)(1U << (4 - col))) != 0U) {
                fb_fill_rect(x + col * scale, y + row * scale, scale, scale, r, g, b);
            }
        }
    }
}

static void fb_draw_cursor(void)
{
    uint8_t old_r = 0U;
    uint8_t old_g = 0U;
    uint8_t old_b = 0U;
    uint8_t old_a = 255U;
    (void)old_a;
    parse_color("#ffffff", &old_r, &old_g, &old_b, &old_a);
    fb_draw_line_raw(g_cursor_x, g_cursor_y, g_cursor_x + 13, g_cursor_y + 18, old_r, old_g, old_b, 2);
    fb_draw_line_raw(g_cursor_x, g_cursor_y, g_cursor_x + 2, g_cursor_y + 22, old_r, old_g, old_b, 2);
    fb_draw_line_raw(g_cursor_x + 2, g_cursor_y + 22, g_cursor_x + 7, g_cursor_y + 16, old_r, old_g, old_b, 2);
    fb_draw_line_raw(g_cursor_x + 7, g_cursor_y + 16, g_cursor_x + 13, g_cursor_y + 18, old_r, old_g, old_b, 2);
    fb_draw_line_raw(g_cursor_x, g_cursor_y, g_cursor_x + 13, g_cursor_y + 18, 16U, 24U, 39U, 1);
    fb_draw_line_raw(g_cursor_x, g_cursor_y, g_cursor_x + 2, g_cursor_y + 22, 16U, 24U, 39U, 1);
    fb_draw_line_raw(g_cursor_x + 2, g_cursor_y + 22, g_cursor_x + 7, g_cursor_y + 16, 16U, 24U, 39U, 1);
    fb_draw_line_raw(g_cursor_x + 7, g_cursor_y + 16, g_cursor_x + 13, g_cursor_y + 18, 16U, 24U, 39U, 1);
}

static void fb_present_cursor_overlay(const NativeUiFramebufferWindow* window)
{
    uint8_t* previous_draw_mem;
    if (window == NULL || g_fb_mem == NULL || g_back_mem == NULL) {
        return;
    }
    fb_clamp_cursor(window);
    if (g_presented_cursor_x >= 0 && g_presented_cursor_y >= 0) {
        fb_copy_back_to_front_rect(g_presented_cursor_x - 3, g_presented_cursor_y - 3, 24, 30);
    }
    previous_draw_mem = g_draw_mem;
    g_draw_mem = g_fb_mem;
    g_dirty_suppressed = 1;
    fb_draw_cursor();
    g_dirty_suppressed = 0;
    g_draw_mem = previous_draw_mem;
    g_presented_cursor_x = g_cursor_x;
    g_presented_cursor_y = g_cursor_y;
}

void native_host_ui_reset(void)
{
    memset(g_windows, 0, sizeof(g_windows));
    g_next_handle = 1;
    fb_close_input();
    g_pointer_down = 0;
    g_shift_down = 0;
}

void native_host_ui_shutdown(void)
{
    fb_close_input();
    if (g_fb_mem != NULL) {
        munmap(g_fb_mem, g_fb_bytes);
        g_fb_mem = NULL;
    }
    if (g_back_mem != NULL) {
        free(g_back_mem);
        g_back_mem = NULL;
    }
    g_draw_mem = NULL;
    if (g_fb_fd >= 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }
}

int native_host_ui_create_window(const char* title, int width, int height, int64_t* out_handle)
{
    NativeUiFramebufferWindow* window;
    (void)title;
    if (out_handle == NULL || !fb_open()) {
        return 0;
    }
    window = fb_alloc_window();
    if (window == NULL) {
        return 0;
    }
    window->handle = g_next_handle++;
    (void)width;
    (void)height;
    window->width = (int)g_fb_var.xres;
    window->height = (int)g_fb_var.yres;
    g_cursor_x = window->width / 2;
    g_cursor_y = window->height / 2;
    *out_handle = window->handle;
    return 1;
}

int native_host_ui_close_window(int64_t handle)
{
    NativeUiFramebufferWindow* window = fb_find_window(handle);
    if (window == NULL) {
        return 0;
    }
    memset(window, 0, sizeof(*window));
    return 1;
}

int native_host_ui_begin_frame(int64_t handle)
{
    NativeUiFramebufferWindow* window = fb_find_window(handle);
    if (window == NULL) {
        return 0;
    }
    g_draw_mem = g_back_mem != NULL ? g_back_mem : g_fb_mem;
    fb_fill_rect(0, 0, window->width, window->height, 255U, 255U, 255U);
    return 1;
}

int native_host_ui_end_frame(int64_t handle) { return fb_find_window(handle) != NULL; }
int native_host_ui_present(int64_t handle)
{
    NativeUiFramebufferWindow* window = fb_find_window(handle);
    uint64_t start_ns = 0U;
    uint64_t end_ns;
    size_t copied_rows = 0U;
    if (window == NULL) {
        return 0;
    }
    if (fb_event_loop_trace_enabled()) {
        start_ns = fb_now_ns();
        fprintf(stderr, "aivectra event-loop trace: present-begin dirtyValid=%d dirty=%d,%d-%d,%d\n",
            g_dirty_valid,
            g_dirty_x0,
            g_dirty_y0,
            g_dirty_x1,
            g_dirty_y1);
    }
    if (g_fb_mem != NULL && g_back_mem != NULL) {
        int y;
        int y0;
        int y1;
        size_t row_bytes = (size_t)g_fb_fix.line_length;
        if (fb_trace_enabled()) {
            start_ns = fb_now_ns();
        }
        g_draw_mem = g_back_mem;
        if (g_dirty_valid) {
            y0 = g_dirty_y0;
            y1 = g_dirty_y1;
        } else {
            y0 = 0;
            y1 = 0;
        }
        for (y = y0; y < y1; y += 1) {
            uint8_t* fb_row = g_fb_mem + (size_t)y * row_bytes;
            uint8_t* back_row = g_back_mem + (size_t)y * row_bytes;
            if (memcmp(fb_row, back_row, row_bytes) != 0) {
                memcpy(fb_row, back_row, row_bytes);
                copied_rows += 1U;
            }
        }
        if (fb_trace_enabled()) {
            end_ns = fb_now_ns();
            fprintf(stderr,
                "aivectra framebuffer present: dirty=%d,%d-%d,%d scannedRows=%d copiedRows=%llu elapsedMs=%.3f\n",
                g_dirty_x0,
                g_dirty_y0,
                g_dirty_x1,
                g_dirty_y1,
                y1 - y0,
                (unsigned long long)copied_rows,
                (double)(end_ns - start_ns) / 1000000.0);
        }
        fb_present_cursor_overlay(window);
        g_dirty_valid = 0;
    }
    if (fb_event_loop_trace_enabled()) {
        end_ns = fb_now_ns();
        fprintf(stderr, "aivectra event-loop trace: present-end elapsedMs=%.3f\n",
            (double)(end_ns - start_ns) / 1000000.0);
    }
    return 1;
}
int native_host_ui_wait_frame(int64_t handle)
{
    struct timespec frame_delay;
    (void)handle;
    frame_delay.tv_sec = 0;
    frame_delay.tv_nsec = 4000000L;
    (void)nanosleep(&frame_delay, NULL);
    return 1;
}

int native_host_ui_draw_rect(int64_t handle, int x, int y, int width, int height, const char* color)
{
    uint8_t r, g, b, a;
    if (fb_find_window(handle) == NULL) return 0;
    parse_color(color, &r, &g, &b, &a);
    if (a == 0U) return 1;
    fb_fill_rect(x, y, width, height, r, g, b);
    return 1;
}

int native_host_ui_draw_ellipse(int64_t handle, int x, int y, int width, int height, const char* color)
{
    uint8_t r, g, b, a;
    int yy;
    int xx;
    int rx;
    int ry;
    if (fb_find_window(handle) == NULL) return 0;
    parse_color(color, &r, &g, &b, &a);
    if (a == 0U || width <= 0 || height <= 0) return 1;
    rx = width / 2;
    ry = height / 2;
    if (rx <= 0 || ry <= 0) return 1;
    for (yy = 0; yy < height; yy += 1) {
        for (xx = 0; xx < width; xx += 1) {
            int dx = xx - rx;
            int dy = yy - ry;
            if ((dx * dx * ry * ry + dy * dy * rx * rx) <= (rx * rx * ry * ry)) {
                fb_put_pixel(x + xx, y + yy, r, g, b);
            }
        }
    }
    return 1;
}

int native_host_ui_draw_image(int64_t handle, int x, int y, int width, int height, const uint8_t* rgba, size_t rgba_length)
{
    int yy;
    int xx;
    if (fb_find_window(handle) == NULL || rgba == NULL || rgba_length != (size_t)width * (size_t)height * 4U) return 0;
    for (yy = 0; yy < height; yy += 1) {
        for (xx = 0; xx < width; xx += 1) {
            size_t offset = ((size_t)yy * (size_t)width + (size_t)xx) * 4U;
            if (rgba[offset + 3U] != 0U) {
                fb_put_pixel(x + xx, y + yy, rgba[offset], rgba[offset + 1U], rgba[offset + 2U]);
            }
        }
    }
    return 1;
}

int native_host_ui_draw_text(int64_t handle, int x, int y, const char* text, const char* color, int font_size)
{
    uint8_t r, g, b, a;
    int scale;
    const char* c;
    int cursor_x = x;
    if (fb_find_window(handle) == NULL || text == NULL) return 0;
    parse_color(color, &r, &g, &b, &a);
    if (a == 0U) return 1;
    scale = fb_font_scale(font_size);
    for (c = text; *c != '\0'; c += 1) {
        fb_draw_glyph(cursor_x, y, *c, r, g, b, scale);
        cursor_x += 6 * scale;
    }
    return 1;
}

int native_host_ui_measure_text(int64_t handle, const char* text, int font_size, int* out_width)
{
    int scale;
    if (fb_find_window(handle) == NULL || text == NULL || out_width == NULL) return 0;
    scale = fb_font_scale(font_size);
    *out_width = (int)strlen(text) * 6 * scale;
    return 1;
}

int native_host_ui_draw_line(int64_t handle, int x1, int y1, int x2, int y2, const char* color, int stroke_width)
{
    uint8_t r, g, b, a;
    if (fb_find_window(handle) == NULL) return 0;
    parse_color(color, &r, &g, &b, &a);
    if (a == 0U) return 1;
    fb_draw_line_raw(x1, y1, x2, y2, r, g, b, stroke_width);
    return 1;
}

int native_host_ui_draw_path(int64_t handle, const char* path, const char* fill_color, const char* stroke_color, int stroke_width)
{
    (void)fill_color;
    (void)stroke_color;
    (void)stroke_width;
    (void)path;
    return fb_find_window(handle) != NULL;
}

int native_host_ui_push_clip_path(int64_t handle, const char* path) { (void)path; return fb_find_window(handle) != NULL; }
int native_host_ui_pop_clip_path(int64_t handle) { return fb_find_window(handle) != NULL; }

int native_host_ui_poll_event(int64_t handle, NativeHostUiEvent* out_event)
{
    NativeUiFramebufferWindow* window = fb_find_window(handle);
    uint64_t start_ns = 0U;
    uint64_t end_ns;
    size_t input_count;
    if (window == NULL || out_event == NULL) return 0;
    if (fb_event_loop_trace_enabled()) {
        start_ns = fb_now_ns();
        fprintf(stderr, "aivectra event-loop trace: loop-begin queueDepth=%llu\n",
            (unsigned long long)fb_event_queue_depth());
    }
    input_count = fb_poll_input_devices(window);
    if (fb_event_queue_empty()) {
        (void)fb_wait_for_input_events(FB_IDLE_INPUT_WAIT_MS);
        input_count += fb_poll_input_devices(window);
    }
    if (fb_pop_event(out_event)) {
        if (fb_event_loop_trace_enabled()) {
            end_ns = fb_now_ns();
            fprintf(stderr, "aivectra event-loop trace: loop-end event=%s inputEvents=%llu queueDepth=%llu elapsedMs=%.3f\n",
                out_event->type,
                (unsigned long long)input_count,
                (unsigned long long)fb_event_queue_depth(),
                (double)(end_ns - start_ns) / 1000000.0);
        }
        return 1;
    }
    memset(out_event, 0, sizeof(*out_event));
    snprintf(out_event->type, sizeof(out_event->type), "none");
    out_event->x = -1;
    out_event->y = -1;
    if (fb_event_loop_trace_enabled()) {
        end_ns = fb_now_ns();
        fprintf(stderr, "aivectra event-loop trace: loop-end event=none inputEvents=%llu queueDepth=%llu elapsedMs=%.3f\n",
            (unsigned long long)input_count,
            (unsigned long long)fb_event_queue_depth(),
            (double)(end_ns - start_ns) / 1000000.0);
    }
    return 1;
}

int native_host_ui_get_window_size(int64_t handle, int* out_width, int* out_height)
{
    NativeUiFramebufferWindow* window = fb_find_window(handle);
    if (window == NULL) return 0;
    if (out_width != NULL) *out_width = window->width;
    if (out_height != NULL) *out_height = window->height;
    return 1;
}

#else
#error "airun_ui_host_framebuffer.c must only be compiled for linux targets."
#endif
