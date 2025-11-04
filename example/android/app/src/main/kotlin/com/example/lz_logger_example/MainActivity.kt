package com.example.lz_logger_example

import android.os.Bundle
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import io.levili.lzlogger.LzLogger
import kotlinx.coroutines.*

class MainActivity : FlutterActivity() {
    private val CHANNEL = "lz_logger_performance_test"
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // 初始化日志系统
        LzLogger.setLogLevel(LzLogger.DEBUG)
        val success = LzLogger.prepareLog(applicationContext, "laozhaozhao", "laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue")
        if (success) {
            LzLogger.log(
                LzLogger.INFO,
                "MainActivity",
                "Logger initialized successfully",
                "onCreate",
                "MainActivity.kt",
                15
            )
        } else {
            android.util.Log.e("MainActivity", "Failed to initialize logger")
        }
    }
    
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            if (call.method == "runPerformanceTests") {
                // 在后台线程运行性能测试
                CoroutineScope(Dispatchers.Default).launch {
                    val summary = runPerformanceTests()
                    withContext(Dispatchers.Main) {
                        result.success(summary)
                    }
                }
            } else {
                result.notImplemented()
            }
        }
    }
    
    private fun runPerformanceTests(): String {
        println("\n========================================")
        println("Android Performance Tests Starting")
        println("========================================\n")
        
        val summary = StringBuilder()
        val testMessages = listOf(
            "Short" to "INFO: User action completed successfully",
            "Medium" to "INFO: Network request to api.example.com/v1/users completed in 250ms with status code 200 and response size 1024 bytes",
            "Long" to "ERROR: Database connection failed after 3 retry attempts. Connection timeout occurred while trying to connect to mysql://db.example.com:3306/production. Last error: SQLSTATE[HY000] [2002] Connection timed out. Stack trace follows for debugging purposes and error tracking in production environment",
            "Burst" to "Benchmark test message"
        )
        
        val iterations = listOf(5000, 5000, 5000, 10000)
        
        // 无加密测试
        println("Running tests WITHOUT encryption...")
        LzLogger.close()
        LzLogger.prepareLog(applicationContext, "perf_test", null)
        
        testMessages.forEachIndexed { index, (name, message) ->
            val result = measureLogPerformance("$name (no encrypt)", message, iterations[index])
            summary.append(result).append("\n")
        }
        
        // 有加密测试
        println("\nRunning tests WITH encryption...")
        LzLogger.close()
        LzLogger.prepareLog(applicationContext, "perf_test_encrypted", "laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue")
        
        testMessages.forEachIndexed { index, (name, message) ->
            val result = measureLogPerformance("$name (encrypt)", message, iterations[index])
            summary.append(result).append("\n")
        }
        
        // 恢复原始配置
        LzLogger.close()
        LzLogger.prepareLog(applicationContext, "laozhaozhao", "laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue")
        
        println("\n========================================")
        println("Android Performance Tests Completed")
        println("========================================\n")
        
        return summary.toString()
    }
    
    private fun measureLogPerformance(testName: String, message: String, iterations: Int): String {
        // 预热
        repeat(1000) {
            LzLogger.log(LzLogger.INFO, "PERF", message, "", "perf", 0)
        }
        LzLogger.flush()
        
        // 测试
        val startTime = System.nanoTime()
        repeat(iterations) {
            LzLogger.log(LzLogger.INFO, "PERF", message, "", "perf", 0)
        }
        LzLogger.flush()
        val endTime = System.nanoTime()
        
        val durationMs = (endTime - startTime) / 1_000_000.0
        val throughput = iterations / durationMs * 1000
        val avgLatency = durationMs / iterations
        
        val resultStr = String.format("%s: %.2f logs/sec, %.3f ms/log", testName, throughput, avgLatency)
        println(resultStr)
        
        return resultStr
    }
}
