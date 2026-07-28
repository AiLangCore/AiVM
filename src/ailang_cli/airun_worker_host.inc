typedef struct NativeWorkerLocalHeap
{
    char* task_name;
    char* payload;
    char* result;
    char* error;
    size_t allocated_bytes;
} NativeWorkerLocalHeap;

typedef struct NativeWorkerState
{
    int used;
    int status; /* 0 pending, 1 success, -1 failure, -2 canceled */
    int completion_status;
    int64_t polls_remaining;
    NativeWorkerLocalHeap* heap;
} NativeWorkerState;

static NativeWorkerState g_native_workers[NATIVE_WORKER_CAPACITY];

static void native_worker_local_heap_free(NativeWorkerLocalHeap* heap)
{
    if (heap == NULL) {
        return;
    }
    free(heap->task_name);
    free(heap->payload);
    free(heap->result);
    free(heap->error);
    free(heap);
}

static void native_worker_clear_local_heap(NativeWorkerState* worker)
{
    if (worker == NULL) {
        return;
    }
    native_worker_local_heap_free(worker->heap);
    worker->heap = NULL;
}

static void native_worker_init_slot(NativeWorkerState* worker)
{
    if (worker == NULL) {
        return;
    }
    native_worker_clear_local_heap(worker);
    memset(worker, 0, sizeof(*worker));
    worker->status = 0;
    worker->completion_status = 1;
}

static void native_worker_reset_all(void)
{
    size_t index;
    for (index = 0U; index < NATIVE_WORKER_CAPACITY; index += 1U) {
        native_worker_init_slot(&g_native_workers[index]);
    }
}

static int native_worker_heap_set_text(NativeWorkerLocalHeap* heap, char** slot, const char* text)
{
    size_t old_length = 0U;
    size_t length;
    char* copy;
    if (heap == NULL || slot == NULL || text == NULL) {
        return 0;
    }
    if (*slot != NULL) {
        old_length = strlen(*slot) + 1U;
    }
    length = strlen(text);
    if (length >= NATIVE_WORKER_TEXT_CAPACITY) {
        length = NATIVE_WORKER_TEXT_CAPACITY - 1U;
    }
    copy = (char*)malloc(length + 1U);
    if (copy == NULL) {
        return 0;
    }
    if (length > 0U) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    if (heap->allocated_bytes >= old_length) {
        heap->allocated_bytes -= old_length;
    } else {
        heap->allocated_bytes = 0U;
    }
    free(*slot);
    *slot = copy;
    heap->allocated_bytes += length + 1U;
    return 1;
}

static NativeWorkerLocalHeap* native_worker_local_heap_create(const char* task_name, const char* payload)
{
    NativeWorkerLocalHeap* heap;
    if (task_name == NULL || payload == NULL) {
        return NULL;
    }
    heap = (NativeWorkerLocalHeap*)calloc(1U, sizeof(*heap));
    if (heap == NULL) {
        return NULL;
    }
    if (!native_worker_heap_set_text(heap, &heap->task_name, task_name) ||
        !native_worker_heap_set_text(heap, &heap->payload, payload)) {
        native_worker_local_heap_free(heap);
        return NULL;
    }
    return heap;
}

static NativeWorkerState* native_worker_lookup(int64_t handle_value)
{
    size_t index;
    if (handle_value <= 0 || handle_value > (int64_t)NATIVE_WORKER_CAPACITY) {
        return NULL;
    }
    index = (size_t)(handle_value - 1);
    if (!g_native_workers[index].used) {
        return NULL;
    }
    return &g_native_workers[index];
}

static size_t native_worker_active_count(void)
{
    size_t index;
    size_t count = 0U;
    for (index = 0U; index < NATIVE_WORKER_CAPACITY; index += 1U) {
        if (g_native_workers[index].used) {
            count += 1U;
        }
    }
    return count;
}

#ifdef AIRUN_TEST_HOST_RESOURCE_LIMIT_WORKER_COUNT
static size_t native_worker_host_resource_limit(size_t profile_value)
{
    (void)profile_value;
    return (size_t)AIRUN_TEST_HOST_RESOURCE_LIMIT_WORKER_COUNT;
}
#else
static size_t native_worker_host_resource_limit(size_t profile_value)
{
    return profile_value;
}
#endif

static size_t native_worker_active_runtime_limit(void)
{
    AivmRuntimeProfile profile = aivm_runtime_default_profile();
    AivmRuntimeProfileLimits limits;
    if (g_native_active_vm != NULL) {
        profile = g_native_active_vm->runtime_profile;
    }
    limits = aivm_runtime_profile_limits(profile);
    return native_worker_host_resource_limit(limits.worker_count);
}

static int64_t native_worker_allocate_slot(void)
{
    size_t index;
    for (index = 0U; index < NATIVE_WORKER_CAPACITY; index += 1U) {
        if (!g_native_workers[index].used) {
            native_worker_init_slot(&g_native_workers[index]);
            g_native_workers[index].used = 1;
            return (int64_t)(index + 1U);
        }
    }
    return -1;
}

static int native_worker_start_task(NativeWorkerState* worker, const char* task_name, const char* payload)
{
    int poll_ticks = 2;
    NativeWorkerLocalHeap* heap;
    if (worker == NULL || task_name == NULL || payload == NULL) {
        return 0;
    }
    native_worker_clear_local_heap(worker);
    heap = native_worker_local_heap_create(task_name, payload);
    if (heap == NULL) {
        return 0;
    }
    worker->heap = heap;

    if (strcmp(heap->task_name, "echo") == 0) {
        worker->completion_status = 1;
        if (!native_worker_heap_set_text(heap, &heap->result, heap->payload)) {
            return 0;
        }
    } else if (strcmp(heap->task_name, "fail") == 0) {
        worker->completion_status = -1;
        if (heap->payload[0] != '\0') {
            if (!native_worker_heap_set_text(heap, &heap->error, heap->payload)) {
                return 0;
            }
        } else if (!native_worker_heap_set_text(heap, &heap->error, "worker_failed")) {
            return 0;
        }
    } else if (strcmp(heap->task_name, "sleep") == 0) {
        char* end = NULL;
        long parsed = strtol(heap->payload, &end, 10);
        if (end == heap->payload || *end != '\0' || parsed < 0L || parsed > 1000000L) {
            worker->completion_status = -1;
            if (!native_worker_heap_set_text(heap, &heap->error, "invalid_sleep_ticks")) {
                return 0;
            }
        } else {
            worker->completion_status = 1;
            poll_ticks = (int)parsed + 1;
            if (!native_worker_heap_set_text(heap, &heap->result, "slept")) {
                return 0;
            }
        }
    } else {
        worker->completion_status = -1;
        if (!native_worker_heap_set_text(heap, &heap->error, "unknown_task")) {
            return 0;
        }
    }

    worker->polls_remaining = (int64_t)poll_ticks;
    return 1;
}

static int native_syscall_worker_start(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int64_t handle;
    NativeWorkerState* worker;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 2U || args == NULL ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_STRING || args[1].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (native_worker_active_count() >= native_worker_active_runtime_limit()) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
    }
    handle = native_worker_allocate_slot();
    if (handle < 0) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    worker = native_worker_lookup(handle);
    if (worker == NULL) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    if (!native_worker_start_task(worker, args[0].string_value, args[1].string_value)) {
        native_worker_init_slot(worker);
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
    }
    *result = aivm_value_int(handle);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_worker_poll(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeWorkerState* worker;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    worker = native_worker_lookup(args[0].int_value);
    if (worker == NULL) {
        *result = aivm_value_int(-3);
        return AIVM_SYSCALL_OK;
    }
    if (worker->status == 0) {
        if (worker->polls_remaining > 0) {
            worker->polls_remaining -= 1;
        }
        if (worker->polls_remaining <= 0) {
            worker->status = worker->completion_status;
        }
    }
    *result = aivm_value_int((int64_t)worker->status);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_worker_result(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeWorkerState* worker;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    worker = native_worker_lookup(args[0].int_value);
    if (worker == NULL || worker->status != 1) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_string(worker->heap != NULL && worker->heap->result != NULL ? worker->heap->result : "");
    return AIVM_SYSCALL_OK;
}

static int native_syscall_worker_error(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeWorkerState* worker;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    worker = native_worker_lookup(args[0].int_value);
    if (worker == NULL) {
        *result = aivm_value_string("unknown_worker");
        return AIVM_SYSCALL_OK;
    }
    if (worker->status == -1 || worker->status == -2) {
        *result = aivm_value_string(worker->heap != NULL && worker->heap->error != NULL ? worker->heap->error : "");
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_string("");
    return AIVM_SYSCALL_OK;
}

static int native_syscall_worker_cancel(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeWorkerState* worker;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    worker = native_worker_lookup(args[0].int_value);
    if (worker == NULL || worker->status != 0) {
        *result = aivm_value_bool(0);
        return AIVM_SYSCALL_OK;
    }
    native_worker_clear_local_heap(worker);
    worker->heap = native_worker_local_heap_create("cancel", "");
    if (worker->heap == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
    }
    worker->status = -2;
    worker->polls_remaining = 0;
    worker->completion_status = -2;
    if (!native_worker_heap_set_text(worker->heap, &worker->heap->error, "canceled")) {
        native_worker_clear_local_heap(worker);
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
    }
    *result = aivm_value_bool(1);
    return AIVM_SYSCALL_OK;
}
