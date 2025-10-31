//
//  LzLoggerPerformanceTests.m
//  RunnerTests
//
//  Performance tests for lz_logger
//

#import <XCTest/XCTest.h>
#import "lz_logger.h"

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
}

- (void)tearDown {
    // 清理测试目录
    [[NSFileManager defaultManager] removeItemAtPath:self.testLogDir error:nil];
    [super tearDown];
}

#pragma mark - 无加密性能测试

- (void)testPerformanceLogWriteWithoutEncryption_ShortMessage {
    // 短消息（~50字节）- 无加密
    [self measureLogWritePerformanceWithMessage:@"INFO: User action completed successfully"
                                     encryptKey:nil
                                     iterations:5000];
}

- (void)testPerformanceLogWriteWithoutEncryption_MediumMessage {
    // 中等消息（~150字节）- 无加密
    [self measureLogWritePerformanceWithMessage:@"INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes"
                                     encryptKey:nil
                                     iterations:5000];
}

- (void)testPerformanceLogWriteWithoutEncryption_LongMessage {
    // 长消息（~300字节）- 无加密
    [self measureLogWritePerformanceWithMessage:@"ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment"
                                     encryptKey:nil
                                     iterations:5000];
}

#pragma mark - 有加密性能测试

- (void)testPerformanceLogWriteWithEncryption_ShortMessage {
    // 短消息（~50字节）- 有加密
    [self measureLogWritePerformanceWithMessage:@"INFO: User action completed successfully"
                                     encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                     iterations:5000];
}

- (void)testPerformanceLogWriteWithEncryption_MediumMessage {
    // 中等消息（~150字节）- 有加密
    [self measureLogWritePerformanceWithMessage:@"INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes"
                                     encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                     iterations:5000];
}

- (void)testPerformanceLogWriteWithEncryption_LongMessage {
    // 长消息（~300字节）- 有加密
    [self measureLogWritePerformanceWithMessage:@"ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment"
                                     encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                     iterations:5000];
}

#pragma mark - 极限性能测试

- (void)testPerformanceBurstWriteWithoutEncryption {
    // 极限性能：连续写入10000条 - 无加密
    [self measureLogWritePerformanceWithMessage:@"Benchmark test message"
                                     encryptKey:nil
                                     iterations:10000];
}

- (void)testPerformanceBurstWriteWithEncryption {
    // 极限性能：连续写入10000条 - 有加密
    [self measureLogWritePerformanceWithMessage:@"Benchmark test message"
                                     encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                     iterations:10000];
}

#pragma mark - 辅助方法

- (void)measureLogWritePerformanceWithMessage:(NSString *)message
                                    encryptKey:(NSString *)encryptKey
                                    iterations:(NSInteger)iterations {
    // 打开 logger
    int32_t innerError = 0;
    int32_t sysErrno = 0;
    lz_logger_handle_t handle = NULL;
    
    const char *key = encryptKey ? [encryptKey UTF8String] : NULL;
    lz_log_error_t ret = lz_logger_open([self.testLogDir UTF8String], key, &handle, &innerError, &sysErrno);
    
    XCTAssertEqual(ret, LZ_LOG_SUCCESS, @"Failed to open logger");
    XCTAssertNotEqual(handle, NULL, @"Logger handle is NULL");
    
    if (ret != LZ_LOG_SUCCESS || handle == NULL) {
        return;
    }
    
    const char *msg = [message UTF8String];
    uint32_t len = (uint32_t)strlen(msg);
    
    // 预热（500次）
    for (int i = 0; i < 500; i++) {
        lz_logger_write(handle, msg, len);
    }
    lz_logger_flush(handle);
    
    // XCTest 性能测量
    [self measureBlock:^{
        for (NSInteger i = 0; i < iterations; i++) {
            lz_log_error_t writeRet = lz_logger_write(handle, msg, len);
            XCTAssertEqual(writeRet, LZ_LOG_SUCCESS, @"Write failed at iteration %ld", (long)i);
        }
    }];
    
    // 关闭 logger
    lz_logger_close(handle);
    
    // 清理测试目录以便下次测试
    [[NSFileManager defaultManager] removeItemAtPath:self.testLogDir error:nil];
    [[NSFileManager defaultManager] createDirectoryAtPath:self.testLogDir 
                              withIntermediateDirectories:YES 
                                               attributes:nil 
                                                    error:nil];
}

@end
