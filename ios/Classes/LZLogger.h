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

@end

NS_ASSUME_NONNULL_END
