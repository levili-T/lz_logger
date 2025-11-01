package com.example.lz_logger_example

import android.os.Bundle
import io.flutter.embedding.android.FlutterActivity
import io.levili.lzlogger.LzLogger

class MainActivity : FlutterActivity() {
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
}
