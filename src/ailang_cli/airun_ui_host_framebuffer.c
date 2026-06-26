#include "airun_ui_host.h"

#ifdef __linux__

#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    int64_t handle;
    int width;
    int height;
} NativeUiFramebufferWindow;

static int g_fb_fd = -1;
static uint8_t* g_fb_mem = NULL;
static size_t g_fb_bytes = 0U;
static struct fb_var_screeninfo g_fb_var;
static struct fb_fix_screeninfo g_fb_fix;
static int64_t g_next_handle = 1;
static NativeUiFramebufferWindow g_windows[NATIVE_HOST_UI_WINDOW_CAPACITY];

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
    return 1;
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
    if (g_fb_mem == NULL || x < 0 || y < 0 || (uint32_t)x >= g_fb_var.xres || (uint32_t)y >= g_fb_var.yres) {
        return;
    }
    offset = (size_t)y * (size_t)g_fb_fix.line_length + (size_t)x * ((size_t)g_fb_var.bits_per_pixel / 8U);
    if (offset + (g_fb_var.bits_per_pixel / 8U) > g_fb_bytes) {
        return;
    }
    pixel = pack_pixel(r, g, b);
    if (g_fb_var.bits_per_pixel == 16U) {
        *(uint16_t*)(void*)(g_fb_mem + offset) = (uint16_t)pixel;
    } else if (g_fb_var.bits_per_pixel == 24U) {
        g_fb_mem[offset] = (uint8_t)(pixel & 0xffU);
        g_fb_mem[offset + 1U] = (uint8_t)((pixel >> 8U) & 0xffU);
        g_fb_mem[offset + 2U] = (uint8_t)((pixel >> 16U) & 0xffU);
    } else {
        *(uint32_t*)(void*)(g_fb_mem + offset) = pixel;
    }
}

static void fb_fill_rect(int x, int y, int width, int height, uint8_t r, uint8_t g, uint8_t b)
{
    int yy;
    int xx;
    if (width <= 0 || height <= 0) {
        return;
    }
    for (yy = y; yy < y + height; yy += 1) {
        for (xx = x; xx < x + width; xx += 1) {
            fb_put_pixel(xx, yy, r, g, b);
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

static void fb_draw_glyph(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, int scale)
{
    int row;
    int col;
    unsigned int seed = (unsigned char)c;
    if (c == ' ') {
        return;
    }
    if (scale < 1) {
        scale = 1;
    }
    for (row = 0; row < 7; row += 1) {
        for (col = 0; col < 5; col += 1) {
            int edge = row == 0 || row == 6 || col == 0 || col == 4;
            int bit = ((seed >> ((row + col) % 6)) & 1U) != 0U;
            if (edge || bit) {
                fb_fill_rect(x + col * scale, y + row * scale, scale, scale, r, g, b);
            }
        }
    }
}

void native_host_ui_reset(void)
{
    memset(g_windows, 0, sizeof(g_windows));
    g_next_handle = 1;
}

void native_host_ui_shutdown(void)
{
    if (g_fb_mem != NULL) {
        munmap(g_fb_mem, g_fb_bytes);
        g_fb_mem = NULL;
    }
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
    window->width = (int)g_fb_var.xres;
    window->height = (int)g_fb_var.yres;
    if (width > 0 && width < window->width) {
        window->width = width;
    }
    if (height > 0 && height < window->height) {
        window->height = height;
    }
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
    fb_fill_rect(0, 0, window->width, window->height, 255U, 255U, 255U);
    return 1;
}

int native_host_ui_end_frame(int64_t handle) { return fb_find_window(handle) != NULL; }
int native_host_ui_present(int64_t handle) { return fb_find_window(handle) != NULL; }
int native_host_ui_wait_frame(int64_t handle) { (void)handle; usleep(16000U); return 1; }

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
    scale = font_size > 18 ? 3 : (font_size > 11 ? 2 : 1);
    for (c = text; *c != '\0'; c += 1) {
        fb_draw_glyph(cursor_x, y - 7 * scale, *c, r, g, b, scale);
        cursor_x += 6 * scale;
    }
    return 1;
}

int native_host_ui_measure_text(int64_t handle, const char* text, int font_size, int* out_width)
{
    int scale;
    if (fb_find_window(handle) == NULL || text == NULL || out_width == NULL) return 0;
    scale = font_size > 18 ? 3 : (font_size > 11 ? 2 : 1);
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
    if (fb_find_window(handle) == NULL || out_event == NULL) return 0;
    memset(out_event, 0, sizeof(*out_event));
    snprintf(out_event->type, sizeof(out_event->type), "none");
    out_event->x = -1;
    out_event->y = -1;
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
