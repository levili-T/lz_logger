#!/usr/bin/env ruby
# frozen_string_literal: true
#
# LZ Logger 日志解密工具 (Ruby 版本)
# 支持 AES-CTR 加密的日志文件解密
#

require 'openssl'
require 'base64'
require 'io/console' # 用于 getpass
require 'optparse'
require 'pathname'
require 'fileutils'

# --- 常量定义 ---
CRYPTO_KEY_SIZE = 32
CRYPTO_BLOCK_SIZE = 16
CRYPTO_SALT_SIZE = 16
PBKDF2_ITERATIONS = 10000
MAGIC_ENDX = 0x456E6478
FOOTER_SIZE = CRYPTO_SALT_SIZE + 4 + 4 + 4 # 盐16字节 + 魔数4字节 + 文件大小4字节 + 已用大小4字节

# 解包格式:
# 'a16' 是 16 字节的字符串
# 'III' 是三个 Little-Endian 32-bit unsigned integer (UINT32)
FOOTER_FORMAT = 'a16III' # salt, magic, file_size, used_size (Little-Endian)

# --- 核心函数 ---

##
# 从密码派生密钥 (PBKDF2-HMAC-SHA256)
# **兼容性修复:** 使用 OpenSSL::PKCS5.pbkdf2_hmac 显式指定 SHA256 算法。
# @param password [String] 用户密码
# @param salt [String] 16字节盐值
# @return [String] 32字节密钥
#
def derive_key(password, salt)
  OpenSSL::PKCS5.pbkdf2_hmac(
    password,
    salt,
    PBKDF2_ITERATIONS,
    CRYPTO_KEY_SIZE,
    OpenSSL::Digest::SHA256.new # 显式指定 SHA256 算法
  )
end

##
# AES-CTR 解密/加密 (CTR 是对称操作)
# @param key [String] 32字节密钥
# @param data [String] 加密数据
# @param offset [Integer] 文件偏移量 (用于计算 counter)
# @return [String] 解密后的数据
#
def decrypt_aes_ctr(key, data, offset = 0)
  # 计算起始 block number
  block_number = offset / CRYPTO_BLOCK_SIZE
  block_offset = offset % CRYPTO_BLOCK_SIZE

  # 构造 IV (Counter 初始值)
  # Counter = 0(8字节) + BlockNumber(8字节, 大端序)
  
  # 使用 Array#pack 手动实现 8 字节大端序转换 ('Q>' 是 64位无符号长整数, 大端序)
  block_num_bytes = [block_number].pack('Q>')
  iv = "\x00" * 8 + block_num_bytes

  # 创建 AES-CTR cipher
  cipher = OpenSSL::Cipher.new('AES-256-CTR')
  cipher.decrypt
  cipher.key = key

  # 设置 IV/Nonce
  cipher.iv = iv

  # 调整 Counter 状态以跳过块内偏移
  if block_offset > 0
    cipher.update("\x00" * block_offset)
  end

  # 解密数据
  decrypted = cipher.update(data)
  decrypted << cipher.final
  decrypted
end

##
# 读取日志文件
# 文件格式: [数据区域][盐16字节][魔数4字节][文件大小4字节][已用大小4字节]
# @param file_path [String] 文件路径
# @return [Array] [salt, encrypted_data, used_size, file_size_from_footer]
#
def read_log_file(file_path)
  File.open(file_path, 'rb') do |f|
    # 获取文件大小
    file_size = f.size

    raise "文件太小 (#{file_size} 字节), 无法读取 footer" if file_size < FOOTER_SIZE

    # 读取 footer: [盐16字节][魔数4字节][文件大小4字节][已用大小4字节]
    f.seek(file_size - FOOTER_SIZE)
    footer = f.read(FOOTER_SIZE)

    # 解析 footer
    # unpack 使用 FOOTER_FORMAT ('a16III')
    salt, magic, footer_file_size, used_size = footer.unpack(FOOTER_FORMAT)

    if magic != MAGIC_ENDX
      warn "警告: 文件尾部魔数不匹配 (期望 0x#{MAGIC_ENDX.to_s(16).upcase}, 实际 0x#{magic.to_s(16).upcase})"
    end

    # 验证 footer 中的文件大小
    if footer_file_size != file_size
      warn "警告: footer 中文件大小(#{footer_file_size})与实际文件大小(#{file_size})不匹配"
    end

    # 计算数据区域大小
    data_size = file_size - FOOTER_SIZE

    # 读取数据区域
    f.seek(0)
    encrypted_data = f.read(data_size)

    # 只处理已使用的部分
    if used_size > 0 && used_size < encrypted_data.bytesize
      encrypted_data = encrypted_data[0...used_size]
    end

    return salt, encrypted_data, used_size, footer_file_size
  end
end

##
# 移除数据末尾的零字节
# @param data [String] 输入数据
# @return [String] 移除零字节后的数据
#
def remove_trailing_zeros(data)
  data.sub(/\x00+$/, '')
end

##
# 解密日志文件
# @param input_file [String] 输入文件路径
# @param output_file [String] 输出文件路径
# @param password [String] 密码
# @return [Boolean] 成功返回 true, 失败返回 false
#
def decrypt_log_file(input_file, output_file, password)
  puts "正在读取文件: #{input_file}"

  begin
    salt, encrypted_data, used_size, footer_file_size = read_log_file(input_file)
  rescue StandardError => e
    puts "错误: 读取文件失败 - #{e.message}"
    return false
  end

  puts "文件大小: #{encrypted_data.bytesize} 字节 (已使用: #{used_size} 字节, footer 文件大小: #{footer_file_size} 字节)"
  puts "盐值: #{salt.unpack('H*').first}"

  # 派生密钥
  print "正在派生密钥..."
  key = derive_key(password, salt)
  puts " 完成"

  # 解密数据 (数据从文件开头开始,偏移量为0)
  print "正在解密..."
  decrypted_data = decrypt_aes_ctr(key, encrypted_data, 0)
  puts " 完成"

  # 移除尾部填充字节
  decrypted_data = remove_trailing_zeros(decrypted_data)

  # 写入输出文件
  File.open(output_file, 'wb') { |f| f.write(decrypted_data) }

  puts "✅ 解密完成: #{output_file}"
  puts "解密后大小: #{decrypted_data.bytesize} 字节"
  true
rescue StandardError => e
  puts "\n解密过程中出现错误: #{e.message}"
  $stderr.puts e.backtrace.join("\n") if $DEBUG
  false
end

##
# 批量解密目录下的所有 .log 文件
# @param input_dir [String] 输入目录
# @param output_dir [String] 输出目录
# @param password [String] 密码
#
def batch_decrypt(input_dir, output_dir, password)
  input_path = Pathname.new(input_dir)
  output_path = Pathname.new(output_dir)

  unless input_path.exist?
    puts "错误: 输入目录不存在: #{input_dir}"
    return
  end

  # 创建输出目录
  FileUtils.mkdir_p(output_path)

  # 查找所有 .log 文件
  log_files = Dir.glob(input_path.join('*.log')).map { |f| Pathname.new(f) }.sort
  if log_files.empty?
    puts "警告: 目录中没有找到 .log 文件: #{input_dir}"
    return
  end

  puts "找到 #{log_files.size} 个日志文件"
  puts "-" * 60

  success_count = 0
  log_files.each do |log_file|
    output_file = output_path.join("#{log_file.basename('.log')}_decrypted.txt")
    puts "\n处理: #{log_file.basename}"

    if decrypt_log_file(log_file.to_s, output_file.to_s, password)
      success_count += 1
    end
  end

  puts "-" * 60
  puts "完成! 成功解密 #{success_count}/#{log_files.size} 个文件"
end

# --- 主程序 ---

def main
  options = {}
  opt_parser = OptionParser.new do |opts|
    opts.banner = "LZ Logger 日志解密工具\n\n"
    opts.on('-f FILE', '--file FILE', '输入日志文件 (单个文件模式)') { |f| options[:file] = f }
    opts.on('-d DIR', '--dir DIR', '输入日志目录 (批量解密模式)') { |d| options[:dir] = d }
    opts.on('-o OUTPUT', '--output OUTPUT', '输出文件/目录 (批量模式必须指定目录)') { |o| options[:output] = o }
    opts.on('-p PASSWORD', '--password PASSWORD', '解密密码 (不提供则交互式输入)') { |p| options[:password] = p }
    opts.on_tail("-h", "--help", "显示此帮助信息") do
      puts opts
      puts "\n示例:"
      puts " # 解密单个文件"
      puts " #{File.basename($PROGRAM_NAME)} -f encrypted.log -o decrypted.txt -p mypassword"
      puts " "
      puts " # 批量解密目录"
      puts " #{File.basename($PROGRAM_NAME)} -d ./logs -o ./decrypted -p mypassword"
      puts " "
      puts " # 交互式输入密码"
      puts " #{File.basename($PROGRAM_NAME)} -f encrypted.log -o decrypted.txt"
      exit
    end
  end

  begin
    opt_parser.parse!(ARGV)
  rescue OptionParser::InvalidOption, OptionParser::MissingArgument => e
    puts "参数错误: #{e.message}"
    puts opt_parser
    exit 1
  end

  # 检查参数
  if options[:file].nil? && options[:dir].nil?
    puts "错误: 必须指定 -f 或 -d 参数"
    puts opt_parser
    exit 1
  end

  if options[:file] && options[:dir]
    puts "错误: 不能同时指定 -f 和 -d 参数"
    exit 1
  end

  # 批量模式必须指定输出目录
  if options[:dir] && options[:output].nil?
    puts "错误: 批量解密模式 (-d) 必须指定输出目录 (-o)"
    exit 1
  end

  # 获取密码
  password = options[:password]
  if password.nil?
    print "请输入解密密码: "
    password = STDIN.noecho(&:gets).chomp
    puts # 换行
    if password.empty?
      puts "错误: 密码不能为空"
      exit 1
    end
  end

  # 执行解密
  begin
    if options[:file]
      # 单文件解密
      output_file = options[:output]
      if output_file.nil?
        path = Pathname.new(options[:file])
        # 默认输出文件名: 原文件名.decrypt.原扩展名
        output_file = path.sub_ext(".decrypt#{path.extname}")
      end

      exit 1 unless decrypt_log_file(options[:file], output_file.to_s, password)
    else
      # 批量解密
      batch_decrypt(options[:dir], options[:output], password)
    end
  rescue Interrupt
    puts "\n\n操作已取消"
    exit 1
  rescue StandardError => e
    puts "\n错误: #{e.message}"
    puts e.backtrace.join("\n") if $DEBUG
    exit 1
  end
end

main if $PROGRAM_NAME == __FILE__
