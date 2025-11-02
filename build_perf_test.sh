#!/bin/bash

echo "🔨 编译 LZ Logger 性能测试工具..."

# 清理旧的编译产物
rm -f performance_test

# 编译（macOS 需要链接 Security 框架）
gcc -o performance_test \
    performance_test.c \
    src/lz_logger.c \
    src/lz_crypto.c \
    -I. \
    -pthread \
    -framework Security \
    -O2 \
    -Wall

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "运行测试："
    echo "  ./performance_test"
    echo ""
else
    echo "❌ 编译失败"
    exit 1
fi
