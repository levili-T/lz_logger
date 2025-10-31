#include "lz_crypto.h"
#include <string.h>
#include <stdio.h>

#if defined(__APPLE__)
// iOS/macOS - 使用 CommonCrypto 和 Security (系统自带)
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include <Security/SecRandom.h>
#elif defined(__ANDROID__)
// Android - 通过 JNI 调用 Java Crypto API
#include <jni.h>
#else
// 其他平台 - 使用 OpenSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

// Android JNI 全局引用
#if defined(__ANDROID__)
static JavaVM *g_jvm = NULL;
static jclass g_crypto_helper_class = NULL;
static jmethodID g_derive_key_method = NULL;
static jmethodID g_generate_salt_method = NULL;
static jmethodID g_process_aes_ctr_method = NULL;

// JNI 初始化函数 (从 lz_logger_jni.cpp 调用)
int lz_crypto_jni_init(JNIEnv *env, JavaVM *jvm) {
    g_jvm = jvm;
    
    // 查找 CryptoHelper 类
    jclass local_class = (*env)->FindClass(env, "io/levili/lzlogger/CryptoHelper");
    if (!local_class) {
        return -1;
    }
    g_crypto_helper_class = (jclass)(*env)->NewGlobalRef(env, local_class);
    (*env)->DeleteLocalRef(env, local_class);
    
    if (!g_crypto_helper_class) {
        return -1;
    }
    
    // 获取方法 ID
    g_derive_key_method = (*env)->GetStaticMethodID(env, g_crypto_helper_class, 
        "deriveKey", "(Ljava/lang/String;[B)[B");
    g_generate_salt_method = (*env)->GetStaticMethodID(env, g_crypto_helper_class, 
        "generateSalt", "()[B");
    g_process_aes_ctr_method = (*env)->GetStaticMethodID(env, g_crypto_helper_class, 
        "processAesCtr", "([B[BJ)[B");
    
    if (!g_derive_key_method || !g_generate_salt_method || !g_process_aes_ctr_method) {
        return -1;
    }
    
    return 0;
}

// 获取 JNIEnv (线程安全)
static JNIEnv* get_jni_env() {
    if (!g_jvm) {
        return NULL;
    }
    
    JNIEnv *env = NULL;
    int status = (*g_jvm)->GetEnv(g_jvm, (void**)&env, JNI_VERSION_1_6);
    
    if (status == JNI_EDETACHED) {
        // 当前线程未附加到 JVM, 需要附加
        status = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
        if (status != JNI_OK) {
            return NULL;
        }
    }
    
    return env;
}
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

#elif defined(__ANDROID__)
    // Android: 通过 JNI 调用 Java
    JNIEnv *env = get_jni_env();
    if (!env || !g_crypto_helper_class || !g_derive_key_method) {
        return -1;
    }
    
    // 创建 Java String
    jstring j_password = (*env)->NewStringUTF(env, password);
    if (!j_password) {
        return -1;
    }
    
    // 创建 Java byte[] (salt)
    jbyteArray j_salt = (*env)->NewByteArray(env, LZ_CRYPTO_SALT_SIZE);
    if (!j_salt) {
        (*env)->DeleteLocalRef(env, j_password);
        return -1;
    }
    (*env)->SetByteArrayRegion(env, j_salt, 0, LZ_CRYPTO_SALT_SIZE, (const jbyte*)salt);
    
    // 调用 Java 方法
    jbyteArray j_key = (jbyteArray)(*env)->CallStaticObjectMethod(env, g_crypto_helper_class, 
        g_derive_key_method, j_password, j_salt);
    
    (*env)->DeleteLocalRef(env, j_password);
    (*env)->DeleteLocalRef(env, j_salt);
    
    if (!j_key) {
        return -1;
    }
    
    // 获取结果
    jsize key_len = (*env)->GetArrayLength(env, j_key);
    if (key_len != LZ_CRYPTO_KEY_SIZE) {
        (*env)->DeleteLocalRef(env, j_key);
        return -1;
    }
    
    (*env)->GetByteArrayRegion(env, j_key, 0, LZ_CRYPTO_KEY_SIZE, (jbyte*)out_key);
    (*env)->DeleteLocalRef(env, j_key);
    
    return 0;

#else
    // 其他平台: 使用 OpenSSL
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

#elif defined(__ANDROID__)
    // Android: 通过 JNI 调用 Java
    JNIEnv *env = get_jni_env();
    if (!env || !g_crypto_helper_class || !g_generate_salt_method) {
        return -1;
    }
    
    // 调用 Java 方法
    jbyteArray j_salt = (jbyteArray)(*env)->CallStaticObjectMethod(env, g_crypto_helper_class, 
        g_generate_salt_method);
    
    if (!j_salt) {
        return -1;
    }
    
    // 获取结果
    jsize salt_len = (*env)->GetArrayLength(env, j_salt);
    if (salt_len != LZ_CRYPTO_SALT_SIZE) {
        (*env)->DeleteLocalRef(env, j_salt);
        return -1;
    }
    
    (*env)->GetByteArrayRegion(env, j_salt, 0, LZ_CRYPTO_SALT_SIZE, (jbyte*)salt);
    (*env)->DeleteLocalRef(env, j_salt);
    
    return 0;

#else
    // 其他平台: 使用 OpenSSL
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
    (void)block_offset; // Used in iOS/Android specific code

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

#elif defined(__ANDROID__)
    // Android: 通过 JNI 调用 Java
    JNIEnv *env = get_jni_env();
    if (!env || !g_crypto_helper_class || !g_process_aes_ctr_method) {
        return -1;
    }
    
    // 创建 Java byte[] (key)
    jbyteArray j_key = (*env)->NewByteArray(env, LZ_CRYPTO_KEY_SIZE);
    if (!j_key) {
        return -1;
    }
    (*env)->SetByteArrayRegion(env, j_key, 0, LZ_CRYPTO_KEY_SIZE, (const jbyte*)ctx->key);
    
    // 创建 Java byte[] (data)
    jbyteArray j_data = (*env)->NewByteArray(env, (jsize)length);
    if (!j_data) {
        (*env)->DeleteLocalRef(env, j_key);
        return -1;
    }
    (*env)->SetByteArrayRegion(env, j_data, 0, (jsize)length, (const jbyte*)input);
    
    // 调用 Java 方法 (注意: offset 需要考虑块内偏移)
    jbyteArray j_result = (jbyteArray)(*env)->CallStaticObjectMethod(env, g_crypto_helper_class, 
        g_process_aes_ctr_method, j_key, j_data, (jlong)offset);
    
    (*env)->DeleteLocalRef(env, j_key);
    (*env)->DeleteLocalRef(env, j_data);
    
    if (!j_result) {
        return -1;
    }
    
    // 获取结果
    jsize result_len = (*env)->GetArrayLength(env, j_result);
    if (result_len != (jsize)length) {
        (*env)->DeleteLocalRef(env, j_result);
        return -1;
    }
    
    (*env)->GetByteArrayRegion(env, j_result, 0, (jsize)length, (jbyte*)output);
    (*env)->DeleteLocalRef(env, j_result);
    
    return 0;

#else
    // 其他平台: 使用 OpenSSL
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
