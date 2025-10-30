#import "LZLogger.h"
#import "lz_logger.h"

@interface LZLogger ()

@property (nonatomic, assign) lz_logger_handle_t handle;
@property (nonatomic, copy) NSString *logDir;
@property (nonatomic, assign) BOOL isInitialized;
@property (nonatomic, strong) dispatch_queue_t logQueue;

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
        // 创建串行队列用于日志操作（可选，如果需要确保顺序）
        _logQueue = dispatch_queue_create("com.lz_logger.queue", DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

#pragma mark - Public Methods

- (BOOL)prepareLog:(NSString *)logDir encryptKey:(nullable NSString *)encryptKey {
    if (self.isInitialized) {
        NSLog(@"[LZLogger] Already initialized");
        return YES;
    }
    
    if (logDir.length == 0) {
        NSLog(@"[LZLogger] Invalid log directory");
        return NO;
    }
    
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
    
    // 构建完整的日志消息
    // 格式: [时间] [级别] [tag] [文件:行号] [函数] 消息
    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss.SSS";
    NSString *timestamp = [formatter stringFromDate:[NSDate date]];
    
    const char *levelStr = [self levelString:level];
    const char *fileName = file ? strrchr(file, '/') ? strrchr(file, '/') + 1 : file : "unknown";
    
    NSString *fullMessage = [NSString stringWithFormat:@"[%@] [%s] [%@] [%s:%lu] [%s] %@\n",
                             timestamp, levelStr, tag ?: @"", fileName, (unsigned long)line, 
                             function ?: "", message];
    
    // 写入日志
    const char *messageCStr = [fullMessage UTF8String];
    uint32_t length = (uint32_t)strlen(messageCStr);
    
    lz_log_error_t ret = lz_logger_write(self.handle, messageCStr, length);
    if (ret != LZ_LOG_SUCCESS) {
        NSLog(@"[LZLogger] Write failed: %s", lz_logger_error_string(ret));
    }
}

- (void)flush {
    if (!self.isInitialized || self.handle == NULL) {
        return;
    }
    
    lz_log_error_t ret = lz_logger_flush(self.handle);
    if (ret != LZ_LOG_SUCCESS) {
        NSLog(@"[LZLogger] Flush failed: %s", lz_logger_error_string(ret));
    }
}

- (void)close {
    if (!self.isInitialized || self.handle == NULL) {
        return;
    }
    
    lz_log_error_t ret = lz_logger_close(self.handle);
    if (ret != LZ_LOG_SUCCESS) {
        NSLog(@"[LZLogger] Close failed: %s", lz_logger_error_string(ret));
    }
    
    self.handle = NULL;
    self.isInitialized = NO;
    
    NSLog(@"[LZLogger] Closed");
}

- (nullable NSString *)exportCurrentLog {
    if (!self.isInitialized || self.handle == NULL) {
        return nil;
    }
    
    char exportPath[1024] = {0};
    lz_log_error_t ret = lz_logger_export_current_log(self.handle, exportPath, sizeof(exportPath));
    
    if (ret != LZ_LOG_SUCCESS) {
        NSLog(@"[LZLogger] Export failed: %s", lz_logger_error_string(ret));
        return nil;
    }
    
    return [NSString stringWithUTF8String:exportPath];
}

- (BOOL)cleanupExpiredLogs:(NSInteger)days {
    if (self.logDir.length == 0 || days < 0) {
        return NO;
    }
    
    const char *logDirCStr = [self.logDir UTF8String];
    lz_log_error_t ret = lz_logger_cleanup_expired_logs(logDirCStr, (int)days);
    
    if (ret != LZ_LOG_SUCCESS) {
        NSLog(@"[LZLogger] Cleanup failed: %s", lz_logger_error_string(ret));
        return NO;
    }
    
    return YES;
}

#pragma mark - Private Methods

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
    if (self.handle != NULL) {
        lz_logger_close(self.handle);
        self.handle = NULL;
    }
}

@end