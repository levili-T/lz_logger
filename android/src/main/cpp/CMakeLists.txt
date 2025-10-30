cmake_minimum_required(VERSION 3.10.2)

project(lz_logger)

# C11 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

# 添加 C 核心源文件
add_library(
    lz_logger
    SHARED
    # JNI 层
    lz_logger_jni.cpp
    # C 核心层
    ../../../src/lz_logger.c
)

# 包含头文件目录
target_include_directories(
    lz_logger
    PRIVATE
    ../../../src
)

# 链接 Android 日志库
find_library(log-lib log)
target_link_libraries(lz_logger ${log-lib})

# 编译选项
target_compile_options(lz_logger PRIVATE -Wall -Wextra)
