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
    LZ_LOG_ERROR_INVALID_MMAP = -12,      // mmap映射无效
    LZ_LOG_ERROR_DIR_ACCESS = -13,        // 目录访问失败
    LZ_LOG_ERROR_HANDLE_CLOSED = -14,     // 句柄已关闭
    LZ_LOG_ERROR_FILE_SWITCH = -15,       // 文件切换失败
    LZ_LOG_ERROR_MUTEX_LOCK = -16,        // 互斥锁失败
    LZ_LOG_ERROR_SYSTEM = -100,           // 系统错误（携带errno）
} lz_log_error_t;

// ============================================================================
// Opaque Handle
// ============================================================================

/** 日志句柄（对外不透明） */
typedef struct lz_logger_context_t* lz_logger_handle_t;

// ============================================================================
// Configuration Constants
// ============================================================================

/** 最小文件大小：1MB（用于测试频繁切换） */
#define LZ_LOG_MIN_FILE_SIZE (1 * 1024 * 1024)

/** 默认文件大小：5MB */
#define LZ_LOG_DEFAULT_FILE_SIZE (5 * 1024 * 1024)

/** 最大文件大小：7MB */
#define LZ_LOG_MAX_FILE_SIZE (7 * 1024 * 1024)

/** 文件尾部魔数标记 */
#define LZ_LOG_MAGIC_ENDX 0x456E6478  // "Endx" in hex

/** 加密盐大小 */
#define LZ_LOG_SALT_SIZE 16

/** 文件尾部元数据大小（盐16字节 + 魔数4字节 + 文件大小4字节 + 已用大小4字节） */
#define LZ_LOG_FOOTER_SIZE 28

// ============================================================================
// Public APIs
// ============================================================================

/**
 * 设置日志文件最大大小
 * @param size 文件大小（字节），范围 [1MB, 7MB]
 * @return 错误码
 * @note 建议在 lz_logger_open 之前调用
 * @note 运行时修改会影响后续新创建的文件，已有文件保持原有大小
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_set_max_file_size(uint32_t size);

/**
 * 打开/创建日志系统
 * @param log_dir 日志目录路径（必须已存在）
 * @param encrypt_key 加密密钥（可为NULL表示不加密）
 * @param out_handle 输出句柄指针
 * @param out_inner_error 输出内部错误码（可为NULL）
 * @param out_sys_errno 输出系统errno（可为NULL），用于调试系统API失败原因
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
    int32_t *out_inner_error,
    int32_t *out_sys_errno
);

/**
 * 写入日志
 * @param handle 日志句柄
 * @param message 日志内容
 * @param len 日志长度
 * @return 错误码
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_write(
    lz_logger_handle_t handle,
    const char *message,
    uint32_t len
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

/**
 * 清理过期日志文件
 * @param log_dir 日志目录路径
 * @param days 保留天数（删除此天数之前的所有日志文件）
 * @return 错误码
 * 
 * 示例：days=7 表示保留最近7天的日志，删除7天前的所有日志文件
 * 根据文件名格式 yyyy-mm-dd-(num).log 解析日期并判断是否过期
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_cleanup_expired_logs(
    const char *log_dir,
    int days
);

/**
 * 导出当前日志文件
 * @param handle 日志句柄
 * @param out_export_path 输出导出文件路径（缓冲区至少1024字节）
 * @param path_buffer_size 路径缓冲区大小
 * @return 错误码
 * 
 * 将当前正在写入的日志文件导出为 export.log
 * - 如果 export.log 已存在，则先删除
 * - 直接从 mmap 读取数据，无需 flush
 * - 只导出已写入的数据部分（不包含 footer）
 * - 返回导出文件的完整路径
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_export_current_log(
    lz_logger_handle_t handle,
    char *out_export_path,
    uint32_t path_buffer_size
);

#ifdef __cplusplus
}
#endif

#endif // LZ_LOGGER_H
