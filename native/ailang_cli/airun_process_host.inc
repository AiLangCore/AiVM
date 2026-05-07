static void native_process_init_slot(NativeProcessState* process)
{
    if (process == NULL) {
        return;
    }
    memset(process, 0, sizeof(*process));
#ifndef _WIN32
    process->pid = (pid_t)-1;
    process->stdout_fd = -1;
    process->stderr_fd = -1;
#endif
}

static int native_process_append_buffer(
    uint8_t** buffer,
    size_t* length,
    size_t* position,
    const uint8_t* chunk,
    size_t chunk_len)
{
    uint8_t* grown;
    size_t unread_len;
    if (buffer == NULL || length == NULL || position == NULL || chunk == NULL || chunk_len == 0U) {
        return 1;
    }
    if (*position > 0U && *position < *length) {
        unread_len = *length - *position;
        memmove(*buffer, *buffer + *position, unread_len);
        *length = unread_len;
        *position = 0U;
    } else if (*position >= *length) {
        *length = 0U;
        *position = 0U;
    }
    grown = (uint8_t*)realloc(*buffer, *length + chunk_len);
    if (grown == NULL) {
        return 0;
    }
    memcpy(grown + *length, chunk, chunk_len);
    *buffer = grown;
    *length += chunk_len;
    return 1;
}

static AivmValue native_process_consume_buffer(
    uint8_t** buffer,
    size_t* length,
    size_t* position)
{
    size_t unread_len;
    AivmValue value;
    value = aivm_value_bytes(NULL, 0U);
    if (buffer == NULL || length == NULL || position == NULL || *buffer == NULL || *position >= *length) {
        return value;
    }
    unread_len = *length - *position;
    value = aivm_value_bytes(*buffer + *position, unread_len);
    *position = *length;
    return value;
}

static int native_process_drain_stream(NativeProcessState* process, int read_stdout)
{
    int drained_any = 0;
    if (process == NULL) {
        return 0;
    }
#ifdef _WIN32
    {
        HANDLE stream = read_stdout ? process->stdout_read : process->stderr_read;
        int* closed_flag = read_stdout ? &process->stdout_closed : &process->stderr_closed;
        uint8_t** buffer = read_stdout ? &process->stdout_buffer : &process->stderr_buffer;
        size_t* length = read_stdout ? &process->stdout_buffer_len : &process->stderr_buffer_len;
        size_t* position = read_stdout ? &process->stdout_buffer_pos : &process->stderr_buffer_pos;
        while (!*closed_flag && stream != NULL) {
            DWORD available = 0;
            DWORD read_count = 0;
            if (!PeekNamedPipe(stream, NULL, 0, NULL, &available, NULL)) {
                DWORD err = GetLastError();
                if (err == ERROR_BROKEN_PIPE) {
                    CloseHandle(stream);
                    if (read_stdout) {
                        process->stdout_read = NULL;
                    } else {
                        process->stderr_read = NULL;
                    }
                    *closed_flag = 1;
                }
                break;
            }
            if (available == 0) {
                break;
            }
            if (available > (DWORD)NATIVE_PROCESS_READ_CHUNK) {
                available = (DWORD)NATIVE_PROCESS_READ_CHUNK;
            }
            if (!ReadFile(stream, g_native_process_read_scratch, available, &read_count, NULL) || read_count == 0) {
                break;
            }
            if (!native_process_append_buffer(buffer, length, position, g_native_process_read_scratch, (size_t)read_count)) {
                break;
            }
            drained_any = 1;
        }
    }
#else
    {
        int fd = read_stdout ? process->stdout_fd : process->stderr_fd;
        int* closed_flag = read_stdout ? &process->stdout_closed : &process->stderr_closed;
        uint8_t** buffer = read_stdout ? &process->stdout_buffer : &process->stderr_buffer;
        size_t* length = read_stdout ? &process->stdout_buffer_len : &process->stderr_buffer_len;
        size_t* position = read_stdout ? &process->stdout_buffer_pos : &process->stderr_buffer_pos;
        while (!*closed_flag && fd >= 0) {
            ssize_t read_count = read(fd, g_native_process_read_scratch, NATIVE_PROCESS_READ_CHUNK);
            if (read_count > 0) {
                if (!native_process_append_buffer(buffer, length, position, g_native_process_read_scratch, (size_t)read_count)) {
                    break;
                }
                drained_any = 1;
                continue;
            }
            if (read_count == 0) {
                close(fd);
                *closed_flag = 1;
                if (read_stdout) {
                    process->stdout_fd = -1;
                } else {
                    process->stderr_fd = -1;
                }
            }
            break;
        }
    }
#endif
    return drained_any;
}

static int native_process_drain_output(NativeProcessState* process)
{
    int drained_stdout = native_process_drain_stream(process, 1);
    int drained_stderr = native_process_drain_stream(process, 0);
    return drained_stdout || drained_stderr;
}

static void native_process_release_slot(NativeProcessState* process)
{
    if (process == NULL || !process->used) {
        return;
    }
    free(process->stdout_buffer);
    process->stdout_buffer = NULL;
    process->stdout_buffer_len = 0U;
    process->stdout_buffer_pos = 0U;
    free(process->stderr_buffer);
    process->stderr_buffer = NULL;
    process->stderr_buffer_len = 0U;
    process->stderr_buffer_pos = 0U;
#ifdef _WIN32
    if (process->process_handle != NULL) {
        CloseHandle(process->process_handle);
        process->process_handle = NULL;
    }
    if (process->stdout_read != NULL) {
        CloseHandle(process->stdout_read);
        process->stdout_read = NULL;
    }
    if (process->stderr_read != NULL) {
        CloseHandle(process->stderr_read);
        process->stderr_read = NULL;
    }
#else
    if (process->stdout_fd >= 0) {
        close(process->stdout_fd);
        process->stdout_fd = -1;
    }
    if (process->stderr_fd >= 0) {
        close(process->stderr_fd);
        process->stderr_fd = -1;
    }
#endif
    native_process_init_slot(process);
}

static void native_process_maybe_release_finished(NativeProcessState* process)
{
    if (process == NULL || !process->used || !process->finished) {
        return;
    }
    if (!process->stdout_closed || !process->stderr_closed) {
        return;
    }
    if (process->stdout_buffer_pos < process->stdout_buffer_len) {
        return;
    }
    if (process->stderr_buffer_pos < process->stderr_buffer_len) {
        return;
    }
    native_process_release_slot(process);
}

static NativeProcessState* native_process_lookup(int64_t handle_value)
{
    size_t index;
    if (handle_value <= 0 || handle_value > (int64_t)NATIVE_PROCESS_CAPACITY) {
        return NULL;
    }
    index = (size_t)(handle_value - 1);
    if (!g_native_processes[index].used) {
        return NULL;
    }
    return &g_native_processes[index];
}

static int64_t native_process_allocate_slot(void)
{
    size_t index;
    for (index = 0U; index < NATIVE_PROCESS_CAPACITY; index += 1U) {
        if (!g_native_processes[index].used) {
            native_process_init_slot(&g_native_processes[index]);
            g_native_processes[index].used = 1;
            return (int64_t)(index + 1U);
        }
    }
    return -1;
}
static int native_syscall_process_cwd(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static char cwd[PATH_MAX];
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef _WIN32
    if (_getcwd(cwd, (int)sizeof(cwd)) == NULL) {
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
#endif
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_string(cwd);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_process_env_get(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* value;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    value = getenv(args[0].string_value);
    *result = aivm_value_string((value == NULL) ? "" : value);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_host_open_default(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_bool(native_host_open_default(args[0].string_value) ? 1 : 0);
    return AIVM_SYSCALL_OK;
}
static int native_syscall_process_argv(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (g_native_active_vm != NULL) {
        airun_log_message(
            AIRUN_LOG_TRACE,
            "runtime",
            "sys.process.args count=%llu handle=%lld",
            (unsigned long long)g_native_active_vm->process_argv_count,
            (long long)g_native_active_vm->process_argv_node_handle);
    }
    *result = aivm_value_node(1);
    return AIVM_SYSCALL_OK;
}
static int native_vm_collect_process_argv(
    const AivmVm* vm,
    const char* command,
    int64_t args_node_handle,
    char*** out_argv,
    size_t* out_argc)
{
    const AivmNodeRecord* args_node = NULL;
    char** argv_values = NULL;
    size_t argc = 0U;
    size_t child_count = 0U;
    size_t i;

    if (vm == NULL || command == NULL || out_argv == NULL || out_argc == NULL) {
        return 0;
    }
    if (args_node_handle > 0) {
        if (!native_vm_lookup_node_record(vm, args_node_handle, &args_node)) {
            return 0;
        }
        child_count = args_node->child_count;
    }

    argv_values = (char**)calloc(child_count + 2U, sizeof(char*));
    if (argv_values == NULL) {
        return 0;
    }
    argv_values[argc++] = (char*)command;
    for (i = 0U; i < child_count; i += 1U) {
        const char* arg_text =
            native_vm_node_first_string_attr(vm, vm->node_children[args_node->child_start + i]);
        if (arg_text == NULL) {
            free(argv_values);
            return 0;
        }
        argv_values[argc++] = (char*)arg_text;
    }
    argv_values[argc] = NULL;
    *out_argv = argv_values;
    *out_argc = argc;
    return 1;
}

#ifdef _WIN32
static int native_windows_append_quoted_arg(
    char* buffer,
    size_t buffer_len,
    size_t* io_used,
    const char* arg)
{
    size_t used;
    size_t i;
    int needs_quotes = 0;

    if (buffer == NULL || io_used == NULL || arg == NULL) {
        return 0;
    }
    used = *io_used;
    for (i = 0U; arg[i] != '\0'; i += 1U) {
        if (arg[i] == ' ' || arg[i] == '\t' || arg[i] == '"') {
            needs_quotes = 1;
            break;
        }
    }
    if (used > 0U) {
        if (used + 1U >= buffer_len) {
            return 0;
        }
        buffer[used++] = ' ';
    }
    if (needs_quotes) {
        if (used + 1U >= buffer_len) {
            return 0;
        }
        buffer[used++] = '"';
    }
    for (i = 0U; arg[i] != '\0'; i += 1U) {
        if (arg[i] == '"') {
            if (used + 2U >= buffer_len) {
                return 0;
            }
            buffer[used++] = '\\';
            buffer[used++] = '"';
        } else {
            if (used + 1U >= buffer_len) {
                return 0;
            }
            buffer[used++] = arg[i];
        }
    }
    if (needs_quotes) {
        if (used + 1U >= buffer_len) {
            return 0;
        }
        buffer[used++] = '"';
    }
    if (used >= buffer_len) {
        return 0;
    }
    buffer[used] = '\0';
    *io_used = used;
    return 1;
}
#endif
static int native_syscall_process_spawn(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 4U || args == NULL ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_NODE ||
        args[2].type != AIVM_VAL_STRING || args[2].string_value == NULL ||
        args[3].type != AIVM_VAL_NODE) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (g_native_active_vm == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
#ifdef _WIN32
    {
        SECURITY_ATTRIBUTES security_attributes;
        HANDLE stdout_read = NULL;
        HANDLE stdout_write = NULL;
        HANDLE stderr_read = NULL;
        HANDLE stderr_write = NULL;
        PROCESS_INFORMATION process_info;
        STARTUPINFOA startup_info;
        int64_t slot_handle;
        NativeProcessState* process;
        char* command_line;
        char** argv_values = NULL;
        size_t argv_count = 0U;
        size_t command_line_used = 0U;
        const char* cwd = NULL;

        memset(&security_attributes, 0, sizeof(security_attributes));
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = TRUE;
        security_attributes.lpSecurityDescriptor = NULL;

        if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0) ||
            !CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0)) {
            if (stdout_read != NULL) {
                CloseHandle(stdout_read);
            }
            if (stdout_write != NULL) {
                CloseHandle(stdout_write);
            }
            if (stderr_read != NULL) {
                CloseHandle(stderr_read);
            }
            if (stderr_write != NULL) {
                CloseHandle(stderr_write);
            }
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }

        (void)SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
        (void)SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

        memset(&startup_info, 0, sizeof(startup_info));
        memset(&process_info, 0, sizeof(process_info));
        startup_info.cb = sizeof(startup_info);
        startup_info.dwFlags = STARTF_USESTDHANDLES;
        startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup_info.hStdOutput = stdout_write;
        startup_info.hStdError = stderr_write;

        if (!native_vm_collect_process_argv(
                g_native_active_vm,
                args[0].string_value,
                args[1].node_handle,
                &argv_values,
                &argv_count)) {
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }
        command_line = (char*)calloc(8192U, 1U);
        if (command_line == NULL) {
            free(argv_values);
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }
        for (size_t arg_i = 0U; arg_i < argv_count; arg_i += 1U) {
            if (!native_windows_append_quoted_arg(command_line, 8192U, &command_line_used, argv_values[arg_i])) {
                free(command_line);
                free(argv_values);
                CloseHandle(stdout_read);
                CloseHandle(stdout_write);
                CloseHandle(stderr_read);
                CloseHandle(stderr_write);
                *result = aivm_value_int(-1);
                return AIVM_SYSCALL_OK;
            }
        }
        free(argv_values);
        if (args[2].string_value[0] != '\0') {
            cwd = args[2].string_value;
        }

        if (!CreateProcessA(
                NULL,
                command_line,
                NULL,
                NULL,
                TRUE,
                CREATE_NO_WINDOW,
                NULL,
                cwd,
                &startup_info,
                &process_info)) {
            free(command_line);
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }

        free(command_line);
        CloseHandle(stdout_write);
        CloseHandle(stderr_write);
        CloseHandle(process_info.hThread);

        slot_handle = native_process_allocate_slot();
        if (slot_handle < 0) {
            (void)TerminateProcess(process_info.hProcess, 1);
            CloseHandle(process_info.hProcess);
            CloseHandle(stdout_read);
            CloseHandle(stderr_read);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }

        process = native_process_lookup(slot_handle);
        if (process == NULL) {
            (void)TerminateProcess(process_info.hProcess, 1);
            CloseHandle(process_info.hProcess);
            CloseHandle(stdout_read);
            CloseHandle(stderr_read);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }
        process->process_handle = process_info.hProcess;
        process->stdout_read = stdout_read;
        process->stderr_read = stderr_read;
        *result = aivm_value_int(slot_handle);
        return AIVM_SYSCALL_OK;
    }
#else
    {
        int stdout_pipe[2] = { -1, -1 };
        int stderr_pipe[2] = { -1, -1 };
        pid_t pid;
        int64_t slot_handle;
        NativeProcessState* process;
        char** argv_values = NULL;
        size_t argv_count = 0U;
        if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            if (stdout_pipe[0] >= 0) {
                close(stdout_pipe[0]);
            }
            if (stdout_pipe[1] >= 0) {
                close(stdout_pipe[1]);
            }
            if (stderr_pipe[0] >= 0) {
                close(stderr_pipe[0]);
            }
            if (stderr_pipe[1] >= 0) {
                close(stderr_pipe[1]);
            }
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }
        if (!native_vm_collect_process_argv(
                g_native_active_vm,
                args[0].string_value,
                args[1].node_handle,
                &argv_values,
                &argv_count)) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }

        if (g_airun_log_level >= AIRUN_LOG_TRACE) {
            const char* arg0 = (argv_count > 0U && argv_values != NULL && argv_values[0] != NULL)
                ? argv_values[0]
                : "";
            const char* arg1 = (argv_count > 1U && argv_values != NULL && argv_values[1] != NULL)
                ? argv_values[1]
                : "";
            const char* arg2 = (argv_count > 2U && argv_values != NULL && argv_values[2] != NULL)
                ? argv_values[2]
                : "";
            airun_log_message(
                AIRUN_LOG_TRACE,
                "process",
                "spawn command=%s cwd=%s argv_count=%llu arg0=%s arg1=%s arg2=%s",
                args[0].string_value,
                args[2].string_value[0] != '\0' ? args[2].string_value : ".",
                (unsigned long long)argv_count,
                arg0,
                arg1,
                arg2);
        }

        pid = fork();
        if (pid == 0) {
            (void)signal(SIGPIPE, SIG_IGN);
            if (args[2].string_value[0] != '\0') {
                if (chdir(args[2].string_value) != 0) {
                    _exit(126);
                }
            }
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            execvp(args[0].string_value, argv_values);
            _exit(127);
        }
        free(argv_values);
        if (pid < 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }

        airun_log_message(
            AIRUN_LOG_TRACE,
            "process",
            "spawned pid=%lld command=%s",
            (long long)pid,
            args[0].string_value);

        slot_handle = native_process_allocate_slot();
        if (slot_handle < 0) {
            kill(pid, SIGTERM);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        process = native_process_lookup(slot_handle);
        if (process == NULL) {
            kill(pid, SIGTERM);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            *result = aivm_value_int(-1);
            return AIVM_SYSCALL_OK;
        }
        process->pid = pid;
        process->stdout_fd = stdout_pipe[0];
        process->stderr_fd = stderr_pipe[0];
        native_process_set_nonblocking(process->stdout_fd);
        native_process_set_nonblocking(process->stderr_fd);
        *result = aivm_value_int(slot_handle);
        return AIVM_SYSCALL_OK;
    }
#endif
}

static int native_syscall_process_wait(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeProcessState* process;
    int exit_code;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    process = native_process_lookup(args[0].int_value);
    if (process == NULL) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
#ifdef _WIN32
    if (!process->finished) {
        for (;;) {
            DWORD wait_status;
            native_process_drain_output(process);
            wait_status = WaitForSingleObject(process->process_handle, 0);
            if (wait_status == WAIT_OBJECT_0) {
                DWORD process_exit_code;
                process->finished = 1;
                if (GetExitCodeProcess(process->process_handle, &process_exit_code) != 0) {
                    process->exit_code = (int)process_exit_code;
                } else {
                    process->exit_code = -1;
                }
                break;
            }
            Sleep(1);
        }
        while (native_process_drain_output(process)) {
        }
        if (process->stdout_read != NULL && !process->stdout_closed) {
            native_process_drain_output(process);
        }
        if (process->stderr_read != NULL && !process->stderr_closed) {
            native_process_drain_output(process);
        }
    }
    exit_code = process->exit_code;
    *result = aivm_value_int((int64_t)exit_code);
    native_process_maybe_release_finished(process);
    return AIVM_SYSCALL_OK;
#else
    if (!process->finished) {
        for (;;) {
            int status;
            pid_t wait_result;
            native_process_drain_output(process);
            wait_result = waitpid(process->pid, &status, WNOHANG);
            if (wait_result == process->pid) {
                process->finished = 1;
                if (WIFEXITED(status)) {
                    process->exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    process->exit_code = 128 + WTERMSIG(status);
                } else {
                    process->exit_code = -1;
                }
                break;
            }
            usleep(1000);
        }
        while (native_process_drain_output(process)) {
        }
    }
    exit_code = process->exit_code;
    *result = aivm_value_int((int64_t)exit_code);
    native_process_maybe_release_finished(process);
    return AIVM_SYSCALL_OK;
#endif
}

static int native_syscall_process_poll(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeProcessState* process;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    process = native_process_lookup(args[0].int_value);
    if (process == NULL) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
#ifdef _WIN32
    native_process_refresh(process);
    *result = aivm_value_int(process->finished ? 1 : 0);
    return AIVM_SYSCALL_OK;
#else
    native_process_refresh(process);
    *result = aivm_value_int(process->finished ? 1 : 0);
    return AIVM_SYSCALL_OK;
#endif
}

static int native_syscall_process_kill(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeProcessState* process;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    process = native_process_lookup(args[0].int_value);
    if (process == NULL) {
        *result = aivm_value_bool(0);
        return AIVM_SYSCALL_OK;
    }
#ifdef _WIN32
    if (process->finished) {
        *result = aivm_value_bool(0);
    } else {
        *result = aivm_value_bool(TerminateProcess(process->process_handle, 1) ? 1 : 0);
    }
#else
    if (process->finished) {
        *result = aivm_value_bool(0);
    } else {
        *result = aivm_value_bool(kill(process->pid, SIGTERM) == 0 ? 1 : 0);
    }
#endif
    return AIVM_SYSCALL_OK;
}

static int native_syscall_process_exit(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int code;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    code = (int)args[0].int_value;
    *result = aivm_value_void();
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

static int native_syscall_process_stream_read(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result,
    int read_stdout)
{
    NativeProcessState* process;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    process = native_process_lookup(args[0].int_value);
    if (process == NULL) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
#ifdef _WIN32
    {
        if (read_stdout) {
            if (process->stdout_buffer_pos < process->stdout_buffer_len) {
                *result = native_process_consume_buffer(
                    &process->stdout_buffer,
                    &process->stdout_buffer_len,
                    &process->stdout_buffer_pos);
                native_process_maybe_release_finished(process);
                return AIVM_SYSCALL_OK;
            }
        } else {
            if (process->stderr_buffer_pos < process->stderr_buffer_len) {
                *result = native_process_consume_buffer(
                    &process->stderr_buffer,
                    &process->stderr_buffer_len,
                    &process->stderr_buffer_pos);
                native_process_maybe_release_finished(process);
                return AIVM_SYSCALL_OK;
            }
        }
        HANDLE stream = read_stdout ? process->stdout_read : process->stderr_read;
        int* closed_flag = read_stdout ? &process->stdout_closed : &process->stderr_closed;
        DWORD available = 0;
        DWORD read_count = 0;
        if (*closed_flag || stream == NULL) {
            *result = aivm_value_bytes(NULL, 0U);
            native_process_maybe_release_finished(process);
            return AIVM_SYSCALL_OK;
        }
        if (!PeekNamedPipe(stream, NULL, 0, NULL, &available, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE) {
                CloseHandle(stream);
                if (read_stdout) {
                    process->stdout_read = NULL;
                } else {
                    process->stderr_read = NULL;
                }
                *closed_flag = 1;
                *result = aivm_value_bytes(NULL, 0U);
                native_process_maybe_release_finished(process);
                return AIVM_SYSCALL_OK;
            }
            *result = aivm_value_bytes(NULL, 0U);
            return AIVM_SYSCALL_OK;
        }
        if (available == 0) {
            *result = aivm_value_bytes(NULL, 0U);
            native_process_maybe_release_finished(process);
            return AIVM_SYSCALL_OK;
        }
        if (available > (DWORD)NATIVE_PROCESS_READ_CHUNK) {
            available = (DWORD)NATIVE_PROCESS_READ_CHUNK;
        }
        if (!ReadFile(stream, g_native_process_read_scratch, available, &read_count, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE) {
                CloseHandle(stream);
                if (read_stdout) {
                    process->stdout_read = NULL;
                } else {
                    process->stderr_read = NULL;
                }
                *closed_flag = 1;
            }
            *result = aivm_value_bytes(NULL, 0U);
            native_process_maybe_release_finished(process);
            return AIVM_SYSCALL_OK;
        }
        if (read_count == 0) {
            *result = aivm_value_bytes(NULL, 0U);
            native_process_maybe_release_finished(process);
            return AIVM_SYSCALL_OK;
        }
        *result = aivm_value_bytes(g_native_process_read_scratch, (size_t)read_count);
        return AIVM_SYSCALL_OK;
    }
#else
    {
        if (read_stdout) {
            if (process->stdout_buffer_pos < process->stdout_buffer_len) {
                *result = native_process_consume_buffer(
                    &process->stdout_buffer,
                    &process->stdout_buffer_len,
                    &process->stdout_buffer_pos);
                native_process_maybe_release_finished(process);
                return AIVM_SYSCALL_OK;
            }
        } else {
            if (process->stderr_buffer_pos < process->stderr_buffer_len) {
                *result = native_process_consume_buffer(
                    &process->stderr_buffer,
                    &process->stderr_buffer_len,
                    &process->stderr_buffer_pos);
                native_process_maybe_release_finished(process);
                return AIVM_SYSCALL_OK;
            }
        }
        int fd = read_stdout ? process->stdout_fd : process->stderr_fd;
        int* closed_flag = read_stdout ? &process->stdout_closed : &process->stderr_closed;
        ssize_t read_count;
        if (*closed_flag || fd < 0) {
            *result = aivm_value_bytes(NULL, 0U);
            native_process_maybe_release_finished(process);
            return AIVM_SYSCALL_OK;
        }
        read_count = read(fd, g_native_process_read_scratch, NATIVE_PROCESS_READ_CHUNK);
        if (read_count > 0) {
            *result = aivm_value_bytes(g_native_process_read_scratch, (size_t)read_count);
            return AIVM_SYSCALL_OK;
        }
        if (read_count == 0) {
            close(fd);
            *closed_flag = 1;
            if (read_stdout) {
                process->stdout_fd = -1;
            } else {
                process->stderr_fd = -1;
            }
            *result = aivm_value_bytes(NULL, 0U);
            native_process_maybe_release_finished(process);
            return AIVM_SYSCALL_OK;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *result = aivm_value_bytes(NULL, 0U);
            return AIVM_SYSCALL_OK;
        }
        *result = aivm_value_bytes(NULL, 0U);
        native_process_maybe_release_finished(process);
        return AIVM_SYSCALL_OK;
    }
#endif
}
static int native_syscall_process_stdout_read(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    return native_syscall_process_stream_read(target, args, arg_count, result, 1);
}

static int native_syscall_process_stderr_read(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    return native_syscall_process_stream_read(target, args, arg_count, result, 0);
}
