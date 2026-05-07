#include "airun_ui_host.h"

#ifdef __APPLE__

#import <AppKit/AppKit.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int64_t handle;
    NSWindow* window;
    id delegate_ref;
    id canvas_ref;
    int close_pending;
} NativeUiWindowSlot;

static NativeUiWindowSlot g_native_ui_windows[8];
static int64_t g_native_ui_next_handle = 1;
static int g_native_ui_app_initialized = 0;
static NativeHostUiEvent g_native_ui_event_queue[64];
static size_t g_native_ui_event_queue_start = 0U;
static size_t g_native_ui_event_queue_count = 0U;

@interface NativeUiCanvasView : NSView
@property(nonatomic, strong) NSImage* frameImage;
@end

@implementation NativeUiCanvasView
- (BOOL)isFlipped
{
    return NO;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [[NSColor whiteColor] setFill];
    NSRectFill([self bounds]);
    if (self.frameImage != nil) {
        [self.frameImage drawInRect:[self bounds]
                            fromRect:NSZeroRect
                           operation:NSCompositingOperationSourceOver
                            fraction:1.0];
    }
}
@end

static NSColor* native_ui_parse_color(const char* color, NSColor* fallback)
{
    unsigned int r;
    unsigned int g;
    unsigned int b;
    if (color == NULL || color[0] == '\0') {
        return fallback;
    }
    if (color[0] == '#') {
        if (sscanf(color + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            return [NSColor colorWithCalibratedRed:(CGFloat)r / 255.0
                                             green:(CGFloat)g / 255.0
                                              blue:(CGFloat)b / 255.0
                                             alpha:1.0];
        }
        return fallback;
    }
    if (strcasecmp(color, "white") == 0) {
        return [NSColor whiteColor];
    }
    if (strcasecmp(color, "black") == 0) {
        return [NSColor blackColor];
    }
    if (strcasecmp(color, "red") == 0) {
        return [NSColor redColor];
    }
    if (strcasecmp(color, "green") == 0) {
        return [NSColor greenColor];
    }
    if (strcasecmp(color, "blue") == 0) {
        return [NSColor blueColor];
    }
    return fallback;
}

static NativeUiWindowSlot* native_ui_find_slot(int64_t handle)
{
    size_t i;
    if (handle <= 0) {
        return NULL;
    }
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        if (g_native_ui_windows[i].handle == handle && g_native_ui_windows[i].window != nil) {
            return &g_native_ui_windows[i];
        }
    }
    return NULL;
}

static NativeUiWindowSlot* native_ui_find_empty_slot(void)
{
    size_t i;
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        if (g_native_ui_windows[i].window == nil) {
            return &g_native_ui_windows[i];
        }
    }
    return NULL;
}

static NativeUiCanvasView* native_ui_canvas_for_slot(NativeUiWindowSlot* slot)
{
    if (slot == NULL || slot->window == nil || slot->canvas_ref == nil) {
        return nil;
    }
    if (![slot->canvas_ref isKindOfClass:[NativeUiCanvasView class]]) {
        return nil;
    }
    return (NativeUiCanvasView*)slot->canvas_ref;
}

static int native_ui_ensure_frame_image(NativeUiCanvasView* canvas)
{
    NSRect bounds;
    NSSize size;
    if (canvas == nil) {
        return 0;
    }
    bounds = [canvas bounds];
    size = bounds.size;
    if (size.width < 1.0) {
        size.width = 1.0;
    }
    if (size.height < 1.0) {
        size.height = 1.0;
    }
    if (canvas.frameImage == nil ||
        canvas.frameImage.size.width != size.width ||
        canvas.frameImage.size.height != size.height) {
        NSImage* image = [[NSImage alloc] initWithSize:size];
        [image lockFocus];
        [[NSColor whiteColor] setFill];
        NSRectFill(NSMakeRect(0.0, 0.0, size.width, size.height));
        [image unlockFocus];
        canvas.frameImage = image;
    }
    return 1;
}

static int native_ui_lock_focus(NativeUiWindowSlot* slot, NSRect* out_bounds)
{
    NativeUiCanvasView* canvas = native_ui_canvas_for_slot(slot);
    if (canvas == nil || !native_ui_ensure_frame_image(canvas) || canvas.frameImage == nil) {
        return 0;
    }
    [canvas.frameImage lockFocus];
    if (out_bounds != NULL) {
        *out_bounds = NSMakeRect(0.0, 0.0, canvas.frameImage.size.width, canvas.frameImage.size.height);
    }
    return 1;
}

static void native_ui_unlock_focus(NativeUiWindowSlot* slot)
{
    NativeUiCanvasView* canvas = native_ui_canvas_for_slot(slot);
    if (canvas == nil || canvas.frameImage == nil) {
        return;
    }
    [canvas.frameImage unlockFocus];
}

static CGFloat native_ui_y_to_cocoa(NSRect bounds, int y_top)
{
    return NSMaxY(bounds) - (CGFloat)y_top;
}

static const char* native_ui_path_skip_separators(const char* cursor)
{
    while (cursor != NULL && *cursor != '\0') {
        if (!isspace((unsigned char)*cursor) && *cursor != ',') {
            break;
        }
        cursor += 1;
    }
    return cursor;
}

static int native_ui_path_parse_number(const char** io_cursor, double* out_number)
{
    char* end_ptr = NULL;
    const char* cursor;
    if (io_cursor == NULL || *io_cursor == NULL || out_number == NULL) {
        return 0;
    }
    cursor = native_ui_path_skip_separators(*io_cursor);
    if (cursor == NULL || *cursor == '\0') {
        return 0;
    }
    *out_number = strtod(cursor, &end_ptr);
    if (end_ptr == cursor) {
        return 0;
    }
    *io_cursor = end_ptr;
    return 1;
}

static int native_ui_draw_svg_path(NSRect bounds, const char* path_text, const char* color, int stroke_width)
{
    const char* cursor;
    char cmd = '\0';
    double x = 0.0;
    double y = 0.0;
    NSBezierPath* bezier;
    NSColor* stroke_color;
    if (path_text == NULL) {
        return 0;
    }
    bezier = [NSBezierPath bezierPath];
    cursor = path_text;
    while (cursor != NULL && *cursor != '\0') {
        double a = 0.0;
        double b = 0.0;
        cursor = native_ui_path_skip_separators(cursor);
        if (*cursor == '\0') {
            break;
        }
        if (isalpha((unsigned char)*cursor)) {
            cmd = *cursor;
            cursor += 1;
            continue;
        }
        if (cmd == '\0') {
            return 0;
        }
        if (cmd == 'M' || cmd == 'm' || cmd == 'L' || cmd == 'l') {
            if (!native_ui_path_parse_number(&cursor, &a) || !native_ui_path_parse_number(&cursor, &b)) {
                return 0;
            }
            if (cmd == 'm' || cmd == 'l') {
                x += a;
                y += b;
            } else {
                x = a;
                y = b;
            }
            if (cmd == 'M' || cmd == 'm') {
                [bezier moveToPoint:NSMakePoint((CGFloat)x, native_ui_y_to_cocoa(bounds, (int)lround(y)))];
                cmd = (cmd == 'm') ? 'l' : 'L';
            } else {
                [bezier lineToPoint:NSMakePoint((CGFloat)x, native_ui_y_to_cocoa(bounds, (int)lround(y)))];
            }
            continue;
        }
        if (cmd == 'H' || cmd == 'h') {
            if (!native_ui_path_parse_number(&cursor, &a)) {
                return 0;
            }
            x = (cmd == 'h') ? (x + a) : a;
            [bezier lineToPoint:NSMakePoint((CGFloat)x, native_ui_y_to_cocoa(bounds, (int)lround(y)))];
            continue;
        }
        if (cmd == 'V' || cmd == 'v') {
            if (!native_ui_path_parse_number(&cursor, &a)) {
                return 0;
            }
            y = (cmd == 'v') ? (y + a) : a;
            [bezier lineToPoint:NSMakePoint((CGFloat)x, native_ui_y_to_cocoa(bounds, (int)lround(y)))];
            continue;
        }
        if (cmd == 'Z' || cmd == 'z') {
            [bezier closePath];
            cmd = '\0';
            continue;
        }
        return 0;
    }
    stroke_color = native_ui_parse_color(color, [NSColor blackColor]);
    [stroke_color setStroke];
    [bezier setLineWidth:(stroke_width > 0) ? (CGFloat)stroke_width : 1.0];
    [bezier stroke];
    return 1;
}

static int native_ui_init_app(void)
{
    if (g_native_ui_app_initialized != 0) {
        return 1;
    }
    [NSApplication sharedApplication];
    if (NSApp == nil) {
        return 0;
    }
    (void)[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    (void)[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    g_native_ui_app_initialized = 1;
    return 1;
}

@interface NativeUiWindowDelegate : NSObject<NSWindowDelegate>
@property(nonatomic, assign) NativeUiWindowSlot* slot;
@end

@implementation NativeUiWindowDelegate
- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    if (self.slot != NULL) {
        self.slot->close_pending = 1;
    }
}
@end

static void native_ui_set_string(char* out, size_t out_capacity, const char* text)
{
    if (out == NULL || out_capacity == 0U) {
        return;
    }
    if (text == NULL) {
        out[0] = '\0';
        return;
    }
    (void)snprintf(out, out_capacity, "%s", text);
}

static const char* native_ui_normalize_key(NSEvent* event)
{
    unsigned short key_code;
    NSString* chars;
    if (event == nil) {
        return "";
    }
    key_code = [event keyCode];
    switch (key_code) {
        case 36:
        case 76:
            return "enter";
        case 48:
            return "tab";
        case 49:
            return "space";
        case 51:
            return "backspace";
        case 53:
            return "escape";
        case 123:
            return "left";
        case 124:
            return "right";
        case 125:
            return "down";
        case 126:
            return "up";
        case 117:
            return "delete";
        default:
            break;
    }
    chars = [event charactersIgnoringModifiers];
    if (chars == nil || [chars length] == 0U) {
        return "";
    }
    return [[chars lowercaseString] UTF8String];
}

static void native_ui_set_text_from_event(char* out, size_t out_capacity, NSEvent* event)
{
    NSString* chars;
    if (out == NULL || out_capacity == 0U) {
        return;
    }
    out[0] = '\0';
    if (event == nil) {
        return;
    }
    chars = [event characters];
    if (chars == nil || [chars length] == 0U) {
        return;
    }
    native_ui_set_string(out, out_capacity, [chars UTF8String]);
}

static int native_ui_is_text_input_event(NSEvent* event, const char* key_name, const char* text)
{
    NSUInteger flags;
    if (event == nil || text == NULL || text[0] == '\0') {
        return 0;
    }
    flags = [event modifierFlags];
    if ((flags & NSEventModifierFlagControl) != 0 || (flags & NSEventModifierFlagCommand) != 0) {
        return 0;
    }
    if (key_name == NULL || key_name[0] == '\0') {
        return 1;
    }
    return strcmp(key_name, "enter") != 0 &&
           strcmp(key_name, "tab") != 0 &&
           strcmp(key_name, "backspace") != 0 &&
           strcmp(key_name, "escape") != 0 &&
           strcmp(key_name, "left") != 0 &&
           strcmp(key_name, "right") != 0 &&
           strcmp(key_name, "up") != 0 &&
           strcmp(key_name, "down") != 0 &&
           strcmp(key_name, "delete") != 0;
}

static int native_ui_queue_push(const NativeHostUiEvent* event)
{
    size_t write_index;
    if (event == NULL) {
        return 0;
    }
    if (g_native_ui_event_queue_count >= (sizeof(g_native_ui_event_queue) / sizeof(g_native_ui_event_queue[0]))) {
        return 0;
    }
    write_index = (g_native_ui_event_queue_start + g_native_ui_event_queue_count) %
                  (sizeof(g_native_ui_event_queue) / sizeof(g_native_ui_event_queue[0]));
    g_native_ui_event_queue[write_index] = *event;
    g_native_ui_event_queue_count += 1U;
    return 1;
}

static int native_ui_queue_pop(NativeHostUiEvent* out_event)
{
    if (out_event == NULL || g_native_ui_event_queue_count == 0U) {
        return 0;
    }
    *out_event = g_native_ui_event_queue[g_native_ui_event_queue_start];
    g_native_ui_event_queue_start =
        (g_native_ui_event_queue_start + 1U) % (sizeof(g_native_ui_event_queue) / sizeof(g_native_ui_event_queue[0]));
    g_native_ui_event_queue_count -= 1U;
    return 1;
}

static void native_ui_queue_reset(void)
{
    g_native_ui_event_queue_start = 0U;
    g_native_ui_event_queue_count = 0U;
}

static int native_ui_translate_event(NativeUiWindowSlot* slot, NSEvent* event, NativeHostUiEvent* out_event, int* out_forward_to_appkit)
{
    NativeUiCanvasView* canvas;
    if (slot == NULL || event == nil || out_event == NULL || out_forward_to_appkit == NULL) {
        return 0;
    }
    memset(out_event, 0, sizeof(*out_event));
    out_event->x = -1;
    out_event->y = -1;
    *out_forward_to_appkit = 1;
    canvas = native_ui_canvas_for_slot(slot);
    switch ([event type]) {
        case NSEventTypeLeftMouseDown: {
            NSWindow* source_window = [event window];
            NSPoint location = [event locationInWindow];
            NSRect content_bounds;
            int y_top;
            if (source_window == nil) {
                source_window = slot->window;
            }
            if (canvas != nil) {
                content_bounds = [canvas bounds];
            } else {
                content_bounds = [[source_window contentView] bounds];
            }
            y_top = (int)llround(content_bounds.size.height - location.y);
            out_event->x = (int)llround(location.x);
            out_event->y = y_top < 0 ? 0 : y_top;
            native_ui_set_string(out_event->type, sizeof(out_event->type), "click");
            return 1;
        }
        case NSEventTypeKeyDown: {
            NSUInteger flags = [event modifierFlags];
            const char* key_name = native_ui_normalize_key(event);
            native_ui_set_text_from_event(out_event->text, sizeof(out_event->text), event);
            if (native_ui_is_text_input_event(event, key_name, out_event->text)) {
                native_ui_set_string(out_event->type, sizeof(out_event->type), "text");
            } else {
                native_ui_set_string(out_event->type, sizeof(out_event->type), "key");
                native_ui_set_string(out_event->key, sizeof(out_event->key), key_name);
            }
            out_event->repeat = [event isARepeat] ? 1 : 0;
            out_event->modifiers =
                ((flags & NSEventModifierFlagShift) != 0 ? 1 : 0) |
                ((flags & NSEventModifierFlagControl) != 0 ? 2 : 0) |
                ((flags & NSEventModifierFlagOption) != 0 ? 4 : 0) |
                ((flags & NSEventModifierFlagCommand) != 0 ? 8 : 0);
            *out_forward_to_appkit = 0;
            return 1;
        }
        default:
            return 0;
    }
}

static void native_ui_pump_events(NativeUiWindowSlot* slot)
{
    NSEvent* event;
    if (slot == NULL) {
        return;
    }
    while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES]) != nil) {
        NativeHostUiEvent translated;
        int forward_to_appkit = 1;
        if (native_ui_translate_event(slot, event, &translated, &forward_to_appkit)) {
            (void)native_ui_queue_push(&translated);
        }
        if (forward_to_appkit != 0) {
            [NSApp sendEvent:event];
        }
    }
    [NSApp updateWindows];
}

void native_host_ui_reset(void)
{
    size_t i;
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        g_native_ui_windows[i].handle = 0;
        g_native_ui_windows[i].window = nil;
        g_native_ui_windows[i].delegate_ref = nil;
        g_native_ui_windows[i].canvas_ref = nil;
        g_native_ui_windows[i].close_pending = 0;
    }
    g_native_ui_next_handle = 1;
    native_ui_queue_reset();
}

void native_host_ui_shutdown(void)
{
    size_t i;
    for (i = 0U; i < sizeof(g_native_ui_windows) / sizeof(g_native_ui_windows[0]); i += 1U) {
        if (g_native_ui_windows[i].window != nil) {
            [g_native_ui_windows[i].window close];
            g_native_ui_windows[i].window = nil;
            g_native_ui_windows[i].delegate_ref = nil;
            g_native_ui_windows[i].canvas_ref = nil;
            g_native_ui_windows[i].handle = 0;
            g_native_ui_windows[i].close_pending = 0;
        }
    }
    native_ui_queue_reset();
}

int native_host_ui_create_window(const char* title, int width, int height, int64_t* out_handle)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot;
        NativeUiCanvasView* canvas;
        NSRect frame;
        NSWindow* window;
        NativeUiWindowDelegate* delegate;
        NSString* window_title;
        if (out_handle == NULL || width <= 0 || height <= 0) {
            return 0;
        }
        *out_handle = 0;
        if (!native_ui_init_app()) {
            return 0;
        }
        slot = native_ui_find_empty_slot();
        if (slot == NULL) {
            return 0;
        }
        frame = NSMakeRect(120, 120, width, height);
        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSWindowStyleMaskTitled |
                                                        NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskResizable |
                                                        NSWindowStyleMaskMiniaturizable)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        if (window == nil) {
            return 0;
        }
        window_title = [NSString stringWithUTF8String:(title == NULL || title[0] == '\0') ? "AiLang" : title];
        [window setTitle:window_title];
        [window setReleasedWhenClosed:NO];
        canvas = [[NativeUiCanvasView alloc] initWithFrame:[[window contentView] bounds]];
        [canvas setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [window setContentView:canvas];
        delegate = [NativeUiWindowDelegate new];
        delegate.slot = slot;
        [window setDelegate:delegate];
        [window makeKeyAndOrderFront:nil];
        [NSApp updateWindows];
        slot->handle = g_native_ui_next_handle++;
        slot->window = window;
        slot->delegate_ref = delegate;
        slot->canvas_ref = canvas;
        slot->close_pending = 0;
        *out_handle = slot->handle;
        return 1;
    }
}

int native_host_ui_close_window(int64_t handle)
{
    NativeUiWindowSlot* slot = native_ui_find_slot(handle);
    if (slot == NULL) {
        return 0;
    }
    [slot->window close];
    slot->window = nil;
    slot->delegate_ref = nil;
    slot->canvas_ref = nil;
    slot->close_pending = 1;
    return 1;
}

int native_host_ui_begin_frame(int64_t handle)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NSRect bounds;
        if (slot == NULL) {
            return 0;
        }
        if (!native_ui_lock_focus(slot, &bounds)) {
            return 0;
        }
        [[NSColor whiteColor] setFill];
        NSRectFill(bounds);
        native_ui_unlock_focus(slot);
        return 1;
    }
}

int native_host_ui_end_frame(int64_t handle)
{
    return native_ui_find_slot(handle) != NULL ? 1 : 0;
}

int native_host_ui_present(int64_t handle)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NativeUiCanvasView* canvas;
        if (slot == NULL) {
            return 0;
        }
        canvas = native_ui_canvas_for_slot(slot);
        if (canvas != nil) {
            [canvas setNeedsDisplay:YES];
            [canvas displayIfNeeded];
        }
        [slot->window displayIfNeeded];
        [NSApp updateWindows];
        native_ui_pump_events(slot);
        return 1;
    }
}

int native_host_ui_wait_frame(int64_t handle)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        if (slot == NULL) {
            return 0;
        }
        native_ui_pump_events(slot);
        [NSThread sleepForTimeInterval:(1.0 / 60.0)];
        native_ui_pump_events(slot);
        return 1;
    }
}

int native_host_ui_draw_rect(int64_t handle, int x, int y, int width, int height, const char* color)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NSRect bounds;
        NSRect rect;
        if (slot == NULL) {
            return 0;
        }
        if (width <= 0 || height <= 0) {
            return 1;
        }
        if (!native_ui_lock_focus(slot, &bounds)) {
            return 0;
        }
        rect = NSMakeRect((CGFloat)x, native_ui_y_to_cocoa(bounds, y + height), (CGFloat)width, (CGFloat)height);
        [native_ui_parse_color(color, [NSColor blackColor]) setFill];
        NSRectFill(rect);
        native_ui_unlock_focus(slot);
        return 1;
    }
}

int native_host_ui_draw_ellipse(int64_t handle, int x, int y, int width, int height, const char* color)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NSRect bounds;
        NSRect rect;
        NSBezierPath* path;
        if (slot == NULL) {
            return 0;
        }
        if (width <= 0 || height <= 0) {
            return 1;
        }
        if (!native_ui_lock_focus(slot, &bounds)) {
            return 0;
        }
        rect = NSMakeRect((CGFloat)x, native_ui_y_to_cocoa(bounds, y + height), (CGFloat)width, (CGFloat)height);
        path = [NSBezierPath bezierPathWithOvalInRect:rect];
        [native_ui_parse_color(color, [NSColor blackColor]) setFill];
        [path fill];
        native_ui_unlock_focus(slot);
        return 1;
    }
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
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NSRect bounds;
        NSRect rect;
        NSBitmapImageRep* bitmap;
        NSImage* image;
        unsigned char* bitmap_data;
        if (slot == NULL || rgba == NULL) {
            return 0;
        }
        if (width <= 0 || height <= 0) {
            return 1;
        }
        if (rgba_length != (size_t)width * (size_t)height * 4U) {
            return 0;
        }
        if (!native_ui_lock_focus(slot, &bounds)) {
            return 0;
        }
        bitmap = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
                          pixelsWide:width
                          pixelsHigh:height
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                          bytesPerRow:width * 4
                         bitsPerPixel:32];
        if (bitmap == nil) {
            native_ui_unlock_focus(slot);
            return 0;
        }
        bitmap_data = [bitmap bitmapData];
        if (bitmap_data == NULL) {
            native_ui_unlock_focus(slot);
            return 0;
        }
        memcpy(bitmap_data, rgba, rgba_length);
        image = [[NSImage alloc] initWithSize:NSMakeSize((CGFloat)width, (CGFloat)height)];
        if (image == nil) {
            native_ui_unlock_focus(slot);
            return 0;
        }
        [image addRepresentation:bitmap];
        rect = NSMakeRect((CGFloat)x, native_ui_y_to_cocoa(bounds, y + height), (CGFloat)width, (CGFloat)height);
        [image drawInRect:rect
                 fromRect:NSZeroRect
                operation:NSCompositingOperationSourceOver
                 fraction:1.0];
        native_ui_unlock_focus(slot);
        return 1;
    }
}

int native_host_ui_draw_text(int64_t handle, int x, int y, const char* text, const char* color, int font_size)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NSRect bounds;
        NSString* ns_text;
        NSDictionary* attrs;
        NSFont* font;
        if (slot == NULL || text == NULL) {
            return 0;
        }
        if (!native_ui_lock_focus(slot, &bounds)) {
            return 0;
        }
        ns_text = [NSString stringWithUTF8String:text];
        if (ns_text == nil) {
            native_ui_unlock_focus(slot);
            return 0;
        }
        font = [NSFont systemFontOfSize:(font_size > 0) ? (CGFloat)font_size : 12.0];
        attrs = @{ NSForegroundColorAttributeName: native_ui_parse_color(color, [NSColor blackColor]), NSFontAttributeName: font };
        [ns_text drawAtPoint:NSMakePoint((CGFloat)x, native_ui_y_to_cocoa(bounds, y + ((font_size > 0) ? font_size : 12)))
              withAttributes:attrs];
        native_ui_unlock_focus(slot);
        return 1;
    }
}

int native_host_ui_draw_line(int64_t handle, int x1, int y1, int x2, int y2, const char* color, int stroke_width)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NSRect bounds;
        NSBezierPath* path;
        if (slot == NULL) {
            return 0;
        }
        if (!native_ui_lock_focus(slot, &bounds)) {
            return 0;
        }
        path = [NSBezierPath bezierPath];
        [path moveToPoint:NSMakePoint((CGFloat)x1, native_ui_y_to_cocoa(bounds, y1))];
        [path lineToPoint:NSMakePoint((CGFloat)x2, native_ui_y_to_cocoa(bounds, y2))];
        [path setLineWidth:(stroke_width > 0) ? (CGFloat)stroke_width : 1.0];
        [native_ui_parse_color(color, [NSColor blackColor]) setStroke];
        [path stroke];
        native_ui_unlock_focus(slot);
        return 1;
    }
}

int native_host_ui_draw_path(int64_t handle, const char* path, const char* color, int stroke_width)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NSRect bounds;
        int ok;
        if (slot == NULL) {
            return 0;
        }
        if (!native_ui_lock_focus(slot, &bounds)) {
            return 0;
        }
        ok = native_ui_draw_svg_path(bounds, path, color, stroke_width);
        native_ui_unlock_focus(slot);
        return ok;
    }
}

int native_host_ui_poll_event(int64_t handle, NativeHostUiEvent* out_event)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        if (slot == NULL || out_event == NULL) {
            return 0;
        }
        memset(out_event, 0, sizeof(*out_event));
        native_ui_set_string(out_event->type, sizeof(out_event->type), "none");
        out_event->x = -1;
        out_event->y = -1;
        if (slot->close_pending != 0) {
            native_ui_set_string(out_event->type, sizeof(out_event->type), "closed");
            slot->close_pending = 0;
            return 1;
        }
        if (native_ui_queue_pop(out_event)) {
            return 1;
        }
        native_ui_pump_events(slot);
        if (slot->close_pending != 0) {
            native_ui_set_string(out_event->type, sizeof(out_event->type), "closed");
            slot->close_pending = 0;
            return 1;
        }
        if (native_ui_queue_pop(out_event)) {
            return 1;
        }
        return 1;
    }
}

int native_host_ui_get_window_size(int64_t handle, int* out_width, int* out_height)
{
    @autoreleasepool {
        NativeUiWindowSlot* slot = native_ui_find_slot(handle);
        NativeUiCanvasView* canvas;
        NSRect content_bounds;
        if (slot == NULL || out_width == NULL || out_height == NULL) {
            return 0;
        }
        canvas = native_ui_canvas_for_slot(slot);
        if (canvas == nil) {
            return 0;
        }
        content_bounds = [canvas bounds];
        *out_width = (int)llround(content_bounds.size.width);
        *out_height = (int)llround(content_bounds.size.height);
        return 1;
    }
}

#endif
