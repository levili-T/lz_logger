#import <Foundation/Foundation.h>
#import <mach/mach_time.h>
#import "lz_logger.h"

@interface LzLoggerPerformanceTest : NSObject

/// 测试日志写入性能并生成报告
+ (void)testLogWritePerformance;

/// 运行所有性能测试
+ (void)runAllTests;

@end

@implementation LzLoggerPerformanceTest

+ (void)testLogWritePerformance {
    NSLog(@"\n========================================");
    NSLog(@"       iOS 日志写入性能测试报告");
    NSLog(@"========================================\n");
    
    // 创建临时日志目录
    NSString *tempDir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"perf_test_logs"];
    NSFileManager *fm = [NSFileManager defaultManager];
    
    // 测试无加密的情况
    NSLog(@"【测试场景 1：无加密】");
    [fm removeItemAtPath:tempDir error:nil];
    [fm createDirectoryAtPath:tempDir withIntermediateDirectories:YES attributes:nil error:nil];
    [self testLogWriteWithEncryption:tempDir encryptKey:nil encrypted:NO];
    
    // 测试有加密的情况
    NSLog(@"\n【测试场景 2：有加密（AES-256-CTR）】");
    [fm removeItemAtPath:tempDir error:nil];
    [fm createDirectoryAtPath:tempDir withIntermediateDirectories:YES attributes:nil error:nil];
    [self testLogWriteWithEncryption:tempDir encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue" encrypted:YES];
    
    // 清理
    [fm removeItemAtPath:tempDir error:nil];
    
    NSLog(@"\n========================================");
    NSLog(@"              测试完成");
    NSLog(@"========================================");
}

+ (void)testLogWriteWithEncryption:(NSString *)logDir encryptKey:(NSString *)encryptKey encrypted:(BOOL)encrypted {
    int32_t innerError = 0;
    int32_t sysErrno = 0;
    lz_logger_handle_t handle = NULL;
    
    const char *key = encryptKey ? [encryptKey UTF8String] : NULL;
    lz_log_error_t ret = lz_logger_open([logDir UTF8String], key, &handle, &innerError, &sysErrno);
    
    if (ret != LZ_LOG_SUCCESS) {
        NSLog(@"❌ 打开日志失败: ret=%d, inner=%d, errno=%d", ret, innerError, sysErrno);
        return;
    }
    
    // 测试不同大小的日志消息
    NSArray *messageLabels = @[
        @"短消息 (~50 字节)",
        @"中等消息 (~150 字节)",
        @"长消息 (~300 字节)"
    ];
    
    NSArray *messages = @[
        @"INFO: User action completed successfully",
        @"INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes",
        @"ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows..."
    ];
    
    int iterations = 5000;  // 测试 5000 次取平均值
    
    NSLog(@"测试参数：");
    NSLog(@"  - 迭代次数: %d", iterations);
    NSLog(@"  - 预热次数: 500");
    NSLog(@"");
    
    // 获取时间基准信息
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    
    for (NSUInteger i = 0; i < messages.count; i++) {
        NSString *message = messages[i];
        const char *msg = [message UTF8String];
        uint32_t len = (uint32_t)strlen(msg);
        
        // 预热
        for (int j = 0; j < 500; j++) {
            lz_logger_write(handle, msg, len);
        }
        lz_logger_flush(handle);
        
        // 正式测试
        uint64_t startTime = mach_absolute_time();
        for (int j = 0; j < iterations; j++) {
            if (lz_logger_write(handle, msg, len) != LZ_LOG_SUCCESS) {
                NSLog(@"❌ 写入日志失败");
                lz_logger_close(handle);
                return;
            }
        }
        uint64_t endTime = mach_absolute_time();
        
        uint64_t totalNs = (endTime - startTime) * timebase.numer / timebase.denom;
        double avgNs = (double)totalNs / iterations;
        double avgUs = avgNs / 1000.0;
        double throughput = 1000000000.0 / avgNs;
        
        NSLog(@"%@:", messageLabels[i]);
        NSLog(@"  ├─ 平均耗时: %.2f μs/条 (%.0f ns/条)", avgUs, avgNs);
        NSLog(@"  ├─ 吞吐量: %.0f 条/秒", throughput);
        NSLog(@"  └─ 实际大小: %u 字节", len);
        NSLog(@"");
    }
    
    // 测试极限性能（连续写入）
    NSLog(@"【极限性能测试】");
    const char *testMsg = "Benchmark test message";
    uint32_t testLen = (uint32_t)strlen(testMsg);
    int burstIterations = 10000;
    
    for (int j = 0; j < 1000; j++) {
        lz_logger_write(handle, testMsg, testLen);
    }
    
    uint64_t burstStart = mach_absolute_time();
    for (int j = 0; j < burstIterations; j++) {
        lz_logger_write(handle, testMsg, testLen);
    }
    uint64_t burstEnd = mach_absolute_time();
    
    uint64_t burstTotalNs = (burstEnd - burstStart) * timebase.numer / timebase.denom;
    double burstAvgUs = (double)burstTotalNs / burstIterations / 1000.0;
    double burstThroughput = 1000000000.0 / ((double)burstTotalNs / burstIterations);
    
    NSLog(@"  ├─ 连续写入 %d 条", burstIterations);
    NSLog(@"  ├─ 平均耗时: %.2f μs/条", burstAvgUs);
    NSLog(@"  └─ 峰值吞吐: %.0f 条/秒", burstThroughput);
    
    lz_logger_close(handle);
}

+ (void)runAllTests {
    [self testLogWritePerformance];
}

@end
