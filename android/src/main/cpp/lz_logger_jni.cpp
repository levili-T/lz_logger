#include <jni.h>
#include <string>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <unistd.h>
#include <android/log.h>
#include "lz_logger.h"

#define LOG_TAG "LzLogger"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 日志消息缓冲区大小
#define LOG_MESSAGE_BUFFER_SIZE 4096

// 获取当前线程 ID
static pid_t get_thread_id() {
    return gettid();
}

// 获取当前时间戳字符串（优化：避免 std::string 动态分配）
static void get_timestamp(char* out_buffer, size_t buffer_size) {
    char time_buf[32];
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);
    
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    
    snprintf(out_buffer, buffer_size, "%s.%03d", time_buf, (int)(tv.tv_usec / 1000));
}

// 日志级别字符串（用于 logcat 输出）
static const char* get_level_string(int level) {
    switch (level) {
        case 0: return "VERBOSE";
        case 1: return "DEBUG";
        case 2: return "INFO";
        case 3: return "WARN";
        case 4: return "ERROR";
        case 5: return "FATAL";
        default: return "UNKNOWN";
    }
}

// 截断日志消息（在末尾添加 ...\n）
static inline void truncate_log_message(char* buffer, size_t buffer_size) {
    size_t len = buffer_size - 1;
    buffer[len - 4] = '.';
    buffer[len - 3] = '.';
    buffer[len - 2] = '.';
    buffer[len - 1] = '\n';
}

// FFI 函数前置声明
extern "C" void lz_logger_ffi_set_handle(lz_logger_handle_t handle);
extern "C" void lz_logger_ffi(int level, const char* tag, const char* function, const char* message);

extern "C" {

/**
 * 打开日志系统
 * @param env JNI 环境
 * @param clazz 类对象
 * @param jLogDir 日志目录
 * @param jEncryptKey 加密密钥
 * @param jOutErrors 输出错误码数组 [innerError, sysErrno]
 * @return 日志句柄（0 表示失败）
 */
JNIEXPORT jlong JNICALL
Java_io_levili_lzlogger_LzLogger_nativeOpen(
        JNIEnv* env,
        jobject /* this */,
        jstring jLogDir,
        jstring jEncryptKey,
        jintArray jOutErrors) {
    
    const char* logDir = env->GetStringUTFChars(jLogDir, nullptr);
    const char* encryptKey = jEncryptKey != nullptr ? env->GetStringUTFChars(jEncryptKey, nullptr) : nullptr;
    
    lz_logger_handle_t handle = nullptr;
    int32_t innerError = 0;
    int32_t sysErrno = 0;
    
    lz_log_error_t ret = lz_logger_open(logDir, encryptKey, &handle, &innerError, &sysErrno);
    
    // 设置错误码
    if (jOutErrors != nullptr) {
        jint errors[2] = { innerError, sysErrno };
        env->SetIntArrayRegion(jOutErrors, 0, 2, errors);
    }
    
    // 释放字符串
    env->ReleaseStringUTFChars(jLogDir, logDir);
    if (encryptKey != nullptr) {
        env->ReleaseStringUTFChars(jEncryptKey, encryptKey);
    }
    
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("Failed to open logger: ret=%d, inner=%d, errno=%d, desc=%s",
             ret, innerError, sysErrno, lz_logger_error_string(ret));
        return 0;
    }
    
    return reinterpret_cast<jlong>(handle);
}

/**
 * 设置 FFI 全局 handle 和日志级别
 */
JNIEXPORT void JNICALL
Java_io_levili_lzlogger_LzLogger_nativeSetFfiHandle(
        JNIEnv* /* env */,
        jobject /* this */,
        jlong jHandle,
        jint jLogLevel) {
    
    lz_logger_handle_t handle = reinterpret_cast<lz_logger_handle_t>(jHandle);
    lz_logger_ffi_set_handle(handle);
    
    // 设置 FFI 日志级别
    extern int g_ffi_log_level;
    g_ffi_log_level = jLogLevel;
    
    LOGI("FFI handle set: %p, log level: %d", handle, jLogLevel);
}

/**
 * 写入日志
 */
JNIEXPORT void JNICALL
Java_io_levili_lzlogger_LzLogger_nativeLog(
        JNIEnv* env,
        jobject /* this */,
        jlong jHandle,
        jint level,
        jstring jTag,
        jstring jFunction,
        jstring jFile,
        jint line,
        jstring jMessage) {
    
    lz_logger_handle_t handle = reinterpret_cast<lz_logger_handle_t>(jHandle);
    if (handle == nullptr) {
        return;
    }
    
    // 安全获取 JNI 字符串（允许为空）
    const char* tag = jTag ? env->GetStringUTFChars(jTag, nullptr) : nullptr;
    const char* function = jFunction ? env->GetStringUTFChars(jFunction, nullptr) : nullptr;
    const char* file = jFile ? env->GetStringUTFChars(jFile, nullptr) : nullptr;
    const char* message = jMessage ? env->GetStringUTFChars(jMessage, nullptr) : nullptr;
    
    // 获取线程 ID
    pid_t tid = get_thread_id();
    
    // 获取时间戳
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    // 文件名由外部保证只传入文件名，无需提取路径
    const char* fileName = (file && *file) ? file : "unknown";
    
    // 构建位置信息：line > 0 时显示 "file:line"，否则只显示 "file"
    char location[256];
    if (line > 0) {
        snprintf(location, sizeof(location), "%s:%d", fileName, line);
    } else {
        snprintf(location, sizeof(location), "%s", fileName);
    }
    
    // 构建完整日志消息
    // 格式: yyyy-MM-dd HH:mm:ss.SSS T:1234 [file:line] [func] [tag] message
    //       如果 function 为空，则省略 [func] 字段
    char fullMessage[LOG_MESSAGE_BUFFER_SIZE];
    char* dynamicBuffer = nullptr;
    char* logBuffer = fullMessage;
    int len; // snprintf 返回写入的字符数（不包括 null）
    
    if (function && *function) {  // 优化：直接检查指针和首字符，无需 strlen
        len = snprintf(fullMessage, sizeof(fullMessage),
                       "%s T:%x [%s] [%s] [%s] %s\n",
                       timestamp,
                       tid,
                       location,
                       function,
                       tag ? tag : "",
                       message ? message : "");
    } else {
        len = snprintf(fullMessage, sizeof(fullMessage),
                       "%s T:%x [%s] [%s] %s\n",
                       timestamp,
                       tid,
                       location,
                       tag ? tag : "",
                       message ? message : "");
    }
    
    // 处理 snprintf 结果：如果出错则跳过
    if (len < 0) {
        LOGE("Log formatting error");
        // 释放字符串后返回
        if (tag) env->ReleaseStringUTFChars(jTag, tag);
        if (function) env->ReleaseStringUTFChars(jFunction, function);
        if (file) env->ReleaseStringUTFChars(jFile, file);
        if (message) env->ReleaseStringUTFChars(jMessage, message);
        return;
    }
    
    // 如果超长，根据日志级别决定策略
    if (len >= (int)sizeof(fullMessage)) {
        // DEBUG级别(level=1)：动态分配内存，完整输出
        if (level == 1) {
            dynamicBuffer = (char*)malloc(len + 1);  // +1 for null terminator
            if (dynamicBuffer != nullptr) {
                // 重新格式化到动态缓冲区
                if (function && *function) {
                    snprintf(dynamicBuffer, len + 1,
                             "%s T:%x [%s] [%s] [%s] %s\n",
                             timestamp,
                             tid,
                             location,
                             function,
                             tag ? tag : "",
                             message ? message : "");
                } else {
                    snprintf(dynamicBuffer, len + 1,
                             "%s T:%x [%s] [%s] %s\n",
                             timestamp,
                             tid,
                             location,
                             tag ? tag : "",
                             message ? message : "");
                }
                logBuffer = dynamicBuffer;
            } else {
                LOGE("Failed to allocate memory for long log message");
            }
        }
        
        // 内存分配失败或非DEBUG级别：截断处理
        if (dynamicBuffer == nullptr) {
            len = sizeof(fullMessage) - 1;
            truncate_log_message(fullMessage, sizeof(fullMessage));
        }
    }
    
    // 写入日志（直接使用计算出的长度，无需再次 strlen）
    lz_log_error_t ret = lz_logger_write(handle, logBuffer, (uint32_t)len);
    
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("Write failed: %s", lz_logger_error_string(ret));
    }
    
#ifdef DEBUG
    // Debug 模式下同步输出到 logcat
    const char* levelStr = get_level_string(level);
    __android_log_print(ANDROID_LOG_INFO, levelStr, "%s", logBuffer);
#endif
    
    // 释放动态分配的内存
    if (dynamicBuffer != nullptr) {
        free(dynamicBuffer);
    }
    
    // 释放字符串
    if (tag) env->ReleaseStringUTFChars(jTag, tag);
    if (function) env->ReleaseStringUTFChars(jFunction, function);
    if (file) env->ReleaseStringUTFChars(jFile, file);
    if (message) env->ReleaseStringUTFChars(jMessage, message);
}

/**
 * 同步日志到磁盘
 */
JNIEXPORT void JNICALL
Java_io_levili_lzlogger_LzLogger_nativeFlush(
        JNIEnv* /* env */,
        jobject /* this */,
        jlong jHandle) {
    
    lz_logger_handle_t handle = reinterpret_cast<lz_logger_handle_t>(jHandle);
    if (handle == nullptr) {
        return;
    }
    
    lz_log_error_t ret = lz_logger_flush(handle);
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("Flush failed: %s", lz_logger_error_string(ret));
    }
}

/**
 * 关闭日志系统
 */
JNIEXPORT void JNICALL
Java_io_levili_lzlogger_LzLogger_nativeClose(
        JNIEnv* /* env */,
        jobject /* this */,
        jlong jHandle) {
    
    lz_logger_handle_t handle = reinterpret_cast<lz_logger_handle_t>(jHandle);
    if (handle == nullptr) {
        return;
    }
    
    lz_log_error_t ret = lz_logger_close(handle);
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("Close failed: %s", lz_logger_error_string(ret));
    } else {
        LOGI("Closed");
    }
}

/**
 * 导出当前日志文件
 */
JNIEXPORT jstring JNICALL
Java_io_levili_lzlogger_LzLogger_nativeExportCurrentLog(
        JNIEnv* env,
        jobject /* this */,
        jlong jHandle) {
    
    lz_logger_handle_t handle = reinterpret_cast<lz_logger_handle_t>(jHandle);
    if (handle == nullptr) {
        return nullptr;
    }
    
    char exportPath[1024] = {0};
    lz_log_error_t ret = lz_logger_export_current_log(handle, exportPath, sizeof(exportPath));
    
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("Export failed: %s", lz_logger_error_string(ret));
        return nullptr;
    }
    
    return env->NewStringUTF(exportPath);
}

/**
 * 清理过期日志
 */
JNIEXPORT jboolean JNICALL
Java_io_levili_lzlogger_LzLogger_nativeCleanupExpiredLogs(
        JNIEnv* env,
        jobject /* this */,
        jstring jLogDir,
        jint days) {
    
    const char* logDir = env->GetStringUTFChars(jLogDir, nullptr);
    
    lz_log_error_t ret = lz_logger_cleanup_expired_logs(logDir, days);
    
    env->ReleaseStringUTFChars(jLogDir, logDir);
    
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("Cleanup failed: %s", lz_logger_error_string(ret));
        return JNI_FALSE;
    }
    
    return JNI_TRUE;
}

} // extern "C"

// ============================================================================
// FFI function for Dart integration (matching iOS implementation)
// ============================================================================

// Global handle and log level cache (must be set before using lz_logger_ffi)
static lz_logger_handle_t g_ffi_handle = nullptr;
int g_ffi_log_level = 2; // 默认 INFO 级别 (非 static，供 JNI 访问)

extern "C" __attribute__((visibility("default"), used))
void lz_logger_ffi_set_handle(lz_logger_handle_t handle) {
    g_ffi_handle = handle;
}

extern "C" __attribute__((visibility("default"), used))
void lz_logger_ffi(int level, const char* tag, const char* function, const char* message) {
    // 级别过滤
    if (level < g_ffi_log_level) {
        return;
    }
    
    if (g_ffi_handle == nullptr) {
        LOGE("lz_logger_ffi: handle not set, call lz_logger_ffi_set_handle first");
        return;
    }
    
    // 获取线程 ID
    pid_t tid = get_thread_id();
    
    // 获取时间戳
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    // 构建完整日志消息
    // 格式: yyyy-MM-dd HH:mm:ss.SSS T:1234 [flutter] [func] [tag] message
    //       如果 function 为空，则省略 [func] 字段
    char fullMessage[LOG_MESSAGE_BUFFER_SIZE];
    char* dynamicBuffer = nullptr;
    char* logBuffer = fullMessage;
    int len; // snprintf 返回写入的字符数（不包括 null）
    
    if (function && *function) {  // 优化：直接检查指针和首字符，无需 strlen
        len = snprintf(fullMessage, sizeof(fullMessage),
                       "%s T:%x [flutter] [%s] [%s] %s\n",
                       timestamp,
                       tid,
                       function,
                       tag ? tag : "",
                       message ? message : "");
    } else {
        len = snprintf(fullMessage, sizeof(fullMessage),
                       "%s T:%x [flutter] [%s] %s\n",
                       timestamp,
                       tid,
                       tag ? tag : "",
                       message ? message : "");
    }
    
    // 处理 snprintf 结果：如果出错则跳过
    if (len < 0) {
        LOGE("FFI log formatting error");
        return;
    }
    
    // 如果超长，根据日志级别决定策略
    if (len >= (int)sizeof(fullMessage)) {
        // DEBUG级别(level=1)：动态分配内存，完整输出
        if (level == 1) {
            dynamicBuffer = (char*)malloc(len + 1);  // +1 for null terminator
            if (dynamicBuffer != nullptr) {
                // 重新格式化到动态缓冲区
                if (function && *function) {
                    snprintf(dynamicBuffer, len + 1,
                             "%s T:%x [flutter] [%s] [%s] %s\n",
                             timestamp,
                             tid,
                             function,
                             tag ? tag : "",
                             message ? message : "");
                } else {
                    snprintf(dynamicBuffer, len + 1,
                             "%s T:%x [flutter] [%s] %s\n",
                             timestamp,
                             tid,
                             tag ? tag : "",
                             message ? message : "");
                }
                logBuffer = dynamicBuffer;
            } else {
                LOGE("Failed to allocate memory for long FFI log message");
            }
        }
        
        // 内存分配失败或非DEBUG级别：截断处理
        if (dynamicBuffer == nullptr) {
            len = sizeof(fullMessage) - 1;
            truncate_log_message(fullMessage, sizeof(fullMessage));
        }
    }
    
    // 写入日志（直接使用计算出的长度，无需再次 strlen）
    lz_log_error_t ret = lz_logger_write(g_ffi_handle, logBuffer, (uint32_t)len);
    
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("FFI write failed: %s", lz_logger_error_string(ret));
    }
    
    // 释放动态分配的内存
    if (dynamicBuffer != nullptr) {
        free(dynamicBuffer);
    }
    
    // 注意：不再输出到 logcat，Dart 层会在 debug 模式用 print() 输出到控制台
}

// ============================================================================
// JNI_OnLoad - 初始化加密模块
// ============================================================================

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("JNI_OnLoad: GetEnv failed");
        return JNI_ERR;
    }
    
    // 初始化加密模块 (Android 使用 Java Crypto API)
    extern int lz_crypto_jni_init(JNIEnv *env, JavaVM *jvm);
    if (lz_crypto_jni_init(env, vm) != 0) {
        LOGE("JNI_OnLoad: lz_crypto_jni_init failed");
        return JNI_ERR;
    }
    
    LOGI("JNI_OnLoad: lz_logger loaded successfully");
    return JNI_VERSION_1_6;
}
