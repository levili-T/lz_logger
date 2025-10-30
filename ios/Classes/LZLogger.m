#import "LZLogger.h"
#import "lz_logger.h"
#import <pthread.h>
#import <UIKit/UIKit.h>

@interface LZLogger ()

@property (nonatomic, assign) lz_logger_handle_t handle;
@property (nonatomic, copy) NSString *logDir;
@property (nonatomic, assign) BOOL isInitialized;

@end

@implementation LZLogger

#pragma mark - Singleton

+ (instancetype)sharedInstance {
    static LZLogger *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[LZLogger alloc] initPrivate];
    });
    return instance;
}

- (instancetype)initPrivate {
    self = [super init];
    if (self) {
        _handle = NULL;
        _isInitialized = NO;
        
        // 监听应用即将终止通知
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(applicationWillTerminate:)
                                                     name:UIApplicationWillTerminateNotification
                                                   object:nil];
    }
    return self;
}

#pragma mark - Public Methods

- (BOOL)prepareLog:(NSString *)logName encryptKey:(nullable NSString *)encryptKey {
    if (self.isInitialized) {
        NSLog(@"[LZLogger] Already initialized");
        return YES;
    }
    
    if (logName.length == 0) {
        NSLog(@"[LZLogger] Invalid log name");
        return NO;
    }
    
    // 获取 Cache 目录
    NSArray *cachePaths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    if (cachePaths.count == 0) {
        NSLog(@"[LZLogger] Failed to get cache directory");
        return NO;
    }
    NSString *cacheDir = cachePaths.firstObject;
    
    // 创建日志目录路径：Cache/logName
    NSString *logDir = [cacheDir stringByAppendingPathComponent:logName];
    
    // 创建日志目录（如果不存在）
    NSFileManager *fileManager = [NSFileManager defaultManager];
    if (![fileManager fileExistsAtPath:logDir]) {
        NSError *error = nil;
        BOOL success = [fileManager createDirectoryAtPath:logDir
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:&error];
        if (!success) {
            NSLog(@"[LZLogger] Failed to create log directory: %@", error);
            return NO;
        }
    }
    
    // 设置目录保护属性为无保护（允许后台访问）
    NSError *protectionError = nil;
    NSDictionary *attributes = @{NSFileProtectionKey: NSFileProtectionNone};
    BOOL setProtection = [fileManager setAttributes:attributes
                                        ofItemAtPath:logDir
                                               error:&protectionError];
    if (!setProtection) {
        NSLog(@"[LZLogger] Warning: Failed to set file protection: %@", protectionError);
        // 不返回 NO，这不是致命错误
    }
    
    // 排除目录备份到 iCloud
    NSURL *logDirURL = [NSURL fileURLWithPath:logDir];
    NSError *excludeError = nil;
    BOOL excludeBackup = [logDirURL setResourceValue:@YES
                                               forKey:NSURLIsExcludedFromBackupKey
                                                error:&excludeError];
    if (!excludeBackup) {
        NSLog(@"[LZLogger] Warning: Failed to exclude from backup: %@", excludeError);
        // 不返回 NO，这不是致命错误
    }
    
    // 调用 C 函数初始化
    const char *logDirCStr = [logDir UTF8String];
    const char *encryptKeyCStr = encryptKey ? [encryptKey UTF8String] : NULL;
    
    lz_logger_handle_t handle = NULL;
    int32_t sysError = 0;
    lz_log_error_t ret = lz_logger_open(logDirCStr, encryptKeyCStr, &handle, &sysError);
    
    if (ret != LZ_LOG_SUCCESS) {
        NSLog(@"[LZLogger] Failed to open logger: %d (errno=%d, %s)", 
              ret, sysError, lz_logger_error_string(ret));
        return NO;
    }
    
    self.handle = handle;
    self.logDir = logDir;
    self.isInitialized = YES;
    
    NSLog(@"[LZLogger] Initialized successfully: %@", logDir);
    
    // 3秒后在低优先级线程清理7天前的日志
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)),
                   dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0), ^{
        [self log:LZLogLevelInfo file:__FILE__ function:__FUNCTION__ line:0 
              tag:@"LZLogger" format:@"Starting cleanup of expired logs (7 days)"];
        BOOL success = [self cleanupExpiredLogs:7];
        if (success) {
            [self log:LZLogLevelInfo file:__FILE__ function:__FUNCTION__ line:0 
                  tag:@"LZLogger" format:@"Cleanup completed successfully"];
        } else {
            [self log:LZLogLevelWarn file:__FILE__ function:__FUNCTION__ line:0 
                  tag:@"LZLogger" format:@"Cleanup failed"];
        }
    });
    
    return YES;
}

- (void)log:(LZLogLevel)level
       file:(const char *)file
   function:(const char *)function
       line:(NSUInteger)line
        tag:(NSString *)tag
     format:(NSString *)format, ... {
    
    if (!self.isInitialized || self.handle == NULL) {
        return;
    }
    
    // 处理可变参数
    va_list args;
    va_start(args, format);
    NSString *message = [[NSString alloc] initWithFormat:format arguments:args];
    va_end(args);
    
    // 获取当前线程 ID
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    
    // 构建完整的日志消息
    // 格式: yyyy-MM-dd HH:mm:ss.SSS tid:xx [file:line] [func] [tag] xxx
    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss.SSS";
    NSString *timestamp = [formatter stringFromDate:[NSDate date]];
    
    const char *levelStr = [self levelString:level];
    const char *fileName = file ? strrchr(file, '/') ? strrchr(file, '/') + 1 : file : "unknown";
    
    // 构建文件位置信息：line 为 0 时只显示文件名
    NSString *location = line > 0 
        ? [NSString stringWithFormat:@"%s:%lu", fileName, (unsigned long)line]
        : [NSString stringWithUTF8String:fileName];
    
    NSString *fullMessage = [NSString stringWithFormat:@"%@ tid:%llu [%@] [%s] [%@] %@\n",
                             timestamp, tid, location, 
                             function ?: "unknown", tag ?: @"", message];
    
    // 写入日志
    const char *messageCStr = [fullMessage UTF8String];
    uint32_t length = (uint32_t)strlen(messageCStr);
    
    lz_log_error_t ret = lz_logger_write(self.handle, messageCStr, length);
    if (ret != LZ_LOG_SUCCESS) {
        // Write 失败用 NSLog，避免递归调用
        NSLog(@"[LZLogger] Write failed: %s", lz_logger_error_string(ret));
    }
    
#ifdef DEBUG
    // Debug 模式下同步输出到控制台
    NSLog(@"[%s] %@", levelStr, fullMessage);
#endif
}

- (void)flush {
    if (!self.isInitialized || self.handle == NULL) {
        return;
    }
    
    lz_log_error_t ret = lz_logger_flush(self.handle);
    if (ret != LZ_LOG_SUCCESS) {
        // Flush 失败用 NSLog，因为可能无法写入日志文件
        NSLog(@"[LZLogger] Flush failed: %s", lz_logger_error_string(ret));
    }
}

- (void)close {
    if (!self.isInitialized || self.handle == NULL) {
        return;
    }
    
    lz_log_error_t ret = lz_logger_close(self.handle);
    
    self.handle = NULL;
    self.isInitialized = NO;
    
    // Close 后用 NSLog，因为日志系统已关闭
    if (ret != LZ_LOG_SUCCESS) {
        NSLog(@"[LZLogger] Close failed: %s", lz_logger_error_string(ret));
    } else {
        NSLog(@"[LZLogger] Closed");
    }
}

- (nullable NSString *)exportCurrentLog {
    if (!self.isInitialized || self.handle == NULL) {
        return nil;
    }
    
    char exportPath[1024] = {0};
    lz_log_error_t ret = lz_logger_export_current_log(self.handle, exportPath, sizeof(exportPath));
    
    if (ret != LZ_LOG_SUCCESS) {
        [self log:LZLogLevelError file:__FILE__ function:__FUNCTION__ line:__LINE__ 
              tag:@"LZLogger" format:@"Export failed: %s", lz_logger_error_string(ret)];
        return nil;
    }
    
    [self log:LZLogLevelInfo file:__FILE__ function:__FUNCTION__ line:__LINE__ 
          tag:@"LZLogger" format:@"Export completed: %s", exportPath];
    return [NSString stringWithUTF8String:exportPath];
}

- (BOOL)cleanupExpiredLogs:(NSInteger)days {
    if (self.logDir.length == 0 || days < 0) {
        return NO;
    }
    
    const char *logDirCStr = [self.logDir UTF8String];
    lz_log_error_t ret = lz_logger_cleanup_expired_logs(logDirCStr, (int)days);
    
    if (ret != LZ_LOG_SUCCESS) {
        if (self.isInitialized) {
            [self log:LZLogLevelError file:__FILE__ function:__FUNCTION__ line:__LINE__ 
                  tag:@"LZLogger" format:@"Cleanup failed: %s", lz_logger_error_string(ret)];
        }
        return NO;
    }
    
    return YES;
}

#pragma mark - Private Methods

- (void)applicationWillTerminate:(NSNotification *)notification {
    if (self.isInitialized) {
        [self log:LZLogLevelInfo file:__FILE__ function:__FUNCTION__ line:0 
              tag:@"LZLogger" format:@"Application will terminate, closing logger"];
    }
    [self close];
}

- (const char *)levelString:(LZLogLevel)level {
    switch (level) {
        case LZLogLevelVerbose: return "VERBOSE";
        case LZLogLevelDebug:   return "DEBUG";
        case LZLogLevelInfo:    return "INFO";
        case LZLogLevelWarn:    return "WARN";
        case LZLogLevelError:   return "ERROR";
        case LZLogLevelFatal:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    
    if (self.handle != NULL) {
        lz_logger_close(self.handle);
        self.handle = NULL;
    }
}

@end