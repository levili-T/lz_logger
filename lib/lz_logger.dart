import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';

/// Symbolic name of the bundled dynamic library.
const String _libName = 'lz_logger';

/// Lazily loads the platform dependent dynamic library for the logger core.
ffi.DynamicLibrary _load() {
  if (Platform.isIOS || Platform.isMacOS) {
    // iOS uses static framework, symbols are in the executable
    return ffi.DynamicLibrary.process();
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return ffi.DynamicLibrary.open('lib$_libName.so');
  }
  if (Platform.isWindows) {
    return ffi.DynamicLibrary.open('$_libName.dll');
  }
  throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
}

final ffi.DynamicLibrary _dylib = _load();

/// Native FFI function lookup
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

/// iOS Release archive 时函数符号会被 strip，但 __DATA,__objc_const 段的变量不会。
/// 通过 lookup 指针变量 lz_logger_ffi_ptr，读取其值获得函数地址。
_LzLoggerFfiDart _lookupFfi() {
  if (Platform.isIOS) {
    // iOS: lookup 指针变量，读取函数地址
    // lz_logger_ffi_ptr 是一个 void* 指向 lz_logger_ffi 函数
    final ptrToPtr = _dylib.lookup<ffi.Pointer<ffi.Void>>('lz_logger_ffi_ptr');
    final funcAddr = ptrToPtr.value.address;
    return ffi.Pointer<ffi.NativeFunction<_LzLoggerFfiNative>>.fromAddress(funcAddr)
        .asFunction();
  }
  // 其他平台直接 lookup 函数符号
  return _dylib
      .lookup<ffi.NativeFunction<_LzLoggerFfiNative>>('lz_logger_ffi')
      .asFunction();
}

final _LzLoggerFfiDart _lzLoggerFfi = _lookupFfi();

/// Log levels matching iOS LZLogLevel enum
class LzLogLevel {
  static const int verbose = 0;
  static const int debug = 1;
  static const int info = 2;
  static const int warn = 3;
  static const int error = 4;
  static const int fatal = 5;
}

/// Sends a log entry to the shared native logger implementation.
void lzLog({
  required int level,
  required String tag,
  String function = '',
  required String message,
}) {
  // Debug 模式下输出到控制台（可在 VSCode Debug Console 看到）
  // 使用 debugPrint 而非 print，避免长日志被截断和高频日志导致卡顿
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

/// 获取日志级别名称
String _getLevelName(int level) {
  switch (level) {
    case LzLogLevel.verbose:
      return 'VERBOSE';
    case LzLogLevel.debug:
      return 'DEBUG';
    case LzLogLevel.info:
      return 'INFO';
    case LzLogLevel.warn:
      return 'WARN';
    case LzLogLevel.error:
      return 'ERROR';
    case LzLogLevel.fatal:
      return 'FATAL';
    default:
      return 'UNKNOWN';
  }
}

/// Convenience functions for different log levels

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
