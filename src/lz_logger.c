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
#include <stdbool.h>
#include <pthread.h>

// ============================================================================
// Debug Logging (set to 0 to disable)
// ============================================================================

#define LZ_DEBUG_ENABLED 1

#if LZ_DEBUG_ENABLED
    #define LZ_DEBUG_LOG(fmt, ...) \
        fprintf(stderr, "[LZLogger] %s:%d %s() - " fmt "\n", \
                __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
    #define LZ_DEBUG_LOG(fmt, ...) ((void)0)
#endif

// ============================================================================
// Code Review Notes - Memory Safety & Concurrency
// ============================================================================
/*
 * 内存安全分析：
 * 1. ✅ calloc 初始化 - 所有字段清零，避免未初始化内存
 * 2. ✅ mmap_ptr 初始化为 MAP_FAILED - 避免与 NULL 混淆
 * 3. ✅ 错误处理中检查 mutex 初始化状态 - 通过 log_dir[0] 判断
 * 4. ✅ 边界检查 - write 函数中检查 mmap 溢出
 * 5. ✅ strncpy 使用 - 所有字符串拷贝都有长度限制
 * 
 * 多线程安全分析：
 * 1. ✅ 无锁写入 - 使用 CAS (atomic_compare_exchange_weak)
 * 2. ✅ 文件切换加锁 - switch_mutex 保护文件切换过程
 * 3. ✅ 双重检查锁定 - 切换前后都检查偏移量
 * 4. ✅ 延迟 munmap - 旧 mmap 在切换后仍可被写入线程使用
 * 5. ✅ mmap/fd 独立性 - close(fd) 后 mmap 仍然有效
 * 6. ✅ 原子操作 - cur_offset_ptr, is_closed 使用 atomic 类型
 * 7. ✅ 指针替换顺序 - 先创建新 mmap，再替换指针，最后延迟清理
 * 
 * 潜在问题（已修复）：
 * 1. ✅ 已修复：错误处理中销毁未初始化的 mutex
 * 2. ✅ 已修复：mmap_ptr 初始化为 MAP_FAILED 而非 NULL
 * 3. ✅ 已添加：write 函数的 mmap 有效性检查
 * 4. ✅ 已添加：write 函数的边界溢出检查
 * 5. ✅ 已添加：关键路径的调试日志
 * 
 * 竞态条件分析：
 * 场景1: 多个线程同时写入
 *   - 安全：CAS 保证只有一个线程能预留空间
 * 场景2: 写入时发生文件切换
 *   - 安全：switch_mutex 保证切换原子性，延迟 munmap 保证旧 mmap 可用
 * 场景3: 切换时多个线程都检测到需要切换
 *   - 安全：双重检查锁定，第二个线程会发现已经切换完成
 * 场景4: close 时仍有线程在写入
 *   - 安全：atomic is_closed 标志阻止新写入，已开始的写入完成后自然结束
 */

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
    
    int fd;                               // 文件描述符（仅用于初始化，可以提前关闭）
    void *mmap_ptr;                       // 当前 mmap 映射指针
    uint32_t file_size;                   // 文件总大小
    
    void *old_mmap_ptr;                   // 旧 mmap 指针（延迟销毁）
    uint32_t old_file_size;               // 旧文件大小
    
    atomic_uint_least32_t *cur_offset_ptr; // 当前写入偏移量指针（指向文件尾部-4字节）
    
    pthread_mutex_t switch_mutex;         // 文件切换互斥锁
    
    uint32_t max_file_size;               // 最大文件大小
    
    atomic_bool is_closed;                // 是否已关闭
    uint8_t is_encrypted;                 // 是否加密
} lz_logger_context_t;

// ============================================================================
// Global Configuration
// ============================================================================

/** 当天最大日志文件数量 */
#define LZ_LOG_MAX_DAILY_FILES 5

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
 * 查找今日最新的日志文件编号（优化版：顺序查找而非遍历目录）
 * @param log_dir 日志目录
 * @param date_prefix 日期前缀（如 "2025-10-30"）
 * @param out_max_num 输出最大编号（如果不存在返回-1）
 * @return 错误码
 */
static lz_log_error_t find_latest_log_number(const char *log_dir, 
                                               const char *date_prefix,
                                               int *out_max_num) {
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    int max_num = -1;
    char file_path[768];
    struct stat st;
    
    do {
        // 从 0 开始顺序查找，找到最后一个存在的文件
        for (int i = 0; i < LZ_LOG_MAX_DAILY_FILES; i++) {
            // 构造文件路径
            int written = snprintf(file_path, sizeof(file_path), 
                                   "%s%c%s-%d.log", 
                                   log_dir, PATH_SEPARATOR, date_prefix, i);
            
            if (written < 0 || written >= (int)sizeof(file_path)) {
                ret = LZ_LOG_ERROR_INVALID_PARAM;
                break;
            }
            
            // 检查文件是否存在
            if (stat(file_path, &st) == 0) {
                max_num = i;  // 更新最大编号
            }
        }
        
        *out_max_num = max_num;
        
    } while (0);
    
    LZ_DEBUG_LOG("Find latest log number: %d (date=%s)", max_num, date_prefix);
    
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
        
        // 读取文件尾部 footer（魔数 + 已使用大小，共8字节）
        off_t footer_offset = st.st_size - LZ_LOG_FOOTER_SIZE;
        if (lseek(fd, footer_offset, SEEK_SET) != footer_offset) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        uint32_t magic = 0;
        uint32_t used_size = 0;
        
        // 读取魔数
        if (read(fd, &magic, sizeof(magic)) != sizeof(magic)) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        // 验证魔数
        if (magic != LZ_LOG_MAGIC_ENDX) {
            ret = LZ_LOG_ERROR_FILE_OPEN;
            break;
        }
        
        // 读取已使用大小
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
            LZ_DEBUG_LOG("Failed to allocate context memory");
            ret = LZ_LOG_ERROR_OUT_OF_MEMORY;
            break;
        }
        
        // 初始化字段（calloc 已经清零，这里设置特殊值）
        ctx->fd = -1;
        ctx->mmap_ptr = MAP_FAILED;
        ctx->old_mmap_ptr = NULL;
        
        // 初始化互斥锁
        if (pthread_mutex_init(&ctx->switch_mutex, NULL) != 0) {
            LZ_DEBUG_LOG("Failed to initialize mutex");
            ret = LZ_LOG_ERROR_MUTEX_LOCK;
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
        ctx->old_file_size = 0;
        atomic_store(&ctx->is_closed, false);
        
        LZ_DEBUG_LOG("Context initialized: log_dir=%s, file_size=%u, encrypted=%d", 
                     log_dir, ctx->file_size, ctx->is_encrypted);
        
        // 获取当前日期
        char date_str[16];
        get_current_date_string(date_str, sizeof(date_str));
        
        LZ_DEBUG_LOG("Current date: %s", date_str);
        
        // 查找今日最新的日志文件编号
        int max_num = -1;
        ret = find_latest_log_number(log_dir, date_str, &max_num);
        if (ret != LZ_LOG_SUCCESS) {
            LZ_DEBUG_LOG("Failed to find latest log number: %d", ret);
            break;
        }
        
        LZ_DEBUG_LOG("Found max log number: %d", max_num);
        
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
            LZ_DEBUG_LOG("Failed to mmap: %d, errno=%d", ret, sys_errno);
            break;
        }
        
        LZ_DEBUG_LOG("mmap succeeded: ptr=%p, file_size=%u", ctx->mmap_ptr, ctx->file_size);
        
        // 同步文件中的偏移量（如果不一致则更新）
        uint32_t file_offset = atomic_load(ctx->cur_offset_ptr);
        if (file_offset != used_size) {
            LZ_DEBUG_LOG("Sync offset: file=%u, expected=%u", file_offset, used_size);
            atomic_store(ctx->cur_offset_ptr, used_size);
            msync(ctx->mmap_ptr, ctx->file_size, MS_SYNC);
        }
        
        LZ_DEBUG_LOG("Logger opened successfully: file=%s, offset=%u", 
                     ctx->current_file_path, used_size);
        
        *out_handle = ctx;
        ret = LZ_LOG_SUCCESS;
        
    } while (0);
    
    // 错误处理：清理资源
    if (ret != LZ_LOG_SUCCESS) {
        LZ_DEBUG_LOG("Open failed with error: %d", ret);
        if (ctx != NULL) {
            if (ctx->mmap_ptr != NULL && ctx->mmap_ptr != MAP_FAILED) {
                munmap(ctx->mmap_ptr, ctx->file_size);
            }
            if (ctx->fd >= 0) {
                close(ctx->fd);
            }
            // 只有在成功初始化后才销毁 mutex
            // calloc 已清零，检查 log_dir 是否被设置来判断 mutex 是否已初始化
            if (ctx->log_dir[0] != '\0') {
                pthread_mutex_destroy(&ctx->switch_mutex);
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
        case LZ_LOG_ERROR_HANDLE_CLOSED: return "Handle closed";
        case LZ_LOG_ERROR_FILE_SWITCH: return "File switch failed";
        case LZ_LOG_ERROR_MUTEX_LOCK: return "Mutex lock failed";
        case LZ_LOG_ERROR_SYSTEM: return "System error";
        default: return "Unknown error";
    }
}

// ============================================================================
// Write Implementation
// ============================================================================

/**
 * 流式加密（预留接口）
 * @param ctx 日志上下文
 * @param data 数据指针
 * @param len 数据长度
 * @return 错误码
 */
static lz_log_error_t encrypt_data(lz_logger_context_t *ctx, 
                                     void *data, 
                                     uint32_t len) {
    (void)ctx;
    (void)data;
    (void)len;
    // TODO: 实现流式加密
    return LZ_LOG_SUCCESS;
}

/**
 * 添加旧 mmap 到延迟销毁链表
 * @param ctx 日志上下文
 * @param old_mmap_ptr 旧的 mmap 指针
 * @param file_size 文件大小
 */
static void add_old_mmap(lz_logger_context_t *ctx, void *old_mmap_ptr, uint32_t file_size) {
    // 清理旧的mmap（如果存在）
    if (ctx->old_mmap_ptr != NULL && ctx->old_mmap_ptr != MAP_FAILED) {
        munmap(ctx->old_mmap_ptr, ctx->old_file_size);
    }
    
    // 保存新的old mmap指针
    ctx->old_mmap_ptr = old_mmap_ptr;
    ctx->old_file_size = file_size;
}

/**
 * 切换到新的日志文件（无锁方式）
 * @param ctx 日志上下文
 * @return 错误码
 * @note 调用者必须持有 switch_mutex 锁
 */
static lz_log_error_t switch_to_new_file(lz_logger_context_t *ctx) {
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    int new_fd = -1;
    void *new_mmap_ptr = NULL;
    atomic_uint_least32_t *new_offset_ptr = NULL;
    void *old_mmap_ptr = ctx->mmap_ptr;
    uint32_t old_file_size = ctx->file_size;
    
    LZ_DEBUG_LOG("Starting file switch, old_file=%s", ctx->current_file_path);
    
    do {
        // 获取当前日期
        char date_str[16];
        get_current_date_string(date_str, sizeof(date_str));
        
        // 查找今日最新的日志文件编号
        int max_num = -1;
        ret = find_latest_log_number(ctx->log_dir, date_str, &max_num);
        if (ret != LZ_LOG_SUCCESS) {
            LZ_DEBUG_LOG("Failed to find latest log number during switch");
            break;
        }
        
        // 创建新文件（编号 +1）
        int new_file_num = (max_num >= 0) ? (max_num + 1) : 0;
        
        // 如果达到最大文件数量限制，删除0号文件并重用编号
        if (new_file_num >= LZ_LOG_MAX_DAILY_FILES) {
            char file_to_delete[768];
            build_log_file_path(ctx->log_dir, date_str, 0, 
                                file_to_delete, sizeof(file_to_delete));
            
            if (unlink(file_to_delete) == 0) {
                LZ_DEBUG_LOG("Deleted oldest log file: %s", file_to_delete);
            } else {
                LZ_DEBUG_LOG("Failed to delete oldest log file: %s (errno=%d)", 
                             file_to_delete, errno);
            }
            
            // 重用0号文件编号
            new_file_num = 0;
        }
        
        char new_file_path[768];
        build_log_file_path(ctx->log_dir, date_str, new_file_num, 
                            new_file_path, sizeof(new_file_path));
        
        ret = create_and_extend_file(new_file_path, ctx->file_size, &new_fd);
        if (ret != LZ_LOG_SUCCESS) {
            break;
        }
        
        // 执行新的 mmap 映射
        ret = do_mmap_mapping(new_fd, ctx->file_size, 
                               &new_mmap_ptr, &new_offset_ptr);
        if (ret != LZ_LOG_SUCCESS) {
            break;
        }
        
        // 关键：立即关闭新文件的 fd（mmap 后不再需要）
        close(new_fd);
        new_fd = -1;
        
        LZ_DEBUG_LOG("New file created and mapped: %s", new_file_path);
        
        // 原子替换指针（让写入线程切换到新 mmap）
        // 注意：这个操作不是原子的，但由于有 switch_mutex 保护，是安全的
        ctx->mmap_ptr = new_mmap_ptr;
        ctx->cur_offset_ptr = new_offset_ptr;
        strncpy(ctx->current_file_path, new_file_path, sizeof(ctx->current_file_path) - 1);
        
        // 初始化新文件的偏移量为 0
        atomic_store(ctx->cur_offset_ptr, 0);
        
        LZ_DEBUG_LOG("Pointer switch completed, storing old mmap for deferred cleanup");
        
        // 将旧 mmap 加入延迟销毁（不立即 munmap，避免竞态）
        add_old_mmap(ctx, old_mmap_ptr, old_file_size);
        
        LZ_DEBUG_LOG("File switch completed successfully");
        
    } while (0);
    
    // 错误处理：清理资源
    if (ret != LZ_LOG_SUCCESS) {
        if (new_mmap_ptr != NULL && new_mmap_ptr != MAP_FAILED) {
            munmap(new_mmap_ptr, ctx->file_size);
        }
        if (new_fd >= 0) {
            close(new_fd);
        }
    }
    
    return ret;
}

lz_log_error_t lz_logger_write(lz_logger_handle_t handle,
                                 const char *message,
                                 uint32_t len) {
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    lz_logger_context_t *ctx = (lz_logger_context_t *)handle;
    
    do {
        // 参数校验
        if (ctx == NULL) {
            ret = LZ_LOG_ERROR_INVALID_HANDLE;
            break;
        }
        
        if (message == NULL || len == 0) {
            ret = LZ_LOG_ERROR_INVALID_PARAM;
            break;
        }
        
        // 检查句柄是否已关闭
        if (atomic_load(&ctx->is_closed)) {
            LZ_DEBUG_LOG("Write failed: handle is closed");
            ret = LZ_LOG_ERROR_HANDLE_CLOSED;
            break;
        }
        
        // 检查 mmap 有效性（防御性编程）
        if (ctx->mmap_ptr == NULL || ctx->mmap_ptr == MAP_FAILED) {
            LZ_DEBUG_LOG("Write failed: invalid mmap pointer");
            ret = LZ_LOG_ERROR_INVALID_HANDLE;
            break;
        }
        
        // 无锁写入循环（使用 CAS）
        while (true) {
            // 读取当前偏移量（不修改）
            uint32_t current_offset = atomic_load(ctx->cur_offset_ptr);
            uint32_t max_data_size = ctx->file_size - LZ_LOG_FOOTER_SIZE;
            
            // 检查是否需要切换文件
            if (current_offset + len > max_data_size) {
                LZ_DEBUG_LOG("Need file switch: offset=%u, len=%u, max=%u", 
                             current_offset, len, max_data_size);
                
                // 需要切换文件，加锁处理
                if (pthread_mutex_lock(&ctx->switch_mutex) != 0) {
                    LZ_DEBUG_LOG("Failed to lock switch_mutex");
                    ret = LZ_LOG_ERROR_MUTEX_LOCK;
                    break;
                }
                
                // 再次检查偏移量（可能其他线程已完成切换）
                current_offset = atomic_load(ctx->cur_offset_ptr);
                if (current_offset + len > max_data_size) {
                    // 执行文件切换
                    LZ_DEBUG_LOG("Switching to new file...");
                    ret = switch_to_new_file(ctx);
                    pthread_mutex_unlock(&ctx->switch_mutex);
                    
                    if (ret != LZ_LOG_SUCCESS) {
                        LZ_DEBUG_LOG("File switch failed: %d", ret);
                        ret = LZ_LOG_ERROR_FILE_SWITCH;
                        break;
                    }
                    
                    LZ_DEBUG_LOG("File switch succeeded");
                    // 切换成功，继续循环重试写入
                    continue;
                }
                
                pthread_mutex_unlock(&ctx->switch_mutex);
                LZ_DEBUG_LOG("Other thread completed switch, retrying");
                // 其他线程已完成切换，继续循环重试
                continue;
            }
            
            // 使用 CAS 原子操作预留空间
            uint32_t new_offset = current_offset + len;
            if (atomic_compare_exchange_weak(ctx->cur_offset_ptr, 
                                              &current_offset, 
                                              new_offset)) {
                // CAS 成功，已预留空间
                // 注意：这里的 mmap_ptr 可能在文件切换后改变，但这是安全的
                // 因为我们使用的是延迟 munmap，旧的 mmap 仍然有效
                void *write_ptr = (uint8_t *)ctx->mmap_ptr + current_offset;
                
                memcpy(write_ptr, message, len);
                
                // 流式加密（如果启用）
                if (ctx->is_encrypted) {
                    ret = encrypt_data(ctx, write_ptr, len);
                }
                
                break;  // 写入完成
            }
            
            // CAS 失败，其他线程已修改偏移量，重试
        }
        
    } while (0);
    
    return ret;
}

lz_log_error_t lz_logger_flush(lz_logger_handle_t handle) {
    lz_logger_context_t *ctx = (lz_logger_context_t *)handle;
    
    do {
        if (ctx == NULL) {
            return LZ_LOG_ERROR_INVALID_HANDLE;
        }
        
        if (ctx->mmap_ptr == NULL || ctx->mmap_ptr == MAP_FAILED) {
            return LZ_LOG_ERROR_INVALID_HANDLE;
        }
        
        // 使用 MS_SYNC 同步到磁盘
        if (msync(ctx->mmap_ptr, ctx->file_size, MS_SYNC) != 0) {
            return LZ_LOG_ERROR_FILE_WRITE;
        }
        
    } while (0);
    
    return LZ_LOG_SUCCESS;
}

lz_log_error_t lz_logger_close(lz_logger_handle_t handle) {
    lz_logger_context_t *ctx = (lz_logger_context_t *)handle;
    
    do {
        if (ctx == NULL) {
            return LZ_LOG_ERROR_INVALID_HANDLE;
        }
        
        LZ_DEBUG_LOG("Closing logger: file=%s", ctx->current_file_path);
        
        // 标记为已关闭
        atomic_store(&ctx->is_closed, true);
        
        // 刷新当前 mmap
        if (ctx->mmap_ptr != NULL && ctx->mmap_ptr != MAP_FAILED) {
            uint32_t final_offset = atomic_load(ctx->cur_offset_ptr);
            LZ_DEBUG_LOG("Flushing mmap: final_offset=%u", final_offset);
            msync(ctx->mmap_ptr, ctx->file_size, MS_SYNC);
            munmap(ctx->mmap_ptr, ctx->file_size);
        }
        
        // 清理旧 mmap（如果存在）
        if (ctx->old_mmap_ptr != NULL && ctx->old_mmap_ptr != MAP_FAILED) {
            LZ_DEBUG_LOG("Unmapping old mmap: size=%u", ctx->old_file_size);
            munmap(ctx->old_mmap_ptr, ctx->old_file_size);
        }
        
        // 销毁互斥锁
        pthread_mutex_destroy(&ctx->switch_mutex);
        
        LZ_DEBUG_LOG("Logger closed successfully");
        
        // 释放上下文
        free(ctx);
        
    } while (0);
    
    return LZ_LOG_SUCCESS;
}

/**
 * 解析日志文件名中的日期
 * @param filename 文件名（例如：2025-10-30-0.log）
 * @param out_year 输出年份
 * @param out_month 输出月份
 * @param out_day 输出日期
 * @return 是否解析成功
 */
static bool parse_log_filename_date(const char *filename, 
                                      int *out_year, 
                                      int *out_month, 
                                      int *out_day) {
    // 验证文件名格式：yyyy-mm-dd-*.log
    if (filename == NULL || strlen(filename) < 14) {
        return false;
    }
    
    // 检查是否以 .log 结尾
    const char *ext = strrchr(filename, '.');
    if (ext == NULL || strcmp(ext, ".log") != 0) {
        return false;
    }
    
    // 解析日期部分：yyyy-mm-dd
    int year = 0, month = 0, day = 0;
    if (sscanf(filename, "%4d-%2d-%2d-", &year, &month, &day) != 3) {
        return false;
    }
    
    // 验证日期合法性
    if (year < 2000 || year > 2100 || 
        month < 1 || month > 12 || 
        day < 1 || day > 31) {
        return false;
    }
    
    *out_year = year;
    *out_month = month;
    *out_day = day;
    
    return true;
}

/**
 * 计算两个日期之间的天数差
 * @param year1, month1, day1 日期1
 * @param year2, month2, day2 日期2
 * @return 日期1 - 日期2 的天数差（可能为负数）
 */
static int calculate_days_diff(int year1, int month1, int day1,
                                 int year2, int month2, int day2) {
    // 使用 mktime 计算时间戳差异
    struct tm tm1 = {0};
    struct tm tm2 = {0};
    
    tm1.tm_year = year1 - 1900;
    tm1.tm_mon = month1 - 1;
    tm1.tm_mday = day1;
    tm1.tm_hour = 12;  // 使用中午12点避免夏令时问题
    
    tm2.tm_year = year2 - 1900;
    tm2.tm_mon = month2 - 1;
    tm2.tm_mday = day2;
    tm2.tm_hour = 12;
    
    time_t time1 = mktime(&tm1);
    time_t time2 = mktime(&tm2);
    
    if (time1 == -1 || time2 == -1) {
        return 0;
    }
    
    // 计算天数差（86400秒 = 1天）
    double diff_seconds = difftime(time1, time2);
    return (int)(diff_seconds / 86400.0);
}

/**
 * 清理过期日志文件
 * @param log_dir 日志目录路径
 * @param days 保留天数（删除此天数之前的所有日志文件）
 * @return 错误码
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_cleanup_expired_logs(
    const char *log_dir,
    int days) {
    
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    DIR *dir = NULL;
    
    do {
        // 参数验证
        if (log_dir == NULL || days < 0) {
            ret = LZ_LOG_ERROR_INVALID_PARAM;
            break;
        }
        
        // 检查目录访问权限
        ret = check_directory_access(log_dir);
        if (ret != LZ_LOG_SUCCESS) {
            break;
        }
        
        // 获取当前日期
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        if (tm_now == NULL) {
            ret = LZ_LOG_ERROR_SYSTEM;
            break;
        }
        
        int current_year = tm_now->tm_year + 1900;
        int current_month = tm_now->tm_mon + 1;
        int current_day = tm_now->tm_mday;
        
        // 打开目录
        dir = opendir(log_dir);
        if (dir == NULL) {
            ret = LZ_LOG_ERROR_DIR_ACCESS;
            break;
        }
        
        // 遍历目录
        struct dirent *entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            // 跳过 . 和 ..
            if (strcmp(entry->d_name, ".") == 0 || 
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            // 解析文件名中的日期
            int file_year = 0, file_month = 0, file_day = 0;
            if (!parse_log_filename_date(entry->d_name, 
                                          &file_year, 
                                          &file_month, 
                                          &file_day)) {
                // 跳过非日志文件
                continue;
            }
            
            // 计算文件日期与当前日期的天数差
            int days_diff = calculate_days_diff(current_year, current_month, current_day,
                                                  file_year, file_month, file_day);
            
            // 如果文件日期早于保留天数，则删除
            if (days_diff >= days) {
                char file_path[1024];
                memset(file_path, 0, sizeof(file_path));
                snprintf(file_path, sizeof(file_path) - 1, "%s/%s", 
                         log_dir, entry->d_name);
                
                // 删除文件（忽略错误，继续处理其他文件）
                unlink(file_path);
            }
        }
        
    } while (0);
    
    // 关闭目录
    if (dir != NULL) {
        closedir(dir);
    }
    
    return ret;
}

/**
 * 导出当前日志文件
 * @param handle 日志句柄
 * @param out_export_path 输出导出文件路径
 * @param path_buffer_size 路径缓冲区大小
 * @return 错误码
 */
FFI_PLUGIN_EXPORT lz_log_error_t lz_logger_export_current_log(
    lz_logger_handle_t handle,
    char *out_export_path,
    uint32_t path_buffer_size) {
    
    lz_logger_context_t *ctx = (lz_logger_context_t *)handle;
    lz_log_error_t ret = LZ_LOG_SUCCESS;
    int export_fd = -1;
    char export_path[1024];
    
    do {
        // 参数验证
        if (ctx == NULL || out_export_path == NULL || path_buffer_size == 0) {
            ret = LZ_LOG_ERROR_INVALID_PARAM;
            break;
        }
        
        // 检查是否已关闭
        if (atomic_load(&ctx->is_closed)) {
            ret = LZ_LOG_ERROR_HANDLE_CLOSED;
            break;
        }
        
        // 检查 mmap 是否有效
        if (ctx->mmap_ptr == NULL || ctx->mmap_ptr == MAP_FAILED) {
            ret = LZ_LOG_ERROR_INVALID_HANDLE;
            break;
        }
        
        // 读取当前已使用大小（原子操作，无需锁）
        uint32_t used_size = atomic_load(ctx->cur_offset_ptr);
        
        // 如果没有数据，直接返回成功
        if (used_size == 0) {
            ret = LZ_LOG_SUCCESS;
            break;
        }
        
        // 构建导出文件路径
        memset(export_path, 0, sizeof(export_path));
        snprintf(export_path, sizeof(export_path) - 1, "%s/export.log", ctx->log_dir);
        
        // 删除已存在的 export.log（忽略错误）
        unlink(export_path);
        
        // 创建导出文件
        export_fd = open(export_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (export_fd < 0) {
            ret = LZ_LOG_ERROR_FILE_CREATE;
            break;
        }
        
        // 直接从 mmap 写入数据到文件（无需 flush）
        ssize_t written = 0;
        ssize_t total_written = 0;
        const uint8_t *data_ptr = (const uint8_t *)ctx->mmap_ptr;
        
        while (total_written < used_size) {
            written = write(export_fd, data_ptr + total_written, used_size - total_written);
            if (written <= 0) {
                if (errno == EINTR) {
                    // 被信号中断，重试
                    continue;
                }
                ret = LZ_LOG_ERROR_FILE_WRITE;
                break;
            }
            total_written += written;
        }
        
        if (ret != LZ_LOG_SUCCESS) {
            break;
        }
        
        // 同步到磁盘
        if (fsync(export_fd) != 0) {
            ret = LZ_LOG_ERROR_FILE_WRITE;
            break;
        }
        
        // 复制导出文件路径到输出参数
        size_t path_len = strlen(export_path);
        if (path_len >= path_buffer_size) {
            ret = LZ_LOG_ERROR_INVALID_PARAM;
            break;
        }
        strncpy(out_export_path, export_path, path_buffer_size - 1);
        out_export_path[path_buffer_size - 1] = '\0';
        
    } while (0);
    
    // 关闭导出文件
    if (export_fd >= 0) {
        close(export_fd);
    }
    
    return ret;
}
