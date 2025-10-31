package io.levili.lzlogger_example;

import android.content.Context;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.filters.LargeTest;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;

import io.levili.lzlogger.LzLogger;

/**
 * Android 日志写入性能测试
 * 
 * 运行方式：
 * ./gradlew connectedAndroidTest
 * 
 * 查看结果：
 * 测试报告会生成在 app/build/reports/androidTests/connected/
 */
@RunWith(AndroidJUnit4.class)
@LargeTest
public class LzLoggerPerformanceTest {
    
    private Context context;
    private File testLogDir;
    
    @Before
    public void setUp() {
        context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        testLogDir = new File(context.getCacheDir(), "lz_logger_perf_tests");
        if (testLogDir.exists()) {
            deleteRecursive(testLogDir);
        }
        testLogDir.mkdirs();
    }
    
    @After
    public void tearDown() {
        if (testLogDir != null && testLogDir.exists()) {
            deleteRecursive(testLogDir);
        }
    }
    
    // ============================================================================
    // 无加密性能测试
    // ============================================================================
    
    @Test
    public void testPerformanceLogWriteWithoutEncryption_ShortMessage() {
        // 短消息（~50字节）- 无加密
        measureLogWritePerformance(
            "INFO: User action completed successfully",
            null,
            5000
        );
    }
    
    @Test
    public void testPerformanceLogWriteWithoutEncryption_MediumMessage() {
        // 中等消息（~150字节）- 无加密
        measureLogWritePerformance(
            "INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes",
            null,
            5000
        );
    }
    
    @Test
    public void testPerformanceLogWriteWithoutEncryption_LongMessage() {
        // 长消息（~300字节）- 无加密
        measureLogWritePerformance(
            "ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment",
            null,
            5000
        );
    }
    
    // ============================================================================
    // 有加密性能测试
    // ============================================================================
    
    @Test
    public void testPerformanceLogWriteWithEncryption_ShortMessage() {
        // 短消息（~50字节）- 有加密
        measureLogWritePerformance(
            "INFO: User action completed successfully",
            "laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue",
            5000
        );
    }
    
    @Test
    public void testPerformanceLogWriteWithEncryption_MediumMessage() {
        // 中等消息（~150字节）- 有加密
        measureLogWritePerformance(
            "INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes",
            "laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue",
            5000
        );
    }
    
    @Test
    public void testPerformanceLogWriteWithEncryption_LongMessage() {
        // 长消息（~300字节）- 有加密
        measureLogWritePerformance(
            "ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment",
            "laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue",
            5000
        );
    }
    
    // ============================================================================
    // 极限性能测试
    // ============================================================================
    
    @Test
    public void testPerformanceBurstWriteWithoutEncryption() {
        // 极限性能：连续写入10000条 - 无加密
        measureLogWritePerformance(
            "Benchmark test message",
            null,
            10000
        );
    }
    
    @Test
    public void testPerformanceBurstWriteWithEncryption() {
        // 极限性能：连续写入10000条 - 有加密
        measureLogWritePerformance(
            "Benchmark test message",
            "laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue",
            10000
        );
    }
    
    // ============================================================================
    // 辅助方法
    // ============================================================================
    
    private void measureLogWritePerformance(String message, String encryptKey, int iterations) {
        // 打开 logger
        boolean success = LzLogger.prepareLog(context, "perf_test", encryptKey);
        if (!success) {
            throw new RuntimeException("Failed to prepare logger");
        }
        
        // 预热（500次）
        for (int i = 0; i < 500; i++) {
            LzLogger.i("PERF", message);
        }
        LzLogger.flush();
        
        // 性能测量
        long startTime = System.nanoTime();
        for (int i = 0; i < iterations; i++) {
            LzLogger.i("PERF", message);
        }
        long endTime = System.nanoTime();
        
        // 计算统计数据
        long totalNanos = endTime - startTime;
        double avgMicros = (double) totalNanos / iterations / 1000.0;
        double throughput = 1_000_000_000.0 / ((double) totalNanos / iterations);
        
        // 输出结果（会显示在测试报告中）
        String encryption = encryptKey != null ? "有加密" : "无加密";
        String result = String.format(
            "\n【%s - %d字节】\n" +
            "  迭代次数: %d\n" +
            "  总耗时: %.2f ms\n" +
            "  平均耗时: %.2f μs/条 (%.0f ns/条)\n" +
            "  吞吐量: %.0f 条/秒\n",
            encryption,
            message.length(),
            iterations,
            totalNanos / 1_000_000.0,
            avgMicros,
            (double) totalNanos / iterations,
            throughput
        );
        
        android.util.Log.i("LzLoggerPerf", result);
        
        // 关闭 logger
        LzLogger.close();
        
        // 清理测试目录
        deleteRecursive(testLogDir);
        testLogDir.mkdirs();
    }
    
    private void deleteRecursive(File file) {
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
}
