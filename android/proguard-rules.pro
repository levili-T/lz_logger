# LzLogger ProGuard Rules
# 保护 JNI 调用的类和方法不被混淆

# 保护 LzLogger 类
-keep class io.levili.lzlogger.LzLogger {
    *;
}

# 保护 CryptoHelper 类（被 JNI 调用）
-keep class io.levili.lzlogger.CryptoHelper {
    public static byte[] deriveKey(java.lang.String, byte[]);
    public static byte[] generateSalt();
    public static byte[] processAesCtr(byte[], byte[], long);
}

# 保护所有 native 方法
-keepclasseswithmembernames class * {
    native <methods>;
}
