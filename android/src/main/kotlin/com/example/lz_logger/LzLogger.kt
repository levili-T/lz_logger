package com.example.lz_logger

object LzLogger {
    init {
        System.loadLibrary("lz_logger")
    }

    @JvmStatic
    fun log(level: Int, tag: String, file: String, line: Int, message: String) {
        nativeLog(level, tag, file, line, message)
    }

    @JvmStatic
    private external fun nativeLog(
        level: Int,
        tag: String,
        file: String,
        line: Int,
        message: String
    )
}
