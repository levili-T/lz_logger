# LZ Logger 解密工具

## 功能说明

这个 Python 脚本用于解密 LZ Logger 加密的日志文件。

### 加密算法

- **算法**: AES-256-CTR (Counter Mode)
- **密钥派生**: PBKDF2-HMAC-SHA256 (10,000 次迭代)
- **特性**: 
  - 流式加密,支持任意长度
  - 支持随机访问解密
  - 加密和解密使用相同操作

### 文件格式

```
[Salt 16字节]
[加密的日志数据 N字节]
[魔数 ENDX 4字节]
[已用大小 4字节]
```

## 安装依赖

```bash
pip install pycryptodome
```

## 使用方法

### 1. 解密单个文件

```bash
# 在命令行指定密码
./decrypt_log.py -f encrypted.log -o decrypted.txt -p mypassword

# 交互式输入密码 (更安全)
./decrypt_log.py -f encrypted.log -o decrypted.txt
请输入解密密码: ********
```

### 2. 批量解密目录

```bash
# 解密目录下所有 .log 文件
./decrypt_log.py -d ./logs -o ./decrypted -p mypassword
```

解密后的文件会以 `原文件名_decrypted.txt` 保存。

### 3. 查看帮助

```bash
./decrypt_log.py -h
```

## 示例

### 解密单个日志文件

```bash
$ ./decrypt_log.py -f 2025-10-31-0.log -o output.txt
请输入解密密码: 
正在读取文件: 2025-10-31-0.log
文件大小: 524288 字节 (已使用: 12345 字节)
盐值: a1b2c3d4e5f6789012345678901234
正在派生密钥...
正在解密...
✅ 解密完成: output.txt
解密后大小: 12345 字节
```

### 批量解密

```bash
$ ./decrypt_log.py -d ./logs -o ./decrypted -p mypassword
找到 5 个日志文件
------------------------------------------------------------

处理: 2025-10-30-0.log
正在读取文件: logs/2025-10-30-0.log
文件大小: 524288 字节 (已使用: 10000 字节)
盐值: a1b2c3d4...
正在派生密钥...
正在解密...
✅ 解密完成: decrypted/2025-10-30-0_decrypted.txt

处理: 2025-10-30-1.log
...

------------------------------------------------------------
完成! 成功解密 5/5 个文件
```

## 常见问题

### Q: 如何获取加密日志?

A: 从手机导出日志文件:
- **Android**: `/sdcard/Android/data/com.klook.shell/files/logs/laozhaozhao/*.log`
- **iOS**: 通过 iTunes 文件共享或 Xcode Devices 窗口导出

### Q: 解密失败怎么办?

A: 检查:
1. 密码是否正确
2. 文件是否完整 (检查文件尾部魔数)
3. 文件是否真的加密了 (没有密码时不会加密)

### Q: 支持哪些平台?

A: Python 3.6+ (Windows/macOS/Linux)

### Q: 解密速度慢怎么办?

A: PBKDF2 需要 10,000 次迭代(安全考虑),首次派生密钥需要几秒钟。
批量解密时密钥会复用,速度会快很多。

## 技术细节

### Counter 值计算

```python
block_number = file_offset // 16  # 每个 AES 块 16 字节
iv = [0] * 8 + block_number.to_bytes(8, 'big')
```

### 文件偏移量

加密数据从文件偏移 16 字节处开始 (前 16 字节是盐值),
所以解密时需要设置 `offset=16`。

### 安全性

- 使用 256-bit 密钥 (AES-256)
- 每个日志文件使用随机盐值
- PBKDF2 防止暴力破解
- CTR 模式防止模式攻击

## License

MIT License
