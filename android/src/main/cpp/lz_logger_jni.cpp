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

// 获取当前线程 ID
static pid_t get_thread_id() {
    return gettid();
}

// 获取当前时间戳字符串
static std::string get_timestamp() {
    char buffer[32];
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);
    
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_info);
    
    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp), "%s.%03d", buffer, (int)(tv.tv_usec / 1000));
    
    return std::string(timestamp);
}

// 日志级别字符串
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
 * 设置 FFI 全局 handle
 */
JNIEXPORT void JNICALL
Java_io_levili_lzlogger_LzLogger_nativeSetFfiHandle(
        JNIEnv* /* env */,
        jobject /* this */,
        jlong jHandle) {
    
    lz_logger_handle_t handle = reinterpret_cast<lz_logger_handle_t>(jHandle);
    lz_logger_ffi_set_handle(handle);
    LOGI("FFI handle set: %p", handle);
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
    
    const char* tag = env->GetStringUTFChars(jTag, nullptr);
    const char* function = env->GetStringUTFChars(jFunction, nullptr);
    const char* file = env->GetStringUTFChars(jFile, nullptr);
    const char* message = env->GetStringUTFChars(jMessage, nullptr);
    
    // 获取线程 ID
    pid_t tid = get_thread_id();
    
    // 获取时间戳
    std::string timestamp = get_timestamp();
    
    // 获取日志级别字符串
    const char* levelStr = get_level_string(level);
    
    // 提取文件名（去掉路径）
    const char* fileName = file;
    if (file != nullptr && strlen(file) > 0) {
        const char* lastSlash = strrchr(file, '/');
        if (lastSlash != nullptr) {
            fileName = lastSlash + 1;
        }
    } else {
        fileName = "unknown";
    }
    
    // 构建位置信息：line > 0 时显示 "file:line"，否则只显示 "file"
    char location[256];
    if (line > 0) {
        snprintf(location, sizeof(location), "%s:%d", fileName, line);
    } else {
        snprintf(location, sizeof(location), "%s", fileName);
    }
    
    // 构建完整日志消息
    // 格式: yyyy-MM-dd HH:mm:ss.SSS tid:0x1234 [file:line] [func] [tag] message
    char fullMessage[4096];
    snprintf(fullMessage, sizeof(fullMessage),
             "%s tid:0x%x [%s] [%s] [%s] %s\n",
             timestamp.c_str(),
             tid,
             location,
             function != nullptr && strlen(function) > 0 ? function : "unknown",
             tag != nullptr ? tag : "",
             message != nullptr ? message : "");
    
    // 写入日志
    uint32_t len = static_cast<uint32_t>(strlen(fullMessage));
    lz_log_error_t ret = lz_logger_write(handle, fullMessage, len);
    
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("Write failed: %s", lz_logger_error_string(ret));
    }
    
#ifdef DEBUG
    // Debug 模式下同步输出到 logcat
    __android_log_print(ANDROID_LOG_INFO, levelStr, "%s", fullMessage);
#endif
    
    // 释放字符串
    env->ReleaseStringUTFChars(jTag, tag);
    env->ReleaseStringUTFChars(jFunction, function);
    env->ReleaseStringUTFChars(jFile, file);
    env->ReleaseStringUTFChars(jMessage, message);
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

// Global handle cache (must be set before using lz_logger_ffi)
static lz_logger_handle_t g_ffi_handle = nullptr;

extern "C" __attribute__((visibility("default"), used))
void lz_logger_ffi_set_handle(lz_logger_handle_t handle) {
    g_ffi_handle = handle;
}

extern "C" __attribute__((visibility("default"), used))
void lz_logger_ffi(int level, const char* tag, const char* function, const char* message) {
    if (g_ffi_handle == nullptr) {
        LOGE("lz_logger_ffi: handle not set, call lz_logger_ffi_set_handle first");
        return;
    }
    
    // 获取线程 ID
    pid_t tid = get_thread_id();
    
    // 获取时间戳
    std::string timestamp = get_timestamp();
    
    // 构建完整日志消息
    // 格式: yyyy-MM-dd HH:mm:ss.SSS tid:0x1234 [flutter] [func] [tag] message
    char fullMessage[4096];
    snprintf(fullMessage, sizeof(fullMessage),
             "%s tid:0x%x [flutter] [%s] [%s] %s\n",
             timestamp.c_str(),
             tid,
             function != nullptr && strlen(function) > 0 ? function : "unknown",
             tag != nullptr ? tag : "",
             message != nullptr ? message : "");
    
    // 写入日志
    uint32_t len = static_cast<uint32_t>(strlen(fullMessage));
    lz_log_error_t ret = lz_logger_write(g_ffi_handle, fullMessage, len);
    
    if (ret != LZ_LOG_SUCCESS) {
        LOGE("FFI write failed: %s", lz_logger_error_string(ret));
    }
    
#ifdef DEBUG
    // Debug 模式下同步输出到 logcat
    const char* levelStr = get_level_string(level);
    __android_log_print(ANDROID_LOG_INFO, levelStr, "%s", fullMessage);
#endif
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
