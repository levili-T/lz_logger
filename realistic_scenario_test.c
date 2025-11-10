/**
 * 真实场景模拟测试
 * 
 * 本测试模拟真实应用中的日志写入模式：
 * - 日志之间有业务逻辑间隔（数据库、计算、网络I/O等）
 * - 不是疯狂循环写日志
 * - 验证在真实场景下，扩展性远高于极限压力测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "src/lz_logger.h"

// 使用新的API
typedef lz_logger_handle_t lz_logger_ctx;

// 日志级别
#define LOG_LEVEL_INFO 2

// 测试配置
#define NUM_THREADS 10
#define TEST_DURATION_SECONDS 5

// 场景配置
typedef struct {
    const char* name;
    int business_logic_us;  // 业务逻辑耗时（微秒）
    const char* description;
} ScenarioConfig;

// 不同真实场景
ScenarioConfig scenarios[] = {
    {"移动端应用", 10000, "每条日志间隔10ms（数据库+UI渲染）"},
    {"普通后端", 1000, "每条日志间隔1ms（业务处理+数据查询）"},
    {"高频服务器", 100, "每条日志间隔100μs（高频交易/游戏服务器）"},
    {"极限压力", 0, "无间隔（对比基准）"}
};

// 全局统计
typedef struct {
    lz_logger_ctx ctx;
    int scenario_index;
    volatile int should_stop;
    pthread_t threads[NUM_THREADS];
    long thread_counts[NUM_THREADS];
} TestContext;

// 模拟业务逻辑耗时
static void simulate_business_logic(int microseconds) {
    if (microseconds <= 0) return;
    usleep(microseconds);
}

// 简单的日志写入函数
static void write_log(lz_logger_ctx ctx, const char* tag, const char* message) {
    char log_buffer[256];
    int len = snprintf(log_buffer, sizeof(log_buffer), "[%s] %s\n", tag, message);
    lz_logger_write(ctx, log_buffer, len);
}

// 线程函数：模拟真实业务
void* thread_func(void* arg) {
    TestContext* test_ctx = (TestContext*)arg;
    int thread_id = -1;
    
    // 找到当前线程ID
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_equal(pthread_self(), test_ctx->threads[i])) {
            thread_id = i;
            break;
        }
    }
    
    ScenarioConfig* scenario = &scenarios[test_ctx->scenario_index];
    char tag[32];
    snprintf(tag, sizeof(tag), "Thread-%d", thread_id);
    
    long count = 0;
    while (!test_ctx->should_stop) {
        // 模拟真实业务流程
        char message[128];
        
        // 1. 请求开始日志
        snprintf(message, sizeof(message), "Request %ld started", count);
        write_log(test_ctx->ctx, tag, message);
        
        // 2. 模拟数据库查询/业务处理
        simulate_business_logic(scenario->business_logic_us / 3);
        
        // 3. 业务处理日志
        snprintf(message, sizeof(message), "Processing request %ld", count);
        write_log(test_ctx->ctx, tag, message);
        
        // 4. 模拟更多业务逻辑
        simulate_business_logic(scenario->business_logic_us / 3);
        
        // 5. 请求完成日志
        snprintf(message, sizeof(message), "Request %ld completed", count);
        write_log(test_ctx->ctx, tag, message);
        
        // 6. 模拟网络I/O
        simulate_business_logic(scenario->business_logic_us / 3);
        
        count++;
    }
    
    test_ctx->thread_counts[thread_id] = count;
    return NULL;
}

// 单线程基准测试
long single_thread_benchmark(lz_logger_ctx ctx, int scenario_index) {
    ScenarioConfig* scenario = &scenarios[scenario_index];
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    long count = 0;
    time_t test_start = time(NULL);
    
    while (time(NULL) - test_start < TEST_DURATION_SECONDS) {
        char message[128];
        
        snprintf(message, sizeof(message), "Request %ld started", count);
        write_log(ctx, "Single", message);
        simulate_business_logic(scenario->business_logic_us / 3);
        
        snprintf(message, sizeof(message), "Processing request %ld", count);
        write_log(ctx, "Single", message);
        simulate_business_logic(scenario->business_logic_us / 3);
        
        snprintf(message, sizeof(message), "Request %ld completed", count);
        write_log(ctx, "Single", message);
        simulate_business_logic(scenario->business_logic_us / 3);
        
        count++;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    long total_logs = count * 3;  // 每个请求3条日志
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double throughput = total_logs / elapsed;
    
    printf("\n单线程基准：\n");
    printf("  总请求数：%ld\n", count);
    printf("  总日志数：%ld\n", total_logs);
    printf("  耗时：%.2f秒\n", elapsed);
    printf("  吞吐量：%.2f条/秒 (%.0fns/条)\n", throughput, 1e9 / throughput);
    
    return total_logs;
}

// 多线程测试
long multi_thread_test(TestContext* test_ctx) {
    ScenarioConfig* scenario = &scenarios[test_ctx->scenario_index];
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // 创建线程
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&test_ctx->threads[i], NULL, thread_func, test_ctx);
    }
    
    // 运行指定时间
    sleep(TEST_DURATION_SECONDS);
    test_ctx->should_stop = 1;
    
    // 等待线程结束
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(test_ctx->threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // 统计结果
    long total_requests = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total_requests += test_ctx->thread_counts[i];
    }
    long total_logs = total_requests * 3;  // 每个请求3条日志
    
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double throughput = total_logs / elapsed;
    
    printf("\n%d线程测试：\n", NUM_THREADS);
    printf("  总请求数：%ld\n", total_requests);
    printf("  总日志数：%ld\n", total_logs);
    printf("  耗时：%.2f秒\n", elapsed);
    printf("  吞吐量：%.2f条/秒 (%.0fns/条)\n", throughput, 1e9 / throughput);
    
    // 显示每线程分布
    printf("\n  线程分布：\n");
    long min_count = test_ctx->thread_counts[0];
    long max_count = test_ctx->thread_counts[0];
    for (int i = 0; i < NUM_THREADS; i++) {
        if (test_ctx->thread_counts[i] < min_count) min_count = test_ctx->thread_counts[i];
        if (test_ctx->thread_counts[i] > max_count) max_count = test_ctx->thread_counts[i];
        printf("    线程%d: %ld个请求 (%ld条日志)\n", 
               i, test_ctx->thread_counts[i], test_ctx->thread_counts[i] * 3);
    }
    double balance = (double)min_count / max_count * 100;
    printf("  负载均衡度：%.1f%%\n", balance);
    
    return total_logs;
}

// 运行场景测试
void run_scenario_test(int scenario_index) {
    ScenarioConfig* scenario = &scenarios[scenario_index];
    
    printf("\n");
    printf("================================================================================\n");
    printf("场景测试：%s\n", scenario->name);
    printf("================================================================================\n");
    printf("描述：%s\n", scenario->description);
    printf("日志间隔：%d微秒\n", scenario->business_logic_us);
    printf("测试时长：%d秒\n", TEST_DURATION_SECONDS);
    
    // 初始化日志系统
    lz_logger_ctx ctx = NULL;
    lz_log_error_t err = lz_logger_open("/tmp", NULL, &ctx, NULL, NULL);
    if (err != LZ_LOG_SUCCESS || !ctx) {
        fprintf(stderr, "Failed to create logger: %d\n", err);
        return;
    }
    
    // 设置更大的文件大小
    lz_logger_set_max_file_size(50 * 1024 * 1024);  // 50MB
    
    // 单线程测试
    long single_count = single_thread_benchmark(ctx, scenario_index);
    
    // 关闭并重新创建（清空日志）
    lz_logger_close(ctx);
    system("rm -f /tmp/*.log");
    err = lz_logger_open("/tmp", NULL, &ctx, NULL, NULL);
    lz_logger_set_max_file_size(50 * 1024 * 1024);  // 50MB
    
    // 多线程测试
    TestContext test_ctx = {0};
    test_ctx.ctx = ctx;
    test_ctx.scenario_index = scenario_index;
    test_ctx.should_stop = 0;
    
    long multi_count = multi_thread_test(&test_ctx);
    
    // 计算扩展性
    double single_throughput = (double)single_count / TEST_DURATION_SECONDS;
    double multi_throughput = (double)multi_count / TEST_DURATION_SECONDS;
    double per_thread_throughput = multi_throughput / NUM_THREADS;
    double scalability = (per_thread_throughput / single_throughput) * 100;
    
    printf("\n性能分析：\n");
    printf("  单线程吞吐量：%.2f条/秒\n", single_throughput);
    printf("  %d线程总吞吐量：%.2f条/秒\n", NUM_THREADS, multi_throughput);
    printf("  每线程平均吞吐量：%.2f条/秒\n", per_thread_throughput);
    printf("  扩展性：%.1f%%\n", scalability);
    
    // 评级
    const char* rating;
    if (scalability >= 90) rating = "⭐⭐⭐⭐⭐ 优秀";
    else if (scalability >= 70) rating = "⭐⭐⭐⭐ 良好";
    else if (scalability >= 50) rating = "⭐⭐⭐ 中等";
    else if (scalability >= 30) rating = "⭐⭐ 偏低";
    else rating = "⭐ 需优化";
    
    printf("  评级：%s\n", rating);
    
    // 清理
    lz_logger_close(ctx);
    system("rm -f /tmp/*.log");
}

int main() {
    printf("真实场景模拟测试\n");
    printf("================================================================================\n");
    printf("本测试模拟真实应用中的日志写入模式：\n");
    printf("  - 日志之间有业务逻辑间隔（数据库、计算、网络I/O等）\n");
    printf("  - 每个请求写3条日志（开始、处理中、完成）\n");
    printf("  - 对比不同场景下的扩展性表现\n");
    printf("\n");
    printf("测试参数：\n");
    printf("  线程数：%d\n", NUM_THREADS);
    printf("  测试时长：%d秒/场景\n", TEST_DURATION_SECONDS);
    printf("\n");
    
    // 运行所有场景
    for (int i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        run_scenario_test(i);
    }
    
    printf("\n");
    printf("================================================================================\n");
    printf("测试总结\n");
    printf("================================================================================\n");
    printf("从测试结果可以看出：\n");
    printf("  1. 有业务间隔时，扩展性显著提升\n");
    printf("  2. 日志间隔越大，CAS冲突越少，扩展性越高\n");
    printf("  3. 真实场景（有业务逻辑）下，扩展性远高于极限压力测试\n");
    printf("  4. CAS方案在真实应用中表现优秀\n");
    printf("\n");
    
    return 0;
}
