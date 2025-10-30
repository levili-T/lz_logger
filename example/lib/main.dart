import 'dart:io';
import 'dart:isolate';
import 'package:flutter/material.dart';
import 'package:lz_logger/lz_logger.dart';

void main() {
  runApp(const MyApp());
}

// Isolate 工作函数
void isolateWorker(Map<String, dynamic> params) {
  final int threadIndex = params['threadIndex'];
  final int seed = params['seed'];
  
  for (int i = 0; i < 20000; i++) {
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
    final delay = 50 + (seed + i * 100) % 950;
    sleep(Duration(milliseconds: delay));
  }
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
      _status = 'Starting 4 isolates (real threads) writing logs...\nStarted at ${startTime.toLocal()}';
    });
    
    // 启动 4 个 Isolate (真正的系统线程)
    final isolates = <Isolate>[];
    try {
      for (int i = 0; i < 4; i++) {
        final isolate = await Isolate.spawn(
          isolateWorker,
          {
            'threadIndex': i,
            'seed': DateTime.now().millisecondsSinceEpoch % 1000,
          },
        );
        isolates.add(isolate);
      }
      
      // 等待一段时间让 isolates 完成工作
      // (每个 isolate 最多运行 20000 * 1000ms ≈ 5.5小时，实际会更快)
      await Future.delayed(const Duration(minutes: 30));
      
    } finally {
      // 清理所有 isolates
      for (final isolate in isolates) {
        isolate.kill(priority: Isolate.immediate);
      }
    }
    
    final endTime = DateTime.now();
    final duration = endTime.difference(startTime);
    
    setState(() {
      _status = 'Completed!\n'
          'Started: ${startTime.toLocal()}\n'
          'Finished: ${endTime.toLocal()}\n'
          'Duration: ${duration.inSeconds}s\n'
          '80,000 logs written (4 isolates × 20,000 loops)\n'
          'Each isolate runs on separate OS thread';
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
