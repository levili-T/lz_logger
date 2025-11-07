#include "src/lz_logger.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>

#define NUM_THREADS 10  // 10个线程
#define LOGS_PER_THREAD 20000  // 每个线程2万条，共20万条
#define TEST_DIR "/tmp/lz_multithread_test"
#define ENCRYPT_KEY "test_encryption_key_12345"  // 测试加密密钥

// 线程参数结构
typedef struct {
    lz_logger_handle_t logger;
    int thread_id;
    int *success_count;
    pthread_mutex_t *count_mutex;
} thread_arg_t;

// 线程函数
void* write_logs(void* arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    int count = 0;
    
    for (int i = 0; i < LOGS_PER_THREAD; i++) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Thread-%d Log-%d\n", targ->thread_id, i);
        
        lz_log_error_t ret = lz_logger_write(targ->logger, log_msg, strlen(log_msg));
        if (ret == LZ_LOG_SUCCESS) {
            count++;
        } else {
            fprintf(stderr, "[Thread-%d] Write failed at log %d: %s\n", 
                    targ->thread_id, i, lz_logger_error_string(ret));
        }
    }
    
    pthread_mutex_lock(targ->count_mutex);
    *targ->success_count += count;
    pthread_mutex_unlock(targ->count_mutex);
    
    printf("[Thread-%d] Completed: %d logs written\n", targ->thread_id, count);
    return NULL;
}

// 验证盐值一致性
int verify_salt_consistency() {
    printf("\n=== 验证盐值一致性 ===\n");
    
    DIR *dir = opendir(TEST_DIR);
    if (!dir) {
        fprintf(stderr, "Failed to open test directory\n");
        return -1;
    }
    
    uint8_t first_salt[16] = {0};
    int first_file = 1;
    int total_files = 0;
    int salt_mismatch = 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".log") == NULL) {
            continue;
        }
        
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s/%s", TEST_DIR, entry->d_name);
        
        FILE *fp = fopen(file_path, "rb");
        if (!fp) {
            continue;
        }
        
        // 读取文件尾部的盐值（footer: [盐16字节][魔数4字节][大小4字节]）
        fseek(fp, -24, SEEK_END);
        uint8_t salt[16];
        if (fread(salt, 1, 16, fp) != 16) {
            fclose(fp);
            continue;
        }
        fclose(fp);
        
        total_files++;
        
        if (first_file) {
            memcpy(first_salt, salt, 16);
            printf("基准盐值 (文件: %s): ", entry->d_name);
            for (int i = 0; i < 16; i++) {
                printf("%02x", salt[i]);
            }
            printf("\n");
            first_file = 0;
        } else {
            if (memcmp(first_salt, salt, 16) != 0) {
                salt_mismatch++;
                printf("❌ 盐值不一致 (文件: %s): ", entry->d_name);
                for (int i = 0; i < 16; i++) {
                    printf("%02x", salt[i]);
                }
                printf("\n");
            } else {
                printf("✅ 盐值一致 (文件: %s)\n", entry->d_name);
            }
        }
    }
    
    closedir(dir);
    
    printf("\n总文件数: %d\n", total_files);
    if (salt_mismatch == 0) {
        printf("✅ 所有文件盐值一致！\n");
        return 0;
    } else {
        printf("❌ 发现 %d 个文件盐值不一致！\n", salt_mismatch);
        return -1;
    }
}

// 验证日志内容（需要解密）
int verify_logs() {
    printf("\n=== 验证加密日志内容 ===\n");
    
    // 先解密所有日志文件
    printf("正在解密日志文件...\n");
    char decrypt_cmd[512];
    snprintf(decrypt_cmd, sizeof(decrypt_cmd), 
             "python3 tools/decrypt_log.py -d %s -p %s -o %s/decrypted",
             TEST_DIR, ENCRYPT_KEY, TEST_DIR);
    
    int decrypt_ret = system(decrypt_cmd);
    if (decrypt_ret != 0) {
        fprintf(stderr, "❌ 解密失败\n");
        return -1;
    }
    
    printf("✅ 解密完成\n\n");
    
    // 验证解密后的文件
    char decrypted_dir[512];
    snprintf(decrypted_dir, sizeof(decrypted_dir), "%s/decrypted", TEST_DIR);
    
    DIR *dir = opendir(decrypted_dir);
    if (!dir) {
        fprintf(stderr, "Failed to open decrypted directory\n");
        return -1;
    }
    
    // 统计每个线程的日志数量
    int thread_counts[NUM_THREADS] = {0};
    int total_logs = 0;
    int total_files = 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "_decrypted.txt") == NULL) {
            continue;
        }
        
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s/%s", decrypted_dir, entry->d_name);
        
        FILE *fp = fopen(file_path, "r");
        if (!fp) {
            fprintf(stderr, "Failed to open %s\n", file_path);
            continue;
        }
        
        total_files++;
        printf("检查文件: %s\n", entry->d_name);
        
        char line[256];
        int file_log_count = 0;
        
        while (fgets(line, sizeof(line), fp)) {
            int thread_id, log_num;
            if (sscanf(line, "Thread-%d Log-%d", &thread_id, &log_num) == 2) {
                if (thread_id >= 0 && thread_id < NUM_THREADS) {
                    thread_counts[thread_id]++;
                    total_logs++;
                    file_log_count++;
                }
            }
        }
        
        printf("  -> 包含 %d 条日志\n", file_log_count);
        fclose(fp);
    }
    
    closedir(dir);
    
    // 打印统计结果
    printf("\n=== 统计结果 ===\n");
    printf("总文件数: %d\n", total_files);
    printf("总日志数: %d (预期: %d)\n", total_logs, NUM_THREADS * LOGS_PER_THREAD);
    printf("\n各线程日志分布:\n");
    
    int total_verified = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        printf("  Thread-%d: %d 条 (预期: %d) %s\n", 
               i, thread_counts[i], LOGS_PER_THREAD,
               thread_counts[i] == LOGS_PER_THREAD ? "✅" : "❌");
        total_verified += thread_counts[i];
    }
    
    printf("\n验证总计: %d / %d\n", total_verified, NUM_THREADS * LOGS_PER_THREAD);
    
    if (total_verified == NUM_THREADS * LOGS_PER_THREAD) {
        printf("✅ 所有日志验证通过！\n");
        return 0;
    } else {
        printf("❌ 日志验证失败！缺失 %d 条日志\n", 
               NUM_THREADS * LOGS_PER_THREAD - total_verified);
        return -1;
    }
}

int main() {
    printf("=== 多线程文件切换竞争测试 ===\n\n");
    
    // 清理测试目录
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", TEST_DIR, TEST_DIR);
    system(cmd);
    
    // 设置较小的文件大小以触发频繁切换
    // 80000 logs * 10 threads * ~25 bytes = ~20MB
    // 每个文件 1MB，预期会切换 20+ 次
    uint32_t file_size = 1 * 1024 * 1024;  // 1MB
    lz_log_error_t size_ret = lz_logger_set_max_file_size(file_size);
    if (size_ret != LZ_LOG_SUCCESS) {
        fprintf(stderr, "❌ 设置文件大小失败: %s\n", lz_logger_error_string(size_ret));
        return -1;
    }
    printf("设置文件大小: %u bytes (%.2f MB)\n", file_size, file_size / (1024.0 * 1024.0));
    printf("线程数: %d\n", NUM_THREADS);
    printf("每线程日志数: %d\n", LOGS_PER_THREAD);
    printf("预计总数据量: %.2f MB\n\n", 
           (NUM_THREADS * LOGS_PER_THREAD * 25.0) / (1024.0 * 1024.0));
    
    // 打开日志系统（启用加密）
    lz_logger_handle_t logger;
    lz_log_error_t ret = lz_logger_open(TEST_DIR, ENCRYPT_KEY, &logger, NULL, NULL);
    if (ret != LZ_LOG_SUCCESS) {
        fprintf(stderr, "❌ 日志系统初始化失败: %s\n", lz_logger_error_string(ret));
        return -1;
    }
    printf("✅ 日志系统初始化成功（加密已启用）\n\n");
    
    // 创建线程
    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];
    int success_count = 0;
    pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    printf("📝 启动 %d 个线程写入日志...\n\n", NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].logger = logger;
        args[i].thread_id = i;
        args[i].success_count = &success_count;
        args[i].count_mutex = &count_mutex;
        
        if (pthread_create(&threads[i], NULL, write_logs, &args[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            return -1;
        }
    }
    
    // 等待所有线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n✅ 所有线程完成\n");
    printf("成功写入: %d / %d 条日志\n", success_count, NUM_THREADS * LOGS_PER_THREAD);
    
    // 刷新并关闭
    lz_logger_flush(logger);
    lz_logger_close(logger);
    
    printf("\n✅ 日志系统已关闭\n");
    
    // 验证盐值一致性
    int salt_result = verify_salt_consistency();
    
    // 验证日志内容
    int verify_result = verify_logs();
    
    // 列出生成的文件
    printf("\n=== 生成的文件列表 ===\n");
    snprintf(cmd, sizeof(cmd), "ls -lh %s/*.log", TEST_DIR);
    system(cmd);
    
    pthread_mutex_destroy(&count_mutex);
    
    if (salt_result == 0 && verify_result == 0 && success_count == NUM_THREADS * LOGS_PER_THREAD) {
        printf("\n✅✅✅ 所有测试完全通过！\n");
        printf("  ✅ 盐值一致性: 通过\n");
        printf("  ✅ 日志完整性: 通过\n");
        printf("  ✅ 加密解密: 通过\n");
        return 0;
    } else {
        printf("\n❌ 测试失败！\n");
        if (salt_result != 0) printf("  ❌ 盐值一致性检查失败\n");
        if (verify_result != 0) printf("  ❌ 日志验证失败\n");
        if (success_count != NUM_THREADS * LOGS_PER_THREAD) printf("  ❌ 日志数量不匹配\n");
        return 1;
    }
}
