#!/bin/bash
# 测试加密功能的脚本

set -e

echo "=== lz_logger 加密功能测试 ==="
echo ""

# 进入src目录
cd /Users/wei.li/Desktop/workNet/lz_logger/src

echo "1. 准备测试代码..."

# 创建测试目录
TEST_DIR="/tmp/lz_logger_encryption_test"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

echo "2. 创建加密日志..."
cat > test_encrypted.c << 'EOF'
#include "lz_logger.h"
#include <stdio.h>
#include <string.h>

int main() {
    lz_logger_handle_t handle = NULL;
    lz_log_error_t ret;
    int32_t inner_error, sys_errno;
    
    // 打开加密日志
    ret = lz_logger_open("/tmp/lz_logger_encryption_test", 
                          "test_password_123",
                          &handle, 
                          &inner_error, 
                          &sys_errno);
    
    if (ret != LZ_LOG_SUCCESS) {
        printf("Failed to open logger: %d\n", ret);
        return 1;
    }
    
    // 写入测试数据
    const char *messages[] = {
        "Hello, this is encrypted message 1",
        "Another encrypted message with some data: 12345",
        "Third message with special chars: !@#$%^&*()",
        "Final test message to verify encryption works properly"
    };
    
    for (int i = 0; i < 4; i++) {
        ret = lz_logger_write(handle, messages[i], strlen(messages[i]));
        if (ret != LZ_LOG_SUCCESS) {
            printf("Failed to write message %d: %d\n", i, ret);
            lz_logger_close(handle);
            return 1;
        }
    }
    
    printf("Successfully wrote %d encrypted messages\n", 4);
    
    lz_logger_close(handle);
    return 0;
}
EOF

gcc test_encrypted.c lz_logger.c lz_crypto.c -o test_write \
    -I. -DDEBUG_ENABLED=1 -std=c11 -framework Security -lpthread

./test_write
echo "   ✓ 日志写入成功"
echo ""

# 检查加密效果
echo "3. 验证文件已加密..."
LOG_FILE=$(ls -t "$TEST_DIR"/*.log | head -1)
echo "   日志文件: $LOG_FILE"

# 检查文件中是否包含明文(不应该包含)
if grep -q "Hello, this is encrypted" "$LOG_FILE" 2>/dev/null; then
    echo "   ✗ 错误: 文件包含明文,加密失败!"
    exit 1
else
    echo "   ✓ 文件已加密(不包含明文)"
fi

# 显示文件头(盐)
echo ""
echo "4. 文件头部(盐):"
xxd -l 32 "$LOG_FILE"
echo ""

# 使用Python脚本解密
echo "5. 解密日志文件..."
cd /Users/wei.li/Desktop/workNet/lz_logger/tools

python3 decrypt_log.py -f "$LOG_FILE" -o "$TEST_DIR/decrypted.log" -p "test_password_123"

echo ""
echo "6. 验证解密结果..."
if [ -f "$TEST_DIR/decrypted.log" ]; then
    echo "   解密后的内容:"
    cat "$TEST_DIR/decrypted.log"
    echo ""
    
    # 验证解密内容
    if grep -q "Hello, this is encrypted message 1" "$TEST_DIR/decrypted.log"; then
        echo "   ✓ 解密成功!内容正确"
    else
        echo "   ✗ 解密内容不匹配"
        exit 1
    fi
else
    echo "   ✗ 解密文件未生成"
    exit 1
fi

echo ""
echo "=== 所有测试通过! ==="
echo ""
echo "清理测试文件..."
rm -rf "$TEST_DIR"
rm -f /Users/wei.li/Desktop/workNet/lz_logger/src/test_encrypted.c
rm -f /Users/wei.li/Desktop/workNet/lz_logger/src/test_write
rm -f /Users/wei.li/Desktop/workNet/lz_logger/src/test_encryption

echo "完成!"
