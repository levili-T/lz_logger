#ifndef LZ_LOGGER_H
#define LZ_LOGGER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Error Codes
// ============================================================================

/** 错误码定义 */
typedef enum {
    LZ_LOG_SUCCESS = 0,                   // 成功
    LZ_LOG_ERROR_INVALID_PARAM = -1,      // 无效参数
    LZ_LOG_ERROR_INVALID_HANDLE = -2,     // 无效句柄
    LZ_LOG_ERROR_OUT_OF_MEMORY = -3,      // 内存不足
    LZ_LOG_ERROR_FILE_NOT_FOUND = -4,     // 文件未找到
    LZ_LOG_ERROR_FILE_CREATE = -5,        // 文件创建失败
    LZ_LOG_ERROR_FILE_OPEN = -6,          // 文件打开失败
    LZ_LOG_ERROR_FILE_WRITE = -7,         // 文件写入失败
    LZ_LOG_ERROR_FILE_EXTEND = -8,        // 文件扩展失败
    LZ_LOG_ERROR_MMAP_FAILED = -9,        // mmap映射失败
    LZ_LOG_ERROR_MUNMAP_FAILED = -10,     // munmap解映射失败
    LZ_LOG_ERROR_FILE_SIZE_EXCEED = -11,  // 文件大小超限
    LZ_LOG_ERROR_ENCRYPTION = -12,        // 加密错误
    LZ_LOG_ERROR_DIR_ACCESS = -13,        // 目录访问失败
    LZ_LOG_ERROR_SYSTEM = -100,           // 系统错误（携带errno）
} lz_log_error_t;

/** 日志级别 */
typedef enum {
    LZ_LOG_LEVEL_VERBOSE = 0,
    LZ_LOG_LEVEL_DEBUG = 1,
    LZ_LOG_LEVEL_INFO = 2,
    LZ_LOG_LEVEL_WARN = 3,
    LZ_LOG_LEVEL_ERROR = 4,
    LZ_LOG_LEVEL_FATAL = 5,
} lz_log_level_t;

// ============================================================================
// Opaque Handle
// ============================================================================

/** 日志句柄（对外不透明） */
typedef struct lz_logger_context_t* lz_logger_handle_t;

// ============================================================================
// Configuration Constants
// ============================================================================

/** 默认文件大小：5MB */
#define LZ_LOG_DEFAULT_FILE_SIZE (5 * 1024 * 1024)

/** 最大文件大小：7MB */
#define LZ_LOG_MAX_FILE_SIZE (7 * 1024 * 1024)

/** 文件尾部魔数标记 */
#define LZ_LOG_MAGIC_ENDX 0x456E6478  // "Endx" in hex

/** 文件尾部元数据大小（魔数4字节 + 已用大小4字节） */
#define LZ_LOG_FOOTER_SIZE 8

// ============================================================================
// Public APIs
// ============================================================================

/**
 * 设置日志文件最大大小
 * @param size 文件大小（字节），范围 [1KB, 7MB]
 * @return 错误码
 * @note 必须在 lz_logger_open 之前调用
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_set_max_file_size(uint32_t size);

/**
 * 打开/创建日志系统
 * @param log_dir 日志目录路径（必须已存在）
 * @param encrypt_key 加密密钥（可为NULL表示不加密）
 * @param out_handle 输出句柄指针
 * @param out_error 输出详细错误码（可为NULL）
 * @return 错误码
 * 
 * 文件命名规则：yyyy-mm-dd-(num).log
 * 例如：2025-10-30-0.log, 2025-10-30-1.log
 * 
 * 文件结构：
 * [日志数据区域 N字节]
 * [魔数 ENDX 4字节]
 * [已使用大小 4字节]
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_open(
    const char *log_dir,
    const char *encrypt_key,
    lz_logger_handle_t *out_handle,
    int32_t *out_error
);

/**
 * 写入日志
 * @param handle 日志句柄
 * @param level 日志级别
 * @param tag 标签
 * @param file_name 文件名（可为NULL）
 * @param line 行号
 * @param message 日志内容
 * @return 错误码
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_write(
    lz_logger_handle_t handle,
    lz_log_level_t level,
    const char *tag,
    const char *file_name,
    int line,
    const char *message
);

/**
 * 同步日志到磁盘
 * @param handle 日志句柄
 * @return 错误码
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_flush(lz_logger_handle_t handle);

/**
 * 关闭日志系统
 * @param handle 日志句柄
 * @return 错误码
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_close(lz_logger_handle_t handle);

/**
 * 获取错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
FFI_PLUGIN_EXPORT const char* lz_logger_error_string(lz_log_error_t error);

// ============================================================================
// Legacy API (deprecated, for backward compatibility)
// ============================================================================

FFI_PLUGIN_EXPORT void lz_logger(int loglevel, const char *tag,
                                  const char *file_name, int line,
                                  const char *message);

#ifdef __cplusplus
}
#endif

#endif // LZ_LOGGER_H
