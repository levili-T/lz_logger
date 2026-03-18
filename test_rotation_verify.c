#include "src/lz_logger.h"
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

// ============================================================================
// 配置
// ============================================================================

#define NUM_THREADS      8
#define FILE_SIZE        (1 * 1024 * 1024)  // 1MB — 最小允许值，触发频繁轮转
#define MAX_DAILY_FILES  5              // 与 lz_logger.c 中 LZ_LOG_MAX_DAILY_FILES 一致
#define TEST_DIR         "/tmp/lz_rotation_verify"
#define ENCRYPT_KEY      "rotation_test_key_123"

// 日志格式: "SEQ:0000000001 T:00\n" = 20 字节（固定长度，便于序号解析）
#define LOG_FMT          "SEQ:%010" PRIu64 " T:%02d\n"
#define LOG_BYTES        20

// 每个文件能容纳的日志条数（去掉 footer 28 字节）
#define LOGS_PER_FILE    ((FILE_SIZE - 28) / LOG_BYTES)

// ============================================================================
// 全局原子序号
// ============================================================================

static atomic_uint_least64_t g_seq = 0;

// ============================================================================
// 写线程
// ============================================================================

typedef struct {
    lz_logger_handle_t logger;
    int    thread_id;
    long long count;
    long long success;
} targ_t;

static void *write_thread(void *arg) {
    targ_t *t = (targ_t *)arg;
    char buf[32];
    t->success = 0;
    for (long long i = 0; i < t->count; i++) {
        uint64_t seq = atomic_fetch_add(&g_seq, 1);
        int len = snprintf(buf, sizeof(buf), LOG_FMT, seq, t->thread_id);
        if (lz_logger_write(t->logger, buf, len) == LZ_LOG_SUCCESS)
            t->success++;
    }
    return NULL;
}

// ============================================================================
// 序号验证：扫描解密目录，统计并验证
// ============================================================================

static int verify_sequences(const char *dec_dir, uint64_t total_seq_assigned,
                             int n_rotations)
{
    DIR *d = opendir(dec_dir);
    if (!d) {
        fprintf(stderr, "  无法打开解密目录: %s\n", dec_dir);
        return -1;
    }

    uint64_t min_seq  = UINT64_MAX;
    uint64_t max_seq  = 0;
    long long found   = 0;
    long long early   = 0;   // 属于"应已被删除"轮次的序号数

    // 若轮转超过 MAX_DAILY_FILES，最旧的 (n_rotations - MAX_DAILY_FILES) 批次
    // 对应的序号应已从磁盘消失
    uint64_t expected_min = 0;
    uint64_t stale_threshold = 0;
    if (n_rotations > MAX_DAILY_FILES) {
        // 最旧保留批次的起始序号（大约）
        expected_min     = (uint64_t)(n_rotations - MAX_DAILY_FILES) * LOGS_PER_FILE;
        // 比期望最小值再退 10%，作为"绝对应消失"的边界
        stale_threshold  = expected_min * 9 / 10;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (!strstr(entry->d_name, "_decrypted.txt")) continue;

        char path[768];
        snprintf(path, sizeof(path), "%s/%s", dec_dir, entry->d_name);

        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char line[64];
        while (fgets(line, sizeof(line), fp)) {
            uint64_t seq;
            int tid;
            if (sscanf(line, "SEQ:%" SCNu64 " T:%d", &seq, &tid) == 2) {
                found++;
                if (seq < min_seq) min_seq = seq;
                if (seq > max_seq) max_seq = seq;
                if (stale_threshold > 0 && seq < stale_threshold)
                    early++;
            }
        }
        fclose(fp);
    }
    closedir(d);

    printf("\n  ┌─ 序号统计 ─────────────────────────────\n");
    printf("  │ 分配序号总数:    %" PRIu64 "\n", total_seq_assigned);
    printf("  │ 文件中找到:      %lld 条\n", found);
    if (found > 0) {
        printf("  │ 最小序号:        %" PRIu64 "\n", min_seq);
        printf("  │ 最大序号:        %" PRIu64 "\n", max_seq);
    }
    if (n_rotations > MAX_DAILY_FILES) {
        printf("  │ 期望最小序号:    ~%" PRIu64
               "  (保留最新 %d 批)\n", expected_min, MAX_DAILY_FILES);
        printf("  │ 旧数据边界:      <%" PRIu64 " 不应出现\n", stale_threshold);
        printf("  │ 旧数据残留数:    %lld 条\n", early);
    }
    printf("  └────────────────────────────────────────\n");

    int ok = 1;

    // 检查1：旧数据不能出现
    if (stale_threshold > 0 && early > 0) {
        printf("  FAIL: 旧数据未被正确删除，%lld 条早期序号仍在磁盘\n", early);
        ok = 0;
    } else if (stale_threshold > 0) {
        printf("  PASS: 旧数据已被完全替换\n");
    }

    // 检查2：剩余数据量合理（最多 MAX_DAILY_FILES 个文件）
    long long expected_found_max = (long long)MAX_DAILY_FILES * LOGS_PER_FILE;
    if (found > expected_found_max * 11 / 10) {
        printf("  FAIL: 找到日志数 %lld 超出预期上限 %lld\n",
               found, expected_found_max);
        ok = 0;
    }

    // 检查3：最大序号接近总分配量（说明最新数据完整）
    if (found > 0 && max_seq < total_seq_assigned * 8 / 10) {
        printf("  FAIL: 最大序号偏低，最新数据可能丢失\n");
        ok = 0;
    }

    return ok ? 0 : -1;
}

// ============================================================================
// 单轮测试
// ============================================================================

static int run_one(int n_rotations) {
    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║  目标轮转: %2d 次  |  MAX_FILES=%d  |  每文件 %d KB  ║\n",
           n_rotations, MAX_DAILY_FILES, FILE_SIZE / 1024);
    printf("╚══════════════════════════════════════════════╝\n");

    // 清空测试目录
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "rm -rf %s && mkdir -p %s/decrypted", TEST_DIR, TEST_DIR);
    system(cmd);

    lz_logger_set_max_file_size(FILE_SIZE);
    atomic_store(&g_seq, 0);

    // 写入量 = (轮转次数 + 1) 个文件，确保触发足够多的切换
    long long total_target = (long long)(n_rotations + 1) * LOGS_PER_FILE;
    long long per_thread   = total_target / NUM_THREADS + 1;

    printf("  每文件约 %d 条 | 目标写入 %lld 条 (%d 线程 × %lld)\n",
           LOGS_PER_FILE, per_thread * NUM_THREADS, NUM_THREADS, per_thread);

    lz_logger_handle_t logger;
    if (lz_logger_open(TEST_DIR, ENCRYPT_KEY, &logger, NULL, NULL) != LZ_LOG_SUCCESS) {
        printf("  FAIL: 打开日志失败\n");
        return -1;
    }

    pthread_t threads[NUM_THREADS];
    targ_t    args[NUM_THREADS];
    long long total_success = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i] = (targ_t){ logger, i, per_thread, 0 };
        pthread_create(&threads[i], NULL, write_thread, &args[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_success += args[i].success;
    }

    uint64_t final_seq = atomic_load(&g_seq);
    lz_logger_flush(logger);
    lz_logger_close(logger);

    printf("  写入完成: %lld 条成功 | 序号分配至 %" PRIu64 "\n",
           total_success, final_seq);

    // 统计并检查文件数
    int file_count = 0;
    DIR *d = opendir(TEST_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)))
            if (strstr(e->d_name, ".log") && !strstr(e->d_name, "export"))
                file_count++;
        closedir(d);
    }
    int count_ok = (file_count <= MAX_DAILY_FILES);
    printf("  文件数: %d / %d  %s\n",
           file_count, MAX_DAILY_FILES, count_ok ? "PASS" : "FAIL (超出上限)");

    // 列出文件
    snprintf(cmd, sizeof(cmd), "ls -lh %s/*.log 2>/dev/null", TEST_DIR);
    system(cmd);

    // 解密
    printf("\n  解密中...\n");
    snprintf(cmd, sizeof(cmd),
             "python3 tools/decrypt_log.py -d %s -p %s -o %s/decrypted 2>&1 | tail -2",
             TEST_DIR, ENCRYPT_KEY, TEST_DIR);
    if (system(cmd) != 0) {
        printf("  FAIL: 解密出错\n");
        return -1;
    }

    // 验证序号
    char dec_dir[256];
    snprintf(dec_dir, sizeof(dec_dir), "%s/decrypted", TEST_DIR);
    int seq_ok = verify_sequences(dec_dir, final_seq, n_rotations);

    return (count_ok && seq_ok == 0) ? 0 : -1;
}

// ============================================================================
// main
// ============================================================================

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║       LZ Logger 循环覆盖轮转验证测试          ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("每文件大小 %d KB，最大文件数 %d，每条日志 %d 字节\n\n",
           FILE_SIZE / 1024, MAX_DAILY_FILES, LOG_BYTES);

    int r6  = run_one(6);
    int r15 = run_one(15);
    int r20 = run_one(20);

    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║                  测试汇总                     ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║   6  次轮转: %s                             ║\n",
           r6  == 0 ? "PASS" : "FAIL");
    printf("║   15 次轮转: %s                             ║\n",
           r15 == 0 ? "PASS" : "FAIL");
    printf("║   20 次轮转: %s                             ║\n",
           r20 == 0 ? "PASS" : "FAIL");
    printf("╚══════════════════════════════════════════════╝\n\n");

    return (r6 == 0 && r15 == 0 && r20 == 0) ? 0 : 1;
}
