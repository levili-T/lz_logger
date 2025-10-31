cmake_minimum_required(VERSION 3.10.2)

project(lz_logger)

# C11 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

# 获取项目根目录 (从 CMakeLists.txt 向上 4 级)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

# 添加 C 核心源文件
add_library(
    lz_logger
    SHARED
    # JNI 层
    ${CMAKE_CURRENT_SOURCE_DIR}/lz_logger_jni.cpp
    # C 核心层
    ${PROJECT_ROOT}/src/lz_logger.c
    ${PROJECT_ROOT}/src/lz_crypto.c
)

# 包含头文件目录
target_include_directories(
    lz_logger
    PRIVATE
    ${PROJECT_ROOT}/src
)

# 链接 Android 日志库和 OpenSSL
find_library(log-lib log)
find_library(crypto-lib crypto)
target_link_libraries(lz_logger ${log-lib} ${crypto-lib})

# 编译选项
target_compile_options(lz_logger PRIVATE -Wall -Wextra)
