#include "src/lz_logger.h"
#include <pthread.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#define TEST_LOG_DIR "/tmp/lz_logger_perf_test"
#define SINGLE_THREAD_ITERATIONS 100000
#define MULTI_THREAD_ITERATIONS 10000
#define NUM_THREADS 10

// 测试日志消息（模拟真实日志格式）
static const char* test_messages[] = {
    "2025-11-02 15:30:45.123 T:1a2b3c [MainActivity.kt:45] [onCreate] [App] Application started successfully\n",
    "2025-11-02 15:30:45.456 T:1a2b3c [NetworkManager.kt:89] [request] [Network] HTTP request to https://api.example.com/data\n",
    "2025-11-02 15:30:45.789 T:2c3d4e [DatabaseHelper.kt:123] [query] [DB] Query executed: SELECT * FROM users WHERE id=12345\n",
    "2025-11-02 15:30:46.012 T:3e4f5a [ImageLoader.kt:67] [loadImage] [Image] Loading image from cache: /cache/img_12345.jpg\n",
    "2025-11-02 15:30:46.345 T:4f5a6b [AnalyticsService.kt:234] [trackEvent] [Analytics] Event tracked: user_login with params {user_id: 67890}\n"
};
static const int num_test_messages = 5;

// 获取当前时间（微秒）
static uint64_t get_timestamp_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 格式化数字（添加千分位分隔符）
static char* format_number(int num) {
    static char buffers[4][32];  // 使用4个缓冲区轮换
    static int buffer_index = 0;
    
    char* buffer = buffers[buffer_index];
    buffer_index = (buffer_index + 1) % 4;
    
    char temp[32];
    snprintf(temp, sizeof(temp), "%d", num);
    
    int len = strlen(temp);
    int comma_count = (len - 1) / 3;
    int new_len = len + comma_count;
    
    buffer[new_len] = '\0';
    
    int temp_idx = len - 1;
    int buf_idx = new_len - 1;
    int count = 0;
    
    while (temp_idx >= 0) {
        if (count == 3) {
            buffer[buf_idx--] = ',';
            count = 0;
        }
        buffer[buf_idx--] = temp[temp_idx--];
        count++;
    }
    
    return buffer;
}

// 创建测试目录
static int create_test_dir() {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", TEST_LOG_DIR, TEST_LOG_DIR);
    return system(cmd);
}

// 单线程性能测试
static void test_single_thread_performance() {
    printf("\n## 测试1: 单线程写入性能\n\n");
    
    lz_logger_handle_t handle = NULL;
    int32_t inner_error = 0, sys_errno = 0;
    
    // 打开日志系统（不加密）
    lz_log_error_t ret = lz_logger_open(TEST_LOG_DIR, NULL, &handle, &inner_error, &sys_errno);
    if (ret != LZ_LOG_SUCCESS) {
        printf("❌ 打开日志失败: %s (inner=%d, errno=%d)\n", 
               lz_logger_error_string(ret), inner_error, sys_errno);
        return;
    }
    
    printf("✅ 日志系统初始化成功\n");
    printf("📝 开始写入 %d 条日志...\n\n", SINGLE_THREAD_ITERATIONS);
    
    uint64_t start_time = get_timestamp_us();
    
    // 写入测试
    for (int i = 0; i < SINGLE_THREAD_ITERATIONS; i++) {
        const char* msg = test_messages[i % num_test_messages];
        ret = lz_logger_write(handle, msg, (uint32_t)strlen(msg));
        if (ret != LZ_LOG_SUCCESS) {
            printf("❌ 写入失败: %s\n", lz_logger_error_string(ret));
            break;
        }
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t elapsed_us = end_time - start_time;
    
    // 强制刷新到磁盘
    lz_logger_flush(handle);
    
    // 计算性能指标
    double elapsed_sec = elapsed_us / 1000000.0;
    double logs_per_sec = SINGLE_THREAD_ITERATIONS / elapsed_sec;
    double ns_per_log = (double)elapsed_us * 1000.0 / SINGLE_THREAD_ITERATIONS; // 纳秒
    double mb_written = (SINGLE_THREAD_ITERATIONS * 120) / (1024.0 * 1024.0); // 平均每条120字节
    double mb_per_sec = mb_written / elapsed_sec;
    
    printf("✅ 测试完成\n\n");
    printf("| 指标 | 数值 |\n");
    printf("|------|------|\n");
    printf("| **总耗时** | %.2f 秒 |\n", elapsed_sec);
    printf("| **日志条数** | %d 条 |\n", SINGLE_THREAD_ITERATIONS);
    printf("| **单条耗时** | **%.0f 纳秒/条** |\n", ns_per_log);
    printf("| **写入速度** | %s 条/秒 |\n", format_number((int)logs_per_sec));
    printf("| **数据量** | %.2f MB |\n", mb_written);
    printf("| **吞吐量** | %.2f MB/秒 |\n", mb_per_sec);
    printf("\n");
    
    lz_logger_close(handle);
}

// 多线程测试的线程函数
typedef struct {
    lz_logger_handle_t handle;
    int thread_id;
    uint64_t elapsed_us;
    int success_count;
} thread_data_t;

static void* thread_write_func(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    
    uint64_t start_time = get_timestamp_us();
    
    for (int i = 0; i < MULTI_THREAD_ITERATIONS; i++) {
        const char* msg = test_messages[i % num_test_messages];
        lz_log_error_t ret = lz_logger_write(data->handle, msg, (uint32_t)strlen(msg));
        if (ret == LZ_LOG_SUCCESS) {
            data->success_count++;
        }
    }
    
    uint64_t end_time = get_timestamp_us();
    data->elapsed_us = end_time - start_time;
    
    return NULL;
}

// 多线程性能测试
static void test_multi_thread_performance() {
    printf("\n## 测试2: 多线程并发写入性能\n\n");
    
    lz_logger_handle_t handle = NULL;
    int32_t inner_error = 0, sys_errno = 0;
    
    // 打开日志系统（不加密）
    lz_log_error_t ret = lz_logger_open(TEST_LOG_DIR, NULL, &handle, &inner_error, &sys_errno);
    if (ret != LZ_LOG_SUCCESS) {
        printf("❌ 打开日志失败: %s (inner=%d, errno=%d)\n", 
               lz_logger_error_string(ret), inner_error, sys_errno);
        return;
    }
    
    printf("✅ 日志系统初始化成功\n");
    printf("📝 启动 %d 个线程，每个写入 %d 条日志...\n\n", 
           NUM_THREADS, MULTI_THREAD_ITERATIONS);
    
    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];
    
    uint64_t start_time = get_timestamp_us();
    
    // 创建线程
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].handle = handle;
        thread_data[i].thread_id = i;
        thread_data[i].elapsed_us = 0;
        thread_data[i].success_count = 0;
        pthread_create(&threads[i], NULL, thread_write_func, &thread_data[i]);
    }
    
    // 等待所有线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t total_elapsed_us = end_time - start_time;
    
    // 强制刷新到磁盘
    lz_logger_flush(handle);
    
    // 统计结果
    int total_success = 0;
    uint64_t max_thread_time = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total_success += thread_data[i].success_count;
        if (thread_data[i].elapsed_us > max_thread_time) {
            max_thread_time = thread_data[i].elapsed_us;
        }
    }
    
    double elapsed_sec = total_elapsed_us / 1000000.0;
    double logs_per_sec = total_success / elapsed_sec;
    double ns_per_log = (double)total_elapsed_us * 1000.0 / total_success; // 总纳秒/总条数
    double mb_written = (total_success * 120) / (1024.0 * 1024.0);
    double mb_per_sec = mb_written / elapsed_sec;
    
    printf("✅ 测试完成\n\n");
    printf("| 指标 | 数值 |\n");
    printf("|------|------|\n");
    printf("| **线程数** | %d 个 |\n", NUM_THREADS);
    printf("| **总耗时** | %.2f 秒 |\n", elapsed_sec);
    printf("| **日志条数** | %s 条 |\n", format_number(total_success));
    printf("| **吞吐量** | **%s 条/秒** ⭐ |\n", format_number((int)logs_per_sec));
    printf("| **平均延迟** | %.0f 纳秒/条 |\n", ns_per_log);
    printf("| **数据量** | %.2f MB |\n", mb_written);
    printf("| **写入速度** | %.2f MB/秒 |\n", mb_per_sec);
    
    printf("\n**各线程性能分布：**\n\n");
    printf("| 线程 | 耗时(秒) | 日志数 | 速度(条/秒) |\n");
    printf("|------|----------|--------|-------------|\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        double thread_sec = thread_data[i].elapsed_us / 1000000.0;
        double thread_logs_per_sec = MULTI_THREAD_ITERATIONS / thread_sec;
        printf("| 线程 %d | %.2f | %s | %s |\n",
               i, thread_sec, 
               format_number(MULTI_THREAD_ITERATIONS),
               format_number((int)thread_logs_per_sec));
    }
    printf("\n");
    
    lz_logger_close(handle);
}

// 测试加密模式性能
static void test_encryption_performance() {
    printf("\n## 测试3: 加密模式性能测试\n\n");
    
    const char* encrypt_key = "test_encryption_key_12345678";
    lz_logger_handle_t handle = NULL;
    int32_t inner_error = 0, sys_errno = 0;
    
    // 打开日志系统（加密）
    lz_log_error_t ret = lz_logger_open(TEST_LOG_DIR, encrypt_key, &handle, &inner_error, &sys_errno);
    if (ret != LZ_LOG_SUCCESS) {
        printf("❌ 打开日志失败: %s (inner=%d, errno=%d)\n", 
               lz_logger_error_string(ret), inner_error, sys_errno);
        return;
    }
    
    printf("✅ 日志系统初始化成功（加密模式: AES-128-CBC）\n");
    printf("📝 开始写入 %s 条日志...\n\n", format_number(SINGLE_THREAD_ITERATIONS));
    
    uint64_t start_time = get_timestamp_us();
    
    // 写入测试
    int success_count = 0;
    for (int i = 0; i < SINGLE_THREAD_ITERATIONS; i++) {
        const char* msg = test_messages[i % num_test_messages];
        ret = lz_logger_write(handle, msg, (uint32_t)strlen(msg));
        if (ret != LZ_LOG_SUCCESS) {
            printf("❌ 写入失败: %s\n", lz_logger_error_string(ret));
            break;
        }
        success_count++;
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t elapsed_us = end_time - start_time;
    
    lz_logger_flush(handle);
    
    double elapsed_sec = elapsed_us / 1000000.0;
    double logs_per_sec = success_count / elapsed_sec;
    double ns_per_log = (double)elapsed_us * 1000.0 / success_count; // 纳秒
    double mb_written = (success_count * 120) / (1024.0 * 1024.0);
    double mb_per_sec = mb_written / elapsed_sec;
    
    printf("✅ 测试完成\n\n");
    printf("| 指标 | 数值 |\n");
    printf("|------|------|\n");
    printf("| **总耗时** | %.2f 秒 |\n", elapsed_sec);
    printf("| **日志条数** | %s 条 |\n", format_number(success_count));
    printf("| **单条耗时** | **%.0f 纳秒/条** |\n", ns_per_log);
    printf("| **写入速度** | %s 条/秒 |\n", format_number((int)logs_per_sec));
    printf("| **数据量** | %.2f MB |\n", mb_written);
    printf("| **吞吐量** | %.2f MB/秒 |\n", mb_per_sec);
    printf("\n");
    
    lz_logger_close(handle);
}

int main() {
    printf("\n");
    printf("# LZ Logger 性能测试报告\n\n");
    printf("**测试工具版本:** v1.0  \n");
    
    // 设置文件大小为40MB（避免文件切换影响测试）
    lz_log_error_t ret = lz_logger_set_max_file_size(40 * 1024 * 1024);
    if (ret != LZ_LOG_SUCCESS) {
        printf("❌ 设置文件大小失败: %s\n", lz_logger_error_string(ret));
        return 1;
    }
    printf("**文件大小:** 40MB (避免文件切换)  \n");
    
    // 创建测试目录
    if (create_test_dir() != 0) {
        printf("❌ 创建测试目录失败\n");
        return 1;
    }
    
    printf("**测试目录:** `%s`  \n", TEST_LOG_DIR);
    
    // 运行测试
    test_single_thread_performance();
    sleep(1);
    
    test_multi_thread_performance();
    sleep(1);
    
    test_encryption_performance();
    
    printf("\n---\n\n");
    printf("✅ **所有测试完成！**\n\n");
    
    return 0;
}
