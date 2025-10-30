import 'package:flutter/material.dart';
import 'package:lz_logger/lz_logger.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  String _status = 'Logger initialized via native code.\nWaiting for user action...';

  @override
  void initState() {
    super.initState();
    _sendInitialLog();
  }

  void _sendInitialLog() {
    // 使用 Dart FFI 发送日志 (iOS 和 Android 已在 native 侧初始化)
    lzLogInfo('DartMain', 'Flutter UI initialized, using Dart FFI');
    setState(() {
      _status = 'Initial Dart FFI log sent.\nTap button to send more logs.';
    });
  }

  void _sendLog() {
    final timestamp = DateTime.now().toIso8601String();
    
    // 测试不同级别的日志
    lzLogVerbose('ButtonTap', 'Verbose log at $timestamp');
    lzLogDebug('ButtonTap', 'Debug log at $timestamp');
    lzLogInfo('ButtonTap', 'Info log at $timestamp');
    lzLogWarn('ButtonTap', 'Warning log at $timestamp');
    lzLogError('ButtonTap', 'Error log at $timestamp');
    
    setState(() {
      _status = 'Last log burst at ${DateTime.now().toLocal()}\n5 levels logged (VERBOSE to ERROR)';
    });
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('Native Packages')),
        body: Center(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Text(
                  'Tap the button to emit a log entry using the shared C logger.',
                  textAlign: TextAlign.center,
                ),
                const SizedBox(height: 16),
                Text(_status, textAlign: TextAlign.center),
                const SizedBox(height: 24),
                ElevatedButton(
                  onPressed: _sendLog,
                  child: const Text('Send Log'),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
