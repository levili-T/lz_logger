#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/** 日志级别 */
typedef NS_ENUM(NSInteger, LZLogLevel) {
    LZLogLevelVerbose = 0,
    LZLogLevelDebug = 1,
    LZLogLevelInfo = 2,
    LZLogLevelWarn = 3,
    LZLogLevelError = 4,
    LZLogLevelFatal = 5,
};

@interface LZLogger : NSObject

/**
 * 获取单例实例
 */
+ (instancetype)sharedInstance;

/**
 * 获取 FFI 函数指针地址（供 Dart 初始化时调用一次）
 * @return lz_logger_ffi 函数的地址
 */
+ (void *)ffiPointer;

/**
 * 禁用 init 方法
 */
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

/**
 * 准备日志系统
 * @param logName 日志文件夹名称（将在 Cache 目录下创建）
 * @param encryptKey 加密密钥（可为 nil 表示不加密）
 * @return 是否成功
 */
- (BOOL)prepareLog:(NSString *)logName encryptKey:(nullable NSString *)encryptKey;

/**
 * 设置日志级别（只记录大于等于此级别的日志）
 * @param level 日志级别
 */
- (void)setLogLevel:(LZLogLevel)level;

/**
 * 写入日志
 * @param level 日志级别
 * @param file 文件名
 * @param function 函数名
 * @param line 行号
 * @param tag 标签
 * @param format 格式化字符串
 */
- (void)log:(LZLogLevel)level
       file:(const char *)file
   function:(const char *)function
       line:(NSUInteger)line
        tag:(NSString *)tag
     format:(NSString *)format, ... NS_FORMAT_FUNCTION(6, 7);

/**
 * 写入日志（不带可变参数，供 Swift 使用）
 * @param level 日志级别
 * @param file 文件名
 * @param function 函数名
 * @param line 行号
 * @param tag 标签
 * @param message 日志消息
 */
- (void)logMessage:(LZLogLevel)level
              file:(const char *)file
          function:(const char *)function
              line:(NSUInteger)line
               tag:(NSString *)tag
           message:(NSString *)message;

/**
 * 同步日志到磁盘
 */
- (void)flush;

/**
 * 关闭日志系统
 */
- (void)close;

/**
 * 导出当前日志文件
 * @return 导出文件路径，失败返回 nil
 */
- (nullable NSString *)exportCurrentLog;

/**
 * 清理过期日志
 * @param days 保留天数
 * @return 是否成功
 */
- (BOOL)cleanupExpiredLogs:(NSInteger)days;

/**
 * 获取最后一次操作的内部错误码
 * @return 内部错误码
 */
- (int32_t)lastInnerError;

/**
 * 获取最后一次操作的系统 errno
 * @return 系统 errno
 */
- (int32_t)lastSysErrno;

/**
 * 获取日志目录路径
 * @return 日志目录路径，未初始化返回 nil
 */
@property (nonatomic, copy, readonly, nullable) NSString *logDir;

@end

// MARK: - 编译期文件名宏

/**
 * LZ_FILE_NAME - 编译期获取文件名（不含路径）
 * Clang 9+ 支持 __FILE_NAME__，在编译期展开为纯文件名
 * 对于不支持的编译器，使用 strrchr 提取文件名
 * （编译器会对字符串常量 __FILE__ 进行常量折叠优化，实际在编译期计算）
 */
#ifndef LZ_FILE_NAME
  #ifdef __FILE_NAME__
    // Clang provides just the last path component
    #define LZ_FILE_NAME __FILE_NAME__
  #elif defined(__has_builtin)
    #if __has_builtin(__builtin_strrchr)
      // Handle both UNIX ('/') and Windows ('\\') separators
      #define LZ_FILE_NAME ( \
        __builtin_strrchr(__FILE__, '/')  ? __builtin_strrchr(__FILE__, '/')  + 1 : \
        (__builtin_strrchr(__FILE__, '\\') ? __builtin_strrchr(__FILE__, '\\') + 1 : __FILE__) )
    #else
      #define LZ_FILE_NAME __FILE__
    #endif
  #else
    #define LZ_FILE_NAME __FILE__
  #endif
#endif

// MARK: - 便捷日志宏

#define LZ_LOG_VERBOSE(tag, format, ...) \
    [[LZLogger sharedInstance] log:LZLogLevelVerbose \
                              file:LZ_FILE_NAME \
                          function:__FUNCTION__ \
                              line:__LINE__ \
                               tag:tag \
                            format:format, ##__VA_ARGS__]

#define LZ_LOG_DEBUG(tag, format, ...) \
    [[LZLogger sharedInstance] log:LZLogLevelDebug \
                              file:LZ_FILE_NAME \
                          function:__FUNCTION__ \
                              line:__LINE__ \
                               tag:tag \
                            format:format, ##__VA_ARGS__]

#define LZ_LOG_INFO(tag, format, ...) \
    [[LZLogger sharedInstance] log:LZLogLevelInfo \
                              file:LZ_FILE_NAME \
                          function:__FUNCTION__ \
                              line:__LINE__ \
                               tag:tag \
                            format:format, ##__VA_ARGS__]

#define LZ_LOG_WARN(tag, format, ...) \
    [[LZLogger sharedInstance] log:LZLogLevelWarn \
                              file:LZ_FILE_NAME \
                          function:__FUNCTION__ \
                              line:__LINE__ \
                               tag:tag \
                            format:format, ##__VA_ARGS__]

#define LZ_LOG_ERROR(tag, format, ...) \
    [[LZLogger sharedInstance] log:LZLogLevelError \
                              file:LZ_FILE_NAME \
                          function:__FUNCTION__ \
                              line:__LINE__ \
                               tag:tag \
                            format:format, ##__VA_ARGS__]

#define LZ_LOG_FATAL(tag, format, ...) \
    [[LZLogger sharedInstance] log:LZLogLevelFatal \
                              file:LZ_FILE_NAME \
                          function:__FUNCTION__ \
                              line:__LINE__ \
                               tag:tag \
                            format:format, ##__VA_ARGS__]

NS_ASSUME_NONNULL_END
