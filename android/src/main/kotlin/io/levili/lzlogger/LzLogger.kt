package io.levili.lzlogger

import android.content.Context
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import java.io.File

/**
 * LzLogger - 高性能跨平台日志系统
 * 
 * 日志级别定义:
 * - VERBOSE = 0
 * - DEBUG = 1
 * - INFO = 2
 * - WARN = 3
 * - ERROR = 4
 * - FATAL = 5
 */
object LzLogger : DefaultLifecycleObserver {
    // 日志级别常量
    const val VERBOSE = 0
    const val DEBUG = 1
    const val INFO = 2
    const val WARN = 3
    const val ERROR = 4
    const val FATAL = 5

    private var handle: Long = 0
    private var logDir: String? = null
    private var isInitialized = false
    private var currentLevel = INFO  // 默认 Info 级别
    private var lastInnerError = 0
    private var lastSysErrno = 0

    init {
        System.loadLibrary("lz_logger")
    }

    /**
     * 准备日志系统
     * @param context Android Context
     * @param logName 日志文件夹名称（将在外部存储 logs 目录下创建）
     *                路径: /sdcard/Android/data/包名/files/logs/logName/
     * @param encryptKey 加密密钥（可为 null 表示不加密）
     * @return 是否成功
     */
    @JvmStatic
    fun prepareLog(context: Context, logName: String, encryptKey: String? = null): Boolean {
        if (isInitialized) {
            android.util.Log.i("LzLogger", "Already initialized")
            return true
        }

        if (logName.isEmpty()) {
            android.util.Log.e("LzLogger", "Invalid log name")
            return false
        }

        val startTime = System.nanoTime()

        // 获取外部存储目录（方便调试导出）
        // 优先使用 /sdcard/Android/data/包名/files/logs/
        // 如果外部存储不可用，则降级到内部 cacheDir
        val baseDir = context.getExternalFilesDir(null) ?: context.filesDir
        
        if (!baseDir.exists()) {
            android.util.Log.e("LzLogger", "Failed to get storage directory")
            return false
        }

        // 创建日志目录：/sdcard/Android/data/包名/files/logs/logName
        val logsDir = File(baseDir, "logs")
        if (!logsDir.exists() && !logsDir.mkdirs()) {
            android.util.Log.e("LzLogger", "Failed to create logs directory: ${logsDir.absolutePath}")
            return false
        }
        
        val logDirFile = File(logsDir, logName)
        if (!logDirFile.exists()) {
            if (!logDirFile.mkdirs()) {
                android.util.Log.e("LzLogger", "Failed to create log directory: ${logDirFile.absolutePath}")
                return false
            }
        }

        val logDirPath = logDirFile.absolutePath

        // 调用 Native 初始化
        val errors = IntArray(2)  // [innerError, sysErrno]
        handle = nativeOpen(logDirPath, encryptKey, errors)

        lastInnerError = errors[0]
        lastSysErrno = errors[1]

        if (handle == 0L) {
            android.util.Log.e(
                "LzLogger",
                "Failed to open logger: inner=$lastInnerError, errno=$lastSysErrno"
            )
            return false
        }

        logDir = logDirPath
        isInitialized = true

        // 设置 FFI 全局 handle 和日志级别 (供 Dart FFI 使用)
        nativeSetFfiHandle(handle, currentLevel)

        // 注册进程生命周期监听，自动关闭日志
        try {
            ProcessLifecycleOwner.get().lifecycle.addObserver(this)
        } catch (e: Exception) {
            android.util.Log.w("LzLogger", "Failed to register lifecycle observer: ${e.message}")
        }

        val elapsedMs = (System.nanoTime() - startTime) / 1_000_000.0
        log(INFO, "LzLogger", "Initialized successfully in %.2fms, path: %s".format(elapsedMs, logDirPath))

        // 3秒后在后台线程清理7天前的日志
        Thread {
            Thread.sleep(3000)
            log(INFO, "LzLogger", "Starting cleanup of expired logs (7 days)")
            val success = cleanupExpiredLogs(7)
            if (success) {
                log(INFO, "LzLogger", "Cleanup completed successfully")
            } else {
                log(WARN, "LzLogger", "Cleanup failed")
            }
        }.start()

        return true
    }

    /**
     * 设置日志级别
     * 注意：必须在 prepareLog 之前调用，初始化后调用会抛出异常
     * @param level 日志级别
     * @throws IllegalStateException 如果在 prepareLog 之后调用
     */
    @JvmStatic
    fun setLogLevel(level: Int) {
        if (isInitialized) {
            throw IllegalStateException("setLogLevel must be called before prepareLog")
        }
        currentLevel = level
    }

    /**
     * 写入日志
     * @param level 日志级别
     * @param tag 标签
     * @param message 日志内容
     * @param function 函数名（可选）
     * @param file 文件名（可选）
     * @param line 行号（可选）
     */
    @JvmStatic
    @JvmOverloads
    fun log(
        level: Int,
        tag: String,
        message: String,
        function: String = "",
        file: String = "",
        line: Int = 0
    ) {
        // 无需加锁 - 底层 C 代码使用 CAS 原子操作保证线程安全
        if (!isInitialized || handle == 0L) {
            return
        }

        // 级别过滤
        if (level < currentLevel) {
            return
        }

        nativeLog(handle, level, tag, function, file, line, message)
    }

    /**
     * 同步日志到磁盘
     */
    @JvmStatic
    fun flush() {
        // 无需加锁 - msync 本身是线程安全的系统调用
        if (!isInitialized || handle == 0L) {
            return
        }
        nativeFlush(handle)
    }

    /**
     * 关闭日志系统
     */
    @JvmStatic
    fun close() {
        if (!isInitialized || handle == 0L) {
            return
        }

        nativeClose(handle)
        handle = 0
        isInitialized = false
        android.util.Log.i("LzLogger", "Closed")
    }

    /**
     * 导出当前日志文件
     * @return 导出文件路径，失败返回 null
     */
    @JvmStatic
    fun exportCurrentLog(): String? {
        // 无需加锁 - 文件拷贝是独立操作,不影响并发写入
        if (!isInitialized || handle == 0L) {
            return null
        }

        val exportPath = nativeExportCurrentLog(handle)
        if (exportPath != null) {
            log(INFO, "LzLogger", "Export completed: $exportPath")
        } else {
            log(ERROR, "LzLogger", "Export failed")
        }
        return exportPath
    }

    /**
     * 清理过期日志
     * @param days 保留天数
     * @return 是否成功
     */
    @JvmStatic
    fun cleanupExpiredLogs(days: Int): Boolean {
        // 无需加锁 - 删除过期文件不影响当前文件的并发写入
        if (logDir.isNullOrEmpty() || days < 0) {
            return false
        }

        val success = nativeCleanupExpiredLogs(logDir!!, days)
        if (!success && isInitialized) {
            log(ERROR, "LzLogger", "Cleanup failed")
        }
        return success
    }

    /**
     * 获取最后一次操作的内部错误码
     */
    @JvmStatic
    fun getLastInnerError(): Int = lastInnerError

    /**
     * 获取最后一次操作的系统 errno
     */
    @JvmStatic
    fun getLastSysErrno(): Int = lastSysErrno

    // ========================================================================
    // 生命周期监听 - 进程退出时自动关闭日志
    // ========================================================================

    override fun onStop(owner: LifecycleOwner) {
        super.onStop(owner)
        if (isInitialized) {
            log(INFO, "LzLogger", "Process stopping, closing logger")
            close()
        }
    }

    // Native 方法声明
    private external fun nativeOpen(logDir: String, encryptKey: String?, outErrors: IntArray): Long
    private external fun nativeSetFfiHandle(handle: Long, logLevel: Int)
    private external fun nativeLog(handle: Long, level: Int, tag: String, function: String, file: String, line: Int, message: String)
    private external fun nativeFlush(handle: Long)
    private external fun nativeClose(handle: Long)
    private external fun nativeExportCurrentLog(handle: Long): String?
    private external fun nativeCleanupExpiredLogs(logDir: String, days: Int): Boolean
}

// 便捷日志函数
fun logVerbose(tag: String, message: String) = LzLogger.log(LzLogger.VERBOSE, tag, message)
fun logDebug(tag: String, message: String) = LzLogger.log(LzLogger.DEBUG, tag, message)
fun logInfo(tag: String, message: String) = LzLogger.log(LzLogger.INFO, tag, message)
fun logWarn(tag: String, message: String) = LzLogger.log(LzLogger.WARN, tag, message)
fun logError(tag: String, message: String) = LzLogger.log(LzLogger.ERROR, tag, message)
fun logFatal(tag: String, message: String) = LzLogger.log(LzLogger.FATAL, tag, message)
