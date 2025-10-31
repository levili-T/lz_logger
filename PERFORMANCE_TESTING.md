# LzLogger 性能测试

## iOS 性能测试

### 运行方式

1. **使用 Xcode：**
   ```bash
   cd example/ios
   open Runner.xcworkspace
   ```
   - 在 Xcode 中选择 `Product` -> `Test` (⌘+U)
   - 或者在 Test Navigator 中右键 `LzLoggerPerformanceTests` -> `Run`

2. **使用命令行：**
   ```bash
   cd example/ios
   xcodebuild test \
     -workspace Runner.xcworkspace \
     -scheme Runner \
     -destination 'platform=iOS Simulator,name=iPhone 15' \
     -only-testing:RunnerTests/LzLoggerPerformanceTests
   ```

### 查看结果

测试完成后，Xcode 会显示：
- ✅ 每个测试方法是否通过
- ⏱️ 平均执行时间（baseline）
- 📊 性能基线对比（如果有）

**示例输出：**
```
✓ testPerformanceLogWriteWithoutEncryption_ShortMessage (0.125 sec, 5000 iterations)
  Average: 0.025 ms, Relative standard deviation: 2.1%
```

---

## Android 性能测试

### 运行方式

1. **使用命令行：**
   ```bash
   cd example/android
   ./gradlew connectedAndroidTest
   ```

2. **运行特定测试：**
   ```bash
   # 只运行无加密测试
   ./gradlew connectedAndroidTest \
     -Pandroid.testInstrumentationRunnerArguments.class=\
io.levili.lzlogger_example.LzLoggerPerformanceTest#testPerformanceLogWriteWithoutEncryption_ShortMessage
   ```

3. **使用 Android Studio：**
   - 打开 `example/android` 项目
   - 右键 `LzLoggerPerformanceTest` -> `Run`

### 查看结果

测试完成后，查看结果：

1. **命令行输出：**
   ```
   adb logcat -s LzLoggerPerf
   ```

2. **HTML 报告：**
   ```
   open example/android/app/build/reports/androidTests/connected/index.html
   ```

**示例输出：**
```
【无加密 - 41字节】
  迭代次数: 5000
  总耗时: 12.50 ms
  平均耗时: 2.50 μs/条 (2500 ns/条)
  吞吐量: 400000 条/秒
```

---

## 性能指标说明

### 测试场景

| 测试 | 消息大小 | 迭代次数 | 说明 |
|------|---------|---------|------|
| ShortMessage | ~50 字节 | 5000 | 典型的简短日志 |
| MediumMessage | ~150 字节 | 5000 | 包含详细信息的日志 |
| LongMessage | ~300 字节 | 5000 | 包含堆栈跟踪的长日志 |
| BurstWrite | ~25 字节 | 10000 | 极限吞吐量测试 |

### 预期性能基准

**iOS (CommonCrypto):**
- 无加密: ~1-2 μs/条 (500,000 - 1,000,000 条/秒)
- 有加密: ~2-4 μs/条 (250,000 - 500,000 条/秒)

**Android (Java Crypto API):**
- 无加密: ~2-3 μs/条 (333,000 - 500,000 条/秒)
- 有加密: ~4-6 μs/条 (166,000 - 250,000 条/秒)

### 性能影响因素

1. **设备性能：** CPU 速度、内存速度
2. **加密开销：** AES-256-CTR 加密约增加 1-2x 耗时
3. **消息大小：** 更长的消息需要更多时间
4. **文件系统：** mmap 性能取决于存储速度

---

## 性能优化建议

如果实际性能低于预期：

1. **检查设备：** 在真实设备上测试（模拟器较慢）
2. **检查日志级别：** 生产环境应过滤低级别日志
3. **批量写入：** 收集多条日志一次性写入
4. **异步写入：** 在后台线程写入日志

---

## 持续集成

### GitHub Actions 示例

```yaml
- name: Run iOS Performance Tests
  run: |
    cd example/ios
    xcodebuild test \
      -workspace Runner.xcworkspace \
      -scheme Runner \
      -destination 'platform=iOS Simulator,name=iPhone 15' \
      -only-testing:RunnerTests/LzLoggerPerformanceTests

- name: Run Android Performance Tests
  run: |
    cd example/android
    ./gradlew connectedAndroidTest
```

---

## 故障排查

### iOS

**问题：** 测试失败 "Logger open failed"
- 检查沙盒权限
- 检查磁盘空间

**问题：** 性能波动大
- 关闭其他应用
- 运行多次取平均值

### Android

**问题：** 找不到测试类
```bash
./gradlew clean
./gradlew connectedAndroidTest
```

**问题：** 设备未连接
```bash
adb devices
```

---

## 贡献性能测试

添加新的性能测试场景：

1. 在测试类中添加新方法
2. 使用 `measureLogWritePerformance` 或 `measureBlock`
3. 提供有意义的测试名称
4. 更新此 README

---

## 参考资料

- [XCTest Performance Testing](https://developer.apple.com/documentation/xctest/performance_tests)
- [Android Testing Guidelines](https://developer.android.com/training/testing)
