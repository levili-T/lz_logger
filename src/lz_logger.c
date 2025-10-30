#include "lz_logger.h"
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <dirent.h>
#include <stdatomic.h>

// ============================================================================
// Platform Specific
// ============================================================================

#ifdef _WIN32
    #include <windows.h>
    #define PATH_SEPARATOR '\\'
#else
    #define PATH_SEPARATOR '/'
#endif

// ============================================================================
// Internal Structures
// ============================================================================

/** 日志上下文结构（对外隐藏） */
typedef struct lz_logger_context_t {
    char log_dir[512];                    // 日志目录
    char encrypt_key[256];                // 加密密钥
    char current_file_path[768];          // 当前日志文件路径
    
    int fd;                               // 文件描述符
    void *mmap_ptr;                       // mmap映射指针
    uint32_t file_size;                   // 文件总大小
    
    atomic_uint_least32_t *cur_offset_ptr; // 当前写入偏移量指针（指向文件尾部-4字节）
    
    uint32_t max_file_size;               // 最大文件大小
    
    uint8_t is_encrypted;                 // 是否加密
} lz_logger_context_t;

// ============================================================================
// Global Configuration
// ============================================================================

/** 全局配置：最大文件大小 */
static atomic_uint_least32_t g_max_file_size = LZ_LOG_DEFAULT_FILE_SIZE;

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * 获取当前日期字符串
 * @param out_date 输出缓冲区，格式：yyyy-mm-dd
 * @param buf_size 缓冲区大小
 */
static void get_current_date_string(char *out_date, size_t buf_size) {
    memset(out_date, 0, buf_size);
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(out_date, buf_size, "%Y-%m-%d", tm_info);
}

/**
 * 检查目录是否存在且可访问
 * @param dir_path 目录路径
 * @return 错误码
 */
static lz_log_error_t check_directory_access(const char *dir_path) {
    struct stat st;
    
    do {
        if (dir_path == NULL || strlen(dir_path) == 0) {
            return LZ_LOG_ERROR_INVALID_PARAM;
        }
        
        if (stat(dir_path, &st) != 0) {
            return LZ_LOG_ERROR_DIR_ACCESS;
        }
        
        if (!S_ISDIR(st.st_mode)) {
            return LZ_LOG_ERROR_DIR_ACCESS;
        }
        
        // 检查读写权限
        if (access(dir_path, R_OK | W_OK) != 0) {
            return LZ_LOG_ERROR_DIR_ACCESS;
        }
    } while (0);
    
    return LZ_LOG_SUCCESS;
}

/**
 * 查找今日最新的日志文件编号
 * @param log_dir 日志目录
 * @param date_prefix 日期前缀（如 "2025-10-30"）
 * @param out_max_num 输出最大编号（如果不存在返回-1）
 * @return 错误码
 */
static lz_log_error_t find_latest_log_number(const char *log_dir, 
                                               const char *date_prefix,
                                               int *out_max_num) {
    DIR *dir = NULL;
    struct dirent *entry;
    int max_num = -1;
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    
    do {
        dir = opendir(log_dir);
        if (dir == NULL) {
            ret = LZ_LOG_ERROR_DIR_ACCESS;
            break;
        }
        
        size_t prefix_len = strlen(date_prefix);
        
        // 遍历目录查找匹配的日志文件
        while ((entry = readdir(dir)) != NULL) {
            // 检查文件名前缀是否匹配
            if (strncmp(entry->d_name, date_prefix, prefix_len) != 0) {
                continue;
            }
            
            // 解析文件编号：yyyy-mm-dd-(num).log
            const char *num_start = entry->d_name + prefix_len;
            if (*num_start != '-') {
                continue;
            }
            num_start++;
            
            char *end_ptr;
            long num = strtol(num_start, &end_ptr, 10);
            
            // 检查是否以 .log 结尾
            if (strcmp(end_ptr, ".log") == 0 && num >= 0) {
                if (num > max_num) {
                    max_num = (int)num;
                }
            }
        }
        
        *out_max_num = max_num;
        
    } while (0);
    
    if (dir != NULL) {
        closedir(dir);
    }
    
    return ret;
}

/**
 * 构造日志文件路径
 * @param log_dir 日志目录
 * @param date_str 日期字符串
 * @param file_num 文件编号
 * @param out_path 输出路径缓冲区
 * @param path_size 缓冲区大小
 */
static void build_log_file_path(const char *log_dir, 
                                  const char *date_str, 
                                  int file_num,
                                  char *out_path, 
                                  size_t path_size) {
    memset(out_path, 0, path_size);
    snprintf(out_path, path_size, "%s%c%s-%d.log", 
             log_dir, PATH_SEPARATOR, date_str, file_num);
}

/**
 * 创建并扩展日志文件
 * @param file_path 文件路径
 * @param file_size 文件大小
 * @param out_fd 输出文件描述符
 * @return 错误码
 */
static lz_log_error_t create_and_extend_file(const char *file_path,
                                               uint32_t file_size,
                                               int *out_fd) {
    int fd = -1;
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    
    do {
        // 创建文件（如果已存在则失败）
        fd = open(file_path, O_RDWR | O_CREAT | O_EXCL, 0644);
        if (fd < 0) {
            ret = LZ_LOG_ERROR_FILE_CREATE;
            break;
        }
        
        // 扩展文件大小（使用 ftruncate）
        if (ftruncate(fd, file_size) != 0) {
            ret = LZ_LOG_ERROR_FILE_EXTEND;
            break;
        }
        
        // 写入文件尾部元数据：[ENDX魔数 4字节][已用大小0 4字节]
        uint32_t magic = LZ_LOG_MAGIC_ENDX;
        uint32_t used_size = 0;
        
        off_t footer_offset = file_size - LZ_LOG_FOOTER_SIZE;
        
        if (lseek(fd, footer_offset, SEEK_SET) != footer_offset) {
            ret = LZ_LOG_ERROR_FILE_WRITE;
            break;
        }
        
        if (write(fd, &magic, sizeof(magic)) != sizeof(magic)) {
            ret = LZ_LOG_ERROR_FILE_WRITE;
            break;
        }
        
        if (write(fd, &used_size, sizeof(used_size)) != sizeof(used_size)) {
            ret = LZ_LOG_ERROR_FILE_WRITE;
            break;
        }
        
        // 同步到磁盘（防止 SIGBUS）
        if (fsync(fd) != 0) {
            ret = LZ_LOG_ERROR_FILE_WRITE;
            break;
        }
        
        *out_fd = fd;
        
    } while (0);
    
    // 错误处理：清理资源
    if (ret != LZ_LOG_SUCCESS && fd >= 0) {
        close(fd);
        unlink(file_path);  // 删除不完整的文件
    }
    
    return ret;
}

/**
 * 打开已存在的日志文件
 * @param file_path 文件路径
 * @param out_fd 输出文件描述符
 * @param out_used_size 输出已使用大小
 * @return 错误码
 */
static lz_log_error_t open_existing_file(const char *file_path,
                                           int *out_fd,
                                           uint32_t *out_used_size) {
    int fd = -1;
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    
    do {
        fd = open(file_path, O_RDWR);
        if (fd < 0) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        // 获取文件大小
        struct stat st;
        if (fstat(fd, &st) != 0) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        // 检查文件大小是否合法
        if (st.st_size < LZ_LOG_FOOTER_SIZE) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        // 读取文件尾部已使用大小（跳过魔数，只读取最后4字节）
        off_t used_size_offset = st.st_size - sizeof(uint32_t);
        if (lseek(fd, used_size_offset, SEEK_SET) != used_size_offset) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        uint32_t used_size = 0;
        
        if (read(fd, &used_size, sizeof(used_size)) != sizeof(used_size)) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        // 验证已使用大小
        if (used_size > (uint32_t)st.st_size - LZ_LOG_FOOTER_SIZE) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        *out_fd = fd;
        *out_used_size = used_size;
        
    } while (0);
    
    if (ret != LZ_LOG_SUCCESS && fd >= 0) {
        close(fd);
    }
    
    return ret;
}

/**
 * 执行 mmap 映射
 * @param fd 文件描述符
 * @param file_size 文件大小
 * @param out_mmap_ptr 输出映射指针
 * @param out_offset_ptr 输出偏移量指针（指向文件尾部-4字节）
 * @return 错误码
 */
static lz_log_error_t do_mmap_mapping(int fd,
                                        uint32_t file_size,
                                        void **out_mmap_ptr,
                                        atomic_uint_least32_t **out_offset_ptr) {
    void *ptr = NULL;
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    
    do {
        // 执行 mmap 映射（读写、共享）
        ptr = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            ret = LZ_LOG_ERROR_MMAP_FAILED;
            break;
        }
        
        // 计算偏移量指针位置：文件尾部倒数第4字节
        atomic_uint_least32_t *offset_ptr = 
            (atomic_uint_least32_t *)((uint8_t *)ptr + file_size - sizeof(uint32_t));
        
        *out_mmap_ptr = ptr;
        *out_offset_ptr = offset_ptr;
        
    } while (0);
    
    return ret;
}

// ============================================================================
// Public API Implementation
// ============================================================================

lz_log_error_t lz_logger_set_max_file_size(uint32_t size) {
    do {
        // 参数校验：范围 [1KB, 7MB]
        if (size < LZ_LOG_DEFAULT_FILE_SIZE || size > LZ_LOG_MAX_FILE_SIZE) {
            return LZ_LOG_ERROR_INVALID_PARAM;
        }
        
        atomic_store(&g_max_file_size, size);
        
    } while (0);
    
    return LZ_LOG_SUCCESS;
}

lz_log_error_t lz_logger_open(const char *log_dir,
                                const char *encrypt_key,
                                lz_logger_handle_t *out_handle,
                                int32_t *out_error) {
    lz_logger_context_t *ctx = NULL;
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    int sys_errno = 0;
    
    do {
        // 参数校验
        if (log_dir == NULL || out_handle == NULL) {
            ret = LZ_LOG_ERROR_INVALID_PARAM;
            break;
        }
        
        // 检查目录访问权限
        ret = check_directory_access(log_dir);
        if (ret != LZ_LOG_SUCCESS) {
            break;
        }
        
        // 分配上下文结构
        ctx = (lz_logger_context_t *)calloc(1, sizeof(lz_logger_context_t));
        if (ctx == NULL) {
            ret = LZ_LOG_ERROR_OUT_OF_MEMORY;
            break;
        }
        
        // 初始化上下文
        strncpy(ctx->log_dir, log_dir, sizeof(ctx->log_dir) - 1);
        if (encrypt_key != NULL) {
            strncpy(ctx->encrypt_key, encrypt_key, sizeof(ctx->encrypt_key) - 1);
            ctx->is_encrypted = 1;
        }
        
        ctx->max_file_size = atomic_load(&g_max_file_size);
        ctx->file_size = ctx->max_file_size;
        ctx->fd = -1;
        
        // 获取当前日期
        char date_str[16];
        get_current_date_string(date_str, sizeof(date_str));
        
        // 查找今日最新的日志文件编号
        int max_num = -1;
        ret = find_latest_log_number(log_dir, date_str, &max_num);
        if (ret != LZ_LOG_SUCCESS) {
            break;
        }
        
        // 尝试打开已存在的文件或创建新文件
        int file_num = (max_num >= 0) ? max_num : 0;
        uint32_t used_size = 0;
        int fd = -1;
        
        if (max_num >= 0) {
            // 尝试打开已存在的文件
            build_log_file_path(log_dir, date_str, file_num, 
                                ctx->current_file_path, sizeof(ctx->current_file_path));
            
            ret = open_existing_file(ctx->current_file_path, &fd, &used_size);
            
            // 如果文件已满，创建新文件
            if (ret == LZ_LOG_SUCCESS && 
                used_size >= ctx->file_size - LZ_LOG_FOOTER_SIZE) {
                close(fd);
                fd = -1;
                file_num++;
                ret = LZ_LOG_ERROR_FILE_NOT_FOUND;  // 标记需要创建新文件
            }
        }
        
        // 如果需要创建新文件
        if (fd < 0) {
            build_log_file_path(log_dir, date_str, file_num, 
                                ctx->current_file_path, sizeof(ctx->current_file_path));
            
            ret = create_and_extend_file(ctx->current_file_path, 
                                          ctx->file_size, &fd);
            if (ret != LZ_LOG_SUCCESS) {
                sys_errno = errno;
                break;
            }
            
            used_size = 0;
        }
        
        ctx->fd = fd;
        
        // 执行 mmap 映射
        ret = do_mmap_mapping(fd, ctx->file_size, 
                               &ctx->mmap_ptr, &ctx->cur_offset_ptr);
        if (ret != LZ_LOG_SUCCESS) {
            sys_errno = errno;
            break;
        }
        
        // 同步文件中的偏移量（如果不一致则更新）
        uint32_t file_offset = atomic_load(ctx->cur_offset_ptr);
        if (file_offset != used_size) {
            atomic_store(ctx->cur_offset_ptr, used_size);
            msync(ctx->mmap_ptr, ctx->file_size, MS_SYNC);
        }
        
        *out_handle = ctx;
        ret = LZ_LOG_SUCCESS;
        
    } while (0);
    
    // 错误处理：清理资源
    if (ret != LZ_LOG_SUCCESS) {
        if (ctx != NULL) {
            if (ctx->mmap_ptr != NULL && ctx->mmap_ptr != MAP_FAILED) {
                munmap(ctx->mmap_ptr, ctx->file_size);
            }
            if (ctx->fd >= 0) {
                close(ctx->fd);
            }
            free(ctx);
        }
        
        if (out_handle != NULL) {
            *out_handle = NULL;
        }
    }
    
    // 输出系统错误码
    if (out_error != NULL) {
        *out_error = sys_errno;
    }
    
    return ret;
}

const char* lz_logger_error_string(lz_log_error_t error) {
    switch (error) {
        case LZ_LOG_SUCCESS: return "Success";
        case LZ_LOG_ERROR_INVALID_PARAM: return "Invalid parameter";
        case LZ_LOG_ERROR_INVALID_HANDLE: return "Invalid handle";
        case LZ_LOG_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case LZ_LOG_ERROR_FILE_NOT_FOUND: return "File not found";
        case LZ_LOG_ERROR_FILE_CREATE: return "File create failed";
        case LZ_LOG_ERROR_FILE_OPEN: return "File open failed";
        case LZ_LOG_ERROR_FILE_WRITE: return "File write failed";
        case LZ_LOG_ERROR_FILE_EXTEND: return "File extend failed";
        case LZ_LOG_ERROR_MMAP_FAILED: return "Mmap failed";
        case LZ_LOG_ERROR_MUNMAP_FAILED: return "Munmap failed";
        case LZ_LOG_ERROR_FILE_SIZE_EXCEED: return "File size exceeded";
        case LZ_LOG_ERROR_ENCRYPTION: return "Encryption error";
        case LZ_LOG_ERROR_DIR_ACCESS: return "Directory access failed";
        case LZ_LOG_ERROR_SYSTEM: return "System error";
        default: return "Unknown error";
    }
}
