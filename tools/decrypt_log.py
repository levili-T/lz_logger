#!/usr/bin/env python3
"""
LZ Logger 日志解密工具
支持 AES-CTR 加密的日志文件解密
"""

import sys
import os
import struct
import hashlib
import argparse
from getpass import getpass
from pathlib import Path

try:
    from Crypto.Cipher import AES
    from Crypto.Protocol.KDF import PBKDF2
    from Crypto.Hash import SHA256
except ImportError:
    print("错误: 需要安装 pycryptodome 库")
    print("请运行: pip install pycryptodome")
    sys.exit(1)

# 常量定义
CRYPTO_KEY_SIZE = 32
CRYPTO_BLOCK_SIZE = 16
CRYPTO_SALT_SIZE = 16
PBKDF2_ITERATIONS = 10000
MAGIC_ENDX = 0x456E6478
FOOTER_SIZE = 40  # 盐16字节 + 魔数4字节 + padding4字节 + 文件大小8字节 + 已用大小8字节


def derive_key(password: str, salt: bytes) -> bytes:
    """从密码派生密钥 (PBKDF2-HMAC-SHA256)"""
    return PBKDF2(
        password.encode('utf-8'),
        salt,
        dkLen=CRYPTO_KEY_SIZE,
        count=PBKDF2_ITERATIONS,
        hmac_hash_module=SHA256
    )


def decrypt_aes_ctr(key: bytes, data: bytes, offset: int = 0) -> bytes:
    """
    AES-CTR 解密
    
    Args:
        key: 32字节密钥
        data: 加密数据
        offset: 文件偏移量 (用于计算 counter)
    
    Returns:
        解密后的数据
    """
    # 计算起始 block number
    block_number = offset // CRYPTO_BLOCK_SIZE
    block_offset = offset % CRYPTO_BLOCK_SIZE
    
    # 构造 IV (Counter 初始值)
    # Counter = 0(8字节) + BlockNumber(8字节, 大端序)
    iv = b'\x00' * 8 + block_number.to_bytes(8, 'big')
    
    # 创建 AES-CTR cipher
    cipher = AES.new(key, AES.MODE_CTR, initial_value=iv, nonce=b'')
    
    # 如果有块内偏移,需要先跳过前面的部分
    if block_offset > 0:
        cipher.encrypt(b'\x00' * block_offset)
    
    # 解密数据 (CTR 模式加密和解密是同一个操作)
    return cipher.encrypt(data)


def read_log_file(file_path: str):
    """
    读取日志文件
    文件格式: [数据区域][盐16字节][魔数4字节][padding4字节][文件大小8字节][已用大小8字节]
    
    Returns:
        (salt, encrypted_data, used_size, file_size_from_footer)
    """
    with open(file_path, 'rb') as f:
        # 获取文件大小
        f.seek(0, os.SEEK_END)
        file_size = f.tell()
        
        if file_size < FOOTER_SIZE:
            raise ValueError("文件太小,无法读取footer")
        
        # 读取 footer: [盐16字节][魔数4字节][padding4字节][文件大小8字节][已用大小8字节]
        f.seek(file_size - FOOTER_SIZE)
        footer = f.read(FOOTER_SIZE)
        
        # 解析footer
        salt = footer[:CRYPTO_SALT_SIZE]
        magic, padding = struct.unpack('<II', footer[CRYPTO_SALT_SIZE:CRYPTO_SALT_SIZE+8])
        footer_file_size, used_size = struct.unpack('<QQ', footer[CRYPTO_SALT_SIZE+8:])
        
        if magic != MAGIC_ENDX:
            print(f"警告: 文件尾部魔数不匹配 (期望 0x{MAGIC_ENDX:08X}, 实际 0x{magic:08X})")
            # 不报错,尝试继续
        
        # 验证footer中的文件大小
        if footer_file_size != file_size:
            print(f"警告: footer中文件大小({footer_file_size})与实际文件大小({file_size})不匹配")
        
        # 计算数据区域大小
        data_size = file_size - FOOTER_SIZE
        
        # 读取数据区域
        f.seek(0)
        encrypted_data = f.read(data_size)
        
        # 只处理已使用的部分
        if 0 < used_size < len(encrypted_data):
            encrypted_data = encrypted_data[:used_size]
        
        return salt, encrypted_data, used_size, footer_file_size


def decrypt_log_file(input_file: str, output_file: str, password: str):
    """解密日志文件"""
    print(f"正在读取文件: {input_file}")
    
    try:
        salt, encrypted_data, used_size, footer_file_size = read_log_file(input_file)
    except Exception as e:
        print(f"错误: 读取文件失败 - {e}")
        return False
    
    print(f"文件大小: {len(encrypted_data)} 字节 (已使用: {used_size} 字节, footer文件大小: {footer_file_size} 字节)")
    print(f"盐值: {salt.hex()}")
    
    # 派生密钥
    print("正在派生密钥...")
    key = derive_key(password, salt)
    
    # 解密数据 (数据从文件开头开始,偏移量为0)
    print("正在解密...")
    decrypted_data = decrypt_aes_ctr(key, encrypted_data, offset=0)
    
    # 写入输出文件
    with open(output_file, 'wb') as f:
        f.write(decrypted_data)
    
    print(f"✅ 解密完成: {output_file}")
    print(f"解密后大小: {len(decrypted_data)} 字节")
    return True


def batch_decrypt(input_dir: str, output_dir: str, password: str):
    """批量解密目录下的所有 .log 文件"""
    input_path = Path(input_dir)
    output_path = Path(output_dir)
    
    if not input_path.exists():
        print(f"错误: 输入目录不存在: {input_dir}")
        return
    
    # 创建输出目录
    output_path.mkdir(parents=True, exist_ok=True)
    
    # 查找所有 .log 文件
    log_files = list(input_path.glob("*.log"))
    if not log_files:
        print(f"警告: 目录中没有找到 .log 文件: {input_dir}")
        return
    
    print(f"找到 {len(log_files)} 个日志文件")
    print("-" * 60)
    
    success_count = 0
    for log_file in sorted(log_files):
        output_file = output_path / f"{log_file.stem}_decrypted.txt"
        print(f"\n处理: {log_file.name}")
        
        if decrypt_log_file(str(log_file), str(output_file), password):
            success_count += 1
    
    print("-" * 60)
    print(f"完成! 成功解密 {success_count}/{len(log_files)} 个文件")


def main():
    parser = argparse.ArgumentParser(
        description='LZ Logger 日志解密工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 解密单个文件
  %(prog)s -f encrypted.log -o decrypted.txt -p mypassword
  
  # 批量解密目录
  %(prog)s -d ./logs -o ./decrypted -p mypassword
  
  # 交互式输入密码
  %(prog)s -f encrypted.log -o decrypted.txt
        """
    )
    
    parser.add_argument('-f', '--file', help='输入日志文件')
    parser.add_argument('-d', '--dir', help='输入日志目录 (批量解密)')
    parser.add_argument('-o', '--output', help='输出文件/目录 (单文件模式下可选,默认为原文件名.decrypt.扩展名)')
    parser.add_argument('-p', '--password', help='解密密码 (不提供则交互式输入)')
    
    args = parser.parse_args()
    
    # 检查参数
    if not args.file and not args.dir:
        parser.error("必须指定 -f 或 -d 参数")
    
    if args.file and args.dir:
        parser.error("不能同时指定 -f 和 -d 参数")
    
    # 批量模式必须指定输出目录
    if args.dir and not args.output:
        parser.error("批量解密模式 (-d) 必须指定输出目录 (-o)")
    
    # 获取密码
    password = args.password
    if not password:
        password = getpass("请输入解密密码: ")
        if not password:
            print("错误: 密码不能为空")
            sys.exit(1)
    
    # 执行解密
    try:
        if args.file:
            # 单文件解密
            # 如果未指定输出文件,生成默认输出文件名: 原文件名.decrypt.原扩展名
            output_file = args.output
            if not output_file:
                path = Path(args.file)
                output_file = str(path.parent / f"{path.stem}.decrypt{path.suffix}")
            
            if not decrypt_log_file(args.file, output_file, password):
                sys.exit(1)
        else:
            # 批量解密
            batch_decrypt(args.dir, args.output, password)
    except KeyboardInterrupt:
        print("\n\n操作已取消")
        sys.exit(1)
    except Exception as e:
        print(f"\n错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
