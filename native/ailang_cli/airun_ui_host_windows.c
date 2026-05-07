#include "airun_ui_host.h"
#include <string.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>

typedef struct {
    int64_t handle;
    HWND hwnd;
    int close_pending;
    int width;
    int height;
    NativeHostUiEvent pending_event;
    int has_pending_event;
} NativeUiWindowsSlot;

static NativeUiWindowsSlot g_native_ui_windows[8];
static int64_t g_native_ui_next_handle = 1;
static int g_native_ui_class_registered = 0;
static const wchar_t* g_native_ui_class_name = L"AiLangNativeUiWindow";

static NativeUiWindowsSlot* native_ui_windows_find_slot(int64_t handle)
{
    size_t i;
    if (handle <= 0) {
        return NULL;
    }
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        if (g_native_ui_windows[i].handle == handle && g_native_ui_windows[i].hwnd != NULL) {
            return &g_native_ui_windows[i];
        }
    }
    return NULL;
}

static NativeUiWindowsSlot* native_ui_windows_find_slot_by_hwnd(HWND hwnd)
{
    size_t i;
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        if (g_native_ui_windows[i].hwnd == hwnd && hwnd != NULL) {
            return &g_native_ui_windows[i];
        }
    }
    return NULL;
}

static NativeUiWindowsSlot* native_ui_windows_find_empty_slot(void)
{
    size_t i;
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        if (g_native_ui_windows[i].hwnd == NULL) {
            return &g_native_ui_windows[i];
        }
    }
    return NULL;
}

static void native_ui_windows_set_event_type(NativeHostUiEvent* event, const char* type)
{
    if (event == NULL) {
        return;
    }
    memset(event, 0, sizeof(*event));
    if (type == NULL) {
        return;
    }
    (void)snprintf(event->type, sizeof(event->type), "%s", type);
}

static void native_ui_windows_set_pending_event(NativeUiWindowsSlot* slot, const NativeHostUiEvent* event)
{
    if (slot == NULL || event == NULL) {
        return;
    }
    slot->pending_event = *event;
    slot->has_pending_event = 1;
}

static int native_ui_windows_is_text_vk(WPARAM vk)
{
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return 1;
    }
    switch (vk) {
        case VK_SPACE:
        case VK_OEM_1:
        case VK_OEM_2:
        case VK_OEM_3:
        case VK_OEM_4:
        case VK_OEM_5:
        case VK_OEM_6:
        case VK_OEM_7:
        case VK_OEM_COMMA:
        case VK_OEM_MINUS:
        case VK_OEM_PERIOD:
        case VK_OEM_PLUS:
            return 1;
        default:
            return 0;
    }
}

static COLORREF native_ui_windows_parse_color(const char* color, COLORREF fallback)
{
    unsigned int red;
    unsigned int green;
    unsigned int blue;
    if (color == NULL || color[0] == '\0') {
        return fallback;
    }
    if (color[0] == '#') {
        if (sscanf(color + 1, "%02x%02x%02x", &red, &green, &blue) == 3) {
            return RGB((BYTE)red, (BYTE)green, (BYTE)blue);
        }
        return fallback;
    }
    if (_stricmp(color, "white") == 0) {
        return RGB(255, 255, 255);
    }
    if (_stricmp(color, "black") == 0) {
        return RGB(0, 0, 0);
    }
    if (_stricmp(color, "red") == 0) {
        return RGB(255, 0, 0);
    }
    if (_stricmp(color, "green") == 0) {
        return RGB(0, 255, 0);
    }
    if (_stricmp(color, "blue") == 0) {
        return RGB(0, 0, 255);
    }
    return fallback;
}

static const char* native_ui_windows_path_skip(const char* cursor)
{
    while (cursor != NULL && *cursor != '\0') {
        if (!isspace((unsigned char)*cursor) && *cursor != ',') {
            break;
        }
        cursor += 1;
    }
    return cursor;
}

static int native_ui_windows_path_parse_number(const char** io_cursor, double* out_value)
{
    char* end_ptr = NULL;
    const char* cursor;
    if (io_cursor == NULL || *io_cursor == NULL || out_value == NULL) {
        return 0;
    }
    cursor = native_ui_windows_path_skip(*io_cursor);
    if (cursor == NULL || *cursor == '\0') {
        return 0;
    }
    *out_value = strtod(cursor, &end_ptr);
    if (end_ptr == cursor) {
        return 0;
    }
    *io_cursor = end_ptr;
    return 1;
}

static void native_ui_windows_key_name(WPARAM wparam, char* out_key, size_t key_capacity)
{
    UINT vk = (UINT)wparam;
    if (out_key == NULL || key_capacity == 0U) {
        return;
    }
    out_key[0] = '\0';
    switch (vk) {
        case VK_RETURN: (void)snprintf(out_key, key_capacity, "enter"); return;
        case VK_TAB: (void)snprintf(out_key, key_capacity, "tab"); return;
        case VK_SPACE: (void)snprintf(out_key, key_capacity, "space"); return;
        case VK_BACK: (void)snprintf(out_key, key_capacity, "backspace"); return;
        case VK_ESCAPE: (void)snprintf(out_key, key_capacity, "escape"); return;
        case VK_LEFT: (void)snprintf(out_key, key_capacity, "left"); return;
        case VK_RIGHT: (void)snprintf(out_key, key_capacity, "right"); return;
        case VK_UP: (void)snprintf(out_key, key_capacity, "up"); return;
        case VK_DOWN: (void)snprintf(out_key, key_capacity, "down"); return;
        case VK_DELETE: (void)snprintf(out_key, key_capacity, "delete"); return;
        default:
            break;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        out_key[0] = (char)tolower((int)vk);
        out_key[1] = '\0';
        return;
    }
}

static LRESULT CALLBACK native_ui_windows_wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot_by_hwnd(hwnd);
    if (slot != NULL) {
        if (message == WM_CLOSE || message == WM_DESTROY) {
            NativeHostUiEvent event;
            slot->close_pending = 1;
            native_ui_windows_set_event_type(&event, "closed");
            native_ui_windows_set_pending_event(slot, &event);
        } else if (message == WM_SIZE) {
            slot->width = LOWORD(lparam);
            slot->height = HIWORD(lparam);
        } else if (message == WM_KEYDOWN) {
            NativeHostUiEvent event;
            if (native_ui_windows_is_text_vk(wparam)) {
                return DefWindowProcW(hwnd, message, wparam, lparam);
            }
            native_ui_windows_set_event_type(&event, "key");
            native_ui_windows_key_name(wparam, event.key, sizeof(event.key));
            (void)snprintf(event.text, sizeof(event.text), "%s", event.key);
            native_ui_windows_set_pending_event(slot, &event);
        } else if (message == WM_CHAR) {
            NativeHostUiEvent event;
            wchar_t wide_char;
            int written;
            if (wparam == '\r' || wparam == '\t' || wparam == '\b' || wparam == 0x1b) {
                return DefWindowProcW(hwnd, message, wparam, lparam);
            }
            native_ui_windows_set_event_type(&event, "text");
            wide_char = (wchar_t)wparam;
            written = WideCharToMultiByte(CP_UTF8, 0, &wide_char, 1, event.text, (int)sizeof(event.text) - 1, NULL, NULL);
            if (written > 0) {
                event.text[written] = '\0';
            }
            native_ui_windows_set_pending_event(slot, &event);
        } else if (message == WM_LBUTTONDOWN) {
            NativeHostUiEvent event;
            native_ui_windows_set_event_type(&event, "click");
            event.x = GET_X_LPARAM(lparam);
            event.y = GET_Y_LPARAM(lparam);
            native_ui_windows_set_pending_event(slot, &event);
        }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static int native_ui_windows_register_class(void)
{
    WNDCLASSW window_class;
    if (g_native_ui_class_registered != 0) {
        return 1;
    }
    memset(&window_class, 0, sizeof(window_class));
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = native_ui_windows_wndproc;
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = g_native_ui_class_name;
    if (RegisterClassW(&window_class) == 0) {
        return 0;
    }
    g_native_ui_class_registered = 1;
    return 1;
}

void native_host_ui_reset(void)
{
    size_t i;
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        memset(&g_native_ui_windows[i], 0, sizeof(g_native_ui_windows[i]));
    }
    g_native_ui_next_handle = 1;
}

void native_host_ui_shutdown(void)
{
    size_t i;
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        if (g_native_ui_windows[i].hwnd != NULL) {
            DestroyWindow(g_native_ui_windows[i].hwnd);
            g_native_ui_windows[i].hwnd = NULL;
            g_native_ui_windows[i].handle = 0;
        }
    }
}

int native_host_ui_create_window(const char* title, int width, int height, int64_t* out_handle)
{
    NativeUiWindowsSlot* slot;
    HWND hwnd;
    RECT rect;
    DWORD style = WS_OVERLAPPEDWINDOW;
    wchar_t title_w[128];
    int title_len;
    if (out_handle == NULL || width <= 0 || height <= 0) {
        return 0;
    }
    *out_handle = 0;
    if (!native_ui_windows_register_class()) {
        return 0;
    }
    slot = native_ui_windows_find_empty_slot();
    if (slot == NULL) {
        return 0;
    }
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    if (!AdjustWindowRect(&rect, style, FALSE)) {
        return 0;
    }
    if (title == NULL || title[0] == '\0') {
        title = "AiLang";
    }
    title_len = MultiByteToWideChar(CP_UTF8, 0, title, -1, title_w, (int)(sizeof(title_w) / sizeof(title_w[0])));
    if (title_len <= 0) {
        (void)MultiByteToWideChar(CP_ACP, 0, title, -1, title_w, (int)(sizeof(title_w) / sizeof(title_w[0])));
    }
    hwnd = CreateWindowExW(
        0,
        g_native_ui_class_name,
        title_w,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL,
        NULL,
        GetModuleHandleW(NULL),
        NULL);
    if (hwnd == NULL) {
        return 0;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    slot->handle = g_native_ui_next_handle++;
    slot->hwnd = hwnd;
    slot->close_pending = 0;
    slot->width = width;
    slot->height = height;
    slot->has_pending_event = 0;
    *out_handle = slot->handle;
    return 1;
}

int native_host_ui_close_window(int64_t handle)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    if (slot == NULL || slot->hwnd == NULL) {
        return 0;
    }
    DestroyWindow(slot->hwnd);
    slot->hwnd = NULL;
    slot->close_pending = 1;
    slot->handle = 0;
    return 1;
}

int native_host_ui_begin_frame(int64_t handle)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    HDC dc;
    RECT rect;
    HBRUSH brush;
    if (slot == NULL || slot->hwnd == NULL) {
        return 0;
    }
    dc = GetDC(slot->hwnd);
    if (dc == NULL) {
        return 0;
    }
    if (GetClientRect(slot->hwnd, &rect)) {
        brush = CreateSolidBrush(RGB(255, 255, 255));
        if (brush != NULL) {
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        }
    }
    ReleaseDC(slot->hwnd, dc);
    return 1;
}

int native_host_ui_end_frame(int64_t handle)
{
    (void)handle;
    return 1;
}

int native_host_ui_present(int64_t handle)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    if (slot == NULL || slot->hwnd == NULL) {
        return 0;
    }
    InvalidateRect(slot->hwnd, NULL, FALSE);
    UpdateWindow(slot->hwnd);
    return 1;
}

int native_host_ui_wait_frame(int64_t handle)
{
    (void)handle;
    Sleep(16);
    return 1;
}

int native_host_ui_draw_rect(int64_t handle, int x, int y, int width, int height, const char* color)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    HDC dc;
    HBRUSH brush;
    RECT rect;
    if (slot == NULL || slot->hwnd == NULL) {
        return 0;
    }
    if (width <= 0 || height <= 0) {
        return 1;
    }
    dc = GetDC(slot->hwnd);
    if (dc == NULL) {
        return 0;
    }
    brush = CreateSolidBrush(native_ui_windows_parse_color(color, RGB(0, 0, 0)));
    if (brush != NULL) {
        rect.left = x;
        rect.top = y;
        rect.right = x + width;
        rect.bottom = y + height;
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
    ReleaseDC(slot->hwnd, dc);
    return 1;
}

int native_host_ui_draw_ellipse(int64_t handle, int x, int y, int width, int height, const char* color)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    HDC dc;
    HBRUSH brush;
    HGDIOBJ prev_brush;
    HGDIOBJ prev_pen;
    if (slot == NULL || slot->hwnd == NULL) {
        return 0;
    }
    if (width <= 0 || height <= 0) {
        return 1;
    }
    dc = GetDC(slot->hwnd);
    if (dc == NULL) {
        return 0;
    }
    brush = CreateSolidBrush(native_ui_windows_parse_color(color, RGB(0, 0, 0)));
    if (brush == NULL) {
        ReleaseDC(slot->hwnd, dc);
        return 0;
    }
    prev_brush = SelectObject(dc, brush);
    prev_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    (void)Ellipse(dc, x, y, x + width, y + height);
    SelectObject(dc, prev_pen);
    SelectObject(dc, prev_brush);
    DeleteObject(brush);
    ReleaseDC(slot->hwnd, dc);
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
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    HDC dc;
    BITMAPINFO bmi;
    uint8_t* bgra = NULL;
    int xi;
    int yi;
    if (slot == NULL || slot->hwnd == NULL || rgba == NULL) {
        return 0;
    }
    if (width <= 0 || height <= 0) {
        return 1;
    }
    if (rgba_length != (size_t)width * (size_t)height * 4U) {
        return 0;
    }
    dc = GetDC(slot->hwnd);
    if (dc == NULL) {
        return 0;
    }
    bgra = (uint8_t*)malloc(rgba_length);
    if (bgra == NULL) {
        ReleaseDC(slot->hwnd, dc);
        return 0;
    }
    for (yi = 0; yi < height; yi += 1) {
        for (xi = 0; xi < width; xi += 1) {
            size_t src_offset = ((size_t)yi * (size_t)width + (size_t)xi) * 4U;
            size_t dst_offset = ((size_t)(height - 1 - yi) * (size_t)width + (size_t)xi) * 4U;
            COLORREF existing = GetPixel(dc, x + xi, y + yi);
            uint8_t dst_r = existing == CLR_INVALID ? 255U : (uint8_t)GetRValue(existing);
            uint8_t dst_g = existing == CLR_INVALID ? 255U : (uint8_t)GetGValue(existing);
            uint8_t dst_b = existing == CLR_INVALID ? 255U : (uint8_t)GetBValue(existing);
            uint8_t src_r = rgba[src_offset];
            uint8_t src_g = rgba[src_offset + 1U];
            uint8_t src_b = rgba[src_offset + 2U];
            uint8_t src_a = rgba[src_offset + 3U];
            uint8_t out_r = (uint8_t)(((unsigned int)src_r * (unsigned int)src_a + (unsigned int)dst_r * (unsigned int)(255U - src_a) + 127U) / 255U);
            uint8_t out_g = (uint8_t)(((unsigned int)src_g * (unsigned int)src_a + (unsigned int)dst_g * (unsigned int)(255U - src_a) + 127U) / 255U);
            uint8_t out_b = (uint8_t)(((unsigned int)src_b * (unsigned int)src_a + (unsigned int)dst_b * (unsigned int)(255U - src_a) + 127U) / 255U);
            bgra[dst_offset] = out_b;
            bgra[dst_offset + 1U] = out_g;
            bgra[dst_offset + 2U] = out_r;
            bgra[dst_offset + 3U] = 0U;
        }
    }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    if (StretchDIBits(
            dc,
            x,
            y,
            width,
            height,
            0,
            0,
            width,
            height,
            bgra,
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY) == GDI_ERROR) {
        free(bgra);
        ReleaseDC(slot->hwnd, dc);
        return 0;
    }
    free(bgra);
    ReleaseDC(slot->hwnd, dc);
    return 1;
}

int native_host_ui_draw_text(int64_t handle, int x, int y, const char* text, const char* color, int font_size)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    HDC dc;
    HFONT font = NULL;
    HFONT prev_font = NULL;
    COLORREF text_color;
    if (slot == NULL || slot->hwnd == NULL || text == NULL) {
        return 0;
    }
    dc = GetDC(slot->hwnd);
    if (dc == NULL) {
        return 0;
    }
    if (font_size > 0) {
        font = CreateFontA(-font_size, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        if (font != NULL) {
            prev_font = (HFONT)SelectObject(dc, font);
        }
    }
    text_color = native_ui_windows_parse_color(color, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text_color);
    (void)TextOutA(dc, x, y, text, (int)strlen(text));
    if (font != NULL) {
        if (prev_font != NULL) {
            SelectObject(dc, prev_font);
        }
        DeleteObject(font);
    }
    ReleaseDC(slot->hwnd, dc);
    return 1;
}

int native_host_ui_draw_line(int64_t handle, int x1, int y1, int x2, int y2, const char* color, int stroke_width)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    HDC dc;
    HPEN pen;
    HPEN prev_pen;
    if (slot == NULL || slot->hwnd == NULL) {
        return 0;
    }
    dc = GetDC(slot->hwnd);
    if (dc == NULL) {
        return 0;
    }
    pen = CreatePen(PS_SOLID, stroke_width > 0 ? stroke_width : 1, native_ui_windows_parse_color(color, RGB(0, 0, 0)));
    if (pen == NULL) {
        ReleaseDC(slot->hwnd, dc);
        return 0;
    }
    prev_pen = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    (void)LineTo(dc, x2, y2);
    SelectObject(dc, prev_pen);
    DeleteObject(pen);
    ReleaseDC(slot->hwnd, dc);
    return 1;
}

int native_host_ui_draw_path(int64_t handle, const char* path, const char* color, int stroke_width)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    HDC dc;
    HPEN pen = NULL;
    HPEN prev_pen = NULL;
    const char* cursor;
    char cmd = '\0';
    double x = 0.0;
    double y = 0.0;
    double start_x = 0.0;
    double start_y = 0.0;
    int has_point = 0;
    if (slot == NULL || slot->hwnd == NULL || path == NULL) {
        return 0;
    }
    dc = GetDC(slot->hwnd);
    if (dc == NULL) {
        return 0;
    }
    pen = CreatePen(PS_SOLID, stroke_width > 0 ? stroke_width : 1, native_ui_windows_parse_color(color, RGB(0, 0, 0)));
    if (pen == NULL) {
        ReleaseDC(slot->hwnd, dc);
        return 0;
    }
    prev_pen = (HPEN)SelectObject(dc, pen);
    cursor = path;
    while (cursor != NULL && *cursor != '\0') {
        double a = 0.0;
        double b = 0.0;
        cursor = native_ui_windows_path_skip(cursor);
        if (*cursor == '\0') {
            break;
        }
        if (isalpha((unsigned char)*cursor)) {
            cmd = *cursor;
            cursor += 1;
            if (cmd == 'Z' || cmd == 'z') {
                if (has_point) {
                    MoveToEx(dc, (int)x, (int)y, NULL);
                    (void)LineTo(dc, (int)start_x, (int)start_y);
                    x = start_x;
                    y = start_y;
                }
                cmd = '\0';
            }
            continue;
        }
        if (cmd == '\0') {
            SelectObject(dc, prev_pen);
            DeleteObject(pen);
            ReleaseDC(slot->hwnd, dc);
            return 0;
        }
        if (cmd == 'M' || cmd == 'm' || cmd == 'L' || cmd == 'l') {
            if (!native_ui_windows_path_parse_number(&cursor, &a) || !native_ui_windows_path_parse_number(&cursor, &b)) {
                SelectObject(dc, prev_pen);
                DeleteObject(pen);
                ReleaseDC(slot->hwnd, dc);
                return 0;
            }
            if (cmd == 'm' || cmd == 'l') {
                a += x;
                b += y;
            }
            if (cmd == 'M' || cmd == 'm') {
                x = a;
                y = b;
                start_x = x;
                start_y = y;
                has_point = 1;
                cmd = (cmd == 'm') ? 'l' : 'L';
            } else {
                MoveToEx(dc, (int)x, (int)y, NULL);
                (void)LineTo(dc, (int)a, (int)b);
                x = a;
                y = b;
                has_point = 1;
            }
            continue;
        }
        if (cmd == 'H' || cmd == 'h') {
            if (!native_ui_windows_path_parse_number(&cursor, &a)) {
                SelectObject(dc, prev_pen);
                DeleteObject(pen);
                ReleaseDC(slot->hwnd, dc);
                return 0;
            }
            a = (cmd == 'h') ? (x + a) : a;
            MoveToEx(dc, (int)x, (int)y, NULL);
            (void)LineTo(dc, (int)a, (int)y);
            x = a;
            has_point = 1;
            continue;
        }
        if (cmd == 'V' || cmd == 'v') {
            if (!native_ui_windows_path_parse_number(&cursor, &a)) {
                SelectObject(dc, prev_pen);
                DeleteObject(pen);
                ReleaseDC(slot->hwnd, dc);
                return 0;
            }
            a = (cmd == 'v') ? (y + a) : a;
            MoveToEx(dc, (int)x, (int)y, NULL);
            (void)LineTo(dc, (int)x, (int)a);
            y = a;
            has_point = 1;
            continue;
        }
        SelectObject(dc, prev_pen);
        DeleteObject(pen);
        ReleaseDC(slot->hwnd, dc);
        return 0;
    }
    SelectObject(dc, prev_pen);
    DeleteObject(pen);
    ReleaseDC(slot->hwnd, dc);
    return 1;
}

int native_host_ui_poll_event(int64_t handle, NativeHostUiEvent* out_event)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    MSG message;
    if (slot == NULL || out_event == NULL) {
        return 0;
    }
    while (PeekMessageW(&message, slot->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (slot->has_pending_event != 0) {
        *out_event = slot->pending_event;
        slot->has_pending_event = 0;
        return 1;
    }
    if (slot->close_pending != 0) {
        native_ui_windows_set_event_type(out_event, "closed");
        return 1;
    }
    native_ui_windows_set_event_type(out_event, "none");
    return 1;
}

int native_host_ui_get_window_size(int64_t handle, int* out_width, int* out_height)
{
    NativeUiWindowsSlot* slot = native_ui_windows_find_slot(handle);
    RECT rect;
    if (slot == NULL || slot->hwnd == NULL) {
        return 0;
    }
    if (GetClientRect(slot->hwnd, &rect)) {
        slot->width = rect.right - rect.left;
        slot->height = rect.bottom - rect.top;
    }
    if (out_width != NULL) {
        *out_width = slot->width;
    }
    if (out_height != NULL) {
        *out_height = slot->height;
    }
    return 1;
}

#else
#error "airun_ui_host_windows.c must only be compiled for windows targets. Use airun_ui_host_unavailable.c on unsupported targets."
#endif
