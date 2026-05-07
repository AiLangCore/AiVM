static int native_syscall_identity_0(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* value = "";
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL || arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (strcmp(target, "sys.platform") == 0) {
#ifdef _WIN32
        value = "windows";
#elif defined(__APPLE__)
        value = "osx";
#else
        value = "linux";
#endif
    } else if (strcmp(target, "sys.arch") == 0) {
#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
        value = "arm64";
#else
        value = "x64";
#endif
    } else if (strcmp(target, "sys.runtime") == 0) {
        value = "airun-native-c";
    } else if (strcmp(target, "sys.os.version") == 0) {
#ifdef _WIN32
        value = "windows";
#elif defined(__APPLE__)
        value = "macos";
#else
        value = "linux";
#endif
    } else {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    *result = aivm_value_string(value);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_time_now_unix_ms(
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
#ifdef _WIN32
    {
        FILETIME file_time;
        ULARGE_INTEGER ticks;
        GetSystemTimeAsFileTime(&file_time);
        ticks.LowPart = file_time.dwLowDateTime;
        ticks.HighPart = file_time.dwHighDateTime;
        *result = aivm_value_int((int64_t)((ticks.QuadPart - 116444736000000000ULL) / 10000ULL));
    }
#else
    {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) != 0) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        *result = aivm_value_int((int64_t)tv.tv_sec * 1000LL + (int64_t)(tv.tv_usec / 1000));
    }
#endif
    return AIVM_SYSCALL_OK;
}

static int native_time_zone_id(char* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return 0;
    }
    buffer[0] = '\0';
#ifdef _WIN32
    {
        DYNAMIC_TIME_ZONE_INFORMATION info;
        DWORD status = GetDynamicTimeZoneInformation(&info);
        size_t i = 0U;
        (void)status;
        while (i + 1U < buffer_size && info.TimeZoneKeyName[i] != 0) {
            buffer[i] = (char)info.TimeZoneKeyName[i];
            i += 1U;
        }
        buffer[i] = '\0';
        return i > 0U ? 1 : 0;
    }
#else
    {
        const char* env_tz = getenv("TZ");
        if (env_tz != NULL && env_tz[0] != '\0') {
            (void)snprintf(buffer, buffer_size, "%s", env_tz);
            return 1;
        }
    }
#if !defined(_WIN32)
    {
        char target[PATH_MAX];
        ssize_t read_count = readlink("/etc/localtime", target, sizeof(target) - 1);
        const char* zoneinfo = NULL;
        if (read_count > 0) {
            target[read_count] = '\0';
            zoneinfo = strstr(target, "/zoneinfo/");
            if (zoneinfo != NULL && zoneinfo[10] != '\0') {
                (void)snprintf(buffer, buffer_size, "%s", zoneinfo + 10);
                return 1;
            }
        }
    }
#endif
    {
        time_t now_value = time(NULL);
        struct tm local_tm;
        if (now_value == (time_t)-1) {
            return 0;
        }
#ifdef _WIN32
        if (localtime_s(&local_tm, &now_value) != 0) {
            return 0;
        }
#else
        if (localtime_r(&now_value, &local_tm) == NULL) {
            return 0;
        }
#endif
        if (strftime(buffer, buffer_size, "%Z", &local_tm) == 0U) {
            return 0;
        }
        return buffer[0] != '\0' ? 1 : 0;
    }
#endif
}

static int native_time_zone_offset_minutes_at(int64_t epoch_ms, int64_t* out_minutes)
{
    if (out_minutes == NULL) {
        return 0;
    }
#ifdef _WIN32
    {
        TIME_ZONE_INFORMATION tz;
        DWORD tz_status = GetTimeZoneInformation(&tz);
        LONG bias = tz.Bias;
        if (tz_status == TIME_ZONE_ID_DAYLIGHT) {
            bias += tz.DaylightBias;
        } else if (tz_status == TIME_ZONE_ID_STANDARD) {
            bias += tz.StandardBias;
        }
        *out_minutes = (int64_t)(-bias);
        (void)epoch_ms;
        return 1;
    }
#else
    {
        time_t epoch_seconds = (time_t)(epoch_ms / 1000LL);
        struct tm local_tm;
        if (localtime_r(&epoch_seconds, &local_tm) == NULL) {
            return 0;
        }
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
        *out_minutes = (int64_t)(local_tm.tm_gmtoff / 60);
        return 1;
#else
        {
            struct tm gm_tm;
            time_t local_seconds;
            time_t gm_seconds;
            if (gmtime_r(&epoch_seconds, &gm_tm) == NULL) {
                return 0;
            }
            local_seconds = mktime(&local_tm);
            gm_seconds = mktime(&gm_tm);
            *out_minutes = (int64_t)((local_seconds - gm_seconds) / 60);
            return 1;
        }
#endif
    }
#endif
}

static int native_syscall_time_monotonic_ms(
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
#ifdef _WIN32
    *result = aivm_value_int((int64_t)GetTickCount64());
#else
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        *result = aivm_value_int((int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L));
    }
#endif
    return AIVM_SYSCALL_OK;
}

static int native_syscall_time_zone_id(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static char zone_id[128];
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_time_zone_id(zone_id, sizeof(zone_id))) {
        zone_id[0] = '\0';
    }
    *result = aivm_value_string(zone_id);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_time_zone_offset_minutes_at(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int64_t minutes = 0;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_time_zone_offset_minutes_at(args[0].int_value, &minutes)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_int(minutes);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_time_sleep_ms(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int64_t ms;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    ms = args[0].int_value;
    if (ms < 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    {
        struct timespec ts;
        ts.tv_sec = (time_t)(ms / 1000);
        ts.tv_nsec = (long)((ms % 1000) * 1000000L);
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        }
    }
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}
