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

// ObjC runtime types for calling [LZLogger ffiPointer]
typedef _ObjcGetClassNative = ffi.Pointer<ffi.Void> Function(ffi.Pointer<ffi.Char>);
typedef _ObjcGetClassDart = ffi.Pointer<ffi.Void> Function(ffi.Pointer<ffi.Char>);
typedef _SelRegisterNameNative = ffi.Pointer<ffi.Void> Function(ffi.Pointer<ffi.Char>);
typedef _SelRegisterNameDart = ffi.Pointer<ffi.Void> Function(ffi.Pointer<ffi.Char>);
typedef _ObjcMsgSendNative = ffi.Pointer<ffi.Void> Function(ffi.Pointer<ffi.Void>, ffi.Pointer<ffi.Void>);
typedef _ObjcMsgSendDart = ffi.Pointer<ffi.Void> Function(ffi.Pointer<ffi.Void>, ffi.Pointer<ffi.Void>);

/// iOS: 通过 ObjC runtime 调用 [LZLogger ffiPointer] 获取函数地址
/// 因为 strip 会移除 C 函数符号，但 ObjC 类方法符号会保留
_LzLoggerFfiDart _lookupFfi() {
  if (Platform.isIOS) {
    // 获取 ObjC runtime 函数
    final objcGetClass = _dylib
        .lookup<ffi.NativeFunction<_ObjcGetClassNative>>('objc_getClass')
        .asFunction<_ObjcGetClassDart>();
    final selRegisterName = _dylib
        .lookup<ffi.NativeFunction<_SelRegisterNameNative>>('sel_registerName')
        .asFunction<_SelRegisterNameDart>();
    final objcMsgSend = _dylib
        .lookup<ffi.NativeFunction<_ObjcMsgSendNative>>('objc_msgSend')
        .asFunction<_ObjcMsgSendDart>();

    // 调用 [LZLogger ffiPointer]
    final classNamePtr = 'LZLogger'.toNativeUtf8();
    final selectorPtr = 'ffiPointer'.toNativeUtf8();
    
    final cls = objcGetClass(classNamePtr.cast());
    final sel = selRegisterName(selectorPtr.cast());
    final funcPtr = objcMsgSend(cls, sel);
    
    calloc.free(classNamePtr);
    calloc.free(selectorPtr);
    
    return ffi.Pointer<ffi.NativeFunction<_LzLoggerFfiNative>>.fromAddress(funcPtr.address)
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
