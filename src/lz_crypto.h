#ifndef LZ_CRYPTO_H
#define LZ_CRYPTO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// AES-CTR 加密模块
// ============================================================================

/** AES 密钥长度 (256-bit) */
#define LZ_CRYPTO_KEY_SIZE 32

/** AES 块大小 */
#define LZ_CRYPTO_BLOCK_SIZE 16

/** PBKDF2 盐长度 */
#define LZ_CRYPTO_SALT_SIZE 16

/** PBKDF2 迭代次数 */
#define LZ_CRYPTO_PBKDF2_ITERATIONS 10000

/** 加密上下文 */
typedef struct lz_crypto_context_t {
    uint8_t key[LZ_CRYPTO_KEY_SIZE];     // AES-256 密钥
    uint8_t *salt_ptr;                    // 盐值指针(指向mmap文件尾部)
    bool is_initialized;                  // 是否已初始化
} lz_crypto_context_t;

/**
 * 从密码派生密钥 (PBKDF2-HMAC-SHA256)
 * @param password 用户密码
 * @param password_len 密码长度
 * @param salt 盐值 (16字节)
 * @param out_key 输出密钥 (32字节)
 * @return 成功返回 0, 失败返回 -1
 */
int lz_crypto_derive_key(
    const char *password,
    size_t password_len,
    const uint8_t *salt,
    uint8_t *out_key
);

/**
 * 初始化加密上下文
 * @param ctx 加密上下文
 * @param password 用户密码 (NULL 表示不加密)
 * @param salt 盐值 (NULL 则自动生成)
 * @return 成功返回 0, 失败返回 -1
 */
int lz_crypto_init(
    lz_crypto_context_t *ctx,
    const char *password,
    const uint8_t *salt
);

/**
 * AES-CTR 加密/解密 (流式操作)
 * @param ctx 加密上下文
 * @param input 输入数据
 * @param output 输出数据 (可与 input 相同,原地操作)
 * @param length 数据长度
 * @param offset 文件偏移量 (用于计算 counter)
 * @return 成功返回 0, 失败返回 -1
 * 
 * 注意: AES-CTR 加密和解密是同一个操作 (XOR)
 */
int lz_crypto_process(
    lz_crypto_context_t *ctx,
    const uint8_t *input,
    uint8_t *output,
    size_t length,
    uint64_t offset
);

/**
 * 生成随机盐值
 * @param salt 输出盐值 (16字节)
 * @return 成功返回 0, 失败返回 -1
 */
int lz_crypto_generate_salt(uint8_t *salt);

/**
 * 清理加密上下文 (安全擦除密钥)
 * @param ctx 加密上下文
 */
void lz_crypto_cleanup(lz_crypto_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // LZ_CRYPTO_H
