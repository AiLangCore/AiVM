#ifndef AIRUN_UI_HOST_H
#define AIRUN_UI_HOST_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char type[16];
    char target_id[48];
    char key[48];
    char text[128];
    int x;
    int y;
    int modifiers;
    int repeat;
} NativeHostUiEvent;

void native_host_ui_reset(void);
void native_host_ui_shutdown(void);

int native_host_ui_create_window(const char* title, int width, int height, int64_t* out_handle);
int native_host_ui_close_window(int64_t handle);
int native_host_ui_begin_frame(int64_t handle);
int native_host_ui_end_frame(int64_t handle);
int native_host_ui_present(int64_t handle);
int native_host_ui_wait_frame(int64_t handle);
int native_host_ui_draw_rect(int64_t handle, int x, int y, int width, int height, const char* color);
int native_host_ui_draw_ellipse(int64_t handle, int x, int y, int width, int height, const char* color);
int native_host_ui_draw_image(
    int64_t handle,
    int x,
    int y,
    int width,
    int height,
    const uint8_t* rgba,
    size_t rgba_length);
int native_host_ui_draw_text(int64_t handle, int x, int y, const char* text, const char* color, int font_size);
int native_host_ui_draw_line(int64_t handle, int x1, int y1, int x2, int y2, const char* color, int stroke_width);
int native_host_ui_draw_path(int64_t handle, const char* path, const char* color, int stroke_width);
int native_host_ui_poll_event(int64_t handle, NativeHostUiEvent* out_event);
int native_host_ui_get_window_size(int64_t handle, int* out_width, int* out_height);

#endif
