## Flutter 插件作为纯 native 依赖集成

即使 lz_logger 是 Flutter 插件，你也可以在纯原生项目中只使用其 native 部分。

### iOS

在纯 native iOS 工程的 Podfile 里这样引用：

```ruby
pod 'lz_logger', :git => 'https://github.com/levili-T/lz_logger.git'
```

这样只会集成 podspec 里声明的 native 代码（Classes/、src/），不会自动集成 Dart/Flutter 相关内容。

### Android

Android 工程可以通过以下方式集成：

1. **Git Submodule 方式**：
   ```bash
   git submodule add https://github.com/levili-T/lz_logger.git
   ```

2. **在 settings.gradle 中添加**：
   ```gradle
   include ':lz_logger'
   project(':lz_logger').projectDir = new File('lz_logger/android')
   ```

3. **在 app/build.gradle 中依赖**：
   ```gradle
   dependencies {
       implementation project(':lz_logger')
   }
   ```

这样只会集成 native 代码（src/main/cpp、Kotlin 类），无需集成 Dart/Flutter 环境。
## Native 工程集成

如果你是纯 native (C/C++/ObjC/Kotlin/Java) 工程，也可以直接集成 lz_logger，方式如下：

### iOS (Objective-C/Swift)

1. **推荐方式：通过 CocoaPods 集成**
   
   在 Podfile 中添加：
   ```ruby
   pod 'lz_logger', :git => 'https://github.com/levili-T/lz_logger.git'
   ```
   
   然后执行 `pod install`，即可使用：
   ```objc
   #import <lz_logger/LZLogger.h>
   
   [[LZLogger sharedInstance] setLogLevel:LZLogLevelDebug];
   [[LZLogger sharedInstance] prepareLog:@"MyLogs" encryptKey:nil];
   LZ_LOG_INFO(@"TAG", @"Hello from native iOS");
   ```

2. **手动集成**
   
   - 将 `src/` 和 `ios/Classes/` 下的源码、头文件加入工程
   - 编译为静态库或直接集成到项目中
   - 在构建设置中链接必要的系统框架（Foundation, UIKit）

### Android (Java/Kotlin)

1. **推荐方式：Gradle 模块依赖**
   
   在 `settings.gradle` 中添加：
   ```gradle
   include ':lz_logger'
   project(':lz_logger').projectDir = new File('../lz_logger/android')
   ```
   
   在 `app/build.gradle` 中添加：
   ```gradle
   dependencies {
       implementation project(':lz_logger')
   }
   ```
   
   然后即可使用：
   ```kotlin
   import io.levili.lzlogger.LzLogger
   import io.levili.lzlogger.*
   
   LzLogger.prepareLog(context, "MyLogs", null)
   logInfo("TAG", "Hello from native Android")
   ```

2. **手动集成 .so 文件**
   
   - 将编译生成的 `liblz_logger.so` 放入 `app/src/main/jniLibs/` 对应架构目录
   - 将 `src/lz_logger.h` 头文件放入 `app/src/main/cpp/`
   - 在 CMakeLists.txt 或 ndk-build 中添加 `lz_logger` 库依赖

### 纯 C/C++ 工程

1. 直接引用 `src/` 目录源码或编译生成的静态库
2. 在 CMakeLists.txt 或 Makefile 中添加源文件：
   
   ```cmake
   add_library(lz_logger STATIC
       src/lz_logger.c
       src/lz_crypto.c
   )
   
   target_include_directories(lz_logger PUBLIC src)
   ```

3. 在代码中使用：
   
   ```c
   #include "lz_logger.h"
   
   lz_logger_handle_t handle;
   int32_t inner_error, sys_errno;
   
   lz_log_error_t ret = lz_logger_open(
       "/path/to/logs",
       "encrypt-key",  // 可传 NULL
       &handle,
       &inner_error,
       &sys_errno
   );
   
   if (ret == LZ_LOG_SUCCESS) {
       const char* log_msg = "Hello from C\n";
       lz_logger_write(handle, log_msg, strlen(log_msg));
       lz_logger_close(handle);
   }
   ```

更多 C API 详见 `src/lz_logger.h` 注释。
# lz_logger

`lz_logger` 是一个通过 Dart FFI 调用跨平台 C 核心的原生日志插件，支持 iOS 与 Android。日志写入的核心逻辑统一在 `src/lz_logger.c` 中实现，可供 Flutter 与其他原生模块共享。

## 日志级别

所有平台统一使用以下日志级别：

| 级别 | 值 | 说明 |
|------|-----|------|
| VERBOSE | 0 | 详细日志 |
| DEBUG | 1 | 调试日志 |
| INFO | 2 | 信息日志（默认级别）|
| WARN | 3 | 警告日志 |
| ERROR | 4 | 错误日志 |
| FATAL | 5 | 致命错误日志 |

**注意**：日志系统只会记录 **大于等于当前设置级别** 的日志。

## Getting Started

### Flutter 集成

1. 将插件添加到 `pubspec.yaml`：

   ```yaml
   dependencies:
     lz_logger:
       path: ../lz_logger
   ```

2. 在 Flutter 端初始化和使用：

   ```dart
   import 'package:lz_logger/lz_logger.dart';

   // 基础 API
   lzLog(
     level: LzLogLevel.info,
     tag: 'demo',
     function: 'main',
     message: 'Hello from Flutter',
   );

   // 便捷 API（推荐）
   lzLogVerbose('demo', 'Verbose message');
   lzLogDebug('demo', 'Debug message', function: 'myFunction');
   lzLogInfo('demo', 'Info message');
   lzLogWarn('demo', 'Warning message');
   lzLogError('demo', 'Error message', function: 'main');
   lzLogFatal('demo', 'Fatal error');
   ```

3. **重要**：日志系统需要在原生层初始化后才能使用。请在各平台原生代码中调用初始化方法。

### iOS 原生集成

1. 在 iOS 项目中初始化日志系统（通常在 AppDelegate 中）：

   ```objc
   #import <lz_logger/LZLogger.h>

   - (BOOL)application:(UIApplication *)application 
       didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
       
       // （可选）设置日志级别 - 必须在 prepareLog 之前调用
       [[LZLogger sharedInstance] setLogLevel:LZLogLevelDebug];
       
       // 初始化日志系统
       BOOL success = [[LZLogger sharedInstance] prepareLog:@"MyAppLogs" 
                                                  encryptKey:@"your-encrypt-key"];
       if (success) {
           LZ_LOG_INFO(@"App", @"Logger initialized successfully");
       }
       
       return YES;
   }
   ```

2. 在代码中使用日志宏（推荐）：

   ```objc
   LZ_LOG_VERBOSE(@"TAG", @"Verbose message");
   LZ_LOG_DEBUG(@"TAG", @"Debug message with param: %d", 123);
   LZ_LOG_INFO(@"TAG", @"Info message");
   LZ_LOG_WARN(@"TAG", @"Warning message");
   LZ_LOG_ERROR(@"TAG", @"Error: %@", error);
   LZ_LOG_FATAL(@"TAG", @"Fatal error!");
   ```

3. 或直接调用方法：

   ```objc
   [[LZLogger sharedInstance] log:LZLogLevelInfo
                              file:__FILE__
                          function:__FUNCTION__
                              line:__LINE__
                               tag:@"TAG"
                            format:@"Message with value: %d", value];
   ```

4. 其他功能：

   ```objc
   // 同步日志到磁盘
   [[LZLogger sharedInstance] flush];
   
   // 导出当前日志文件
   NSString *exportPath = [[LZLogger sharedInstance] exportCurrentLog];
   
   // 清理 7 天前的日志
   [[LZLogger sharedInstance] cleanupExpiredLogs:7];
   
   // 关闭日志系统（通常不需要手动调用，系统会自动处理）
   [[LZLogger sharedInstance] close];
   ```

### Android 原生集成

1. 在 Android 项目中初始化（通常在 Application 类中）：

   ```kotlin
   import io.levili.lzlogger.LzLogger
   import io.levili.lzlogger.*

   class MyApplication : Application() {
       override fun onCreate() {
           super.onCreate()
           
           // （可选）设置日志级别 - 必须在 prepareLog 之前调用
           LzLogger.setLogLevel(LzLogger.DEBUG)
           
           // 初始化日志系统
           val success = LzLogger.prepareLog(
               context = this,
               logName = "MyAppLogs",
               encryptKey = "your-encrypt-key"  // 可选，传 null 则不加密
           )
           
           if (success) {
               logInfo("App", "Logger initialized successfully")
           }
       }
   }
   ```

2. 在代码中使用便捷函数（推荐）：

   ```kotlin
   import io.levili.lzlogger.*

   logVerbose("TAG", "Verbose message")
   logDebug("TAG", "Debug message")
   logInfo("TAG", "Info message")
   logWarn("TAG", "Warning message")
   logError("TAG", "Error message: ${error.message}")
   logFatal("TAG", "Fatal error!")
   ```

3. 或使用完整 API：

   ```kotlin
   LzLogger.log(
       level = LzLogger.INFO,
       tag = "TAG",
       message = "Message text",
       function = "myFunction",  // 可选
       file = "MyFile.kt",       // 可选
       line = 123                // 可选
   )
   ```

4. 其他功能：

   ```kotlin
   // 同步日志到磁盘
   LzLogger.flush()
   
   // 导出当前日志文件
   val exportPath = LzLogger.exportCurrentLog()
   
   // 清理 7 天前的日志
   LzLogger.cleanupExpiredLogs(7)
   
   // 关闭日志系统（通常不需要手动调用）
   LzLogger.close()
   ```

## Project Structure

项目结构说明：

* **`src/`**: 核心 C 代码实现
  - `lz_logger.c/h`: 日志系统核心实现
  - `lz_crypto.c/h`: 加密功能实现
  - `CMakeLists.txt`: 用于构建动态库

* **`lib/`**: Dart FFI 封装代码
  - `lz_logger.dart`: Dart API 定义，通过 `dart:ffi` 调用 native 代码
  - `lz_logger_bindings_generated.dart`: FFI 绑定（通过 ffigen 自动生成）

* **`ios/`**: iOS 平台封装
  - `Classes/LZLogger.h/m`: Objective-C 封装层
  - `lz_logger.podspec`: CocoaPods 配置

* **`android/`**: Android 平台封装
  - `src/main/cpp/lz_logger_jni.cpp`: JNI 桥接层
  - `src/main/kotlin/.../LzLogger.kt`: Kotlin API 封装
  - `build.gradle`: Android 构建配置

* **`example/`**: 示例应用

* **`tools/`**: 辅助工具
  - `decrypt_log.py`: 日志解密工具
  - `decrypt.sh`: 解密脚本

## Building and Bundling Native Code

lz_logger 在 `pubspec.yaml` 中声明为 FFI 插件：

```yaml
plugin:
  platforms:
    android:
      ffiPlugin: true
    ios:
      ffiPlugin: true
```

这会自动触发各平台的 native 构建系统：

* **Android**: 使用 Gradle + Android NDK
  - 构建配置：`android/build.gradle`
  - CMake 配置：`android/src/main/cpp/CMakeLists.txt`
  - 输出：`liblz_logger.so`

* **iOS/macOS**: 使用 Xcode + CocoaPods
  - Pod 配置：`ios/lz_logger.podspec`
  - 输出：静态框架（.a）

* **Linux/Windows**: 使用 CMake（如需支持可自行配置）

构建产物会自动打包到 Flutter 应用中。

## Binding to Native Code

Dart FFI 绑定通过 `package:ffigen` 从 C 头文件自动生成，避免手写绑定代码。

**重新生成绑定**（当修改 `src/lz_logger.h` 后）：

```bash
dart run ffigen --config ffigen.yaml
```

这会更新 `lib/lz_logger_bindings_generated.dart` 文件。

配置文件 `ffigen.yaml` 定义了：
- 头文件路径
- 要生成的函数/结构体
- 输出文件路径

## Features

- ✅ **跨平台统一 C 核心**：iOS、Android 共享同一份日志实现
- ✅ **高性能**：mmap 映射、零拷贝、批量刷新
- ✅ **日志加密**：支持 AES-256-CBC 加密（可选）
- ✅ **日志分级**：6 个级别，可动态设置过滤级别
- ✅ **自动清理**：支持按天数清理过期日志
- ✅ **日志导出**：支持导出当前日志到指定路径
- ✅ **线程安全**：内部使用互斥锁保护
- ✅ **自动轮转**：单文件达到限制自动创建新文件
- ✅ **Dart FFI**：Flutter 可直接调用 native 性能
- ✅ **原生友好**：提供 Objective-C、Kotlin、C API

## Performance

经过三轮优化，Android JNI 层性能提升约 **38%**：

- 消除动态内存分配（std::string）
- 优化字符串操作（减少 strlen 调用）
- 使用 snprintf 返回值避免重复扫描
- 缓冲区大小宏定义，便于调整
- 长消息截断而非丢弃

详见 `OPTIMIZATION_SUMMARY.md`。

## Log Format

所有平台统一的日志格式：

```
yyyy-MM-dd HH:mm:ss.SSS T:threadid [location] [function] [tag] message
```

示例：
```
2025-11-02 15:30:45.123 T:1a2b3c [MainActivity.kt:45] [onCreate] [App] Application started
2025-11-02 15:30:45.456 T:1a2b3c [MyFile.m:89] [NetworkManager] Network request completed
```

- **时间戳**：毫秒精度
- **线程 ID**：十六进制格式（T: 前缀）
- **位置**：文件名:行号（行号为 0 时只显示文件名）
- **函数**：函数名（可选，为空时省略此字段）
- **标签**：自定义标签
- **消息**：日志内容

## Log File Location

### iOS
```
/var/mobile/Containers/Data/Application/<UUID>/Library/Caches/<logName>/
```

### Android
```
/sdcard/Android/data/<package>/files/logs/<logName>/
```

日志文件命名格式：`log_YYYYMMDD_HHMMSS_pid.mmap`

## Decryption Tools

如果启用了日志加密，可使用提供的工具解密：

```bash
# 使用 Python 脚本
cd tools
python3 decrypt_log.py /path/to/encrypted.log your-encrypt-key

# 或使用 Shell 脚本
./tools/decrypt.sh /path/to/encrypted.log your-encrypt-key
```

详见 `tools/README.md`。

## API Reference

### Dart API

```dart
// 基础 API
void lzLog({
  required int level,      // 日志级别 (0-5)
  required String tag,     // 标签
  String function = '',    // 函数名（可选）
  required String message, // 消息
});

// 便捷 API
void lzLogVerbose(String tag, String message, {String function = ''});
void lzLogDebug(String tag, String message, {String function = ''});
void lzLogInfo(String tag, String message, {String function = ''});
void lzLogWarn(String tag, String message, {String function = ''});
void lzLogError(String tag, String message, {String function = ''});
void lzLogFatal(String tag, String message, {String function = ''});
```

### iOS API

详见 `ios/Classes/LZLogger.h`

### Android API

详见 `android/src/main/kotlin/.../LzLogger.kt`

### C API

详见 `src/lz_logger.h`

## Contributing

欢迎提交 Issue 和 Pull Request！

## License

详见 `LICENSE` 文件。

## Contributing

欢迎提交 Issue 和 Pull Request！

## License

详见 `LICENSE` 文件。
