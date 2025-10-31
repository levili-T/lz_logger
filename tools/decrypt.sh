#!/bin/bash
# LZ Logger 快速解密脚本
# 使用固定密钥解密日志文件

set -e

# 固定密钥
PASSWORD="laozhaozhaozaoshangqushangbanxiaozhaozhaoqushangxue"

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DECRYPT_TOOL="$SCRIPT_DIR/decrypt_log.py"

# 检查参数
if [ $# -eq 0 ]; then
    echo "用法: $0 <日志文件路径>"
    echo ""
    echo "示例:"
    echo "  $0 /path/to/2025-10-31-0.log"
    echo ""
    echo "解密后的文件会生成在相同目录下,文件名格式: 原文件名.decrypt.log"
    exit 1
fi

INPUT_FILE="$1"

# 检查输入文件是否存在
if [ ! -f "$INPUT_FILE" ]; then
    echo "错误: 文件不存在: $INPUT_FILE"
    exit 1
fi

# 检查解密工具是否存在
if [ ! -f "$DECRYPT_TOOL" ]; then
    echo "错误: 解密工具不存在: $DECRYPT_TOOL"
    exit 1
fi

# 生成输出文件路径
INPUT_DIR="$(dirname "$INPUT_FILE")"
INPUT_BASENAME="$(basename "$INPUT_FILE")"
INPUT_NAME="${INPUT_BASENAME%.*}"
INPUT_EXT="${INPUT_BASENAME##*.}"

OUTPUT_FILE="$INPUT_DIR/${INPUT_NAME}.decrypt.${INPUT_EXT}"

echo "========================================="
echo "LZ Logger 日志解密"
echo "========================================="
echo "输入文件: $INPUT_FILE"
echo "输出文件: $OUTPUT_FILE"
echo "========================================="
echo ""

# 调用Python解密工具
python3 "$DECRYPT_TOOL" -f "$INPUT_FILE" -o "$OUTPUT_FILE" -p "$PASSWORD"

echo ""
echo "========================================="
echo "解密完成!"
echo "========================================="
