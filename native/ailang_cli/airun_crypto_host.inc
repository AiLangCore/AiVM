} NativeSha1Ctx;

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_used;
} NativeSha256Ctx;

static uint32_t native_rotl32(uint32_t v, uint32_t n) { return (v << n) | (v >> (32U - n)); }
static uint32_t native_rotr32(uint32_t v, uint32_t n) { return (v >> n) | (v << (32U - n)); }

static uint32_t native_read_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static void native_write_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24U) & 0xFFU);
    p[1] = (uint8_t)((v >> 16U) & 0xFFU);
    p[2] = (uint8_t)((v >> 8U) & 0xFFU);
    p[3] = (uint8_t)(v & 0xFFU);
}

static void native_write_be64(uint8_t* p, uint64_t v)
{
    p[0] = (uint8_t)((v >> 56U) & 0xFFU);
    p[1] = (uint8_t)((v >> 48U) & 0xFFU);
    p[2] = (uint8_t)((v >> 40U) & 0xFFU);
    p[3] = (uint8_t)((v >> 32U) & 0xFFU);
    p[4] = (uint8_t)((v >> 24U) & 0xFFU);
    p[5] = (uint8_t)((v >> 16U) & 0xFFU);
    p[6] = (uint8_t)((v >> 8U) & 0xFFU);
    p[7] = (uint8_t)(v & 0xFFU);
}

static void native_sha1_transform(NativeSha1Ctx* ctx, const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    size_t t;
    for (t = 0U; t < 16U; t += 1U) {
        w[t] = native_read_be32(block + t * 4U);
    }
    for (; t < 80U; t += 1U) {
        w[t] = native_rotl32(w[t - 3U] ^ w[t - 8U] ^ w[t - 14U] ^ w[t - 16U], 1U);
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    for (t = 0U; t < 80U; t += 1U) {
        uint32_t f;
        uint32_t k;
        uint32_t temp;
        if (t < 20U) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (t < 40U) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (t < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        temp = native_rotl32(a, 5U) + f + e + k + w[t];
        e = d;
        d = c;
        c = native_rotl32(b, 30U);
        b = a;
        a = temp;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void native_sha1_init(NativeSha1Ctx* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xC3D2E1F0U;
}

static void native_sha1_update(NativeSha1Ctx* ctx, const uint8_t* data, size_t len)
{
    size_t i = 0U;
    if (ctx == NULL || (data == NULL && len > 0U)) {
        return;
    }
    ctx->bit_count += (uint64_t)len * 8ULL;
    while (i < len) {
        size_t to_copy = 64U - ctx->buffer_used;
        if (to_copy > len - i) {
            to_copy = len - i;
        }
        memcpy(ctx->buffer + ctx->buffer_used, data + i, to_copy);
        ctx->buffer_used += to_copy;
        i += to_copy;
        if (ctx->buffer_used == 64U) {
            native_sha1_transform(ctx, ctx->buffer);
            ctx->buffer_used = 0U;
        }
    }
}

static void native_sha1_final(NativeSha1Ctx* ctx, uint8_t out[20])
{
    uint8_t len_buf[8];
    size_t i;
    if (ctx == NULL || out == NULL) {
        return;
    }
    native_write_be64(len_buf, ctx->bit_count);
    {
        uint8_t pad = 0x80U;
        native_sha1_update(ctx, &pad, 1U);
    }
    while (ctx->buffer_used != 56U) {
        uint8_t zero = 0U;
        native_sha1_update(ctx, &zero, 1U);
    }
    native_sha1_update(ctx, len_buf, sizeof(len_buf));
    for (i = 0U; i < 5U; i += 1U) {
        native_write_be32(out + i * 4U, ctx->state[i]);
    }
}

static void native_sha256_transform(NativeSha256Ctx* ctx, const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };
    uint32_t w[64];
    uint32_t a,b,c,d,e,f,g,h;
    size_t t;
    for (t = 0U; t < 16U; t += 1U) {
        w[t] = native_read_be32(block + t * 4U);
    }
    for (; t < 64U; t += 1U) {
        uint32_t s0 = native_rotr32(w[t - 15U], 7U) ^ native_rotr32(w[t - 15U], 18U) ^ (w[t - 15U] >> 3U);
        uint32_t s1 = native_rotr32(w[t - 2U], 17U) ^ native_rotr32(w[t - 2U], 19U) ^ (w[t - 2U] >> 10U);
        w[t] = w[t - 16U] + s0 + w[t - 7U] + s1;
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (t = 0U; t < 64U; t += 1U) {
        uint32_t s1 = native_rotr32(e, 6U) ^ native_rotr32(e, 11U) ^ native_rotr32(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[t] + w[t];
        uint32_t s0 = native_rotr32(a, 2U) ^ native_rotr32(a, 13U) ^ native_rotr32(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void native_sha256_init(NativeSha256Ctx* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x6a09e667U; ctx->state[1] = 0xbb67ae85U; ctx->state[2] = 0x3c6ef372U; ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU; ctx->state[5] = 0x9b05688cU; ctx->state[6] = 0x1f83d9abU; ctx->state[7] = 0x5be0cd19U;
}

static void native_sha256_update(NativeSha256Ctx* ctx, const uint8_t* data, size_t len)
{
    size_t i = 0U;
    if (ctx == NULL || (data == NULL && len > 0U)) {
        return;
    }
    ctx->bit_count += (uint64_t)len * 8ULL;
    while (i < len) {
        size_t to_copy = 64U - ctx->buffer_used;
        if (to_copy > len - i) {
            to_copy = len - i;
        }
        memcpy(ctx->buffer + ctx->buffer_used, data + i, to_copy);
        ctx->buffer_used += to_copy;
        i += to_copy;
        if (ctx->buffer_used == 64U) {
            native_sha256_transform(ctx, ctx->buffer);
            ctx->buffer_used = 0U;
        }
    }
}

static void native_sha256_final(NativeSha256Ctx* ctx, uint8_t out[32])
{
    uint8_t len_buf[8];
    size_t i;
    if (ctx == NULL || out == NULL) {
        return;
    }
    native_write_be64(len_buf, ctx->bit_count);
    {
        uint8_t pad = 0x80U;
        native_sha256_update(ctx, &pad, 1U);
    }
    while (ctx->buffer_used != 56U) {
        uint8_t zero = 0U;
        native_sha256_update(ctx, &zero, 1U);
    }
    native_sha256_update(ctx, len_buf, sizeof(len_buf));
    for (i = 0U; i < 8U; i += 1U) {
        native_write_be32(out + i * 4U, ctx->state[i]);
    }
}

static void native_hex_encode(const uint8_t* bytes, size_t len, char* out, size_t out_cap)
{
    static const char* hex = "0123456789abcdef";
    size_t i;
    if (out == NULL || out_cap == 0U) {
        return;
    }
    if (bytes == NULL || out_cap < len * 2U + 1U) {
        out[0] = '\0';
        return;
    }
    for (i = 0U; i < len; i += 1U) {
        out[i * 2U] = hex[(bytes[i] >> 4U) & 0x0FU];
        out[i * 2U + 1U] = hex[bytes[i] & 0x0FU];
    }
    out[len * 2U] = '\0';
}

static int native_syscall_crypto_string_base64_encode(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t in_len;
    size_t out_len;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    in_len = strlen(args[0].string_value);
    out_len = ((in_len + 2U) / 3U) * 4U;
    if (out_len + 1U > NATIVE_BYTES_SCRATCH_CAPACITY ||
        !native_bytes_to_base64((const uint8_t*)args[0].string_value, in_len, g_native_base64_scratch, out_len + 1U)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_string(g_native_base64_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_crypto_string_base64_decode(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t out_len = 0U;
    size_t i;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_bytes_from_base64(args[0].string_value, NULL, 0U, &out_len) ||
        out_len + 1U > NATIVE_BYTES_SCRATCH_CAPACITY ||
        !native_bytes_from_base64(args[0].string_value, g_native_bytes_scratch, out_len, &out_len)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_is_valid_utf8_without_nul(g_native_bytes_scratch, out_len)) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    for (i = 0U; i < out_len; i += 1U) {
        g_native_base64_scratch[i] = (char)g_native_bytes_scratch[i];
    }
    g_native_base64_scratch[out_len] = '\0';
    *result = aivm_value_string(g_native_base64_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_crypto_sha1(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeSha1Ctx ctx;
    uint8_t digest[20];
    static char hex_out[41];
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    native_sha1_init(&ctx);
    native_sha1_update(&ctx, (const uint8_t*)args[0].string_value, strlen(args[0].string_value));
    native_sha1_final(&ctx, digest);
    native_hex_encode(digest, sizeof(digest), hex_out, sizeof(hex_out));
    *result = aivm_value_string(hex_out);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_crypto_sha256(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeSha256Ctx ctx;
    uint8_t digest[32];
    static char hex_out[65];
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    native_sha256_init(&ctx);
    native_sha256_update(&ctx, (const uint8_t*)args[0].string_value, strlen(args[0].string_value));
    native_sha256_final(&ctx, digest);
    native_hex_encode(digest, sizeof(digest), hex_out, sizeof(hex_out));
    *result = aivm_value_string(hex_out);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_crypto_hmac_sha256(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    uint8_t key_block[64];
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    uint8_t inner_hash[32];
    size_t i;
    size_t key_len;
    const uint8_t* msg;
    size_t msg_len;
    NativeSha256Ctx ctx;
    static char hex_out[65];
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
    memset(key_block, 0, sizeof(key_block));
    key_len = strlen(args[0].string_value);
    if (key_len > 64U) {
        uint8_t kh[32];
        native_sha256_init(&ctx);
        native_sha256_update(&ctx, (const uint8_t*)args[0].string_value, key_len);
        native_sha256_final(&ctx, kh);
        memcpy(key_block, kh, sizeof(kh));
    } else {
        memcpy(key_block, args[0].string_value, key_len);
    }
    for (i = 0U; i < 64U; i += 1U) {
        k_ipad[i] = (uint8_t)(key_block[i] ^ 0x36U);
        k_opad[i] = (uint8_t)(key_block[i] ^ 0x5cU);
    }
    msg = (const uint8_t*)args[1].string_value;
    msg_len = strlen(args[1].string_value);
    native_sha256_init(&ctx);
    native_sha256_update(&ctx, k_ipad, sizeof(k_ipad));
    native_sha256_update(&ctx, msg, msg_len);
    native_sha256_final(&ctx, inner_hash);
    native_sha256_init(&ctx);
    native_sha256_update(&ctx, k_opad, sizeof(k_opad));
    native_sha256_update(&ctx, inner_hash, sizeof(inner_hash));
    native_sha256_final(&ctx, inner_hash);
    native_hex_encode(inner_hash, sizeof(inner_hash), hex_out, sizeof(hex_out));
    *result = aivm_value_string(hex_out);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_crypto_random_bytes(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t len;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT || args[0].int_value < 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    len = (size_t)args[0].int_value;
    if (len == 0U) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    if (len > NATIVE_BYTES_SCRATCH_CAPACITY) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
#ifdef _WIN32
    {
        size_t i;
        for (i = 0U; i < len; i += 1U) {
            unsigned int v = 0U;
            if (rand_s(&v) != 0) {
                result->type = AIVM_VAL_VOID;
                return AIVM_SYSCALL_ERR_INVALID;
            }
            g_native_bytes_scratch[i] = (uint8_t)(v & 0xFFU);
        }
    }
#else
    {
        int fd = open("/dev/urandom", O_RDONLY);
        size_t read_total = 0U;
        if (fd < 0) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        while (read_total < len) {
            ssize_t got = read(fd, g_native_bytes_scratch + read_total, len - read_total);
            if (got <= 0) {
                close(fd);
                result->type = AIVM_VAL_VOID;
                return AIVM_SYSCALL_ERR_INVALID;
            }
            read_total += (size_t)got;
        }
        close(fd);
    }
#endif
    *result = aivm_value_bytes(g_native_bytes_scratch, len);
    return AIVM_SYSCALL_OK;
}

