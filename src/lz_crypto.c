#include "lz_crypto.h"
#include <string.h>
#include <stdio.h>

#if defined(__APPLE__)
// iOS/macOS - 使用 CommonCrypto 和 Security (系统自带)
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include <Security/SecRandom.h>
#elif defined(__ANDROID__)
// Android - 使用 OpenSSL (NDK 自带)
#include <openssl/evp.h>
#include <openssl/rand.h>
#else
// 其他平台 - 使用 OpenSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

// ============================================================================
// 密钥派生 (PBKDF2)
// ============================================================================

int lz_crypto_derive_key(
    const char *password,
    size_t password_len,
    const uint8_t *salt,
    uint8_t *out_key
) {
    if (!password || !salt || !out_key || password_len == 0) {
        return -1;
    }

#if defined(__APPLE__)
    // iOS/macOS: 使用 CommonCrypto
    int result = CCKeyDerivationPBKDF(
        kCCPBKDF2,                          // 算法
        password, password_len,             // 密码
        salt, LZ_CRYPTO_SALT_SIZE,          // 盐
        kCCPRFHmacAlgSHA256,                // PRF
        LZ_CRYPTO_PBKDF2_ITERATIONS,        // 迭代次数
        out_key, LZ_CRYPTO_KEY_SIZE         // 输出密钥
    );
    return (result == kCCSuccess) ? 0 : -1;

#else
    // Android/其他平台: 使用 OpenSSL
    int result = PKCS5_PBKDF2_HMAC(
        password, (int)password_len,
        salt, LZ_CRYPTO_SALT_SIZE,
        LZ_CRYPTO_PBKDF2_ITERATIONS,
        EVP_sha256(),
        LZ_CRYPTO_KEY_SIZE,
        out_key
    );
    return (result == 1) ? 0 : -1;
#endif
}

// ============================================================================
// 生成随机盐值
// ============================================================================

int lz_crypto_generate_salt(uint8_t *salt) {
    if (!salt) {
        return -1;
    }

#if defined(__APPLE__)
    // iOS/macOS: 使用 SecRandomCopyBytes
    int result = SecRandomCopyBytes(kSecRandomDefault, LZ_CRYPTO_SALT_SIZE, salt);
    return (result == errSecSuccess) ? 0 : -1;

#else
    // Android/其他平台: 使用 OpenSSL
    int result = RAND_bytes(salt, LZ_CRYPTO_SALT_SIZE);
    return (result == 1) ? 0 : -1;
#endif
}

// ============================================================================
// 初始化加密上下文
// ============================================================================

int lz_crypto_init(lz_crypto_context_t *ctx, const char *password, const uint8_t *salt) {
    if (!ctx || !salt) {
        return -1;
    }

    // 清空密钥区域
    memset(ctx->key, 0, LZ_CRYPTO_KEY_SIZE);

    // 如果没有密码,表示不加密
    if (!password || strlen(password) == 0) {
        ctx->is_initialized = false;
        return 0;
    }

    // salt_ptr应该已经在调用前设置好,指向mmap的footer区域
    ctx->salt_ptr = (uint8_t *)salt;

    // 派生密钥
    if (lz_crypto_derive_key(password, strlen(password), ctx->salt_ptr, ctx->key) != 0) {
        lz_crypto_cleanup(ctx);
        return -1;
    }

    ctx->is_initialized = true;
    return 0;
}

// ============================================================================
// AES-CTR 加密/解密
// ============================================================================

int lz_crypto_process(
    lz_crypto_context_t *ctx,
    const uint8_t *input,
    uint8_t *output,
    size_t length,
    uint64_t offset
) {
    if (!ctx || !input || !output || length == 0) {
        return -1;
    }

    // 如果未初始化,直接拷贝(不加密)
    if (!ctx->is_initialized) {
        if (input != output) {
            memcpy(output, input, length);
        }
        return 0;
    }

    // 计算起始 Counter 值
    // Counter = Nonce(8字节) + BlockNumber(8字节)
    // 我们使用文件偏移量作为 BlockNumber
    uint64_t block_number = offset / LZ_CRYPTO_BLOCK_SIZE;
    uint32_t block_offset = offset % LZ_CRYPTO_BLOCK_SIZE;

    // 构造 IV (Counter 初始值)
    uint8_t iv[LZ_CRYPTO_BLOCK_SIZE];
    memset(iv, 0, LZ_CRYPTO_BLOCK_SIZE);
    
    // 将 block_number 放到 IV 的低 8 字节 (大端序)
    for (int i = 0; i < 8; i++) {
        iv[15 - i] = (block_number >> (i * 8)) & 0xFF;
    }

#if defined(__APPLE__)
    // iOS/macOS: 使用 CommonCrypto
    CCCryptorRef cryptor = NULL;
    CCCryptorStatus status;

    // 创建 CTR 模式加密器
    status = CCCryptorCreateWithMode(
        kCCEncrypt,                      // 加密或解密 (CTR 模式两者相同)
        kCCModeCTR,                      // CTR 模式
        kCCAlgorithmAES,                 // AES 算法
        ccNoPadding,                     // 不需要填充
        iv,                              // IV (Counter 初始值)
        ctx->key,                        // 密钥
        LZ_CRYPTO_KEY_SIZE,              // 密钥长度
        NULL,                            // tweak (CTR 不需要)
        0,                               // tweak 长度
        0,                               // 轮数 (使用默认)
        0,                               // 选项
        &cryptor                         // 输出 cryptor
    );

    if (status != kCCSuccess) {
        return -1;
    }

    // 如果有块内偏移,需要先跳过前面的部分
    if (block_offset > 0) {
        uint8_t dummy[LZ_CRYPTO_BLOCK_SIZE];
        size_t dataOutMoved = 0;
        CCCryptorUpdate(cryptor, dummy, block_offset, dummy, block_offset, &dataOutMoved);
    }

    // 执行加密/解密
    size_t dataOutMoved = 0;
    status = CCCryptorUpdate(
        cryptor,
        input,
        length,
        output,
        length,
        &dataOutMoved
    );

    CCCryptorRelease(cryptor);
    return (status == kCCSuccess && dataOutMoved == length) ? 0 : -1;

#else
    // Android/其他平台: 使用 OpenSSL
    EVP_CIPHER_CTX *ctx_cipher = EVP_CIPHER_CTX_new();
    if (!ctx_cipher) {
        return -1;
    }

    int result = -1;
    int len = 0;

    // 初始化 AES-256-CTR
    if (EVP_EncryptInit_ex(ctx_cipher, EVP_aes_256_ctr(), NULL, ctx->key, iv) != 1) {
        goto cleanup;
    }

    // 如果有块内偏移,先跳过
    if (block_offset > 0) {
        uint8_t dummy[LZ_CRYPTO_BLOCK_SIZE];
        if (EVP_EncryptUpdate(ctx_cipher, dummy, &len, dummy, (int)block_offset) != 1) {
            goto cleanup;
        }
    }

    // 执行加密/解密
    if (EVP_EncryptUpdate(ctx_cipher, output, &len, input, (int)length) != 1) {
        goto cleanup;
    }

    if ((size_t)len != length) {
        goto cleanup;
    }

    result = 0;

cleanup:
    EVP_CIPHER_CTX_free(ctx_cipher);
    return result;
#endif
}

// ============================================================================
// 清理上下文
// ============================================================================

void lz_crypto_cleanup(lz_crypto_context_t *ctx) {
    if (ctx) {
        // 安全擦除密钥 (防止内存泄露)
        memset(ctx, 0, sizeof(lz_crypto_context_t));
    }
}
