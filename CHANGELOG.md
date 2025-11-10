# 更新日志

## v2.1.0 (2025-11)

### 性能优化
- **[重大优化]** 使用 `atomic_fetch_add` 替换 CAS 循环,多线程性能提升 27.4%
  - 单线程性能: 35.7M ops/sec (保持顶级)
  - 多线程性能: 7.5M → 9.6M ops/sec (+27.4%)
  - 平均延迟: 133ns → 104ns (-21.8%)
  - 消除 CAS 重试开销,实现 O(1) 空间预留
- **正确性验证**: 200,000 条日志多线程写入测试通过

### 技术细节
- 原子操作优化: `atomic_compare_exchange_weak` 循环 → `atomic_fetch_add` + `atomic_fetch_sub` 回滚
- 时间复杂度: O(n) → O(1)
- 硬件指令: x86 LOCK XADD, ARM LDADD

---

## v2.0 (2025-11-08)

### 架构重构
- 三层架构设计: C 核心 + 原生平台 + Dart FFI
- 无锁并发写入 (CAS 原子操作)
- mmap 零拷贝技术
- 可选 AES-128-CBC 加密

### 性能基准
- 单线程: 14.5M ops/sec
- 多线程 (5线程): 2.8M ops/sec
- 加密模式: 1.9M ops/sec

### 跨平台支持
- iOS / Android / macOS / Linux
- Flutter 友好: Dart FFI 直接调用

---

## v1.0

### 初始版本
- 基础日志功能
- 文件轮转
- 多级别日志
