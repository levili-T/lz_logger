import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'lz_logger_bindings_generated.dart';

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

final LzLoggerBindings _bindings = LzLoggerBindings(_load());

/// Sends a log entry to the shared native logger implementation.
void lzLog({
  required int level,
  required String tag,
  required String file,
  required int line,
  required String message,
}) {
  final ffi.Pointer<ffi.Char> tagPtr = tag.toNativeUtf8().cast();
  final ffi.Pointer<ffi.Char> filePtr = file.toNativeUtf8().cast();
  final ffi.Pointer<ffi.Char> messagePtr = message.toNativeUtf8().cast();

  try {
    _bindings.lz_logger(level, tagPtr, filePtr, line, messagePtr);
  } finally {
    calloc.free(tagPtr);
    calloc.free(filePtr);
    calloc.free(messagePtr);
  }
}
