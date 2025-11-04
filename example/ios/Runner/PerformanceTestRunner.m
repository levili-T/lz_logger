//
//  PerformanceTestRunner.m
//  Runner
//
//  Performance test runner for LZLogger
//

#import "PerformanceTestRunner.h"
#import <lz_logger/LZLogger.h>

@implementation PerformanceTestRunner

+ (void)runPerformanceTestsWithCompletion:(void (^)(NSDictionary *results))completion {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSMutableDictionary *allResults = [NSMutableDictionary dictionary];
        
        NSLog(@"\n========================================");
        NSLog(@"Starting LZ Logger Performance Tests");
        NSLog(@"========================================\n");
        
        // 测试 1: 短消息无加密
        NSLog(@"Test 1/8: Short message without encryption...");
        NSDictionary *result1 = [self measureLogPerformance:@"Short message (no encryption)"
                                                    message:@"INFO: User action completed successfully"
                                                 encryptKey:nil
                                                 iterations:5000];
        allResults[@"short_no_encrypt"] = result1;
        
        // 测试 2: 中等消息无加密
        NSLog(@"Test 2/8: Medium message without encryption...");
        NSDictionary *result2 = [self measureLogPerformance:@"Medium message (no encryption)"
                                                    message:@"INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes"
                                                 encryptKey:nil
                                                 iterations:5000];
        allResults[@"medium_no_encrypt"] = result2;
        
        // 测试 3: 长消息无加密
        NSLog(@"Test 3/8: Long message without encryption...");
        NSDictionary *result3 = [self measureLogPerformance:@"Long message (no encryption)"
                                                    message:@"ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment"
                                                 encryptKey:nil
                                                 iterations:5000];
        allResults[@"long_no_encrypt"] = result3;
        
        // 测试 4: 极限性能无加密
        NSLog(@"Test 4/8: Burst write without encryption...");
        NSDictionary *result4 = [self measureLogPerformance:@"Burst write (no encryption)"
                                                    message:@"Benchmark test message"
                                                 encryptKey:nil
                                                 iterations:10000];
        allResults[@"burst_no_encrypt"] = result4;
        
        // 重新初始化带加密的日志系统
        NSLog(@"\nReinitializing logger with encryption...");
        [[LZLogger sharedInstance] close];
        [NSThread sleepForTimeInterval:0.1];
        [[LZLogger sharedInstance] prepareLog:@"perf_test_encrypted"
                                    encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"];
        
        // 测试 5: 短消息有加密
        NSLog(@"Test 5/8: Short message with encryption...");
        NSDictionary *result5 = [self measureLogPerformance:@"Short message (with encryption)"
                                                    message:@"INFO: User action completed successfully"
                                                 encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                                 iterations:5000];
        allResults[@"short_encrypt"] = result5;
        
        // 测试 6: 中等消息有加密
        NSLog(@"Test 6/8: Medium message with encryption...");
        NSDictionary *result6 = [self measureLogPerformance:@"Medium message (with encryption)"
                                                    message:@"INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes"
                                                 encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                                 iterations:5000];
        allResults[@"medium_encrypt"] = result6;
        
        // 测试 7: 长消息有加密
        NSLog(@"Test 7/8: Long message with encryption...");
        NSDictionary *result7 = [self measureLogPerformance:@"Long message (with encryption)"
                                                    message:@"ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment"
                                                 encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                                 iterations:5000];
        allResults[@"long_encrypt"] = result7;
        
        // 测试 8: 极限性能有加密
        NSLog(@"Test 8/8: Burst write with encryption...");
        NSDictionary *result8 = [self measureLogPerformance:@"Burst write (with encryption)"
                                                    message:@"Benchmark test message"
                                                 encryptKey:@"laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"
                                                 iterations:10000];
        allResults[@"burst_encrypt"] = result8;
        
        // 输出汇总报告
        [self printSummaryReport:allResults];
        
        // 恢复无加密的日志系统
        [[LZLogger sharedInstance] close];
        [NSThread sleepForTimeInterval:0.1];
        [[LZLogger sharedInstance] prepareLog:@"app_log" encryptKey:nil];
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if (completion) {
                completion(allResults);
            }
        });
    });
}

+ (NSDictionary *)measureLogPerformance:(NSString *)testName
                                message:(NSString *)message
                             encryptKey:(NSString *)encryptKey
                             iterations:(NSInteger)iterations {
    LZLogger *logger = [LZLogger sharedInstance];
    
    // 预热（1000次）
    for (int i = 0; i < 1000; i++) {
        [logger log:LZLogLevelInfo
               file:"perf"
           function:""
               line:0
                tag:@"PERF"
             format:@"%@", message];
    }
    [logger flush];
    
    // 正式测试
    NSDate *startTime = [NSDate date];
    
    for (NSInteger i = 0; i < iterations; i++) {
        [logger log:LZLogLevelInfo
               file:"perf"
           function:""
               line:0
                tag:@"PERF"
             format:@"%@", message];
    }
    [logger flush];
    
    NSDate *endTime = [NSDate date];
    
    double durationMs = [endTime timeIntervalSinceDate:startTime] * 1000.0;
    double throughput = (double)iterations / durationMs * 1000.0;
    double avgLatency = durationMs / (double)iterations;
    
    NSLog(@"  Test: %@", testName);
    NSLog(@"  Iterations: %ld (warmup: 1000)", (long)iterations);
    NSLog(@"  Duration: %.2f ms", durationMs);
    NSLog(@"  Throughput: %.2f logs/sec", throughput);
    NSLog(@"  Avg Latency: %.3f ms/log", avgLatency);
    NSLog(@"");
    
    return @{
        @"name": testName,
        @"iterations": @(iterations),
        @"durationMs": @(durationMs),
        @"throughput": @(throughput),
        @"avgLatency": @(avgLatency)
    };
}

+ (void)printSummaryReport:(NSDictionary *)results {
    NSLog(@"\n========================================");
    NSLog(@"Performance Test Summary Report");
    NSLog(@"Platform: iOS");
    NSLog(@"========================================\n");
    
    NSLog(@"No Encryption Tests:");
    NSLog(@"  Short msg:  %.2f logs/sec, %.3f ms/log",
          [results[@"short_no_encrypt"][@"throughput"] doubleValue],
          [results[@"short_no_encrypt"][@"avgLatency"] doubleValue]);
    NSLog(@"  Medium msg: %.2f logs/sec, %.3f ms/log",
          [results[@"medium_no_encrypt"][@"throughput"] doubleValue],
          [results[@"medium_no_encrypt"][@"avgLatency"] doubleValue]);
    NSLog(@"  Long msg:   %.2f logs/sec, %.3f ms/log",
          [results[@"long_no_encrypt"][@"throughput"] doubleValue],
          [results[@"long_no_encrypt"][@"avgLatency"] doubleValue]);
    NSLog(@"  Burst:      %.2f logs/sec, %.3f ms/log",
          [results[@"burst_no_encrypt"][@"throughput"] doubleValue],
          [results[@"burst_no_encrypt"][@"avgLatency"] doubleValue]);
    NSLog(@"");
    
    NSLog(@"With Encryption Tests:");
    NSLog(@"  Short msg:  %.2f logs/sec, %.3f ms/log",
          [results[@"short_encrypt"][@"throughput"] doubleValue],
          [results[@"short_encrypt"][@"avgLatency"] doubleValue]);
    NSLog(@"  Medium msg: %.2f logs/sec, %.3f ms/log",
          [results[@"medium_encrypt"][@"throughput"] doubleValue],
          [results[@"medium_encrypt"][@"avgLatency"] doubleValue]);
    NSLog(@"  Long msg:   %.2f logs/sec, %.3f ms/log",
          [results[@"long_encrypt"][@"throughput"] doubleValue],
          [results[@"long_encrypt"][@"avgLatency"] doubleValue]);
    NSLog(@"  Burst:      %.2f logs/sec, %.3f ms/log",
          [results[@"burst_encrypt"][@"throughput"] doubleValue],
          [results[@"burst_encrypt"][@"avgLatency"] doubleValue]);
    NSLog(@"");
    
    double avgNoEncrypt = ([results[@"short_no_encrypt"][@"throughput"] doubleValue] +
                          [results[@"medium_no_encrypt"][@"throughput"] doubleValue] +
                          [results[@"long_no_encrypt"][@"throughput"] doubleValue]) / 3.0;
    
    double avgEncrypt = ([results[@"short_encrypt"][@"throughput"] doubleValue] +
                        [results[@"medium_encrypt"][@"throughput"] doubleValue] +
                        [results[@"long_encrypt"][@"throughput"] doubleValue]) / 3.0;
    
    double encryptionOverhead = ((avgNoEncrypt - avgEncrypt) / avgNoEncrypt) * 100.0;
    
    NSLog(@"Average Performance:");
    NSLog(@"  No Encryption: %.2f logs/sec", avgNoEncrypt);
    NSLog(@"  With Encryption: %.2f logs/sec", avgEncrypt);
    NSLog(@"  Encryption Overhead: %.2f%%", encryptionOverhead);
    NSLog(@"\n========================================\n");
}

@end
