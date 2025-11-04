//
//  LzLoggerPerformanceTests.m
//  RunnerTests
//
//  Performance tests for lz_logger
//

#import <XCTest/XCTest.h>
#import <lz_logger/LZLogger.h>

@interface LzLoggerPerformanceTests : XCTestCase
@property (nonatomic, strong) NSString *testLogDir;
@end

@implementation LzLoggerPerformanceTests

- (void)setUp {
    [super setUp];
    // 创建测试目录
    self.testLogDir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"lz_logger_perf_tests"];
    [[NSFileManager defaultManager] createDirectoryAtPath:self.testLogDir 
                              withIntermediateDirectories:YES 
                                               attributes:nil 
                                                    error:nil];
    
    // 准备日志系统
    [[LZLogger sharedInstance] prepareLog:@"perf_test" encryptKey:nil];
}

- (void)tearDown {
    // 关闭日志系统
    [[LZLogger sharedInstance] close];
    
    // 清理测试目录
    [[NSFileManager defaultManager] removeItemAtPath:self.testLogDir error:nil];
    [super tearDown];
}

#pragma mark - 无加密性能测试

- (void)testPerformanceLogWriteWithoutEncryption_ShortMessage {
    // 短消息（~50字节）- 无加密
    self.continueAfterFailure = NO;
    [self measureLogWritePerformanceWithMessage:@"INFO: User action completed successfully"
                                     encryptKey:nil
                                     iterations:5000];
}

- (void)testPerformanceLogWriteWithoutEncryption_MediumMessage {
    // 中等消息（~150字节）- 无加密
    self.continueAfterFailure = NO;
    [self measureLogWritePerformanceWithMessage:@"INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes"
                                     encryptKey:nil
                                     iterations:5000];
}

- (void)testPerformanceLogWriteWithoutEncryption_LongMessage {
    // 长消息（~300字节）- 无加密
    self.continueAfterFailure = NO;
    [self measureLogWritePerformanceWithMessage:@"ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment"
                                     encryptKey:nil
                                     iterations:5000];
}

#pragma mark - 有加密性能测试

- (void)testPerformanceLogWriteWithEncryption_ShortMessage {
    // 短消息（~50字节）- 有加密
    self.continueAfterFailure = NO;
    [self measureLogWritePerformanceWithMessage:@"INFO: User action completed successfully"
                                     encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                     iterations:5000];
}

- (void)testPerformanceLogWriteWithEncryption_MediumMessage {
    // 中等消息（~150字节）- 有加密
    self.continueAfterFailure = NO;
    [self measureLogWritePerformanceWithMessage:@"INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes"
                                     encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                     iterations:5000];
}

- (void)testPerformanceLogWriteWithEncryption_LongMessage {
    // 长消息（~300字节）- 有加密
    self.continueAfterFailure = NO;
    [self measureLogWritePerformanceWithMessage:@"ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment"
                                     encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                     iterations:5000];
}

#pragma mark - 极限性能测试

- (void)testPerformanceBurstWriteWithoutEncryption {
    // 极限性能：连续写入10000条 - 无加密
    self.continueAfterFailure = NO;
    [self measureLogWritePerformanceWithMessage:@"Benchmark test message"
                                     encryptKey:nil
                                     iterations:10000];
}

- (void)testPerformanceBurstWriteWithEncryption {
    // 极限性能：连续写入10000条 - 有加密
    self.continueAfterFailure = NO;
    [self measureLogWritePerformanceWithMessage:@"Benchmark test message"
                                     encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                     iterations:10000];
}

#pragma mark - 辅助方法

- (void)measureLogWritePerformanceWithMessage:(NSString *)message
                                    encryptKey:(NSString *)encryptKey
                                    iterations:(NSInteger)iterations {
    // 关闭之前的实例并重新初始化
    [[LZLogger sharedInstance] close];
    
    // 准备新的日志实例
    NSString *logName = encryptKey ? @"perf_test_encrypted" : @"perf_test";
    BOOL success = [[LZLogger sharedInstance] prepareLog:logName encryptKey:encryptKey];
    
    XCTAssertTrue(success, @"Failed to prepare logger");
    if (!success) {
        return;
    }
    
    // 预热（1000次，确保 CPU 缓存稳定）
    for (int i = 0; i < 1000; i++) {
        [[LZLogger sharedInstance] log:LZLogLevelInfo
                                  file:__FILE__
                              function:__FUNCTION__
                                  line:__LINE__
                                   tag:@"PERF"
                                format:@"%@", message];
    }
    [[LZLogger sharedInstance] flush];
    
    // XCTest 性能测量
    [self measureBlock:^{
        for (NSInteger i = 0; i < iterations; i++) {
            [[LZLogger sharedInstance] log:LZLogLevelInfo
                                      file:__FILE__
                                  function:__FUNCTION__
                                      line:__LINE__
                                       tag:@"PERF"
                                    format:@"%@", message];
        }
        // 确保所有数据写入磁盘，包含在性能测量中
        [[LZLogger sharedInstance] flush];
    }];
}

@end
