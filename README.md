### Flutter 插件作为纯 native 依赖集成

#### iOS

即使 lz_logger 是 Flutter 插件，只要其 ios 目录下有 podspec 并声明 native 代码，你可以在纯 native iOS 工程的 Podfile 里这样引用：

```ruby
pod 'lz_logger', :git => 'https://github.com/levili-T/lz_logger.git'
```

这样只会集成 podspec 里声明的 native 代码（如 Classes/、src/），不会自动集成 Dart/Flutter 相关内容。

#### Android

同理，Android 工程可以通过 git submodule、本地 module 或直接依赖 Flutter 插件的 native 产物（如 .so、头文件），只用 native 部分：

1. 通过 git submodule 拉取 lz_logger 仓库。
2. 在 settings.gradle 或 build.gradle 里用 `include ':lz_logger'`，只依赖 native 代码（src/main/cpp、src/main/jniLibs）。
3. 只需在 CMakeLists.txt/ndk-build 里链接 lz_logger 的 .so/头文件，无需集成 Dart/Flutter。

这样可以像普通 native 库一样使用 Flutter 插件的 native 部分。
## Native 工程集成

如果你是纯 native (C/C++/ObjC/Kotlin/Java) 工程，也可以直接集成 lz_logger，方式如下：

### iOS (Objective-C/Swift)

1. 推荐方式：通过 CocoaPods 集成
  - 在 Podfile 中添加：
    ```ruby
    pod 'lz_logger', :git => 'https://github.com/levili-T/lz_logger.git'
    ```
  - 或者将 lz_logger 作为本地 podspec 引入。
  - 集成后可直接 `#import <lz_logger/LZLogger.h>`，按 podspec 说明链接静态库。

2. 手动集成：
  - 直接将 `src/` 和 `ios/Classes/` 下的源码、头文件加入工程，编译为静态库。

### Android (Java/Kotlin)

1. 推荐方式：手动集成 .so 文件和头文件
  - 将编译生成的 `liblz_logger.so` 放入 `app/src/main/jniLibs/` 对应架构目录。
  - 将 `src/lz_logger.h` 头文件放入 `app/src/main/cpp/`。
  - 在 CMakeLists.txt 或 ndk-build 中添加 `lz_logger` 静态/动态库依赖。

2. 也可将 lz_logger 作为本地 module 或 aar 集成。

### 纯 C/C++ 工程

1. 直接引用 `src/` 目录源码或编译生成的静态库。
2. 在 CMakeLists.txt 或 Makefile 中添加 `lz_logger.c`、`lz_crypto.c` 源文件或链接静态库。
3. 头文件引用：
  ```c
  #include "src/lz_logger.h"
  ```

更多 API 见 src/lz_logger.h 注释。
# lz_logger

`lz_logger` 是一个通过 Dart FFI 调用跨平台 C 核心的原生日志插件，支持 iOS 与 Android。日志写入的核心逻辑统一在 `src/lz_logger.c` 中实现，可供 Flutter 与其他原生模块共享。

## Getting Started

1. 将插件添加到 `pubspec.yaml`：

   ```yaml
   dependencies:
     lz_logger:
       path: ../lz_logger
   ```

2. 在 Flutter 端调用：

   ```dart
   import 'package:lz_logger/lz_logger.dart';

   lzLog(
     level: 1,
     tag: 'demo',
     function: 'main',
     message: 'Hello from Flutter',
   );

   // 便捷 API
   lzLogInfo('demo', 'Info message');
   lzLogError('demo', 'Error message', function: 'main');
   ```


3. iOS 与 Android 会各自打包同一个 lz_logger 核心库，其他原生模块可直接链接头文件 `src/lz_logger.h` 暴露的接口。

  - iOS: 实际打包的是静态库（.a），使用 `#import <lz_logger/LZLogger.h>` 并调用 `+[LZLogger logWithLevel:tag:file:line:message:]`。
  - Android: 打包为动态库（.so），使用 `io.levili.lzlogger.LzLogger.log(...)`，内部通过 JNI 转调共享的 C 核心。

    典型用法：
    ```kotlin
    import io.levili.lzlogger.LzLogger
    import io.levili.lzlogger.logInfo

    // 初始化日志系统
    val success = LzLogger.prepareLog(context, "logDir", "encryptKey")
    if (success) {
        logInfo("TAG", "Logger initialized successfully")
        LzLogger.log(LzLogger.ERROR, "TAG", "Error message", "functionName", "fileName.kt", 123)
    }
    ```

## Project structure

项目结构说明：

* `src`: Contains the native source code, and a CmakeFile.txt file for building
  that source code into a dynamic library.

* `lib`: Contains the Dart code that defines the API of the plugin, and which
  calls into the native code using `dart:ffi`.

* platform folders (`android`, `ios`, `windows`, etc.): Contains the build files
  for building and bundling the native code library with the platform application.

## Building and bundling native code

The `pubspec.yaml` specifies FFI plugins as follows:

```yaml
  plugin:
    platforms:
      some_platform:
        ffiPlugin: true
```

This configuration invokes the native build for the various target platforms
and bundles the binaries in Flutter applications using these FFI plugins.

This can be combined with dartPluginClass, such as when FFI is used for the
implementation of one platform in a federated plugin:

```yaml
  plugin:
    implements: some_other_plugin
    platforms:
      some_platform:
        dartPluginClass: SomeClass
        ffiPlugin: true
```

A plugin can have both FFI and method channels:

```yaml
  plugin:
    platforms:
      some_platform:
        pluginClass: SomeName
        ffiPlugin: true
```

The native build systems that are invoked by FFI (and method channel) plugins are:

* For Android: Gradle, which invokes the Android NDK for native builds.
  * See the documentation in android/build.gradle.
* For iOS and MacOS: Xcode, via CocoaPods.
  * See the documentation in ios/lz_logger.podspec.
  * See the documentation in macos/lz_logger.podspec.
* For Linux and Windows: CMake.
  * See the documentation in linux/CMakeLists.txt.
  * See the documentation in windows/CMakeLists.txt.

## Binding to native code

To use the native code, bindings in Dart are needed.
To avoid writing these by hand, they are generated from the header file
(`src/lz_logger.h`) by `package:ffigen`.
Regenerate the bindings by running `dart run ffigen --config ffigen.yaml`.

## Flutter help

For help getting started with Flutter, view our
[online documentation](https://docs.flutter.dev), which offers tutorials,
samples, guidance on mobile development, and a full API reference.

