#include "airun_ui_host.h"

#include <stdio.h>
#include <string.h>

void native_host_ui_reset(void) {}
void native_host_ui_shutdown(void) {}

int native_host_ui_create_window(const char* title, int width, int height, int64_t* out_handle)
{
    (void)title;
    (void)width;
    (void)height;
    if (out_handle != NULL) {
        *out_handle = 0;
    }
    return 0;
}

int native_host_ui_close_window(int64_t handle) { (void)handle; return 0; }
int native_host_ui_begin_frame(int64_t handle) { (void)handle; return 0; }
int native_host_ui_end_frame(int64_t handle) { (void)handle; return 0; }
int native_host_ui_present(int64_t handle) { (void)handle; return 0; }
int native_host_ui_wait_frame(int64_t handle) { (void)handle; return 0; }

int native_host_ui_draw_rect(int64_t handle, int x, int y, int width, int height, const char* color)
{
    (void)handle;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
    return 0;
}

int native_host_ui_draw_ellipse(int64_t handle, int x, int y, int width, int height, const char* color)
{
    (void)handle;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
    return 0;
}

int native_host_ui_draw_image(
    int64_t handle,
    int x,
    int y,
    int width,
    int height,
    const uint8_t* rgba,
    size_t rgba_length)
{
    (void)handle;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)rgba;
    (void)rgba_length;
    return 0;
}

int native_host_ui_draw_text(int64_t handle, int x, int y, const char* text, const char* color, int font_size)
{
    (void)handle;
    (void)x;
    (void)y;
    (void)text;
    (void)color;
    (void)font_size;
    return 0;
}

int native_host_ui_draw_line(int64_t handle, int x1, int y1, int x2, int y2, const char* color, int stroke_width)
{
    (void)handle;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
    (void)stroke_width;
    return 0;
}

int native_host_ui_draw_path(int64_t handle, const char* path, const char* color, int stroke_width)
{
    (void)handle;
    (void)path;
    (void)color;
    (void)stroke_width;
    return 0;
}

int native_host_ui_poll_event(int64_t handle, NativeHostUiEvent* out_event)
{
    (void)handle;
    if (out_event != NULL) {
        memset(out_event, 0, sizeof(*out_event));
        (void)snprintf(out_event->type, sizeof(out_event->type), "unsupported");
    }
    return 0;
}

int native_host_ui_get_window_size(int64_t handle, int* out_width, int* out_height)
{
    (void)handle;
    if (out_width != NULL) {
        *out_width = 0;
    }
    if (out_height != NULL) {
        *out_height = 0;
    }
    return 0;
}
