static AivmVm* g_native_active_vm = NULL;
static char g_native_ui_event_type[16] = "none";
static char g_native_ui_event_key[48] = "";
static char g_native_ui_event_text[128] = "";
static char g_native_ui_event_target_id[48] = "";
static int64_t g_native_ui_active_window_handles[8];
static const char* g_native_debug_mode = "off";
static int g_native_scene_capture_enabled = 0;
static FILE* g_native_scene_capture_file = NULL;
static int64_t g_native_scene_frame_id = 0;
static int64_t g_native_scene_frame_node_count = 0;
static int g_native_scene_frame_open = 0;

static void native_scene_capture_close(void)
{
    if (g_native_scene_capture_file != NULL) {
        fclose(g_native_scene_capture_file);
        g_native_scene_capture_file = NULL;
    }
}

static void native_scene_capture_reset(void)
{
    g_native_scene_capture_enabled = 0;
    g_native_scene_frame_id = 0;
    g_native_scene_frame_node_count = 0;
    g_native_scene_frame_open = 0;
    native_scene_capture_close();
}

static int native_scene_capture_escape(const char* text, char* out, size_t out_capacity)
{
    size_t i = 0U;
    size_t j = 0U;
    if (out == NULL || out_capacity == 0U) {
        return 0;
    }
    if (text == NULL) {
        out[0] = '\0';
        return 1;
    }
    while (text[i] != '\0' && j + 1U < out_capacity) {
        char ch = text[i];
        if ((ch == '\\' || ch == '"') && j + 2U < out_capacity) {
            out[j++] = '\\';
            out[j++] = ch;
        } else if (ch == '\n' && j + 2U < out_capacity) {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (ch == '\r' && j + 2U < out_capacity) {
            out[j++] = '\\';
            out[j++] = 'r';
        } else {
            out[j++] = ch;
        }
        i += 1U;
    }
    out[j] = '\0';
    return 1;
}

static void native_scene_capture_emit_line(const char* line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }
    fprintf(stdout, "%s\n", line);
    fflush(stdout);
    if (g_native_scene_capture_file != NULL) {
        fprintf(g_native_scene_capture_file, "%s\n", line);
        fflush(g_native_scene_capture_file);
    }
}

static void native_scene_capture_begin_frame(int64_t handle, int width, int height)
{
    char line[256];
    if (!g_native_scene_capture_enabled) {
        return;
    }
    if (width < 64) {
        width = 800;
    }
    if (height < 64) {
        height = 600;
    }
    g_native_scene_frame_id += 1;
    g_native_scene_frame_node_count = 0;
    g_native_scene_frame_open = 1;
    (void)snprintf(
        line,
        sizeof(line),
        "aivectra.frame = { id=%lld, width=%d, height=%d, hash=\"native-%lld-%lld\" }",
        (long long)g_native_scene_frame_id,
        width,
        height,
        (long long)handle,
        (long long)g_native_scene_frame_id);
    native_scene_capture_emit_line(line);
}

static void native_scene_capture_end_frame(void)
{
    char line[128];
    if (!g_native_scene_capture_enabled || !g_native_scene_frame_open) {
        return;
    }
    (void)snprintf(
        line,
        sizeof(line),
        "aivectra.frame_end = { id=%lld, node_count=%lld }",
        (long long)g_native_scene_frame_id,
        (long long)g_native_scene_frame_node_count);
    native_scene_capture_emit_line(line);
    g_native_scene_frame_open = 0;
}

static void native_scene_capture_emit_node(
    const char* kind,
    const char* fill,
    const char* stroke,
    int stroke_width,
    const char* text_value,
    const char* path_value,
    int font_size,
    int x,
    int y,
    int w,
    int h)
{
    char line[1536];
    char id[64];
    char fill_escaped[128];
    char stroke_escaped[128];
    char text_escaped[512];
    char path_escaped[512];
    if (!g_native_scene_capture_enabled || !g_native_scene_frame_open || kind == NULL) {
        return;
    }
    g_native_scene_frame_node_count += 1;
    (void)snprintf(
        id,
        sizeof(id),
        "n%lld_%lld",
        (long long)g_native_scene_frame_id,
        (long long)g_native_scene_frame_node_count);
    (void)native_scene_capture_escape(fill == NULL ? "" : fill, fill_escaped, sizeof(fill_escaped));
    (void)native_scene_capture_escape(stroke == NULL ? "" : stroke, stroke_escaped, sizeof(stroke_escaped));
    (void)native_scene_capture_escape(text_value == NULL ? "" : text_value, text_escaped, sizeof(text_escaped));
    (void)native_scene_capture_escape(path_value == NULL ? "" : path_value, path_escaped, sizeof(path_escaped));
    (void)snprintf(
        line,
        sizeof(line),
        "aivectra.node = { frame=%lld, order=%lld, id=\"%s\", parent=\"\", kind=\"%s\", transform=\"matrix(1,0,0,1,0,0)\", fill=\"%s\", stroke=\"%s\", stroke_width=%d, text=\"%s\", path=\"%s\", font_size=%d, x=%d, y=%d, w=%d, h=%d }",
        (long long)g_native_scene_frame_id,
        (long long)g_native_scene_frame_node_count,
        id,
        kind,
        fill_escaped,
        stroke_escaped,
        stroke_width,
        text_escaped,
        path_escaped,
        font_size,
        x,
        y,
        w,
        h);
    native_scene_capture_emit_line(line);
}

static void native_scene_capture_configure(const char* debug_mode, const char* out_dir)
{
    char scene_log_path[PATH_MAX];
    native_scene_capture_reset();
    g_native_debug_mode = "off";
    if (debug_mode == NULL || debug_mode[0] == '\0') {
        return;
    }
    g_native_debug_mode = debug_mode;
    if (!starts_with(debug_mode, "scene")) {
        return;
    }
    g_native_scene_capture_enabled = 1;
    if (out_dir == NULL || out_dir[0] == '\0') {
        return;
    }
    if (!ensure_directory_recursive(out_dir)) {
        g_native_scene_capture_enabled = 0;
        return;
    }
    if (!join_path(out_dir, "scene.log", scene_log_path, sizeof(scene_log_path))) {
        g_native_scene_capture_enabled = 0;
        return;
    }
    g_native_scene_capture_file = fopen(scene_log_path, "wb");
    if (g_native_scene_capture_file == NULL) {
        g_native_scene_capture_enabled = 0;
    }
}

enum {
    NATIVE_UI_EVENT_ATTR_TYPE = 0,
    NATIVE_UI_EVENT_ATTR_X = 1,
    NATIVE_UI_EVENT_ATTR_Y = 2,
    NATIVE_UI_EVENT_ATTR_KEY = 3,
    NATIVE_UI_EVENT_ATTR_TEXT = 4,
    NATIVE_UI_EVENT_ATTR_TARGET_ID = 5,
    NATIVE_UI_EVENT_ATTR_MODIFIERS = 6,
    NATIVE_UI_EVENT_ATTR_REPEAT = 7
};

static int native_vm_append_host_node(
    AivmVm* vm,
    const char* kind,
    const char* id,
    const AivmNodeAttr* attrs,
    size_t attr_count,
    int64_t* out_handle)
{
    AivmNodeRecord* node;
    size_t i;
    size_t node_index;
    if (vm == NULL || kind == NULL || id == NULL || out_handle == NULL) {
        return 0;
    }
    if (vm->node_count >= AIVM_VM_NODE_CAPACITY || vm->node_attr_count + attr_count > AIVM_VM_NODE_ATTR_CAPACITY) {
        return 0;
    }
    node_index = vm->node_count;
    node = &vm->nodes[node_index];
    node->kind = kind;
    node->id = id;
    node->attr_start = vm->node_attr_count;
    node->attr_count = attr_count;
    node->child_start = vm->node_child_count;
    node->child_count = 0U;
    for (i = 0U; i < attr_count; i += 1U) {
        vm->node_attrs[vm->node_attr_count + i] = attrs[i];
    }
    vm->node_count += 1U;
    vm->node_attr_count += attr_count;
    if (vm->node_count > vm->node_high_water) {
        vm->node_high_water = vm->node_count;
    }
    if (vm->node_attr_count > vm->node_attr_high_water) {
        vm->node_attr_high_water = vm->node_attr_count;
    }
    *out_handle = (int64_t)vm->node_count;
    return 1;
}

static void native_ui_runtime_reset_handles(void)
{
    memset(g_native_ui_active_window_handles, 0, sizeof(g_native_ui_active_window_handles));
}

static int native_ui_runtime_is_active_handle(int64_t handle)
{
    size_t i;
    if (handle <= 0) {
        return 0;
    }
    for (i = 0U; i < sizeof(g_native_ui_active_window_handles) / sizeof(g_native_ui_active_window_handles[0]); i += 1U) {
        if (g_native_ui_active_window_handles[i] == handle) {
            return 1;
        }
    }
    return 0;
}

static int native_ui_runtime_register_handle(int64_t handle)
{
    size_t i;
    if (handle <= 0) {
        return 0;
    }
    for (i = 0U; i < sizeof(g_native_ui_active_window_handles) / sizeof(g_native_ui_active_window_handles[0]); i += 1U) {
        if (g_native_ui_active_window_handles[i] == handle) {
            return 1;
        }
    }
    for (i = 0U; i < sizeof(g_native_ui_active_window_handles) / sizeof(g_native_ui_active_window_handles[0]); i += 1U) {
        if (g_native_ui_active_window_handles[i] == 0) {
            g_native_ui_active_window_handles[i] = handle;
            return 1;
        }
    }
    return 0;
}

static void native_ui_runtime_unregister_handle(int64_t handle)
{
    size_t i;
    if (handle <= 0) {
        return;
    }
    for (i = 0U; i < sizeof(g_native_ui_active_window_handles) / sizeof(g_native_ui_active_window_handles[0]); i += 1U) {
        if (g_native_ui_active_window_handles[i] == handle) {
            g_native_ui_active_window_handles[i] = 0;
            return;
        }
    }
}

static int native_ui_ensure_event_node(AivmVm* vm, int64_t* out_handle)
{
    AivmNodeAttr attrs[8];
    if (out_handle == NULL) {
        return 0;
    }
    if (vm->ui_empty_event_node_handle > 0 &&
        vm->ui_empty_event_node_handle <= (int64_t)vm->node_count) {
        *out_handle = vm->ui_empty_event_node_handle;
        return 1;
    }
    attrs[0].key = "type";
    attrs[0].kind = AIVM_NODE_ATTR_STRING;
    attrs[0].string_value = g_native_ui_event_type;
    attrs[1].key = "x";
    attrs[1].kind = AIVM_NODE_ATTR_INT;
    attrs[1].int_value = 0;
    attrs[2].key = "y";
    attrs[2].kind = AIVM_NODE_ATTR_INT;
    attrs[2].int_value = 0;
    attrs[3].key = "key";
    attrs[3].kind = AIVM_NODE_ATTR_STRING;
    attrs[3].string_value = g_native_ui_event_key;
    attrs[4].key = "text";
    attrs[4].kind = AIVM_NODE_ATTR_STRING;
    attrs[4].string_value = g_native_ui_event_text;
    attrs[5].key = "targetId";
    attrs[5].kind = AIVM_NODE_ATTR_STRING;
    attrs[5].string_value = g_native_ui_event_target_id;
    attrs[6].key = "modifiers";
    attrs[6].kind = AIVM_NODE_ATTR_INT;
    attrs[6].int_value = 0;
    attrs[7].key = "repeat";
    attrs[7].kind = AIVM_NODE_ATTR_BOOL;
    attrs[7].bool_value = 0;
    if (!native_vm_append_host_node(vm, "Map", "ui_event", attrs, 8U, &vm->ui_empty_event_node_handle)) {
        return 0;
    }
    *out_handle = vm->ui_empty_event_node_handle;
    return 1;
}

static int native_ui_ensure_size_node(AivmVm* vm, int64_t* out_handle)
{
    AivmNodeAttr attrs[2];
    if (out_handle == NULL) {
        return 0;
    }
    if (vm->ui_default_window_size_node_handle > 0 &&
        vm->ui_default_window_size_node_handle <= (int64_t)vm->node_count) {
        *out_handle = vm->ui_default_window_size_node_handle;
        return 1;
    }
    attrs[0].key = "width";
    attrs[0].kind = AIVM_NODE_ATTR_INT;
    attrs[0].int_value = 0;
    attrs[1].key = "height";
    attrs[1].kind = AIVM_NODE_ATTR_INT;
    attrs[1].int_value = 0;
    if (!native_vm_append_host_node(vm, "Map", "ui_size", attrs, 2U, &vm->ui_default_window_size_node_handle)) {
        return 0;
    }
    *out_handle = vm->ui_default_window_size_node_handle;
    return 1;
}

static void native_ui_update_size_node(AivmVm* vm, int width, int height)
{
    const AivmNodeRecord* node;
    if (vm == NULL || vm->ui_default_window_size_node_handle <= 0 ||
        vm->ui_default_window_size_node_handle > (int64_t)vm->node_count) {
        return;
    }
    node = &vm->nodes[(size_t)(vm->ui_default_window_size_node_handle - 1)];
    if (node->attr_count < 2U || node->attr_start + 1U >= vm->node_attr_count) {
        return;
    }
    vm->node_attrs[node->attr_start].int_value = width;
    vm->node_attrs[node->attr_start + 1U].int_value = height;
}

static void native_ui_update_event_node(AivmVm* vm, const NativeHostUiEvent* event)
{
    const AivmNodeRecord* node;
    size_t base;
    if (vm == NULL || event == NULL ||
        vm->ui_empty_event_node_handle <= 0 ||
        vm->ui_empty_event_node_handle > (int64_t)vm->node_count) {
        return;
    }
    (void)snprintf(g_native_ui_event_type, sizeof(g_native_ui_event_type), "%s", event->type);
    (void)snprintf(g_native_ui_event_key, sizeof(g_native_ui_event_key), "%s", event->key);
    (void)snprintf(g_native_ui_event_text, sizeof(g_native_ui_event_text), "%s", event->text);
    (void)snprintf(g_native_ui_event_target_id, sizeof(g_native_ui_event_target_id), "%s", event->target_id);
    node = &vm->nodes[(size_t)(vm->ui_empty_event_node_handle - 1)];
    if (node->attr_count < 8U || node->attr_start + 7U >= vm->node_attr_count) {
        return;
    }
    base = node->attr_start;
    vm->node_attrs[base + NATIVE_UI_EVENT_ATTR_TYPE].string_value = g_native_ui_event_type;
    vm->node_attrs[base + NATIVE_UI_EVENT_ATTR_X].int_value = event->x;
    vm->node_attrs[base + NATIVE_UI_EVENT_ATTR_Y].int_value = event->y;
    vm->node_attrs[base + NATIVE_UI_EVENT_ATTR_KEY].string_value = g_native_ui_event_key;
    vm->node_attrs[base + NATIVE_UI_EVENT_ATTR_TEXT].string_value = g_native_ui_event_text;
    vm->node_attrs[base + NATIVE_UI_EVENT_ATTR_TARGET_ID].string_value = g_native_ui_event_target_id;
    vm->node_attrs[base + NATIVE_UI_EVENT_ATTR_MODIFIERS].int_value = event->modifiers;
    vm->node_attrs[base + NATIVE_UI_EVENT_ATTR_REPEAT].bool_value = event->repeat ? 1 : 0;
}

static int native_syscall_ui_create_window(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 3U ||
        args[0].type != AIVM_VAL_STRING ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    {
        int64_t handle = 0;
        if (!native_host_ui_create_window(args[0].string_value, (int)args[1].int_value, (int)args[2].int_value, &handle)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        if (!native_ui_runtime_register_handle(handle)) {
            (void)native_host_ui_close_window(handle);
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        airun_log_message(AIRUN_LOG_INFO, "ui", "create-window title=%s width=%lld height=%lld handle=%lld",
            args[0].string_value,
            (long long)args[1].int_value,
            (long long)args[2].int_value,
            (long long)handle);
        *result = aivm_value_int(handle);
    }
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_void_1(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (strcmp(target, "sys.ui.closeWindow") == 0) {
        if (!native_host_ui_close_window(args[0].int_value)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        airun_log_message(AIRUN_LOG_INFO, "ui", "close-window handle=%lld", (long long)args[0].int_value);
        native_ui_runtime_unregister_handle(args[0].int_value);
    } else if (strcmp(target, "sys.ui.waitFrame") == 0) {
        if (!native_host_ui_wait_frame(args[0].int_value)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        airun_log_message(AIRUN_LOG_TRACE, "ui", "wait-frame handle=%lld", (long long)args[0].int_value);
    } else if (strcmp(target, "sys.ui.beginFrame") == 0) {
        if (!native_host_ui_begin_frame(args[0].int_value)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        airun_log_message(AIRUN_LOG_TRACE, "ui", "begin-frame handle=%lld", (long long)args[0].int_value);
        {
            int width = 0;
            int height = 0;
            if (native_host_ui_get_window_size(args[0].int_value, &width, &height)) {
                native_scene_capture_begin_frame(args[0].int_value, width, height);
            } else {
                native_scene_capture_begin_frame(args[0].int_value, 800, 600);
            }
        }
    } else if (strcmp(target, "sys.ui.endFrame") == 0) {
        if (!native_host_ui_end_frame(args[0].int_value)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        airun_log_message(AIRUN_LOG_TRACE, "ui", "end-frame handle=%lld", (long long)args[0].int_value);
        native_scene_capture_end_frame();
    } else if (strcmp(target, "sys.ui.present") == 0) {
        if (!native_host_ui_present(args[0].int_value)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        airun_log_message(AIRUN_LOG_TRACE, "ui", "present handle=%lld", (long long)args[0].int_value);
    } else {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_rect(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || strcmp(target, "sys.ui.drawRect") != 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    if (args == NULL || arg_count != 6U ||
        args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_INT || args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_INT || args[4].type != AIVM_VAL_INT || args[5].type != AIVM_VAL_STRING) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_host_ui_draw_rect(
            args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            (int)args[3].int_value,
            (int)args[4].int_value,
            args[5].string_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    native_scene_capture_emit_node(
        "Rect",
        args[5].string_value,
        "",
        0,
        "",
        "",
        0,
        (int)args[1].int_value,
        (int)args[2].int_value,
        (int)args[3].int_value,
        (int)args[4].int_value);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_ellipse(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || strcmp(target, "sys.ui.drawEllipse") != 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    if (args == NULL || arg_count != 6U ||
        args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_INT || args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_INT || args[4].type != AIVM_VAL_INT || args[5].type != AIVM_VAL_STRING) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_host_ui_draw_ellipse(
            args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            (int)args[3].int_value,
            (int)args[4].int_value,
            args[5].string_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    native_scene_capture_emit_node(
        "Ellipse",
        args[5].string_value,
        "",
        0,
        "",
        "",
        0,
        (int)args[1].int_value,
        (int)args[2].int_value,
        (int)args[3].int_value,
        (int)args[4].int_value);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_image(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    uint8_t* rgba = NULL;
    size_t rgba_length = 0U;
    size_t expected_length = 0U;
    int width;
    int height;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || strcmp(target, "sys.ui.drawImage") != 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    if (args == NULL || arg_count != 6U ||
        args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_INT || args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_INT || args[4].type != AIVM_VAL_INT || args[5].type != AIVM_VAL_STRING) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    width = (int)args[3].int_value;
    height = (int)args[4].int_value;
    if (width <= 0 || height <= 0) {
        native_scene_capture_emit_node(
            "Image",
            "",
            "",
            0,
            "",
            "",
            0,
            (int)args[1].int_value,
            (int)args[2].int_value,
            width,
            height);
        *result = aivm_value_void();
        return AIVM_SYSCALL_OK;
    }
    if ((size_t)width > (SIZE_MAX / (size_t)height) ||
        ((size_t)width * (size_t)height) > (SIZE_MAX / 4U)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    expected_length = (size_t)width * (size_t)height * 4U;
    if (!native_bytes_from_base64(args[5].string_value, NULL, 0U, &rgba_length) ||
        rgba_length != expected_length) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    rgba = (uint8_t*)malloc(rgba_length > 0U ? rgba_length : 1U);
    if (rgba == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_bytes_from_base64(args[5].string_value, rgba, rgba_length, &rgba_length) ||
        rgba_length != expected_length ||
        !native_host_ui_draw_image(
            args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            width,
            height,
            rgba,
            rgba_length)) {
        free(rgba);
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    free(rgba);
    native_scene_capture_emit_node(
        "Image",
        "",
        "",
        0,
        "",
        "",
        0,
        (int)args[1].int_value,
        (int)args[2].int_value,
        width,
        height);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_text(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || strcmp(target, "sys.ui.drawText") != 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    if (args == NULL || arg_count != 6U ||
        args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_INT || args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_STRING || args[4].type != AIVM_VAL_STRING || args[5].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_host_ui_draw_text(
            args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            args[3].string_value,
            args[4].string_value,
            (int)args[5].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    native_scene_capture_emit_node(
        "Text",
        args[4].string_value,
        "",
        0,
        args[3].string_value,
        "",
        (int)args[5].int_value,
        (int)args[1].int_value,
        (int)args[2].int_value,
        0,
        0);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || strcmp(target, "sys.ui.drawLine") != 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    if (args == NULL || arg_count != 7U ||
        args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_INT || args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_INT || args[4].type != AIVM_VAL_INT || args[5].type != AIVM_VAL_STRING ||
        args[6].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_host_ui_draw_line(
            args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            (int)args[3].int_value,
            (int)args[4].int_value,
            args[5].string_value,
            (int)args[6].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    {
        char path_text[128];
        (void)snprintf(
            path_text,
            sizeof(path_text),
            "M%d,%d L%d,%d",
            (int)args[1].int_value,
            (int)args[2].int_value,
            (int)args[3].int_value,
            (int)args[4].int_value);
        native_scene_capture_emit_node(
            "Path",
            "",
            args[5].string_value,
            (int)args[6].int_value,
            "",
            path_text,
            0,
            0,
            0,
            0,
            0);
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_path(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || strcmp(target, "sys.ui.drawPath") != 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    if (args == NULL || arg_count != 4U ||
        args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_STRING ||
        args[2].type != AIVM_VAL_STRING || args[3].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_host_ui_draw_path(
            args[0].int_value,
            args[1].string_value,
            args[2].string_value,
            (int)args[3].int_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    native_scene_capture_emit_node(
        "Path",
        "",
        args[2].string_value,
        (int)args[3].int_value,
        "",
        args[1].string_value,
        0,
        0,
        0,
        0,
        0);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_poll_event(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || strcmp(target, "sys.ui.pollEvent") != 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        if (g_native_active_vm != NULL) {
            (void)snprintf(
                g_native_active_vm->error_detail_storage,
                sizeof(g_native_active_vm->error_detail_storage),
                "AIVMS001: sys.ui.pollEvent invalid window handle=%lld active=false",
                (long long)args[0].int_value);
            g_native_active_vm->error_detail = g_native_active_vm->error_detail_storage;
        }
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    {
        NativeHostUiEvent event;
        int64_t node_handle;
        if (g_native_active_vm == NULL || !native_ui_ensure_event_node(g_native_active_vm, &node_handle)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        memset(&event, 0, sizeof(event));
        (void)snprintf(event.type, sizeof(event.type), "none");
        airun_interact_drain_stdin();
        if (airun_pop_injected_event(&event)) {
            airun_log_message(
                AIRUN_LOG_INFO,
                "debug",
                "inject-event handle=%lld type=%s x=%d y=%d key=%s text=%s",
                (long long)args[0].int_value,
                event.type,
                event.x,
                event.y,
                event.key,
                event.text);
        } else if (!native_host_ui_poll_event(args[0].int_value, &event)) {
            if (g_native_active_vm != NULL) {
                (void)snprintf(
                    g_native_active_vm->error_detail_storage,
                    sizeof(g_native_active_vm->error_detail_storage),
                    "AIVMS001: sys.ui.pollEvent host poll failed handle=%lld",
                    (long long)args[0].int_value);
                g_native_active_vm->error_detail = g_native_active_vm->error_detail_storage;
            }
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        native_ui_update_event_node(g_native_active_vm, &event);
        airun_record_captured_event(&event);
        if (strcmp(event.type, "none") != 0) {
            airun_log_message(AIRUN_LOG_INFO, "ui", "poll-event handle=%lld type=%s x=%d y=%d key=%s text=%s target=%s",
                (long long)args[0].int_value,
                event.type,
                event.x,
                event.y,
                event.key,
                event.text,
                event.target_id);
        } else {
            airun_log_message(AIRUN_LOG_TRACE, "ui", "poll-event handle=%lld type=none", (long long)args[0].int_value);
        }
        *result = aivm_value_node(node_handle);
    }
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_get_window_size(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || strcmp(target, "sys.ui.getWindowSize") != 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_ui_runtime_is_active_handle(args[0].int_value)) {
        if (g_native_active_vm != NULL) {
            (void)snprintf(
                g_native_active_vm->error_detail_storage,
                sizeof(g_native_active_vm->error_detail_storage),
                "AIVMS001: sys.ui.getWindowSize invalid window handle=%lld active=false",
                (long long)args[0].int_value);
            g_native_active_vm->error_detail = g_native_active_vm->error_detail_storage;
        }
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (g_native_active_vm == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    {
        int width = 0;
        int height = 0;
        int64_t node_handle;
        if (!native_ui_ensure_size_node(g_native_active_vm, &node_handle)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        if (!native_host_ui_get_window_size(args[0].int_value, &width, &height)) {
            (void)snprintf(
                g_native_active_vm->error_detail_storage,
                sizeof(g_native_active_vm->error_detail_storage),
                "AIVMS001: sys.ui.getWindowSize host size failed handle=%lld",
                (long long)args[0].int_value);
            g_native_active_vm->error_detail = g_native_active_vm->error_detail_storage;
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        native_ui_update_size_node(g_native_active_vm, width, height);
        airun_log_message(AIRUN_LOG_TRACE, "ui", "window-size handle=%lld width=%d height=%d",
            (long long)args[0].int_value,
            width,
            height);
        *result = aivm_value_node(node_handle);
    }
    return AIVM_SYSCALL_OK;
}

