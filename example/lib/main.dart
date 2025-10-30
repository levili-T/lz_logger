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
  String _status = 'No log sent yet.';

  @override
  void initState() {
    super.initState();
    _sendInitialLog();
  }

  void _sendInitialLog() {
    lzLog(
      level: 0,
      tag: 'example',
      file: 'main.dart',
      line: 24,
      message: 'lz_logger example initialized.',
    );
    _status = 'Initial log dispatched.';
  }

  void _sendLog() {
    lzLog(
      level: 1,
      tag: 'example',
      file: 'main.dart',
      line: 38,
      message: 'Button tapped at ${DateTime.now().toIso8601String()}',
    );
    setState(() {
      _status = 'Last log at ${DateTime.now().toLocal()}';
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
