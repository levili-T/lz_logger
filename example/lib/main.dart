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

  void _sendLog() async {
    final startTime = DateTime.now();
    
    setState(() {
      _status = 'Starting 4 threads writing logs concurrently...\nStarted at ${startTime.toLocal()}';
    });
    
    // 启动 4 个线程并发写日志
    final futures = List.generate(4, (threadIndex) {
      return Future(() async {
        final random = DateTime.now().millisecondsSinceEpoch % 1000;
        for (int i = 0; i < 10; i++) {
          final timestamp = DateTime.now().toIso8601String();
          
          // 每个线程写不同级别的日志
          switch (threadIndex) {
            case 0:
              lzLogVerbose('Thread-$threadIndex', 'Loop $i at $timestamp');
              break;
            case 1:
              lzLogDebug('Thread-$threadIndex', 'Loop $i at $timestamp');
              break;
            case 2:
              lzLogInfo('Thread-$threadIndex', 'Loop $i at $timestamp');
              break;
            case 3:
              lzLogWarn('Thread-$threadIndex', 'Loop $i at $timestamp');
              break;
          }
          
          // 随机等待 50ms - 1000ms
          final delay = 50 + (random + i * 100) % 950;
          await Future.delayed(Duration(milliseconds: delay));
        }
      });
    });
    
    // 等待所有线程完成
    await Future.wait(futures);
    
    final endTime = DateTime.now();
    final duration = endTime.difference(startTime);
    
    setState(() {
      _status = 'Completed!\n'
          'Started: ${startTime.toLocal()}\n'
          'Finished: ${endTime.toLocal()}\n'
          'Duration: ${duration.inMilliseconds}ms\n'
          '40 logs written (4 threads × 10 loops)';
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
