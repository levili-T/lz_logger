package io.levili.lzlogger;

import android.content.Context;
import android.util.Log;
import java.io.File;

/**
 * 日志写入性能测试工具
 */
public class PerformanceTest {
    private static final String TAG = "LzLoggerPerf";

    /**
     * 测试日志写入性能（完整流程：格式化 + 加密 + mmap写入）
     */
    public static void testLogWritePerformance(Context context) {
        Log.i(TAG, "\n========================================");
        Log.i(TAG, "     Android 日志写入性能测试报告");
        Log.i(TAG, "========================================\n");
        
        // 创建临时日志目录
        File tempDir = new File(context.getCacheDir(), "perf_test_logs");
        if (tempDir.exists()) {
            deleteRecursive(tempDir);
        }
        tempDir.mkdirs();
        
        String logDir = tempDir.getAbsolutePath();
        String encryptKey = "laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue";
        
        // 测试无加密的情况
        Log.i(TAG, "【测试场景 1：无加密】");
        testLogWriteWithEncryption(logDir, null, false);
        
        // 清理并测试有加密的情况
        deleteRecursive(tempDir);
        tempDir.mkdirs();
        Log.i(TAG, "\n【测试场景 2：有加密（AES-256-CTR）】");
        testLogWriteWithEncryption(logDir, encryptKey, true);
        
        // 清理
        deleteRecursive(tempDir);
        
        Log.i(TAG, "\n========================================");
        Log.i(TAG, "            测试完成");
        Log.i(TAG, "========================================");
    }

    private static void testLogWriteWithEncryption(String logDir, String encryptKey, boolean encrypted) {
        int[] outErrors = new int[2];
        long handle = LzLogger.nativeOpen(logDir, encryptKey, outErrors);
        
        if (handle == 0) {
            Log.e(TAG, String.format("❌ 打开日志失败: inner=%d, errno=%d", 
                    outErrors[0], outErrors[1]));
            return;
        }
        
        // 测试不同大小的日志消息
        String[] messageLabels = {
            "短消息 (~50 字节)",
            "中等消息 (~150 字节)",
            "长消息 (~300 字节)"
        };
        
        String[] messages = {
            "INFO: User action completed successfully",
            "INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes",
            "ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows..."
        };
        
        int iterations = 5000;  // 测试 5000 次取平均值
        
        Log.i(TAG, "测试参数：");
        Log.i(TAG, "  - 迭代次数: " + iterations);
        Log.i(TAG, "  - 预热次数: 500");
        Log.i(TAG, "");
        
        for (int i = 0; i < messages.length; i++) {
            String message = messages[i];
            
            // 预热 JVM 和文件系统缓存
            for (int j = 0; j < 500; j++) {
                LzLogger.nativeLog(handle, 2, "PERF", "testMethod", "Test.java", 42, message);
            }
            LzLogger.nativeFlush(handle);
            
            // 正式测试
            long startTime = System.nanoTime();
            for (int j = 0; j < iterations; j++) {
                LzLogger.nativeLog(handle, 2, "PERF", "testMethod", "Test.java", 42, message);
            }
            long endTime = System.nanoTime();
            
            long totalNs = endTime - startTime;
            double avgNs = (double) totalNs / iterations;
            double avgUs = avgNs / 1000.0;
            double throughput = 1_000_000_000.0 / avgNs;
            
            Log.i(TAG, messageLabels[i] + ":");
            Log.i(TAG, String.format("  ├─ 平均耗时: %.2f μs/条 (%.0f ns/条)", avgUs, avgNs));
            Log.i(TAG, String.format("  ├─ 吞吐量: %.0f 条/秒", throughput));
            Log.i(TAG, String.format("  └─ 实际大小: %d 字节", message.length()));
            Log.i(TAG, "");
        }
        
        // 测试极限性能（连续写入）
        Log.i(TAG, "【极限性能测试】");
        String testMsg = "Benchmark test message";
        int burstIterations = 10000;
        
        for (int j = 0; j < 1000; j++) {
            LzLogger.nativeLog(handle, 2, "PERF", "benchmark", "Test.java", 1, testMsg);
        }
        
        long burstStart = System.nanoTime();
        for (int j = 0; j < burstIterations; j++) {
            LzLogger.nativeLog(handle, 2, "PERF", "benchmark", "Test.java", 1, testMsg);
        }
        long burstEnd = System.nanoTime();
        
        long burstTotalNs = burstEnd - burstStart;
        double burstAvgUs = (double) burstTotalNs / burstIterations / 1000.0;
        double burstThroughput = 1_000_000_000.0 / ((double) burstTotalNs / burstIterations);
        
        Log.i(TAG, String.format("  ├─ 连续写入 %d 条", burstIterations));
        Log.i(TAG, String.format("  ├─ 平均耗时: %.2f μs/条", burstAvgUs));
        Log.i(TAG, String.format("  └─ 峰值吞吐: %.0f 条/秒", burstThroughput));
        
        LzLogger.nativeClose(handle);
    }

    private static void deleteRecursive(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        file.delete();
    }

    /**
     * 运行日志写入性能测试并生成报告
     */
    public static void runAllTests(Context context) {
        testLogWritePerformance(context);
    }
}
