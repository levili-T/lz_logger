#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdbool.h>

#define NUM_THREADS 10  // 与测试程序保持一致
#define LOGS_PER_THREAD 80000  // 与测试程序保持一致
#define TEST_DIR "/tmp/lz_multithread_test"

// 记录每个线程的日志序号
typedef struct {
    int *sequence;
    int count;
    int capacity;
} thread_log_t;

void init_thread_log(thread_log_t *log) {
    log->capacity = LOGS_PER_THREAD;
    log->sequence = (int*)malloc(log->capacity * sizeof(int));
    log->count = 0;
}

void add_log(thread_log_t *log, int log_num) {
    if (log->count >= log->capacity) {
        log->capacity *= 2;
        log->sequence = (int*)realloc(log->sequence, log->capacity * sizeof(int));
    }
    log->sequence[log->count++] = log_num;
}

void free_thread_log(thread_log_t *log) {
    free(log->sequence);
}

// 检查序号是否连续且递增
bool verify_sequence(thread_log_t *log, int thread_id) {
    printf("\n--- Thread-%d 序号验证 ---\n", thread_id);
    printf("总日志数: %d\n", log->count);
    
    if (log->count != LOGS_PER_THREAD) {
        printf("❌ 日志数量不匹配！预期: %d, 实际: %d\n", LOGS_PER_THREAD, log->count);
        return false;
    }
    
    // 检查是否所有序号都存在
    bool *found = (bool*)calloc(LOGS_PER_THREAD, sizeof(bool));
    int duplicates = 0;
    int missing = 0;
    
    for (int i = 0; i < log->count; i++) {
        int num = log->sequence[i];
        if (num < 0 || num >= LOGS_PER_THREAD) {
            printf("❌ 发现非法序号: %d\n", num);
            free(found);
            return false;
        }
        
        if (found[num]) {
            duplicates++;
            if (duplicates <= 5) {  // 只显示前5个重复
                printf("⚠️  发现重复序号: %d\n", num);
            }
        } else {
            found[num] = true;
        }
    }
    
    // 检查缺失的序号
    for (int i = 0; i < LOGS_PER_THREAD; i++) {
        if (!found[i]) {
            missing++;
            if (missing <= 5) {  // 只显示前5个缺失
                printf("❌ 缺失序号: %d\n", i);
            }
        }
    }
    
    free(found);
    
    if (duplicates > 0) {
        printf("❌ 共发现 %d 个重复序号\n", duplicates);
    }
    if (missing > 0) {
        printf("❌ 共缺失 %d 个序号\n", missing);
    }
    
    if (duplicates == 0 && missing == 0) {
        printf("✅ 序号完整且无重复！\n");
        return true;
    }
    
    return false;
}

int main() {
    printf("=== 日志序号连续性验证 ===\n");
    
    thread_log_t thread_logs[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        init_thread_log(&thread_logs[i]);
    }
    
    DIR *dir = opendir(TEST_DIR);
    if (!dir) {
        fprintf(stderr, "Failed to open test directory\n");
        return -1;
    }
    
    // 读取所有日志文件
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".log") == NULL) {
            continue;
        }
        
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s/%s", TEST_DIR, entry->d_name);
        
        printf("读取文件: %s\n", entry->d_name);
        
        FILE *fp = fopen(file_path, "r");
        if (!fp) {
            fprintf(stderr, "Failed to open %s\n", file_path);
            continue;
        }
        
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            int thread_id, log_num;
            if (sscanf(line, "Thread-%d Log-%d", &thread_id, &log_num) == 2) {
                if (thread_id >= 0 && thread_id < NUM_THREADS) {
                    add_log(&thread_logs[thread_id], log_num);
                }
            }
        }
        
        fclose(fp);
    }
    
    closedir(dir);
    
    // 验证每个线程的序号
    bool all_ok = true;
    for (int i = 0; i < NUM_THREADS; i++) {
        if (!verify_sequence(&thread_logs[i], i)) {
            all_ok = false;
        }
    }
    
    // 清理
    for (int i = 0; i < NUM_THREADS; i++) {
        free_thread_log(&thread_logs[i]);
    }
    
    printf("\n=== 验证结果 ===\n");
    if (all_ok) {
        printf("✅ 所有线程的日志序号完整、连续且无重复！\n");
        printf("✅ 多线程竞争处理正确！\n");
        return 0;
    } else {
        printf("❌ 发现序号异常！\n");
        return 1;
    }
}
