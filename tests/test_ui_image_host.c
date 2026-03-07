#define AIRUN_UI_HOST_EXTERNAL 1
#define main airun_embedded_main_for_test
#include "../../../AiCLI/native/airun.c"
#undef main

#include <string.h>

static int g_create_window_calls = 0;
static int g_draw_image_calls = 0;
static int64_t g_last_draw_handle = 0;
static int g_last_draw_x = 0;
static int g_last_draw_y = 0;
static int g_last_draw_width = 0;
static int g_last_draw_height = 0;
static size_t g_last_draw_rgba_length = 0U;
static uint8_t g_last_draw_rgba[16];

void native_host_ui_reset(void) {}
void native_host_ui_shutdown(void) {}

int native_host_ui_create_window(const char* title, int width, int height, int64_t* out_handle)
{
    (void)title;
    (void)width;
    (void)height;
    g_create_window_calls += 1;
    if (out_handle != NULL) {
        *out_handle = 1;
    }
    return 1;
}

int native_host_ui_close_window(int64_t handle) { (void)handle; return 1; }
int native_host_ui_begin_frame(int64_t handle) { (void)handle; return 1; }
int native_host_ui_end_frame(int64_t handle) { (void)handle; return 1; }
int native_host_ui_present(int64_t handle) { (void)handle; return 1; }
int native_host_ui_wait_frame(int64_t handle) { (void)handle; return 1; }
int native_host_ui_draw_rect(int64_t handle, int x, int y, int width, int height, const char* color)
{
    (void)handle;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
    return 1;
}
int native_host_ui_draw_ellipse(int64_t handle, int x, int y, int width, int height, const char* color)
{
    (void)handle;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
    return 1;
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
    g_draw_image_calls += 1;
    g_last_draw_handle = handle;
    g_last_draw_x = x;
    g_last_draw_y = y;
    g_last_draw_width = width;
    g_last_draw_height = height;
    g_last_draw_rgba_length = rgba_length;
    if (rgba_length > sizeof(g_last_draw_rgba)) {
        return 0;
    }
    if (rgba_length > 0U && rgba != NULL) {
        memcpy(g_last_draw_rgba, rgba, rgba_length);
    }
    return 1;
}
int native_host_ui_draw_text(int64_t handle, int x, int y, const char* text, const char* color, int font_size)
{
    (void)handle;
    (void)x;
    (void)y;
    (void)text;
    (void)color;
    (void)font_size;
    return 1;
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
    return 1;
}
int native_host_ui_draw_path(int64_t handle, const char* path, const char* color, int stroke_width)
{
    (void)handle;
    (void)path;
    (void)color;
    (void)stroke_width;
    return 1;
}
int native_host_ui_poll_event(int64_t handle, NativeHostUiEvent* out_event)
{
    (void)handle;
    if (out_event != NULL) {
        memset(out_event, 0, sizeof(*out_event));
        (void)snprintf(out_event->type, sizeof(out_event->type), "none");
    }
    return 1;
}
int native_host_ui_get_window_size(int64_t handle, int* out_width, int* out_height)
{
    (void)handle;
    if (out_width != NULL) {
        *out_width = 64;
    }
    if (out_height != NULL) {
        *out_height = 64;
    }
    return 1;
}

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d\n", __LINE__); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    AivmValue create_args[3];
    AivmValue draw_args[6];
    AivmValue result;
    AivmSyscallStatus status;
    int64_t handle;

    create_args[0] = aivm_value_string("Image Test");
    create_args[1] = aivm_value_int(64);
    create_args[2] = aivm_value_int(64);
    status = native_syscall_ui_create_window("sys.ui.createWindow", create_args, 3U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == 1);
    CHECK(g_create_window_calls == 1);
    handle = result.int_value;

    draw_args[0] = aivm_value_int(handle);
    draw_args[1] = aivm_value_int(3);
    draw_args[2] = aivm_value_int(4);
    draw_args[3] = aivm_value_int(1);
    draw_args[4] = aivm_value_int(1);
    draw_args[5] = aivm_value_string("AQIDBA==");
    status = native_syscall_ui_draw_image("sys.ui.drawImage", draw_args, 6U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_VOID);
    CHECK(g_draw_image_calls == 1);
    CHECK(g_last_draw_handle == handle);
    CHECK(g_last_draw_x == 3);
    CHECK(g_last_draw_y == 4);
    CHECK(g_last_draw_width == 1);
    CHECK(g_last_draw_height == 1);
    CHECK(g_last_draw_rgba_length == 4U);
    CHECK(g_last_draw_rgba[0] == 1U);
    CHECK(g_last_draw_rgba[1] == 2U);
    CHECK(g_last_draw_rgba[2] == 3U);
    CHECK(g_last_draw_rgba[3] == 4U);

    draw_args[5] = aivm_value_string("bad-base64");
    status = native_syscall_ui_draw_image("sys.ui.drawImage", draw_args, 6U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    draw_args[5] = aivm_value_string("AQID");
    status = native_syscall_ui_draw_image("sys.ui.drawImage", draw_args, 6U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    return 0;
}
