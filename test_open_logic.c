#include "src/lz_logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>

// ============================================================================
// 配置
// ============================================================================

#define TEST_DIR     "/tmp/lz_open_logic_test"
#define FILE_SIZE    (1 * 1024 * 1024)   // 1MB（最小合法值）
#define MAX_FILES    5
#define FOOTER_SIZE  28
#define SALT_SIZE    16
#define MAGIC        0x456E6478u

// 测试消息，长度固定为 16 字节，便于断言
#define TEST_MSG     "OPEN_LOGIC_TEST\n"
#define TEST_MSG_LEN 16

static char g_date[16];

// ============================================================================
// 工具函数
// ============================================================================

static void get_today(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(g_date, sizeof(g_date), "%Y-%m-%d", &tm);
}

static void build_path(char *out, size_t sz, int num)
{
    snprintf(out, sz, "%s/%s-%d.log", TEST_DIR, g_date, num);
}

/** 创建一个带有合法 footer 的日志文件，used_size 可指定 */
static int create_log_file(int num, uint32_t used_size)
{
    char path[512];
    build_path(path, sizeof(path), num);

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    if (ftruncate(fd, FILE_SIZE) != 0) { close(fd); return -1; }

    // 写入 footer: [salt 16B][magic 4B][file_size 4B][used_size 4B]
    lseek(fd, FILE_SIZE - FOOTER_SIZE, SEEK_SET);
    uint8_t salt[SALT_SIZE] = {0};
    write(fd, salt, SALT_SIZE);
    uint32_t magic = MAGIC;
    write(fd, &magic, sizeof(magic));
    uint32_t fs = FILE_SIZE;
    write(fd, &fs, sizeof(fs));
    write(fd, &used_size, sizeof(used_size));
    fsync(fd);
    close(fd);
    return 0;
}

/** 读取 footer 中的 used_size */
static uint32_t get_used_size(int num)
{
    char path[512];
    build_path(path, sizeof(path), num);
    FILE *f = fopen(path, "rb");
    if (!f) return (uint32_t)-1;
    fseek(f, FILE_SIZE - 4, SEEK_SET);
    uint32_t used = 0;
    fread(&used, 4, 1, f);
    fclose(f);
    return used;
}

/** 检查文件是否存在 */
static int file_exists(int num)
{
    char path[512];
    build_path(path, sizeof(path), num);
    struct stat st;
    return stat(path, &st) == 0;
}

/** 将 num 号文件的 mtime 设为 now+offset_secs */
static void set_mtime(int num, int offset_secs)
{
    char path[512];
    build_path(path, sizeof(path), num);
    time_t t = time(NULL) + offset_secs;
    struct timeval tv[2] = {{t, 0}, {t, 0}};
    utimes(path, tv);
}

static void reset_dir(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", TEST_DIR, TEST_DIR);
    system(cmd);
}

// ============================================================================
// 单个用例
// ============================================================================

/**
 * 建立初始状态：所有 5 个文件都存在，used_size = half_full，
 * 然后将 latest_num 设为 mtime 最新，并可选设为满。
 * 打开 logger，写入 TEST_MSG，关闭，返回 0=PASS / -1=FAIL。
 *
 * expected_write_num: 预期写入的文件编号
 * new_file_expected:  1 表示预期 expected_write_num 是新建的（从 0 起）
 */
static int run_case(const char *name,
                    int latest_num,
                    int make_full,
                    int expected_write_num,
                    int new_file_expected)
{
    printf("\n  [%s]\n", name);
    printf("    latest=%d  full=%s  expect_write=file-%d  new=%s\n",
           latest_num, make_full ? "YES" : "NO",
           expected_write_num, new_file_expected ? "YES" : "NO");

    reset_dir();

    uint32_t half = (FILE_SIZE - FOOTER_SIZE) / 2;
    uint32_t full = FILE_SIZE - FOOTER_SIZE;

    // 创建 5 个文件，mtime 从旧到新间隔 10 秒
    for (int i = 0; i < MAX_FILES; i++)
    {
        uint32_t used = (i == latest_num && make_full) ? full : half;
        if (create_log_file(i, used) != 0)
        {
            printf("    FAIL: 创建文件 %d 失败\n", i);
            return -1;
        }
        set_mtime(i, -(MAX_FILES - i) * 10);  // 越小越旧
    }
    // latest_num 获得最新 mtime
    set_mtime(latest_num, 0);

    // 记录写前状态
    uint32_t before[MAX_FILES];
    for (int i = 0; i < MAX_FILES; i++)
        before[i] = get_used_size(i);

    // 打开 logger，写入，关闭
    lz_logger_set_max_file_size(FILE_SIZE);
    lz_logger_handle_t h = NULL;
    if (lz_logger_open(TEST_DIR, NULL, &h, NULL, NULL) != LZ_LOG_SUCCESS)
    {
        printf("    FAIL: lz_logger_open 失败\n");
        return -1;
    }
    lz_logger_write(h, TEST_MSG, TEST_MSG_LEN);
    lz_logger_flush(h);
    lz_logger_close(h);

    // 判断结果：找到 used_size 发生变化的文件
    int written = -1;
    if (new_file_expected)
    {
        // 新建文件：used_size 从 0 变为 TEST_MSG_LEN
        for (int i = 0; i < MAX_FILES; i++)
        {
            uint32_t after = get_used_size(i);
            if (before[i] != (uint32_t)TEST_MSG_LEN && after == (uint32_t)TEST_MSG_LEN)
            {
                written = i;
                break;
            }
        }
        // 也可能是：文件先被 unlink 再创建，before 读不到（返回 -1），after 为 TEST_MSG_LEN
        if (written == -1 && get_used_size(expected_write_num) == (uint32_t)TEST_MSG_LEN)
            written = expected_write_num;
    }
    else
    {
        // 续写已有文件：used_size 增加了 TEST_MSG_LEN
        for (int i = 0; i < MAX_FILES; i++)
        {
            uint32_t after = get_used_size(i);
            if (before[i] != (uint32_t)-1 && after == before[i] + TEST_MSG_LEN)
            {
                written = i;
                break;
            }
        }
    }

    if (written == expected_write_num)
    {
        printf("    PASS: 正确写入 file-%d\n", written);
        return 0;
    }
    else
    {
        printf("    FAIL: 写入了 file-%d，预期 file-%d\n", written, expected_write_num);
        // 打印各文件状态帮助诊断
        for (int i = 0; i < MAX_FILES; i++)
            printf("      file-%d: before=%u after=%u exists=%d\n",
                   i, before[i], get_used_size(i), file_exists(i));
        return -1;
    }
}

// ============================================================================
// 无文件场景：今天完全没有日志，期望从 file-0 开始
// ============================================================================

static int case_no_files(void)
{
    printf("\n  [Case 0: 无任何日志文件 → 创建 file-0]\n");
    reset_dir();

    lz_logger_set_max_file_size(FILE_SIZE);
    lz_logger_handle_t h = NULL;
    if (lz_logger_open(TEST_DIR, NULL, &h, NULL, NULL) != LZ_LOG_SUCCESS)
    {
        printf("    FAIL: open 失败\n");
        return -1;
    }
    lz_logger_write(h, TEST_MSG, TEST_MSG_LEN);
    lz_logger_flush(h);
    lz_logger_close(h);

    uint32_t used = get_used_size(0);
    int ok = (used == (uint32_t)TEST_MSG_LEN);
    printf("    %s: file-0 used_size=%u (预期 %d)\n",
           ok ? "PASS" : "FAIL", used, TEST_MSG_LEN);
    return ok ? 0 : -1;
}

// ============================================================================
// main
// ============================================================================

int main(void)
{
    get_today();
    lz_logger_set_max_file_size(FILE_SIZE);

    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║         LZ Logger Open 逻辑验证测试                   ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("文件大小: %d KB  最大文件数: %d  日期: %s\n\n",
           FILE_SIZE / 1024, MAX_FILES, g_date);

    int r[7];

    // Case 0: 无文件 → 创建 file-0
    r[0] = case_no_files();

    // Case 1: file-2 mtime 最新（未满）→ 续写 file-2
    r[1] = run_case("Case 1: file-2 最新(未满) → 续写 file-2",
                    2, 0, 2, 0);

    // Case 2: file-0 mtime 最新（wrap 后场景，未满）→ 续写 file-0
    r[2] = run_case("Case 2: file-0 最新(wrap后,未满) → 续写 file-0",
                    0, 0, 0, 0);

    // Case 3: file-3 mtime 最新（未满）→ 续写 file-3
    r[3] = run_case("Case 3: file-3 最新(未满) → 续写 file-3",
                    3, 0, 3, 0);

    // Case 4: file-1 mtime 最新（未满）→ 续写 file-1
    r[4] = run_case("Case 4: file-1 最新(未满) → 续写 file-1",
                    1, 0, 1, 0);

    // Case 5: file-4 最新且满 → 下一槽 (4+1)%5=0 → 删旧 file-0 重建
    r[5] = run_case("Case 5: file-4 最新且满 → 删除并重建 file-0",
                    4, 1, 0, 1);

    // Case 6: file-2 最新且满 → 下一槽 (2+1)%5=3 → 删旧 file-3 重建
    r[6] = run_case("Case 6: file-2 最新且满 → 删除并重建 file-3",
                    2, 1, 3, 1);

    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║                      测试汇总                         ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");

    const char *names[] = {
        "无文件 → 创建 file-0           ",
        "file-2 最新(未满) → 续写 file-2",
        "file-0 最新(未满) → 续写 file-0",
        "file-3 最新(未满) → 续写 file-3",
        "file-1 最新(未满) → 续写 file-1",
        "file-4 最新且满  → 重建 file-0 ",
        "file-2 最新且满  → 重建 file-3 ",
    };

    int all_pass = 1;
    for (int i = 0; i < 7; i++)
    {
        printf("║  Case %d  %s  %s  ║\n",
               i, names[i], r[i] == 0 ? "PASS" : "FAIL");
        if (r[i] != 0) all_pass = 0;
    }
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    return all_pass ? 0 : 1;
}
