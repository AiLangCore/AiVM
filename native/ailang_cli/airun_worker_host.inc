typedef struct NativeWorkerState
{
    int used;
    int status; /* 0 pending, 1 success, -1 failure, -2 canceled */
    int completion_status;
    int64_t polls_remaining;
    char result[NATIVE_WORKER_TEXT_CAPACITY];
    char error[NATIVE_WORKER_TEXT_CAPACITY];
} NativeWorkerState;

static NativeWorkerState g_native_workers[NATIVE_WORKER_CAPACITY];

static void native_worker_init_slot(NativeWorkerState* worker)
{
    if (worker == NULL) {
        return;
    }
    memset(worker, 0, sizeof(*worker));
    worker->status = 0;
    worker->completion_status = 1;
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

static void native_worker_start_task(NativeWorkerState* worker, const char* task_name, const char* payload)
{
    int poll_ticks = 2;
    if (worker == NULL || task_name == NULL || payload == NULL) {
        return;
    }

    if (strcmp(task_name, "echo") == 0) {
        worker->completion_status = 1;
        (void)snprintf(worker->result, sizeof(worker->result), "%s", payload);
    } else if (strcmp(task_name, "fail") == 0) {
        worker->completion_status = -1;
        if (payload[0] != '\0') {
            (void)snprintf(worker->error, sizeof(worker->error), "%s", payload);
        } else {
            (void)snprintf(worker->error, sizeof(worker->error), "worker_failed");
        }
    } else if (strcmp(task_name, "sleep") == 0) {
        char* end = NULL;
        long parsed = strtol(payload, &end, 10);
        if (end == payload || *end != '\0' || parsed < 0L || parsed > 1000000L) {
            worker->completion_status = -1;
            (void)snprintf(worker->error, sizeof(worker->error), "invalid_sleep_ticks");
        } else {
            worker->completion_status = 1;
            poll_ticks = (int)parsed + 1;
            (void)snprintf(worker->result, sizeof(worker->result), "slept");
        }
    } else {
        worker->completion_status = -1;
        (void)snprintf(worker->error, sizeof(worker->error), "unknown_task");
    }

    worker->polls_remaining = (int64_t)poll_ticks;
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
    native_worker_start_task(worker, args[0].string_value, args[1].string_value);
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
    *result = aivm_value_string(worker->result);
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
        *result = aivm_value_string(worker->error);
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
    worker->status = -2;
    worker->polls_remaining = 0;
    worker->completion_status = -2;
    (void)snprintf(worker->error, sizeof(worker->error), "canceled");
    *result = aivm_value_bool(1);
    return AIVM_SYSCALL_OK;
}
