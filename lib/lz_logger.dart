import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';

/// Symbolic name of the bundled dynamic library.
const String _libName = 'lz_logger';

/// Lazily loads the platform dependent dynamic library for the logger core.
ffi.DynamicLibrary _load() {
  if (Platform.isIOS || Platform.isMacOS) {
    return ffi.DynamicLibrary.open('$_libName.framework/$_libName');
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

final _LzLoggerFfiDart _lzLoggerFfi = _dylib
    .lookup<ffi.NativeFunction<_LzLoggerFfiNative>>('lz_logger_ffi')
    .asFunction();

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
