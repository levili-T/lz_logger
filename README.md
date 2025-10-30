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
     file: 'main.dart',
     line: 42,
     message: 'Hello from Flutter',
   );
   ```

3. iOS 与 Android 会各自打包同一个 `lz_logger` 动态库，其他原生模块可直接链接头文件 `src/lz_logger.h` 暴露的接口。

  - iOS: 使用 `#import <lz_logger/LZLogger.h>` 并调用 `+[LZLogger logWithLevel:tag:file:line:message:]`。
  - Android: 使用 `com.example.lz_logger.LzLogger.log(...)`，内部通过 JNI 转调共享的 C 核心。

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

