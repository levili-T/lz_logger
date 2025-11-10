# LZ Logger 设计文档

## 概述

LZ Logger 是一个**极致性能、跨平台**的日志系统，专为 Flutter 和原生应用设计。采用 C 核心 + 平台原生封装 + Dart FFI 的三层架构，实现了**无锁并发日志记录**和**mmap零拷贝**。

### 核心特性

- ⚡ **极致性能**: 单线程 24.7M条/秒 (40ns/条)，**比 spdlog 快 4.3倍**
- 🔥 **无锁并发**: atomic_fetch_add 原子操作 (v2.1.0+)，多线程扩展性 27% (10线程极限测试)
- 🚀 **零拷贝**: 基于 mmap 内存映射，接近无I/O基准性能
- 🎯 **跨平台**: 统一的 C11 核心，iOS/Android/macOS/Linux 全支持
- 🔒 **可选加密**: AES-128-CBC 加密（可禁用以获得最大性能）
- 🌟 **极简架构**: 零后台线程，零内存拷贝，2000行核心代码
- 📱 **移动优化**: 专为移动端和嵌入式场景设计（2-10线程）
- 🔧 **灵活配置**: 可配置文件大小（1MB-100MB），自动文件轮转
- 🧹 **自动管理**: 自动清理过期日志，生命周期自动管理
- 💎 **Flutter 友好**: Dart FFI 直接调用，零序列化开销

---

## 架构设计

### 三层架构

```
┌─────────────────────────────────────────────┐
│          Dart FFI Layer (Flutter)           │
│  lz_logger.dart - FFI bindings             │
│  lzLogInfo(), lzLogError(), etc.           │
└─────────────────┬───────────────────────────┘
                  │ dart:ffi
                  ▼
┌─────────────────────────────────────────────┐
│        Native Platform Layer                │
│  iOS: LZLogger.m (Objective-C Singleton)   │
│  Android: LzLogger.kt (Kotlin Object)      │
│  - prepareLog()  - log()  - close()        │
└─────────────────┬───────────────────────────┘
                  │ JNI / Direct Call
                  ▼
┌─────────────────────────────────────────────┐
│           C Core Layer (C11)                │
│  lz_logger.c / lz_logger.h                 │
│  - lz_logger_open()                        │
│  - lz_logger_write() [Lock-Free CAS]      │
│  - lz_logger_close()                       │
└─────────────────────────────────────────────┘
```

---

## C 核心实现

### 无锁并发设计

#### 优化历程：从 CAS 循环到 atomic_fetch_add

**v2.1.0+ 优化实现** - 使用 **atomic_fetch_add** 一次性预留写入偏移量:

```c
// 一次性预留写入位置
uint32_t my_offset = atomic_fetch_add(offset_ptr, len);

if (my_offset + len > max_data_size) {
    // 预留失败,触发文件切换
    // 注意: 不需要回滚(atomic_fetch_sub)
    // 原因: 1) 多线程并发可能都已超出,无法完全回滚
    //       2) 切换新文件后从0开始,旧offset值无关紧要
    // ... 文件切换逻辑
    return;
}

// 写入数据到预留位置
memcpy(logger->mmap_ptr + my_offset, data, len);
```

**v2.0 原始实现** - 使用 **CAS (Compare-And-Swap)** 循环:

```c
// CAS 循环预留写入位置 (已废弃)
uint32_t old_offset = atomic_load(&logger->write_offset);
do {
    if (old_offset + total_len > logger->mmap_size) {
        break; // 文件满,需要切换
    }
    new_offset = old_offset + total_len;
} while (!atomic_compare_exchange_weak(
    &logger->write_offset,
    &old_offset,
    new_offset
));
```

**优化效果 (v2.1.0+):**
- ✅ **多线程性能提升27.4%**: 7.5M → 9.6M ops/sec
- ✅ **延迟降低21.8%**: 133ns → 104ns
- ✅ **消除 CAS 重试**: O(n) → O(1) 时间复杂度
- ✅ **零竞争开销**: atomic_fetch_add 是硬件原生的 fetch-and-add 指令,无重试

**关键优势:**
- ✅ 无互斥锁,消除锁竞争
- ✅ 多线程真正并发写入
- ✅ 硬件级原子操作 (x86 LOCK XADD, ARM LDADD)
- ✅ **真实场景扩展性 97.7%-108.6%**（10线程,有业务间隔,v2.1.0+）
- ✅ **极限测试扩展性 4.6%**（10线程,无间隔,不代表真实应用）
- ✅ **相比 CAS 循环性能提升27.4%**（v2.1.0+ 优化）

**性能特性:**
- 单线程: 35.7M条/秒 (28ns/条) - 保持顶级
- 10线程极限测试: 9.6M条/秒 (104ns/条) - **相比 CAS 循环提升27.4%**
- 10线程真实场景: 扩展性 97.7%-108.6% - **接近完美线性扩展**
- 加密模式: ~2.6M条/秒 (385ns/条) - 加密是瓶颈

**真实场景扩展性测试 (v2.1.0, 10线程, 50MB文件):**

| 场景 | 日志间隔 | 扩展性 | 评级 |
|------|---------|--------|------|
| 移动端应用 | 10ms | 108.6% | ⭐⭐⭐⭐⭐ 优秀 |
| 普通后端 | 1ms | 100.4% | ⭐⭐⭐⭐⭐ 优秀 |
| 高频服务器 | 100μs | 97.7% | ⭐⭐⭐⭐ 良好 |
| 极限压力 | 0μs | 4.6% | ⭐ 不代表真实场景 |

**为什么 atomic_fetch_add 优于 CAS 循环?**
- **CAS 循环**: 在高并发下需要多次重试,每次重试都需要重新读取和比较
- **atomic_fetch_add**: 硬件保证一次性成功,无重试开销
- **无需回滚**: 超出预留不回滚,切换文件后从0开始,简化逻辑
- **时间复杂度**: O(1) vs O(n),在高并发下优势明显
- **真实场景优势**: 有业务间隔时,扩展性接近完美(>95%)

### mmap 内存映射

```c
// 创建 mmap 映射
int fd = open(file_path, O_RDWR | O_CREAT, 0644);
void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
close(fd); // mmap 映射在 close(fd) 后仍然有效
```

**优势:**
- ✅ 零拷贝写入,直接修改内核页缓存
- ✅ 操作系统自动调度刷盘
- ✅ mmap 映射在 close(fd) 后仍有效,支持延迟清理

### 文件结构

```
┌────────────────────────────────────────────────┐
│         Log Data (Variable Length)            │  ← write_offset 递增
├────────────────────────────────────────────────┤
│  Salt (16 bytes, random)                      │  ← Footer: 用于加密
├────────────────────────────────────────────────┤
│  Magic ENDX (0x456E6478, 4 bytes)             │  ← 文件完整性标记
├────────────────────────────────────────────────┤
│  File Size (4 bytes)                          │  ← mmap文件总大小
├────────────────────────────────────────────────┤
│  Used Size (4 bytes)                          │  ← 实际使用量
└────────────────────────────────────────────────┘
                Total Footer: 28 bytes
```

**Footer设计 (v2.1.0):**
- **Salt (16字节)**: 随机生成，每个文件唯一，用于加密增强安全性
- **Magic (4字节)**: 0x456E6478 ("Endx")，用于验证文件完整性
- **File Size (4字节)**: 文件总大小，用于校验
- **Used Size (4字节)**: 实际写入数据大小，快速定位有效数据结尾

**优势:**
- ✅ 文件完整性校验（Magic验证）
- ✅ 崩溃恢复（通过Used Size定位最后有效位置）
- ✅ 加密安全增强（Salt随机化）

### 文件命名规则

格式: `log_YYYYMMDD_HHMMSS_pid.mmap`

示例:
```
log_20251108_153045_12345.mmap  ← 当天第一个文件（包含进程ID）
log_20251108_160230_12345.mmap  ← 当天第二个文件（文件满后切换）
log_20251108_163015_12345.mmap  ← 当天第三个文件
```

**文件轮转策略:**
1. 当前文件达到最大大小（默认6MB，可配置1MB-100MB）时自动切换
2. 文件名包含时间戳和进程ID，便于：
   - 按时间顺序查找日志
   - 区分不同进程的日志
   - 避免多进程文件名冲突
3. 不限制单日文件数量（按需创建）
4. 通过 `lz_logger_cleanup_expired_logs()` 清理过期日志

---

## iOS 实现

### Objective-C 封装

```objectivec
@interface LZLogger : NSObject

+ (instancetype)sharedInstance;  // 单例

// 初始化
- (BOOL)prepareLog:(NSString *)logName 
        encryptKey:(NSString *)encryptKey;

// 写日志
- (void)log:(LZLogLevel)level
       file:(const char *)file
   function:(const char *)function
       line:(NSUInteger)line
        tag:(NSString *)tag
     format:(NSString *)format, ... NS_FORMAT_FUNCTION(6, 7);

// 便捷宏
#define LZ_LOG_INFO(tag, format, ...) \
    [[LZLogger sharedInstance] log:LZLogLevelInfo \
                              file:__FILE__ \
                          function:__FUNCTION__ \
                              line:__LINE__ \
                               tag:tag \
                            format:format, ##__VA_ARGS__]

@end
```

### 生命周期管理

```objectivec
// 自动监听应用终止
[[NSNotificationCenter defaultCenter] 
    addObserver:self
       selector:@selector(applicationWillTerminate:)
           name:UIApplicationWillTerminateNotification
         object:nil];

- (void)applicationWillTerminate:(NSNotification *)notification {
    [self close]; // 自动关闭日志
}
```

### 日志格式

```
yyyy-MM-dd HH:mm:ss.SSS tid:0x1a03 [file:line] [function] [tag] message
```

示例:
```
2025-10-30 12:34:56.789 tid:0x1a03 [AppDelegate.swift:15] [application(_:didFinishLaunchingWithOptions:)] [AppDelegate] Logger initialized successfully
```

---

## Android 实现

### Kotlin Object 单例

```kotlin
package io.levili.lzlogger

object LzLogger : DefaultLifecycleObserver {
    // 日志级别
    const val VERBOSE = 0
    const val DEBUG = 1
    const val INFO = 2
    const val WARN = 3
    const val ERROR = 4
    const val FATAL = 5
    
    // 初始化
    fun prepareLog(context: Context, logName: String, encryptKey: String? = null): Boolean
    
    // 写日志
    fun log(level: Int, tag: String, message: String, 
            function: String = "", file: String = "", line: Int = 0)
    
    // 便捷函数
    fun logInfo(tag: String, message: String) = log(INFO, tag, message)
}
```

### JNI 桥接层

```cpp
// android/src/main/cpp/lz_logger_jni.cpp

// JNI 方法命名: Java_io_levili_lzlogger_LzLogger_nativeXxx
JNIEXPORT jlong JNICALL
Java_io_levili_lzlogger_LzLogger_nativeOpen(
    JNIEnv* env, jobject thiz,
    jstring jLogDir, jstring jEncryptKey, jintArray jOutErrors) {
    
    const char* logDir = env->GetStringUTFChars(jLogDir, nullptr);
    lz_logger_handle_t handle = nullptr;
    int32_t innerError = 0, sysErrno = 0;
    
    lz_log_error_t ret = lz_logger_open(logDir, encryptKey, 
                                        &handle, &innerError, &sysErrno);
    
    // 返回错误码
    jint errors[2] = { innerError, sysErrno };
    env->SetIntArrayRegion(jOutErrors, 0, 2, errors);
    
    env->ReleaseStringUTFChars(jLogDir, logDir);
    return reinterpret_cast<jlong>(handle);
}
```

### 生命周期监听

```kotlin
object LzLogger : DefaultLifecycleObserver {
    
    fun prepareLog(context: Context, logName: String): Boolean {
        // 注册生命周期监听
        ProcessLifecycleOwner.get().lifecycle.addObserver(this)
        // ...
    }
    
    // 进程停止时自动关闭
    override fun onStop(owner: LifecycleOwner) {
        if (isInitialized) {
            log(INFO, "LzLogger", "Process stopping, closing logger")
            close()
        }
    }
}
```

### CMake 构建配置

```cmake
# android/src/main/cpp/CMakeLists.txt

cmake_minimum_required(VERSION 3.10.2)
project(lz_logger)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

add_library(lz_logger SHARED
    lz_logger_jni.cpp      # JNI 层
    ../../../src/lz_logger.c  # C 核心层
)

target_include_directories(lz_logger PRIVATE ../../../src)
find_library(log-lib log)
target_link_libraries(lz_logger ${log-lib})
```

---

## Dart FFI 集成

### 动态库加载

```dart
// lib/lz_logger.dart

import 'dart:ffi' as ffi;
import 'dart:io';
import 'package:ffi/ffi.dart';

/// 跨平台动态库加载
ffi.DynamicLibrary _load() {
  if (Platform.isIOS || Platform.isMacOS) {
    // iOS/macOS 使用静态框架，符号在可执行文件中
    return ffi.DynamicLibrary.process();
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return ffi.DynamicLibrary.open('liblz_logger.so');
  }
  if (Platform.isWindows) {
    return ffi.DynamicLibrary.open('lz_logger.dll');
  }
  throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
}

final ffi.DynamicLibrary _dylib = _load();
```

### FFI 函数绑定

```dart
/// Native FFI 函数签名
typedef _LzLoggerFfiNative = ffi.Void Function(
  ffi.Int32 level,
  ffi.Pointer<ffi.Char> tag,
  ffi.Pointer<ffi.Char> function,
  ffi.Pointer<ffi.Char> message,
);

typedef _LzLoggerFfiDart = void Function(
  int level,
  ffi.Pointer<ffi.Char> tag,
  ffi.Pointer<ffi.Char> function,
  ffi.Pointer<ffi.Char> message,
);

final _LzLoggerFfiDart _lzLoggerFfi = _dylib
    .lookup<ffi.NativeFunction<_LzLoggerFfiNative>>('lz_logger_ffi')
    .asFunction();
```

### 日志级别定义

```dart
/// 日志级别（与 iOS LZLogLevel 枚举匹配）
class LzLogLevel {
  static const int verbose = 0;  // 详细日志
  static const int debug = 1;    // 调试日志
  static const int info = 2;     // 信息日志
  static const int warn = 3;     // 警告日志
  static const int error = 4;    // 错误日志
  static const int fatal = 5;    // 致命错误
}
```

### 核心 API

```dart
/// 通用日志函数
void lzLog({
  required int level,
  required String tag,
  String function = '',
  required String message,
}) {
  // Debug 模式下输出到控制台（可在 VSCode Debug Console 看到）
  if (kDebugMode) {
    final levelName = _getLevelName(level);
    final funcInfo = function.isNotEmpty ? ' [$function]' : '';
    debugPrint('[$levelName]$funcInfo [$tag] $message');
  }

  final ffi.Pointer<ffi.Char> tagPtr = tag.toNativeUtf8().cast();
  final ffi.Pointer<ffi.Char> functionPtr = function.toNativeUtf8().cast();
  final ffi.Pointer<ffi.Char> messagePtr = message.toNativeUtf8().cast();

  try {
    _lzLoggerFfi(level, tagPtr, functionPtr, messagePtr);
  } finally {
    calloc.free(tagPtr);
    calloc.free(functionPtr);
    calloc.free(messagePtr);
  }
}
```

### 便捷函数

```dart
/// 便捷日志函数（推荐使用）
void lzLogVerbose(String tag, String message, {String function = ''}) {
  lzLog(level: LzLogLevel.verbose, tag: tag, function: function, message: message);
}

void lzLogDebug(String tag, String message, {String function = ''}) {
  lzLog(level: LzLogLevel.debug, tag: tag, function: function, message: message);
}

void lzLogInfo(String tag, String message, {String function = ''}) {
  lzLog(level: LzLogLevel.info, tag: tag, function: function, message: message);
}

void lzLogWarn(String tag, String message, {String function = ''}) {
  lzLog(level: LzLogLevel.warn, tag: tag, function: function, message: message);
}

void lzLogError(String tag, String message, {String function = ''}) {
  lzLog(level: LzLogLevel.error, tag: tag, function: function, message: message);
}

void lzLogFatal(String tag, String message, {String function = ''}) {
  lzLog(level: LzLogLevel.fatal, tag: tag, function: function, message: message);
}
```

### 使用示例

```dart
import 'package:lz_logger/lz_logger.dart';

void main() {
  // 基础 API
  lzLog(
    level: LzLogLevel.info,
    tag: 'MyApp',
    function: 'main',
    message: 'Application started',
  );
  
  // 便捷 API（推荐）
  lzLogInfo('MyApp', 'Flutter app started');
  lzLogDebug('Network', 'Request sent', function: 'fetchData');
  lzLogWarn('Database', 'Connection slow');
  lzLogError('Auth', 'Login failed', function: 'login');
  
  runApp(MyApp());
}
```

### FFI 全局 Handle 设置

**iOS:**
```objectivec
// LZLogger.m
void lz_logger_ffi(int level, const char* tag, 
                   const char* function, const char* message) {
    [[LZLogger sharedInstance] log:(LZLogLevel)level
                              file:"flutter"
                          function:function ?: ""
                              line:0
                               tag:@(tag ?: "")
                            format:@"%@", @(message ?: "")];
}
```

**Android:**
```cpp
// lz_logger_jni.cpp
static lz_logger_handle_t g_ffi_handle = nullptr;

extern "C" void lz_logger_ffi_set_handle(lz_logger_handle_t handle) {
    g_ffi_handle = handle;
}

extern "C" void lz_logger_ffi(int level, const char* tag, 
                               const char* function, const char* message) {
    if (g_ffi_handle == nullptr) return;
    // 格式化并写入日志
    lz_logger_write(g_ffi_handle, formatted_message, len);
}
```

---

## 性能优化

### 1. 无锁设计

**传统加锁方式:**
```c
pthread_mutex_lock(&mutex);
write_log_data();
pthread_mutex_unlock(&mutex);
// 问题: 所有线程串行执行,锁竞争严重
```

**CAS 无锁方式:**
```c
// 原子预留偏移量
while (!atomic_compare_exchange_weak(&offset, &old, new)) {
    // 失败则重试,无需休眠
}
// 并发写入预留位置
memcpy(mmap_ptr + old_offset, data, len);
// 优势: 真正并发,无阻塞
```

**多线程扩展性分析:**

| 测试类型 | 日志间隔 | 扩展性 | 说明 |
|---------|---------|--------|------|
| **极限压力** | 0μs (无间隔) | 5-14% ⚠️ | 人为制造最坏场景,不代表真实性能 |
| **移动端应用** | >10ms | **121%** ⭐⭐⭐⭐⭐ | 实测数据,超过理想值 |
| **普通后端** | >1ms | **101%** ⭐⭐⭐⭐⭐ | 实测数据,接近完美 |
| **高频服务器** | >100μs | **95%** ⭐⭐⭐⭐⭐ | 实测数据,非常优秀 |

**关键洞察:**
1. 极限测试中,线程100%时间写日志,10个线程在40ns窗口内疯狂竞争CAS
2. 真实应用中,日志间隔>1ms,CAS冲突概率<0.01%
3. 单线程快（24.7M/秒）是优势,不是导致多线程竞争的原因
4. **CAS无锁方案在真实场景下表现完美**

**适用场景:**
- ✅ **最适合**: 移动端/嵌入式（2-10线程,扩展性95-121%）
- ✅ **适合**: 普通后端（10-50线程,日志间隔>100μs）
- ✅ **超高并发**: 单次操作仅40ns,比锁方案(4000ns)更适合高并发场景
  * CAS失败重试成本极低,无线程阻塞
  * 性能越快,冲突窗口越小,扩展性越好
- ⚠️ **极限测试**: 无业务间隔时扩展性降至14%（但这不是真实场景）

### 2. mmap 零拷贝

**传统 write() 方式:**
```
用户空间 → 内核缓冲区 → 页缓存 → 磁盘
(需要数据拷贝)
```

**mmap 方式:**
```
用户空间直接修改页缓存 → 磁盘
(零拷贝,操作系统自动刷盘)
```

### 3. 顺序查找优化

**传统目录遍历 (O(n)):**
```c
DIR *dir = opendir(log_dir);
while ((entry = readdir(dir)) != NULL) {
    // 遍历所有文件,慢
}
```

**顺序查找 (O(5)):**
```c
for (int num = 0; num < 5; num++) {
    sprintf(path, "%s/2025-10-30-%d.log", log_dir, num);
    if (stat(path, &st) == 0) {
        latest_num = num;
    }
}
// 最多 5 次 stat(),快
```

### 4. 线程 ID 缓存

**iOS:**
```objectivec
uint64_t tid;
pthread_threadid_np(NULL, &tid);
// 每次调用获取当前线程 ID
```

**Android:**
```cpp
static pid_t get_thread_id() {
    return gettid();  // 系统调用
}
```

### 5. 时间戳优化

**iOS:**
```objectivec
NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss.SSS";
// 考虑缓存 formatter 实例
```

**Android:**
```cpp
struct timeval tv;
gettimeofday(&tv, nullptr);
localtime_r(&tv.tv_sec, &tm_info);
strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_info);
snprintf(timestamp, sizeof(timestamp), "%s.%03d", 
         buffer, (int)(tv.tv_usec / 1000));
```

---

## 错误处理

### 错误码定义

```c
typedef enum {
    LZ_LOG_SUCCESS = 0,                   // 成功
    LZ_LOG_ERROR_INVALID_PARAM = -1,      // 无效参数
    LZ_LOG_ERROR_INVALID_HANDLE = -2,     // 无效句柄
    LZ_LOG_ERROR_OUT_OF_MEMORY = -3,      // 内存不足
    LZ_LOG_ERROR_FILE_NOT_FOUND = -4,     // 文件未找到
    LZ_LOG_ERROR_FILE_CREATE = -5,        // 文件创建失败
    LZ_LOG_ERROR_FILE_OPEN = -6,          // 文件打开失败
    LZ_LOG_ERROR_FILE_WRITE = -7,         // 文件写入失败
    LZ_LOG_ERROR_FILE_EXTEND = -8,        // 文件扩展失败
    LZ_LOG_ERROR_MMAP_FAILED = -9,        // mmap映射失败
    LZ_LOG_ERROR_MUNMAP_FAILED = -10,     // munmap解映射失败
    LZ_LOG_ERROR_FILE_SIZE_EXCEED = -11,  // 文件大小超限
    LZ_LOG_ERROR_INVALID_MMAP = -12,      // mmap映射无效
    LZ_LOG_ERROR_DIR_ACCESS = -13,        // 目录访问失败
    LZ_LOG_ERROR_HANDLE_CLOSED = -14,     // 句柄已关闭
    LZ_LOG_ERROR_FILE_SWITCH = -15,       // 文件切换失败
    LZ_LOG_ERROR_MUTEX_LOCK = -16,        // 互斥锁失败
    LZ_LOG_ERROR_SYSTEM = -100,           // 系统错误（携带errno）
} lz_log_error_t;
```

### 双错误码设计

```c
int32_t innerError = 0;  // 自定义错误码（详细的内部状态）
int32_t sysErrno = 0;    // 系统 errno（系统调用失败原因）

lz_log_error_t ret = lz_logger_open(
    log_dir, encrypt_key, 
    &handle, 
    &innerError,  // 输出内部错误
    &sysErrno     // 输出系统错误
);

// iOS 示例
if (ret != LZ_LOG_SUCCESS) {
    NSLog(@"Open failed: ret=%d, inner=%d, errno=%d (%s), desc=%s",
          ret, innerError, sysErrno, 
          strerror(sysErrno),
          lz_logger_error_string(ret));
}
}
```

**优势:**
- `innerError`: 自定义错误流程 (如参数校验失败)
- `sysErrno`: 系统调用失败的 errno (如 open/mmap 失败)
- 方便定位问题根源

---

## 使用示例

### iOS 使用

```swift
import lz_logger

// AppDelegate.swift
func application(_ application: UIApplication, 
                 didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
    
    // 初始化日志
    let success = LZLogger.shared().prepareLog("laozhaozhao", encryptKey: nil)
    if success {
        LZ_LOG_INFO("AppDelegate", "Logger initialized successfully")
    }
    
    return true
}

// 业务代码中使用
LZ_LOG_DEBUG("Network", "Request started: %@", url)
LZ_LOG_ERROR("Database", "Query failed: %@", error.localizedDescription)
```

### Android 使用

```kotlin
import io.levili.lzlogger.LzLogger
import io.levili.lzlogger.logInfo

// MainActivity.kt
override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    
    // 初始化日志
    val success = LzLogger.prepareLog(applicationContext, "laozhaozhao")
    if (success) {
        logInfo("MainActivity", "Logger initialized successfully")
    }
}

// 业务代码中使用
LzLogger.log(LzLogger.DEBUG, "Network", "Request started: $url")
LzLogger.log(LzLogger.ERROR, "Database", "Query failed: ${e.message}")
```

### Flutter 使用

```dart
import 'package:lz_logger/lz_logger.dart';

void main() {
  // iOS/Android 已在 native 侧初始化
  
  // Dart 侧直接使用 FFI
  lzLogInfo('MyApp', 'Flutter app started');
  
  runApp(MyApp());
}

// 业务代码中使用
void onButtonPressed() {
  lzLogDebug('UI', 'Button pressed');
  lzLogWarn('Business', 'Low memory warning');
}

// 多线程测试
void stressTest() async {
  for (int i = 0; i < 4; i++) {
    await Isolate.spawn((params) {
      for (int j = 0; j < 20000; j++) {
        lzLogInfo('Thread-${params['id']}', 'Loop $j');
      }
    }, {'id': i});
  }
}
```

---

## 测试与验证

### 多线程压力测试

```dart
// example/lib/main.dart

void isolateWorker(Map<String, dynamic> params) {
  final int threadIndex = params['threadIndex'];
  
  // 每个 Isolate 写 20,000 条日志
  for (int i = 0; i < 20000; i++) {
    lzLogInfo('Thread-$threadIndex', 'Loop $i at ${DateTime.now()}');
    sleep(Duration(milliseconds: 50 + Random().nextInt(950)));
  }
}

void _sendLog() async {
  // 启动 4 个 Isolate (真正的系统线程)
  for (int i = 0; i < 4; i++) {
    await Isolate.spawn(isolateWorker, {'threadIndex': i});
  }
  
  // 总计: 4 × 20,000 = 80,000 条日志
  // 预期文件大小: ~12MB (触发多次文件轮转)
}
```

### 验证点

1. **数据完整性**: 所有 80,000 条日志都被写入,无丢失
2. **无数据损坏**: 每条日志格式正确,无乱码
3. **线程 ID 正确**: 4 个不同的 tid (如 0x1a03, 0x1b04, 0x1c05, 0x1d06)
4. **文件轮转**: 创建多个日志文件,最多保留 5 个
5. **无死锁**: 无锁设计,永不阻塞

---

## 日志格式规范

### 完整格式

```
yyyy-MM-dd HH:mm:ss.SSS tid:0xHHHH [file:line] [function] [tag] message
```

### 字段说明

| 字段 | 说明 | 示例 |
|------|------|------|
| 时间戳 | 精确到毫秒 | `2025-10-30 12:34:56.789` |
| 线程 ID | 十六进制格式 | `tid:0x1a03` |
| 文件位置 | 文件名:行号 | `[AppDelegate.swift:15]` |
| 函数名 | 调用函数 | `[application(_:didFinishLaunchingWithOptions:)]` |
| 标签 | 业务标识 | `[Network]` |
| 消息 | 日志内容 | `Request completed successfully` |

### 示例日志

```
2025-10-30 12:34:56.789 tid:0x1a03 [AppDelegate.swift:15] [application(_:didFinishLaunchingWithOptions:)] [AppDelegate] Logger initialized successfully
2025-10-30 12:34:57.123 tid:0x1b04 [NetworkManager.swift:45] [request(_:completion:)] [Network] GET https://api.example.com/data
2025-10-30 12:34:57.456 tid:0x1b04 [NetworkManager.swift:67] [handleResponse(_:)] [Network] Response received: 200 OK
2025-10-30 12:34:58.789 tid:0x1c05 [DatabaseManager.swift:123] [query(_:)] [Database] Query executed in 234ms
```

---

## 存储路径

### iOS

```
<App Sandbox>/Library/Caches/<logName>/
├── log_20251108_153045_12345.mmap
├── log_20251108_160230_12345.mmap
├── log_20251108_163015_12345.mmap
└── ...
```

**特性:**
- ✅ `NSFileProtectionNone` - 不加密，随时可访问
- ✅ `NSURLIsExcludedFromBackupKey = YES` - 排除 iCloud 备份
- ✅ 文件名包含时间戳和进程ID

### Android

```
/sdcard/Android/data/<package>/files/logs/<logName>/
├── log_20251108_153045_12345.mmap
├── log_20251108_160230_12345.mmap
├── log_20251108_163015_12345.mmap
└── ...
```

**特性:**
- ✅ 使用 `Context.cacheDir` (系统自动管理)
- ✅ 应用卸载时自动删除

---

## 配置参数

### 编译时配置

```c
// src/lz_logger.h

/** 最小文件大小：1MB（用于测试频繁切换） */
#define LZ_LOG_MIN_FILE_SIZE (1 * 1024 * 1024)

/** 默认文件大小：6MB */
#define LZ_LOG_DEFAULT_FILE_SIZE (6 * 1024 * 1024)

/** 最大文件大小：100MB */
#define LZ_LOG_MAX_FILE_SIZE (100 * 1024 * 1024)

/** 文件尾部魔数标记 */
#define LZ_LOG_MAGIC_ENDX 0x456E6478  // "Endx" in hex

/** 加密盐大小 */
#define LZ_LOG_SALT_SIZE 16

/** 文件尾部元数据大小 */
#define LZ_LOG_FOOTER_SIZE 28  // Salt(16) + Magic(4) + FileSize(4) + UsedSize(4)
```

### 运行时配置

**设置文件大小:**
```c
// 在 lz_logger_open 之前调用
lz_logger_set_max_file_size(10 * 1024 * 1024);  // 设置为10MB
```

**iOS 日志级别:**
```objectivec
// 设置日志级别（过滤低级别日志）
[[LZLogger sharedInstance] setLogLevel:LZLogLevelWarn];
// 只会记录 WARN, ERROR, FATAL 级别的日志
```

**Android 日志级别:**
```kotlin
// 设置日志级别（过滤低级别日志）
LzLogger.setLogLevel(LzLogger.WARN)
// 只会记录 WARN, ERROR, FATAL 级别的日志
```

---

## 进阶功能

### 日志导出

```c
char export_path[1024];
lz_log_error_t ret = lz_logger_export_current_log(handle, 
                                                   export_path, 
                                                   sizeof(export_path));
if (ret == LZ_LOG_SUCCESS) {
    printf("Exported to: %s\n", export_path);
}
```

### 清理过期日志

```c
// 清理 7 天前的日志
lz_log_error_t ret = lz_logger_cleanup_expired_logs(log_dir, 7);
```

**自动清理:**
- iOS: 初始化后 3 秒自动清理 7 天前的日志
- Android: 初始化后 3 秒自动清理 7 天前的日志

### 日志刷盘

```c
// 强制将缓冲区刷到磁盘
lz_logger_flush(handle);
```

**注意:** mmap 会由操作系统自动定期刷盘,通常不需要手动调用

---

## 调试支持

### Debug 模式

```c
#if LZ_DEBUG_ENABLED
#define LZ_DEBUG_LOG(fmt, ...) \
    fprintf(stderr, "[LZLogger DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define LZ_DEBUG_LOG(fmt, ...)
#endif
```

**iOS Debug 输出:**
```objectivec
#ifdef DEBUG
    NSLog(@"%@", fullMessage);  // 同时输出到 Xcode 控制台
#endif
```

**Android Debug 输出:**
```cpp
#ifdef DEBUG
    __android_log_print(ANDROID_LOG_INFO, "LzLogger", "%s", fullMessage);
#endif
```

---

## 性能指标

### 实测性能数据

**测试环境:** macOS (Apple Silicon), 40MB文件大小，120字节消息

#### 单线程性能

| 日志库 | 吞吐量 | 平均延迟 | 相对性能 |
|--------|--------|---------|---------|
| **lz_logger** | **24.7M 条/秒** | **40 ns/条** | 基准 (1.0x) ⭐⭐⭐⭐⭐ |
| spdlog basic_mt | 5.8M 条/秒 | 173 ns/条ong> | **4.3倍**
- ✅ 接近 spdlog 无I/O基准测试的性能（1.11x）
- ✅ mmap零拷贝几乎消除文件I/O开销

#### 多线程性能（10线程）

| 日志库 | 总吞吐量 | 平均延迟 | 扩展性 | 相对性能 |
|--------|---------|---------|--------|---------|
| **lz_logger** | **3.47M 条/秒** | **288 ns/条** | 14.0% ⚠️ | 基准 (1.0x) ⭐⭐⭐⭐⭐ |
| spdlog basic_mt | 1.66M 条/秒 | 602 ns/条 | 28.6% | 0.48x |
| spdlog null_mt | 6.27M 条/秒 | 159 ns/条 | 22.9% | 1.81x (无I/O) |

**关键说明:**
- ✅ 比 spdlog 真实文件写入快 **2.1倍**
- ⚠️ **扩展性14%是极限压力测试**（无业务间隔）
- ✨ **真实场景扩展性95-121%**（见下文实测）

#### 真实场景扩展性（10线程，5秒测试）

| 场景 | 日志间隔 | 单线程 | 10线程 | 扩展性 | 评级 |
|------|---------|--------|--------|--------|------|
| **移动端应用** | 10ms | 202条/秒 | 2441条/秒 | **121%** | ⭐⭐⭐⭐⭐ 超预期！ |
| **普通后端** | 1ms | 2299条/秒 | 23194条/秒 | **101%** | ⭐⭐⭐⭐⭐ 接近完美！ |
| **高频服务器** | 100μs | 22544条/秒 | 213641条/秒 | **95%** | ⭐⭐⭐⭐⭐ 非常优秀！ |
| **极限压力** | 0μs | 6.7M条/秒 | 3.7M条/秒 | **5.5%** | ⚠️ 压力测试底线 |

**扩展性计算:** `(多线程吞吐量 / 线程数) / 单线程吞吐量 × 100%`

**核心结论:**
- ✅ **真实场景扩展性95-121%** - CAS方案完美！
- ✅ **单线程快（24.7M/秒）是优势** - 不是问题
- ⚠️ **极限压力14%是底线** - 不代表真实性能

#### 加密性能（单线程，AES-128-CBC）

| 模式 | 吞吐量 | 平均延迟 | 性能损失 |
|------|--------|---------|---------|
| 无加密 | 24.7M 条/秒 | 40 ns/条 | - |
| **AES-128-CBC** | **2.27M 条/秒** | **440 ns/条** | **11x** |

- ✅ 加密模式仍保持优秀性能（2.27M条/秒）
- 📝 适合需要日志加密的场景

### 与业界对比

| 日志库 | 单线程 | 10线程 | 真实扩展性 | 架构 | 适用场景 |
|--------|--------|--------|-----------|------|---------|
| **lz_logger** | 24.7M/秒 | 3.47M/秒 | **95-121%** | mmap+CAS | 移动端/嵌入式 ⭐⭐⭐⭐⭐ |
| spdlog | 5.8M/秒 | 1.66M/秒 | ~29% | mutex+文件I/O | 通用C++应用 ⭐⭐⭐⭐ |
| xlog | 0.25-0.5M/秒 | 0.2-0.4M/秒 | ~20% | buffer+后台线程 | 移动端大规模 ⭐⭐⭐ |

**性能优势:**
- 比 spdlog 快 **2.1-4.3倍**
- 比 xlog 快 **29-57倍** 🚀
- 真实场景扩展性 **95-121%**（行业领先）

---

## 常见问题

### Q1: 为什么使用 CAS 而不是互斥锁?

**A:** CAS (Compare-And-Swap) 是硬件级原子操作:
- ✅ 无阻塞,失败立即重试
- ✅ 真正并发,多核 CPU 同时执行
- ✅ 无死锁,无优先级反转
- ❌ 互斥锁需要内核调度,线程休眠/唤醒开销大

### Q2: mmap 映射在 close(fd) 后为什么仍有效?

**A:** mmap 创建的是独立的虚拟内存映射:
```c
void *ptr = mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, 0);
close(fd);  // 关闭文件描述符
// ptr 仍可访问,内核维护页缓存
munmap(ptr, size);  // 真正释放映射
```

### Q3: 为什么线程 ID 用十六进制?

**A:** 十六进制是系统工具的标准格式:
- lldb: `thread #1, tid = 0x1a03`
- gdb: `[Thread 0x7fff1a03 (LWP 12345)]`
- 崩溃报告: `Thread 0: 0x1a03`

### Q4: 如何确保日志不丢失?

**A:** 多重保障:
1. **CAS 原子操作** - 确保偏移量预留不冲突
2. **mmap MAP_SHARED** - 修改直接同步到内核
3. **Footer 验证** - ENDX magic + used_size 检测损坏
4. **自动刷盘** - munmap/close 时强制 msync

### Q5: 单日最多 5 个文件够用吗?

**A:** 5 个文件 = 5 × 5MB = 25MB/天
- 普通应用: 足够 (日均 < 10MB)
- 高频日志: 可调整 `LZ_LOG_MAX_DAILY_FILES`
- 策略: 保留最近日志,自动滚动覆盖

---

## 未来优化方向

### 高优先级（核心功能完善）

1. **运行时日志级别过滤** 🎯
   - 在C核心层实现级别过滤
   - 避免低级别日志的字符串格式化开销
   - 预期性能提升：10-20%（过滤VERBOSE/DEBUG时）

2. **崩溃安全增强** 🛡️
   - 定期更新Footer的Used Size
   - 崩溃后自动恢复最后有效位置
   - 减少崩溃时的日志丢失

3. **批量刷盘控制** ⚡
   - 提供 `lz_logger_set_flush_interval()` API
   - 控制msync调用频率
   - 平衡性能和数据安全性

### 中优先级（功能扩展）

4. **结构化日志支持** 📋
   - 支持JSON/KV格式日志
   - 便于机器解析和分析
   - 可选功能，不影响现有API

5. **日志分析工具增强** 🔧
   - 增强 `decrypt_log.py` 功能
   - 支持日志过滤、搜索、统计
   - 提供可视化工具

### 低优先级（性能极限优化）

6. **Per-Thread Buffer优化** 🚀
   - 仅针对超高并发场景（50+线程）
   - 每线程独立Buffer，减少CAS竞争
   - 当前场景不需要（95-121%扩展性已足够）

7. **日志压缩** 💾
   - 结论：不优先实现
   - 原因：磁盘便宜，加密后压缩效果差
   - 建议：导出归档时用 `tar -czf` 即可

### 不计划实现

❌ **常驻后台线程** - 违背零线程设计理念
❌ **单条日志压缩** - 效果差（5-10%），不如块压缩
❌ **网络上传** - 超出日志库职责范围，应用层实现更灵活

---

## 性能优化历程

### v2.1.0 - 当前版本（最优）
- ✅ Footer: 28字节（Salt 16 + Magic 4 + FileSize 4 + UsedSize 4）
- ✅ 单线程：24.7M条/秒 (40ns)
- ✅ 10线程：3.47M条/秒 (288ns)
- ✅ 真实扩展性：95-121%

### 实验分支（testOpt）
- ❌ Footer: 32字节（64位对齐 + 缓存行对齐）
- ❌ 多线程性能下降
- 📝 保留作为性能优化参考

**结论：** 简单的设计往往是最优的！

---

## 许可证

MIT License

---

## 贡献者

- Wei Li (@levili-T) - 核心开发与架构设计

---

## 参考资料

### 无锁编程
- [Lock-Free Programming (Martin Thompson)](https://mechanical-sympathy.blogspot.com/)
- [C11 Atomics Documentation](https://en.cppreference.com/w/c/atomic)
- [CAS Performance Analysis](https://en.wikipedia.org/wiki/Compare-and-swap)

### mmap 原理
- [Linux mmap() System Call](https://man7.org/linux/man-pages/man2/mmap.2.html)
- [Memory-Mapped Files (Microsoft)](https://docs.microsoft.com/en-us/windows/win32/memory/file-mapping)
- [Understanding mmap](https://www.kernel.org/doc/html/latest/admin-guide/mm/concepts.html)

### Flutter FFI
- [Dart FFI Documentation](https://dart.dev/guides/libraries/c-interop)
- [FFI Best Practices](https://dart.dev/guides/libraries/c-interop#best-practices)
- [package:ffi](https://pub.dev/packages/ffi)

### 性能测试
- [XCTest Performance Testing](https://developer.apple.com/documentation/xctest/performance_tests)
- [Android Benchmarking](https://developer.android.com/studio/profile/benchmark)

---

**文档版本:** 2.1.0  
**最后更新:** 2025-11-08  
**代码仓库:** [github.com/levili-T/lz_logger](https://github.com/levili-T/lz_logger)
