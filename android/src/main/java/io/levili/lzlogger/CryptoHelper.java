package io.levili.lzlogger;

import javax.crypto.Cipher;
import javax.crypto.SecretKey;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;
import java.nio.ByteBuffer;
import java.security.SecureRandom;
import java.security.spec.KeySpec;

/**
 * 加密辅助类 - 提供 AES-CTR 加密功能
 * 被 Native 代码通过 JNI 调用
 */
public class CryptoHelper {
    private static final String TAG = "CryptoHelper";
    private static final int KEY_SIZE = 32; // AES-256
    private static final int SALT_SIZE = 16;
    private static final int PBKDF2_ITERATIONS = 10000;

    /**
     * 使用 PBKDF2 从密码派生密钥
     * @param password 密码
     * @param salt 盐值
     * @return 32 字节密钥，失败返回 null
     */
    public static byte[] deriveKey(String password, byte[] salt) {
        try {
            KeySpec spec = new PBEKeySpec(
                password.toCharArray(),
                salt,
                PBKDF2_ITERATIONS,
                KEY_SIZE * 8 // bits
            );
            SecretKeyFactory factory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256");
            return factory.generateSecret(spec).getEncoded();
        } catch (Exception e) {
            android.util.Log.e(TAG, "deriveKey failed", e);
            return null;
        }
    }

    /**
     * 生成随机盐值
     * @return 16 字节盐值，失败返回 null
     */
    public static byte[] generateSalt() {
        try {
            byte[] salt = new byte[SALT_SIZE];
            SecureRandom random = new SecureRandom();
            random.nextBytes(salt);
            return salt;
        } catch (Exception e) {
            android.util.Log.e(TAG, "generateSalt failed", e);
            return null;
        }
    }

    /**
     * AES-CTR 加密/解密
     * @param key 32 字节密钥
     * @param data 待处理数据
     * @param offset 文件偏移量（用于计算 counter）
     * @return 处理后的数据，失败返回 null
     */
    public static byte[] processAesCtr(byte[] key, byte[] data, long offset) {
        try {
            // 计算 counter: offset / 16
            long counter = offset / 16;
            
            // 构建 IV: 前 8 字节为 0，后 8 字节为 counter (big-endian)
            byte[] iv = new byte[16];
            ByteBuffer.wrap(iv, 8, 8).putLong(counter);
            
            SecretKey secretKey = new SecretKeySpec(key, "AES");
            Cipher cipher = Cipher.getInstance("AES/CTR/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, secretKey, new IvParameterSpec(iv));
            
            return cipher.doFinal(data);
        } catch (Exception e) {
            android.util.Log.e(TAG, "processAesCtr failed", e);
            return null;
        }
    }
}
